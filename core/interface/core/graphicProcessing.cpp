
#include <display.h>

//#include "mtStructs.h"
#include "interfaceDefs.h"

#include "VoiceStreamManager.h"
#include "graphicProcessing.h"
#include "mtAudioEngine.h"
#include <algorithm>
#include <cr_section_macros.h>

__NOINIT(EXTERNAL_RAM) static int16_t s_zoomReadBuf[4096];

cGraphicProcessing GP;

void cGraphicProcessing::spectrumResetZoom(uint16_t position,
                                           uint32_t sampleLength,
                                           strZoomParams* zoom)
{
    zoom->zoomPosition = position;

    zoom->zoomResolution = sampleLength / 600;

    uint16_t min_resolution = sampleLength / MAX_16BIT + 1;
    if (zoom->zoomResolution < min_resolution)
        zoom->zoomResolution = min_resolution;

    zoom->zoomStart = 0;
    zoom->zoomEnd = MAX_16BIT;
    zoom->zoomWidth = MAX_16BIT;
    zoom->zoomValue = 1.0;
}

void cGraphicProcessing::spectrumAutoZoomIn(uint16_t position,
                                            uint16_t zoomWidth,
                                            uint32_t sampleLength,
                                            strZoomParams* zoom)
{
    zoom->zoomPosition = position;

    zoom->zoomResolution = ((((float)zoomWidth) / MAX_16BIT) * sampleLength) / 600;

    uint16_t min_resolution = sampleLength / MAX_16BIT + 1;
    if (zoom->zoomResolution < min_resolution)
        zoom->zoomResolution = min_resolution;

    zoom->zoomValue = sampleLength / ((zoom->zoomResolution) * 600.0);
}

void cGraphicProcessing::spectrumChangeZoom(int16_t value,
                                            uint32_t sampleLength,
                                            strZoomParams* zoom)
{
    if (sampleLength < 600)
    {
        zoom->zoomResolution = 1;
        zoom->zoomValue = 1.0;
        return;
    }

    uint16_t max_resolution = sampleLength / 600;
    uint16_t min_resolution = sampleLength / MAX_16BIT + 1;

    uint16_t step = (max_resolution - min_resolution) / 100;
    if (step == 0)
        step = 1;

    if (zoom->zoomResolution - (value * step) < min_resolution)
        zoom->zoomResolution = min_resolution;
    else if (zoom->zoomResolution - (value * step) > max_resolution)
        zoom->zoomResolution = max_resolution;
    else
        zoom->zoomResolution -= value * step;

    if (zoom->zoomResolution == max_resolution)
        zoom->zoomValue = 1.0;
    else
        zoom->zoomValue = sampleLength / ((zoom->zoomResolution) * 600.0);
}

void cGraphicProcessing::processSpectrum(strSpectrumParams* params,
                                         strZoomParams* zoom,
                                         strTrackerSpectrum* spectrum)
{
    if (params->length == 0)
    {
        for (uint16_t i = 0; i < 600; i++)
        {
            spectrum->upperData[i] = 0;
            spectrum->lowerData[i] = 0;
        }

        zoom->zoomWidth = MAX_16BIT;
        zoom->zoomStart = 0;
        zoom->zoomEnd = MAX_16BIT;
        spectrum->spectrumType = 1;

        return;
    }

    if (params->recordInProgressFlag)
    {
        int16_t* sampleData = params->address;
        uint32_t resolution = params->length / 600;

        if (resolution < 1)
            resolution = 1;

        uint16_t offset_pixel = 0;

        zoom->zoomWidth = MAX_16BIT;
        zoom->zoomStart = 0;
        zoom->zoomEnd = MAX_16BIT;

        int16_t up = 0;
        int16_t low = 0;
        uint32_t step = 0;

        uint16_t max_resolution =
          (resolution < 200) ? resolution : 200; // ogranicza liczbe przetwarzanych probek
                                                 // oszukujac troche na wygladzie

        for (uint16_t i = offset_pixel; i < 600; i++)
        {
            low = up = 0; //*(sampleData+step);

            for (uint16_t j = 0; j < max_resolution; j++)
            {
                int16_t sample = *(sampleData + step + j);

                if (sample > up)
                    up = sample;
                else if (sample < low)
                    low = sample;
            }
            step += resolution;

            up = up / 300;
            low = low / 300;

            spectrum->upperData[i] = up;
            spectrum->lowerData[i] = low;
        }

        if (params->length / zoom->zoomValue < 3600)
            spectrum->spectrumType = 1;
        else
            spectrum->spectrumType = 0;

        return;
    }

    processSpectrum(params->address, params->length, zoom, spectrum);
}

