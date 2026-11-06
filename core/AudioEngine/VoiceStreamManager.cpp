#include "VoiceStreamManager.h"
#include "cr_section_macros.h"
#include "log.h"
#include <string.h>

extern strMtProject mtProject;

VoiceStreamManager voiceStreamManager;

static constexpr uint32_t kSectorSize = 512;
static constexpr uint32_t kStagingBufferBytes =
  (VoiceStreamManager::kChunkFrames * sizeof(int16_t)) + (2 * kSectorSize);

__NOINIT(EXTERNAL_RAM)
__attribute__((aligned(32))) static int16_t
  s_voiceRingBuffers[VoiceStreamManager::kVoiceCount][VoiceStreamManager::kMaxChannels]
                    [VoiceStreamManager::kRingFrames];

__NOINIT(EXTERNAL_RAM)
__attribute__((aligned(32))) static int16_t
  s_voiceLoopWrapBuffers[VoiceStreamManager::kVoiceCount][VoiceStreamManager::kMaxChannels]
                        [VoiceStreamManager::kLoopWrapFrames];

#ifdef PLATFORM_IMX
__NOINIT(EXTERNAL_RAM)
#endif
__attribute__((aligned(32))) static uint8_t s_dmaStagingBuffer[kStagingBufferBytes];

__NOINIT(EXTERNAL_RAM)
static char s_instrumentStreamPaths[VoiceStreamManager::kMaxInstruments][256];

__NOINIT(EXTERNAL_RAM)
static uint32_t s_instrumentClmt[VoiceStreamManager::kMaxInstruments][kClmtElementCount];

__NOINIT(EXTERNAL_RAM)
static WaveformPeakPoint s_instrumentWaveformPeaks[VoiceStreamManager::kMaxInstruments]
                                                  [VoiceStreamManager::kMaxChannels]
                                                  [kWaveformPeakPoints];

struct SliceCacheEntry
{
    uint32_t startFrame;
    uint16_t frameCount;
    uint16_t bufferOffset;
};

__NOINIT(EXTERNAL_RAM)
static SliceCacheEntry s_instrumentSliceCache[VoiceStreamManager::kMaxInstruments][MAX_SLICE_NUMBER];

static bool s_instrumentSliceCacheValid[VoiceStreamManager::kMaxInstruments];

void VoiceStreamManager::init(FileSystem* fileSystem)
{
    if (fileSystem != nullptr)
    {
        fs = fileSystem;
    }
    currentJob = AsyncJob{};
    for (int v = 0; v < kVoiceCount; ++v)
    {
        voices[v] = VoiceStreamState{};
    }
    for (int i = 0; i < kMaxInstruments; ++i)
    {
        clearInstrumentMetadata(i);
    }
}

char* VoiceStreamManager::getInstrumentStreamPath(int instrIdx)
{
    if (instrIdx < 0 || instrIdx >= kMaxInstruments)
    {
        return nullptr;
    }
    return s_instrumentStreamPaths[instrIdx];
}

const char* VoiceStreamManager::getInstrumentStreamPath(int instrIdx) const
{
    if (instrIdx < 0 || instrIdx >= kMaxInstruments)
    {
        return nullptr;
    }
    return s_instrumentStreamPaths[instrIdx];
}

uint32_t* VoiceStreamManager::getInstrumentClmt(int instrIdx)
{
    if (instrIdx < 0 || instrIdx >= kMaxInstruments)
    {
        return nullptr;
    }
    return s_instrumentClmt[instrIdx];
}

const uint32_t* VoiceStreamManager::getInstrumentClmt(int instrIdx) const
{
    if (instrIdx < 0 || instrIdx >= kMaxInstruments)
    {
        return nullptr;
    }
    return s_instrumentClmt[instrIdx];
}

WaveformPeakPoint* VoiceStreamManager::getInstrumentWaveformPeaks(int instrIdx, int channel)
{
    if (instrIdx < 0 || instrIdx >= kMaxInstruments)
    {
        return nullptr;
    }
    int ch = (channel >= 0 && channel < kMaxChannels) ? channel : 0;
    return s_instrumentWaveformPeaks[instrIdx][ch];
}

const WaveformPeakPoint* VoiceStreamManager::getInstrumentWaveformPeaks(int instrIdx,
                                                                        int channel) const
{
    if (instrIdx < 0 || instrIdx >= kMaxInstruments)
    {
        return nullptr;
    }
    int ch = (channel >= 0 && channel < kMaxChannels) ? channel : 0;
    return s_instrumentWaveformPeaks[instrIdx][ch];
}

void VoiceStreamManager::clearInstrumentMetadata(int instrIdx)
{
    if (instrIdx < 0 || instrIdx >= kMaxInstruments)
    {
        return;
    }
    invalidateInstrumentVoices(instrIdx);
    s_instrumentStreamPaths[instrIdx][0] = '\0';
    memset(s_instrumentClmt[instrIdx], 0, sizeof(s_instrumentClmt[instrIdx]));
    memset(s_instrumentSliceCache[instrIdx], 0, sizeof(s_instrumentSliceCache[instrIdx]));
    s_instrumentSliceCacheValid[instrIdx] = false;
    mtProject.sampleMemory[instrIdx].isStreamed = false;
    mtProject.sampleMemory[instrIdx].isStereo = false;
    mtProject.sampleMemory[instrIdx].hasValidClmt = false;
    mtProject.sampleMemory[instrIdx].hasWaveformPeaks = false;
    mtProject.sampleMemory[instrIdx].ramFrames = 0;
    mtProject.sampleMemory[instrIdx].preCacheFrames = 0;
    mtProject.sampleMemory[instrIdx].fileDataOffset = 0;
    mtProject.sampleMemory[instrIdx].databaseSector = 0;
    mtProject.sampleMemory[instrIdx].sectorsPerCluster = 0;
    mtProject.sampleMemory[instrIdx].sampleHash = 0;
}

void VoiceStreamManager::copyInstrumentMetadata(int srcInstrIdx, int dstInstrIdx)
{
    if (srcInstrIdx < 0 || srcInstrIdx >= kMaxInstruments || dstInstrIdx < 0 ||
        dstInstrIdx >= kMaxInstruments || srcInstrIdx == dstInstrIdx)
    {
        return;
    }
    memcpy(s_instrumentStreamPaths[dstInstrIdx],
           s_instrumentStreamPaths[srcInstrIdx],
           sizeof(s_instrumentStreamPaths[0]));
    memcpy(s_instrumentClmt[dstInstrIdx],
           s_instrumentClmt[srcInstrIdx],
           sizeof(s_instrumentClmt[0]));
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        memcpy(s_instrumentWaveformPeaks[dstInstrIdx][ch],
               s_instrumentWaveformPeaks[srcInstrIdx][ch],
               sizeof(s_instrumentWaveformPeaks[0][0]));
    }
    memcpy(s_instrumentSliceCache[dstInstrIdx],
           s_instrumentSliceCache[srcInstrIdx],
           sizeof(s_instrumentSliceCache[0]));
    s_instrumentSliceCacheValid[dstInstrIdx] = s_instrumentSliceCacheValid[srcInstrIdx];
    const auto& srcMem = mtProject.sampleMemory[srcInstrIdx];
    auto& dstMem = mtProject.sampleMemory[dstInstrIdx];
    dstMem.isStreamed = srcMem.isStreamed;
    dstMem.isStereo = srcMem.isStereo;
    dstMem.hasValidClmt = srcMem.hasValidClmt;
    dstMem.hasWaveformPeaks = srcMem.hasWaveformPeaks;
    dstMem.ramFrames = srcMem.ramFrames;
    dstMem.preCacheFrames = srcMem.preCacheFrames;
    dstMem.fileDataOffset = srcMem.fileDataOffset;
    dstMem.databaseSector = srcMem.databaseSector;
    dstMem.sectorsPerCluster = srcMem.sectorsPerCluster;
    dstMem.sampleHash = srcMem.sampleHash;
}

