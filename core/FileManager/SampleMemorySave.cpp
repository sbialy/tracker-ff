#include "SampleMemorySave.h"

#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"

#include <cstring>
#include <cstdio>

void SampleMemorySave::update()
{
    SampleMemorySaveStep nextStep = operationStep;

    switch (operationStep)
    {
        case SampleMemorySaveStep::initStep:
        {
            sampleMemorySaveInit();
            nextStep = SampleMemorySaveStep::saveInstrumentStructToWorkspace;
            break;
        }
        case SampleMemorySaveStep::saveInstrumentStructToWorkspace:
        {
            newFileManager.setInstrumentStructChanged(currentInstrument);
            bool result = newFileManager.saveInstrumentStructToWorkspace(currentInstrument);
            if (result)
            {
                nextStep = SampleMemorySaveStep::saveInstrumentSampleToWorkspace;
            }
            else
            {
                stopOperationWithError(fmImportSampleFromSampleEditorError);
            }
            break;
        }
        case SampleMemorySaveStep::saveInstrumentSampleToWorkspace:
        {
            bool result = saveStereoDataToWorkspace(
              currentInstrument, memoryAddressL, memoryAddressR, memorySizeBytes);
            if (result)
            {
                nextStep = SampleMemorySaveStep::finishStep;
                newFileManager.clearSampleChanged(currentInstrument);
            }
            break;
        }
        case SampleMemorySaveStep::finishStep:
        {
            sampleMemorySaveFinish();
            break;
        }
        default: stopOperationWithError(fmImportSampleFromSampleEditorError); break;
    }

    operationStep = nextStep;
}

//=================================================================================================
//=================================================================================================
void SampleMemorySave::sampleMemorySaveInit()
{
    // save samples count in pti file struct
    mtProject.instrument[currentInstrument].sample.length = memorySizeBytes / 2;
    mtProject.instrument[currentInstrument].setStreamPreferred(false);
}

bool SampleMemorySave::saveStereoDataToWorkspace(int idx,
                                                 uint8_t* dataSourceAddressL,
                                                 uint8_t* dataSourceAddressR,
                                                 uint32_t dataSizeBytes)
{
    assert(idx < kSampleInstrumentsCount);
    assert(mtProject.instrument[idx].isActive != 0);

    char instrFilePath[PATCH_SIZE];
    volatile int pathSize = sizeof(instrFilePath);
    snprintf(instrFilePath, pathSize, cWorkspaceInstrumentFileFormatNew, idx + 1);

    uint8_t saveStatus = fileTransfer.saveSampleRawDataToFile(
      dataSourceAddressL,
      dataSizeBytes, // single buffer size (L/R)
      instrFilePath,
      sizeof(strInstrumentFile),
      (dataSourceAddressR != dataSourceAddressL && dataSourceAddressR != nullptr)
        ? dataSourceAddressR
        : nullptr);

    if (saveStatus == fileTransferEnd)
    {
        return true;
    }
    else if (saveStatus == fileTransferFileNoValid)
    {
        stopOperationWithError(fmImportSampleFromSampleEditorError);
    }
    else if (saveStatus >= fileTransferError)
    {
        stopOperationWithError(fmImportSampleFromSampleEditorError);
    }

    return false;
}

void SampleMemorySave::sampleMemorySaveFinish()
{
    newFileManager.setStatus(fmImportSampleFromSampleEditorEnd);
    newFileManager.setCurrentOperation(fmNoOperation);
}

//=================================================================================================
//=================================================================================================
bool SampleMemorySave::saveSampleFromSampleEditor(int16_t* memoryAddressL,
                                                  int16_t* memoryAddressR,
                                                  uint32_t samplesCount,
                                                  uint8_t instrumentIdx)
{
    if (newFileManager.getStatus() != fmIdle)
        return false;
    if (newFileManager.getCurrentOperation() != fmNoOperation)
        return false;

    if (mtProject.instrument[instrumentIdx].isActive == 0)
    {
        if (samplesCount > 0)
        {
            newFileManager.setDefaultActiveInstrument(&mtProject.instrument[instrumentIdx]);
            if (mtProject.instrument[instrumentIdx].sample.file_name[0] == '\0')
            {
                snprintf(mtProject.instrument[instrumentIdx].sample.file_name, SAMPLE_NAME_SIZE, "Sample %02d", instrumentIdx + 1);
            }
            mtProject.instrument[instrumentIdx].isActive = 1;
        }
        else
            return false;
    }

    currentInstrument = instrumentIdx;
    this->memoryAddressL = (uint8_t*)memoryAddressL;
    this->memoryAddressR = (uint8_t*)memoryAddressR;
    memorySizeBytes = samplesCount * 2;

    newFileManager.setStatus(fmImportingSampleFromSampleEditor);
    newFileManager.setCurrentOperation(fmImportSampleFromSampleEditor);
    operationStep = SampleMemorySaveStep::initStep;
    return true;
}

//=================================================================================================
//=================================================================================================
