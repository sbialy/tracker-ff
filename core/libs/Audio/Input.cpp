#include "Input.h"
#include "LineInRing.h"
#include "utility/dspinst.h"
#include <cr_section_macros.h>

#if !defined(PLATFORM_IMX)
namespace
{
constexpr uint32_t kLineInBlocks = 8;   // 4 KB: the cushion block, a short backlog after a double render, writes in flight
// SDRAM like the other audio scratch (the internal RAM is the heap and the stack). Only the CPU touches it, never the DMA.
__NOINIT(EXTERNAL_RAM) uint32_t s_lineInSlots[kLineInBlocks][AUDIO_BLOCK_SAMPLES];
LineInRing<kLineInBlocks, AUDIO_BLOCK_SAMPLES> s_lineIn{ s_lineInSlots };   // constant initialised: the input interrupt never checks the pointer

// the input interrupt, reached by a tail call from setInBuffer so that one adds no stack frame to the nest of the stack gate
__attribute__((noinline)) void lineInPush(const void* data)
{
    if (data != nullptr) s_lineIn.push(data);
}
} // namespace

extern "C" void ff_linein_counts(uint32_t out[4])
{
    out[0] = s_lineIn.dup;
    out[1] = s_lineIn.skip;
    out[2] = s_lineIn.under;
    out[3] = s_lineIn.drop;
}
#else
extern "C" void ff_linein_counts(uint32_t out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0;
}
#endif

AUDIO_CORE_FLASH void AudioInputStream::update()
{
#if !defined(PLATFORM_IMX)
    if (lineInRing)
    {
        data = const_cast<uint32_t*>(s_lineIn.fetch());   // exactly one block per render, nullptr before the first input block
    }
#endif
#ifdef AUDIO_NULL_IS_SILENCE
    if (data == nullptr && silentWithoutData)
        return;
#endif
    audio_block_t* blockLeft = allocate();
    audio_block_t* blockRight = allocate();

    if (blockLeft == nullptr || blockRight == nullptr)
    {
        if (blockLeft)
        {
            release(blockLeft);
        }

        if (blockRight)
        {
            release(blockRight);
        }

        return;
    }

    if (data == nullptr)
    {
        for (int idx = 0; idx < AUDIO_BLOCK_SAMPLES; idx++)
        {
            blockLeft->data[idx] = 0;
            blockRight->data[idx] = 0;
        }
    }
    else
    {
#if defined(KINETISK)
        if ((reinterpret_cast<uintptr_t>(data) & 3) == 0)
        {
            // two frames per round: the two left samples and the two right samples are packed into one word each
            const uint32_t* inputWords = static_cast<const uint32_t*>(data);
            uint32_t* leftWords = reinterpret_cast<uint32_t*>(blockLeft->data);
            uint32_t* rightWords = reinterpret_cast<uint32_t*>(blockRight->data);
            for (int idx = 0; idx < AUDIO_BLOCK_SAMPLES / 2; idx++)
            {
                const uint32_t frame0 = inputWords[0]; // L0 | R0 << 16
                const uint32_t frame1 = inputWords[1];
                inputWords += 2;
                *leftWords++ = pack_16b_16b(frame1, frame0);  // L1 << 16 | L0
                *rightWords++ = pack_16t_16t(frame1, frame0); // R1 << 16 | R0
            }
        }
        else
#endif
        {
            int16_t* inputData = (int16_t*)data;
            int16_t* leftData = blockLeft->data;
            int16_t* rightData = blockRight->data;

            for (int idx = 0; idx < AUDIO_BLOCK_SAMPLES; idx++)
            {
                *leftData++ = *inputData++;
                *rightData++ = *inputData++;
            }
        }
    }

    transmit(blockLeft, 0);
    transmit(blockRight, 1);

    release(blockLeft);
    release(blockRight);
}

AUDIO_CORE_FLASH void AudioInputStream::setInBuffer(void* data)
{
#if !defined(PLATFORM_IMX)
    if (lineInRing)
    {   // the input interrupt: copy the half the DMA just finished, the DMA fills the other half for the next 2.9 ms
        lineInPush(data);
        return;
    }
#endif
    AudioInputStream::data = data;
}
