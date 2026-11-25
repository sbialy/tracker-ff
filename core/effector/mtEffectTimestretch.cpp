#include "mtEffectTimestretch.h"
#include "AudioStream.h"
#include "core/interfacePopups.h"
#include "mtStructs.h"

#include <cmath>
#include <float.h>
#include <stdint.h>

#define MIN_GRANULES 4

mtEffectTimestretchBeat sampleEditorTimestretchBeat;
mtEffectTimestretchNote sampleEditorTimestretchNote;

void mtEffectTimestretchBeat::setParameter(float& value, int idx)
{
    switch (idx)
    {
        case TEMPO:
            value = value == 0.0f ? mtProject.values.globalTempo : value;
            tempo = (uint16_t)value;
            break;
        case STEPS: steps = (int)value; break;
        case MICRO_STEP: microStep = value; break;
        case GRANULE_DIV: granuleDiv = value; break;
        default: assert(false); break;
    }
}

void mtEffectTimestretchNote::setParameter(float& value, int idx)
{
    switch (idx)
    {
        case LENGTH:
            value = value == 0.0f ? (float)confirmed[0].selection.length / AUDIO_SAMPLE_RATE_EXACT
                                  : value;
            targetLength = value;
            break;
        case GRANULE_SIZE: granuleSize = (int)value; break;
        default: assert(false);
    }
}

bool mtEffectTimestretchBeat::startProcess()
{
    sequenceSize = (granuleDiv * confirmed[0].selection.length / 4) / ((float)steps + microStep);

    return mtEffectTimestretch::startProcess();
}

bool mtEffectTimestretchNote::startProcess()
{
    sequenceSize = granuleSize * AUDIO_SAMPLE_RATE_EXACT / 1000;

    return mtEffectTimestretch::startProcess();
}

uint32_t mtEffectTimestretchNote::getExpectedProcessLength()
{
    return AUDIO_SAMPLE_RATE_EXACT * targetLength;
}

uint32_t mtEffectTimestretchBeat::getExpectedProcessLength()
{
    return AUDIO_SAMPLE_RATE_EXACT * ((float)steps + microStep) * 60 / (tempo * 4);
}

bool mtEffectTimestretch::startProcess()
{
    targetSamples = getExpectedProcessLength();

    if (targetSamples > maxFrames())
    {
        mtPopups.show(0, "Stretched sample is too long!");

        finished = true;
        return false;
    }

    if (targetSamples < sequenceSize * MIN_GRANULES)
    {
        sequenceSize = targetSamples / MIN_GRANULES;
    }

    length = confirmed[0].selection.length;

    for (int channel = 0; channel < nChannels(); channel++)
    {
        channelParams[channel].srcAddr = confirmed[channel].selection.addr;
        channelParams[channel].dstAddr = processed[channel].selection.addr;
    }

    processedSamples = 0;
    finished = false;
    for (int channel = 0; channel < nChannels(); channel++)
    {
        channelParams[channel].sola.init(channelParams[channel].dstAddr,
                                         channelParams[channel].srcAddr,
                                         length,
                                         targetSamples,
                                         sequenceSize);
    }

    return true;
}

int32_t mtEffectTimestretch::processBlock()
{
    if (finished)
    {
        return -1;
    }

    uint32_t processedSamples = 0;

    for (int channel = 0; channel < nChannels(); channel++)
    {
        processedSamples = channelParams[channel].sola.processSequence();
    }

    if (processedSamples == 0)
    {
        for (int channel = 0; channel < nChannels(); channel++)
        {
            processed[channel].selection.length = targetSamples;
            processed[channel].area.length = targetSamples;
            processed[channel].selection.addr = processed[channel].area.addr;
        }
        finished = true;
    }

    return processedSamples;
}

bool mtEffectTimestretch::getProcessState()
{
    return !finished;
}

