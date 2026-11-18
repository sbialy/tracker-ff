#include "mtEffect.h"
#include "VoiceStreamManager.h"
#include "mtDataCopyier.h"
#include "mtStructs.h"
#include "string.h"

extern int16_t* externalRamSampleMemoryBegin;

void mtEffect::update()
{
    switch (operationType)
    {
        case OperationType::operationTypeLoading: updateLoading(); break;
        case OperationType::operationTypeProcessing: updateProcessingSelection(); break;
        default: break;
    }
}

void mtEffect::switchToThisEffect(mtEffect* lastEffect)
{
    lastEffect->applyParams.isProcessedData = 0;
    memcpy(&this->processing, &lastEffect->processing, sizeof(this->processing));
    for (uint32_t idx = 0; idx < kChannels; idx++)
    {
        memcpy(&this->processed[idx], &lastEffect->processed[idx], sizeof(this->processed[idx]));
        memcpy(&this->confirmed[idx], &lastEffect->confirmed[idx], sizeof(this->confirmed[idx]));
    }

    memcpy(&this->loadingParams, &lastEffect->loadingParams, sizeof(this->loadingParams));
    memcpy(&this->saveParams, &lastEffect->saveParams, sizeof(this->saveParams));
    memcpy(&this->applyParams, &lastEffect->applyParams, sizeof(this->applyParams));
    memcpy(&this->undoParams, &lastEffect->undoParams, sizeof(this->undoParams));
}
//***********LOAD

void mtEffect::setBaseSampleLength(uint32_t val)
{
    for (uint32_t idx = 0; idx < kChannels; idx++)
    {
        confirmed[idx].area.length = val;
        confirmed[idx].selection.length = val;
    }
}
mtEffect::LoadingResult mtEffect::startLoad(uint8_t instr_idx)
{
    if (mtProject.instrument[instr_idx].sample.length > EDIT_BUFF_SIZE / 2)
        return LoadingResult::instrumentIsTooLong;
    if (!mtProject.instrument[instr_idx].isActive)
        return LoadingResult::instrumentIsNotActive;
    assert(kChannels > 0);
    if (dataCopyier[0]->getState())
        return LoadingResult::loadingBusy;

    operationType = OperationType::operationTypeLoading;

    for (uint32_t idx = 0; idx < kChannels; idx++)
    {
        int16_t* confirmedBuffer = externalRamEffectsBuff + idx * EDIT_BUFF_SIZE;
        int16_t* processedBuffer =
          externalRamEffectsBuff + idx * EDIT_BUFF_SIZE + EDIT_BUFF_SIZE / 2;

        uint32_t selectOffset = (confirmed[idx].selection.addr >= confirmed[idx].area.addr)
          ? (uint32_t)(confirmed[idx].selection.addr - confirmed[idx].area.addr)
          : 0;
        confirmed[idx].area.addr = confirmedBuffer;
        confirmed[idx].selection.addr = confirmedBuffer + selectOffset;

        uint32_t procOffset = (processed[idx].selection.addr >= processed[idx].area.addr)
          ? (uint32_t)(processed[idx].selection.addr - processed[idx].area.addr)
          : 0;
        processed[idx].area.addr = processedBuffer;
        processed[idx].selection.addr = processedBuffer + procOffset;
    }

    loadingParams.processingParams.currentProgressValue = 0;
    loadingParams.processingParams.maxProgressValue = mtProject.instrument[instr_idx].sample.length;
    loadingParams.processingParams.state = 1;

    for (uint32_t idx = 0; idx < kChannels; idx++)
    {
        confirmed[idx].area.length = mtProject.instrument[instr_idx].sample.length;
        confirmed[idx].selection.length = mtProject.instrument[instr_idx].sample.length;
    }

    int16_t* bufferWithoutOverlapWithEditorAreas[kChannels] = {};

    for (uint32_t idx = 0; idx < kChannels; idx++)
    {
        int16_t* src = mtProject.sampleMemory[instr_idx].address[idx];
        int16_t* dst = confirmed[idx].area.addr;
        uint32_t len = mtProject.instrument[instr_idx].sample.length;

        if (src && dst && len > 0)
        {
            if (mtProject.sampleMemory[instr_idx].isStreamed)
            {
                if (idx == 0 || mtProject.sampleMemory[instr_idx].isStereo)
                {
                    voiceStreamManager.readInstrumentFramesBlocking(instr_idx, idx, 0, len, dst);
                }
                else
                {
                    memcpy(dst, confirmed[0].area.addr, len * sizeof(int16_t));
                }
                dataCopyier[idx]->start(dst, dst, 0);
                continue;
            }

            if (src == dst)
            {
                continue;
            }

            // Check if source and destination memory ranges overlap
            bool overlap = (src < dst + len) && (dst < src + len);
            if (overlap)
            {
                bufferWithoutOverlapWithEditorAreas[idx] =
                  externalRamSampleMemoryBegin + EDIT_BUFF_SIZE * idx;

                memmove(
                  bufferWithoutOverlapWithEditorAreas[idx],
                  src,
                  len * sizeof(int16_t));
                src = bufferWithoutOverlapWithEditorAreas[idx];
            }

            dataCopyier[idx]->start(src, dst, len);
        }
    }

    return LoadingResult::loadingSuccessful;
}
void mtEffect::updateLoading()
{
    if (!loadingParams.processingParams.state)
        return;

    int32_t blockSize = 0;

    for (uint32_t idx = 0; idx < kChannels; idx++)
    {
        blockSize = dataCopyier[idx]->update();
    }

    if (blockSize != -1)
        loadingParams.processingParams.currentProgressValue += blockSize;

    assert(kChannels > 0);
    loadingParams.processingParams.state = dataCopyier[0]->getState();
    if (!loadingParams.processingParams.state)
    {

        operationType = OperationType::operationTypeIdle;
        processing.isLoadedData = true;
        changeSelectionRange(loadingParams.preloadPoints.a, loadingParams.preloadPoints.b);
        if (loadingParams.isAnyOtherProcessingOnFinish)
        {
            loadingParams.isAnyOtherProcessingOnFinish = false;
            processSelection();
        }
    }
}
bool mtEffect::getLoadingState()
{
    return getState(&loadingParams.processingParams);
}
uint8_t mtEffect::getLoadingProgress()
{
    return getProgress(&loadingParams.processingParams);
}