void VoiceStreamManager::buildWaveformPeaksFromRam(int instrIdx,
                                                   int channel,
                                                   const int16_t* samples,
                                                   uint32_t totalFrames)
{
    if (instrIdx < 0 || instrIdx >= kMaxInstruments || samples == nullptr || totalFrames == 0)
    {
        return;
    }
    int ch = (channel >= 0 && channel < kMaxChannels) ? channel : 0;
    WaveformPeakPoint* peaks = s_instrumentWaveformPeaks[instrIdx][ch];
    for (uint32_t p = 0; p < kWaveformPeakPoints; ++p)
    {
        uint32_t start = (uint32_t)(((uint64_t)p * totalFrames) / kWaveformPeakPoints);
        uint32_t end = (uint32_t)(((uint64_t)(p + 1) * totalFrames) / kWaveformPeakPoints);
        if (end <= start)
        {
            end = start + 1;
        }
        if (end > totalFrames)
        {
            end = totalFrames;
        }
        uint32_t step = 1;
        if (end - start > 64)
        {
            step = (end - start) / 64;
        }
        int16_t low = 0;
        int16_t up = 0;
        for (uint32_t i = start; i < end; i += step)
        {
            int16_t v = samples[i];
            if (v > up)
                up = v;
            else if (v < low)
                low = v;
        }
        if (totalFrames < kWaveformPeakPoints)
        {
            int8_t val = (int8_t)(samples[start] / 300);
            peaks[p].minVal = val;
            peaks[p].maxVal = val;
        }
        else
        {
            peaks[p].minVal = (int8_t)(low / 300);
            peaks[p].maxVal = (int8_t)(up / 300);
        }
    }
    mtProject.sampleMemory[instrIdx].hasWaveformPeaks = true;
}

bool VoiceStreamManager::setupInstrumentStream(int instrIdx,
                                               const char* filePath,
                                               uint32_t dataOffset,
                                               uint32_t totalFrames,
                                               bool isStereo)
{
    if (fs == nullptr || instrIdx < 0 || instrIdx >= kMaxInstruments || filePath == nullptr ||
        filePath[0] == '\0' || totalFrames == 0)
    {
        return false;
    }

    invalidateInstrumentVoices(instrIdx);

    auto& mem = mtProject.sampleMemory[instrIdx];
    uint32_t preCacheFrames =
      (totalFrames < kStreamPreCacheFrames) ? totalFrames : kStreamPreCacheFrames;

    strncpy(s_instrumentStreamPaths[instrIdx], filePath, PATCH_SIZE - 1);
    s_instrumentStreamPaths[instrIdx][PATCH_SIZE - 1] = '\0';

    mem.isStreamed = true;
    mem.isStereo = isStereo && (kMaxChannels > 1);
    mem.ramFrames = preCacheFrames;
    mem.preCacheFrames = preCacheFrames;
    mem.fileDataOffset = dataOffset;
    mem.hasValidClmt = false;
    mem.hasWaveformPeaks = false;
    mem.databaseSector = 0;
    mem.sectorsPerCluster = 0;

    auto [isOpen, fh] = fs->openFile(filePath, FileSystem::kRead);
    if (!isOpen || fh == nullptr)
    {
        return false;
    }

    s_instrumentClmt[instrIdx][0] = kClmtElementCount;
    uint32_t dbSector = 0;
    uint32_t csize = 0;
    bool clmtOk =
      fs->enableFastSeek(fh, s_instrumentClmt[instrIdx], kClmtElementCount, &dbSector, &csize);
    mem.hasValidClmt = clmtOk;
    mem.databaseSector = dbSector;
    mem.sectorsPerCluster = csize;

    // 1. Read Attack Pre-Cache into SDRAM for Left (and Right if stereo)
    int numChannels = mem.isStereo ? 2 : 1;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        int16_t* dst = mem.address[ch];
        if (dst == nullptr)
        {
            fs->closeFile(fh);
            return false;
        }
        uint32_t chOffset = dataOffset + (ch == 1 ? (totalFrames * sizeof(int16_t)) : 0);
        if (!fs->seekFile(chOffset, fh))
        {
            fs->closeFile(fh);
            return false;
        }
        size_t toRead = preCacheFrames * sizeof(int16_t);
        int32_t rd = fs->readFile(dst, toRead, fh);
        if (rd < 0)
        {
            fs->closeFile(fh);
            return false;
        }
        if (static_cast<size_t>(rd) < toRead)
        {
            memset(reinterpret_cast<uint8_t*>(dst) + rd, 0, toRead - rd);
        }
    }

    // Compute content hash of pre-cache + sample length for ring buffer reuse across copied instruments
    uint32_t hash = 2166136261u;
    hash = (hash ^ totalFrames) * 16777619u;
    hash = (hash ^ (uint32_t)numChannels) * 16777619u;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const int16_t* dst = mem.address[ch];
        if (dst != nullptr)
        {
            uint32_t step = (preCacheFrames > 256) ? (preCacheFrames / 256) : 1;
            for (uint32_t i = 0; i < preCacheFrames; i += step)
            {
                hash = (hash ^ (uint16_t)dst[i]) * 16777619u;
            }
        }
    }
    if (hash == 0)
    {
        hash = 1;
    }
    mem.sampleHash = hash;

    // 2. Build 600-point Waveform Peak Cache per channel
    int16_t windowBuf[64];
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const int16_t* ramBase = mem.address[ch];
        WaveformPeakPoint* peaks = s_instrumentWaveformPeaks[instrIdx][ch];
        uint32_t chOffset = dataOffset + (ch == 1 ? (totalFrames * sizeof(int16_t)) : 0);

        for (uint32_t p = 0; p < kWaveformPeakPoints; ++p)
        {
            uint32_t start = (uint32_t)(((uint64_t)p * totalFrames) / kWaveformPeakPoints);
            uint32_t end = (uint32_t)(((uint64_t)(p + 1) * totalFrames) / kWaveformPeakPoints);
            if (end <= start)
            {
                end = start + 1;
            }
            if (end > totalFrames)
            {
                end = totalFrames;
            }

            int16_t low = 0;
            int16_t up = 0;
            int16_t firstSample = 0;

            if (end <= preCacheFrames && ramBase != nullptr)
            {
                uint32_t count = end - start;
                if (count > 200)
                {
                    count = 200;
                }
                firstSample = ramBase[start];
                for (uint32_t i = 0; i < count; ++i)
                {
                    int16_t v = ramBase[start + i];
                    if (v > up)
                        up = v;
                    else if (v < low)
                        low = v;
                }
            }
            else
            {
                uint32_t count = end - start;
                if (count > 64)
                {
                    count = 64;
                }
                if (fs->seekFile(chOffset + start * sizeof(int16_t), fh))
                {
                    int32_t rd = fs->readFile(windowBuf, count * sizeof(int16_t), fh);
                    if (rd < 0)
                    {
                        fs->closeFile(fh);
                        return false;
                    }
                    uint32_t samplesRead = static_cast<uint32_t>(rd) / sizeof(int16_t);
                    if (samplesRead > 0)
                    {
                        firstSample = windowBuf[0];
                    }
                    for (uint32_t i = 0; i < samplesRead; ++i)
                    {
                        int16_t v = windowBuf[i];
                        if (v > up)
                            up = v;
                        else if (v < low)
                            low = v;
                    }
                }
            }

            if (totalFrames < kWaveformPeakPoints)
            {
                int8_t val = (int8_t)(firstSample / 300);
                peaks[p].minVal = val;
                peaks[p].maxVal = val;
            }
            else
            {
                peaks[p].minVal = (int8_t)(low / 300);
                peaks[p].maxVal = (int8_t)(up / 300);
            }
        }
    }

    if (numChannels == 1 && kMaxChannels > 1)
    {
        memcpy(s_instrumentWaveformPeaks[instrIdx][1],
               s_instrumentWaveformPeaks[instrIdx][0],
               sizeof(s_instrumentWaveformPeaks[0][0]));
    }

    mem.hasWaveformPeaks = true;
    fs->closeFile(fh);

    s_instrumentSliceCacheValid[instrIdx] = false;
    memset(s_instrumentSliceCache[instrIdx], 0, sizeof(s_instrumentSliceCache[instrIdx]));
    if (mtProject.instrument[instrIdx].playMode == playModeSlice ||
        mtProject.instrument[instrIdx].playMode == playModeBeatSlice)
    {
        refreshInstrumentSliceCache(instrIdx);
    }

    return true;
}

