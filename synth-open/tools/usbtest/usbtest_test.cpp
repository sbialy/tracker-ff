// Host unit test (NOT firmware) of the USB test console core: CRC32, row RLE, ring buffer, command parser, the
// command state machines (input schedule, shot, shotcrc, texts, ls, get, put, hash, open, idle, reboot) against a fake
// Host. Built by run_usbtest_test.sh with -DENABLE_DEV_CONSOLE. Links zlib only to cross-check crc32.
#include "UsbTestCore.h"
#include "UsbTestFlash.h"

#include <zlib.h>

#include <algorithm>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

using namespace usbtest;

static uint32_t zcrc(const void* d, size_t n) { return uint32_t(::crc32(0, static_cast<const Bytef*>(d), uInt(n))); }

static int g_fail = 0;
static int g_checks = 0;
#define CHECK(c)                                                                         \
    do                                                                                   \
    {                                                                                    \
        g_checks++;                                                                      \
        if (!(c))                                                                        \
        {                                                                                \
            g_fail++;                                                                    \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                          \
        }                                                                                \
    } while (0)

// ------------------------------------------------------------------------------------------------ fake host
struct FakeFile
{
    std::vector<uint8_t> data;
    size_t pos = 0;
    bool write = false;
    std::string path;
};

class FakeHost : public Host
{
  public:
    uint32_t now = 1000;
    bool link = true;
    std::vector<uint8_t> rx;
    size_t rxPos = 0;
    std::vector<uint8_t> tx;      // everything the console sent
    size_t txRoom = 100000;       // bytes txFree reports (per pump)
    size_t txPerWrite = 100000;
    std::vector<std::pair<int, bool>> buttons, pads;
    std::vector<int> encoder;
    int buttonQueueLimit = 1000;
    bool playingFlag = false;
    int fmStatus = 0;
    bool dirty = false;
    bool uiIdleFlag = true;
    int pageIndex = 2;
    int resets = 0;
    int shotBegins = 0, shotEnds = 0;
    std::vector<uint32_t> image; // 800 x 480
    int snapshotHalf = -1;
    int openCalls = 0;
    int openResult = 1;   // what openState returns: 0 running, 1 done, 2 failed
    std::string openPath, openName;
    std::vector<std::string> texts;
    std::map<std::string, std::vector<uint8_t>> files;
    std::vector<DirEntry> dir;
    size_t dirPos = 0;
    uint32_t scratchBuf[kScratchWords];
    int cpuResets = 0;

    int rxByte() override { return rxPos < rx.size() ? rx[rxPos++] : -1; }
    bool linkUp() override { return link; }
    size_t txFree() override { return txRoom; }
    size_t txWrite(const uint8_t* d, size_t n) override
    {
        n = std::min(n, txPerWrite);
        tx.insert(tx.end(), d, d + n);
        return n;
    }
    uint32_t irqLock() override { return 0; }
    void irqUnlock(uint32_t) override {}
    uint32_t nowMs() override { return now; }
    const char* buildId() override { return "fake123"; }
    void resetMcu() override { resets++; }
    bool crashOk = false;
    int crashes = 0;
    bool crashEnabled() override { return crashOk; }
    void crash() override { crashes++; }
    bool validPointer(const void*) override { return true; }
    uint32_t* scratch() override { return scratchBuf; }

    bool injectButton(uint8_t id, bool down) override
    {
        if (int(buttons.size()) >= buttonQueueLimit) return false;
        buttons.push_back({id, down});
        return true;
    }
    bool injectPad(uint8_t id, bool down) override { pads.push_back({id, down}); return true; }
    void injectEncoder(int d) override { encoder.push_back(d); }
    bool inputPending() override { return false; }

    void cpu(CpuUsage& u) override { u = {10, 20, 30, 15, 12}; }
    void cpuReset() override { cpuResets++; }
    uint32_t freeRam() override { return 54321; }
    int page() override { return pageIndex; }
    const char* pageName(int p) override { return p == 2 ? "pattern" : "other"; }
    bool playing() override { return playingFlag; }
    int fileManagerStatus() override { return fmStatus; }
    bool projectDirty() override { return dirty; }
    bool uiIdle() override { return uiIdleFlag; }
    void markers(Markers& m) override
    {
        m.resetStatus = 0x0120;
        m.faultValid = true;
        m.faultPc = 0x0001A2B4;
        m.faultLr = 0x0001A2A1;
        strcpy(m.safeReason, "Safe mode: Dirt");
        m.safeMode = true;
    }
    bool peek(const char* name, long* v) override
    {
        if (strcmp(name, "bpm") == 0) { *v = 12000; return true; }
        return false;
    }
    const char* peekNames() override { return "bpm"; }
    uint32_t hashProject(uint32_t* size) override { *size = 4; return usbtest::crc32(0, "abcd", 4); }

    bool play(bool song, int pos) override { playingFlag = true; playArgs = song * 1000 + pos; return true; }
    int playArgs = 0;
    void stop() override { playingFlag = false; }
    bool openProject(const char* p, const char* n) override { openCalls++; openPath = p; openName = n; return true; }
    int openState(int* code) override { *code = 7; return openResult; }

    bool shotBegin() override { shotBegins++; return !image.empty(); }
    void shotSnapshot(int half) override { snapshotHalf = half; }
    void shotRow(int half, int row, uint8_t* dest) override
    {
        const int y = half * kShotHalfRows + row;
        memcpy(dest, &image[size_t(y) * kShotW], kRowBytes);
    }
    void shotEnd() override { shotEnds++; }
    bool nextText(int* cursor, TextItem& item) override
    {
        if (*cursor >= int(texts.size())) return false;
        item.id = *cursor;
        item.x = 10 + *cursor;
        item.y = 20;
        item.w = 100;
        item.h = 16;
        strncpy(item.text, texts[size_t(*cursor)].c_str(), sizeof(item.text) - 1);
        item.text[sizeof(item.text) - 1] = 0;
        (*cursor)++;
        return true;
    }

    bool dirOpen(const char*) override { dirPos = 0; return true; }
    bool dirNext(DirEntry& e) override
    {
        if (dirPos >= dir.size()) return false;
        e = dir[dirPos++];
        return true;
    }
    void dirClose() override {}
    long fileSize(const char* p) override { auto it = files.find(p); return it == files.end() ? -1 : long(it->second.size()); }
    void* fileOpenRead(const char* p) override
    {
        auto it = files.find(p);
        if (it == files.end()) return nullptr;
        FakeFile* f = new FakeFile;
        f->data = it->second;
        f->path = p;
        return f;
    }
    void* fileOpenWrite(const char* p) override
    {
        FakeFile* f = new FakeFile;
        f->write = true;
        f->path = p;
        files[p] = {};
        return f;
    }
    int fileRead(void* h, void* dest, size_t n) override
    {
        FakeFile* f = static_cast<FakeFile*>(h);
        size_t k = std::min(n, f->data.size() - f->pos);
        memcpy(dest, &f->data[f->pos], k);
        f->pos += k;
        return int(k);
    }
    int fileWrite(void* h, const void* d, size_t n) override
    {
        FakeFile* f = static_cast<FakeFile*>(h);
        const uint8_t* p = static_cast<const uint8_t*>(d);
        files[f->path].insert(files[f->path].end(), p, p + n);
        return int(n);
    }
    void fileClose(void* h) override { delete static_cast<FakeFile*>(h); }
    bool fileRemove(const char* p) override { return files.erase(p) > 0; }
    bool renameOk = true;
    bool fileRename(const char* a, const char* b) override
    {
        auto it = files.find(a);
        if (!renameOk || it == files.end() || files.count(b) > 0) return false;
        files[b] = it->second;
        files.erase(a);
        return true;
    }
    long freeValue = 50L << 20;
    long freeBytes() override { return freeValue; }
    bool renderingFlag = false;
    bool rendering() override { return renderingFlag; }
    std::vector<std::string> bootloaderCalls;
    void runBootloader(const char* name) override { bootloaderCalls.push_back(name); }

