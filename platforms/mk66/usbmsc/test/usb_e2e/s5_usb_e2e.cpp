// Simulator row S5 of ref/usb-mass-storage-feasibility.md (phase 1b): the USB drive end to end on the Mac, no device.
//   macOS / Windows style host  ->  simulated USB-FS controller (usbsim.c)  ->  the REAL teensy_core/usb_dev.c (usb_isr, control
//   requests, SET_CONFIGURATION, CLEAR_FEATURE, usb_msc_impl.h)  ->  the Bot (msc_bot.cpp)  ->  the Pump (msc_pump.cpp, the main
//   loop of the drive)  ->  FileSystemImpl::hostReadBlocks/hostWriteBlocks, SD.cpp, FatFs on a macOS-formatted disk image.
// The packet side of S4 (the fake endpoint pair) is replaced by the firmware's own endpoint driver. Host only, NOT firmware.
//
//   s5_usb_e2e IMAGE REF MODE [audio]
//     IMAGE  the card before the session (a copy, written by the session)     REF  the same card after macOS changed it directly
//     MODE   macos    enumeration, polls with no medium (NOT READY), Drive mode entry, mount, copy (WRITE(10) up to 128 blocks),
//                     read back, SYNC CACHE, eject, Exit (card back), polls with no medium again
//            windows  enumeration with SET/GET_INTERFACE, READ FORMAT CAPACITIES, MODE SENSE 1C and 3F, WRITE(10) up to 64 blocks, eject
//            pull     macos, the cable pulled in a WRITE(10) data stage: SOFs stop, 500 ms later the pump gives up, plugged in again
//                     (bus reset, enumeration), mounts again and writes on
//            reset    macos, then the Bulk-Only error paths: bad CBW signature (both pipes halt), reset recovery, INQUIRY with a
//                     larger allocation length than the reply (IN stall, CSW after CLEAR_FEATURE), Mass Storage Reset
//            exit     a READ(10) in flight the host never takes, Exit: the pump waits 3 s and drops it
//            off      the drive disabled at start: no interface, class requests stall, endpoints 9/10 silent
//            drop     macos with its last WRITE(10) left out: negative control, must end with differing blocks
//     audio  (optional) enumerate with the USB audio function on as well (interface 6 for the drive)
// Ends "N checks, M failures". The image is compared to REF byte for byte, fsck_msdos -n runs in the script.
#include "FileSystemImpl.h"
#include "msc_bot.h"
#include "msc_endpoints.h"
#include "msc_pump.h"
#include "usbsim.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ot::msc;

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
    uint32_t millis() override { return uint32_t(usbsim_now_us() / 1000); }
    uint32_t micros() override { return uint32_t(usbsim_now_us()); }
    void delay(int d) override { usbsim_advance(uint32_t(d) * 1000); }
    uint32_t clockCycles() override { return 0; }
    uint32_t clockCyclesPerUs() override { return 180; }
};

namespace
{
using ff::mscdrive::Pump;

// ---- the card behind the drive: the firmware's own block path, with the time a card needs --------------------------------------
class CardDev : public IBlockDevice
{
  public:
    explicit CardDev(FileSystemImpl& f) : fs(f) {}
    uint32_t slowUs = 0; // extra time per request (the exit timeout test)
    bool read(uint32_t lba, uint8_t* dst, uint32_t count) override
    {
        usbsim_advance(100 + count * 512 / 16 + slowUs / 2); // half the transfer before the data lands, half after: an interrupt in between sees a pending request
        g_hostIo = true;
        const bool ok = fs.hostReadBlocks(lba, dst, count);
        g_hostIo = false;
        usbsim_advance(100 + count * 512 / 16 + slowUs / 2);
        return ok;
    }
    bool write(uint32_t lba, const uint8_t* src, uint32_t count) override
    {
        usbsim_advance(100 + count * 512 / 16 + slowUs / 2);
        g_hostIo = true;
        const bool ok = fs.hostWriteBlocks(lba, src, count);
        g_hostIo = false;
        usbsim_advance(100 + count * 512 / 16 + slowUs / 2);
        return ok;
    }
    FileSystemImpl& fs;
};

class SimEnv : public ff::mscdrive::Env
{
  public:
    explicit SimEnv(FileSystemImpl& f) : fs(f) {}
    uint32_t millis() override { usbsim_advance(2); return uint32_t(usbsim_now_us() / 1000); }
    uint32_t micros() override { usbsim_advance(2); return uint32_t(usbsim_now_us()); }
    void delayUs(uint32_t us) override { usbsim_advance(us); }
    void usbMask() override { usbsim_mask(1); }
    void usbUnmask() override { usbsim_mask(0); }
    void kick() override { usbsim_deliver_pending(); }
    bool cardIn() override { return fs.cardIn(); }
    FileSystemImpl& fs;
};

// The firmware side as msc_drive.cpp builds it (Drive, isrHook, init, enter, ffUsbMscTakeBack)
alignas(32) uint8_t g_ring[Bot::kChunk * Bot::kMaxChunks];
uint8_t g_small[Bot::kSmallBytes];
struct Drive
{
    Drive(FileSystemImpl& f) : card(f), env(f), pump(bot, ep, card, env) {}
    ff::mscdrive::UsbEndpoints ep;
    CardDev card;
    SimEnv env;
    Bot bot;
    Pump pump;
};
Drive* g_drive;
void isrHook(uint8_t event)
{
    if (g_drive) g_drive->pump.isrEvent(event);
}
} // namespace