bool mtEffect::isDataLoaded()
{
    return processing.isLoadedData;
}

void mtEffect::clearLoadedData()
{
    processing.isLoadedData = false;
    loadingParams.preloadPoints.a = 0;
    loadingParams.preloadPoints.b = 65535;
    for (uint32_t idx = 0; idx < kChannels; idx++)
    {
        int16_t* confirmedBuffer = externalRamEffectsBuff + idx * EDIT_BUFF_SIZE;
        int16_t* processedBuffer =
          externalRamEffectsBuff + idx * EDIT_BUFF_SIZE + EDIT_BUFF_SIZE / 2;

        confirmed[idx].area.addr = confirmedBuffer;
        confirmed[idx].selection.addr = confirmedBuffer;
        processed[idx].area.addr = processedBuffer;
        processed[idx].selection.addr = processedBuffer;
    }
}
//***********
//***********APPLY
void mtEffect::apply()
{
    if (!applyParams.isProcessedData)
    {
        processSelection();
        processing.isApplyOnEnd = 1;
        return;
    }

    for (uint32_t idx = 0; idx < kChannels; idx++)
    {
        strMemoryAreaWithSelection tmp = processed[idx];
        processed[idx] = confirmed[idx];
        confirmed[idx] = tmp;
    }

    processing.isLoadedData = true;
    undoParams.isEnable = 1;
    applyParams.isProcessedData = 0;
}
//***********
//***********APPLY
void mtEffect::undo()
{
    if (!undoParams.isEnable)
        return;

    for (uint32_t idx = 0; idx < kChannels; idx++)
    {
        strMemoryAreaWithSelection tmp = processed[idx];
        processed[idx] = confirmed[idx];
        confirmed[idx] = tmp;
    }

    undoParams.isEnable = 0;
}

//***********
//***********PROCESSING