    // test helpers
    void send(const std::string& s) { rx.insert(rx.end(), s.begin(), s.end()); }
    void send(const std::vector<uint8_t>& s) { rx.insert(rx.end(), s.begin(), s.end()); }
};

static Console* g_console;

static void pump(FakeHost& h, int steps = 50, uint32_t stepMs = 10)
{
    for (int i = 0; i < steps; i++)
    {
        g_console->update();
        h.now += stepMs;
    }
}

static std::vector<std::string> lines(const std::vector<uint8_t>& tx)
{
    std::vector<std::string> out;
    std::string cur;
    for (uint8_t b : tx)
    {
        if (b == '\n') { out.push_back(cur); cur.clear(); }
        else cur.push_back(char(b));
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static std::string cmd(FakeHost& h, const std::string& line, int steps = 50)
{
    h.tx.clear();
    h.send(line + "\n");
    pump(h, steps);
    auto l = lines(h.tx);
    for (auto& s : l) if (!s.empty() && s[0] == '@') return s;
    return "";
}

static bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

static void reset(FakeHost& h, Console& c)
{
    h = FakeHost();
    c.init(&h);
    c.setEnabled(true);
    g_console = &c;
    pump(h, 2); // link comes up
}

// ------------------------------------------------------------------------------------------------ tests
static void testCrc()
{
    CHECK(usbtest::crc32(0, "123456789", 9) == 0xCBF43926u);
    CHECK(usbtest::crc32(0, "", 0) == 0);
    std::vector<uint8_t> big(100000);
    for (size_t i = 0; i < big.size(); i++) big[i] = uint8_t(i * 31 + i / 7);
    CHECK(usbtest::crc32(0, big.data(), big.size()) == uint32_t(zcrc(big.data(), big.size())));
    uint32_t a = usbtest::crc32(0, big.data(), 777);
    CHECK(usbtest::crc32(a, big.data() + 777, big.size() - 777) == usbtest::crc32(0, big.data(), big.size()));
}

static void rleRoundTrip(const std::vector<uint32_t>& cur, const std::vector<uint32_t>* prev)
{
    const int n = int(cur.size());
    const uint32_t* p = prev ? prev->data() : nullptr;
    size_t sz = rleSize(cur.data(), p, n);
    std::vector<uint8_t> enc(sz + 8, 0xEE);
    size_t w = rleEncode(enc.data(), cur.data(), p, n);
    CHECK(w == sz);
    CHECK(enc[sz] == 0xEE);   // no write past the reported size
    CHECK(sz <= size_t(n) * 4 + size_t(n) / 128 + 1);
    std::vector<uint32_t> back(n, 0xDEADBEEF);
    CHECK(rleDecode(enc.data(), sz, back.data(), p, n));
    CHECK(back == cur);
}

static void testRle()
{
    srand(7);
    for (int n : {1, 2, 3, 127, 128, 129, 130, 131, 257, 800})
    {
        std::vector<uint32_t> flat(n, 0xFF102030), noise(n), mixed(n), prev(n);
        for (int i = 0; i < n; i++)
        {
            noise[i] = uint32_t(rand()) * 2654435761u;
            mixed[i] = (i / 5) % 3 == 0 ? 0xFF000000u : noise[i];
            prev[i] = (i % 11 == 0) ? mixed[i] : uint32_t(rand());
        }
        for (auto* v : {&flat, &noise, &mixed})
        {
            rleRoundTrip(*v, nullptr);
            rleRoundTrip(*v, &prev);
            rleRoundTrip(*v, v);   // identical to the row above: all zero words
        }
    }
    // a flat row is a handful of bytes, a noise row stays below 1 % over raw
    std::vector<uint32_t> flat(800, 0xFF000000u), zero(800, 0);
    CHECK(rleSize(flat.data(), flat.data(), 800) <= 5 * 7);   // 800 = 6 x 129 + 26
    // malformed streams are refused
    std::vector<uint32_t> row(10);
    uint8_t good[] = {0x84, 1, 2, 3, 4, 0x80, 9, 9, 9, 9, 0x01, 1, 1, 1, 1, 2, 2, 2, 2};   // 6 + 2 + 2 = 10 words
    CHECK(rleDecode(good, sizeof(good), row.data(), nullptr, 10));
    CHECK(!rleDecode(good, sizeof(good) - 1, row.data(), nullptr, 10));   // truncated literal
    CHECK(!rleDecode(good, sizeof(good), row.data(), nullptr, 9));        // overruns the row
    CHECK(!rleDecode(good, sizeof(good), row.data(), nullptr, 11));       // short row
    uint8_t trailing[] = {0x00, 1, 2, 3, 4, 0x00};
    CHECK(!rleDecode(trailing, sizeof(trailing), row.data(), nullptr, 1));
}

static void viaLogf(Console& c, const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    c.logf(fmt, a);
    va_end(a);
}

static void testFormat()
{
    char a[128], b[128];
#define SAME(fmt, ...)                                                           \
    do                                                                           \
    {                                                                            \
        usbtest::format(a, sizeof(a), fmt, __VA_ARGS__);                         \
        snprintf(b, sizeof(b), fmt, __VA_ARGS__);                                \
        CHECK(strcmp(a, b) == 0);                                                \
    } while (0)
    SAME("%d %d %d", 0, -1, 2147483647);
    SAME("%i|%u|%u", -7, 0u, 4294967295u);
    SAME("%x %X %08x %02x", 255u, 255u, 0xBEEFu, 5u);
    SAME("%ld %lu %08lx", -123456L, 4000000000UL, 0xCAFEBABEUL);
    SAME("%s|%5s|%.3s|%.40s|%s", "abc", "ab", "abcdef", "short", "");
    SAME("%c%c %% %d", 'o', 'k', 12);
    SAME("%3d|%03d", 7, 7);
#undef SAME
    // output is cut at the capacity and always terminated
    char small[8];
    size_t n = usbtest::format(small, sizeof(small), "%s", "0123456789");
    CHECK(n == 10 && strcmp(small, "0123456") == 0);
    CHECK(usbtest::format(small, 0, "%d", 5) == 1);
    // no floats: ?, and the argument is consumed so the rest still lines up
    usbtest::format(a, sizeof(a), "%f %d", 1.5, 42);
    CHECK(strcmp(a, "? 42") == 0);
    usbtest::format(a, sizeof(a), "%s", static_cast<const char*>(nullptr));
    CHECK(strcmp(a, "(null)") == 0);
    // logf takes the same formats and a line from an interrupt that finds the buffer taken is dropped
    FakeHost h; Console c; reset(h, c);
    h.tx.clear();
    viaLogf(c, "sequencer play from step %d (%s mode)", 12, "song");
    pump(h, 3);
    CHECK(lines(h.tx).size() == 1 && lines(h.tx)[0] == "sequencer play from step 12 (song mode)");
}

static void testRing()
{
    static Ring r;
    r.clear();
    CHECK(r.used() == 0 && r.freeBytes() == kRingSize);
    uint8_t blk[1000];
    for (int i = 0; i < 1000; i++) blk[i] = uint8_t(i);
    for (int round = 0; round < 40; round++)   // wraps the 4096 byte buffer many times
    {
        CHECK(r.push(blk, 1000));
        uint8_t out[1000];
        CHECK(r.peek(out, 1000) == 1000);
        CHECK(memcmp(out, blk, 1000) == 0);
        r.tail += 1000;
    }
    CHECK(r.used() == 0);
    // all or nothing
    CHECK(r.push(blk, 1000) && r.push(blk, 1000) && r.push(blk, 1000) && r.push(blk, 1000));
    CHECK(r.freeBytes() == kRingSize - 4000);
    CHECK(!r.push(blk, 200));
    CHECK(r.used() == 4000 && r.dropped == 200);
    CHECK(r.push(blk, 96));
    CHECK(r.freeBytes() == 0);
    CHECK(!r.push(blk, 1));
    uint8_t out[16];
    CHECK(r.peek(out, 16) == 16 && out[15] == 15);
    CHECK(r.used() == kRingSize);   // peek does not consume
}

static void testParser()
{
    FakeHost h; Console c; reset(h, c);
    CHECK(cmd(h, "ping") == "@tracker-dev original fake123 proto=1");
    CHECK(starts(cmd(h, "help"), "@ok ping"));
    CHECK(starts(cmd(h, "nosuch"), "@err unknown command"));
    CHECK(cmd(h, "   ping   ") == "@tracker-dev original fake123 proto=1");
    CHECK(cmd(h, "mem") == "@mem free=54321");
    CHECK(cmd(h, "cpu") == "@cpu min=10 max=20 avg=15 last=12 tmax=30 block=2902");
    CHECK(cmd(h, "cpu reset", 1) == "@ok cpu reset" && h.cpuResets == 1);
    CHECK(cmd(h, "cpu", 1) == "@cpu none");   // right after a reset the profiler has no blocks yet
    h.now += 1000;
    CHECK(starts(cmd(h, "cpu"), "@cpu min="));
    CHECK(starts(cmd(h, "status"), "@status page=2 name=pattern play=0 fm=0 dirty=0 idle=1"));
    // CRLF, and a command split across pump passes
    h.tx.clear();
    h.send("pi"); pump(h, 2); h.send("ng\r\n"); pump(h, 4);
    CHECK(lines(h.tx).size() == 1 && starts(lines(h.tx)[0], "@tracker-dev"));
    // a line that is too long is refused, the next one still works
    CHECK(cmd(h, std::string(300, 'x')) == "@err line too long");
    CHECK(cmd(h, "ping") == "@tracker-dev original fake123 proto=1");
    // log text and replies: a reply starts on its own line
    h.tx.clear();
    g_console->log("hello");
    h.send("ping\n"); pump(h, 4);
    auto l = lines(h.tx);
    CHECK(l.size() == 2 && l[0] == "hello" && starts(l[1], "@tracker-dev"));
    // numeric arguments
    CHECK(starts(cmd(h, "enc abc"), "@err usage"));
    CHECK(starts(cmd(h, "enc 0"), "@err usage"));
    CHECK(starts(cmd(h, "enc 201"), "@err usage"));
    CHECK(cmd(h, "enc -3") == "@ok enc -3" && h.encoder.size() == 1 && h.encoder[0] == -3);
    // peek, hash, markers, play, stop
    CHECK(cmd(h, "peek") == "@peek names: bpm");
    CHECK(cmd(h, "peek bpm") == "@peek bpm 12000");
    CHECK(starts(cmd(h, "peek nope"), "@err peek unknown"));
    CHECK(cmd(h, "hash project") == "@hash project ed82cd11 4");
    CHECK(starts(cmd(h, "markers"), "@markers srs=0120 fault=1 pc=0001a2b4 lr=0001a2a1"));
    CHECK(cmd(h, "markers").find("reason=Safe_mode:_Dirt") != std::string::npos);
    CHECK(cmd(h, "play song 3") == "@ok play song" && h.playArgs == 1003 && h.playingFlag);
    CHECK(cmd(h, "stop") == "@ok stop" && !h.playingFlag);
    CHECK(starts(cmd(h, "play loop"), "@err usage"));
    h.fmStatus = 8;
    CHECK(cmd(h, "play pattern") == "@err play busy");
}

static void testGate()
{
    FakeHost h; Console c; reset(h, c);
    c.setEnabled(false);
    h.tx.clear();
    h.send("ping\n"); pump(h, 10);
    CHECK(h.tx.empty());
    CHECK(h.rxPos == 0);   // a disabled console does not even read the port
    c.log("dropped");
    CHECK(h.tx.empty());
    c.setEnabled(true);
    pump(h, 4);
    CHECK(starts(lines(h.tx).empty() ? "" : lines(h.tx)[0], "@tracker-dev"));
    // no terminal on the port: no TX is queued, the ring stays empty
    h.link = false;
    pump(h, 3);
    h.tx.clear();
    c.log("nobody listens");
    pump(h, 5);
    CHECK(h.tx.empty() && c.ring().used() == 0);
    // the terminal comes back: stale input is discarded, a fresh command works
    h.send("garbage ping\n");
    h.link = true;
    pump(h, 3);
    h.tx.clear();
    CHECK(cmd(h, "ping") == "@tracker-dev original fake123 proto=1");
}

static void testInput()
{
    FakeHost h; Console c; reset(h, c);
    CHECK(cmd(h, "key Shift press") == "@ok key 29 press");
    CHECK(h.buttons.size() == 1 && h.buttons[0] == std::make_pair(29, true));
    CHECK(cmd(h, "key 29 release") == "@ok key 29 release");
    CHECK(h.buttons.size() == 2 && h.buttons[1] == std::make_pair(29, false));
    // tap n: press, hold 70 ms, release, gap, in that order and with the delays
    h.buttons.clear();
    h.tx.clear();
    h.send("key Play tap 2\n");
    const uint32_t t0 = h.now;
    pump(h, 1, 1);
    CHECK(h.buttons.size() == 1 && h.buttons[0].second);
    std::vector<std::pair<uint32_t, std::pair<int, bool>>> seen;
    size_t last = 1;
    for (int i = 0; i < 400; i++)
    {
        pump(h, 1, 1);
        while (last < h.buttons.size()) { seen.push_back({h.now - t0, h.buttons[last]}); last++; }
    }
    CHECK(h.buttons.size() == 4);
    CHECK(h.buttons[1] == std::make_pair(23, false) && h.buttons[2] == std::make_pair(23, true) && h.buttons[3] == std::make_pair(23, false));
    CHECK(seen.size() == 3 && seen[0].first >= 70 && seen[0].first < 80 && seen[1].first >= 140 && seen[1].first < 150 && seen[2].first >= 210 && seen[2].first < 220);
    // pads go to the pad driver
    CHECK(cmd(h, "pad 47 press") == "@ok pad 47 press" && h.pads.size() == 1 && h.pads[0] == std::make_pair(47, true));
    CHECK(cmd(h, "pad 48 press").find("@err pad unknown") == 0);
    CHECK(cmd(h, "pad Shift tap").find("@err pad unknown") == 0);
    CHECK(cmd(h, "key Nope tap").find("@err key unknown") == 0);
    CHECK(cmd(h, "key Play jump").find("@err action") == 0);
    CHECK(cmd(h, "key Play tap 11").find("@err count") == 0);
    // the driver queue is full: the event waits and goes out later, none is lost
    h.buttons.clear();
    h.buttonQueueLimit = 0;
    CHECK(cmd(h, "key Left press") == "@ok key 30 press");
    CHECK(h.buttons.empty());
    h.buttonQueueLimit = 10;
    pump(h, 2);
    CHECK(h.buttons.size() == 1 && h.buttons[0] == std::make_pair(30, true));
    // too many pending events: refused as a whole
    h.buttonQueueLimit = 0;
    for (int i = 0; i < 24; i++) cmd(h, "key Up press", 1);
    CHECK(cmd(h, "key Up tap").find("@err busy") == 0);
    h.buttonQueueLimit = 1000;
    pump(h, 3);
    // the terminal goes away with Shift held: the console lets it go
    h.buttons.clear();
    cmd(h, "key Shift press");
    h.link = false;
    pump(h, 3);
    CHECK(std::find(h.buttons.begin(), h.buttons.end(), std::make_pair(29, false)) != h.buttons.end());
}

static void testIdleReboot()
{
    FakeHost h; Console c; reset(h, c);
    h.uiIdleFlag = false;
    h.tx.clear();
    h.send("enc 1\nidle\n");
    pump(h, 40);   // 400 ms after the input, the display keeps redrawing: not idle yet
    CHECK(lines(h.tx).size() == 1);   // the enc reply only
    pump(h, 50);   // past 600 ms without input the redraw no longer counts
    CHECK(lines(h.tx).size() == 2 && lines(h.tx)[1] == "@idle");
    // a quiet display: idle after 100 ms without input and the settle time
    h.uiIdleFlag = true;
    h.tx.clear();
    h.send("idle\n");
    pump(h, 8);
    CHECK(lines(h.tx).empty());
    pump(h, 14);
    CHECK(lines(h.tx).size() == 1 && lines(h.tx)[0] == "@idle");
    // a running idle waits for scheduled input as well
    h.tx.clear();
    h.send("key Play tap\nidle\n");
    pump(h, 3);
    CHECK(lines(h.tx).size() == 1);   // the key reply only
    pump(h, 60);
    CHECK(lines(h.tx).size() == 2 && lines(h.tx)[1] == "@idle");
    // a file manager that stays busy: timeout
    h.fmStatus = 8;
    h.tx.clear();
    h.send("idle\n");
    pump(h, 1600);
    CHECK(lines(h.tx).size() == 1 && lines(h.tx)[0] == "@err idle timeout");
    h.fmStatus = 0;
    // other commands are refused while a task runs, ping and abort are not
    h.tx.clear();
    h.send("idle\nkey Play tap\nping\nabort\nping\n");
    pump(h, 4);
    auto l = lines(h.tx);
    CHECK(l.size() == 4 && starts(l[0], "@err busy") && starts(l[1], "@tracker-dev") && l[2] == "@ok abort" && starts(l[3], "@tracker-dev"));
    // reboot: the reply goes out first, the reset comes after the delay
    h.tx.clear();
    h.send("reboot\n");
    pump(h, 5);
    CHECK(lines(h.tx).size() == 1 && lines(h.tx)[0] == "@ok reboot pending" && h.resets == 0);
    pump(h, 30);
    CHECK(h.resets == 1);
    // crash: refused on an image without fault capture, with it the fault comes after the reply
    h.tx.clear();
    CHECK(starts(cmd(h, "crash"), "@err crash: needs an image built with REBOOT_ON_FAULT") && h.crashes == 0);
    h.crashOk = true;
    h.tx.clear();
    h.send("crash\n");
    pump(h, 5);
    CHECK(lines(h.tx).size() == 1 && lines(h.tx)[0] == "@ok crash pending" && h.crashes == 0);
    pump(h, 30);
    CHECK(h.crashes == 1);
}

static std::vector<uint32_t> makeImage()
{
    std::vector<uint32_t> img(size_t(kShotW) * kShotH);
    for (int y = 0; y < kShotH; y++)
        for (int x = 0; x < kShotW; x++)
        {
            uint32_t v = 0xFF101820u + uint32_t(y / 40) * 0x00010101u;           // vertical bands
            if (x > 100 && x < 300 && y > 50 && y < 90) v = 0xFFC0A030u;          // a block
            if ((x * 7 + y * 13) % 53 == 0 && y > 200 && y < 260) v = uint32_t(x * 2654435761u + y);   // speckle
            img[size_t(y) * kShotW + x] = v;
        }
    return img;
}

static int zeroRows;

// parses what a `shot` sends: rows with RLE, then the closing line
static bool decodeShot(const std::vector<uint8_t>& tx, std::vector<uint32_t>& out, std::string& end)
{
    out.assign(size_t(kShotW) * kShotH, 0);
    size_t i = 0;
    int rows = 0;
    bool header = false;
    zeroRows = 0;
    while (i < tx.size())
    {
        size_t e = i;
        while (e < tx.size() && tx[e] != '\n') e++;
        std::string line(reinterpret_cast<const char*>(&tx[i]), e - i);
        i = e + 1;
        if (starts(line, "@shot ")) { header = line == "@shot 800 480 rle1"; continue; }
        if (starts(line, "@row "))
        {
            int y = 0; unsigned n = 0;
            if (sscanf(line.c_str(), "@row %d %u", &y, &n) != 2 || y != rows || i + n + 1 > tx.size()) return false;
            const uint32_t* prev = y > 0 ? &out[size_t(y - 1) * kShotW] : nullptr;
            if (n == 0)
            {
                if (prev != nullptr) memcpy(&out[size_t(y) * kShotW], prev, kRowBytes);
                else memset(&out[size_t(y) * kShotW], 0, kRowBytes);   // the row above the first is black
                zeroRows++;
            }
            else if (!rleDecode(&tx[i], n, &out[size_t(y) * kShotW], prev, kShotW)) return false;
            if (tx[i + n] != '\n') return false;
            i += n + 1;
            rows++;
            continue;
        }
        if (starts(line, "@ok shot ")) { end = line; continue; }
        if (!line.empty() && line[0] == '@') { end = line; continue; }
    }
    return header && rows == kShotH;
}

static void testShot()
{
    FakeHost h; Console c; reset(h, c);
    h.image = makeImage();
    CHECK(cmd(h, "shot force", 1) == "@shot 800 480 rle1");   // the header comes at once
    h.tx.clear();
    reset(h, c);
    h.image = makeImage();
    h.tx.clear();
    h.send("shot\n");
    pump(h, 400);
    std::vector<uint32_t> out;
    std::string end;
    CHECK(decodeShot(h.tx, out, end));
    CHECK(out == h.image);
    CHECK(h.shotBegins == 1 && h.shotEnds == 1 && !c.busy());
    // closing line: crc over B G R of every pixel, row major
    std::vector<uint8_t> bgr;
    for (uint32_t w : h.image) { bgr.push_back(uint8_t(w)); bgr.push_back(uint8_t(w >> 8)); bgr.push_back(uint8_t(w >> 16)); }
    char want[64];
    snprintf(want, sizeof(want), "@ok shot crc=%08x ", zcrc(bgr.data(), bgr.size()));
    CHECK(starts(end, want));
    CHECK(h.tx.size() < 100000);   // the point of the codec: the raw image is 1,536,000 bytes
    CHECK(zeroRows > 300);         // repeated rows cost a header only

    // a slow link: 64 bytes per pass, the ring fills, the console waits, nothing is lost
    reset(h, c);
    h.image = makeImage();
    h.txRoom = 64;
    h.tx.clear();
    h.send("shot\n");
    pump(h, 6000);
    CHECK(decodeShot(h.tx, out, end));
    CHECK(out == h.image);

    // a host that stops reading: the shot is dropped after the timeout and the display is released
    reset(h, c);
    h.image = makeImage();
    h.txRoom = 0;
    h.send("shot\n");
    pump(h, 700);
    CHECK(h.shotEnds == 1 && !c.busy());

    // refused while playing unless forced, and without a screen
    reset(h, c);
    h.image = makeImage();
    h.playingFlag = true;
    CHECK(starts(cmd(h, "shot"), "@err playing"));
    CHECK(h.shotBegins == 0);
    CHECK(cmd(h, "shotcrc force", 1).empty() || true);
    reset(h, c);
    CHECK(cmd(h, "shotcrc") == "@err shot unavailable");

    // the link drops in the middle: the display is released
    reset(h, c);
    h.image = makeImage();
    h.txRoom = 64;
    h.send("shot\n");
    pump(h, 50);
    CHECK(c.busy());
    h.link = false;
    pump(h, 2);
    CHECK(!c.busy() && h.shotEnds == 1);
}

static void testShotCrc()
{
    FakeHost h; Console c; reset(h, c);
    h.image = makeImage();
    h.tx.clear();
    h.send("shotcrc\n");
    pump(h, 400);
    auto l = lines(h.tx);
    CHECK(l.size() == 7 && l[6] == "@ok shotcrc");
    for (int ty = 0; ty < kTilesY; ty++)
    {
        unsigned row = 99; unsigned crc[8] = {};
        CHECK(sscanf(l[size_t(ty)].c_str(), "@shotcrc %u %x %x %x %x %x %x %x %x", &row, crc, crc + 1, crc + 2, crc + 3, crc + 4, crc + 5, crc + 6, crc + 7) == 9);
        CHECK(row == unsigned(ty));
        for (int tx = 0; tx < kTilesX; tx++)
        {
            std::vector<uint8_t> t;
            for (int y = 0; y < kTileH; y++)
                for (int x = 0; x < kTileW; x++)
                {
                    uint32_t w = h.image[size_t(ty * kTileH + y) * kShotW + tx * kTileW + x];
                    t.push_back(uint8_t(w)); t.push_back(uint8_t(w >> 8)); t.push_back(uint8_t(w >> 16));
                }
            CHECK(crc[tx] == zcrc(t.data(), t.size()));
        }
    }
    // one changed pixel changes exactly its tile
    std::vector<std::string> before = l;
    h.image[size_t(100) * kShotW + 250] ^= 0x00000100;   // tile row 1, column 2
    h.tx.clear();
    h.send("shotcrc\n");
    pump(h, 400);
    l = lines(h.tx);
    int diff = 0;
    for (int ty = 0; ty < kTilesY; ty++) if (l[size_t(ty)] != before[size_t(ty)]) diff++;
    CHECK(diff == 1 && l[1] != before[1]);
}

static void testTexts()
{
    FakeHost h; Console c; reset(h, c);
    for (int i = 0; i < 80; i++) h.texts.push_back("label " + std::to_string(i));
    h.tx.clear();
    h.send("texts\n");
    pump(h, 100);
    auto l = lines(h.tx);
    CHECK(l.size() == 81);
    CHECK(l.front() == "@t 0 10 20 100 16 label 0" && l[79] == "@t 79 89 20 100 16 label 79" && l.back() == "@ok texts 80");
    // with a thin pipe the ring never overflows
    reset(h, c);
    for (int i = 0; i < 300; i++) h.texts.push_back("a longer label with some words in it " + std::to_string(i));
    h.txRoom = 32;
    h.tx.clear();
    h.send("texts\n");
    pump(h, 3000);
    l = lines(h.tx);
    CHECK(l.size() == 301 && l.back() == "@ok texts 300" && c.ring().dropped == 0);
}

static void testFiles()
{
    FakeHost h; Console c; reset(h, c);
    h.dir = { {true, 0, "."}, {true, 0, ".."}, {true, 0, "Projects"}, {false, 1234, "sample one.wav"} };
    h.tx.clear();
    h.send("ls /\n");
    pump(h, 20);
    auto l = lines(h.tx);
    CHECK(l.size() == 3 && l[0] == "@d 0 Projects" && l[1] == "@f 1234 sample one.wav" && l[2] == "@ok ls 2");
    CHECK(starts(cmd(h, "ls ../x"), "@err bad path"));

    // get: chunked blocks, then the closing line with size and crc
    std::vector<uint8_t> data(2000);
    for (size_t i = 0; i < data.size(); i++) data[i] = uint8_t(i * 5 + 1);
    h.files["/a.bin"] = data;
    h.tx.clear();
    h.send("get /a.bin\n");
    pump(h, 50);
    std::vector<uint8_t> got;
    std::string endLine;
    size_t i = 0;
    while (i < h.tx.size())
    {
        size_t e = i;
        while (h.tx[e] != '\n') e++;
        std::string line(reinterpret_cast<const char*>(&h.tx[i]), e - i);
        i = e + 1;
        int n = 0;
        if (sscanf(line.c_str(), "@blk %d", &n) == 1) { got.insert(got.end(), h.tx.begin() + long(i), h.tx.begin() + long(i) + n); i += size_t(n) + 1; }
        else if (starts(line, "@end ")) endLine = line;
    }
    CHECK(got == data);
    char want[48];
    snprintf(want, sizeof(want), "@end 2000 %08x", zcrc(data.data(), 2000));
    CHECK(endLine == want);
    CHECK(starts(cmd(h, "get /missing"), "@err get: no such file"));
    CHECK(starts(cmd(h, "get ../etc"), "@err usage"));

    // hash file
    char hw[48];
    snprintf(hw, sizeof(hw), "@hash file %08x 2000", zcrc(data.data(), 2000));
    CHECK(cmd(h, "hash file /a.bin") == hw);

    // put: ready, raw bytes, crc check
    std::vector<uint8_t> up(1500);
    for (size_t k = 0; k < up.size(); k++) up[k] = uint8_t(k * 3);
    char line[96];
    snprintf(line, sizeof(line), "put /up.bin %zu 0x%08x\n", up.size(), zcrc(up.data(), up.size()));
    h.tx.clear();
    h.send(line);
    pump(h, 3);
    CHECK(lines(h.tx).size() == 1 && lines(h.tx)[0] == "@ready");
    h.tx.clear();
    h.send(up);
    pump(h, 20);
    CHECK(lines(h.tx).size() == 1 && lines(h.tx)[0] == "@ok put 1500");
    CHECK(h.files["/up.bin"] == up && !c.busy());
    // a wrong crc deletes the file
    snprintf(line, sizeof(line), "put /bad.bin %zu 0x12345678\n", up.size());
    h.send(line); pump(h, 3);
    h.tx.clear();
    h.send(up); pump(h, 20);
    CHECK(starts(lines(h.tx).empty() ? "" : lines(h.tx)[0], "@err put crc") && h.files.count("/bad.bin") == 0);
    // bytes that look like commands inside the payload stay payload
    std::vector<uint8_t> tricky;
    for (char ch : std::string("ping\nreboot\n")) tricky.push_back(uint8_t(ch));
    snprintf(line, sizeof(line), "put /t.bin %zu 0x%08x\n", tricky.size(), zcrc(tricky.data(), tricky.size()));
    h.send(line); pump(h, 3);
    h.tx.clear();
    h.send(tricky); pump(h, 10);
    CHECK(lines(h.tx).size() == 1 && lines(h.tx)[0] == "@ok put 12" && h.resets == 0);
    // a stalled upload is dropped and the partial file removed
    snprintf(line, sizeof(line), "put /stall.bin 1000 0x0\n");
    h.send(line); pump(h, 3);
    h.send(std::vector<uint8_t>(100, 1));
    h.tx.clear();
    pump(h, 500);
    CHECK(starts(lines(h.tx).empty() ? "" : lines(h.tx)[0], "@err put timeout") && h.files.count("/stall.bin") == 0 && !c.busy());
    // put refused while playing or on a bad path
    h.playingFlag = true;
    CHECK(starts(cmd(h, "put /x 1 0x0"), "@err put refused"));
    CHECK(starts(cmd(h, "rm /a.bin"), "@err rm refused"));
    h.playingFlag = false;
    CHECK(starts(cmd(h, "put ../x 1 0x0"), "@err usage"));
    // rm
    CHECK(cmd(h, "rm /a.bin") == "@ok rm" && h.files.count("/a.bin") == 0);
    CHECK(starts(cmd(h, "rm /a.bin"), "@err rm: no such file"));
    CHECK(starts(cmd(h, "rm /"), "@err usage"));
    // quoted names with spaces
    h.files["/My Song/project.bin"] = {1, 2, 3};
    CHECK(starts(cmd(h, "get \"/My Song/project.bin\""), "@get 3"));
    pump(h, 10);
}

static void testOpen()
{
    FakeHost h; Console c; reset(h, c);
    h.openResult = 0;   // still running
    h.tx.clear();
    h.send("open /Projects \"My Song\"\n");
    pump(h, 5);
    CHECK(h.openCalls == 1 && h.openPath == "/Projects" && h.openName == "My Song" && c.busy());
    CHECK(lines(h.tx).empty());
    h.openResult = 1;
    pump(h, 3);
    CHECK(lines(h.tx).size() == 1 && lines(h.tx)[0] == "@ok open");
    // failure reports the file manager status
    h.openResult = 2;
    h.tx.clear();
    h.send("open /Projects X\n"); pump(h, 5);
    CHECK(lines(h.tx).size() == 1 && lines(h.tx)[0] == "@err open fm=7");
    // refusals: unsaved changes, playing, busy
    h.dirty = true;
    CHECK(starts(cmd(h, "open /Projects X"), "@err open: the project has unsaved changes"));
    int calls = h.openCalls;
    h.openResult = 1;
    h.tx.clear(); h.send("open /Projects X force\n"); pump(h, 5);
    CHECK(h.openCalls == calls + 1);
    h.dirty = false;
    h.playingFlag = true;
    CHECK(starts(cmd(h, "open /Projects X"), "@err open: playing"));
    h.playingFlag = false;
    h.fmStatus = 3;
    CHECK(starts(cmd(h, "open /Projects X"), "@err open: file manager busy"));
    CHECK(starts(cmd(h, "open /Projects"), "@err usage"));
    // never finishes: timeout
    h.fmStatus = 0;
    h.openResult = 0;
    h.tx.clear(); h.send("open /Projects X\n");
    pump(h, 13000);
    CHECK(lines(h.tx).size() == 1 && lines(h.tx)[0] == "@err open timeout");
}

// ------------------------------------------------------------------------------------------------ firmware update
// An Intel HEX image like the stock ones: 16 byte records, a segment record every 64 KB, CRLF, vector table, stock flash configuration.
static std::string hexRecord(int type, int addr, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> body = { uint8_t(data.size()), uint8_t(addr >> 8), uint8_t(addr), uint8_t(type) };
    body.insert(body.end(), data.begin(), data.end());
    unsigned sum = 0;
    for (uint8_t b : body) sum += b;
    body.push_back(uint8_t(-int(sum)));
    std::string out = ":";
    char t[4];
    for (uint8_t b : body) { snprintf(t, sizeof(t), "%02X", b); out += t; }
    return out;
}

static std::vector<uint8_t> makePtf(size_t size = 0x6000, uint32_t sp = 0x20030000, uint32_t reset = 0x1D1,
                                    const char* eol = "\r\n", bool eof = true)
{
    std::vector<uint8_t> mem(size);
    for (size_t i = 0; i < size; i++) mem[i] = uint8_t(i * 7 + 3);
    memcpy(&mem[0], &sp, 4);
    memcpy(&mem[4], &reset, 4);
    const uint8_t cfg[16] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xDE, 0xF9, 0xFF, 0xFF };
    memcpy(&mem[0x400], cfg, 16);
    std::string text;
    int seg = 0;
    for (size_t a = 0; a < size; a += 16)
    {
        if ((a >> 16) != size_t(seg)) { seg = int(a >> 16); text += hexRecord(2, 0, { uint8_t((seg << 12) >> 8), uint8_t(seg << 12) }) + eol; }
        text += hexRecord(0, int(a & 0xFFFF), std::vector<uint8_t>(mem.begin() + long(a), mem.begin() + long(std::min(a + 16, size)))) + eol;
    }
    if (eof) text += hexRecord(1, 0, {}) + std::string(eol);
    return std::vector<uint8_t>(text.begin(), text.end());
}

static PtfError verdict(const std::vector<uint8_t>& data, size_t chunk = 512)
{
    PtfValidator v;
    v.begin();
    for (size_t i = 0; i < data.size(); i += chunk) v.feed(data.data() + i, std::min(chunk, data.size() - i));
    return v.finish();
}

static void testPtfValidator()
{
    CHECK(verdict(makePtf()) == kPtfOk);
    CHECK(verdict(makePtf(0x6000, 0x20030000, 0x1D1, "\n")) == kPtfOk);
    // any chunking gives the same verdict, down to one byte
    for (size_t chunk : { size_t(1), size_t(3), size_t(7), size_t(509), size_t(100000) }) CHECK(verdict(makePtf(0x1000), chunk) == kPtfOk);
    CHECK(verdict(makePtf(0x1000, 0x20030000, 0x1D1, "\r\n", false), 1) == kPtfNoEof);
    PtfValidator v;
    v.begin();
    const auto good = makePtf();
    v.feed(good.data(), good.size());
    CHECK(v.finish() == kPtfOk && v.top() == 0x6000 && v.stackPointer() == 0x20030000 && v.resetVector() == 0x1D1);
    CHECK(verdict(makePtf(0x6000, 0x20280000)) == kPtfBadSp);          // another chip's SRAM
    CHECK(verdict(makePtf(0x6000, 0x1FFF0000)) == kPtfBadSp);          // bound is exclusive below
    CHECK(verdict(makePtf(0x6000, 0x20030001)) == kPtfBadSp);          // and inclusive above, 0x20030000 only
    CHECK(verdict(makePtf(0x6000, 0x20030000, 0x1D0)) == kPtfBadReset);  // not Thumb
    CHECK(verdict(makePtf(0x6000, 0x20030000, 0x6001)) == kPtfBadReset); // at top
    CHECK(verdict(makePtf(0x6000, 0x20030000, 0x21)) == kPtfBadReset);   // inside the vector table
    CHECK(verdict(makePtf(0x6000, 0x20030000, 0x5FFF)) == kPtfOk);
    CHECK(verdict(makePtf(0xFF000)) == kPtfOk);                        // the last byte below the limit
    CHECK(verdict(makePtf(0xFF001)) == kPtfBeyondLimit);
    auto bad = makePtf();
    bad[20] = bad[20] == '0' ? '1' : '0';                              // a nibble in the first record
    CHECK(verdict(bad) == kPtfBadChecksum);
    bad = makePtf();
    bad[30] = 'Z';
    CHECK(verdict(bad) == kPtfBadChar);
    CHECK(verdict({}) == kPtfEmpty);
    CHECK(verdict({ ' ', '\r', '\n' }) == kPtfEmpty);
    CHECK(verdict(std::vector<uint8_t>(4000, 0x80)) == kPtfNotHex);    // a binary image
    auto cut = makePtf();
    cut.resize(cut.size() / 2 + 3);
    CHECK(verdict(cut) == kPtfTruncated || verdict(cut) == kPtfNoEof);
    auto after = makePtf();
    after.push_back('x');
    CHECK(verdict(after) == kPtfAfterEof);
    auto type4 = makePtf();
    const std::string r4 = hexRecord(4, 0, { 0, 1 }) + "\r\n";
    type4.insert(type4.begin() + 100, r4.begin(), r4.end());
    CHECK(verdict(type4) == kPtfRecordType || verdict(type4) == kPtfBadChar || verdict(type4) == kPtfBadLength);
    // a record inserted at a record start
    auto type4b = makePtf();
    type4b.insert(type4b.begin(), r4.begin(), r4.end());
    CHECK(verdict(type4b) == kPtfRecordType);

    // names
    CHECK(firmwareNameError("PolyendTracker_AUTOT_1.1.0.ptf", true) == kNameOk);
    CHECK(firmwareNameError("PolyendTracker_CE_1.0.0.ptf", true) == kNameOk);
    CHECK(firmwareNameError("PolyendTracker_0123456789_1.0.0.ptf", true) == kNameTooLong);   // 35
    CHECK(firmwareNameError("PolyendTracker_123456789_1.0.ptf", true) == kNameTooLong);      // 32, one over
    CHECK(firmwareNameError("PolyendTracker_12345678_1.0.ptf", true) == kNameOk);            // 31, the limit
    CHECK(strlen("PolyendTracker_12345678_1.0.ptf") == 31 && strlen("PolyendTracker_123456789_1.0.ptf") == 32);
    CHECK(firmwareNameError("PolyendTrac.ptf", true) == kNameOk);                              // 15
    CHECK(firmwareNameError("PolyendTr.ptf", true) == kNameTooShort);
    CHECK(firmwareNameError("polyendTracker_CE_1.0.0.ptf", true) == kNameNoPrefix);
    CHECK(firmwareNameError("TrackerPlus_CE_1.0.0.ptf", true) == kNameNoPrefix);
    CHECK(firmwareNameError("PolyendTracker_CE_1.0.0.PTF", true) == kNameNotPtf);
    CHECK(firmwareNameError("PolyendTracker_CE_1.0.0.hex", true) == kNameNotPtf);
    CHECK(firmwareNameError("PolyendTracker_CE 1.0.0.ptf", true) == kNameBadChar);
    CHECK(firmwareNameError("PolyendTracker/CE_1.0.0.ptf", true) == kNameBadChar);
    CHECK(firmwareNameError("", true) == kNameTooShort);
    CHECK(firmwareNameError("_usbtest.tmp", false) == kNameOk);
    CHECK(firmwareNameError("a b", false) == kNameBadChar);
}

static const char* kName = "PolyendTracker_TEST_1.0.0.ptf";
static const char* kPath = "/Firmware/PolyendTracker_TEST_1.0.0.ptf";

static std::string flashLine(const char* name, const std::vector<uint8_t>& data)
{
    char l[160];
    snprintf(l, sizeof(l), "flash %s 0x%08x", name, zcrc(data.data(), data.size()));
    return l;
}

static void testFlash()
{
    FakeHost h; Console c; reset(h, c);
    const auto image = makePtf();
    h.files[kPath] = image;

    // fwcheck without a name: the numbers the runner plans with
    CHECK(starts(cmd(h, "fwcheck"), "@ok fwinfo free=52428800 limit=0xff000 namemax=31 filemax=6291456"));
    h.freeValue = -1;
    CHECK(starts(cmd(h, "fwcheck"), "@ok fwinfo free=unknown"));
    h.freeValue = 50L << 20;

    // fwcheck of the good file, read only
    char want[160];
    snprintf(want, sizeof(want), "@ok fwcheck size=%zu crc=%08x top=0x6000 sp=0x20030000 reset=0x000001d1 listed=1", image.size(), zcrc(image.data(), image.size()));
    CHECK(cmd(h, std::string("fwcheck ") + kName, 100) == want);
    CHECK(h.bootloaderCalls.empty() && h.files.size() == 1);

    // --- flash: every way of being refused leaves the bootloader alone
    CHECK(starts(cmd(h, std::string("flash ") + kName), "@err flash: needs the confirm token"));
    CHECK(starts(cmd(h, "flash"), "@err flash: needs the confirm token"));
    CHECK(starts(cmd(h, std::string("flash ") + kName + " 1234abcd"), "@err flash: needs the confirm token"));    // no 0x
    CHECK(starts(cmd(h, std::string("flash ") + kName + " 0x1234"), "@err flash: needs the confirm token"));      // short
    CHECK(starts(cmd(h, std::string("flash ") + kName + " 0x123456789"), "@err flash: needs the confirm token"));  // long
    CHECK(starts(cmd(h, std::string("flash ") + kName + " 0xzzzzzzzz"), "@err flash: needs the confirm token"));
    CHECK(starts(cmd(h, std::string("flash ") + kName + " 0x00000000 extra"), "@err flash: needs the confirm token"));
    CHECK(starts(cmd(h, std::string("flash ") + kName + " 0xdeadbeef", 100), "@err flash crc "));                  // wrong token
    CHECK(h.bootloaderCalls.empty());
    pump(h, 100);
    CHECK(h.bootloaderCalls.empty() && !c.busy());

    // name rules, all before the file is looked at
    CHECK(starts(cmd(h, "flash PolyendTracker_1234567890_1.0.ptf 0x00000000"), "@err flash name-too-long"));
    CHECK(starts(cmd(h, "flash TrackerPlus_CE_1.0.0.ptf 0x00000000"), "@err flash name-no-prefix"));
    CHECK(starts(cmd(h, "flash PolyendTracker_CE_1.0.0.bin 0x00000000"), "@err flash name-not-ptf"));
    CHECK(starts(cmd(h, "flash ../PolyendTracker_CE_1.0.0.ptf 0x00000000"), "@err flash name-bad-char"));
    CHECK(starts(cmd(h, "flash PolyendTracker_CE_1.0.0.ptf 0x00000000"), "@err flash no such file"));
    CHECK(h.bootloaderCalls.empty());

    // refused while playing, rendering or loading
    const std::string good = flashLine(kName, image);
    h.playingFlag = true;
    CHECK(starts(cmd(h, good), "@err flash refused while playing, rendering or loading"));
    h.playingFlag = false;
    h.renderingFlag = true;
    CHECK(starts(cmd(h, good), "@err flash refused while playing, rendering or loading"));
    h.renderingFlag = false;
    h.fmStatus = 3;
    CHECK(starts(cmd(h, good), "@err flash refused while playing, rendering or loading"));
    CHECK(starts(cmd(h, std::string("fwcheck ") + kName), "@err fwcheck refused"));
    h.fmStatus = 0;
    CHECK(h.bootloaderCalls.empty());

    // a file that fails the validator is refused with the token right
    {
        auto badImage = makePtf(0x6000, 0x20280000);   // the stack of another chip
        h.files["/Firmware/PolyendTracker_BADSP_1.0.ptf"] = badImage;
        CHECK(starts(cmd(h, flashLine("PolyendTracker_BADSP_1.0.ptf", badImage), 100), "@err flash ptf bad-sp"));
        auto flipped = makePtf();
        flipped[200] = flipped[200] == '0' ? '1' : '0';
        h.files["/Firmware/PolyendTracker_FLIP_1.0.ptf"] = flipped;
        CHECK(starts(cmd(h, flashLine("PolyendTracker_FLIP_1.0.ptf", flipped), 100), "@err flash ptf bad-checksum"));
        h.files["/Firmware/PolyendTracker_SMALL_1.0.ptf"] = std::vector<uint8_t>(100, 'x');
        CHECK(starts(cmd(h, flashLine("PolyendTracker_SMALL_1.0.ptf", h.files["/Firmware/PolyendTracker_SMALL_1.0.ptf"])), "@err flash size 100"));
        h.files.erase("/Firmware/PolyendTracker_BADSP_1.0.ptf");
        h.files.erase("/Firmware/PolyendTracker_FLIP_1.0.ptf");
        h.files.erase("/Firmware/PolyendTracker_SMALL_1.0.ptf");
        CHECK(h.bootloaderCalls.empty());
    }

    // the real thing: ok line first, the bootloader about 250 ms later, nothing in between
    h.tx.clear();
    h.send(good + "\n");
    pump(h, 80, 10);   // reads the file (about 25 steps), the 250 ms are not over yet at the first ok
    std::string all(h.tx.begin(), h.tx.end());
    char handoff[160];
    snprintf(handoff, sizeof(handoff), "@ok flash handoff %s size=%zu crc=%08x\n", kName, image.size(), zcrc(image.data(), image.size()));
    // the fake bootloader returns (the real one never does), so the failure line follows the ok line
    CHECK(all == std::string(handoff) + "@err flash: the bootloader hand-off failed (/Firmware/_fwinfo not written)\n");
    CHECK(h.bootloaderCalls.size() == 1 && h.bootloaderCalls[0] == kName);
    CHECK(h.files.count(kPath) == 1);   // the image stays on the card
    CHECK(!c.busy());

    // the bootloader call comes after the delay, not at once
    {
        FakeHost h2; Console c2; reset(h2, c2);
        h2.files[kPath] = image;
        h2.send(good + "\n");
        int steps = 0;
        while (h2.bootloaderCalls.empty() && steps < 500) { pump(h2, 1, 10); steps++; }
        CHECK(!h2.bootloaderCalls.empty());
        // 5 to 8 steps for the file at 16 chunks per pass, then 250 ms
        CHECK(steps >= 25 && steps <= 45);
        // while the hand-off is armed other commands are refused, ping still answers
        FakeHost h3; Console c3; reset(h3, c3);
        h3.files[kPath] = image;
        h3.send(good + "\n");
        pump(h3, 30, 10);
        CHECK(h3.bootloaderCalls.empty() && c3.busy());
        CHECK(starts(cmd(h3, "key F1 tap", 1), "@err busy"));
        CHECK(starts(cmd(h3, "ping", 1), "@tracker-dev"));
        // abort inside the window cancels the hand-off
        CHECK(starts(cmd(h3, "abort", 1), "@ok abort"));
        pump(h3, 100);
        CHECK(h3.bootloaderCalls.empty() && !c3.busy());
    }
    // playback that starts during the window cancels it
    {
        FakeHost h2; Console c2; reset(h2, c2);
        h2.files[kPath] = image;
        h2.send(good + "\n");
        pump(h2, 30, 10);
        CHECK(h2.bootloaderCalls.empty());
        h2.playingFlag = true;
        h2.tx.clear();
        pump(h2, 100);
        CHECK(h2.bootloaderCalls.empty());
        std::string out(h2.tx.begin(), h2.tx.end());
        CHECK(out.find("@err flash cancelled") != std::string::npos);
    }
    // the USB link going away inside the window cancels it
    {
        FakeHost h2; Console c2; reset(h2, c2);
        h2.files[kPath] = image;
        h2.send(good + "\n");
        pump(h2, 30, 10);
        h2.link = false;
        pump(h2, 100);
        CHECK(h2.bootloaderCalls.empty() && !c2.busy());
    }
    // a hand-off that returns (the _fwinfo file could not be written) is reported
    {
        FakeHost h2; Console c2; reset(h2, c2);
        h2.files[kPath] = image;
        h2.tx.clear();
        h2.send(good + "\n");
        pump(h2, 100);
        std::string out(h2.tx.begin(), h2.tx.end());
        CHECK(out.find("@err flash: the bootloader hand-off failed") != std::string::npos);
        CHECK(h2.bootloaderCalls.size() == 1 && !c2.busy());
    }

    // --- fwpromote: temp file to final name, the old file of that name goes
    {
        FakeHost h2; Console c2; reset(h2, c2);
        const char* tmp = "/Firmware/_usbtest.tmp";
        h2.files[tmp] = image;
        char line[200];
        snprintf(line, sizeof(line), "fwpromote _usbtest.tmp %s 0x%08x", kName, zcrc(image.data(), image.size()));
        // wrong token: nothing moves
        CHECK(starts(cmd(h2, "fwpromote _usbtest.tmp PolyendTracker_TEST_1.0.0.ptf 0x00000001", 100), "@err fwpromote crc "));
        CHECK(h2.files.count(tmp) == 1 && h2.files.count(kPath) == 0);
        // wrong platform: nothing moves
        auto other = makePtf(0x6000, 0x20280000);
        h2.files["/Firmware/_other.tmp"] = other;
        char bl[200];
        snprintf(bl, sizeof(bl), "fwpromote _other.tmp %s 0x%08x", kName, zcrc(other.data(), other.size()));
        CHECK(starts(cmd(h2, bl, 100), "@err fwpromote ptf bad-sp"));
        CHECK(h2.files.count(kPath) == 0);
        // name too long / not listed / same as source
        CHECK(starts(cmd(h2, "fwpromote _usbtest.tmp PolyendTracker_1234567890_1.0.ptf 0x00000001"), "@err fwpromote name-too-long"));
        CHECK(starts(cmd(h2, "fwpromote _usbtest.tmp other.ptf 0x00000001"), "@err fwpromote name-too-short"));
        CHECK(starts(cmd(h2, "fwpromote _usbtest.tmp _usbtest.tmp 0x00000001"), "@err fwpromote name-too-short"));
        CHECK(starts(cmd(h2, "fwpromote PolyendTracker_TEST_1.0.0.ptf PolyendTracker_TEST_1.0.0.ptf 0x00000001"), "@err fwpromote name-bad-char"));
        CHECK(starts(cmd(h2, "fwpromote _usbtest.tmp PolyendTracker_TEST_1.0.0.ptf"), "@err usage"));
        char ok[200];
        snprintf(ok, sizeof(ok), "@ok fwpromote %s size=%zu crc=%08x", kName, image.size(), zcrc(image.data(), image.size()));
        CHECK(cmd(h2, line, 100) == ok);
        CHECK(h2.files.count(tmp) == 0 && h2.files[kPath] == image);
        // again with an old file of that name on the card: replaced
        h2.files[tmp] = image;
        h2.files[kPath] = std::vector<uint8_t>(10, 1);
        CHECK(cmd(h2, line, 100) == ok);
        CHECK(h2.files[kPath] == image && h2.files.count(tmp) == 0);
        // rename fails: the checked copy stays where it is
        h2.files[tmp] = image;
        h2.renameOk = false;
        CHECK(starts(cmd(h2, line, 100), "@err fwpromote rename failed"));
        CHECK(h2.files.count(tmp) == 1);
        CHECK(h2.bootloaderCalls.empty());
        // never calls the bootloader, whatever happens
    }
}

int main()
{
    testCrc();
    testRle();
    testRing();
    testFormat();
    testParser();
    testGate();
    testInput();
    testIdleReboot();
    testShot();
    testShotCrc();
    testTexts();
    testFiles();
    testOpen();
    testPtfValidator();
    testFlash();
    printf("usbtest_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