void cGraphicProcessing::processSpectrum(strSampleMemory* sampleMemory,
                                         strInstrument* instrument,
                                         strZoomParams* zoom,
                                         strTrackerSpectrum* spectrum,
                                         strSampleMemory::Channel channel,
                                         uint32_t wt_pos)
{
    // uwaga tu wazna kolejnosc + do sprawdzenia
    if (mtProject.instrument[mtProject.values.lastUsedInstrument].isActive == 0)
    {
        for (uint16_t i = 0; i < 600; i++)
        {
            spectrum->upperData[i] = 0;
            spectrum->lowerData[i] = 0;
        }

        spectrum->spectrumType = 1;

        return;
    }

    int16_t* sampleData;

    if (instrument->sample.type == mtSampleTypeWavetable)
    {
        zoom->zoomWidth = MAX_16BIT;
        zoom->zoomStart = 0;
        zoom->zoomValue = 1;
        zoom->zoomEnd = MAX_16BIT;
        uint16_t windowSize =
          instrument->sample.wavetable_window_size; // instrument->sample.wavetable_window_size;

        if (wt_pos * windowSize > instrument->sample.length || windowSize == 0)
            return;

        if (sampleMemory->isStreamed)
        {
            int instrIdx = (int)(sampleMemory - mtProject.sampleMemory);
            uint8_t streamCh = sampleMemory->isStereo ? (uint8_t)channel : 0;
            uint32_t readLen = std::min<uint32_t>(
              windowSize,
              std::min<uint32_t>(4096, instrument->sample.length - (wt_pos * windowSize)));
            voiceStreamManager.readInstrumentFramesBlocking(
              instrIdx, streamCh, wt_pos * windowSize, readLen, s_zoomReadBuf);
            sampleData = s_zoomReadBuf;
        }
        else
        {
            sampleData = sampleMemory->address[channel] + (wt_pos * windowSize);
        }

        float resolution = windowSize / 600.0;

        int16_t up = 0;
        int16_t low = 0;
        float step = 0;

        for (uint16_t i = 0; i < 600; i++)
        {
            low = up = 0; //*(sampleData+step);

            for (uint16_t j = 0; j < resolution; j++)
            {
                int16_t sample = *(sampleData + (uint32_t)step + j);

                if (sample > up)
                    up = sample;
                else if (sample < low)
                    low = sample;
            }
            step += resolution;

            up = up / 300;
            low = low / 300;

            spectrum->upperData[i] = up;
            spectrum->lowerData[i] = low;
        }

        // if(resolution <= 1)
        spectrum->spectrumType = 1;
        // else spectrum.spectrumType = 0;

        return;
    }

    if (sampleMemory->isStreamed && sampleMemory->hasWaveformPeaks)
    {
        uint32_t length = instrument->sample.length;
        int instrIdx = (int)(sampleMemory - mtProject.sampleMemory);
        uint8_t streamCh = sampleMemory->isStereo ? (uint8_t)channel : 0;
        const WaveformPeakPoint* peaks =
          voiceStreamManager.getInstrumentWaveformPeaks(instrIdx, streamCh);

        if (zoom->zoomValue > 1.0f && length > 600)
        {
            zoom->zoomWidth = (MAX_16BIT / zoom->zoomValue);
            zoom->zoomStart = zoom->zoomPosition - zoom->zoomWidth / 2;
            zoom->zoomEnd = zoom->zoomPosition + zoom->zoomWidth / 2;

            if (zoom->zoomStart < 0)
            {
                zoom->zoomEnd = zoom->zoomWidth;
                zoom->zoomStart = 0;
            }
            else if (zoom->zoomEnd > MAX_16BIT)
            {
                zoom->zoomEnd = MAX_16BIT;
                zoom->zoomStart = MAX_16BIT - zoom->zoomWidth;
            }
        }
        else
        {
            zoom->zoomWidth = MAX_16BIT;
            zoom->zoomStart = 0;
            zoom->zoomEnd = MAX_16BIT;
        }

        if (zoom->zoomValue <= 4.0f && peaks != nullptr)
        {
            for (uint16_t i = 0; i < 600; i++)
            {
                int32_t normPos = zoom->zoomStart + ((int32_t)i * zoom->zoomWidth) / 600;
                int32_t peakIdx = (normPos * 600) / MAX_16BIT;
                peakIdx = std::clamp<int32_t>(peakIdx, 0, 599);
                spectrum->upperData[i] = peaks[peakIdx].maxVal;
                spectrum->lowerData[i] = peaks[peakIdx].minVal;
            }
            spectrum->spectrumType = (length / zoom->zoomValue < 3600) ? 1 : 0;
            return;
        }

        uint32_t startFrame = ((uint64_t)zoom->zoomStart * length) / MAX_16BIT;
        uint32_t endFrame = ((uint64_t)zoom->zoomEnd * length) / MAX_16BIT;
        if (endFrame > length)
        {
            endFrame = length;
        }
        if (endFrame <= startFrame)
        {
            endFrame = std::min<uint32_t>(length, startFrame + 1);
        }
        uint32_t visibleFrames = endFrame - startFrame;

        if (visibleFrames <= 4096)
        {
            voiceStreamManager.readInstrumentFramesBlocking(
              instrIdx, streamCh, startFrame, visibleFrames, s_zoomReadBuf);

            for (uint16_t i = 0; i < 600; i++)
            {
                uint32_t f0 = ((uint64_t)i * visibleFrames) / 600;
                uint32_t f1 = ((uint64_t)(i + 1) * visibleFrames) / 600;
                if (f1 <= f0)
                {
                    f1 = std::min<uint32_t>(visibleFrames, f0 + 1);
                }
                int16_t up = 0;
                int16_t low = 0;
                for (uint32_t f = f0; f < f1; ++f)
                {
                    int16_t s = s_zoomReadBuf[f];
                    if (s > up)
                        up = s;
                    if (s < low)
                        low = s;
                }
                if (f1 == f0 + 1)
                {
                    int8_t val = (int8_t)(s_zoomReadBuf[f0] / 300);
                    spectrum->upperData[i] = val;
                    spectrum->lowerData[i] = val;
                }
                else
                {
                    spectrum->upperData[i] = (int8_t)(up / 300);
                    spectrum->lowerData[i] = (int8_t)(low / 300);
                }
            }
            spectrum->spectrumType = (visibleFrames < 3600) ? 1 : 0;
            return;
        }
        else
        {
            static constexpr uint32_t kNumChunks = 15;
            static constexpr uint32_t kColsPerChunk = 600 / kNumChunks;
            static constexpr uint32_t kFramesPerChunk = 256;

            for (uint32_t c = 0; c < kNumChunks; ++c)
            {
                uint32_t chunkStart = startFrame + ((uint64_t)c * visibleFrames) / kNumChunks;
                uint32_t readLen = std::min<uint32_t>(kFramesPerChunk, length - chunkStart);
                voiceStreamManager.readInstrumentFramesBlocking(
                  instrIdx, streamCh, chunkStart, readLen, s_zoomReadBuf);

                for (uint32_t col = 0; col < kColsPerChunk; ++col)
                {
                    uint32_t f0 = (col * readLen) / kColsPerChunk;
                    uint32_t f1 = ((col + 1) * readLen) / kColsPerChunk;
                    if (f1 <= f0)
                        f1 = std::min<uint32_t>(readLen, f0 + 1);
                    int16_t up = 0;
                    int16_t low = 0;
                    for (uint32_t f = f0; f < f1; ++f)
                    {
                        int16_t s = s_zoomReadBuf[f];
                        if (s > up)
                            up = s;
                        if (s < low)
                            low = s;
                    }
                    uint16_t outIdx = c * kColsPerChunk + col;
                    spectrum->upperData[outIdx] = (int8_t)(up / 300);
                    spectrum->lowerData[outIdx] = (int8_t)(low / 300);
                }
            }
            spectrum->spectrumType = 0;
            return;
        }
    }

    processSpectrum(sampleMemory->address[channel], instrument->sample.length, zoom, spectrum);
}

