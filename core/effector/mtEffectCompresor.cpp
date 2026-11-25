#include "mtEffectCompresor.h"
#include "log.h"
#include "string.h"
#include <cmath>

void mtEffectCompresor::setParameter(float& value, int idx)
{
    switch (idx)
    {
        case 0: interfaceParams.threshold = (int)value; break;
        case 1: interfaceParams.ratio = value; break;
        case 2: interfaceParams.attack = (int)value; break;
        case 3: interfaceParams.release = (int)value; break;
        default: assert(false); break;
    }
}

bool mtEffectCompresor::startProcess()
{

    length = confirmed[0].selection.length;

    for (int channel = 0; channel < nChannels(); channel++)
    {
        channelParams[channel].srcAddr = confirmed[channel].selection.addr;
        channelParams[channel].dstAddr = processed[channel].selection.addr;
        channelParams[channel].inputRms = 0.0f;
        channelParams[channel].filteredLinearGain = 1.0f;
    }

    compressorThreshold = (float)interfaceParams.threshold;
    compressorSlope = 1.0f - (1.0f / interfaceParams.ratio);
    attackCoeff = 1 - expf(-2.2f * (1000.0f / (44100.0f * interfaceParams.attack)));
    releaseCoeff = 1 - expf(-2.2f * (1000.0f / (44100.0f * interfaceParams.release)));
    timeAverageCoeff = 0.002f;

    processedSamples = 0;
    state = true;

    return true;
}
int32_t mtEffectCompresor::processBlock()
{
    if (!state)
    {
        return -1;
    }

    int32_t dif = length - processedSamples;
    int32_t processedBlockLength = std::min<int32_t>(kProcessingBlockLength, dif);

    if (processedBlockLength < kProcessingBlockLength)
    {
        state = false;
    }

    for (int channel = 0; channel < nChannels(); channel++)
    {
        for (int idx = 0; idx < processedBlockLength; idx++)
        {
            float normalizedInput = channelParams[channel].srcAddr[idx] < 0
                                      ? (float)channelParams[channel].srcAddr[idx] /
                                          (float)std::numeric_limits<int16_t>::min()
                                      : (float)channelParams[channel].srcAddr[idx] /
                                          (float)std::numeric_limits<int16_t>::max();

            channelParams[channel].inputRms =
              (1 - timeAverageCoeff) * channelParams[channel].inputRms +
              timeAverageCoeff * powf(normalizedInput, 2);

            channelParams[channel].logarythmicInput = 10 * log10f(channelParams[channel].inputRms);
            channelParams[channel].logarythmicGain = std::min<float>(
              compressorSlope * (compressorThreshold - channelParams[channel].logarythmicInput),
              0.0f);

            channelParams[channel].linearGain =
              powf(10.0f, channelParams[channel].logarythmicGain / 20.0f);

            channelParams[channel].filterCoeff =
              channelParams[channel].linearGain < channelParams[channel].filteredLinearGain
                ? attackCoeff
                : releaseCoeff;

            channelParams[channel].filteredLinearGain =
              (1 - channelParams[channel].filterCoeff) * channelParams[channel].filteredLinearGain +
              channelParams[channel].filterCoeff * channelParams[channel].linearGain;

            channelParams[channel].dstAddr[idx] =
              channelParams[channel].filteredLinearGain * channelParams[channel].srcAddr[idx];
        }

        channelParams[channel].srcAddr += processedBlockLength;
        channelParams[channel].dstAddr += processedBlockLength;
    }

    processedSamples += processedBlockLength;
    if (!state)
    {
        for (int channel = 0; channel < nChannels(); channel++)
        {
            processed[channel].selection.length = processedSamples; // redundancy
            processed[channel].area.length =
              confirmed[channel].area.length + (processed[channel].selection.length -
                                                confirmed[channel].selection.length); // redundancy
        }
    }

    return processedBlockLength;
}
bool mtEffectCompresor::getProcessState()
{
    return state;
}
uint32_t mtEffectCompresor::getExpectedProcessLength()
{
    return confirmed[0].selection.length;
}

mtEffectCompresor sampleEditorCompresor;
//***********