bool VoiceStreamManager::rebindInstrumentStreamFile(int instrIdx,
                                                    const char* filePath,
                                                    uint32_t dataOffset)
{
    if (fs == nullptr || instrIdx < 0 || instrIdx >= kMaxInstruments || filePath == nullptr ||
        filePath[0] == '\0')
    {
        return false;
    }

    auto& mem = mtProject.sampleMemory[instrIdx];
    strncpy(s_instrumentStreamPaths[instrIdx], filePath, PATCH_SIZE - 1);
    s_instrumentStreamPaths[instrIdx][PATCH_SIZE - 1] = '\0';
    mem.fileDataOffset = dataOffset;

    return rebuildInstrumentClmt(instrIdx);
}

void VoiceStreamManager::rebindAllInstrumentStreamFiles()
{
    if (fs == nullptr)
    {
        return;
    }

    for (int instrIdx = 0; instrIdx < kMaxInstruments; ++instrIdx)
    {
        if (!mtProject.sampleMemory[instrIdx].isStreamed ||
            s_instrumentStreamPaths[instrIdx][0] == '\0')
        {
            continue;
        }

        [[maybe_unused]] bool rebound = rebuildInstrumentClmt(instrIdx);
        LOGF("stream rebind %d %s: %s",
             instrIdx,
             rebound ? "ok" : "failed",
             s_instrumentStreamPaths[instrIdx]);
    }
}

bool VoiceStreamManager::rebuildInstrumentClmt(int instrIdx)
{
    auto& mem = mtProject.sampleMemory[instrIdx];

    auto [isOpen, fh] = fs->openFile(s_instrumentStreamPaths[instrIdx], FileSystem::kRead);
    if (!isOpen || fh == nullptr)
    {
        mem.hasValidClmt = false;
        return false;
    }

    s_instrumentClmt[instrIdx][0] = kClmtElementCount;
    uint32_t dbSector = 0;
    uint32_t csize = 0;
    bool clmtOk =
      fs->enableFastSeek(fh, s_instrumentClmt[instrIdx], kClmtElementCount, &dbSector, &csize);
    mem.hasValidClmt = clmtOk;
    mem.databaseSector = dbSector;
    mem.sectorsPerCluster = csize;
    fs->closeFile(fh);
    return clmtOk;
}

void VoiceStreamManager::refreshInstrumentSliceCache(int instrIdx)
{
    if (fs == nullptr || instrIdx < 0 || instrIdx >= kMaxInstruments)
    {
        return;
    }

    auto& mem = mtProject.sampleMemory[instrIdx];
    if (!mem.isStreamed || mem.ramFrames == 0)
    {
        return;
    }

    const auto& instr = mtProject.instrument[instrIdx];
    uint32_t totalFrames = instr.sample.length;
    if (totalFrames == 0)
    {
        return;
    }

    bool isSlicedMode =
      (instr.playMode == playModeSlice || instr.playMode == playModeBeatSlice) &&
      (instr.sliceNumber > 0) && (mem.ramFrames >= kStreamPreCacheFrames);

    const char* path = s_instrumentStreamPaths[instrIdx];
    int numChannels = mem.isStereo ? 2 : 1;
    uint32_t kLinearHalf = kStreamPreCacheFrames / 2;

    if (!isSlicedMode)
    {
        if (s_instrumentSliceCacheValid[instrIdx] && path[0] != '\0')
        {
            uint32_t fullPreCache =
              (totalFrames < mem.ramFrames) ? totalFrames : mem.ramFrames;
            if (fullPreCache > kLinearHalf)
            {
                auto [isOpen, fh] = fs->openFile(path, FileSystem::kRead);
                if (isOpen && fh != nullptr)
                {
                    if (mem.hasValidClmt)
                    {
                        fs->enableFastSeek(fh, s_instrumentClmt[instrIdx], kClmtElementCount);
                    }
                    uint32_t restoreCount = fullPreCache - kLinearHalf;
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        int16_t* dst = mem.address[ch];
                        if (dst != nullptr)
                        {
                            uint32_t chOffset =
                              mem.fileDataOffset +
                              (ch == 1 ? (totalFrames * sizeof(int16_t)) : 0);
                            if (fs->seekFile(chOffset + kLinearHalf * sizeof(int16_t), fh))
                            {
                                size_t toRead = restoreCount * sizeof(int16_t);
                                int32_t rd =
                                  fs->readFile(dst + kLinearHalf, toRead, fh);
                                if (rd < 0)
                                {
                                    fs->closeFile(fh);
                                    return;
                                }
                                if (static_cast<size_t>(rd) < toRead)
                                {
                                    memset(reinterpret_cast<uint8_t*>(dst + kLinearHalf) + rd,
                                           0,
                                           toRead - rd);
                                }
                            }
                        }
                    }
                    fs->closeFile(fh);
                }
            }
            mem.preCacheFrames = fullPreCache;
        }
        s_instrumentSliceCacheValid[instrIdx] = false;
        memset(s_instrumentSliceCache[instrIdx], 0, sizeof(s_instrumentSliceCache[instrIdx]));
        return;
    }

    if (path[0] == '\0')
    {
        return;
    }

    uint8_t sliceCount = instr.sliceNumber;
    if (sliceCount > MAX_SLICE_NUMBER)
    {
        sliceCount = MAX_SLICE_NUMBER;
    }

    uint32_t linearFrames = (totalFrames < kLinearHalf) ? totalFrames : kLinearHalf;
    mem.preCacheFrames = linearFrames;

    uint32_t slotFrames = (kLinearHalf / sliceCount) & ~15u;
    if (slotFrames == 0)
    {
        slotFrames = kLinearHalf / sliceCount;
    }

    memset(s_instrumentSliceCache[instrIdx], 0, sizeof(s_instrumentSliceCache[instrIdx]));

    auto [isOpen, fh] = fs->openFile(path, FileSystem::kRead);
    if (!isOpen || fh == nullptr)
    {
        return;
    }

    if (mem.hasValidClmt)
    {
        fs->enableFastSeek(fh, s_instrumentClmt[instrIdx], kClmtElementCount);
    }

    for (uint8_t s = 0; s < sliceCount; ++s)
    {
        uint32_t rawStart =
          (uint32_t)((float)instr.slices[s] * ((float)totalFrames / MAX_16BIT));
        if (rawStart >= totalFrames)
        {
            rawStart = totalFrames - 1;
        }

        uint32_t count = slotFrames;
        if (rawStart + count > totalFrames)
        {
            count = totalFrames - rawStart;
        }

        if (rawStart + count <= linearFrames)
        {
            uint32_t linearAvail = linearFrames - rawStart;
            if (linearAvail > UINT16_MAX)
            {
                linearAvail = UINT16_MAX;
            }
            s_instrumentSliceCache[instrIdx][s].startFrame = rawStart;
            s_instrumentSliceCache[instrIdx][s].frameCount = (uint16_t)linearAvail;
            s_instrumentSliceCache[instrIdx][s].bufferOffset = (uint16_t)rawStart;
        }
        else
        {
            uint16_t bufOff = (uint16_t)(kLinearHalf + (uint32_t)s * slotFrames);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                int16_t* dst = mem.address[ch];
                if (dst != nullptr)
                {
                    uint32_t chOffset =
                      mem.fileDataOffset + (ch == 1 ? (totalFrames * sizeof(int16_t)) : 0);
                    if (fs->seekFile(chOffset + rawStart * sizeof(int16_t), fh))
                    {
                        size_t toRead = count * sizeof(int16_t);
                        int32_t rd = fs->readFile(dst + bufOff, toRead, fh);
                        if (rd < 0)
                        {
                            fs->closeFile(fh);
                            return;
                        }
                        if (static_cast<size_t>(rd) < toRead)
                        {
                            memset(reinterpret_cast<uint8_t*>(dst + bufOff) + rd,
                                   0,
                                   toRead - rd);
                        }
                    }
                }
            }
            s_instrumentSliceCache[instrIdx][s].startFrame = rawStart;
            s_instrumentSliceCache[instrIdx][s].frameCount = (uint16_t)count;
            s_instrumentSliceCache[instrIdx][s].bufferOffset = bufOff;
        }
    }

    fs->closeFile(fh);
    s_instrumentSliceCacheValid[instrIdx] = true;
}

