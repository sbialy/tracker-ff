#include "mtEffectDelay.h"
#include "mtStructs.h"
#include <cmath>
#include <cr_section_macros.h>
#include <string.h>

__NOINIT(EXTERNAL_RAM) mtEffectDelay::ChannelParams mtEffectDelay::channelParams[kChannels];

void mtEffectDelay::setParameter(float& value, int idx)
{
    switch (idx)
    {
        case 0:
            effectDelayParams.feedback = value;
            calcLastVoiceIdx();
            break;
        case 1: effectDelayParams.time = (uint16_t)value; break;
        default: assert(false); break;
    }
}

bool mtEffectDelay::startProcess()
{
    if (effectDelayParams.feedback == 0 || effectDelayParams.time == 0)
    {
        return false;
    }

    constexpr float MILISECOND_SAMPLES = 44.1f;

    uint32_t timeInSamples = (uint32_t)((float)effectDelayParams.time * MILISECOND_SAMPLES);

    for (int channel = 0; channel < nChannels(); channel++)
    {
        channelParams[channel].currentDstAddr = processed[channel].selection.addr;
        channelParams[channel].lastSrcAddr =
          confirmed[channel].selection.addr + confirmed[channel].selection.length;

        channelParams[channel].firstActiveVoiceIdx = 0;
        channelParams[channel].lastActiveVoiceIdx = 0;

        for (uint8_t i = 0; i < MAX_DELAY_VOICE_NUMBER; i++)
        {
            channelParams[channel].sourceVoicePointer[i] = confirmed[channel].selection.addr;
            channelParams[channel].feedbackVoiceMult[i] = powf(effectDelayParams.feedback, i);
            channelParams[channel].lastIndexInVoice[i] = i * timeInSamples;
        }
        channelParams[channel].processedSamples = 0;
    }

    state = true;
    return true;
}
int32_t mtEffectDelay::processBlock()
{
    if (!state)
        return -1;

    int32_t processedBlockLength = 0;

    for (int channel = 0; channel < nChannels(); channel++)
    {
        processedBlockLength = 0;
        for (uint32_t i = 0; i < 8192; i++)
        {
            if ((channelParams[channel].sourceVoicePointer[lastVoiceIdx] <
                 channelParams[channel].lastSrcAddr) &&
                (channelParams[channel].processedSamples <= maxFrames()))
            {
                int32_t currentSum = 0;

                for (uint8_t i = channelParams[channel].firstActiveVoiceIdx;
                     i <= channelParams[channel].lastActiveVoiceIdx;
                     i++)
                {
                    currentSum += *channelParams[channel].sourceVoicePointer[i] *
                                  channelParams[channel].feedbackVoiceMult[i];
                    channelParams[channel].sourceVoicePointer[i]++;
                }

                if (currentSum > MAX_SIGNED_16BIT)
                    currentSum = MAX_SIGNED_16BIT;
                else if (currentSum < MIN_SIGNED_16BIT)
                    currentSum = MIN_SIGNED_16BIT;

                *channelParams[channel].currentDstAddr++ = (int16_t)currentSum;

                processedBlockLength++;
                channelParams[channel].processedSamples++;

                if ((channelParams[channel].processedSamples >
                     channelParams[channel]
                       .lastIndexInVoice[channelParams[channel].lastActiveVoiceIdx]))
                    channelParams[channel].lastActiveVoiceIdx++;
                if (channelParams[channel]
                      .sourceVoicePointer[channelParams[channel].firstActiveVoiceIdx] >=
                    channelParams[channel].lastSrcAddr)
                    channelParams[channel].firstActiveVoiceIdx++;

                if (channelParams[channel].lastActiveVoiceIdx > MAX_DELAY_VOICE_NUMBER - 1)
                    channelParams[channel].lastActiveVoiceIdx = MAX_DELAY_VOICE_NUMBER - 1;
                if (channelParams[channel].firstActiveVoiceIdx > MAX_DELAY_VOICE_NUMBER - 1)
                    channelParams[channel].firstActiveVoiceIdx = MAX_DELAY_VOICE_NUMBER - 1;
            }
            else
            {
                state = false;
                processed[channel].selection.length =
                  channelParams[channel].processedSamples; // redundancy
                processed[channel].area.length =
                  confirmed[channel].area.length +
                  (processed[channel].selection.length -
                   confirmed[channel].selection.length); // redundancy
                break;
            }
        }
    }

    return processedBlockLength;
}
bool mtEffectDelay::getProcessState()
{
    return state;
}
uint32_t mtEffectDelay::getExpectedProcessLength()
{
    constexpr float MILISECOND_TIME = 44.1f;
    uint32_t timeInSamples = (uint32_t)((float)effectDelayParams.time * MILISECOND_TIME);

    return confirmed[0].selection.length + (lastVoiceIdx - 1) * timeInSamples;
}

void mtEffectDelay::calcLastVoiceIdx()
{
    uint8_t maxVoiceNumber = 0;
    float feedbackGain = 1.0;

    while (feedbackGain > 0.01f)
    {
        feedbackGain *= effectDelayParams.feedback;
        maxVoiceNumber++;
        if (maxVoiceNumber == MAX_DELAY_VOICE_NUMBER)
            break;
    }

    lastVoiceIdx = maxVoiceNumber - 1;
}

mtEffectDelay sampleEditorDelay;
//***********
