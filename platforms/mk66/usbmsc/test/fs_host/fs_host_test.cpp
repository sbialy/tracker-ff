// Simulator rows S3 (and the gate half of S2) of ref/usb-mass-storage-feasibility.md on the real card code: platforms/mk66
// FileSystemImpl.cpp, 3rdparty/SD/SD.cpp, FatFs (ff.c, diskio.c, fsl_sd_disk.c) compiled for the Mac with -DFEATURE_USB_MSC. Only the NXP
// SD driver is replaced (shim/fsl_sd.h, fake_sd.cpp: the card is a disk image file). Host only, NOT firmware. Driven by s3_fat_roundtrip.sh.
//
//   fs_host_test IMAGE SRC HOSTCMD
//     IMAGE    a FAT32 image formatted by macOS (newfs_msdos)
//     SRC      the computer's files: project_host.mt, stream_b_new.wav, Copied/ (compared byte for byte after the remount)
//     HOSTCMD  run while the computer owns the card: macOS mounts IMAGE (hdiutil + msdosfs) and changes it
//
// 1. the firmware writes a project, two stream files and keeps a file open for writing, opens the streams with fast seek and reads
//    their first block through readSectorsAsync, as sample streaming does
// 2. lendToHost: every FileSystem call fails with its failure value and not one block reaches the card (also FatFs called directly),
//    the drive's own block calls work within the card's bounds only
// 3. HOSTCMD: the computer deletes stream A, replaces stream B, replaces /Workspace/project.mt, copies a folder
// 4. remount: handles of the old mount are dead, stream A cannot be bound again, stream B binds again and streams the new content,
//    the project and the copied folder read back equal to the computer's files, the open file's data reached the card before step 3
#include "FileSystemImpl.h"
#include "fsl_sd_disk.h"
#include "msc_drive.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern const char* g_imagePath;
extern unsigned g_reads, g_writes, g_asyncReads, g_hostReads, g_hostWrites;
extern bool g_hostIo;
extern int g_fakeCardOut;

