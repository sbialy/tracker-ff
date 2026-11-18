#include "mtSliceManager.h"
#include "VoiceStreamManager.h"
#include "mtEffect.h"
#include "mtSliceDetector.h"
#include "string.h"
#include <algorithm>

mtSliceManager sliceManager;

void mtSliceManager::addSlice(strInstrument* instr)
{
    if (instr->sliceNumber == MAX_SLICE_NUMBER)
        return;
    if (instr->sliceNumber == 0)
    {
        instr->sliceNumber++;
        instr->selectedSlice = 0;
        instr->slices[0] = 0;
        return;
    }
    // Lowering the slice count can leave the selection past the last slice.
    if (instr->selectedSlice >= instr->sliceNumber)
        instr->selectedSlice = instr->sliceNumber - 1;
    if (instr->selectedSlice == (instr->sliceNumber - 1))
    {
        instr->selectedSlice++;
        instr->sliceNumber++;
        instr->slices[instr->selectedSlice] =
          (MAX_16BIT + instr->slices[instr->selectedSlice - 1]) / 2;
        return;
    }
    for (int8_t i = instr->sliceNumber; i > (instr->selectedSlice + 1); i--)
    {
        instr->slices[i] = instr->slices[i - 1];
    }
    instr->selectedSlice++;
    instr->slices[instr->selectedSlice] = ((uint32_t)(instr->slices[instr->selectedSlice - 1] +
                                                      instr->slices[instr->selectedSlice + 1])) /
                                          2;

    instr->sliceNumber++;
}
void mtSliceManager::removeSlice(strInstrument* instr)
{
    if ((instr->sliceNumber == 0) || (instr->sliceNumber <= instr->selectedSlice))
        return;
    if ((instr->sliceNumber - 1) == instr->selectedSlice)
    {
        instr->selectedSlice = instr->sliceNumber == 1 ? 0 : instr->selectedSlice - 1;
        instr->sliceNumber--;
        return;
    }

    for (uint8_t i = instr->selectedSlice; i < instr->sliceNumber - 1; i++)
    {
        instr->slices[i] = instr->slices[i + 1];
    }
    instr->sliceNumber--;
    if (instr->selectedSlice != 0)
        instr->selectedSlice--;
    else
        instr->selectedSlice = 0;
}
void mtSliceManager::adjustSlice(strInstrument* instr, int32_t val)
{
    if ((instr->sliceNumber == 0) || (instr->selectedSlice >= instr->sliceNumber))
        return;

    if ((instr->selectedSlice != (instr->sliceNumber - 1)) && (instr->selectedSlice != 0))
    {
        if (instr->slices[instr->selectedSlice] + val >= instr->slices[instr->selectedSlice + 1])
            instr->slices[instr->selectedSlice] = instr->slices[instr->selectedSlice + 1] - 1;
        else if (instr->slices[instr->selectedSlice] + val <=
                 instr->slices[instr->selectedSlice - 1])
            instr->slices[instr->selectedSlice] = instr->slices[instr->selectedSlice - 1] + 1;
        else
            instr->slices[instr->selectedSlice] += val;
    }
    else if ((instr->selectedSlice == 0) && (instr->selectedSlice == (instr->sliceNumber - 1)))
    {
        if (instr->slices[instr->selectedSlice] + val > MAX_16BIT)
            instr->slices[instr->selectedSlice] = MAX_16BIT;
        else if (instr->slices[instr->selectedSlice] + val < 0)
            instr->slices[instr->selectedSlice] = 0;
        else
            instr->slices[instr->selectedSlice] += val;
    }
    else if (instr->selectedSlice == instr->sliceNumber - 1)
    {
        if (instr->slices[instr->selectedSlice] + val > MAX_16BIT)
            instr->slices[instr->selectedSlice] = MAX_16BIT;
        else if (instr->slices[instr->selectedSlice] + val <=
                 instr->slices[instr->selectedSlice - 1])
            instr->slices[instr->selectedSlice] = instr->slices[instr->selectedSlice - 1] + 1;
        else
            instr->slices[instr->selectedSlice] += val;
    }
    else if (instr->selectedSlice == 0)
    {
        if (instr->slices[instr->selectedSlice] + val >= instr->slices[instr->selectedSlice + 1])
            instr->slices[instr->selectedSlice] = instr->slices[instr->selectedSlice + 1] - 1;
        else if (instr->slices[instr->selectedSlice] + val < 0)
            instr->slices[instr->selectedSlice] = 0;
        else
            instr->slices[instr->selectedSlice] += val;
    }
}
void mtSliceManager::autoSlice(strInstrument* instr)
{
    memset(instr->slices, 0, MAX_SLICE_NUMBER * 2);
    instr->selectedSlice = 0;

    int instrIdx = (int)(instr - mtProject.instrument);
    if (instrIdx >= 0 && instrIdx < kSampleInstrumentsCount &&
        mtProject.sampleMemory[instrIdx].isStreamed)
    {
        uint32_t maxFrames = std::min<uint32_t>(instr->sample.length, (uint32_t)EDIT_BUFF_SIZE);
        voiceStreamManager.readInstrumentFramesBlocking(
          instrIdx, 0, 0, maxFrames, externalRamEffectsBuff);
        sliceDetector.start(
          externalRamEffectsBuff, maxFrames, instr->slices, &instr->sliceNumber);
        return;
    }

    sliceDetector.start(
      instr->sample.address, instr->sample.length, instr->slices, &instr->sliceNumber);
}

void mtSliceManager::equalSlice(strInstrument* instr, uint8_t n)
{
    instr->sliceNumber = n;
    if (instr->selectedSlice >= n)
        instr->selectedSlice = (n > 0) ? n - 1 : 0;

    for (uint8_t i = 0; i < n; i++)
    {
        instr->slices[i] = (MAX_16BIT * i) / n;
    }
}

uint8_t mtSliceManager::getAutoSliceProgress()
{
    return sliceDetector.getProgress();
}
uint8_t mtSliceManager::getAutoSliceState()
{
    return sliceDetector.getState();
}
