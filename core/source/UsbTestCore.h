#ifndef SOURCE_USBTESTCORE_H_
#define SOURCE_USBTESTCORE_H_

// USB test console, portable part: ring buffer, CRC32, row RLE, command parser and the command state machines.
// It knows nothing about the Tracker. Everything it needs from the outside goes through usbtest::Host
// (UsbTest.cpp binds it to the firmware core, the unit test and the Mac front end bind it to fakes and stand-ins).
// Protocol: docs/usb-test-console.md. Compiled only with ENABLE_DEV_CONSOLE.

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#if defined(ENABLE_DEV_CONSOLE) && !defined(PLATFORM_IMX)

namespace usbtest
{
constexpr int kProtocol = 1;
constexpr uint32_t kRingSize = 4096; // power of two
constexpr size_t kLineMax = 128;
constexpr int kShotW = 800;
constexpr int kShotH = 480;
constexpr int kShotHalfRows = 240;
constexpr size_t kRowBytes = 3200; // 800 px * 4 B, what the FT812 snapshot gives per row
constexpr int kTileW = 100;
constexpr int kTileH = 80;
constexpr int kTilesX = 8;
constexpr int kTilesY = 6;
constexpr size_t kChunk = 512;                       // file transfer block
// scratch: current row, previous row, encoded row (the file chunk reuses the first). 4 byte aligned.
constexpr size_t kScratchWords = (3 * kRowBytes + 64) / 4;

// zlib compatible: crc32(crc32(0, a), b) == crc32(0, a + b)
uint32_t crc32(uint32_t crc, const void* data, size_t length);

// ---- row codec "rle1": PackBits on 32 bit words of (pixel XOR the pixel above). Control byte c:
//      c < 0x80  : c + 1 literal words follow (1..128)
//      c >= 0x80 : the one word that follows repeats (c & 0x7F) + 2 times (2..129)
// Pixels are the FT812 ARGB8 words, bytes B G R A in memory, little endian. The console sends a row equal to the one above
// as a zero length body (@row y 0), the first row is coded against a black row.
size_t rleSize(const uint32_t* cur, const uint32_t* prev, int count);
size_t rleEncode(uint8_t* out, const uint32_t* cur, const uint32_t* prev, int count);
// False if the stream is malformed or does not fill exactly `count` words.
bool rleDecode(const uint8_t* in, size_t length, uint32_t* row, const uint32_t* prev, int count);

// ---- transmit ring. Writers (main loop and interrupts) must hold the host's interrupt lock, the single reader is
//      the main loop. All or nothing per push.
struct Ring
{
    uint8_t buf[kRingSize];
    volatile uint32_t head;
    volatile uint32_t tail;
    uint32_t dropped;
    uint8_t lastByte;

    void clear();
    uint32_t used() const { return head - tail; }
    uint32_t freeBytes() const { return kRingSize - (head - tail); }
    bool push(const uint8_t* data, size_t length);
    size_t peek(uint8_t* dest, size_t maxLength); // copies, the reader advances tail itself
};

// ---- what the console needs from its surroundings
struct CpuUsage
{
    int minimum;
    int maximum;
    int tempMaximum;
    int average;
    int last;
};

struct Markers
{
    uint32_t resetStatus;  // RCM_SRS1 << 8 | RCM_SRS0 at boot, 0 where there is no such thing
    uint32_t faultPc;      // saved by the fault handler of a REBOOT_ON_FAULT image
    uint32_t faultLr;
    uint32_t faultPsr;
    uint32_t faultCfsr;
    bool faultValid;       // the fault record belongs to the reset that just happened
    bool safeMode;         // plugin host safe mode
    bool pluginLoading;    // /Plugins/.loading exists
    char safeReason[40];
};

struct TextItem
{
    int id;
    int x;
    int y;
    int w;
    int h;
    char text[64];
};

struct DirEntry
{
    bool isDir;
    uint32_t size;
    char name[96];
};

class Host
{
  public:
    virtual ~Host() {}

    // transport and platform
    virtual int rxByte() = 0;           // next received byte, -1 when none
    virtual bool linkUp() = 0;          // a terminal has the port open
    virtual size_t txFree() = 0;        // bytes txWrite takes right now without waiting
    virtual size_t txWrite(const uint8_t* data, size_t length) = 0;
    virtual uint32_t irqLock() = 0;
    virtual void irqUnlock(uint32_t state) = 0;
    virtual uint32_t nowMs() = 0;
    virtual const char* buildId() = 0;
    virtual void resetMcu() = 0;
    virtual bool validPointer(const void* p) = 0;
    virtual uint32_t* scratch() = 0; // kScratchWords words, main loop only

    // input, applied by the platform's own input drivers
    virtual bool injectButton(uint8_t id, bool down) = 0;
    virtual bool injectPad(uint8_t id, bool down) = 0;
    virtual void injectEncoder(int delta) = 0;
    virtual bool inputPending() = 0;

    // state
    virtual void cpu(CpuUsage& usage) = 0; // microseconds
    virtual void cpuReset() = 0;
    virtual uint32_t freeRam() = 0;
    virtual int page() = 0;
    virtual const char* pageName(int page) = 0;
    virtual bool playing() = 0;
    virtual int fileManagerStatus() = 0;
    virtual bool projectDirty() = 0;
    virtual bool uiIdle() = 0;
    virtual void markers(Markers& m) = 0;
    virtual bool peek(const char* name, long* value) = 0;
    virtual const char* peekNames() = 0;
    virtual uint32_t hashProject(uint32_t* size) = 0;

