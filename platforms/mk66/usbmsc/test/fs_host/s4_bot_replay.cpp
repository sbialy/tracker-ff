// Simulator row S4 of ref/usb-mass-storage-feasibility.md: SCSI command sequences through the Bot (msc_bot.cpp, the USB drive's
// Bulk-Only/SCSI layer) into the real block path of the original (FileSystemImpl::hostReadBlocks / hostWriteBlocks, SD.cpp, FatFs on a
// disk image via shim/fsl_sd.h). Host only, NOT firmware. Driven by s4_bot_replay.sh.
//
//   s4_bot_replay IMAGE REF MODE
//     IMAGE  the card before the session (a copy, written by the replay)
//     REF    the same card after macOS (msdosfs) changed it directly: the blocks that differ are the computer's writes
//     MODE   macos    INQUIRY, TEST UNIT READY (UNIT ATTENTION once), READ CAPACITY, MODE SENSE(6), PREVENT, READ(10) of the FAT
//                     area, the writes as WRITE(10) of up to 128 blocks, SYNCHRONIZE CACHE, ALLOW, START STOP UNIT (eject)
//            windows  INQUIRY, READ FORMAT CAPACITIES, READ CAPACITY, MODE SENSE(6) 1C and 3F, READ(10), WRITE(10) of up to 64
//                     blocks, no cache flush, START STOP UNIT (eject)
//            pull     macos, with the cable pulled in the data stage of a WRITE(10) half way (Bot::abort, bus reset), the medium
//                     presented again, the computer mounts again and writes from that command on
// After the session IMAGE must equal REF byte for byte, and the firmware mounts it again (remount) and lists the card.
// The packet side is the fake endpoint pair of OpenTracker's msc_bot_test.cpp (Apache-2.0, see msc_bot.h).
#include "FileSystemImpl.h"
#include "msc_bot.h"
#include "msc_drive.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

using namespace ot::msc;

extern const char* g_imagePath;
extern unsigned g_reads, g_writes, g_asyncReads, g_hostReads, g_hostWrites;
extern bool g_hostIo;

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

