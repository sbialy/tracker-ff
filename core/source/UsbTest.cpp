#include "UsbTest.h"

#if defined(ENABLE_DEV_CONSOLE) && !defined(PLATFORM_IMX)

#include "Platform.h"
#include "Profiler.h"
#include "UsbTestCore.h"
#include "core/interface.h"
#include "display.h"
#include "fileManager.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "cr_section_macros.h"

#ifdef FEATURE_PLUG_HOST
#include "plug_host.h"
#endif

#include <stddef.h>
#include <string.h>

using namespace usbtest;

namespace
{
// scratch for the screenshot rows and the file chunks, in the SDRAM, uninitialised (nothing may touch the SDRAM before
// main() has set it up: a plain array, no constructor)
__NOINIT(EXTERNAL_RAM) __attribute__((aligned(32))) uint32_t g_scratch[kScratchWords];

const char* const kPageNames[] = { "file",   "importer",    "pattern", "play",  "song",
                                   "instr",  "sample-edit", "config",  "rec",   "performance",
                                   "image",  "?",           "master" };

struct PeekField
{
    const char* name;
    uint16_t offset;
    uint8_t kind; // 0 u8, 1 i8, 2 u16, 3 float * 100
};

#define PEEK(n, k) { #n, uint16_t(offsetof(strMtValues, n)), k }
const PeekField kPeekFields[] = {
    PEEK(lastUsedNote, 0),   PEEK(lastUsedInstrument, 0), PEEK(lastUsedVolume, 0), PEEK(lastUsedFx, 0),
    PEEK(actualPattern, 0),  PEEK(patternEditStep, 1),    PEEK(bitDepth, 0),       PEEK(patternLength, 2),
    PEEK(projectNotSavedFlag, 0), PEEK(delayTime, 2),     PEEK(limiterThreshold, 2), PEEK(globalTempo, 3),
};
#undef PEEK

class TrackerHost : public Host
{
  public:
    Platform* platform;
    char nameBuffer[272];
    uint8_t openStarted;
    uint32_t openStartMs;
    char peekList[220];

    void init(Platform* p)
    {
        platform = p;
        openStarted = 0;
        peekList[0] = 0;
        size_t n = 0;
        for (const PeekField& f : kPeekFields)
        {
            const size_t l = strlen(f.name);
            if (n + l + 2 < sizeof(peekList))
            {
                memcpy(peekList + n, f.name, l);
                n += l;
                peekList[n++] = ' ';
            }
        }
        if (n > 0)
        {
            n--;
        }
        peekList[n] = 0;
    }

    FileSystem* fs() { return platform->getFileSystem(); }

    int rxByte() override { return UsbTestPlatform_RxByte(); }
    bool linkUp() override { return UsbTestPlatform_LinkUp(); }
    size_t txFree() override { return UsbTestPlatform_TxFree(); }
    size_t txWrite(const uint8_t* d, size_t n) override { return UsbTestPlatform_TxWrite(d, n); }
    uint32_t irqLock() override { return UsbTestPlatform_IrqLock(); }
    void irqUnlock(uint32_t s) override { UsbTestPlatform_IrqUnlock(s); }
    uint32_t nowMs() override { return platform->getTimer()->millis(); }
    const char* buildId() override { return platform->getSystemInfo()->buildId; }
    void resetMcu() override { platform->resetMCU(); }
    bool validPointer(const void* p) override { return UsbTestPlatform_ValidPointer(p); }
    uint32_t* scratch() override { return g_scratch; }

    bool injectButton(uint8_t id, bool down) override { return UsbTestPlatform_InjectButton(id, down); }
    bool injectPad(uint8_t id, bool down) override { return UsbTestPlatform_InjectPad(id, down); }
    void injectEncoder(int delta) override { UsbTestPlatform_InjectEncoder(delta); }
    bool inputPending() override { return UsbTestPlatform_InputPending(); }