void VoiceStreamManager::refreshInstrumentSingleSliceCache(int instrIdx, uint8_t sliceIdx)
{
    if (fs == nullptr || instrIdx < 0 || instrIdx >= kMaxInstruments)
    {
        return;
    }

    auto& mem = mtProject.sampleMemory[instrIdx];
    if (!mem.isStreamed || mem.ramFrames < kStreamPreCacheFrames)
    {
        return;
    }

    if (!s_instrumentSliceCacheValid[instrIdx])
    {
        refreshInstrumentSliceCache(instrIdx);
        return;
    }

    const auto& instr = mtProject.instrument[instrIdx];
    uint8_t sliceCount = instr.sliceNumber;
    if (sliceCount == 0 || sliceIdx >= sliceCount || sliceIdx >= MAX_SLICE_NUMBER)
    {
        return;
    }

    uint32_t totalFrames = instr.sample.length;
    if (totalFrames == 0)
    {
        return;
    }

    uint32_t kLinearHalf = kStreamPreCacheFrames / 2;
    uint32_t linearFrames = mem.preCacheFrames;
    uint32_t slotFrames = (kLinearHalf / sliceCount) & ~15u;
    if (slotFrames == 0)
    {
        slotFrames = kLinearHalf / sliceCount;
    }

    uint32_t rawStart =
      (uint32_t)((float)instr.slices[sliceIdx] * ((float)totalFrames / MAX_16BIT));
    if (rawStart >= totalFrames)
    {
        rawStart = totalFrames - 1;
    }

    uint32_t count = slotFrames;
    if (rawStart + count > totalFrames)
    {
        count = totalFrames - rawStart;
    }

    if (rawStart + count <= linearFrames)
    {
        uint32_t linearAvail = linearFrames - rawStart;
        if (linearAvail > UINT16_MAX)
        {
            linearAvail = UINT16_MAX;
        }
        s_instrumentSliceCache[instrIdx][sliceIdx].startFrame = rawStart;
        s_instrumentSliceCache[instrIdx][sliceIdx].frameCount = (uint16_t)linearAvail;
        s_instrumentSliceCache[instrIdx][sliceIdx].bufferOffset = (uint16_t)rawStart;
        return;
    }

    const char* path = s_instrumentStreamPaths[instrIdx];
    if (path[0] == '\0')
    {
        return;
    }

    auto [isOpen, fh] = fs->openFile(path, FileSystem::kRead);
    if (!isOpen || fh == nullptr)
    {
        return;
    }

    if (mem.hasValidClmt)
    {
        fs->enableFastSeek(fh, s_instrumentClmt[instrIdx], kClmtElementCount);
    }

    uint16_t bufOff = (uint16_t)(kLinearHalf + (uint32_t)sliceIdx * slotFrames);
    int numChannels = mem.isStereo ? 2 : 1;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        int16_t* dst = mem.address[ch];
        if (dst != nullptr)
        {
            uint32_t chOffset =
              mem.fileDataOffset + (ch == 1 ? (totalFrames * sizeof(int16_t)) : 0);
            if (fs->seekFile(chOffset + rawStart * sizeof(int16_t), fh))
            {
                size_t toRead = count * sizeof(int16_t);
                int32_t rd = fs->readFile(dst + bufOff, toRead, fh);
                if (rd < 0)
                {
                    fs->closeFile(fh);
                    return;
                }
                if (static_cast<size_t>(rd) < toRead)
                {
                    memset(reinterpret_cast<uint8_t*>(dst + bufOff) + rd, 0, toRead - rd);
                }
            }
        }
    }

    fs->closeFile(fh);
    s_instrumentSliceCache[instrIdx][sliceIdx].startFrame = rawStart;
    s_instrumentSliceCache[instrIdx][sliceIdx].frameCount = (uint16_t)count;
    s_instrumentSliceCache[instrIdx][sliceIdx].bufferOffset = bufOff;
}

AUDIO_CORE_RAM bool VoiceStreamManager::getSlicePreCache(int instrIdx,
                                                         uint8_t sliceIdx,
                                                         int channel,
                                                         uint32_t actualStartFrame,
                                                         const int16_t*& outAddr,
                                                         uint32_t& outFrames) const
{
    outAddr = nullptr;
    outFrames = 0;
    if ((uint32_t)instrIdx >= kMaxInstruments || sliceIdx >= MAX_SLICE_NUMBER ||
        !s_instrumentSliceCacheValid[instrIdx])
    {
        return false;
    }

    const auto& mem = mtProject.sampleMemory[instrIdx];
    int ch = (channel > 0 && mem.isStereo) ? 1 : 0;
    const int16_t* baseAddr = mem.address[ch];
    if (baseAddr == nullptr)
    {
        return false;
    }

    const auto& entry = s_instrumentSliceCache[instrIdx][sliceIdx];
    if (entry.frameCount == 0)
    {
        return false;
    }

    if (actualStartFrame >= entry.startFrame &&
        actualStartFrame < entry.startFrame + entry.frameCount)
    {
        uint32_t delta = actualStartFrame - entry.startFrame;
        outAddr = baseAddr + entry.bufferOffset + delta;
        outFrames = entry.frameCount - delta;
        return true;
    }
    return false;
}

bool VoiceStreamManager::translateOffsetToLba(int instrIdx,
                                              uint32_t fileByteOffset,
                                              uint32_t& outLbaSector,
                                              uint32_t& outMaxContigSectors) const
{
    if (instrIdx < 0 || instrIdx >= kMaxInstruments)
    {
        return false;
    }
    const auto& mem = mtProject.sampleMemory[instrIdx];
    if (!mem.hasValidClmt || mem.sectorsPerCluster == 0 || mem.databaseSector == 0)
    {
        return false;
    }

    uint32_t fileSectorIdx = fileByteOffset / kSectorSize;
    uint32_t csize = mem.sectorsPerCluster;
    uint32_t cl = fileSectorIdx / csize;
    uint32_t csect = fileSectorIdx % csize;

    const uint32_t* tbl = &s_instrumentClmt[instrIdx][1];
    for (uint32_t i = 0; i < (kClmtElementCount - 2) / 2; ++i)
    {
        uint32_t ncl = *tbl++;
        if (ncl == 0)
        {
            return false;
        }
        uint32_t startClust = *tbl++;
        if (cl < ncl)
        {
            uint32_t targetClust = startClust + cl;
            if (targetClust < 2)
            {
                return false;
            }
            uint32_t remClusters = ncl - cl;
            outLbaSector = mem.databaseSector + (targetClust - 2) * csize + csect;
            outMaxContigSectors = remClusters * csize - csect;
            return true;
        }
        cl -= ncl;
    }
    return false;
}