bool mtEffect::processSelection()
{
    if (!processing.isLoadedData)
    {
        uint8_t instr_idx = mtProject.values.lastUsedInstrument;
        bool hasInstrumentSample = (instr_idx < kSampleInstrumentsCount) &&
                                   mtProject.instrument[instr_idx].isActive &&
                                   (mtProject.sampleMemory[instr_idx].address[0] != nullptr) &&
                                   (mtProject.instrument[instr_idx].sample.length > 0);
        if (hasInstrumentSample)
        {
            loadingParams.isAnyOtherProcessingOnFinish = true;
            startLoad(instr_idx);
            return false;
        }
        else if (!processingWithoutCopying)
        {
            return false;
        }
    }

    if (processingState != ProcessingState::idle)
        return false;
    uint32_t expectedSelectLen = getExpectedProcessLength();
    if (expectedSelectLen == 0)
        return false;

    for (uint32_t idx = 0; idx < kChannels; idx++)
    {
        if (processingWithoutCopying)
        {
            if (expectedSelectLen > EDIT_BUFF_SIZE / 2)
                return false;

            processed[idx].selection.addr = processed[idx].area.addr;
            processed[idx].selection.length = expectedSelectLen;
            processed[idx].area.length = expectedSelectLen;
        }
        else
        {
            int32_t dif = expectedSelectLen - confirmed[idx].selection.length;

            if (confirmed[idx].area.length + dif > EDIT_BUFF_SIZE / 2)
                return false;

            processed[idx].selection.addr =
              processed[idx].area.addr +
              (uint32_t)(confirmed[idx].selection.addr - confirmed[idx].area.addr);
            processed[idx].selection.length = expectedSelectLen;
            processed[idx].area.length = confirmed[idx].area.length + (processed[idx].selection.length -
                                                                       confirmed[idx].selection.length);
        }
    }
    processing.processParams.currentProgressValue = 0;
    processing.processParams.maxProgressValue = expectedSelectLen;
    processing.processParams.state = true;

    endProcessingState = true;

    operationType = OperationType::operationTypeProcessing;

    return true;
}
void mtEffect::updateProcessingSelection()
{
    switch (processingState)
    {
        case ProcessingState::idle: break;
        case ProcessingState::copyingBeforeProcessing:
            if (!processingWithoutCopying)
            {
                updateCopying();
            }
            break;
        case ProcessingState::processingSelection: updateProcessing(); break;
        case ProcessingState::copyingAfterProcessing:
            if (!processingWithoutCopying)
            {
                updateCopying();
            }
            break;
        default: break;
    }

    if (endProcessingState)
    {
        endProcessingState = 0;

        switch (processingState)
        {
            case ProcessingState::idle:
                if (processingWithoutCopying)
                {
                    endProcessingState = true;
                }
                else
                {
                    for (uint32_t idx = 0; idx < kChannels; idx++)
                    {
                        dataCopyier[idx]->start(
                          confirmed[idx].area.addr,
                          processed[idx].area.addr,
                          (uint32_t)(confirmed[idx].selection.addr - confirmed[idx].area.addr));
                    }
                }

                break;
            case ProcessingState::copyingBeforeProcessing:
                if (!startProcess())
                {
                    endProcessingState = true;
                    processingState = ProcessingState::copyingAfterProcessing;
                    processing.processParams.state = false;
                    applyParams.isProcessedData = 0;
                    undoParams.isEnable = 0;
                }
                break;
            case ProcessingState::processingSelection:
                if (processingWithoutCopying)
                {
                    endProcessingState = true;
                }
                else
                {
                    for (uint32_t idx = 0; idx < kChannels; idx++)
                    {
                        dataCopyier[idx]->start(
                          (int16_t*)(confirmed[idx].selection.addr +
                                     confirmed[idx].selection.length),
                          (int16_t*)(processed[idx].selection.addr +
                                     processed[idx].selection.length),
                          confirmed[idx].area.length -
                            (confirmed[idx].selection.length +
                             (uint32_t)(confirmed[idx].selection.addr - confirmed[idx].area.addr)));
                    }
                }
                break;
            case ProcessingState::copyingAfterProcessing:
                processing.processParams.state = false;
                applyParams.isProcessedData = 1;
                undoParams.isEnable = 0;
                if (processing.isApplyOnEnd)
                {
                    apply();
                    processing.isApplyOnEnd = false;
                }
                break;
        }

        if (processingState != ProcessingState::copyingAfterProcessing)
            processingState++;
        else
            processingState = ProcessingState::idle;
    }
}
uint8_t mtEffect::getProcessingSelectionProgress()
{
    return getProgress(&processing.processParams);
}
bool mtEffect::getProcessingSelectionState()
{
    return getState(&processing.processParams);
}