    // transport control of the application
    virtual bool play(bool song, int position) = 0;
    virtual void stop() = 0;
    virtual bool openProject(const char* path, const char* name) = 0;
    virtual int openState(int* code) = 0; // 0 running, 1 done, 2 failed (code = file manager status)

    // screen
    virtual bool shotBegin() = 0;
    virtual void shotSnapshot(int half) = 0;
    virtual void shotRow(int half, int row, uint8_t* dest) = 0; // kRowBytes
    virtual void shotEnd() = 0;
    virtual bool nextText(int* cursor, TextItem& item) = 0;

    // files
    virtual bool dirOpen(const char* path) = 0;
    virtual bool dirNext(DirEntry& entry) = 0;
    virtual void dirClose() = 0;
    virtual long fileSize(const char* path) = 0; // -1 if missing
    virtual void* fileOpenRead(const char* path) = 0;
    virtual void* fileOpenWrite(const char* path) = 0; // creates, replaces
    virtual int fileRead(void* file, void* dest, size_t length) = 0;
    virtual int fileWrite(void* file, const void* data, size_t length) = 0;
    virtual void fileClose(void* file) = 0;
    virtual bool fileRemove(const char* path) = 0;
};

// Name table for the front panel buttons (the ids Interface::buttonChange takes). -1 if unknown.
int buttonByName(const char* name);
const char* buttonName(int id);
constexpr int kButtonCount = 48;
constexpr int kPadCount = 48;

class Console
{
  public:
    void init(Host* host);
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

    // main loop
    void update();

    // any context (audio interrupt included): the text goes through the ring, nothing waits.
    void log(const char* text);
    void logf(const char* format, va_list args);

    // for tests and for the host front end
    bool busy() const { return task_ != kTaskNone; }
    // a task runs (a put that waits for bytes does not count), input is scheduled or output waits for the transport
    bool pending() const;
    const Ring& ring() const { return ring_; }

  private:
    enum Task : uint8_t
    {
        kTaskNone,
        kTaskShot,
        kTaskTexts,
        kTaskLs,
        kTaskGet,
        kTaskPut,
        kTaskOpen,
        kTaskIdle,
        kTaskReboot,
    };

    struct Scheduled
    {
        uint32_t due;
        uint8_t kind; // 0 button, 1 pad
        uint8_t id;
        uint8_t down;
        uint8_t used;
    };

    void reply(const char* format, ...) __attribute__((format(printf, 2, 3)));
    void replyV(const char* format, va_list args);
    bool pushLocked(const uint8_t* data, size_t length);
    bool pushBlock(const char* header, const uint8_t* data, size_t length); // header line, data, newline
    bool needSpace(uint32_t bytes) const { return ring_.freeBytes() >= bytes; }

    void rxPump();
    void execLine(char* line);
    void txPump();
    void runTask(uint32_t now);
    void pumpInput(uint32_t now);
    void abortTask();
    void finishTask();

    // commands
    void cmdKey(int argc, char** argv, bool pad);
    void cmdEnc(int argc, char** argv);
    void cmdCpu(int argc, char** argv);
    void cmdStatus();
    void cmdShot(int argc, char** argv, bool crcOnly);
    void cmdPlay(int argc, char** argv);
    void cmdPeek(int argc, char** argv);
    void cmdHash(int argc, char** argv);
    void cmdMarkers();
    void cmdLs(int argc, char** argv);
    void cmdGet(int argc, char** argv, bool hashOnly);
    void cmdPut(int argc, char** argv);
    void cmdRm(int argc, char** argv);
    void cmdOpen(int argc, char** argv);
    bool schedule(uint8_t kind, uint8_t id, bool down, uint32_t due);

    // tasks
    void stepShot(uint32_t now);
    void stepTexts();
    void stepLs();
    void stepGet();
    void stepPut(uint32_t now);
    void stepOpen(uint32_t now);
    void stepIdle(uint32_t now);
    void putByte(uint8_t b);
    void putFinish();

    bool badPath(const char* path) const;

    Host* host_;
    bool enabled_;
    bool linkWasUp_;
    volatile uint8_t blockOpen_;
    Ring ring_;

    char line_[kLineMax];
    uint8_t lineLength_;
    bool lineOverflow_;

    Task task_;
    uint32_t taskStartMs_;
    uint32_t taskLastMs_;
    uint32_t cpuResetMs_;
    uint32_t now_;
    uint64_t heldButtons_;
    uint64_t heldPads_;
    uint32_t idleSinceMs_;
    uint32_t lastInputMs_;

    Scheduled queue_[24];

    // shot
    uint8_t shotCrcOnly_;
    int8_t shotHalf_;
    int16_t shotRow_;
    uint8_t shotHaveRow_;
    uint8_t shotNeedSnapshot_;
    uint32_t shotCrc_;
    uint32_t shotBytes_;
    uint32_t tileCrc_[kTilesX];

    // texts / ls
    int cursor_;
    int count_;

    // get / put / hash
    void* file_;
    uint32_t fileSize_;
    uint32_t fileDone_;
    uint32_t fileCrc_;
    uint32_t fileWantCrc_;
    uint8_t hashOnly_;
    uint16_t putFill_;
    char path_[80];
    char name_[48];
};
} // namespace usbtest

#endif /* ENABLE_DEV_CONSOLE */

#endif /* SOURCE_USBTESTCORE_H_ */