void VoiceStreamManager::notifyNoteOn(int voiceIdx,
                                      int instrIdx,
                                      uint32_t startFrame,
                                      uint32_t loopStartFrame,
                                      uint32_t loopEndFrame,
                                      uint32_t endFrame,
                                      uint8_t playMode,
                                      bool isReverse,
                                      float pitchSpeed,
                                      uint32_t sliceCachedFrames)
{
    if (voiceIdx < 0 || voiceIdx >= kVoiceCount)
    {
        return;
    }
    auto& v = voices[voiceIdx];
    v.generation = v.generation + 1;

    if (instrIdx < 0 || instrIdx >= kMaxInstruments ||
        !mtProject.sampleMemory[instrIdx].isStreamed)
    {
        v.active = false;
        return;
    }

    const auto& mem = mtProject.sampleMemory[instrIdx];
    uint32_t totalFrames = mtProject.instrument[instrIdx].sample.length;
    if (endFrame > totalFrames || endFrame == 0)
    {
        endFrame = totalFrames;
    }
    if (loopEndFrame > endFrame || loopEndFrame == 0)
    {
        loopEndFrame = endFrame;
    }

    int8_t dir = isReverse ? -1 : 1;
    uint32_t initialFrame = (dir < 0) ? ((endFrame > 0) ? (endFrame - 1) : 0) : startFrame;

    uint32_t effectivePreCache = mem.preCacheFrames;
    if (dir >= 0 && sliceCachedFrames > 0)
    {
        uint32_t sliceCacheEnd = initialFrame + sliceCachedFrames;
        if (sliceCacheEnd > endFrame)
        {
            sliceCacheEnd = endFrame;
        }
        if (sliceCacheEnd > effectivePreCache)
        {
            effectivePreCache = sliceCacheEnd;
        }
    }

    v.active = false;
    v.instrIdx = (uint8_t)instrIdx;
    v.playMode = playMode;
    v.direction = dir;
    v.isStereo = mem.isStereo && (kMaxChannels > 1);
    v.startFrame = startFrame;
    v.endFrame = endFrame;
    v.loopStartFrame = loopStartFrame;
    v.loopEndFrame = loopEndFrame;
    v.preCacheFrames = effectivePreCache;
    v.totalFrames = totalFrames;
    v.currentFrame = initialFrame;
    v.pitchSpeed = (pitchSpeed > 0.01f) ? pitchSpeed : 1.0f;

    if (dir >= 0)
    {
        uint32_t initValid = (initialFrame < effectivePreCache) ? effectivePreCache : initialFrame;
        if (mem.sampleHash != 0 && v.ringSampleHash == mem.sampleHash &&
            v.validStartFrame <= initValid && v.validEndFrame > initValid)
        {
            v.validStartFrame = initValid;
            if (currentJob.inProgress && currentJob.voiceIdx == voiceIdx &&
                currentJob.targetType == AsyncTargetType::kRingForward &&
                mtProject.sampleMemory[currentJob.instrIdx].sampleHash == mem.sampleHash)
            {
                currentJob.expectedGeneration = v.generation;
            }
        }
        else
        {
            v.validStartFrame = initValid;
            v.validEndFrame = initValid;
            v.ringSampleHash = mem.sampleHash;
            if (currentJob.inProgress && currentJob.voiceIdx == voiceIdx &&
                currentJob.targetType == AsyncTargetType::kRingForward &&
                mem.sampleHash != 0 &&
                mtProject.sampleMemory[currentJob.instrIdx].sampleHash == mem.sampleHash &&
                currentJob.fetchStartFrame == initValid)
            {
                currentJob.expectedGeneration = v.generation;
            }
        }
    }
    else
    {
        uint32_t initValid = (initialFrame < mem.preCacheFrames) ? mem.preCacheFrames : (initialFrame + 1);
        v.validStartFrame = initValid;
        v.validEndFrame = initValid;
        v.ringSampleHash = mem.sampleHash;
    }

    v.loopWrapReady = false;
    v.loopWrapStartFrame = 0;
    v.loopWrapEndFrame = 0;
    v.fadeInCounter = (initialFrame < effectivePreCache) ? 32 : 0;
    v.active = true;
}

void VoiceStreamManager::notifyNoteOff(int voiceIdx)
{
    if (voiceIdx < 0 || voiceIdx >= kVoiceCount)
    {
        return;
    }
    voices[voiceIdx].active = false;
    voices[voiceIdx].generation = voices[voiceIdx].generation + 1;
}

void VoiceStreamManager::invalidateInstrumentVoices(int instrIdx)
{
    for (int i = 0; i < kVoiceCount; ++i)
    {
        if (voices[i].active && voices[i].instrIdx == (uint8_t)instrIdx)
        {
            voices[i].active = false;
            voices[i].generation = voices[i].generation + 1;
        }
    }
}

AUDIO_CORE_RAM void VoiceStreamManager::updateVoicePlayhead(int voiceIdx,
                                                            uint32_t absFrame,
                                                            int8_t direction,
                                                            float pitchSpeed)
{
    if ((uint32_t)voiceIdx >= kVoiceCount)
    {
        return;
    }
    auto& v = voices[voiceIdx];
    if (!v.active)
    {
        return;
    }
    v.currentFrame = absFrame;
    v.direction = direction;
    if (pitchSpeed > 0.01f)
    {
        v.pitchSpeed = pitchSpeed;
    }
}

AUDIO_CORE_RAM int16_t VoiceStreamManager::getSample(int voiceIdx,
                                                     int channel,
                                                     uint32_t absFrame,
                                                     const int16_t* preCacheBase,
                                                     uint32_t preCacheFrames)
{
    if (absFrame < preCacheFrames && preCacheBase != nullptr)
    {
        return preCacheBase[absFrame];
    }
    if ((uint32_t)voiceIdx >= kVoiceCount)
    {
        return 0;
    }
    auto& v = voices[voiceIdx];
    if (!v.active)
    {
        return 0;
    }

    int ch = (channel > 0 && v.isStereo) ? 1 : 0;
    int16_t sampleVal = 0;
    bool hit = false;

    uint32_t vStart = v.validStartFrame;
    uint32_t vEnd = v.validEndFrame;
    if (absFrame >= vStart && absFrame < vEnd)
    {
        sampleVal = s_voiceRingBuffers[voiceIdx][ch][absFrame & kRingMask];
        hit = true;
    }
    else if (v.loopWrapReady)
    {
        uint32_t lwStart = v.loopWrapStartFrame;
        uint32_t lwEnd = v.loopWrapEndFrame;
        if (absFrame >= lwStart && absFrame < lwEnd)
        {
            sampleVal = s_voiceLoopWrapBuffers[voiceIdx][ch][absFrame - lwStart];
            hit = true;
        }
    }

    if (!hit)
    {
        if (channel == 0)
        {
            v.fadeInCounter = 0;
        }
        return 0;
    }

    uint16_t fade = v.fadeInCounter;
    if (fade < 32)
    {
        sampleVal = (int16_t)(((int32_t)sampleVal * (int32_t)fade) >> 5);
        if (channel == 0)
        {
            v.fadeInCounter = fade + 1;
        }
    }

    return sampleVal;
}

