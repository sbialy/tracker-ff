#ifndef FILEMANAGER_SAMPLEIMPORT_H_
#define FILEMANAGER_SAMPLEIMPORT_H_

#include "FileManagerOperation.h"
#include "wavHeaderReader.h"

#include <stdint.h>

class SampleImport : public FileManagerOperation
{
  public:
    virtual void update() override;
    virtual float getProgress() override;

    bool importSamplesToProject(uint8_t fileFrom,
                                uint8_t fileTo,
                                uint8_t instrumentSlot,
                                uint16_t dataFrom,
                                uint16_t dataTo,
                                bool isLofi,
                                SampleChannelMode sampleMode,
                                SampleLoadMode loadMode = SampleLoadMode::kRam);
    bool importSampleToProject(char* filePath,
                               char* fileName,
                               uint8_t instrumentSlot,
                               SampleChannelMode sampleMode);

  private:
    enum SampleImportStep
    {
        initStep,
        moveMemory,
        loadSampleData,
        saveInstrumentStructToWorkspace,
        saveInstrumentSampleToWorkspace,
        finishStep,
    };

    SampleImportStep operationStep;
    SampleLoadMode importLoadMode = SampleLoadMode::kRam;

    uint32_t currentInstrumentFileStructSize;
    int currentInstrument;
    uint8_t sampleLoadState;

    uint8_t importCurrentFileIdx;
    uint8_t importExplorerSlotFrom;
    uint8_t importExplorerSlotTo;

    int8_t importStartSlot;
    int8_t importEndSlot;
    uint8_t firstSlotToMoveInMemory;

    uint16_t importDataFrom;
    uint16_t importDataTo;
    bool importIsLofi;
    SampleChannelMode sampleChannelMode;

    uint32_t oldSamplesSize = 0;

    uint32_t memoryToImportTotal;
    uint32_t memoryToImportDone;

    void importSamplesToWorkspaceInit();
    void importSamplesToWorkspaceFinish();

    void loadImportingSamplesData(FileManagerOperation::SampleLoadData* sampleImportData,
                                  char* sourceFilePath,
                                  char** sourceFileNameList);
    void fillImportStruct(int idx,
                          FileManagerOperation::SampleLoadData* sampleImportData,
                          strWavFileHeader* sampleHead,
                          uint32_t fileWaveDataSize,
                          SampleChannelMode channelMode,
                          uint16_t dataFrom,
                          uint16_t dataTo);

    bool loadSampleDataToMemeory(int idx, FileManagerOperation::SampleLoadData* sampleImportData);
    bool saveStreamedImportSampleToWorkspace(
      int idx, FileManagerOperation::SampleLoadData* sampleImportData);
};

#endif /* FILEMANAGER_SAMPLEIMPORT_H_ */