extern "C" void ffUsbMscTakeBack(void)
{
    if (g_drive) g_drive->pump.end();
}

namespace
{
struct Result
{
    std::vector<uint8_t> data;
    uint8_t status = 0xFF;
    bool gotCsw = false;
    uint32_t residue = 0;
};

FileSystemImpl* g_fs;

// the firmware's main loop between USB events: the card detect, then the drive
void tick()
{
    usbsim_advance(20);
    g_fs->update();
    if (g_drive) g_drive->pump.service();
}
void idleFor(uint32_t ms)
{
    const uint64_t end = usbsim_now_us() + uint64_t(ms) * 1000;
    while (usbsim_now_us() < end)
    {
        usbsim_advance(500);
        tick();
    }
}

// ---- a computer: enumeration, then Bulk-Only on the two bulk endpoints ----------------------------------------------------------
struct Host
{
    int iface = -1, inEp = 0, outEp = 0, numIfaces = 0;
    uint32_t tag = 1;
    int ctl(uint8_t type, uint8_t req, uint16_t val, uint16_t idx, uint16_t len, uint8_t* data = nullptr, int cap = 0)
    {
        uint8_t s[8] = {type, req, uint8_t(val), uint8_t(val >> 8), uint8_t(idx), uint8_t(idx >> 8), uint8_t(len), uint8_t(len >> 8)};
        return usbsim_control(s, data, cap);
    }
    // Reset, descriptors, SET_ADDRESS, SET_CONFIGURATION: what a computer does on every plug-in
    void enumerate(bool expectDrive, bool audioOn)
    {
        usbsim_bus_reset();
        uint8_t d[18] = {};
        CHECK(ctl(0x80, 6, 0x0100, 0, 18, d, 18) == 18 && d[1] == 1, "device descriptor");
        uint8_t c[512] = {};
        CHECK(ctl(0x80, 6, 0x0200, 0, 9, c, 9) == 9, "configuration header");
        const int total = c[2] | (c[3] << 8);
        CHECK(ctl(0x80, 6, 0x0200, 0, uint16_t(total), c, sizeof c) == total, "configuration, %d bytes", total);
        numIfaces = c[4];
        iface = -1;
        inEp = outEp = 0;
        int cur = -1, walked = 0;
        for (int p = 0; p + 2 <= total && c[p];)
        {
            walked = p + c[p];
            if (c[p + 1] == 4) cur = (c[p + 5] == 0x08 && c[p + 6] == 0x06 && c[p + 7] == 0x50) ? c[p + 2] : -1;
            if (c[p + 1] == 5 && cur >= 0)
            {
                iface = cur;
                if (c[p + 2] & 0x80) inEp = c[p + 2] & 0x7F; else outEp = c[p + 2];
                CHECK(c[p + 3] == 2 && c[p + 4] == 64, "bulk 64 B endpoint");
            }
            p += c[p];
        }
        CHECK(walked == total, "the descriptor chain walks to wTotalLength (%d of %d)", walked, total);
        CHECK(ctl(0x00, 5, 7, 0, 0) == 0, "SET_ADDRESS");
        CHECK(ctl(0x00, 9, 1, 0, 0) == 0, "SET_CONFIGURATION");
        for (int e = 1; e <= 10; e++) usbsim_host_toggle_reset(e);
        if (expectDrive)
        {
            CHECK(iface == (audioOn ? 6 : 4), "drive interface %d, want %d", iface, audioOn ? 6 : 4);
            CHECK(inEp == 9 && outEp == 10 && numIfaces == iface + 1, "endpoints %d/%d interfaces %d", inEp, outEp, numIfaces);
            uint8_t lun = 0xFF;
            CHECK(ctl(0xA1, 0xFE, 0, uint16_t(iface), 1, &lun, 1) == 1 && lun == 0, "Get Max LUN");
        }
        else
        {
            CHECK(iface < 0, "no mass storage interface while the drive is off");
        }
    }
    bool clearHalt(bool in)
    {
        const int ep = in ? inEp : outEp;
        const int r = ctl(0x02, 1, 0, uint16_t(ep | (in ? 0x80 : 0)), 0);
        usbsim_host_toggle_reset(ep);
        return r == 0;
    }
    bool bulkReset()
    {
        const bool r = ctl(0x21, 0xFF, 0, uint16_t(iface), 0) == 0;
        return r;
    }
    // One OUT packet, ticking the firmware while it NAKs. false: stall or nothing
    int outPacket(const uint8_t* p, uint32_t n, bool* stalled)
    {
        for (int i = 0; i < 200000; i++)
        {
            const int r = usbsim_out(outEp, p, n);
            if (r == USBSIM_ACK) return 1;
            if (r == USBSIM_STALL) { *stalled = true; return 0; }
            if (r == USBSIM_NONE) return 0;
            tick();
        }
        return 0;
    }
    int inPacket(uint8_t* p, uint32_t* n, bool* stalled)
    {
        for (int i = 0; i < 200000; i++)
        {
            const int r = usbsim_in(inEp, p, n);
            if (r == USBSIM_ACK) return 1;
            if (r == USBSIM_STALL) { *stalled = true; return 0; }
            if (r == USBSIM_NONE) return 0;
            tick();
        }
        return 0;
    }
    static std::vector<uint8_t> rw10(uint8_t opc, uint32_t lba, uint16_t n)
    {
        return {opc, 0, uint8_t(lba >> 24), uint8_t(lba >> 16), uint8_t(lba >> 8), uint8_t(lba), 0, uint8_t(n >> 8), uint8_t(n), 0};
    }
    // CBW, data stage, CSW (BOT 6.7 with the stall handling of a real host). pullAt: the cable is pulled after that many data bytes.
    // badSig: a CBW with a wrong signature.
    Result cmd(const std::vector<uint8_t>& cdb, uint32_t len, bool in, const uint8_t* out = nullptr, uint32_t pullAt = 0, bool badSig = false)
    {
        Result r;
        uint8_t c[31] = {'U', 'S', 'B', badSig ? uint8_t('X') : uint8_t('C')};
        ++tag;
        memcpy(c + 4, &tag, 4);
        memcpy(c + 8, &len, 4);
        c[12] = in ? 0x80 : 0x00;
        c[14] = uint8_t(cdb.size());
        memcpy(c + 15, cdb.data(), cdb.size());
        bool stalled = false;
        if (!outPacket(c, 31, &stalled)) return r;
        if (badSig) return r;
        if (len && in)
        {
            uint32_t got = 0;
            while (got < len)
            {
                uint8_t p[64];
                uint32_t n = 0;
                if (!inPacket(p, &n, &stalled))
                {
                    if (stalled) { CHECK(clearHalt(true), "CLEAR_FEATURE(HALT) IN"); }
                    break;
                }
                r.data.insert(r.data.end(), p, p + n);
                got += n;
                if (n < 64) break;
            }
        }
        else if (len)
        {
            uint32_t off = 0;
            while (off < len)
            {
                if (pullAt && off >= pullAt) { usbsim_connect(0); return r; } // the cable is gone
                const uint32_t n = len - off < 64 ? len - off : 64;
                if (!outPacket(out + off, n, &stalled))
                {
                    if (stalled) { CHECK(clearHalt(false), "CLEAR_FEATURE(HALT) OUT"); }
                    break;
                }
                off += n;
            }
        }
        for (int attempt = 0; attempt < 2; attempt++)
        {
            uint8_t p[64];
            uint32_t n = 0;
            stalled = false;
            if (inPacket(p, &n, &stalled))
            {
                r.gotCsw = n == 13 && memcmp(p, "USBS", 4) == 0 && memcmp(p + 4, &tag, 4) == 0;
                if (r.gotCsw) { r.status = p[12]; memcpy(&r.residue, p + 8, 4); }
                break;
            }
            if (!stalled) break;
            CHECK(clearHalt(true), "CLEAR_FEATURE(HALT) IN before the CSW"); // BOT 6.7.2: clear the stall, read the CSW again
        }
        return r;
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
        fprintf(stderr, "usage: s5_usb_e2e IMAGE REF macos|windows|pull|reset|exit|off|drop [audio]\n");
        return 2;
    }
    g_imagePath = argv[1];
    const std::string mode = argv[3];
    const bool audio = argc > 4 && std::string(argv[4]) == "audio";
    const bool driveOff = mode == "off";
    const auto before = load(argv[1]), ref = load(argv[2]);
    CHECK(!before.empty() && before.size() == ref.size(), "images %zu %zu", before.size(), ref.size());
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
    printf("%s%s: %u blocks changed by macOS in %zu WRITE(10) commands\n", mode.c_str(), audio ? "+audio" : "", changed, runs.size());