void mtEffect::updateProcessing()
{

    int32_t blockSize = processBlock();

    if (blockSize != -1)
        processing.processParams.currentProgressValue += blockSize;

    if (!getProcessState())
        endProcessingState = true;
}

void mtEffect::updateCopying()
{
    int32_t blockSize = 0;
    for (uint32_t idx = 0; idx < kChannels; idx++)
    {
        blockSize = dataCopyier[idx]->update();
    }

    if (blockSize != -1)
        processing.processParams.currentProgressValue += blockSize;

    assert(kChannels > 0);
    if (!dataCopyier[0]->getState())
    {
        endProcessingState = true;
    }
}

bool mtEffect::isDataProcessed()
{
    return applyParams.isProcessedData;
}

void mtEffect::clearProcessedData()
{
    applyParams.isProcessedData = false;
}

uint32_t mtEffect::getExpectedProcessedSampleLength()
{
    uint32_t expectedSelectionLength = getExpectedProcessLength();

    assert(kChannels > 0);
    if (processingWithoutCopying)
    {
        return expectedSelectionLength;
    }
    uint32_t expectedAreaLength =
      confirmed[0].area.length + (expectedSelectionLength - confirmed[0].selection.length);

    return expectedAreaLength;
}
//***********PREVIEW GETTERS
int16_t* mtEffect::getSampleAddresToPreview(Channel channel)
{
    if (channel >= kChannels)
    {
        return processed[0].selection.addr;
    }
    return processed[channel].selection.addr;
}
uint32_t mtEffect::getSampleLengthToPreview()
{
    assert(kChannels > 0);
    return processed[0].selection.length;
}
//***********
//***********PLAY GETTERS
int16_t* mtEffect::getAddresToPolyphonicPlay(Channel channel)
{
    if (channel >= kChannels)
    {
        return confirmed[0].area.addr;
    }

    return confirmed[channel].area.addr;
}
uint32_t mtEffect::getLengthToPolyphonicPlay()
{
    assert(kChannels > 0);
    return confirmed[0].area.length;
}
//***********
//***********SELECTION SET
void mtEffect::changeSelectionRange(uint16_t a, uint16_t b)
{
    uint32_t length;
    uint32_t addressShift;

    if (a > b)
    {
        uint16_t temp = a;
        a = b;
        b = temp;
    }
    assert(kChannels > 0);
    length = (uint32_t)((uint32_t)b * (float)(confirmed[0].area.length) / MAX_16BIT);
    addressShift = (uint32_t)((uint32_t)a * (float)(confirmed[0].area.length) / MAX_16BIT);

    for (uint32_t idx = 0; idx < kChannels; idx++)
    {
        confirmed[idx].selection.addr = confirmed[idx].area.addr + addressShift;
        confirmed[idx].selection.length = length - addressShift;
    }

    if (!processing.isLoadedData)
    {
        loadingParams.preloadPoints.a = a;
        loadingParams.preloadPoints.b = b;
    }
}

uint16_t mtEffect::getNewEndPoint()
{
    uint16_t startPoint = getNewStartPoint();
    assert(kChannels > 0);
    if (confirmed[0].area.length == 0)
        return MAX_16BIT;

    return startPoint +
           (uint16_t)((confirmed[0].selection.length / (float)confirmed[0].area.length) *
                      MAX_16BIT);
}
uint16_t mtEffect::getNewStartPoint()
{
    assert(kChannels > 0);
    if (confirmed[0].area.length == 0)
        return 0;

    return (uint16_t)(((uint32_t)(confirmed[0].selection.addr - confirmed[0].area.addr) /
                       (float)confirmed[0].area.length) *
                      MAX_16BIT);
}
//***********
bool mtEffect::getState(ProcessingParams* ptr)
{
    return ptr->state;
}

uint8_t mtEffect::getProgress(ProcessingParams* ptr)
{
    if (ptr->maxProgressValue == 0)
        return 0;
    return 100 * (ptr->currentProgressValue / (float)ptr->maxProgressValue);
}

//***********