bool VoiceStreamManager::readInstrumentFramesBlocking(int instrIdx,
                                                      int channel,
                                                      uint32_t startFrame,
                                                      uint32_t frameCount,
                                                      int16_t* dest)
{
    if (fs == nullptr || instrIdx < 0 || instrIdx >= kMaxInstruments || dest == nullptr ||
        frameCount == 0)
    {
        return false;
    }

    const auto& mem = mtProject.sampleMemory[instrIdx];
    uint32_t totalFrames = mtProject.instrument[instrIdx].sample.length;
    if (startFrame >= totalFrames)
    {
        memset(dest, 0, frameCount * sizeof(int16_t));
        return true;
    }
    if (startFrame + frameCount > totalFrames)
    {
        uint32_t validPart = totalFrames - startFrame;
        memset(dest + validPart, 0, (frameCount - validPart) * sizeof(int16_t));
        frameCount = validPart;
    }

    int ch = (mem.isStereo && channel > 0) ? 1 : 0;
    const int16_t* ramPtr = mem.address[ch];

    if (!mem.isStreamed)
    {
        if (ramPtr != nullptr)
        {
            memcpy(dest, ramPtr + startFrame, frameCount * sizeof(int16_t));
            return true;
        }
        return false;
    }

    // Serve any leading frames that fall inside the SDRAM Attack Pre-Cache
    if (startFrame < mem.preCacheFrames && ramPtr != nullptr)
    {
        uint32_t preCount = mem.preCacheFrames - startFrame;
        if (preCount >= frameCount)
        {
            memcpy(dest, ramPtr + startFrame, frameCount * sizeof(int16_t));
            return true;
        }
        memcpy(dest, ramPtr + startFrame, preCount * sizeof(int16_t));
        dest += preCount;
        startFrame += preCount;
        frameCount -= preCount;
    }

    const char* path = s_instrumentStreamPaths[instrIdx];
    if (path[0] == '\0')
    {
        return false;
    }

    uint32_t channelBaseByteOffset =
      mem.fileDataOffset + (ch == 1 ? (totalFrames * sizeof(int16_t)) : 0);
    uint32_t byteOffset = channelBaseByteOffset + startFrame * sizeof(int16_t);
    uint32_t bytesToRead = frameCount * sizeof(int16_t);

    auto [isOpen, fh] = fs->openFile(path, FileSystem::kRead);
    if (!isOpen || fh == nullptr)
    {
        return false;
    }

    if (mem.hasValidClmt)
    {
        fs->enableFastSeek(fh, s_instrumentClmt[instrIdx], kClmtElementCount);
    }

    bool ok = fs->seekFile(byteOffset, fh);
    if (ok)
    {
        int32_t rd = fs->readFile(dest, bytesToRead, fh);
        ok = (rd == static_cast<int32_t>(bytesToRead));
    }
    fs->closeFile(fh);
    return ok;
}

bool VoiceStreamManager::startAsyncSectorReadForJob(uint8_t channel)
{
    if (fs == nullptr)
    {
        return false;
    }
    int instrIdx = currentJob.instrIdx;
    const auto& mem = mtProject.sampleMemory[instrIdx];
    uint32_t totalFrames = mtProject.instrument[instrIdx].sample.length;

    uint32_t channelBaseOffset =
      mem.fileDataOffset + ((channel == 1 && mem.isStereo) ? (totalFrames * sizeof(int16_t)) : 0);
    uint32_t byteOffset = channelBaseOffset + currentJob.fetchStartFrame * sizeof(int16_t);
    uint32_t intraOffset = byteOffset % kSectorSize;
    uint32_t totalBytes = intraOffset + currentJob.fetchFrameCount * sizeof(int16_t);
    uint32_t sectorsNeeded = (totalBytes + kSectorSize - 1) / kSectorSize;

    uint32_t lbaSector = 0;
    uint32_t maxContigSectors = 0;
    if (!translateOffsetToLba(instrIdx, byteOffset, lbaSector, maxContigSectors) ||
        maxContigSectors == 0)
    {
        return false;
    }

    if (sectorsNeeded > maxContigSectors)
    {
        sectorsNeeded = maxContigSectors;
        uint32_t availBytes = sectorsNeeded * kSectorSize;
        if (availBytes <= intraOffset)
        {
            return false;
        }
        uint32_t clampedFrames = (availBytes - intraOffset) / sizeof(int16_t);
        if (clampedFrames == 0)
        {
            return false;
        }
        currentJob.fetchFrameCount = clampedFrames;
    }

    currentJob.channel = channel;
    currentJob.intraSectorByteOffset = intraOffset;

    return fs->readSectorsAsync(s_dmaStagingBuffer, lbaSector, sectorsNeeded);
}

void VoiceStreamManager::completeChannelTransfer()
{
    int vIdx = currentJob.voiceIdx;
    if (vIdx < 0 || vIdx >= kVoiceCount)
    {
        currentJob.inProgress = false;
        return;
    }

    auto& v = voices[vIdx];
    if (!v.active || v.generation != currentJob.expectedGeneration)
    {
        currentJob.inProgress = false;
        return;
    }

    const int16_t* srcSamples =
      reinterpret_cast<const int16_t*>(s_dmaStagingBuffer + currentJob.intraSectorByteOffset);
    uint32_t count = currentJob.fetchFrameCount;
    uint32_t startF = currentJob.fetchStartFrame;
    uint8_t ch = currentJob.channel;

    if (currentJob.targetType == AsyncTargetType::kLoopWrap)
    {
        if (count > kLoopWrapFrames)
        {
            count = kLoopWrapFrames;
        }
        memcpy(&s_voiceLoopWrapBuffers[vIdx][ch][0], srcSamples, count * sizeof(int16_t));
    }
    else
    {
        int16_t* ring = s_voiceRingBuffers[vIdx][ch];
        uint32_t ringStartIdx = startF & kRingMask;
        if (ringStartIdx + count <= kRingFrames)
        {
            memcpy(&ring[ringStartIdx], srcSamples, count * sizeof(int16_t));
        }
        else
        {
            uint32_t firstPart = kRingFrames - ringStartIdx;
            memcpy(&ring[ringStartIdx], srcSamples, firstPart * sizeof(int16_t));
            memcpy(&ring[0], srcSamples + firstPart, (count - firstPart) * sizeof(int16_t));
        }
    }

    // If stereo voice needs channel 1 next, advance to channel 1 and try launching now
    if (ch + 1 < currentJob.totalChannels)
    {
        currentJob.channel = ch + 1;
        currentJob.transferLaunched = false;
        if (startAsyncSectorReadForJob(ch + 1))
        {
            currentJob.transferLaunched = true;
        }
        return;
    }

    // All channels transferred: commit valid frame window atomically
    if (currentJob.targetType == AsyncTargetType::kLoopWrap)
    {
        v.loopWrapStartFrame = startF;
        v.loopWrapEndFrame = startF + count;
        v.loopWrapReady = true;
    }
    else if (currentJob.targetType == AsyncTargetType::kRingForward)
    {
        uint32_t newEnd = startF + count;
        uint32_t vStart = v.validStartFrame;
        if (vStart > startF)
        {
            vStart = startF;
        }
        if (newEnd - vStart > kRingFrames)
        {
            vStart = newEnd - kRingFrames;
        }
        v.validStartFrame = vStart;
        v.validEndFrame = newEnd;
    }
    else if (currentJob.targetType == AsyncTargetType::kRingBackward)
    {
        uint32_t vEnd = v.validEndFrame;
        if (vEnd < startF + count)
        {
            vEnd = startF + count;
        }
        if (vEnd - startF > kRingFrames)
        {
            vEnd = startF + kRingFrames;
        }
        v.validEndFrame = vEnd;
        v.validStartFrame = startF;
    }

    currentJob.inProgress = false;
}

