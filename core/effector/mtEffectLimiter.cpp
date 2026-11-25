#include "mtEffectLimiter.h"
#include <cmath>

void mtEffectLimiter::setParameter(float& value, int idx)
{
    switch (idx)
    {
        case 0:
        {
            int logarythmicThreshold = (int)value;
            float linearThreshold = powf(10.0f, logarythmicThreshold / 20.0f);
            threshold = linearThreshold * std::numeric_limits<int16_t>::max();
            break;
        }
        case 1:
        {
            float attackMs = value;
            attackCoeff = 1 - expf(-2.2f * (1000.0f / (44100.0f * attackMs)));
            break;
        }
        case 2:
        {
            float releaseMs = value;

            releaseCoeff = 1 - expf(-2.2f * (1000.0f / (44100.0f * releaseMs)));
            break;
        }
        default: break;
    }
}

bool mtEffectLimiter::startProcess()
{
    length = confirmed[0].selection.length;

    for (int channel = 0; channel < nChannels(); channel++)
    {
        channelParams[channel].srcAddr = confirmed[channel].selection.addr;
        channelParams[channel].dstAddr = processed[channel].selection.addr;
        channelParams[channel].a = 0;
    }

    processedSamples = 0;
    state = true;

    return true;
}
int32_t mtEffectLimiter::processBlock()
{
    if (!state)
        return -1;

    int32_t dif = length - processedSamples;
    int32_t processedBlockLength = dif > 8192 ? 8192 : dif;

    if (processedBlockLength < 8192)
        state = false;

    for (int channel = 0; channel < nChannels(); channel++)
    {

        for (int n = 0; n < processedBlockLength; n++)
        {
            uint16_t inputAbs = (*channelParams[channel].srcAddr < 0)
                                  ? -*channelParams[channel].srcAddr
                                  : *channelParams[channel].srcAddr;

            channelParams[channel].a =
              (1 - timeAverageCoeff) * channelParams[channel].a + timeAverageCoeff * inputAbs;

            channelParams[channel].coeff = (channelParams[channel].a > channelParams[channel].xpeak)
                                             ? attackCoeff
                                             : releaseCoeff;

            channelParams[channel].xpeak =
              (1 - channelParams[channel].coeff) * channelParams[channel].xpeak +
              channelParams[channel].coeff * channelParams[channel].a;

            channelParams[channel].f = threshold / (float)channelParams[channel].xpeak;
            channelParams[channel].f =
              (channelParams[channel].f < 1.0f) ? channelParams[channel].f : 1.0f;

            channelParams[channel].coeff =
              (channelParams[channel].f < channelParams[channel].g) ? attackCoeff : releaseCoeff;

            channelParams[channel].g =
              (1 - channelParams[channel].coeff) * channelParams[channel].g +
              channelParams[channel].coeff * channelParams[channel].f;

            *channelParams[channel].dstAddr++ =
              channelParams[channel].g * *channelParams[channel].srcAddr++;
        }
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
bool mtEffectLimiter::getProcessState()
{
    return state;
}
uint32_t mtEffectLimiter::getExpectedProcessLength()
{
    return confirmed[0].selection.length;
}

mtEffectLimiter sampleEditorLimiter;
//***********