    // ---- the firmware starts: the card mounted, the USB stack up -------------------------------------------------------------
    FakeTimer timer;
    static FileSystemImpl fs;
    g_fs = &fs;
    fs.init(5, &timer);
    CHECK(fs.isSdCardInserted(), "the image did not mount");
    usbsim_init(audio ? 1 : 0, driveOff ? 0 : 1);
    if (!driveOff)
    {
        g_drive = new Drive(fs);
        g_drive->bot.init(&g_drive->ep, g_ring, sizeof g_ring, g_small); // msc_drive.cpp init()
        usb_msc_set_hook(&isrHook);
    }
    Host h;
    h.enumerate(!driveOff, audio);

    if (driveOff)
    {
        // the interface does not exist: no class request is answered, the two endpoints are silent, the card is the firmware's
        uint8_t lun;
        CHECK(h.ctl(0xA1, 0xFE, 0, audio ? 6 : 4, 1, &lun, 1) == -1, "Get Max LUN stalls with the drive off");
        CHECK(h.ctl(0x21, 0xFF, 0, audio ? 6 : 4, 0) == -1, "Mass Storage Reset stalls with the drive off");
        CHECK(h.ctl(0x01, 11, 0, audio ? 6 : 4, 0) == -1, "SET_INTERFACE of the missing interface stalls");
        uint8_t cbw[31] = {'U', 'S', 'B', 'C'};
        CHECK(usbsim_out(10, cbw, 31) == USBSIM_NONE, "endpoint 10 is silent");
        uint8_t b[64];
        uint32_t n;
        CHECK(usbsim_in(9, b, &n) == USBSIM_NONE, "endpoint 9 is silent");
        idleFor(50);
        CHECK(fs.isSdCardInserted(), "the firmware keeps the card");
        char* list[16] = {};
        CHECK(fs.createFilesList("/Samples", 0, list, 16, 4000) >= 1, "the firmware reads the card");
        for (auto* p : list) delete[] p;
        CHECK(g_hostReads == 0 && g_hostWrites == 0, "no drive block call");
        printf("off: %u checks\n", s_checks);
        printf("%d checks, %d failures\n", s_checks, s_fail);
        return s_fail ? 1 : 0;
    }

