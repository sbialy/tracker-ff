#include "RecordingSave.h"
#include <Recorder.h>

#include "TrackerImpl.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"
#include "mtStructs.h"
#include "polyassert.h"
#include "polyrequire.h"
#include <new>

static char recordedFileName[SAMPLE_NAME_SIZE + 1];

void RecordingSave::update()
{
    RecordingImportStep nextStep = operationStep;

    switch (operationStep)
    {
        case RecordingImportStep::saveRecordedFile:
        {
            if (saveRecording())
            {
                nextStep = RecordingImportStep::saveInstrumentStruct;
            }
            break;
        }
        case RecordingImportStep::saveInstrumentStruct:
        {
            prepareInstrumentStruct(instrumentSaveContext.importInstrumentIdx, recordedFileName);

            newFileManager.setInstrumentStructChanged(instrumentSaveContext.importInstrumentIdx);
            newFileManager.saveInstrumentStructToWorkspace(
              instrumentSaveContext.importInstrumentIdx);

            nextStep = RecordingImportStep::copySampleData;
            break;
        }
        case RecordingImportStep::copySampleData:
        {
            bool result = saveStereoDataToWorkspace(instrumentSaveContext.importInstrumentIdx,
                                                    instrumentSaveContext.memoryAddressL,
                                                    instrumentSaveContext.memoryAddressR,
                                                    instrumentSaveContext.channelDataSizeBytes);
            if (result)
            {
                nextStep = RecordingImportStep::finishStep;
            }
            break;
        }
        case RecordingImportStep::finishStep:
        {
            saveRecordingFinish();
            break;
        }
        default: stopOperationWithError(fmSaveRecordedError); break;
    }

    operationStep = nextStep;
}

//=================================================================================================
//=================================================================================================
bool RecordingSave::saveRecording()
{
    if (updateSave() == false)
    {
        if (newFileManager.getStatus() == fmSavingImportingRecordedSound)
        {
            return true;
        }
        else
        {
            newFileManager.setStatus(fmSaveRecordedSoundEnd);
            newFileManager.setCurrentOperation(fmNoOperation);
        }
    }

    return false;
}

bool RecordingSave::updateSave()
{
    if (!sdCardFileSaveContext.isActive)
    {
        return false;
    }

    if (sdCardFileSaveContext.savedSoundData < sdCardFileSaveContext.soundLength)
    {
        FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
        static constexpr uint32_t kBlockLength = 512;
        uint32_t blockLength = std::min(
          kBlockLength, sdCardFileSaveContext.soundLength - sdCardFileSaveContext.savedSoundData);

        uint32_t blockSize =
          blockLength * sizeof(int16_t) * sdCardFileSaveContext.soundAddress.size();

        if (sdCardFileSaveContext.soundAddress.size() == 1)
        {
            fileSystem->writeFile(
              &sdCardFileSaveContext.soundAddress[0][sdCardFileSaveContext.savedSoundData],
              blockSize,
              sdCardFileSaveContext.file);
        }
        else if (sdCardFileSaveContext.soundAddress.size() == 2)
        {
            int16_t* const interleavedBuffer =
              new (std::nothrow) int16_t[blockSize / sizeof(int16_t)];
            if (interleavedBuffer == nullptr)
            {
                // short heap: close the file (the wav is incomplete) and fail the save, nothing writes through null
                fileSystem->closeFile(sdCardFileSaveContext.file, false);
                sdCardFileSaveContext.isActive = false;
                stopOperationWithError(fmSaveRecordedError);
                return true;
            }
            int16_t* destAddress = interleavedBuffer;
            int16_t* sourceLeft = (int16_t*)&sdCardFileSaveContext
                                    .soundAddress[0][sdCardFileSaveContext.savedSoundData];
            int16_t* sourceRight = (int16_t*)&sdCardFileSaveContext
                                     .soundAddress[1][sdCardFileSaveContext.savedSoundData];

            for (uint32_t idx = 0; idx < blockLength; idx++)
            {
                *destAddress++ = sourceLeft[idx];
                *destAddress++ = sourceRight[idx];
            }

            fileSystem->writeFile(interleavedBuffer, blockSize, sdCardFileSaveContext.file);

            delete[] interleavedBuffer;
        }

        sdCardFileSaveContext.savedSoundData += blockLength;
    }
    else
    {
        finishSave();

        return false;
    }

    return true;
}

