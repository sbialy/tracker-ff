#include "mtEffectAmplifier.h"

mtEffectAmplifier sampleEditorAmplifier;

void mtEffectAmplifier::setParameter(float& value, int /*idx*/)
{
    assert(idx == 0);
    amp = value;
    return;
}

bool mtEffectAmplifier::startProcess()
{
    length = confirmed[0].selection.length;

    for (int channel = 0; channel < nChannels(); channel++)
    {
        channelParams[channel].srcAddr = confirmed[channel].selection.addr;
        channelParams[channel].dstAddr = processed[channel].selection.addr;
    }

    processedSamples = 0;
    state = true;
    return true;
}
int32_t mtEffectAmplifier::processBlock()
{
    if (!state)
        return -1;

    uint32_t dif = length - processedSamples;
    uint32_t processedBlockLength = dif > 8192 ? 8192 : dif;

    for (int channel = 0; channel < nChannels(); channel++)
    {
        for (uint16_t i = 0; i < processedBlockLength; i++)
        {
            int32_t processVal = amp * (*channelParams[channel].srcAddr);

            if (processVal > MAX_SIGNED_16BIT)
                processVal = MAX_SIGNED_16BIT;
            if (processVal < MIN_SIGNED_16BIT)
                processVal = MIN_SIGNED_16BIT;

            *channelParams[channel].dstAddr = (int16_t)processVal;
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
bool mtEffectAmplifier::getProcessState()
{
    return state;
}
uint32_t mtEffectAmplifier::getExpectedProcessLength()
{
    return confirmed[0].selection.length;
}