namespace
{
// ---- the endpoint pair (OpenTracker msc_bot_test.cpp FakeEp, trimmed) ---------------------------------------------------
struct Seg
{
    uint8_t* buf;
    uint32_t len, done;
    bool complete;
};
class FakeEp : public IEndpoints
{
  public:
    std::deque<Seg> rx, tx;
    bool haltIn = false, haltOut = false;
    bool configured() const override { return true; }
    bool rxQueue(uint8_t* buf, uint32_t len) override
    {
        if (rx.size() >= 4 || !len) return false;
        rx.push_back({buf, len, 0, false});
        return true;
    }
    bool rxPoll(uint32_t* got) override
    {
        if (rx.empty() || !rx.front().complete) return false;
        *got = rx.front().done;
        rx.pop_front();
        return true;
    }
    bool txQueue(const uint8_t* buf, uint32_t len) override
    {
        if (tx.size() >= 4 || !len) return false;
        tx.push_back({const_cast<uint8_t*>(buf), len, 0, false});
        return true;
    }
    bool txPoll() override
    {
        if (tx.empty() || !tx.front().complete) return false;
        tx.pop_front();
        return true;
    }
    void rxFlush() override { rx.clear(); }
    void txFlush() override { tx.clear(); }
    void stall(bool in) override { (in ? haltIn : haltOut) = true; }
    bool stalled(bool in) const override { return in ? haltIn : haltOut; }
    enum Tok { kAck, kNak, kStall };
    Tok hostOut(const uint8_t* p, uint32_t n)
    {
        if (haltOut) return kStall;
        for (Seg& s : rx)
        {
            if (s.complete) continue;
            if (s.len - s.done < n) return kNak;
            memcpy(s.buf + s.done, p, n);
            s.done += n;
            if (s.done >= s.len || n < 64) s.complete = true;
            return kAck;
        }
        return kNak;
    }
    Tok hostIn(std::vector<uint8_t>& out, uint32_t* n)
    {
        *n = 0;
        if (haltIn) return kStall;
        for (Seg& s : tx)
        {
            if (s.complete) continue;
            uint32_t k = s.len - s.done;
            if (k > 64) k = 64;
            out.insert(out.end(), s.buf + s.done, s.buf + s.done + k);
            s.done += k;
            if (s.done >= s.len) s.complete = true;
            *n = k;
            return kAck;
        }
        return kNak;
    }
};

// ---- the card behind the drive: the firmware's own block path ------------------------------------------------------------
class CardDev : public IBlockDevice
{
  public:
    explicit CardDev(FileSystemImpl& f) : fs(f) {}
    bool read(uint32_t lba, uint8_t* dst, uint32_t count) override
    {
        g_hostIo = true;
        const bool ok = fs.hostReadBlocks(lba, dst, count);
        g_hostIo = false;
        return ok;
    }
    bool write(uint32_t lba, const uint8_t* src, uint32_t count) override
    {
        g_hostIo = true;
        const bool ok = fs.hostWriteBlocks(lba, src, count);
        g_hostIo = false;
        return ok;
    }
    FileSystemImpl& fs;
};

struct Result
{
    std::vector<uint8_t> data;
    uint8_t status = 0xFF;
    bool gotCsw = false;
};

// A Bulk-Only host on the fake pair (msc_bot_test.cpp Rig::cmd, trimmed). pullAt: give up after that many OUT bytes of the data
// stage (the cable is pulled), returns with gotCsw false.
struct Host
{
    FakeEp ep;
    Bot bot;
    CardDev* dev;
    uint8_t buf[16384] = {};
    uint8_t small[Bot::kSmallBytes] = {};
    uint32_t tag = 1;
    explicit Host(CardDev* d) : dev(d) { bot.init(&ep, buf, sizeof buf, small); }
    void step()
    {
        bot.service();
        bot.runIo(*dev);
        bot.service();
    }
    Result cmd(const std::vector<uint8_t>& cdb, uint32_t len, bool in, const uint8_t* out = nullptr, uint32_t pullAt = 0)
    {
        Result r;
        uint8_t c[31] = {'U', 'S', 'B', 'C'};
        ++tag;
        memcpy(c + 4, &tag, 4);
        memcpy(c + 8, &len, 4);
        c[12] = in ? 0x80 : 0x00;
        c[14] = uint8_t(cdb.size());
        memcpy(c + 15, cdb.data(), cdb.size());
        bool sent = false;
        for (int i = 0; i < 100 && !sent; ++i)
        {
            step();
            sent = ep.hostOut(c, 31) == FakeEp::kAck;
        }
        if (!sent) return r;
        if (len && in)
        {
            uint32_t got = 0;
            for (int i = 0; i < 2000000 && got < len; ++i)
            {
                step();
                uint32_t n = 0;
                const auto t = ep.hostIn(r.data, &n);
                if (t == FakeEp::kStall) break;
                if (t == FakeEp::kAck) { got += n; if (n < 64) break; }
            }
        }
        else if (len)
        {
            uint32_t off = 0;
            for (int i = 0; i < 2000000 && off < len; ++i)
            {
                if (pullAt && off >= pullAt) return r;   // the cable is gone
                step();
                const uint32_t n = len - off < 64 ? len - off : 64;
                const auto t = ep.hostOut(out + off, n);
                if (t == FakeEp::kStall) { ep.haltOut = false; break; }
                if (t == FakeEp::kAck) off += n;
            }
        }
        for (int i = 0; i < 2000000; ++i)
        {
            step();
            std::vector<uint8_t> csw;
            uint32_t n = 0;
            const auto t = ep.hostIn(csw, &n);
            if (t == FakeEp::kStall) { ep.haltIn = false; continue; }
            if (t == FakeEp::kAck) { r.gotCsw = n == 13; if (r.gotCsw) r.status = csw[12]; break; }
        }
        step();
        return r;
    }
    static std::vector<uint8_t> rw10(uint8_t opc, uint32_t lba, uint16_t n)
    {
        return {opc, 0, uint8_t(lba >> 24), uint8_t(lba >> 16), uint8_t(lba >> 8), uint8_t(lba), 0, uint8_t(n >> 8), uint8_t(n), 0};
    }
};

std::vector<uint8_t> load(const char* p)
{
    std::vector<uint8_t> v;
    if (FILE* f = fopen(p, "rb"))
    {
        fseeko(f, 0, SEEK_END);
        v.resize(size_t(ftello(f)));
        fseeko(f, 0, SEEK_SET);
        if (fread(v.data(), 1, v.size(), f) != v.size()) v.clear();
        fclose(f);
    }
    return v;
}

struct Run
{
    uint32_t lba, n;
};
} // namespace

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: s4_bot_replay IMAGE REF macos|windows|pull\n");
        return 2;
    }
    g_imagePath = argv[1];
    const std::string mode = argv[3];
    const auto before = load(argv[1]), ref = load(argv[2]);
    CHECK(!before.empty() && before.size() == ref.size(), "images %zu %zu", before.size(), ref.size());
    // the computer's writes: the 512 B blocks macOS changed, as runs of up to `chunk` blocks in card order
    const uint32_t chunk = mode == "windows" ? 64 : 128;
    std::vector<Run> runs;
    for (uint32_t b = 0; b < ref.size() / 512; b++)
    {
        if (memcmp(&before[b * 512], &ref[b * 512], 512) == 0) continue;
        if (!runs.empty() && runs.back().lba + runs.back().n == b && runs.back().n < chunk) runs.back().n++;
        else runs.push_back({b, 1});
    }
    uint32_t changed = 0;
    for (auto& r : runs) changed += r.n;
    printf("%s: %u blocks changed by macOS in %zu WRITE(10) commands\n", mode.c_str(), changed, runs.size());

    FakeTimer timer;
    static FileSystemImpl fs;
    fs.init(5, &timer);
    CHECK(fs.isSdCardInserted(), "the image did not mount");
    CHECK(fs.lendToHost(), "lendToHost");
    const unsigned fwR = g_reads, fwW = g_writes;
    CardDev dev(fs);
    Host h(&dev);
    const uint32_t blocks = fs.hostBlockCount();
    h.bot.setMedium(true, blocks);

    bool verifyReads = true;   // the second mount after a cable pull reads a card that is half written: not compared
    auto mount = [&](bool windows) {
        Result r = h.cmd({op::kInquiry, 0, 0, 0, 36, 0}, 36, true);
        CHECK(r.status == 0 && r.data.size() == 36 && (r.data[1] & 0x80), "INQUIRY: removable");
        r = h.cmd({op::kTestUnitReady, 0, 0, 0, 0, 0}, 0, false);   // UNIT ATTENTION: medium changed, once
        if (r.status != 0)
        {
            Result s = h.cmd({op::kRequestSense, 0, 0, 0, 18, 0}, 18, true);
            CHECK(s.data.size() >= 14 && (s.data[2] & 0x0F) == 6 && s.data[12] == 0x28, "the first sense is UNIT ATTENTION 28h");
            r = h.cmd({op::kTestUnitReady, 0, 0, 0, 0, 0}, 0, false);
        }
        CHECK(r.status == 0, "TEST UNIT READY");
        if (windows)
        {
            r = h.cmd({op::kReadFormatCapacities, 0, 0, 0, 0, 0, 0, 0, 0xFC, 0}, 0xFC, true);
            CHECK(r.status == 0 && r.data.size() >= 12, "READ FORMAT CAPACITIES");
        }
        r = h.cmd({op::kReadCapacity10, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8, true);
        const uint32_t last = r.data.size() == 8 ? uint32_t(r.data[0]) << 24 | r.data[1] << 16 | r.data[2] << 8 | r.data[3] : 0;
        CHECK(r.status == 0 && last == blocks - 1, "READ CAPACITY: last block %u of %u", last, blocks);
        for (uint8_t page : windows ? std::vector<uint8_t>{0x1C, 0x3F} : std::vector<uint8_t>{0x3F})
        {
            r = h.cmd({op::kModeSense6, 0, page, 0, 0xC0, 0}, 0xC0, true);
            CHECK(r.status == 0 && !r.data.empty() && !(r.data[2] & 0x80), "MODE SENSE(6) %02x: not write protected", page);
        }
        if (!windows) CHECK(h.cmd({op::kPreventAllow, 0, 0, 0, 1, 0}, 0, false).status == 0, "PREVENT");
        // the boot sector, FSINFO and the FATs (what msdosfs reads at mount), 64 KB at a time
        uint32_t rd = 0;
        for (uint32_t lba = 0; lba < 2048 && lba < blocks; lba += 128)
        {
            r = h.cmd(Host::rw10(op::kRead10, lba, 128), 128 * 512, true);
            CHECK(r.status == 0 && r.data.size() == 128 * 512 && (!verifyReads || memcmp(r.data.data(), &before[lba * 512], r.data.size()) == 0),
                  "READ(10) %u", lba);
            rd++;
        }
        return rd;
    };
    auto writeFrom = [&](size_t from, size_t pullAt) -> size_t {   // returns the run the cable was pulled in, or runs.size()
        for (size_t i = from; i < runs.size(); i++)
        {
            const Run& w = runs[i];
            const uint32_t pull = i == pullAt ? w.n * 512 / 2 / 64 * 64 + 64 : 0;
            Result r = h.cmd(Host::rw10(op::kWrite10, w.lba, uint16_t(w.n)), w.n * 512, false, &ref[w.lba * 512], pull);
            if (pull) return i;
            CHECK(r.status == 0 && r.gotCsw, "WRITE(10) %u x %u status %d", w.lba, w.n, int(r.status));
        }
        return runs.size();
    };
    auto eject = [&](bool windows) {
        if (!windows) CHECK(h.cmd({op::kSyncCache10, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0, false).status == 0, "SYNCHRONIZE CACHE");
        if (!windows) CHECK(h.cmd({op::kPreventAllow, 0, 0, 0, 0, 0}, 0, false).status == 0, "ALLOW");
        CHECK(h.cmd({op::kStartStopUnit, 0, 0, 0, 0x02, 0}, 0, false).status == 0, "START STOP UNIT (eject)");
        CHECK(h.bot.hostEjected(), "the drive did not see the eject");
        Result r = h.cmd({op::kTestUnitReady, 0, 0, 0, 0, 0}, 0, false);
        Result s = h.cmd({op::kRequestSense, 0, 0, 0, 18, 0}, 18, true);
        CHECK(r.status == 1 && s.data.size() >= 14 && (s.data[2] & 0x0F) == 2 && s.data[12] == 0x3A, "after the eject: NOT READY 3Ah");
    };

    const bool windows = mode == "windows";
    uint32_t mountReads = mount(windows);
    if (mode == "pull")
    {
        const size_t at = writeFrom(0, runs.size() / 2);
        CHECK(at == runs.size() / 2, "pulled at %zu", at);
        // the cable is gone: the drive gives up on the command (HostWatch, 500 ms without frames), the bus resets when it is back
        h.bot.abort();
        h.ep.rx.clear();
        h.ep.tx.clear();
        h.ep.haltIn = h.ep.haltOut = false;
        h.bot.onReset();
        for (int i = 0; i < 10 && h.bot.busy(); i++) h.step();
        CHECK(!h.bot.busy(), "the drive stays busy after the reset");
        h.bot.setMedium(false, 0);
        h.bot.setMedium(true, blocks);
        verifyReads = false;
        mountReads += mount(false);   // the computer finds the card again and writes from the interrupted command on
        writeFrom(at, size_t(-1));
        eject(false);
    }
    else
    {
        if (mode == "drop") runs.pop_back();   // negative control of s4_bot_replay.sh: one WRITE(10) less must show as a difference
        writeFrom(0, size_t(-1));
        eject(windows);
    }
    CHECK(g_reads == fwR && g_writes == fwW, "the firmware touched the card in the session");
    h.bot.setMedium(false, 0);

    // the card back: byte for byte macOS's own result, and the firmware mounts it
    const auto after = load(argv[1]);
    size_t diff = 0;
    for (size_t b = 0; b < after.size() / 512 && after.size() == ref.size(); b++) diff += memcmp(&after[b * 512], &ref[b * 512], 512) != 0;
    CHECK(after.size() == ref.size() && diff == 0, "%zu blocks differ from macOS's image", diff);
    CHECK(fs.remount(), "remount");
    char* list[16] = {};
    const int n = fs.createFilesList("/Copied", 0, list, 16, 4000);
    CHECK(n >= 3, "the firmware lists %d files in /Copied", n);
    for (auto* p : list) delete[] p;
    printf("%s: %u mount reads, %u drive reads %u drive writes, image equal to macOS's, firmware lists %d files in /Copied\n", mode.c_str(),
           mountReads, g_hostReads, g_hostWrites, n);
    printf("%d checks, %d failures\n", s_checks, s_fail);
    return s_fail ? 1 : 0;
}