void VoiceStreamManager::fillJobSynchronously(const AsyncJob& job)
{
    int vIdx = job.voiceIdx;
    if (vIdx < 0 || vIdx >= kVoiceCount)
    {
        return;
    }
    auto& v = voices[vIdx];
    if (!v.active || v.generation != job.expectedGeneration)
    {
        return;
    }

    uint32_t count = job.fetchFrameCount;
    uint32_t startF = job.fetchStartFrame;

    for (uint8_t ch = 0; ch < job.totalChannels; ++ch)
    {
        if (job.targetType == AsyncTargetType::kLoopWrap)
        {
            uint32_t lwCount = (count > kLoopWrapFrames) ? kLoopWrapFrames : count;
            readInstrumentFramesBlocking(
              job.instrIdx, ch, startF, lwCount, &s_voiceLoopWrapBuffers[vIdx][ch][0]);
        }
        else
        {
            int16_t* tempDest = reinterpret_cast<int16_t*>(s_dmaStagingBuffer);
            if (readInstrumentFramesBlocking(job.instrIdx, ch, startF, count, tempDest))
            {
                int16_t* ring = s_voiceRingBuffers[vIdx][ch];
                uint32_t ringStartIdx = startF & kRingMask;
                if (ringStartIdx + count <= kRingFrames)
                {
                    memcpy(&ring[ringStartIdx], tempDest, count * sizeof(int16_t));
                }
                else
                {
                    uint32_t firstPart = kRingFrames - ringStartIdx;
                    memcpy(&ring[ringStartIdx], tempDest, firstPart * sizeof(int16_t));
                    memcpy(&ring[0], tempDest + firstPart, (count - firstPart) * sizeof(int16_t));
                }
            }
        }
    }

    if (job.targetType == AsyncTargetType::kLoopWrap)
    {
        v.loopWrapStartFrame = startF;
        v.loopWrapEndFrame = startF + count;
        v.loopWrapReady = true;
    }
    else if (job.targetType == AsyncTargetType::kRingForward)
    {
        uint32_t newEnd = startF + count;
        uint32_t vStart = v.validStartFrame;
        if (vStart > startF)
        {
            vStart = startF;
        }
        if (newEnd - vStart > kRingFrames)
        {
            vStart = newEnd - kRingFrames;
        }
        v.validStartFrame = vStart;
        v.validEndFrame = newEnd;
    }
    else if (job.targetType == AsyncTargetType::kRingBackward)
    {
        uint32_t vEnd = v.validEndFrame;
        if (vEnd < startF + count)
        {
            vEnd = startF + count;
        }
        if (vEnd - startF > kRingFrames)
        {
            vEnd = startF + kRingFrames;
        }
        v.validEndFrame = vEnd;
        v.validStartFrame = startF;
    }
}

