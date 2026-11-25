#include "mtEffectCut.h"
#include "string.h"

mtEffectCut sampleEditorCut;

void mtEffectCut::setParameter(float&, int)
{
    return;
}

bool mtEffectCut::startProcess()
{
    processingWithoutCopying = true;

    length = confirmed[0].selection.length;

    for (int channel = 0; channel < nChannels(); channel++)
    {
        channelParams[channel].srcAddr = confirmed[channel].selection.addr;
        channelParams[channel].dstAddr = processed[channel].area.addr;
    }

    processedSamples = 0;
    state = true;
    return true;
}
int32_t mtEffectCut::processBlock()
{
    if (!state)
        return -1;

    uint32_t dif = length - processedSamples;
    uint32_t processedBlockLength = dif > 8192 ? 8192 : dif;
    for (int channel = 0; channel < nChannels(); channel++)
    {
        memcpy(
          channelParams[channel].dstAddr, channelParams[channel].srcAddr, 2 * processedBlockLength);
        channelParams[channel].dstAddr += processedBlockLength;
        channelParams[channel].srcAddr += processedBlockLength;
    }

    if (processedBlockLength < 8192)
    {
        state = 0;
    }

    processedSamples += processedBlockLength;

    if (!state)
    {
        for (int channel = 0; channel < nChannels(); channel++)
        {
            processed[channel].selection.length = processedSamples; // redundancy
            processed[channel].area.length = processedSamples;

            processed[channel].selection.addr = processed[channel].area.addr;
        }
    }

    return processedBlockLength;
}
bool mtEffectCut::getProcessState()
{
    return state;
}
uint32_t mtEffectCut::getExpectedProcessLength()
{
    return confirmed[0].selection.length;
}
