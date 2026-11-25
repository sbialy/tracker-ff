#include "mtEffectFadeOut.h"

mtEffectFadeOut sampleEditorFadeOut;

void mtEffectFadeOut::setParameter(float&, int)
{
    return;
}

bool mtEffectFadeOut::startProcess()
{
    length = confirmed[0].selection.length;

    for (int channel = 0; channel < nChannels(); channel++)
    {
        channelParams[channel].srcAddr = confirmed[channel].selection.addr;
        channelParams[channel].dstAddr = processed[channel].selection.addr;
        channelParams[channel].decreaseVolumePerSampel = 1.0f / length;
        channelParams[channel].volumeMultiplier = 1.0f;
    }

    processedSamples = 0;
    state = true;
    return true;
}
int32_t mtEffectFadeOut::processBlock()
{
    if (!state)
        return -1;

    uint32_t dif = length - processedSamples;
    uint32_t processedBlockLength = dif > 8192 ? 8192 : dif;

    for (int channel = 0; channel < nChannels(); channel++)
    {
        for (uint16_t i = 0; i < processedBlockLength; i++)
        {
            *channelParams[channel].dstAddr =
              *channelParams[channel].srcAddr * channelParams[channel].volumeMultiplier;
            channelParams[channel].volumeMultiplier -=
              channelParams[channel].decreaseVolumePerSampel;
            channelParams[channel].dstAddr++;
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
bool mtEffectFadeOut::getProcessState()
{
    return state;
}
uint32_t mtEffectFadeOut::getExpectedProcessLength()
{
    return confirmed[0].selection.length;
}
