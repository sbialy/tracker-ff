#include "mtEffectBitcrusher.h"
#include "mtStructs.h"
#include <cstring>

void mtEffectBitcrusher::setParameter(float& value, int idx)
{
    switch (idx)
    {
        case 0: effectBitcrusherParams.bits = (uint8_t)value; break;
        case 1: effectBitcrusherParams.rate = (uint16_t)value; break;
        default: assert(false); break;
    }
}

bool mtEffectBitcrusher::startProcess()
{
    length = confirmed[0].selection.length;
    if (length == 0)
        return false;

    for (int channel = 0; channel < nChannels(); channel++)
    {
        channelParams[channel].srcAddr = confirmed[channel].selection.addr;
        channelParams[channel].dstAddr = processed[channel].selection.addr;
    }

    int n = (44117.64706 / effectBitcrusherParams.rate) + 0.5;
    if (n < 1)
        n = 1;
    else if (n > 64)
        n = 64;
    sampleStep = n;

    if (effectBitcrusherParams.bits > 16)
        effectBitcrusherParams.bits = 16;
    else if (effectBitcrusherParams.bits == 0)
        effectBitcrusherParams.bits = 1;
    crushBits = effectBitcrusherParams.bits;

    processedSamples = 0;

    state = true;

    return true;
}

int32_t mtEffectBitcrusher::processBlock()
{
    if (!state)
        return -1;

    uint32_t dif = length - processedSamples;
    uint32_t processedBlockLength = dif > 8192 ? 8192 : dif;

    for (int channel = 0; channel < nChannels(); channel++)
    {
        int16_t* dst = channelParams[channel].dstAddr;
        const int16_t* src = channelParams[channel].srcAddr;

        if (crushBits == 16 && sampleStep <= 1)
        {
            memcpy(dst, src, processedBlockLength * sizeof(int16_t));
            dst += processedBlockLength;
            src += processedBlockLength;
        }
        else if (sampleStep <= 1)
        {
            uint8_t shift = 16 - crushBits;
            for (uint32_t i = 0; i < processedBlockLength; i++)
            {
                int32_t sampleSquidge = (*src++) >> shift;
                *dst++ = (int16_t)(sampleSquidge << shift);
            }
        }
        else
        {
            uint8_t shift = (crushBits < 16) ? (16 - crushBits) : 0;
            uint32_t i = 0;
            while (i < processedBlockLength)
            {
                int32_t sampleSqueeze = *src;
                if (shift > 0)
                {
                    sampleSqueeze = (sampleSqueeze >> shift) << shift;
                }
                uint32_t step = (processedBlockLength - i < (uint32_t)sampleStep)
                                  ? (processedBlockLength - i)
                                  : (uint32_t)sampleStep;
                for (uint32_t j = 0; j < step; j++)
                {
                    *dst++ = (int16_t)sampleSqueeze;
                    src++;
                }
                i += step;
            }
        }

        channelParams[channel].dstAddr = dst;
        channelParams[channel].srcAddr = const_cast<int16_t*>(src);
    }

    processedSamples += processedBlockLength;

    if (processedSamples >= length)
    {
        state = false;
        for (int channel = 0; channel < nChannels(); channel++)
        {
            processed[channel].selection.length = processedSamples;
            processed[channel].area.length =
              confirmed[channel].area.length +
              (processed[channel].selection.length - confirmed[channel].selection.length);
        }
    }

    return processedBlockLength;
}

bool mtEffectBitcrusher::getProcessState()
{
    return state;
}
uint32_t mtEffectBitcrusher::getExpectedProcessLength()
{
    return confirmed[0].selection.length;
}

mtEffectBitcrusher sampleEditorBitcrusher;
//***********
