// The unit's .pti files on the Mac. Host only, NOT firmware, nothing here is in a device image.
//
// A .pti written by the unit holds the instrument of the device: 372 bytes, 32 bit pointer, 4 byte alignment (pti_dev_layout.h). The host's
// strInstrument is larger (64 bit pointer, 8 byte alignment), and the firmware reads and writes the file with sizeof(strInstrumentFile) as a
// byte count and as the start of the sample data, so it cannot read the card's file. This file gives it the host's picture of the file, and
// keeps the card in the device's layout, so a card folder is the same bytes on the unit and under the sim:
//
//   1. PtiFs (ptiFileSystem) wraps the host FileSystem. A .pti that holds a device layout head is a virtual file: its first
//      sizeof(strInstrumentFile) bytes are the host head (translated field by field, ptiDevToHost), the sample data follows. A write into the
//      head goes back through ptiHostToDev (field by field, the CRC-32 recomputed, the padding bytes and the stale 32 bit pointer of the old
//      file kept), a write after it is a write of sample data at the device's offset. Files that are not in the device layout (a .wav, a new file
//      that does not start with a host head, a console put of the card's bytes) pass through untouched.
//   2. InstrumentFileValidation::validate is the firmware's (the source is included below under another name) with one host difference:
//      its data offset is 16 + size + 4, which is sizeof(strInstrumentFile) on the device. On the host the struct is padded to 8 bytes, so the
//      offset of the sample data is sizeof(strInstrumentFile) there, which is what the other half of the firmware (save, import, stream setup,
//      isPtiFileValid) already uses. The file's core/FileManager/instrumentFileValidation.cpp is in exclude.txt.
#include "host_pti.h"

#include "fileManagerDefs.h"
#include "pti_dev_layout.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>

// ------------------------------------------------------------------------------------------------ the firmware's validator, renamed
#define validate validateDeviceLayout
#include "instrumentFileValidation.cpp"
#undef validate

namespace InstrumentFileValidation
{
bool validate(const uint8_t* data, uint32_t loadedSize, uint32_t fileSize, uint16_t currentInstrumentSize, uint8_t currentFileVersion, Result& result)
{
    if (!validateDeviceLayout(data, loadedSize, fileSize, currentInstrumentSize, currentFileVersion, result)) return false;
    // see the top of host_pti.cpp: the host's data offset is sizeof(strInstrumentFile), when the file has its CRC and the size of this struct
    if (result.hasCrc && result.instrumentSize == sizeof(strInstrument) && fileSize >= sizeof(strInstrumentFile) && loadedSize >= sizeof(strInstrumentFile))
        result.dataOffset = sizeof(strInstrumentFile);
    return true;
}
}   // namespace InstrumentFileValidation