void RecordingSave::finishSave()
{
    FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();

    uint32_t dataSize = sdCardFileSaveContext.soundAddress.size() *
                        sdCardFileSaveContext.soundLength * sizeof(int16_t);

    strWavFileHeader header;
    header.chunkId = 0x46464952; // "RIFF"
    header.chunkSize = dataSize + 36;
    header.waveId = WAVEID;          // "WAVE"
    header.subchunk1Id = 0x20746d66; // "fmt "
    header.subchunk1Size = 16;
    header.AudioFormat = 1;
    header.numChannels = sdCardFileSaveContext.soundAddress.size();
    header.sampleRate = 44100;
    header.bitsPerSample = 16;
    header.byteRate = header.sampleRate * header.numChannels * (header.bitsPerSample / 8);
    header.blockAlign = header.numChannels * (header.bitsPerSample / 8);
    header.subchunk2Id = 0x61746164; // "data"
    header.subchunk2Size = dataSize;

    fileSystem->seekFile(0, sdCardFileSaveContext.file);
    fileSystem->writeFile(&header, sizeof(header), sdCardFileSaveContext.file);
    fileSystem->closeFile(sdCardFileSaveContext.file, false);

    sdCardFileSaveContext.isActive = false;
}

void RecordingSave::prepareInstrumentStruct(int idx, char* name)
{
    if (mtProject.instrument[idx].isActive == 0)
    {
        newFileManager.setDefaultActiveInstrument(&mtProject.instrument[idx]);
    }
    mtProject.instrument[idx].setStreamPreferred(false);

    mtProject.instrument[idx].sample.length = recorder.getLength();
    strncpy(mtProject.instrument[idx].sample.file_name, name, SAMPLE_NAME_SIZE);
    mtProject.instrument[idx].sample.file_name[SAMPLE_NAME_SIZE] = 0;

    instrumentSaveContext.memoryAddressL =
      (uint8_t*)recorder.getAddress(AudioInput::InputChannels::kLeft);
    instrumentSaveContext.memoryAddressR =
      (uint8_t*)recorder.getAddress(AudioInput::InputChannels::kRight);
    instrumentSaveContext.channelDataSizeBytes = recorder.getLength() * sizeof(int16_t);
}

bool RecordingSave::saveStereoDataToWorkspace(int idx,
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

void RecordingSave::saveRecordingFinish()
{
    newFileManager.setStatus(fmSaveRecordedSoundEnd);
    newFileManager.setCurrentOperation(fmNoOperation);
}

float RecordingSave::getProgress()
{
    return std::roundf(
      ((float)sdCardFileSaveContext.savedSoundData / (float)sdCardFileSaveContext.soundLength) *
      100.0f);
}

//=================================================================================================
//=================================================================================================
bool RecordingSave::saveRecordedSound(char* filePath,
                                      char* fileName,
                                      std::optional<int8_t> instrIdx,
                                      const int16_t* leftChannelAddress,
                                      const int16_t* rightChannelAddress,
                                      uint32_t length)
{
    if (newFileManager.getStatus() != fmIdle)
        return false;
    if (newFileManager.getCurrentOperation() != fmNoOperation)
        return false;

    if (!fileSystem->exists(filePath))
    {
        fileSystem->mkdir(0, filePath);
    }

    char fullfilePath[PATCH_SIZE];
    sprintf(fullfilePath, "%s/%s.wav", filePath, fileName);
    if (fileSystem->exists(fullfilePath))
    {
        fileSystem->remove(fullfilePath);
    }

    [[maybe_unused]] auto [isOpen, fileHandle] = fileSystem->openFile(
      fullfilePath, FileSystem::FileOpenMode::kWriteCreateNew, &sdCardFileSaveContext.file);

    if (!isOpen)
    {
        return false;
    }

    fileSystem->seekFile(sizeof(strWavFileHeader), sdCardFileSaveContext.file);

    assert(leftChannelAddress != nullptr);
    assert(rightChannelAddress != nullptr);

    sdCardFileSaveContext.soundAddress.clear();
    sdCardFileSaveContext.soundAddress.push_back(leftChannelAddress);
    if (leftChannelAddress != rightChannelAddress)
    {
        sdCardFileSaveContext.soundAddress.push_back(rightChannelAddress);
    }

    sdCardFileSaveContext.savedSoundData = 0;
    sdCardFileSaveContext.soundLength = length;

    sdCardFileSaveContext.isActive = true;

    bool isImportingRequired = instrIdx.has_value();

    if (!isImportingRequired)
    {
        newFileManager.setStatus(fmSavingRecordedSound);
        newFileManager.setCurrentOperation(fmSaveRecordedSound);
    }
    else
    {
        strncpy(recordedFileName, fileName, SAMPLE_NAME_SIZE);
        recordedFileName[SAMPLE_NAME_SIZE] = 0;
        instrumentSaveContext.importInstrumentIdx = instrIdx.value();
        newFileManager.setStatus(fmSavingImportingRecordedSound);
        newFileManager.setCurrentOperation(fmSaveRecordedSound);
    }

    operationStep = RecordingImportStep::saveRecordedFile;

    return true;
}