    void cpu(CpuUsage& u) override
    {
        Profiler::AudioProcessorUsage usage;
        Profiler::getInstance()->getAudioProcessingUsageUs(usage, false);
        u.minimum = usage.minimum;
        u.maximum = usage.maximum;
        u.tempMaximum = usage.tempMaximum;
        u.average = usage.average;
        u.last = usage.last;
    }
    void cpuReset() override { Profiler::getInstance()->resetMeasuringAudio(); }
    uint32_t freeRam() override { return uint32_t(Profiler::getInstance()->freeRamBytes()); }
    int page() override { return mtInterface.devPageIndex(); }
    const char* pageName(int p) override
    {
        return (p >= 0 && p < int(sizeof(kPageNames) / sizeof(kPageNames[0]))) ? kPageNames[p] : "none";
    }
    bool playing() override { return sequencer.isPlay(); }
    int fileManagerStatus() override { return newFileManager.getStatus(); }
    bool projectDirty() override
    {
        return newFileManager.isProjectChanged() || (mtProject.values.projectNotSavedFlag & 1) != 0;
    }
    bool uiIdle() override
    {
        return newFileManager.getStatus() == fmIdle && (sequencer.isPlay() || display.isIdle());
    }

    void markers(Markers& m) override
    {
        m.resetStatus = UsbTestPlatform_ResetStatus();
        m.faultValid = UsbTestPlatform_FaultRecord(&m.faultPc, &m.faultLr, &m.faultPsr, &m.faultCfsr);
        m.safeReason[0] = 0;
#ifdef FEATURE_PLUG_HOST
        m.safeMode = plug::safeMode();
        const char* reason = plug::safeReason();
        if (reason != nullptr)
        {
            strncpy(m.safeReason, reason, sizeof(m.safeReason) - 1);
            m.safeReason[sizeof(m.safeReason) - 1] = 0;
        }
#endif
        m.pluginLoading = fs()->exists("/Plugins/.loading");
    }

    bool peek(const char* name, long* value) override
    {
        const uint8_t* base = reinterpret_cast<const uint8_t*>(&mtProject.values);
        for (const PeekField& f : kPeekFields)
        {
            if (strcmp(f.name, name) != 0)
            {
                continue;
            }
            const uint8_t* p = base + f.offset;
            switch (f.kind)
            {
                case 0: *value = *p; break;
                case 1: *value = *reinterpret_cast<const int8_t*>(p); break;
                case 2:
                {
                    uint16_t v;
                    memcpy(&v, p, 2);
                    *value = v;
                    break;
                }
                default:
                {
                    float v;
                    memcpy(&v, p, 4);
                    *value = long(v * 100.0f + (v < 0 ? -0.5f : 0.5f));
                    break;
                }
            }
            return true;
        }
        return false;
    }
    const char* peekNames() override { return peekList; }
    uint32_t hashProject(uint32_t* size) override
    {
        *size = sizeof(mtProject.values);
        return crc32(0, &mtProject.values, sizeof(mtProject.values));
    }

    bool play(bool song, int position) override
    {
        if (song)
        {
            if (position < 0)
            {
                sequencer.playSong();
            }
            else
            {
                sequencer.playSongFromPosition(uint8_t(position));
            }
        }
        else if (position < 0)
        {
            sequencer.playPattern();
        }
        else
        {
            sequencer.playPattern(uint8_t(position));
        }
        return true;
    }
    void stop() override { sequencer.stop(); }