int SOLA::seekBestOverlap(const int16_t* inputPrev, const int16_t* inputNew)
{
    int bestOffset = 0;
    float bestCorr = -FLT_MAX;

    // Find best overlapSize offset within [0..SEEK_WINDOW]
    for (uint32_t i = 0; i < seekWindow; i++)
    {
        float crossCorr = 0;

        for (uint32_t j = 0; j < overlapSize; j++)
        {
            crossCorr += (float)inputNew[i + j] *
                         ((float)(inputPrev[j] * (int32_t)j * ((int32_t)overlapSize - (int32_t)j)));
        }
        if (crossCorr > bestCorr)
        {
            // found new best offset candidate
            bestCorr = crossCorr;
            bestOffset = i;
        }
    }
    return bestOffset;
}

// Overlap 'inputPrev' with 'inputNew' by sliding the amplitudes during
// overlapSize samples.
void SOLA::processOverlap(int16_t* output,
                          const int16_t* inputPrev,
                          const int16_t* inputNew,
                          uint32_t outSamples)
{
    for (uint32_t i = 0; i < outSamples; i++)
    {
        output[i] =
          (inputPrev[i] * ((int32_t)overlapSize - (int32_t)i) + inputNew[i] * (int32_t)i) /
          (int32_t)overlapSize;
    }
}

void SOLA::init(int16_t* output,
                const int16_t* input,
                uint32_t inSamples,
                uint32_t outSamples,
                uint32_t sequenceSize)
{
    this->output = output;
    this->input = input;
    this->inSamples = inSamples;
    this->outSamples = outSamples;
    this->sequenceSize = sequenceSize;
    overlapSize = sequenceSize / 5;
    seekWindow = sequenceSize / 6;
    seqOffset = input;
    sequenceSkip = (uint64_t)(sequenceSize - overlapSize) * inSamples / outSamples;
    flatSize = (sequenceSize - 2 * (overlapSize));
}

uint32_t SOLA::processSequence()
{
    if (outSamples == 0)
    {
        return 0;
    }

    bool doesSeqOffsetFitMemory =
      inSamples - (((int)(seqOffset - input) + flatSize)) > (sequenceSkip + seekWindow);
    bool doesInputFitMemory = inSamples > (sequenceSkip + seekWindow);

    int bestOverlap =
      seekBestOverlap(seqOffset + flatSize, input) > (int)(sequenceSkip + seekWindow);
    bool doesPrevSeqOffsetFitMemory = inSamples - bestOverlap;

    if (doesSeqOffsetFitMemory && doesInputFitMemory && doesPrevSeqOffsetFitMemory)
    {
        const int16_t* prevOffset;
        const uint32_t flatSizeCapped = std::min(flatSize, outSamples);
        // copy flat mid-sequenceSize from current processing sequenceSize to output
        memcpy(output, seqOffset, flatSizeCapped * sizeof(int16_t));
        // calculate a pointer to overlapSize at end of the processing sequenceSize
        prevOffset = seqOffset + flatSize;

        // update input pointer to theoretical next processing sequenceSize begin
        input += sequenceSkip - overlapSize;
        // seek actual best matching offset using cross-correlation
        seqOffset = input + bestOverlap;

        // do overlapping between previous & new sequenceSize, copy result to output
        processOverlap(output + flatSizeCapped,
                       prevOffset,
                       seqOffset,
                       std::min(overlapSize, outSamples - flatSizeCapped));

        // Update input & sequenceSize pointers by overlapping amount
        seqOffset += overlapSize;
        input += overlapSize;

        // Update output pointer & sample counters
        const uint32_t processed = std::min(sequenceSize - overlapSize, outSamples);
        output += processed;
        inSamples -= sequenceSkip;
        outSamples -= processed;

        return processed;
    }
    else
    {
        // copy leftover in input buffer
        uint32_t offset = seqOffset - input;

        int samplesLeft = std::min(inSamples - offset, outSamples);
        memcpy(output, seqOffset, samplesLeft * sizeof(int16_t));
        output += samplesLeft;
        outSamples -= samplesLeft;

        // zero-out leftover space in out buffer
        if (outSamples > 0)
        {
            memset(output, 0, outSamples * sizeof(int16_t));
        }
        outSamples = 0;

        return samplesLeft;
    }
}
