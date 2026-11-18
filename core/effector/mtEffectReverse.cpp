#include "mtEffectReverse.h"

mtEffectReverse sampleEditorReverse;

void mtEffectReverse::setParameter(float&, int)
{
    return;
}

bool mtEffectReverse::startProcess()
{
    length = confirmed[0].selection.length;

    for (int channel = 0; channel < kChannels; channel++)
    {
        channelParams[channel].srcAddr = confirmed[channel].selection.addr;
        // the last sample of the selection: the first source sample lands there (addr + length was one past the end, so the first
        // source sample was written outside the selection and the first destination sample kept what was in memory)
        channelParams[channel].dstAddr =
          processed[channel].selection.addr + confirmed[channel].selection.length - 1;
    }

    processedSamples = 0;
    state = true;
    return true;
}
int32_t mtEffectReverse::processBlock()
{
    if (!state)
        return -1;

    uint32_t dif = length - processedSamples;
    uint32_t processedBlockLength = dif > 8192 ? 8192 : dif;

    for (int channel = 0; channel < kChannels; channel++)
    {
        for (uint16_t i = 0; i < processedBlockLength; i++)
        {
            *channelParams[channel].dstAddr = *channelParams[channel].srcAddr;
            channelParams[channel].dstAddr--;
            channelParams[channel].srcAddr++;
        }
    }

    if (processedBlockLength < 8192)
    {
        state = 0;
    }

    processedSamples += processedBlockLength;

    if (!state)
    {
        for (int channel = 0; channel < kChannels; channel++)
        {
            processed[channel].selection.length = processedSamples; // redundancy
            processed[channel].area.length =
              confirmed[channel].area.length + (processed[channel].selection.length -
                                                confirmed[channel].selection.length); // redundancy
        }
    }

    return processedBlockLength;
}
bool mtEffectReverse::getProcessState()
{
    return state;
}
uint32_t mtEffectReverse::getExpectedProcessLength()
{
    return confirmed[0].selection.length;
}