    bool openProject(const char* path, const char* name) override
    {
        char p[80];
        char n[48];
        strncpy(p, path, sizeof(p) - 1);
        p[sizeof(p) - 1] = 0;
        strncpy(n, name, sizeof(n) - 1);
        n[sizeof(n) - 1] = 0;
        newFileManager.clearStatus();
        if (!newFileManager.openProject(p, n))
        {
            return false;
        }
        openStarted = 0;
        openStartMs = nowMs();
        return true;
    }
    // The project editor swallows fmLoadEnd itself when its page is up, on any other page it stays until someone clears it.
    int openState(int* code) override
    {
        const uint8_t s = newFileManager.getStatus();
        if (s >= fmError)
        {
            *code = s;
            newFileManager.clearStatus();
            return 2;
        }
        if (s == fmLoadEnd)
        {
            if (mtInterface.devPageIndex() != 0)
            {
                newFileManager.clearStatus();
            }
            return 1;
        }
        if (s != fmIdle)
        {
            openStarted = 1;
            return 0;
        }
        if (openStarted != 0)
        {
            return 1;
        }
        if (nowMs() - openStartMs > 1500)
        {
            *code = -1; // never started
            return 2;
        }
        return 0;
    }

    bool shotBegin() override { return display.devShotBegin(); }
    void shotSnapshot(int half) override { display.devShotSnapshot(half); }
    void shotRow(int half, int row, uint8_t* dest) override
    {
        (void)half;
        display.devShotRow(row, dest);
    }
    void shotEnd() override { display.devShotEnd(); }
    bool nextText(int* cursor, TextItem& item) override
    {
        int id;
        int x;
        int y;
        int w;
        int h;
        const char* text;
        while (display.devNextText(cursor, &id, &x, &y, &w, &h, &text))
        {
            if (!UsbTestPlatform_ValidPointer(text))
            {
                continue;
            }
            size_t n = 0;
            for (; n + 1 < sizeof(item.text) && text[n] != 0; n++)
            {
                const unsigned char c = static_cast<unsigned char>(text[n]);
                item.text[n] = (c >= 32 && c < 127) ? char(c) : '?';
            }
            item.text[n] = 0;
            if (n == 0)
            {
                continue;
            }
            item.id = id;
            item.x = x;
            item.y = y;
            item.w = w;
            item.h = h;
            return true;
        }
        return false;
    }

    bool dirOpen(const char* path) override { return fs()->nesDir_open(path); }
    bool dirNext(DirEntry& e) override
    {
        uint8_t isDir = 0;
        uint8_t hidden = 0;
        uint32_t size = 0;
        if (!fs()->nesDir_readItem(nameBuffer, &isDir, &hidden, &size))
        {
            return false;
        }
        e.isDir = isDir != 0;
        e.size = size;
        strncpy(e.name, nameBuffer, sizeof(e.name) - 1);
        e.name[sizeof(e.name) - 1] = 0;
        return true;
    }
    void dirClose() override { fs()->nesDir_close(); }
    long fileSize(const char* path) override { return fs()->exists(path) ? long(fs()->getFileSize(path)) : -1; }
    void* fileOpenRead(const char* path) override
    {
        auto r = fs()->openFile(path, FileSystem::kRead);
        return std::get<0>(r) ? std::get<1>(r) : nullptr;
    }
    void* fileOpenWrite(const char* path) override
    {
        if (fs()->exists(path))
        {
            fs()->remove(path);
        }
        auto r = fs()->openFile(path, FileSystem::kWriteCreateNew);
        return std::get<0>(r) ? std::get<1>(r) : nullptr;
    }
    int fileRead(void* f, void* dest, size_t n) override { return fs()->readFile(dest, n, f); }
    int fileWrite(void* f, const void* data, size_t n) override { return fs()->writeFile(data, n, f); }
    void fileClose(void* f) override { fs()->closeFile(f); }
    bool fileRemove(const char* path) override { return fs()->remove(path); }
};

TrackerHost g_host;
Console g_console;
bool g_ready;
} // namespace

void UsbTest_Init(Platform* platform, bool develop)
{
    g_host.init(platform);
    g_console.init(&g_host);
    g_console.setEnabled(develop);
    g_ready = develop;
}

void UsbTest_Update()
{
    if (g_ready)
    {
        g_console.update();
    }
}

void UsbTest_Log(const char* text)
{
    g_console.log(text);
}

void UsbTest_LogV(const char* format, va_list args)
{
    g_console.logf(format, args);
}

#endif /* ENABLE_DEV_CONSOLE */