static int s_fail, s_checks;
#define CHECK(c, ...)                                                                                                          \
    do                                                                                                                         \
    {                                                                                                                          \
        s_checks++;                                                                                                            \
        if (!(c))                                                                                                              \
        {                                                                                                                      \
            s_fail++;                                                                                                          \
            printf("FAIL  %s:%d %s: ", __FILE__, __LINE__, #c);                                                                \
            printf(__VA_ARGS__);                                                                                               \
            printf("\n");                                                                                                      \
        }                                                                                                                      \
    } while (0)

// what the platform provides around FileSystemImpl
unsigned g_takeBacks;
extern "C" void ffUsbMscTakeBack(void) { g_takeBacks++; }
void _debuglog(const char* msg) { printf("log: %s\n", msg); }
void _debuglogf(const char* format, ...)
{
    va_list a;
    va_start(a, format);
    printf("log: ");
    vprintf(format, a);
    printf("\n");
    va_end(a);
}
class FakeTimer : public Timer
{
  public:
    uint32_t millis() override { return ms += 10; }
    uint32_t micros() override { return ms * 1000; }
    void delay(int d) override { ms += uint32_t(d); }
    uint32_t clockCycles() override { return 0; }
    uint32_t clockCyclesPerUs() override { return 180; }
    uint32_t ms = 0;
};

static std::vector<uint8_t> pattern(size_t n, unsigned mul)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) v[i] = uint8_t(i * mul + (i >> 9));
    return v;
}
static std::vector<uint8_t> macFile(const std::string& p)
{
    std::vector<uint8_t> v;
    if (FILE* f = fopen(p.c_str(), "rb"))
    {
        uint8_t b[4096];
        size_t n;
        while ((n = fread(b, 1, sizeof b, f)) > 0) v.insert(v.end(), b, b + n);
        fclose(f);
    }
    return v;
}
static bool put(FileSystemImpl& fs, const char* path, const std::vector<uint8_t>& d)
{
    auto [ok, h] = fs.openFile(path, FileSystem::kWriteCreateNew);
    if (!ok) return false;
    bool w = fs.writeFile(d.data(), d.size(), h) == int32_t(d.size());
    fs.closeFile(h);
    return w;
}
static std::vector<uint8_t> get(FileSystemImpl& fs, const char* path)
{
    std::vector<uint8_t> v;
    auto [ok, h] = fs.openFile(path, FileSystem::kRead);
    if (!ok) return v;
    v.resize(fs.getFileSize(h));
    if (fs.readFile(v.data(), v.size(), h) != int32_t(v.size())) v.clear();
    fs.closeFile(h);
    return v;
}
// a stream as VoiceStreamManager binds one: open, fast seek table, first block straight from the card sectors
static bool bindStream(FileSystemImpl& fs, const char* path, FileSystem::FileHandle* h, std::vector<uint8_t>& first)
{
    static uint32_t clmt[2][64];
    static int k;
    auto [ok, hh] = fs.openFile(path, FileSystem::kRead);
    *h = hh;
    if (!ok) return false;
    uint32_t dbSector = 0, spc = 0;
    uint32_t* t = clmt[k++ & 1];
    if (!fs.enableFastSeek(hh, t, 64, &dbSector, &spc)) return false;
    // the first fragment of the link map (FatFs: t[0] size, then cluster count and first cluster pairs), as
    // VoiceStreamManager turns a file offset into a card sector: database + (cluster - 2) * sectors per cluster
    if (t[1] == 0 || t[2] < 2) return false;
    first.assign(512, 0);
    if (!fs.readSectorsAsync(first.data(), dbSector + (t[2] - 2) * spc, 1)) return false;
    return fs.waitAsyncRead();
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: fs_host_test IMAGE SRC HOSTCMD\n");
        return 2;
    }
    g_imagePath = argv[1];
    const std::string src = argv[2];
    const char* hostCmd = argv[3];
    FakeTimer timer;
    static FileSystemImpl fs;

    // 1. the firmware owns the card
    fs.init(5, &timer);
    CHECK(fs.isSdCardInserted(), "the image did not mount");
    CHECK(fs.mkdir(0, "/Workspace") && fs.mkdir(0, "/Samples"), "mkdir");
    const auto projRam = pattern(2048, 3), streamA = pattern(300 * 1024, 7), streamB = pattern(300 * 1024, 13);
    CHECK(put(fs, "/Workspace/project.mt", projRam) && put(fs, "/Samples/stream_a.wav", streamA) && put(fs, "/Samples/stream_b.wav", streamB),
          "writing the project and the streams");
    FileSystem::FileHandle ha = nullptr, hb = nullptr;
    std::vector<uint8_t> first;
    CHECK(bindStream(fs, "/Samples/stream_a.wav", &ha, first) && memcmp(first.data(), streamA.data(), 512) == 0, "stream A bind");
    CHECK(bindStream(fs, "/Samples/stream_b.wav", &hb, first) && memcmp(first.data(), streamB.data(), 512) == 0, "stream B bind");
    auto [wok, hw] = fs.openFile("/Workspace/pending.txt", FileSystem::kWriteCreateNew);
    const std::vector<uint8_t> pending(1000, 'P');
    CHECK(wok && fs.writeFile(pending.data(), pending.size(), hw) == 1000, "a file left open for writing");

    // 2. the computer owns the card
    CHECK(fs.lendToHost(), "lendToHost");
    CHECK(fs.hostOwned(), "hostOwned");
    const unsigned r0 = g_reads, w0 = g_writes, a0 = g_asyncReads;
    unsigned calls = 0;
    std::vector<uint8_t> buf(4096);
    char* list[4] = {};
    char names[256];
    char* fileList[8];
    int count = 8;
    uint32_t clmt[64], sz = 0;
    uint8_t isDir = 0, isHid = 0;
    char item[64];
    CHECK(!fs.exists("/Workspace/project.mt"), "exists"); calls++;
    CHECK(!fs.rename("/Samples/stream_b.wav", "/Samples/x.wav"), "rename"); calls++;
    CHECK(!fs.mkdir(0, "/New"), "mkdir"); calls++;
    CHECK(!fs.remove("/Workspace/project.mt"), "remove"); calls++;
    CHECK(!fs.removeDirWithFiles("/Samples"), "removeDirWithFiles"); calls++;
    CHECK(!fs.removeOnlyDirFiles("/Samples"), "removeOnlyDirFiles"); calls++;
    CHECK(fs.getFileSize("/Workspace/project.mt") == 0, "getFileSize(path)"); calls++;
    CHECK(fs.getFileSize(hb) == 0, "getFileSize(h)"); calls++;
    CHECK(fs.getFileAvailable(hb) == 0, "getFileAvailable"); calls++;
    {
        auto [ok, h] = fs.openFile("/Workspace/new.txt", FileSystem::kWriteCreateNew);
        CHECK(!ok && h == nullptr, "openFile"); calls++;
    }
    CHECK(fs.readFile(buf.data(), 512, hb) == -1, "readFile"); calls++;
    CHECK(fs.writeFile(buf.data(), 512, hw) == -1, "writeFile"); calls++;
    CHECK(!fs.seekFileCur(10, hb), "seekFileCur"); calls++;
    CHECK(!fs.seekFile(10, hb), "seekFile"); calls++;
    fs.endFile(hw); calls++;
    CHECK(!fs.enableFastSeek(hb, clmt, 64), "enableFastSeek"); calls++;
    CHECK(!fs.readSectorsAsync(buf.data(), 100, 1), "readSectorsAsync"); calls++;
    CHECK(fs.createInstrumentsList("/Workspace", list, 4, &sz) == FileSystem::kListError, "createInstrumentsList"); calls++;
    CHECK(fs.countContainedFiles("/Samples") == 0, "countContainedFiles"); calls++;
    CHECK(fs.createFilesList("/Samples", 0, list, 4, 2000) == FileSystem::kListError, "createFilesList"); calls++;
    fs.listDirContents("/Samples", FileSystem::kFiles, nullptr, names, sizeof names, 32, count, fileList); calls++;
    CHECK(count == 0, "listDirContents");
    CHECK(fs.readNbFiles("/Samples") == 0, "readNbFiles"); calls++;
    CHECK(!fs.nesDir_open("/Samples"), "nesDir_open"); calls++;
    CHECK(!fs.nesDir_readItem(item, &isDir, &isHid, &sz), "nesDir_readItem"); calls++;
    fs.nesDir_close(); calls++;
    fs.closeFile(hw); calls++;   // the writer the mode caught: its slot comes back, the card is not touched
    CHECK(f_stat("/Workspace/project.mt", nullptr) == FR_NOT_ENABLED, "FatFs direct: %d", int(f_stat("/Workspace/project.mt", nullptr)));
    CHECK(g_reads == r0 && g_writes == w0 && g_asyncReads == a0, "blocks reached the card while the computer owned it: %u reads %u writes %u async",
          g_reads - r0, g_writes - w0, g_asyncReads - a0);
    CHECK(fs.refusedCalls() == calls, "refused %u of %u calls", fs.refusedCalls(), calls);
    const uint32_t blocks = fs.hostBlockCount();
    g_hostIo = true;
    CHECK(blocks > 0 && fs.hostReadBlocks(0, buf.data(), 1) && buf[510] == 0x55 && buf[511] == 0xAA, "the drive reads the boot sector");
    CHECK(fs.hostReadBlocks(blocks - 8, buf.data(), 8), "the drive reads the last blocks");
    CHECK(!fs.hostReadBlocks(blocks - 1, buf.data(), 2) && !fs.hostWriteBlocks(blocks, buf.data(), 1) && !fs.hostReadBlocks(0, buf.data(), 0),
          "the drive's block calls past the card");
    g_hostIo = false;
    printf("lent: %u calls refused, 0 blocks from the firmware, the drive read %u times, %u blocks\n", fs.refusedCalls(), g_hostReads, blocks);

    // 3. the computer
    fflush(stdout);
    const int hrc = system(hostCmd);
    CHECK(hrc == 0, "HOSTCMD exit %d", hrc);

    // 4. the card back
    CHECK(fs.remount(), "remount");
    CHECK(!fs.hostOwned() && g_takeBacks == 1, "owned %d take backs %u", int(fs.hostOwned()), g_takeBacks);
    CHECK(fs.readFile(buf.data(), 512, ha) <= 0 && fs.readFile(buf.data(), 512, hb) <= 0, "a handle of the old mount still reads");
    fs.closeFile(ha);
    fs.closeFile(hb);
    CHECK(!bindStream(fs, "/Samples/stream_a.wav", &ha, first), "stream A (deleted by the computer) bound again");
    if (ha) fs.closeFile(ha);
    const auto bNew = macFile(src + "/stream_b_new.wav");
    CHECK(bNew.size() >= 512 && bindStream(fs, "/Samples/stream_b.wav", &hb, first) && memcmp(first.data(), bNew.data(), 512) == 0,
          "stream B does not stream the computer's new file");
    fs.closeFile(hb);
    CHECK(get(fs, "/Samples/stream_b.wav") == bNew, "stream B read back");
    CHECK(get(fs, "/Workspace/project.mt") == macFile(src + "/project_host.mt"), "/Workspace/project.mt is not the computer's");
    CHECK(get(fs, "/Workspace/pending.txt") == pending, "the open file's 1000 bytes did not reach the card before the computer");
    unsigned copied = 0;
    // the folder s3_fat_roundtrip.sh made (FatFs owns the name DIR here, so no dirent.h)
    for (const char* n : {"sample_1.wav", "sample_2.wav", "sample_3.wav", "sample_4.wav", "sample_5.wav", "sample_6.wav", "sample_7.wav",
                          "sample_8.wav", "long_loop.wav"})
    {
        const std::string card = std::string("/Copied/") + n;
        const auto mac = macFile(src + "/Copied/" + n);
        CHECK(!mac.empty() && get(fs, card.c_str()) == mac, "%s differs", card.c_str());
        copied++;
    }
    CHECK(copied >= 3, "copied %u files", copied);
    CHECK(put(fs, "/Workspace/after.txt", pattern(5000, 5)) && get(fs, "/Workspace/after.txt") == pattern(5000, 5), "writing after the remount");
    printf("back: stream A gone, stream B new, project and %u copied files equal to the computer's, writes work\n", copied);

    // a remount with the card pulled has nothing to mount, and the gate is open (no stale ownership)
    CHECK(fs.lendToHost(), "second lend");
    g_fakeCardOut = 1;
    fs.isSdCardInserted();   // the detect pin: SdCard::stop
    CHECK(!fs.remount() && !fs.hostOwned(), "remount with the card pulled");
    g_fakeCardOut = 0;

    printf("%d checks, %d failures\n", s_checks, s_fail);
    return s_fail ? 1 : 0;
}