// The window of a zoomed view is not always inside the sample. At the very end it reaches a few
// samples past it, and with a zoom that was set on a sample of another length it can lie
// anywhere around it.
static int16_t sampleOrSilence(const int16_t* address, uint32_t length, int32_t index)
{
    return (index >= 0 && (uint32_t)index < length) ? address[index] : 0;
}

void cGraphicProcessing::processSpectrum(int16_t* address,
                                         uint32_t length,
                                         strZoomParams* zoom,
                                         strTrackerSpectrum* spectrum)
{

    if (address == nullptr || length == 0)
    {
        if (spectrum)
        {
            for (uint16_t i = 0; i < 600; i++)
            {
                spectrum->upperData[i] = 0;
                spectrum->lowerData[i] = 0;
            }
            spectrum->spectrumType = 1;
        }
        return;
    }

    uint16_t offset_pixel;
    int16_t* sampleData;
    uint32_t resolution;

    if (zoom->zoomValue > 1.0 && length > 600)
    {
        zoom->zoomWidth = (MAX_16BIT / zoom->zoomValue);

        zoom->zoomStart = zoom->zoomPosition - zoom->zoomWidth / 2;
        zoom->zoomEnd = zoom->zoomPosition + zoom->zoomWidth / 2;

        if (zoom->zoomStart < 0)
        {
            zoom->zoomEnd = zoom->zoomWidth;
            zoom->zoomStart = 0;
            offset_pixel = ((zoom->zoomPosition - zoom->zoomStart) * 599) / zoom->zoomWidth;
        }
        else if (zoom->zoomEnd > MAX_16BIT)
        {
            zoom->zoomEnd = MAX_16BIT;
            zoom->zoomStart = MAX_16BIT - zoom->zoomWidth;
            offset_pixel = ((zoom->zoomPosition - zoom->zoomStart) * 599) / zoom->zoomWidth;
        }
        else
        {
            offset_pixel = ((zoom->zoomPosition - zoom->zoomStart) * 599) / zoom->zoomWidth;
        }

        uint32_t offset = ((float)zoom->zoomPosition / MAX_16BIT) * length;

        sampleData = address + offset;

        // resolution = (((float)zoomWidth/MAX_16BIT) * editorInstrument->sample.length ) / 600;

        resolution = zoom->zoomResolution;
    }
    else
    {

        offset_pixel = 0;
        zoom->zoomWidth = MAX_16BIT;
        zoom->zoomStart = 0;
        zoom->zoomEnd = MAX_16BIT;
        sampleData = address;
        resolution = length / 600;
    }

    int16_t up = 0;
    int16_t low = 0;

    // kiedy dlugosc sampla mniejsza niz 600 probek
    if (resolution < 1) // resolution = 1;
    {
        uint16_t sample = 0;
        uint16_t px_per_sample = (length > 0) ? (600 / length) : 0;

        for (uint16_t i = 0; i < 600; i++)
        {
            if (px_per_sample > 0)
            {
                px_per_sample--;
            }
            else
            {
                sample++;
                if (length > sample)
                {
                    px_per_sample = ((600 - i) / (length - sample));
                }
                else
                {
                    px_per_sample = 0;
                    sample = length - 1;
                }
            }

            spectrum->lowerData[i] = spectrum->upperData[i] = (*(sampleData + sample)) / 300;
        }

        spectrum->spectrumType = 1;
        return;
    }

    const int32_t firstSample = sampleData - address;
    uint32_t step = 0;
    uint16_t max_resolution =
      (resolution < 200) ? resolution : 200; // ogranicza liczbe przetwarzanych probek
                                             // oszukujac troche na wygladzie
    if (offset_pixel > 0)
    {
        for (int16_t i = offset_pixel - 1; i >= 0; i--)
        {
            low = up = 0; //*(sampleData+step);

            for (uint16_t j = 0; j < max_resolution; j++)
            {
                int16_t sample = sampleOrSilence(address, length, firstSample - (int32_t)step + j);

                if (sample > up)
                    up = sample;
                else if (sample < low)
                    low = sample;
            }
            step += resolution;

            up = up / 300;
            low = low / 300;

            spectrum->upperData[i] = up;
            spectrum->lowerData[i] = low;
        }
    }

    up = 0;
    low = 0;
    step = 0;

    for (uint16_t i = offset_pixel; i < 600; i++)
    {
        low = up = 0; //*(sampleData+step);

        for (uint16_t j = 0; j < max_resolution; j++)
        {
            int16_t sample = sampleOrSilence(address, length, firstSample + (int32_t)step + j);

            if (sample > up)
                up = sample;
            else if (sample < low)
                low = sample;
        }

        step += resolution;

        up = up / 300;
        low = low / 300;

        spectrum->upperData[i] = up;
        spectrum->lowerData[i] = low;
    }

    // dobrany doswiadczalnie warunek kiedy najlepiej zmienic tryb wyswietlania

    if (length / zoom->zoomValue < 3600)
        spectrum->spectrumType = 1;
    // if(length*zoom->zoomValue > 131000) spectrum->spectrumType = 1;
    else
        spectrum->spectrumType = 0;
}
