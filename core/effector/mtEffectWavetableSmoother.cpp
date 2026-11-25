#include "mtEffectWavetableSmoother.h"

mtEffectWavetableSmoother sampleEditorWavetableSmoother;

void mtEffectWavetableSmoother::setParameter(float& value, int idx)
{
    switch (idx)
    {
        case 0: effectWavetableSmootherParams.windowsNumber = (int)value; break;
        case 1: effectWavetableSmootherParams.windowSize = (int)value; break;
        default: assert(false); break;
    }
}

bool mtEffectWavetableSmoother::startProcess()
{
    if (effectWavetableSmootherParams.windowsNumber == 0 ||
        effectWavetableSmootherParams.windowSize == 0)
    {
        return false;
    }
    for (int channel = 0; channel < nChannels(); channel++)
    {
        channelParams[channel].sourceWindowMax =
          (confirmed[channel].selection.length / effectWavetableSmootherParams.windowSize) - 1;
        if (channelParams[channel].sourceWindowMax >
            ((uint32_t)effectWavetableSmootherParams.windowsNumber - 1))
        {
            channelParams[channel].sourceWindowMax =
              effectWavetableSmootherParams.windowsNumber - 1;
        }

        // smoothWindowStep nie moze byc rowne 0 bo zawiesza processing -kz
        if (channelParams[channel].sourceWindowMax <= 0)
            channelParams[channel].sourceWindowMax = 1;

        channelParams[channel].smoothWindowStep =
          channelParams[channel].sourceWindowMax /
          (float)effectWavetableSmootherParams.windowsNumber;
        channelParams[channel].currentWindowNumber = 0;
        channelParams[channel].iCurrentWindowNumber = 0;
        channelParams[channel].iLastWindowNumber = 0;

        channelParams[channel].dstAddr = processed[channel].selection.addr;
        channelParams[channel].srcAddr = confirmed[channel].selection.addr;

        channelParams[channel].currentCrossfadeCoef = 1.0f;
        channelParams[channel].crossfadeStep = 1.0f / getSmoothStepNumber(channel);
        channelParams[channel].processedSamples = 0;
    }

    length = effectWavetableSmootherParams.windowsNumber * effectWavetableSmootherParams.windowSize;

    state = true;
    return true;
}
int32_t mtEffectWavetableSmoother::processBlock()
{
    if (!state)
        return -1;

    uint32_t dif = length - channelParams[0].processedSamples;
    uint32_t processedBlockLength = dif > 8192 ? 8192 : dif;

    for (int channel = 0; channel < nChannels(); channel++)
    {

        if (channelParams[channel].sourceWindowMax ==
            (uint32_t)effectWavetableSmootherParams.windowsNumber)
        {
            for (uint32_t i = 0; i < processedBlockLength; i++)
            {
                channelParams[channel].dstAddr[channelParams[channel].processedSamples] =
                  channelParams[channel].srcAddr[channelParams[channel].processedSamples];
                channelParams[channel].processedSamples++;
            }
        }
        else
        {
            for (uint32_t i = 0; i < processedBlockLength; i++)
            {
                uint32_t currentSampleInWindow = channelParams[channel].processedSamples %
                                                 effectWavetableSmootherParams.windowSize;

                channelParams[channel].dstAddr[channelParams[channel].processedSamples] =
                  (channelParams[channel].currentCrossfadeCoef *
                   channelParams[channel]
                     .srcAddr[(int)(((int)channelParams[channel].currentWindowNumber *
                                     (effectWavetableSmootherParams.windowSize - 1)) +
                                    currentSampleInWindow)]) +
                  (((uint32_t)channelParams[channel].currentWindowNumber ==
                    channelParams[channel].sourceWindowMax)
                     ? 0
                     : (1.0f - channelParams[channel].currentCrossfadeCoef) *
                         channelParams[channel]
                           .srcAddr[(int)((((int)channelParams[channel].currentWindowNumber + 1) *
                                           (effectWavetableSmootherParams.windowSize - 1)) +
                                          currentSampleInWindow)]);

                if (!(channelParams[channel].processedSamples %
                      (effectWavetableSmootherParams.windowSize - 1)))
                {

                    channelParams[channel].currentWindowNumber +=
                      channelParams[channel].smoothWindowStep;
                    channelParams[channel].currentCrossfadeCoef -=
                      channelParams[channel].crossfadeStep;
                    if (detectChangeSourceWindow(channel))
                    {
                        channelParams[channel].currentCrossfadeCoef = 1.0f;
                        channelParams[channel].crossfadeStep = 1.0f / getSmoothStepNumber(channel);
                    }
                }
                channelParams[channel].processedSamples++;
            }
        }
    }

    if (processedBlockLength < 8192)
    {
        state = false;
        for (int channel = 0; channel < nChannels(); channel++)
        {
            processed[channel].selection.length =
              channelParams[channel].processedSamples; // redundancy
            processed[channel].area.length =
              confirmed[channel].area.length + (processed[channel].selection.length -
                                                confirmed[channel].selection.length); // redundancy
        }
    }

    return processedBlockLength;
}
bool mtEffectWavetableSmoother::getProcessState()
{
    return state;
}
uint32_t mtEffectWavetableSmoother::getExpectedProcessLength()
{
    return effectWavetableSmootherParams.windowsNumber * effectWavetableSmootherParams.windowSize;
}

bool mtEffectWavetableSmoother::detectChangeSourceWindow(int channel)
{
    bool result = false;

    channelParams[channel].iCurrentWindowNumber = (int)channelParams[channel].currentWindowNumber;

    if (channelParams[channel].iCurrentWindowNumber != channelParams[channel].iLastWindowNumber)
    {
        result = true;
    }

    channelParams[channel].iLastWindowNumber = channelParams[channel].iCurrentWindowNumber;

    return result;
}

uint32_t mtEffectWavetableSmoother::getSmoothStepNumber(int channel)
{
    float localCurrentWindow = channelParams[channel].currentWindowNumber;

    int iLocalCurrentWindow = (int)localCurrentWindow;
    int iLocalLastWindow = iLocalCurrentWindow;

    uint32_t result = 0;

    while (1)
    {
        result++;
        localCurrentWindow += channelParams[channel].smoothWindowStep;

        iLocalCurrentWindow = (int)localCurrentWindow;

        if (iLocalCurrentWindow != iLocalLastWindow)
        {
            return result;
        }

        iLocalLastWindow = iLocalCurrentWindow;
    }
}