bool VoiceStreamManager::scheduleNextVoiceJob(bool synchronous)
{
    int bestVoice = -1;
    float lowestUrgency = 1e9f;
    AsyncJob bestJob{};

    for (int i = 0; i < kVoiceCount; ++i)
    {
        auto& v = voices[i];
        if (!v.active)
        {
            continue;
        }

        uint8_t instrIdx = v.instrIdx;
        if (instrIdx >= kMaxInstruments || !mtProject.sampleMemory[instrIdx].isStreamed)
        {
            v.active = false;
            continue;
        }

        uint32_t cur = v.currentFrame;
        uint32_t preCache = v.preCacheFrames;
        uint32_t endF = v.endFrame;
        int8_t dir = v.direction;
        float speed = v.pitchSpeed;

        if (dir >= 0)
        {
            // Forward playback
            uint32_t vStart = v.validStartFrame;
            uint32_t vEnd = v.validEndFrame;

            uint32_t effectiveEnd = endF;
            if ((v.playMode == playModeLoopForward || v.playMode == playModePingpong) &&
                v.loopEndFrame > v.loopStartFrame && v.loopEndFrame < endF &&
                cur <= v.loopEndFrame)
            {
                effectiveEnd = v.loopEndFrame;
            }

            if (cur < preCache)
            {
                if (vStart > preCache || vEnd < preCache)
                {
                    vStart = preCache;
                    vEnd = preCache;
                    v.validStartFrame = vStart;
                    v.validEndFrame = vEnd;
                }
            }
            else if (cur < vStart || cur > vEnd)
            {
                if (v.loopWrapReady && cur >= v.loopWrapStartFrame && cur < v.loopWrapEndFrame)
                {
                    if (vStart != v.loopWrapEndFrame || vEnd < vStart)
                    {
                        vStart = v.loopWrapEndFrame;
                        vEnd = v.loopWrapEndFrame;
                    }
                }
                else
                {
                    vStart = cur;
                    vEnd = cur;
                }
                v.validStartFrame = vStart;
                v.validEndFrame = vEnd;
            }

            uint32_t minKeep = (cur > kLookbehindFrames) ? (cur - kLookbehindFrames) : 0;
            if ((v.playMode == playModeLoopForward || v.playMode == playModePingpong) &&
                v.loopEndFrame > v.loopStartFrame)
            {
                uint32_t loopLow = (v.loopStartFrame > preCache) ? v.loopStartFrame : preCache;
                if (effectiveEnd > loopLow && (effectiveEnd - loopLow) <= kRingFrames &&
                    minKeep > loopLow)
                {
                    // Entire loop fits inside the ring buffer; never evict loopLow!
                    minKeep = loopLow;
                }
            }
            if (minKeep < vStart)
            {
                minKeep = vStart;
            }

            uint32_t maxRingBuffered = kRingFrames;
            uint32_t preCacheSpan = (preCache > v.startFrame) ? (preCache - v.startFrame) : 0;
            if (!synchronous && preCacheSpan > 0 && cur < v.startFrame + (preCacheSpan >> 1))
            {
                // While inside the first half of the SDRAM attack/slice pre-cache,
                // only prefetch 1 chunk so ultra-short 1-step notes don't
                // saturate the SD bus with 4 chunks before getting retriggered.
                maxRingBuffered = kChunkFrames;
            }
            uint32_t maxAllowedEnd = minKeep + maxRingBuffered;
            uint32_t freeRingSpace = (maxAllowedEnd > vEnd) ? (maxAllowedEnd - vEnd) : 0;

            if (vEnd < effectiveEnd &&
                (freeRingSpace >= kChunkFrames || (effectiveEnd - vEnd) <= freeRingSpace))
            {
                uint32_t framesAhead = (vEnd > cur) ? (vEnd - cur) : 0;
                float urgency = (float)framesAhead / speed;
                if (urgency < lowestUrgency)
                {
                    uint32_t fetchCount = kChunkFrames;
                    if (fetchCount > freeRingSpace)
                    {
                        fetchCount = freeRingSpace;
                    }
                    if (vEnd + fetchCount > effectiveEnd)
                    {
                        fetchCount = effectiveEnd - vEnd;
                    }
                    if (fetchCount > 0)
                    {
                        lowestUrgency = urgency;
                        bestVoice = i;
                        bestJob.inProgress = true;
                        bestJob.voiceIdx = i;
                        bestJob.instrIdx = instrIdx;
                        bestJob.channel = 0;
                        bestJob.totalChannels = v.isStereo ? 2 : 1;
                        bestJob.targetType = AsyncTargetType::kRingForward;
                        bestJob.fetchStartFrame = vEnd;
                        bestJob.fetchFrameCount = fetchCount;
                        bestJob.expectedGeneration = v.generation;
                    }
                }
            }
            else if (!v.loopWrapReady &&
                     v.playMode == playModeLoopForward &&
                     v.loopStartFrame >= preCache && v.loopEndFrame > v.loopStartFrame)
            {
                uint32_t lwCount = v.loopEndFrame - v.loopStartFrame;
                if (lwCount > kLoopWrapFrames)
                {
                    lwCount = kLoopWrapFrames;
                }
                float urgency = 50000.0f;
                if (urgency < lowestUrgency && lwCount > 0)
                {
                    lowestUrgency = urgency;
                    bestVoice = i;
                    bestJob.inProgress = true;
                    bestJob.voiceIdx = i;
                    bestJob.instrIdx = instrIdx;
                    bestJob.channel = 0;
                    bestJob.totalChannels = v.isStereo ? 2 : 1;
                    bestJob.targetType = AsyncTargetType::kLoopWrap;
                    bestJob.fetchStartFrame = v.loopStartFrame;
                    bestJob.fetchFrameCount = lwCount;
                    bestJob.expectedGeneration = v.generation;
                }
            }
        }
        else
        {
            // Backward playback (BackwardLoop or reverse phase of PingPongLoop)
            uint32_t vStart = v.validStartFrame;
            uint32_t vEnd = v.validEndFrame;

            uint32_t minTarget = preCache;
            if ((v.playMode == playModePingpong || v.playMode == playModeLoopBackward) &&
                v.loopStartFrame > preCache && cur >= v.loopStartFrame)
            {
                minTarget = v.loopStartFrame;
            }

            if (cur >= preCache && (cur < vStart || cur >= vEnd))
            {
                if (v.loopWrapReady && cur >= v.loopWrapStartFrame && cur < v.loopWrapEndFrame)
                {
                    if (vEnd != v.loopWrapStartFrame)
                    {
                        vStart = v.loopWrapStartFrame;
                        vEnd = v.loopWrapStartFrame;
                    }
                }
                else
                {
                    vStart = cur + 1;
                    vEnd = cur + 1;
                }
                v.validStartFrame = vStart;
                v.validEndFrame = vEnd;
            }

            uint32_t maxKeep = cur + kLookbehindFrames;
            if ((v.playMode == playModePingpong || v.playMode == playModeLoopBackward) &&
                v.loopEndFrame > minTarget)
            {
                if ((v.loopEndFrame - minTarget) <= kRingFrames && maxKeep < v.loopEndFrame)
                {
                    // Entire loop fits inside the ring buffer; never evict loopEndFrame!
                    maxKeep = v.loopEndFrame;
                }
            }
            if (maxKeep > vEnd)
            {
                maxKeep = vEnd;
            }

            uint32_t minAllowedStart = (maxKeep > kRingFrames) ? (maxKeep - kRingFrames) : 0;
            uint32_t freeRingSpace = (vStart > minAllowedStart) ? (vStart - minAllowedStart) : 0;

            if (vStart > minTarget &&
                (freeRingSpace >= kChunkFrames || (vStart - minTarget) <= freeRingSpace))
            {
                uint32_t framesAhead = (cur >= vStart) ? (cur - vStart) : 0;
                float urgency = (float)framesAhead / speed;
                if (urgency < lowestUrgency)
                {
                    uint32_t fetchCount = kChunkFrames;
                    if (fetchCount > freeRingSpace)
                    {
                        fetchCount = freeRingSpace;
                    }
                    if (vStart - minTarget < fetchCount)
                    {
                        fetchCount = vStart - minTarget;
                    }
                    if (fetchCount > 0)
                    {
                        lowestUrgency = urgency;
                        bestVoice = i;
                        bestJob.inProgress = true;
                        bestJob.voiceIdx = i;
                        bestJob.instrIdx = instrIdx;
                        bestJob.channel = 0;
                        bestJob.totalChannels = v.isStereo ? 2 : 1;
                        bestJob.targetType = AsyncTargetType::kRingBackward;
                        bestJob.fetchStartFrame = vStart - fetchCount;
                        bestJob.fetchFrameCount = fetchCount;
                        bestJob.expectedGeneration = v.generation;
                    }
                }
            }
            else if (!v.loopWrapReady &&
                     v.playMode == playModeLoopBackward &&
                     v.loopEndFrame > preCache && v.loopEndFrame > v.loopStartFrame)
            {
                uint32_t lwCount = v.loopEndFrame - v.loopStartFrame;
                if (lwCount > kLoopWrapFrames)
                {
                    lwCount = kLoopWrapFrames;
                }
                uint32_t lwStart = v.loopEndFrame - lwCount;
                if (lwStart < preCache)
                {
                    lwStart = preCache;
                    lwCount = v.loopEndFrame - lwStart;
                }
                float urgency = 50000.0f;
                if (urgency < lowestUrgency && lwCount > 0)
                {
                    lowestUrgency = urgency;
                    bestVoice = i;
                    bestJob.inProgress = true;
                    bestJob.voiceIdx = i;
                    bestJob.instrIdx = instrIdx;
                    bestJob.channel = 0;
                    bestJob.totalChannels = v.isStereo ? 2 : 1;
                    bestJob.targetType = AsyncTargetType::kLoopWrap;
                    bestJob.fetchStartFrame = lwStart;
                    bestJob.fetchFrameCount = lwCount;
                    bestJob.expectedGeneration = v.generation;
                }
            }
        }
    }

    if (bestVoice < 0)
    {
        return false;
    }

    if (synchronous || !mtProject.sampleMemory[bestJob.instrIdx].hasValidClmt)
    {
        fillJobSynchronously(bestJob);
        return true;
    }

    currentJob = bestJob;
    currentJob.transferLaunched = false;
    if (startAsyncSectorReadForJob(0))
    {
        currentJob.transferLaunched = true;
    }
    return true;
}

void VoiceStreamManager::setSuspended(bool suspended)
{
    isSuspended = suspended;

    if (isSuspended && fs != nullptr)
    {
        // A read already on its way would otherwise complete during the next
        // owner's first request. Its voice asks again after the suspension.
        fs->waitAsyncRead();
        currentJob.inProgress = false;
    }
}

void VoiceStreamManager::update()
{
    if (fs == nullptr || isSuspended)
    {
        return;
    }

    if (currentJob.inProgress)
    {
        if (!currentJob.transferLaunched)
        {
            int vIdx = currentJob.voiceIdx;
            if (vIdx < 0 || vIdx >= kVoiceCount || !voices[vIdx].active ||
                voices[vIdx].generation != currentJob.expectedGeneration)
            {
                currentJob.inProgress = false;
            }
            else if (!fs->isAsyncReadBusy())
            {
                if (startAsyncSectorReadForJob(currentJob.channel))
                {
                    currentJob.transferLaunched = true;
                }
                else
                {
                    AsyncJob fallbackJob = currentJob;
                    currentJob.inProgress = false;
                    fillJobSynchronously(fallbackJob);
                }
            }
            return;
        }

        bool ok = false;
        if (!fs->pollAsyncRead(&ok))
        {
            return; // Still transferring in background via ADMA2
        }
        if (ok)
        {
            completeChannelTransfer();
        }
        else
        {
            currentJob.inProgress = false;
        }
    }

    if (!currentJob.inProgress && !fs->isAsyncReadBusy())
    {
        scheduleNextVoiceJob(false);
    }
}

void VoiceStreamManager::updateSynchronous()
{
    if (fs == nullptr)
    {
        return;
    }

    while (currentJob.inProgress)
    {
        if (!currentJob.transferLaunched)
        {
            if (!startAsyncSectorReadForJob(currentJob.channel))
            {
                currentJob.inProgress = false;
                break;
            }
            currentJob.transferLaunched = true;
        }
        if (fs->waitAsyncRead())
        {
            completeChannelTransfer();
        }
        else
        {
            currentJob.inProgress = false;
        }
    }

    // Pump all active voices until their ring buffers are topped up
    for (int iter = 0; iter < kVoiceCount * 4; ++iter)
    {
        if (!scheduleNextVoiceJob(true))
        {
            break;
        }
    }
}
