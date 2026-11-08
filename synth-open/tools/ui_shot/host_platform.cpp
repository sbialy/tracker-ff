// Host Platform for ui_shot. NOT firmware. See host_platform.h.
#include "host_platform.h"

#include "ConfigManager.h"

#include <algorithm>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace hostp
{
uint32_t g_nowMs = 1000;
bool g_resetRequested;
static std::string s_root = ".";
static bool g_held[64];
void advance(uint32_t ms) { g_nowMs += ms; }
void setHeld(int idx, bool on) { if (idx >= 0 && idx < 64) g_held[idx] = on; }
void setSdRoot(const std::string& d) { s_root = d; }
const std::string& sdRoot() { return s_root; }
// replaced by poly-devtools host_usbtest.cpp in a CONSOLE=1 build (host_platform.h)
__attribute__((weak)) bool keyPressed(bool, int) { return false; }
__attribute__((weak)) const char* simBuildId() { return "uishot"; }
__attribute__((weak)) void simBootloaderHandoff(const char*) {}

namespace
{
// ------------------------------------------------------------------------------------------------ simple devices
class HTimer : public Timer
{
  public:
    uint32_t millis() override { return g_nowMs; }
    uint32_t micros() override { return g_nowMs * 1000u; }
    void delay(int ms) override { g_nowMs += uint32_t(ms); }
    uint32_t clockCycles() override { return g_nowMs * 180000u; }
    uint32_t clockCyclesPerUs() override { return 180; }
};

class HEncoder : public Encoder
{
  public:
    void setAcceleration(int) override {}
    void setResolution(int) override {}
};

class HBattery : public Battery
{
  public:
    void enableMonitoring() override {}
    int getLevel() override { return 100; }
    float getVoltage() override { return 4.1f; }
    bool getCharging() override { return false; }
    int getTimeTo() override { return 0; }
    bool getState() override { return true; }
    bool isEmpty() override { return false; }
};

class HLeds : public Leds
{
  public:
    void setLed(int, bool, int) override {}
};

class HSwitch : public SwitchGroup
{
  public:
    explicit HSwitch(bool pad = false) : pad_(pad) {}
    void setHoldTime(int) override {}
    bool isButtonPressed(int i) override { return (!pad_ && i >= 0 && i < 64 && g_held[i]) || keyPressed(pad_, i); }
    bool pad_;
    void setActionCallback(ActionType, void (*)(uint8_t)) override {}
};

class HRadio : public Radio
{
  public:
    void enableLineInput(bool) override {}
    void enable(bool) override {}
    bool setFrequency(float f) override { freq_ = f; return true; }
    void setSeekCallback(void (*)(void)) override {}
    void seek(SeekDirection) override {}
    void updateSeek() override {}
    bool updateRDS() override { return false; }
    float getFrequency() override { return freq_; }
    float getBaseFrequency() override { return 87.5f; }
    float getSpacing() override { return 0.1f; }
    bool isinitialized() override { return false; }
    void clearRDS() override {}
    void setRegion(Region) override {}
    bool isRegionChanged() override { return false; }
    char* getStationName() override { static char n[1] = {0}; return n; }

  private:
    float freq_ = 100.0f;
};

class HCodec : public Sgtl5000
{
  public:
    void init() override {}
    void setInputType(InputType) override {}
    void setHeadphoneSource(HeadphoneSource) override {}
    void setLineInLevel(int) override {}
    void setMicGain(int) override {}
    void setHeadphoneMuted(bool) override {}
    void setMasterVolume(float) override {}
    void setAutoVolumeEnabled(bool) override {}
    void configureAutoVolume(AutoVolumeParams*) override {}
    void setEnhanceBassEnabled(bool) override {}
    void configureEnhanceBass(EnhanceBassParams*) override {}
    void setEqualizerType(EqualizerType) override {}
    void configureFiveBandEqualizer(float*) override {}
    void enableSurroundSound(bool) override {}
    void configureSurroundSound(SurroundSoundParams*) override {}
    void enableHardwareProcessor(bool) override {}
};

class HUsbAudio : public UsbAudio
{
  public:
    void beginFillData() override {}
    void fillChannelData(void*, int, int) override {}
    void finishFillData() override {}
};

class HUsbMidi : public UsbMidi
{
  public:
    bool isReadAvailable() override { return false; }
    void read() override {}
    void sendRealTime(MessageType) override {}
    void sendControlChange(uint8_t, uint8_t, uint8_t) override {}
    void sendProgramChange(uint8_t, uint8_t) override {}
    void sendAfterTouch(uint8_t, uint8_t) override {}
    void sendPolyPressure(uint8_t, uint8_t, uint8_t) override {}
    void sendPitchBend(int, uint8_t) override {}
    void sendNoteOn(uint8_t, uint8_t, uint8_t) override {}
    void sendNoteOff(uint8_t, uint8_t, uint8_t) override {}
    void setReceiveActions(MidiReceiveActions*) override {}
};

class HUartMidi : public UartMidi
{
  public:
    bool isReadAvailable() override { return false; }
    void read() override {}
    void sendRealTime(MessageType) override {}
    void sendControlChange(uint8_t, uint8_t, uint8_t) override {}
    void sendProgramChange(uint8_t, uint8_t) override {}
    void sendAfterTouch(uint8_t, uint8_t) override {}
    void sendPolyPressure(uint8_t, uint8_t, uint8_t) override {}
    void sendPitchBend(int, uint8_t) override {}
    void sendNoteOn(uint8_t, uint8_t, uint8_t) override {}
    void sendNoteOff(uint8_t, uint8_t, uint8_t) override {}
    void init() override {}
    void setReceiveActions(MidiReceiveActions*) override {}
    void setReceiveNoteOnAction(void (*)(uint8_t, uint8_t, uint8_t)) override {}
};

class HUsb : public Usb
{
  public:
    void init() override {}
    void reset() override {}
    void update() override {}
    void setClassState(ClassType, bool) override {}
    bool getClassState(ClassType) override { return false; }
    bool isAttached() override { return false; }
};

class HCrc : public CrcCalculator
{
  public:
    uint32_t calculate32bitCrc(const uint8_t* d, const uint16_t n) override
    {
        uint32_t c = 0xFFFFFFFFu;   // CRC-32 (IEEE), the value only has to be stable here
        for (uint16_t i = 0; i < n; i++)
        {
            c ^= d[i];
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        return ~c;
    }
};

class HConfig : public ConfigManager
{
  public:
    bool readData(int address, void* data, int size) override
    {
        if (address < 0 || address + size > int(sizeof(mem_))) return false;
        memcpy(data, mem_ + address, size_t(size));
        return true;
    }
    void writeData(int address, void* data, int size) override
    {
        if (address < 0 || address + size > int(sizeof(mem_))) return;
        memcpy(mem_ + address, data, size_t(size));
    }

  private:
    uint8_t mem_[4096] = {};   // an erased EEPROM reads 0xFF on the device, the host starts with zeros: the UI then writes its defaults
};

// ------------------------------------------------------------------------------------------------ file system
struct HFile
{
    FILE* f = nullptr;
    std::string path;
};

class HFileSystem : public FileSystem
{
  public:
    std::string abs(const char* p) const
    {
        std::string s = p ? p : "";
        while (!s.empty() && s[0] == '/') s.erase(0, 1);
        return s_root + "/" + s;
    }
    bool isSdCardInserted() override { return true; }
    void setMountEventCallback(CardStateChangeCallback) override {}
    bool remount() override { return true; }
    bool exists(const char* path) override { struct stat st; return stat(abs(path).c_str(), &st) == 0; }
    bool rename(const char* a, const char* b) override { return ::rename(abs(a).c_str(), abs(b).c_str()) == 0; }
    bool mkdir(uint8_t, const char* path, bool pFlag) override
    {
        std::string p = abs(path);
        if (pFlag)
            for (size_t i = s_root.size() + 1; i < p.size(); i++)
                if (p[i] == '/') ::mkdir(p.substr(0, i).c_str(), 0755);
        return ::mkdir(p.c_str(), 0755) == 0 || exists(path);
    }
    bool remove(const char* path) override { return ::remove(abs(path).c_str()) == 0; }
    bool removeDirWithFiles(const char* path) override { return system(("rm -rf '" + abs(path) + "'").c_str()) == 0; }
    bool removeOnlyDirFiles(const char* path) override { return system(("find '" + abs(path) + "' -type f -delete").c_str()) == 0; }
    size_t getFileSize(const char* path) override { struct stat st; return stat(abs(path).c_str(), &st) == 0 ? size_t(st.st_size) : 0; }
    size_t getFileSize(FileHandle h) override
    {
        auto* f = static_cast<HFile*>(h);
        if (!f || !f->f) return 0;
        const long cur = ftell(f->f);
        fseek(f->f, 0, SEEK_END);
        const long n = ftell(f->f);
        fseek(f->f, cur, SEEK_SET);
        return size_t(n);
    }
    size_t getFileAvailable(FileHandle h) override
    {
        auto* f = static_cast<HFile*>(h);
        if (!f || !f->f) return 0;
        return getFileSize(h) - size_t(ftell(f->f));
    }
    std::tuple<bool, FileHandle> openFile(const char* filepath, FileOpenMode mode, FileHandle* fileHandle) override
    {
        const char* m = mode == kRead ? "rb" : (mode == kWriteOpen ? "r+b" : (mode == kWriteAppend ? "ab" : "wb"));
        const std::string p = abs(filepath);
        if (mode == kWriteCreateNew || mode == kWriteAppend)
        {
            const size_t sl = p.rfind('/');
            if (sl != std::string::npos) system(("mkdir -p '" + p.substr(0, sl) + "'").c_str());
        }
        FILE* f = fopen(p.c_str(), m);
        if (!f && mode == kWriteOpen) f = fopen(p.c_str(), "w+b");
        HFile* h = (fileHandle && *fileHandle) ? static_cast<HFile*>(*fileHandle) : new HFile;
        h->f = f;
        h->path = filepath;
        if (fileHandle) *fileHandle = h;
        return {f != nullptr, h};
    }
    int32_t readFile(void* d, size_t n, FileHandle h) override
    {
        auto* f = static_cast<HFile*>(h);
        return f && f->f ? int32_t(fread(d, 1, n, f->f)) : -1;
    }
    int32_t writeFile(const void* d, size_t n, FileHandle h) override
    {
        auto* f = static_cast<HFile*>(h);
        return f && f->f ? int32_t(fwrite(d, 1, n, f->f)) : -1;
    }
    bool seekFileCur(int off, FileHandle h) override { auto* f = static_cast<HFile*>(h); return f && f->f && fseek(f->f, off, SEEK_CUR) == 0; }
    bool seekFile(uint32_t pos, FileHandle h) override { auto* f = static_cast<HFile*>(h); return f && f->f && fseek(f->f, long(pos), SEEK_SET) == 0; }
    void closeFile(FileHandle h, bool) override
    {
        auto* f = static_cast<HFile*>(h);
        if (f && f->f) { fclose(f->f); f->f = nullptr; }
    }
    void endFile(FileHandle h) override { auto* f = static_cast<HFile*>(h); if (f && f->f) { fflush(f->f); if (ftruncate(fileno(f->f), ftell(f->f)) != 0) {} } }
    bool isFileOpen(FileHandle h) override { auto* f = static_cast<HFile*>(h); return f && f->f; }
    bool enableFastSeek(FileHandle, uint32_t*, uint32_t, uint32_t*, uint32_t*) override { return false; }
    bool readSectorsAsync(uint8_t*, uint32_t, uint32_t) override { return false; }
    bool pollAsyncRead(bool* ok) override { if (ok) *ok = false; return true; }
    bool waitAsyncRead() override { return true; }
    bool isAsyncReadBusy() override { return false; }

    // directory listings: names of the entries of path, folders first when asked (a stand-in: the file browsers are not drawn here)
    static std::vector<std::string> names(const std::string& dir, bool folders)
    {
        std::vector<std::string> v;
        if (DIR* d = opendir(dir.c_str()))
        {
            while (dirent* e = readdir(d))
            {
                if (e->d_name[0] == '.') continue;
                struct stat st;
                if (stat((dir + "/" + e->d_name).c_str(), &st) != 0) continue;
                if ((S_ISDIR(st.st_mode) != 0) == folders) v.push_back(e->d_name);
            }
            closedir(d);
        }
        std::sort(v.begin(), v.end());
        return v;
    }
    int createInstrumentsList(const char*, char**, uint8_t, uint32_t* sizeOut) override { if (sizeOut) *sizeOut = 0; return 0; }
    int countContainedFiles(const char* path) override { return int(names(abs(path), false).size()); }
    // the firmware's SdDir::createFilesList: frees what the list held, fills it from slot start_line on with new char[] entries
    // (folders as "/name", files by name), skips skip_count matches and returns the slot after the last entry, kListError when the
    // folder cannot be opened. Callers own the entries and delete them. A folder bigger than max_files_in_subdir is not split
    // into "(1-100)" parts here.
    static bool hasExt(const std::string& s, const char* e)
    {
        const size_t n = strlen(e);
        return s.size() > n && strcasecmp(s.c_str() + s.size() - n, e) == 0;
    }
    int createFilesList(const char* path, uint8_t startLine, char** list, uint8_t length, uint16_t, enListFileFilter filter, uint16_t skip, uint16_t, enListFileFilter) override
    {
        for (uint8_t i = 0; i < length; i++)
            if (list[i] != nullptr) { delete[] list[i]; list[i] = nullptr; }
        DIR* d = opendir(abs(path).c_str());
        if (d == nullptr) return kListError;
        closedir(d);
        const bool folders = filter == enListFileFilter_folder;
        auto v = names(abs(path), folders);
        int n = startLine;
        for (size_t i = 0; i < v.size() && n < length; i++)
        {
            if (!folders && filter == enListFileFilter_wave && !hasExt(v[i], ".wav")) continue;
            if (!folders && filter == enListFileFilter_wave_inst && !hasExt(v[i], ".wav") && !hasExt(v[i], ".pti")) continue;
            if (skip > 0) { skip--; continue; }
            const std::string e = folders ? "/" + v[i] : v[i];
            list[n] = new char[e.size() + 1];
            memcpy(list[n], e.c_str(), e.size() + 1);
            n++;
        }
        return n;
    }
    void listDirContents(const char* dir, DirContentType type, std::function<bool(const char*)> nameFilter, char* buf, int bufSize, int maxNameLen, int& outCount, char** fileList) override
    {
        outCount = 0;
        auto v = names(abs(dir), type == kFolders);
        int used = 0;
        for (auto& s : v)
        {
            if (nameFilter && !nameFilter(s.c_str())) continue;
            const int len = int(std::min<size_t>(s.size(), size_t(maxNameLen - 1)));
            if (used + len + 1 > bufSize) break;
            memcpy(buf + used, s.c_str(), size_t(len));
            buf[used + len] = 0;
            if (fileList) fileList[outCount] = buf + used;
            used += len + 1;
            outCount++;
        }
    }
    int readNbFiles(const char*) override { return 0; }
    // one directory walk at a time, like the firmware's
    bool nesDir_open(const char* path) override
    {
        nesDir_close();
        nesBase_ = abs(path);
        nesDir_ = opendir(nesBase_.c_str());
        return nesDir_ != nullptr;
    }
    bool nesDir_readItem(char* name, uint8_t* isDir, uint8_t* isHid, uint32_t* size) override
    {
        while (nesDir_ != nullptr)
        {
            dirent* e = readdir(nesDir_);
            if (e == nullptr) return false;
            struct stat st;
            if (stat((nesBase_ + "/" + e->d_name).c_str(), &st) != 0) continue;
            strcpy(name, e->d_name);
            *isDir = S_ISDIR(st.st_mode) ? 1 : 0;
            *isHid = 0;
            if (size) *size = uint32_t(st.st_size);
            return true;
        }
        return false;
    }
    void nesDir_close() override
    {
        if (nesDir_ != nullptr) closedir(nesDir_);
        nesDir_ = nullptr;
    }

  private:
    DIR* nesDir_ = nullptr;
    std::string nesBase_;
};

class HPlatform : public Platform
{
  public:
    void setPowerMode(bool) override {}
    void resetMCU() override { g_resetRequested = true; }
    void runBootloader(char* name) override { hostp::simBootloaderHandoff(name); }
    void setPowerButtonCallback(PowerButtonCallback) override {}
    bool getPowerButtonState() override { return true; }
    bool readForceStartUp() override { return true; }
    Sgtl5000* getCodecAudio() override { return &codec; }
    Radio* getRadio() override { return &radio; }
    SwitchGroup* getButtons() override { return &buttons; }
    SwitchGroup* getPads() override { return &pads; }
    Leds* getLeds() override { return &leds; }
    Battery* getBattery() override { return &battery; }
    Timer* getTimer() override { return &timer; }
    Encoder* getEncoder() override { return &encoder; }
    DisplayDriver* getDisplayDriver() override { return &eve; }
    UsbAudio* getUsbAudio() override { return &usbAudio; }
    UartMidi* getUartMidi() override { return &uartMidi; }
    UsbMidi* getUsbMidi() override { return &usbMidi; }
    ConfigManager* getConfigManager() override { return &config; }
    FileSystem* getFileSystem() override { return &fs; }
    CrcCalculator* getCrcCalculator() override { return &crc; }
    SystemInfo* getSystemInfo() override { return &info; }
    Usb* getUsb() override { return &usb; }

    HTimer timer;
    HEncoder encoder;
    HBattery battery;
    HLeds leds;
    HSwitch buttons{false}, pads{true};
    HRadio radio;
    HCodec codec;
    HUsbAudio usbAudio;
    HUsbMidi usbMidi;
    HUartMidi uartMidi;
    HUsb usb;
    HCrc crc;
    HConfig config;
    HFileSystem fs;
    EveModel eve;
    SystemInfo info = {hostp::simBuildId()};
};

HPlatform* g_platform;
}   // namespace

Platform* platform()
{
    if (!g_platform) g_platform = new HPlatform;
    return g_platform;
}

EveModel& eve()
{
    platform();
    return g_platform->eve;
}
}   // namespace hostp
