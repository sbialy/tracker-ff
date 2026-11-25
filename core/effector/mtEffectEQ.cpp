#include "mtEffectEQ.h"
#include "mtStructs.h"

#include "util_math.h"
#include <algorithm>
#include <cmath>
#include <stdint.h>

#define UPDATE_BLOCK_LENGTH 8192u

void mtEffectEQ::setParameter(float& value, int idx)
{
    switch (idx)
    {
        case LEVEL_LOW: levelLow = value; break;
        case LEVEL_MID: levelMid = value; break;
        case LEVEL_HIGH: levelHigh = value; break;
        case FREQ_LOW:
            freqLow = value;
            bwLow = calculateBandwidth(freqLow, qLow);
            break;
        case FREQ_MID:
            freqMid = value;
            bwMid = calculateBandwidth(freqMid, qMid);
            break;
        case FREQ_HIGH:
            freqHigh = value;
            bwHigh = calculateBandwidth(freqHigh, qHigh);
            break;
        case Q_LOW:
            qLow = value;
            bwLow = calculateBandwidth(freqLow, qLow);
            break;
        case Q_MID:
            qMid = value;
            bwMid = calculateBandwidth(freqMid, qMid);
            break;
        case Q_HIGH:
            qHigh = value;
            bwHigh = calculateBandwidth(freqHigh, qHigh);
            break;
        default: assert(false); break;
    }
}

void mtEffectEQ::initContext(EqContext* ctx, float freq, float bandwidth, float level)
{
    ctx->z1 = 0;
    ctx->z2 = 0;
    // remap level +- 12db => gain 1-4 peak, 0-1 notch
    ctx->gain = level >= 0 ? (1 + level * 3 / 12) : (1 + level * 0.8 / 12);
#ifdef FW_TRIM
    // float cosf/tanf instead of double cos/tan (flash trim, drops the double libm kernels)
    ctx->d = cosf(2 * (float)M_PI * freq / AUDIO_SAMPLE_RATE_EXACT);
    ctx->c = tanf((float)M_PI * bandwidth / AUDIO_SAMPLE_RATE_EXACT);
#else
    ctx->d = cos(2 * M_PI * freq / AUDIO_SAMPLE_RATE_EXACT);
    ctx->c = tan(M_PI * bandwidth / AUDIO_SAMPLE_RATE_EXACT);
#endif
}

bool mtEffectEQ::startProcess()
{
    length = confirmed[0].selection.length;

    for (int channel = 0; channel < nChannels(); channel++)
    {
        channelParams[channel].srcAddr = confirmed[channel].selection.addr;
        channelParams[channel].dstAddr = processed[channel].selection.addr;
    }

    processedSamples = 0;
    state = true;
    for (int channel = 0; channel < nChannels(); channel++)
    {
        initContext(&channelParams[channel].ctxLow, freqLow, bwLow, levelLow);
        initContext(&channelParams[channel].ctxMid, freqMid, bwMid, levelMid);
        initContext(&channelParams[channel].ctxHigh, freqHigh, bwHigh, levelHigh);
    }

    return true;
}

// based on eqfil implementation in soundpipe
// https://github.com/eljeff/Soundpipe/blob/master/modules/eqfil.c
// which is based on csound
// http://www.csounds.com/manual/html/eqfil.html
// which is based on Regalia and Mitra design
// ("Tunable Digital Frequency Response Equalization Filters", IEEE Trans. on Ac., Sp. and Sig
// Proc., 35 (1), 1987)
// https://pdfs.semanticscholar.org/3170/8e375695e99ac4565891e10c9b8ad2333f4d.pdf
float mtEffectEQ::computeSample(EqContext* p, float in)
{
    float z1 = p->z1, z2 = p->z2, c, d, w, a, y;
    float g;
    float out;

    c = p->c;
    d = p->d;
    a = (1.0 - c) / (1.0 + c);
    g = p->gain;

    w = in + d * (1.0 + a) * z1 - a * z2;
    y = w * a - d * (1.0 + a) * z1 + z2;
    z2 = z1;
    z1 = w;
    out = (0.5 * (y + in + g * (in - y)));

    p->z1 = z1;
    p->z2 = z2;

    return out;
}

int32_t mtEffectEQ::processBlock()
{
    if (!state)
    {
        return -1;
    }

    uint32_t toProcess = length - processedSamples;
    uint32_t blockLength = std::min<uint32_t>(toProcess, UPDATE_BLOCK_LENGTH);

    for (int channel = 0; channel < nChannels(); channel++)
    {
        for (uint16_t i = 0; i < blockLength; i++)
        {
            float out = (float)*channelParams[channel].srcAddr / INT16_MAX;

            if (levelLow != 1)
            {
                out = computeSample(&channelParams[channel].ctxLow, out);
            }
            if (levelMid != 1)
            {
                out = computeSample(&channelParams[channel].ctxMid, out);
            }
            if (levelHigh != 1)
            {
                out = computeSample(&channelParams[channel].ctxHigh, out);
            }

            out = out > 1.0 ? 1.0 : (out < -1.0 ? -1.0 : out);

            *channelParams[channel].dstAddr = (int16_t)(out * INT16_MAX);
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

bool mtEffectEQ::getProcessState()
{
    return state;
}

uint32_t mtEffectEQ::getExpectedProcessLength()
{
    return confirmed[0].selection.length;
}

float mtEffectEQ::calculateBandwidth(float freq, float q)
{
    freq = clamp<float>(freq, 0.0f, frequencyMax);

    float qOffset = qOffsetMax * (freq / frequencyMax);

    return clamp(freq / (q + qOffset), 0.0f, frequencyMax);
}

mtEffectEQ sampleEditorEQ;
