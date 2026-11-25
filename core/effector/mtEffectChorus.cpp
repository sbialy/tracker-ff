#include "mtEffectChorus.h"

#include "string.h"
#include <cr_section_macros.h>

mtEffectChorus sampleEditorChorus;

__NOINIT(EXTERNAL_RAM) int16_t chorusDelayBuffer[mtEffect::kChannels][CHORUS_BUFFER_LENGTH];

void mtEffectChorus::setParameter(float& value, int idx)
{

    switch (idx)
    {
        case 0: delay_length = (int)value / 2; break;
        case 1: num_chorus = (int)value; break;
        default: assert(false); break;
    }

    return;
}

bool mtEffectChorus::startProcess()
{
    length = confirmed[0].selection.length;

    for (int channel = 0; channel < nChannels(); channel++)
    {
        channelParams[channel].srcAddr = confirmed[channel].selection.addr;
        channelParams[channel].dstAddr = processed[channel].selection.addr;

        channelParams[channel].l_delayline = &chorusDelayBuffer[channel][0];
        channelParams[channel].l_circ_idx = 0;
        memset(channelParams[channel].l_delayline, 0, 2 * delay_length);
    }

    processedSamples = 0;
    state = true;
    return true;
}
int32_t mtEffectChorus::processBlock()
{
    if (!state)
        return -1;

    uint32_t dif = length - processedSamples;
    uint32_t processedBlockLength = dif > 8192 ? 8192 : dif;
    uint32_t processingNumber = processedBlockLength / 128;

    for (int channel = 0; channel < nChannels(); channel++)
    {
        for (int i = 0; i < (int)processingNumber; i++)
        {
            calculate(channelParams[channel].srcAddr, channelParams[channel].dstAddr, channel);
            channelParams[channel].srcAddr += 128;
            channelParams[channel].dstAddr += 128;
        }
    }

    if (processedBlockLength < 8192)
    {
        processedBlockLength = processingNumber * 128;
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
bool mtEffectChorus::getProcessState()
{
    return state;
}
uint32_t mtEffectChorus::getExpectedProcessLength()
{
    return confirmed[0].selection.length - confirmed[0].selection.length % 128;
}

void mtEffectChorus::calculate(int16_t* src, int16_t* dst, int channel) // process 128 samples
{
    constexpr uint16_t PROCESSED_BLOCK_LENGTH = 128;
    int sum;
    int c_idx;

    if (channelParams[channel].l_delayline == nullptr)
        return;

    if (num_chorus <= 1)
    {

        for (int i = 0; i < PROCESSED_BLOCK_LENGTH; i++)
        {
            channelParams[channel].l_circ_idx++;
            if (channelParams[channel].l_circ_idx >= delay_length)
            {
                channelParams[channel].l_circ_idx = 0;
            }
            channelParams[channel].l_delayline[channelParams[channel].l_circ_idx] = *src++;
        }

        src -= PROCESSED_BLOCK_LENGTH;

        for (int i = 0; i < PROCESSED_BLOCK_LENGTH; i++)
        {
            *dst++ = *src++;
        }

        return;
    }

    uint32_t tmp = delay_length / (num_chorus - 1) - 1;

    for (int i = 0; i < PROCESSED_BLOCK_LENGTH; i++)
    {
        channelParams[channel].l_circ_idx++;

        if (channelParams[channel].l_circ_idx >= delay_length)
        {
            channelParams[channel].l_circ_idx = 0;
        }

        channelParams[channel].l_delayline[channelParams[channel].l_circ_idx] = *src++;
        sum = 0;
        c_idx = channelParams[channel].l_circ_idx;

        for (int k = 0; k < num_chorus; k++)
        {
            sum += channelParams[channel].l_delayline[c_idx];
            if (num_chorus > 1)
                c_idx -= tmp;

            if (c_idx < 0)
                c_idx += delay_length;
        }

        *dst++ = sum / num_chorus;
    }
}