    Drive& d = *g_drive;
    const uint32_t blocks = [&] { return uint32_t(before.size() / 512); }();
    // ---- 1. the card belongs to the firmware: the computer sees an empty reader --------------------------------------------------
    const unsigned fwR0 = g_reads, fwW0 = g_writes;
    for (int i = 0; i < 4; i++)
    {
        Result r = h.cmd({op::kTestUnitReady, 0, 0, 0, 0, 0}, 0, false);
        CHECK(r.gotCsw && r.status == 1, "TEST UNIT READY with no medium: failed command");
        Result s = h.cmd({op::kRequestSense, 0, 0, 0, 18, 0}, 18, true);
        CHECK(s.data.size() >= 14 && (s.data[2] & 0x0F) == 2 && s.data[12] == 0x3A, "no medium: NOT READY 3Ah");
        idleFor(30);
    }
    Result rc = h.cmd({op::kReadCapacity10, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8, true);
    CHECK(rc.gotCsw && rc.status != 0, "READ CAPACITY with no medium fails");
    Result rd = h.cmd(Host::rw10(op::kRead10, 0, 8), 8 * 512, true);
    CHECK(rd.gotCsw && rd.status != 0 && rd.data.empty(), "READ(10) with no medium returns no data");
    Result wr = h.cmd(Host::rw10(op::kWrite10, 100, 1), 512, false, &ref[0]);
    CHECK(wr.gotCsw && wr.status != 0, "WRITE(10) with no medium fails");
    Result inq = h.cmd({op::kInquiry, 0, 0, 0, 36, 0}, 36, true);
    CHECK(inq.status == 0 && inq.data.size() == 36 && (inq.data[1] & 0x80), "INQUIRY answers: a removable drive");
    CHECK(g_hostReads == 0 && g_hostWrites == 0, "the computer reached the card with no medium (%u reads %u writes)", g_hostReads, g_hostWrites);
    {   // the firmware uses the card while the computer polls
        char* list[16] = {};
        const int n = fs.createFilesList("/Samples", 0, list, 16, 4000);
        CHECK(n >= 1, "the firmware lists the card while the drive is not presented (%d)", n);
        for (auto* p : list) delete[] p;
    }

    // ---- 2. Drive mode: Config > USB > Storage lends the card ------------------------------------------------------------------
    CHECK(fs.lendToHost(), "lendToHost");
    CHECK(d.pump.begin(fs.hostBlockCount()), "the pump presents the card");
    const unsigned fwR = g_reads, fwW = g_writes;

    bool verifyReads = true;
    uint32_t mountReads = 0;
    const bool windows = mode == "windows";
    auto mount = [&](bool win) {
        Result r = h.cmd({op::kInquiry, 0, 0, 0, 36, 0}, 36, true);
        CHECK(r.status == 0 && r.data.size() == 36, "INQUIRY");
        r = h.cmd({op::kTestUnitReady, 0, 0, 0, 0, 0}, 0, false); // UNIT ATTENTION: medium changed, once
        if (r.status != 0)
        {
            Result s = h.cmd({op::kRequestSense, 0, 0, 0, 18, 0}, 18, true);
            CHECK(s.data.size() >= 14 && (s.data[2] & 0x0F) == 6 && s.data[12] == 0x28, "the first sense is UNIT ATTENTION 28h");
            r = h.cmd({op::kTestUnitReady, 0, 0, 0, 0, 0}, 0, false);
        }
        CHECK(r.status == 0, "TEST UNIT READY");
        if (win)
        {
            r = h.cmd({op::kReadFormatCapacities, 0, 0, 0, 0, 0, 0, 0, 0xFC, 0}, 0xFC, true);
            CHECK(r.status == 0 && r.data.size() >= 12, "READ FORMAT CAPACITIES");
            CHECK(h.ctl(0x01, 11, 0, uint16_t(h.iface), 0) == 0, "SET_INTERFACE alternate 0 of the drive");
            uint8_t alt = 0xFF;
            CHECK(h.ctl(0x81, 10, 0, uint16_t(h.iface), 1, &alt, 1) == 1 && alt == 0, "GET_INTERFACE of the drive");
            CHECK(h.ctl(0x01, 11, 1, uint16_t(h.iface), 0) == -1, "SET_INTERFACE alternate 1 of the drive stalls");
        }
        r = h.cmd({op::kReadCapacity10, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8, true);
        const uint32_t last = r.data.size() == 8 ? uint32_t(r.data[0]) << 24 | r.data[1] << 16 | r.data[2] << 8 | r.data[3] : 0;
        CHECK(r.status == 0 && last == blocks - 1, "READ CAPACITY: last block %u of %u", last, blocks);
        for (uint8_t page : win ? std::vector<uint8_t>{0x1C, 0x3F} : std::vector<uint8_t>{0x3F})
        {
            r = h.cmd({op::kModeSense6, 0, page, 0, 0xC0, 0}, 0xC0, true);
            CHECK(r.status == 0 && !r.data.empty() && !(r.data[2] & 0x80), "MODE SENSE(6) %02x: not write protected", page);
        }
        if (!win) CHECK(h.cmd({op::kPreventAllow, 0, 0, 0, 1, 0}, 0, false).status == 0, "PREVENT");
        uint32_t n = 0;
        for (uint32_t lba = 0; lba < 2048 && lba < blocks; lba += 128)
        {
            r = h.cmd(Host::rw10(op::kRead10, lba, 128), 128 * 512, true);
            CHECK(r.status == 0 && r.data.size() == 128 * 512 && (!verifyReads || memcmp(r.data.data(), &before[lba * 512], r.data.size()) == 0),
                  "READ(10) %u", lba);
            n++;
        }
        return n;
    };
    auto writeFrom = [&](size_t from, size_t pullAt) -> size_t {
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
    auto readBack = [&](size_t upTo) { // what the computer wrote reads back as written, through the same path
        size_t shown = 0;
        for (size_t i = 0; i < runs.size() && shown < upTo; i += 3, shown++)
        {
            const Run& w = runs[i];
            Result r = h.cmd(Host::rw10(op::kRead10, w.lba, uint16_t(w.n)), w.n * 512, true);
            CHECK(r.status == 0 && r.data.size() == w.n * 512 && memcmp(r.data.data(), &ref[w.lba * 512], r.data.size()) == 0, "read back %u x %u", w.lba, w.n);
        }
    };
    auto eject = [&](bool win) {
        if (!win) CHECK(h.cmd({op::kSyncCache10, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0, false).status == 0, "SYNCHRONIZE CACHE");
        if (!win) CHECK(h.cmd({op::kPreventAllow, 0, 0, 0, 0, 0}, 0, false).status == 0, "ALLOW");
        CHECK(h.cmd({op::kStartStopUnit, 0, 0, 0, 0x02, 0}, 0, false).status == 0, "START STOP UNIT (eject)");
        CHECK(d.bot.hostEjected(), "the drive did not see the eject");
        Result r = h.cmd({op::kTestUnitReady, 0, 0, 0, 0, 0}, 0, false);
        Result s = h.cmd({op::kRequestSense, 0, 0, 0, 18, 0}, 18, true);
        CHECK(r.status == 1 && s.data.size() >= 14 && (s.data[2] & 0x0F) == 2 && s.data[12] == 0x3A, "after the eject: NOT READY 3Ah");
    };
    auto exitMode = [&]() { // Interface::handleFileTransferMode(0): remount(), which takes the card back (ffUsbMscTakeBack)
        const uint64_t t0 = usbsim_now_us();
        CHECK(fs.remount(), "remount");
        CHECK(!d.bot.busy(), "the drive is busy after the take-back");
        CHECK(!d.bot.mediumPresent(), "the medium is still presented after the take-back");
        return (usbsim_now_us() - t0) / 1000;
    };
    auto expectNoMedium = [&](const char* when) {
        Result r = h.cmd({op::kTestUnitReady, 0, 0, 0, 0, 0}, 0, false);
        Result s = h.cmd({op::kRequestSense, 0, 0, 0, 18, 0}, 18, true);
        CHECK(r.gotCsw && r.status == 1 && s.data.size() >= 14 && (s.data[2] & 0x0F) == 2 && s.data[12] == 0x3A, "%s: NOT READY 3Ah", when);
        Result x = h.cmd(Host::rw10(op::kRead10, 0, 8), 8 * 512, true);
        CHECK(x.gotCsw && x.status != 0 && x.data.empty(), "%s: READ(10) returns nothing", when);
    };

    mountReads = mount(windows);
    uint64_t t0 = usbsim_now_us();
    if (mode == "pull")
    {
        const size_t at = writeFrom(0, runs.size() / 2);
        CHECK(at == runs.size() / 2, "pulled at %zu", at);
        CHECK(!usbsim_connected(), "the cable is out");
        CHECK(d.bot.busy(), "the drive is mid-command when the cable goes");
        idleFor(450);
        CHECK(d.pump.drops() == 0, "the pump gave up before 450 ms (%u)", d.pump.drops());
        idleFor(200); // 100 ms without frames until the host counts as gone, then the 500 ms of HostWatch
        CHECK(d.pump.drops() == 1, "the pump gave up on the command after the cable was out for 650 ms (%u)", d.pump.drops());
        for (int i = 0; i < 20; i++) tick();
        CHECK(!d.bot.busy(), "the drive stays busy after the cable pull");
        // plugged in again: bus reset, enumeration, the computer mounts the (still presented) card again and writes from the
        // interrupted command on
        usbsim_connect(1);
        h.enumerate(true, audio);
        verifyReads = false;
        mountReads += mount(false);
        writeFrom(at, size_t(-1));
        readBack(12);
        eject(false);
    }
    else if (mode == "exit")
    {
        // a READ(10) the computer never takes: the data sits queued, the command stays busy, the computer is still there (frames)
        uint8_t c[31] = {'U', 'S', 'B', 'C', 9, 9, 9, 9, 0, 0, 0x10, 0, 0x80, 0, 10};
        const auto cdb = Host::rw10(op::kRead10, 0, 64);
        memcpy(c + 15, cdb.data(), cdb.size());
        bool st = false;
        CHECK(h.outPacket(c, 31, &st), "CBW of the read nobody takes");
        for (int i = 0; i < 40; i++) tick();
        CHECK(d.bot.busy(), "the read is in flight");
        const uint32_t drops0 = d.pump.drops();
        const uint64_t ms = exitMode();
        CHECK(d.pump.drops() == drops0 + 1, "Exit dropped the command after the cap (%u)", d.pump.drops());
        CHECK(ms >= 2900 && ms <= 3300, "Exit took %llu ms, want the 3 s cap", (unsigned long long)ms);
        // the computer recovers: both pipes are halted by the abort, reset recovery works
        CHECK(usbsim_endpoint_halted(9) && usbsim_endpoint_halted(10), "the pipes are halted after the drop");
        CHECK(h.bulkReset() && h.clearHalt(true) && h.clearHalt(false), "reset recovery");
        expectNoMedium("after the dropped exit");
        printf("%s: Exit with a read in flight took %llu ms\n", mode.c_str(), (unsigned long long)ms);
        printf("%d checks, %d failures\n", s_checks, s_fail);
        return s_fail ? 1 : 0;
    }
    else
    {
        if (mode == "drop") runs.pop_back(); // negative control: one WRITE(10) less must show as a difference
        writeFrom(0, size_t(-1));
        readBack(12);
        if (mode == "reset")
        {
            // Bulk-Only error paths on the real endpoints
            Result r = h.cmd({op::kTestUnitReady, 0, 0, 0, 0, 0}, 0, false, nullptr, 0, true); // bad signature: both pipes halt
            CHECK(!r.gotCsw, "a CBW with a bad signature gets no CSW");
            CHECK(usbsim_endpoint_halted(9) && usbsim_endpoint_halted(10), "both pipes halted after the invalid CBW");
            idleFor(5);
            CHECK(h.bulkReset(), "Mass Storage Reset");
            CHECK(h.clearHalt(true) && h.clearHalt(false), "CLEAR_FEATURE(HALT) both");
            r = h.cmd({op::kTestUnitReady, 0, 0, 0, 0, 0}, 0, false);
            CHECK(r.gotCsw && r.status == 0, "the unit answers after reset recovery");
            Result q = h.cmd({op::kInquiry, 0, 0, 0, 255, 0}, 255, true); // allocation 255, reply 36: IN stall, residue 219
            CHECK(q.data.size() == 36 && q.gotCsw && q.status == 0 && q.residue == 219, "INQUIRY(255): %zu bytes, residue %u", q.data.size(), q.residue);
            CHECK(!usbsim_endpoint_halted(9), "the IN stall was cleared by the host");
            Result t = h.cmd(Host::rw10(op::kRead10, 3, 4), 4 * 512, true); // and a normal command after it
            CHECK(t.status == 0 && t.data.size() == 2048 && memcmp(t.data.data(), &ref[3 * 512], 2048) == 0, "READ(10) after the stall");
            // an unsupported command: CHECK CONDITION, ILLEGAL REQUEST, the next one works
            Result u = h.cmd({0xA3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0, false);
            CHECK(u.gotCsw && u.status == 1, "an unsupported opcode fails");
            CHECK(h.cmd({op::kTestUnitReady, 0, 0, 0, 0, 0}, 0, false).status == 0, "and the next command works");
            // SET_CONFIGURATION again with a read half way (a computer that re-configures the device): everything queued is dropped,
            // both endpoints restart at DATA0 and the next command is clean
            {
                uint8_t c[31] = {'U', 'S', 'B', 'C', 7, 7, 7, 7, 0, 0, 0x10, 0, 0x80, 0, 10};
                const auto cdb = Host::rw10(op::kRead10, 2048, 64);
                memcpy(c + 15, cdb.data(), cdb.size());
                bool st = false;
                CHECK(h.outPacket(c, 31, &st), "CBW of the read that gets re-configured");
                uint8_t p[64];
                uint32_t n = 0;
                for (int i = 0; i < 5; i++) CHECK(h.inPacket(p, &n, &st) && n == 64, "data packet %d", i);
                CHECK(h.ctl(0x00, 9, 1, 0, 0) == 0, "SET_CONFIGURATION in the middle of a read");
                for (int e = 1; e <= 10; e++) usbsim_host_toggle_reset(e);
                for (int i = 0; i < 30; i++) tick();
                CHECK(!d.bot.busy(), "the drive is still busy after SET_CONFIGURATION");
                Result t2 = h.cmd(Host::rw10(op::kRead10, 5, 8), 8 * 512, true);
                CHECK(t2.status == 0 && t2.data.size() == 4096 && memcmp(t2.data.data(), &ref[5 * 512], 4096) == 0, "READ(10) after SET_CONFIGURATION");
            }
        }
        eject(windows);
    }
    const uint64_t sessionMs = (usbsim_now_us() - t0) / 1000;
    CHECK(g_reads == fwR && g_writes == fwW, "the firmware touched the card while it was lent (%u reads %u writes)", g_reads - fwR, g_writes - fwW);

    // ---- 3. Exit: the card back, the computer sees an empty reader again ---------------------------------------------------------
    const uint64_t exitMs = exitMode();
    expectNoMedium("after Exit");
    idleFor(100);
    CHECK(g_hostReads + g_hostWrites > 0, "the drive moved blocks");
    unsigned hr = g_hostReads, hw = g_hostWrites;
    expectNoMedium("later");
    idleFor(20);
    CHECK(g_hostReads == hr && g_hostWrites == hw, "the drive touched the card after Exit");

    const auto after = load(argv[1]);
    size_t diff = 0;
    for (size_t b = 0; b < after.size() / 512 && after.size() == ref.size(); b++) diff += memcmp(&after[b * 512], &ref[b * 512], 512) != 0;
    CHECK(after.size() == ref.size() && diff == 0, "%zu blocks differ from macOS's image", diff);
    char* list[16] = {};
    const int n = fs.createFilesList("/Copied", 0, list, 16, 4000);
    CHECK(n >= 3, "the firmware lists %d files in /Copied", n);
    for (auto* p : list) delete[] p;
    usbsim_stats_t* st = usbsim_stats();
    CHECK(st->overruns == 0 && st->toggle_errors == 0 && st->masked_tokens == 0 && st->nested_isr == 0,
          "bus errors: %u overruns %u toggle errors %u tokens while masked %u nested ISRs", st->overruns, st->toggle_errors, st->masked_tokens, st->nested_isr);
    printf("%s%s: %u mount reads, %u drive reads %u drive writes, image equal to macOS's, firmware lists %d files in /Copied\n", mode.c_str(),
           audio ? "+audio" : "", mountReads, g_hostReads, g_hostWrites, n);
    printf("%s: session %llu ms of simulated bus time, Exit %llu ms, %u tokens, %u frames, %u retransmissions dropped, %u commands, pump drops %u\n",
           mode.c_str(), (unsigned long long)sessionMs, (unsigned long long)exitMs, st->tokens, st->sofs, st->dts_drops, d.bot.commands(), d.pump.drops());
    printf("%d checks, %d failures\n", s_checks, s_fail);
    return s_fail ? 1 : 0;
}
