#include "mtEffectOverdrive.h"
#include "mtStructs.h"

#include <cmath>
#include <stdint.h>

#define UPDATE_BLOCK_LENGTH 8192u

void mtEffectOverdrive::setParameter(float& value, int idx)
{
    switch (idx)
    {
        case GAIN: gain = value; break;
        case TYPE: type = (int)value; break;
        default: assert(false); break;
    }
}

bool mtEffectOverdrive::startProcess()
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

int32_t mtEffectOverdrive::processBlock()
{
    if (!state)
    {
        return -1;
    }

    uint32_t toProcess = length - processedSamples;
    uint32_t blockLength = std::min<uint32_t>(toProcess, UPDATE_BLOCK_LENGTH);

    float gainNorm = gain;

    switch (type)
    {
        case EXPONENTIAL: gainNorm *= 4; break;
        case RECIPROCAL: gainNorm *= 10; break;
    };

    for (int channel = 0; channel < nChannels(); channel++)
    {
        for (uint16_t i = 0; i < blockLength; i++)
        {
            int16_t outVal = 0;

            // https://ccrma.stanford.edu/~orchi/Documents/DAFx.pdf
            // https://www.dafx12.york.ac.uk/papers/dafx12_submission_45.pdf
            // https://www.desmos.com/calculator/n6kgglhn9b

            switch (type)
            {
                case QUADRATIC:
                {
                    const int32_t in = *channelParams[channel].srcAddr * gainNorm;
                    const int32_t inAbs = in >= 0 ? in : -in;
                    const int32_t inSign = in >= 0 ? INT16_MAX : INT16_MIN;
                    if (inAbs > (int32_t)2 * INT16_MAX / 3)
                    {
                        // y = 1
                        outVal = inSign;
                    }
                    else if (inAbs > INT16_MAX / 3)
                    {
                        // y = (3 - (2 - 3x)^2) / 3
                        int32_t temp = 2 * (int32_t)INT16_MAX - 3 * (int32_t)inAbs;
                        outVal = (3 * (int32_t)INT16_MAX - (temp * temp / 0x8000)) / 3;
                        if (inSign < 0)
                        {
                            outVal = -outVal;
                        }
                    }
                    else
                    {
                        // y = 2x
                        outVal = 2 * in;
                    }
                    break;
                }
                case EXPONENTIAL:
                {
                    const float in = (float)*channelParams[channel].srcAddr / INT16_MAX;
                    const float inSign = in >= 0 ? 1 : -1;

#ifdef FW_TRIM
                    // float expf instead of double exp (flash trim)
                    outVal =
                      (inSign * ((float)M_E - expf(1.0f - gainNorm * in * inSign)) / (float)M_E) * INT16_MAX;
#else
                    outVal =
                      (inSign * (M_E - exp(1.0f - gainNorm * in * inSign)) / M_E) * INT16_MAX;
#endif
                    break;
                }
                case RECIPROCAL:
                {
                    const float in = (float)*channelParams[channel].srcAddr / INT16_MAX;
                    const float inSign = in == 0 ? 0 : (in >= 0 ? 1 : -1);

                    outVal = inSign * (1 - 1 / (abs(gainNorm * in) + 1)) * INT16_MAX;
                    break;
                }
                default: assert(0);
            }

            *channelParams[channel].dstAddr = (int16_t)outVal;
            channelParams[channel].dstAddr++;
            channelParams[channel].srcAddr++;
        }
    }
    if (blockLength < UPDATE_BLOCK_LENGTH)
    {
        state = 0;
    }

    processedSamples += blockLength;

    if (!state)
    {
        for (int channel = 0; channel < nChannels(); channel++)
        {
            processed[channel].selection.length = processedSamples;
            processed[channel].area.length =
              confirmed[channel].area.length +
              (processed[channel].selection.length - confirmed[channel].selection.length);
        }
    }

    return blockLength;
}

bool mtEffectOverdrive::getProcessState()
{
    return state;
}

uint32_t mtEffectOverdrive::getExpectedProcessLength()
{
    return confirmed[0].selection.length;
}

mtEffectOverdrive sampleEditorOverdrive;
