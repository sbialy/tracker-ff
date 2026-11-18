#ifndef PLATFORM_IMX

#include "UsbMultiTap.h"

// Audio block, inside update_all (so the CPU readout and the governor see it). A source that sent nothing (an idle voice, a part
// without a patch) is a null pointer, the platform writes silence for it. 14 pointers and 14 block pointers on the stack, 112 B.
void UsbMultiTap::update(void)
{
    audio_block_t* blocks[kInputs];
    const int16_t* in[kInputs];
    for (int i = 0; i < kInputs; i++)
    {
        blocks[i] = receiveReadOnly(i);
        in[i] = blocks[i] != nullptr ? blocks[i]->data : nullptr;
    }
    sink_(in);
    for (int i = 0; i < kInputs; i++)
    {
        if (blocks[i] != nullptr) release(blocks[i]);
    }
}

#endif