namespace
{
using namespace ptidev;

// ------------------------------------------------------------------------------------------------ the device's instrument
// natural alignment, no pointers, so the same layout on the arm and on the Mac (the offsets are asserted against pti_dev_layout.h)
struct DevEnv
{
    float amount;
    uint16_t delay, attack, hold, decay;
    float sustain;
    uint16_t release;
    uint8_t loop, enable;
};
struct DevLfo
{
    uint8_t shape, speed, wtBend, wtSust;
    float amount;
};
struct DevSample
{
    uint8_t type;
    char file_name[32];
    uint32_t address;
    uint32_t length;
    uint16_t wavetable_window_size;
    uint32_t wavetableWindowNumber;
};
struct DevGranular
{
    uint16_t grainLength, currentPosition;
    uint8_t shape, type;
};
struct DevInstrument
{
    uint8_t isActive;
    DevSample sample;
    char name[4];
    uint8_t playMode;
    uint16_t startPoint, loopPoint1, loopPoint2, endPoint;
    uint32_t wavetableCurrentWindow;
    DevEnv envelope[kEnvMax];
    DevLfo lfo[kEnvMax];
    float cutOff, resonance;
    uint8_t filterType, filterEnable;
    int8_t tune, fineTune;
    uint8_t volume;
    uint16_t glide;
    int16_t panning;
    uint8_t delaySend, slicePlayThru;
    uint16_t slices[kSlices];
    uint8_t sliceNumber, selectedSlice;
    DevGranular granular;
    uint8_t reverbSend, overdrive, bitDepth;
    int8_t tuneResample;
};

#define OFF(T, m, n) static_assert(offsetof(T, m) == (n), #T "::" #m " is not where the device has it")
static_assert(sizeof(DevInstrument) == kInstrument, "the device instrument is 372 bytes");
static_assert(sizeof(DevEnv) == kEnvSize && sizeof(DevLfo) == kLfoSize && sizeof(DevSample) == kSampleSize && sizeof(DevGranular) == kGranularSize, "sizes");
OFF(DevInstrument, isActive, oIsActive);
OFF(DevInstrument, sample, oSample);
OFF(DevSample, type, oSampleType);
OFF(DevSample, file_name, oSampleFileName);
OFF(DevSample, address, oSampleAddress);
OFF(DevSample, length, oSampleLength);
OFF(DevSample, wavetable_window_size, oSampleWindowSize);
OFF(DevSample, wavetableWindowNumber, oSampleWindowNumber);
OFF(DevInstrument, name, oName);
OFF(DevInstrument, playMode, oPlayMode);
OFF(DevInstrument, startPoint, oStartPoint);
OFF(DevInstrument, loopPoint1, oLoopPoint1);
OFF(DevInstrument, loopPoint2, oLoopPoint2);
OFF(DevInstrument, endPoint, oEndPoint);
OFF(DevInstrument, wavetableCurrentWindow, oWavetableCurrentWindow);
OFF(DevInstrument, envelope, oEnvelope);
OFF(DevEnv, amount, oEnvAmount);
OFF(DevEnv, delay, oEnvDelay);
OFF(DevEnv, attack, oEnvAttack);
OFF(DevEnv, hold, oEnvHold);
OFF(DevEnv, decay, oEnvDecay);
OFF(DevEnv, sustain, oEnvSustain);
OFF(DevEnv, release, oEnvRelease);
OFF(DevEnv, loop, oEnvLoop);
OFF(DevEnv, enable, oEnvEnable);
OFF(DevInstrument, lfo, oLfo);
OFF(DevLfo, amount, oLfoAmount);
OFF(DevInstrument, cutOff, oCutOff);
OFF(DevInstrument, resonance, oResonance);
OFF(DevInstrument, filterType, oFilterType);
OFF(DevInstrument, filterEnable, oFilterEnable);
OFF(DevInstrument, tune, oTune);
OFF(DevInstrument, fineTune, oFineTune);
OFF(DevInstrument, volume, oVolume);
OFF(DevInstrument, glide, oGlide);
OFF(DevInstrument, panning, oPanning);
OFF(DevInstrument, delaySend, oDelaySend);
OFF(DevInstrument, slicePlayThru, oSlicePlayThru);
OFF(DevInstrument, slices, oSlices);
OFF(DevInstrument, sliceNumber, oSliceNumber);
OFF(DevInstrument, selectedSlice, oSelectedSlice);
OFF(DevInstrument, granular, oGranular);
OFF(DevGranular, grainLength, oGranGrainLength);
OFF(DevGranular, currentPosition, oGranCurrentPosition);
OFF(DevGranular, shape, oGranShape);
OFF(DevGranular, type, oGranType);
OFF(DevInstrument, reverbSend, oReverbSend);
OFF(DevInstrument, overdrive, oOverdrive);
OFF(DevInstrument, bitDepth, oBitDepth);
OFF(DevInstrument, tuneResample, oTuneResample);
#undef OFF
static_assert(envMax == kEnvMax && MAX_SLICE_NUMBER == kSlices && SAMPLE_NAME_SIZE + 1 == sizeof(DevSample::file_name), "the host's counts");
static_assert(sizeof(envelopeGenerator::strEnv) == kEnvSize && sizeof(strInstrument::strEnvBasedLfo) == kLfoSize, "the arrays copy as they are");
static_assert(sizeof(strInstrument::strGranular) == kGranularSize, "granular");

constexpr size_t kInstrOff = offsetof(strInstrumentFile::strInstrumentDataAndHeader, instrument);
constexpr size_t kCrcOff = offsetof(strInstrumentFile, crc);
static_assert(sizeof(strProjectFileHeader) <= kHeader && kInstrOff >= kHeader, "the header");

// One field of the host instrument into the device's: same type, so a type that drifts stops the build here
template <typename D, typename H>
void put(D& d, const H& h)
{
    static_assert(sizeof(D) == sizeof(H), "field size");
    d = static_cast<D>(h);
}
template <typename D, typename H, size_t N>
void putArr(D (&d)[N], const H (&h)[N])
{
    static_assert(sizeof(D) == sizeof(H), "element size");
    memcpy(d, h, sizeof d);
}

uint32_t crc32(const uint8_t* p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;   // CRC-32 (IEEE), the same as the host's CrcCalculator and the unit's files (checked on every file of a project)
    for (size_t i = 0; i < n; i++)
    {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
}   // namespace

namespace hostp
{
size_t ptiHostHead() { return sizeof(strInstrumentFile); }

static bool headOk(const uint8_t* h, size_t avail, size_t size)
{
    return avail >= kHeader && h[0] == 'T' && h[1] == 'I' && u16(h + 2) == 1 && h[8] >= 8 && h[9] == h[8] && h[10] == h[8] && h[11] == h[8] && u16(h + kSizeField) == size;
}
bool ptiIsDeviceHead(const uint8_t* h, size_t avail) { return headOk(h, avail, kInstrument); }
bool ptiIsHostHead(const uint8_t* h, size_t avail) { return headOk(h, avail, sizeof(strInstrument)); }

void ptiDevToHost(const uint8_t* dev, uint8_t* host)
{
    memset(host, 0, sizeof(strInstrumentFile));
    memcpy(host, dev, 14);   // id, type, fwVersion, fileStructureVersion, size
    const uint16_t size = sizeof(strInstrument);
    host[kSizeField] = uint8_t(size & 0xFF);
    host[kSizeField + 1] = uint8_t(size >> 8);
    DevInstrument d;
    memcpy(&d, dev + kHeader, sizeof d);
    strInstrument& h = *reinterpret_cast<strInstrument*>(host + kInstrOff);
    put(h.isActive, d.isActive);
    put(h.sample.type, d.sample.type);
    memcpy(h.sample.file_name, d.sample.file_name, sizeof d.sample.file_name);
    h.sample.address = nullptr;   // the unit's pointer means nothing here, the loader sets it
    put(h.sample.length, d.sample.length);
    put(h.sample.wavetable_window_size, d.sample.wavetable_window_size);
    put(h.sample.wavetableWindowNumber, d.sample.wavetableWindowNumber);
    memcpy(h.name, d.name, sizeof d.name);
    put(h.playMode, d.playMode);
    put(h.startPoint, d.startPoint);
    put(h.loopPoint1, d.loopPoint1);
    put(h.loopPoint2, d.loopPoint2);
    put(h.endPoint, d.endPoint);
    put(h.wavetableCurrentWindow, d.wavetableCurrentWindow);
    for (int i = 0; i < envMax; i++)
    {
        put(h.envelope[i].amount, d.envelope[i].amount);
        put(h.envelope[i].delay, d.envelope[i].delay);
        put(h.envelope[i].attack, d.envelope[i].attack);
        put(h.envelope[i].hold, d.envelope[i].hold);
        put(h.envelope[i].decay, d.envelope[i].decay);
        put(h.envelope[i].sustain, d.envelope[i].sustain);
        put(h.envelope[i].release, d.envelope[i].release);
        put(h.envelope[i].loop, d.envelope[i].loop);
        put(h.envelope[i].enable, d.envelope[i].enable);
        put(h.lfo[i].shape, d.lfo[i].shape);
        put(h.lfo[i].speed, d.lfo[i].speed);
        put(h.lfo[i].wtBend, d.lfo[i].wtBend);
        put(h.lfo[i].wtSust, d.lfo[i].wtSust);
        put(h.lfo[i].amount, d.lfo[i].amount);
    }
    put(h.cutOff, d.cutOff);
    put(h.resonance, d.resonance);
    put(h.filterType, d.filterType);
    put(h.filterEnable, d.filterEnable);
    put(h.tune, d.tune);
    put(h.fineTune, d.fineTune);
    put(h.volume, d.volume);
    put(h.glide, d.glide);
    put(h.panning, d.panning);
    put(h.delaySend, d.delaySend);
    put(h.slicePlayThru, d.slicePlayThru);
    putArr(h.slices, d.slices);
    put(h.sliceNumber, d.sliceNumber);
    put(h.selectedSlice, d.selectedSlice);
    put(h.granular.grainLength, d.granular.grainLength);
    put(h.granular.currentPosition, d.granular.currentPosition);
    put(h.granular.shape, d.granular.shape);
    put(h.granular.type, d.granular.type);
    put(h.reverbSend, d.reverbSend);
    put(h.overdrive, d.overdrive);
    put(h.bitDepth, d.bitDepth);
    put(h.tuneResample, d.tuneResample);
    memcpy(host + kCrcOff, dev + kHeader + kInstrument, kCrc);   // as the card has it, the firmware writes a new one when it saves
}

static void instrToDev(const strInstrument& h, DevInstrument& d)
{
    put(d.isActive, h.isActive);
    put(d.sample.type, h.sample.type);
    memcpy(d.sample.file_name, h.sample.file_name, sizeof d.sample.file_name);
    put(d.sample.length, h.sample.length);
    put(d.sample.wavetable_window_size, h.sample.wavetable_window_size);
    put(d.sample.wavetableWindowNumber, h.sample.wavetableWindowNumber);
    memcpy(d.name, h.name, sizeof d.name);
    put(d.playMode, h.playMode);
    put(d.startPoint, h.startPoint);
    put(d.loopPoint1, h.loopPoint1);
    put(d.loopPoint2, h.loopPoint2);
    put(d.endPoint, h.endPoint);
    put(d.wavetableCurrentWindow, h.wavetableCurrentWindow);
    for (int i = 0; i < envMax; i++)
    {
        put(d.envelope[i].amount, h.envelope[i].amount);
        put(d.envelope[i].delay, h.envelope[i].delay);
        put(d.envelope[i].attack, h.envelope[i].attack);
        put(d.envelope[i].hold, h.envelope[i].hold);
        put(d.envelope[i].decay, h.envelope[i].decay);
        put(d.envelope[i].sustain, h.envelope[i].sustain);
        put(d.envelope[i].release, h.envelope[i].release);
        put(d.envelope[i].loop, h.envelope[i].loop);
        put(d.envelope[i].enable, h.envelope[i].enable);
        put(d.lfo[i].shape, h.lfo[i].shape);
        put(d.lfo[i].speed, h.lfo[i].speed);
        put(d.lfo[i].wtBend, h.lfo[i].wtBend);
        put(d.lfo[i].wtSust, h.lfo[i].wtSust);
        put(d.lfo[i].amount, h.lfo[i].amount);
    }
    put(d.cutOff, h.cutOff);
    put(d.resonance, h.resonance);
    put(d.filterType, h.filterType);
    put(d.filterEnable, h.filterEnable);
    put(d.tune, h.tune);
    put(d.fineTune, h.fineTune);
    put(d.volume, h.volume);
    put(d.glide, h.glide);
    put(d.panning, h.panning);
    put(d.delaySend, h.delaySend);
    put(d.slicePlayThru, h.slicePlayThru);
    putArr(d.slices, h.slices);
    put(d.sliceNumber, h.sliceNumber);
    put(d.selectedSlice, h.selectedSlice);
    put(d.granular.grainLength, h.granular.grainLength);
    put(d.granular.currentPosition, h.granular.currentPosition);
    put(d.granular.shape, h.granular.shape);
    put(d.granular.type, h.granular.type);
    put(d.reverbSend, h.reverbSend);
    put(d.overdrive, h.overdrive);
    put(d.bitDepth, h.bitDepth);
    put(d.tuneResample, h.tuneResample);
}

void ptiHostToDev(const uint8_t* host, const uint8_t* prevDev, uint8_t* dev)
{
    if (prevDev)
        memcpy(dev, prevDev, kFile);
    else
        memset(dev, 0, kFile);
    memcpy(dev, host, 14);
    dev[kSizeField] = uint8_t(kInstrument & 0xFF);
    dev[kSizeField + 1] = uint8_t(kInstrument >> 8);
    DevInstrument d;
    memcpy(&d, dev + kHeader, sizeof d);   // the padding bytes and the stale pointer stay what the file had
    instrToDev(*reinterpret_cast<const strInstrument*>(host + kInstrOff), d);
    memcpy(dev + kHeader, &d, sizeof d);
    const uint32_t crc = crc32(dev, kHeader + kInstrument);
    for (int i = 0; i < 4; i++) dev[kHeader + kInstrument + i] = uint8_t(crc >> (8 * i));
}

void ptiInstrumentToDev(const void* hostInstrument, uint8_t* dev372)
{
    DevInstrument d;
    memset(&d, 0, sizeof d);
    instrToDev(*static_cast<const strInstrument*>(hostInstrument), d);
    memcpy(dev372, &d, sizeof d);
}

// ------------------------------------------------------------------------------------------------ the file system
namespace
{
bool g_raw = false;

bool isPti(const char* path)
{
    const size_t n = path ? strlen(path) : 0;
    return n > 4 && strcasecmp(path + n - 4, ".pti") == 0;
}

// What an open file is: Raw (as the card has it), View (a device layout .pti), Undecided (a new empty file: the first write says)
struct Open
{
    enum Kind { Raw, View, Undecided } kind = Raw;
    uint32_t pos = 0;
    std::vector<uint8_t> host;   // the head as the firmware sees it, sizeof(strInstrumentFile) bytes
    uint8_t dev[kFile] = {};     // the head as the card has it
    bool hasDev = false;
};

class PtiFs : public FileSystem
{
  public:
    explicit PtiFs(FileSystem* inner) : in_(inner) {}

    bool isSdCardInserted() override { return in_->isSdCardInserted(); }
    void setMountEventCallback(CardStateChangeCallback f) override { in_->setMountEventCallback(f); }
    bool remount() override { open_.clear(); return in_->remount(); }
    bool exists(const char* p) override { return in_->exists(p); }
    bool rename(const char* a, const char* b) override { return in_->rename(a, b); }
    bool mkdir(uint8_t hidden, const char* p, bool pFlag) override { return in_->mkdir(hidden, p, pFlag); }
    bool remove(const char* p) override { return in_->remove(p); }
    bool removeDirWithFiles(const char* p) override { return in_->removeDirWithFiles(p); }
    bool removeOnlyDirFiles(const char* p) override { return in_->removeOnlyDirFiles(p); }

    size_t getFileSize(const char* path) override
    {
        const size_t n = in_->getFileSize(path);
        return !g_raw && isPti(path) && deviceFile(path, n) ? viewSize(n) : n;
    }
    size_t getFileSize(FileHandle h) override
    {
        const size_t n = in_->getFileSize(h);
        Open* o = find(h);
        return o && o->kind == Open::View ? viewSize(n) : n;
    }
    size_t getFileAvailable(FileHandle h) override
    {
        Open* o = find(h);
        if (!o || o->kind != Open::View) return in_->getFileAvailable(h);
        const size_t size = getFileSize(h);
        return o->pos < size ? size - o->pos : 0;
    }

    std::tuple<bool, FileHandle> openFile(const char* path, FileOpenMode mode, FileHandle* fh) override
    {
        auto r = in_->openFile(path, mode, fh);
        FileHandle h = std::get<1>(r);
        if (h) open_.erase(h);
        if (!std::get<0>(r) || g_raw || !isPti(path) || mode == kWriteAppend) return r;
        Open o;
        const size_t size = in_->getFileSize(h);
        if (mode == kWriteCreateNew || (mode == kWriteOpen && size == 0))
        {
            o.kind = Open::Undecided;
        }
        else if (size >= kFile)
        {
            uint8_t head[kFile];
            in_->seekFile(0, h);
            const int32_t got = in_->readFile(head, kFile, h);
            in_->seekFile(0, h);
            if (got == int32_t(kFile) && ptiIsDeviceHead(head, kFile))
            {
                o.kind = Open::View;
                memcpy(o.dev, head, kFile);
                o.hasDev = true;
                o.host.resize(sizeof(strInstrumentFile));
                ptiDevToHost(o.dev, o.host.data());
            }
        }
        if (o.kind != Open::Raw) open_[h] = std::move(o);
        return r;
    }

    int32_t readFile(void* d, size_t n, FileHandle h) override
    {
        Open* o = find(h);
        if (!o || o->kind != Open::View) return in_->readFile(d, n, h);
        const size_t size = getFileSize(h);
        if (o->pos >= size) return 0;
        n = std::min(n, size - o->pos);
        uint8_t* out = static_cast<uint8_t*>(d);
        size_t done = 0;
        const size_t head = o->host.size();
        if (o->pos < head)
        {
            const size_t k = std::min(n, head - o->pos);
            memcpy(out, o->host.data() + o->pos, k);
            done = k;
        }
        if (done < n)
        {
            in_->seekFile(uint32_t(kFile + (o->pos + done - head)), h);
            const int32_t got = in_->readFile(out + done, n - done, h);
            if (got > 0) done += size_t(got);
        }
        o->pos += uint32_t(done);
        return int32_t(done);
    }

    int32_t writeFile(const void* d, size_t n, FileHandle h) override
    {
        Open* o = find(h);
        if (!o) return in_->writeFile(d, n, h);
        const uint8_t* src = static_cast<const uint8_t*>(d);
        if (o->kind == Open::Undecided)
        {
            // the first write of a new file says which layout it is in: the host's head is translated, anything else is the card's bytes
            if (o->pos == 0 && ptiIsHostHead(src, n))
            {
                o->kind = Open::View;
                o->host.assign(sizeof(strInstrumentFile), 0);
            }
            else
            {
                o->kind = Open::Raw;
            }
        }
        if (o->kind == Open::Raw)
        {
            open_.erase(h);
            return in_->writeFile(d, n, h);
        }
        const size_t head = o->host.size();
        size_t done = 0;
        if (o->pos < head)
        {
            const size_t k = std::min(n, head - o->pos);
            memcpy(o->host.data() + o->pos, src, k);
            done = k;
            flushHead(o, h);
        }
        if (done < n)
        {
            in_->seekFile(uint32_t(kFile + (o->pos + done - head)), h);
            const int32_t w = in_->writeFile(src + done, n - done, h);
            if (w < 0) return w;
            done += size_t(w);
        }
        o->pos += uint32_t(done);
        return int32_t(done);
    }

    bool seekFileCur(int off, FileHandle h) override
    {
        Open* o = find(h);
        if (!o || o->kind == Open::Raw) return in_->seekFileCur(off, h);
        const long p = long(o->pos) + off;
        if (p < 0) return false;
        o->pos = uint32_t(p);
        return true;
    }
    bool seekFile(uint32_t pos, FileHandle h) override
    {
        Open* o = find(h);
        if (!o || o->kind == Open::Raw) return in_->seekFile(pos, h);
        o->pos = pos;
        return true;
    }
    void closeFile(FileHandle h, bool del) override
    {
        open_.erase(h);
        in_->closeFile(h, del);
    }
    void endFile(FileHandle h) override
    {
        Open* o = find(h);
        if (!o || o->kind != Open::View) { in_->endFile(h); return; }
        in_->seekFile(uint32_t(o->pos >= o->host.size() ? kFile + (o->pos - o->host.size()) : kFile), h);   // the head stays
        in_->endFile(h);
    }
    bool isFileOpen(FileHandle h) override { return in_->isFileOpen(h); }
    bool enableFastSeek(FileHandle h, uint32_t* a, uint32_t b, uint32_t* c, uint32_t* d) override { return in_->enableFastSeek(h, a, b, c, d); }
    bool readSectorsAsync(uint8_t* a, uint32_t b, uint32_t c) override { return in_->readSectorsAsync(a, b, c); }
    bool pollAsyncRead(bool* ok) override { return in_->pollAsyncRead(ok); }
    bool waitAsyncRead() override { return in_->waitAsyncRead(); }
    bool isAsyncReadBusy() override { return in_->isAsyncReadBusy(); }

    int createInstrumentsList(const char* p, char** l, uint8_t n, uint32_t* s) override { return in_->createInstrumentsList(p, l, n, s); }
    int countContainedFiles(const char* p) override { return in_->countContainedFiles(p); }
    int createFilesList(const char* p, uint8_t s, char** l, uint8_t n, uint16_t m, enListFileFilter f, uint16_t sk, uint16_t mx, enListFileFilter ff) override
    {
        return in_->createFilesList(p, s, l, n, m, f, sk, mx, ff);
    }
    void listDirContents(const char* d, DirContentType t, std::function<bool(const char*)> f, char* b, int bs, int ml, int& c, char** fl) override
    {
        in_->listDirContents(d, t, f, b, bs, ml, c, fl);
    }
    int readNbFiles(const char* p) override { return in_->readNbFiles(p); }
    bool nesDir_open(const char* p) override { dir_ = p ? p : ""; return in_->nesDir_open(p); }
    bool nesDir_readItem(char* name, uint8_t* isDir, uint8_t* isHid, uint32_t* size) override
    {
        const bool ok = in_->nesDir_readItem(name, isDir, isHid, size);
        if (ok && size && isDir && !*isDir && !g_raw && isPti(name))
        {
            const std::string full = dir_ + "/" + name;
            if (deviceFile(full.c_str(), *size)) *size = uint32_t(viewSize(*size));
        }
        return ok;
    }
    void nesDir_close() override { in_->nesDir_close(); }

  private:
    Open* find(FileHandle h)
    {
        auto it = open_.find(h);
        return it == open_.end() ? nullptr : &it->second;
    }
    static size_t viewSize(size_t cardSize) { return (cardSize >= kFile ? cardSize - kFile : 0) + sizeof(strInstrumentFile); }
    bool deviceFile(const char* path, size_t size)
    {
        if (size < kFile) return false;
        auto r = in_->openFile(path, kRead, nullptr);
        if (!std::get<0>(r)) return false;
        uint8_t head[kHeader];
        const int32_t got = in_->readFile(head, kHeader, std::get<1>(r));
        in_->closeFile(std::get<1>(r), false);
        return got == int32_t(kHeader) && ptiIsDeviceHead(head, kHeader);
    }
    // the head of the file as the card must have it: the host's bytes translated, written at 0
    void flushHead(Open* o, FileHandle h)
    {
        uint8_t dev[kFile];
        ptiHostToDev(o->host.data(), o->hasDev ? o->dev : nullptr, dev);
        memcpy(o->dev, dev, kFile);
        o->hasDev = true;
        in_->seekFile(0, h);
        in_->writeFile(dev, kFile, h);
    }

    FileSystem* in_;
    std::unordered_map<FileHandle, Open> open_;
    std::string dir_;
};
}   // namespace

FileSystem* ptiFileSystem(FileSystem* inner)
{
    static PtiFs* fs = nullptr;
    if (!fs) fs = new PtiFs(inner);
    return fs;
}
void ptiRaw(bool on) { g_raw = on; }
bool ptiIsRaw() { return g_raw; }
}   // namespace hostp
