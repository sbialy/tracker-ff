#include "Platform.h"
#include "fileManagerDefs.h"
#include "log.h"
#include "mtAudioEngine.h"
#include "mtAudioExport.h"
#include "mtConfig.h"
#include "mtStructs.h"

#include "mtSequencer.h"

#include "fileManager.h"
#include "fileTransfer.h"
#include "instrumentFileValidation.h"

#include "projectEditor/projectEditor.h"

__NOINIT(EXTERNAL_RAM) uint8_t externalRamFileManagerSrcBuff[READ_WRITE_BUFOR_SIZE]; // 32768
__NOINIT(EXTERNAL_RAM) uint8_t externalRamFileManagerBuffL[READ_WRITE_BUFOR_SIZE];   // 32768
//__NOINIT(EXTERNAL_RAM) uint8_t externalRamFileManagerBuffR[READ_WRITE_BUFOR_SIZE];   // 32768

__NOINIT(EXTERNAL_RAM) char* cFileManager::projectsList[cFileManager::list_length_max];
__NOINIT(EXTERNAL_RAM) char* cFileManager::firmwaresList[cFileManager::list_length_max];
__NOINIT(EXTERNAL_RAM) char* cFileManager::modsList[cFileManager::list_length_max];
__NOINIT(EXTERNAL_RAM) char* cFileManager::explorerList[cFileManager::list_length_max];
__NOINIT(EXTERNAL_RAM) uint32_t cFileManager::currentFolderMemoryFileUsage[cFileManager::list_length_max];
__NOINIT(EXTERNAL_RAM) cFileManager::strImportFileDataCommon cFileManager::impFileData;

cFileManager newFileManager;

FileSystem::FileHandle wavfile = nullptr;

elapsedMillis autoSaveTimer;

#ifdef FILEMANAGER_DEBUGLOG
static char errorText[100];
#endif
//==========================================================================================================
//==========================================================================================================
//==========================================================================================================

void cFileManager::init(FileSystem* fileSystem, Synth* synth, bool stereoSampleMode)
{
    this->fileSystem = fileSystem;
    this->synth = synth;

    memset(projectsList, 0, sizeof(projectsList));
    memset(firmwaresList, 0, sizeof(firmwaresList));
    memset(modsList, 0, sizeof(modsList));
    memset(explorerList, 0, sizeof(explorerList));
    memset(currentFolderMemoryFileUsage, 0, sizeof(currentFolderMemoryFileUsage));
    memset(&impFileData, 0, sizeof(impFileData));

    fileTransfer.init(fileSystem);
    patternsMetadata.init(fileSystem);
    FileManagerOperation::init(fileSystem);

    this->stereoSampleMode = stereoSampleMode;
}

void cFileManager::update()
{
    if (currentOperation == fmNoOperation && !sequencer.isRenderMode())
    {
        autoSaveProjectToWorkspace();

        return;
    }

    switch (currentOperation)
    {
        case fmLoadWorkspaceProject: workspaceProjectLoad.update(); break;
        case fmSaveWorkspaceProject: workspaceProjectSave.update(); break;
        case fmOpenProject: projectOpen.update(); break;
        case fmCopyWorkspaceToProjects: projectSave.update(); break;

        case fmBrowseSamples: updateBrowseSamples(); break;
        case fmBrowseFirmwares: updateBrowseFirmwares(); break;
        case fmBrowseGames: updateBrowseGames(); break;

        case fmImportSamplesToWorkspace: sampleImport.update(); break;
        case fmCopyInstrumentsInWorkspace: instrumentCopy.update(); break;
        case fmPreviewSamplesFromSD: break;
        case fmDeleteInstruments: instrumentDelete.update(); break;

        case fmLoadWorkspacePattern: updateLoadWorkspacePattern(); break;

        case fmExportSound: updateExportSound(); break;
        case fmExportProjectSamples: updateExportProjectSamples(); break;
        case fmSaveRecordedSound: recordingSave.update(); break;

        case fmImportSampleFromSampleEditor: sampleMemorySave.update(); break;

        case fmImportModFile: updateImportModFile(); break;
        case fmExportModFile: updateExportModFile(); break;

        case fmReloadSamples: sampleMemoryReload.update(); break;

        case fmDrawSampleWaveform: updateDrawSampleWaveform(); break;

        default: break;
    }
}

uint8_t cFileManager::getProgress()
{
    if (status >= fmLoadEnd)
    {
        return 100;
    }

    switch (status)
    {
        case fmSavingProjectToProjects:
        {
            if (currentOperation == fmSaveWorkspaceProject)
            {
                return 10;
            }
            else if (currentOperation == fmCopyWorkspaceToProjects)
            {
                return 10 + projectSave.getProgress() * 90;
            }
            return 100;
        }
        case fmLoadingProjectfromWorkspace: return workspaceProjectLoad.getProgress() * 100;
        case fmLoadingProjectFromProjects:
        {
            if (currentOperation == fmOpenProject)
            {
                return projectOpen.getProgress() * 50;
            }
            else if (currentOperation == fmLoadWorkspaceProject)
            {
                return 50 + workspaceProjectLoad.getProgress() * 50;
            }
            return 100;
        }
        case fmImportingSamplesToWorkspace: return sampleImport.getProgress() * 100;
        case fmCopyingInstrumentsInWorkspace: return instrumentCopy.getProgress() * 100;
        case fmExportingProjectSamples:
            actualMemoryTransfered = getTotalSamplesMemoryExported();
            break;
        case fmSavingImportingRecordedSound:
        case fmSavingRecordedSound: return recordingSave.getProgress();
        default: return 100;
    }

    return (actualMemoryTransfered < totalMemoryToTranfser)
             ? (actualMemoryTransfered * 100) / totalMemoryToTranfser
             : 100;
}

//====================================================================================
//====================================================================================
//====================================================================================

// forceSaveAll - jak narazie nigdzie nie uzywane force=true
bool cFileManager::saveProjectToWorkspace(bool forceSaveAll)
{
    if (workspaceSaveSuspended)
    {
        // The change flags stay set, so the save happens after the suspension.
        return false;
    }

    changesFlags.save_asap = 0;
    return workspaceProjectSave.saveProjectToWorkspace(forceSaveAll);
}

bool cFileManager::flushWorkspaceSave()
{
    bool autosaving =
      currentOperation == fmSaveWorkspaceProject && status == fmSavingProjectToWorkspace;

    if (currentOperation != fmNoOperation && !autosaving)
    {
        return false;
    }

    // An autosave already under way may have passed the newest changes, so
    // it is followed by one more save.
    for (int pass = 0; pass < 2; pass++)
    {
        if (currentOperation == fmNoOperation && !saveProjectToWorkspace())
        {
            break;
        }
        while (currentOperation == fmSaveWorkspaceProject)
        {
            update();
        }
    }

    return true;
}

void cFileManager::setWorkspaceSaveSuspended(bool suspended)
{
    workspaceSaveSuspended = suspended;
}

bool cFileManager::openProjectFromWorkspace()
{
    stopAutosave();

    return workspaceProjectLoad.openProjectFromWorkspace();
}

bool cFileManager::openProject(char* path, char* name)
{
    stopAutosave();

    return projectOpen.openProject(path, name);
}

bool cFileManager::importSamplesToProject(uint8_t fileFrom,
                                          uint8_t fileTo,
                                          uint8_t instrumentSlot,
                                          uint16_t dataFrom,
                                          uint16_t dataTo,
                                          bool isLofi,
                                          FileManagerOperation::SampleLoadMode loadMode)
{
    stopAutosave();

    return sampleImport.importSamplesToProject(
      fileFrom,
      fileTo,
      instrumentSlot,
      dataFrom,
      dataTo,
      isLofi,
      stereoSampleMode ? FileManagerOperation::kLeftAndRight : FileManagerOperation::kLeft,
      loadMode);
}

bool cFileManager::importSampleToProject(char* filePath, char* fileName, uint8_t instrumentSlot)
{
    stopAutosave();

    return sampleImport.importSampleToProject(filePath,
                                              fileName,
                                              instrumentSlot,
                                              stereoSampleMode ? FileManagerOperation::kLeftAndRight
                                                               : FileManagerOperation::kLeft);
}

bool cFileManager::importModAfterLoadNewProject(char* path, char* name)
{
    // pobranie nazwy otwieranego projektu tu
    strcpy(impFileData.path, path);
    strcat(impFileData.path, "/");
    strcat(impFileData.path, name);

    strcpy(impFileData.filename, name);

    char modName[PROJECT_NAME_SIZE + 1]{ 0 };

    for (uint8_t a = 0; a < sizeof(modName); a++)
    {
        if (impFileData.filename[a] != '.')
        {
            modName[a] = impFileData.filename[a];
        }
        else
        {
            break;
        }
    }

    // PROJECT_NAME_SIZE
    strncpy(currentProjectName, modName, PROJECT_NAME_SIZE);
    currentProjectName[PROJECT_NAME_SIZE - 1] = 0;

    importModFileAfterNewProject = 1;

    return true;
}

bool cFileManager::saveRecordedSound(char* dirPath,
                                     char* fileName,
                                     std::optional<int8_t> importSlot,
                                     int16_t* leftChannelAddress,
                                     int16_t* rightChannelAddress,
                                     uint32_t length)
{
    stopAutosave();

    return recordingSave.saveRecordedSound(
      dirPath, fileName, importSlot, leftChannelAddress, rightChannelAddress, length);
}

bool cFileManager::saveProject(char* projectPathToSave, char* projectNameToSave)
{
    stopAutosave();

    return projectSave.saveProject(projectPathToSave, projectNameToSave);
}

// narazie kopiowanie dziala tylko na 1 instrument
bool cFileManager::copyInstrumentsInWorkspace(uint8_t copyInstrSrc,
                                              uint8_t copyInstrCount,
                                              uint8_t copyInstrDest)
{
    stopAutosave();

    return instrumentCopy.copyInstrumentsInWorkspace(copyInstrSrc, copyInstrCount, copyInstrDest);
}

bool cFileManager::deleteInstruments(uint8_t instrumentSlotFrom, uint8_t instrumentSlotTo)
{
    stopAutosave();

    return instrumentDelete.deleteInstruments(instrumentSlotFrom, instrumentSlotTo);
}

bool cFileManager::loadWorkspacePattern(uint8_t index)
{
    assert(index > 0);

    stopAutosave(); // TODO: sprawdzic czy nie opoznia ladowania patternu

    // to wyjatek który moze byc wywolywany podczas exportu songa
    if (status != fmIdle && status != fmExportingSoundSong && status != fmExportingSoundSongStems &&
        status != fmSavingProjectToWorkspace)
        return false;
    if (currentOperation != fmNoOperation && currentOperation != fmExportSound &&
        currentOperation != fmSaveWorkspaceProject)
        return false;

    currentPattern = index;

    if (status != fmExportingSoundSong && status != fmExportingSoundSongStems)
    {
        status = fmLoadingPatternFromWorkspace;
    }

    currentOperationStep = 0;
    currentOperation = fmLoadWorkspacePattern;
    return true;
}

bool cFileManager::deleteProjectOrFolder(char* projectPath)
{
    if (status != fmIdle && status != fmSavingProjectToWorkspace)
        return false;
    if (currentOperation != fmNoOperation && currentOperation != fmSaveWorkspaceProject)
        return false;

    fileSystem->removeDirWithFiles(projectPath);

    return true;
}

bool cFileManager::deleteMod(char* modPathWithName)
{
    if (status != fmIdle && status != fmSavingProjectToWorkspace)
        return false;
    if (currentOperation != fmNoOperation && currentOperation != fmSaveWorkspaceProject)
        return false;

    fileSystem->remove(modPathWithName);

    return true;
}

bool cFileManager::importSampleFromSampleEditor(int16_t* memoryAddressL,
                                                int16_t* memoryAddressR,
                                                uint32_t samplesCount,
                                                uint8_t instrumentIdx)
{
    stopAutosave();

    return sampleMemorySave.saveSampleFromSampleEditor(
      memoryAddressL, memoryAddressR, samplesCount, instrumentIdx);
}

bool cFileManager::reloadSamplesFromWorkspace()
{
    stopAutosave();

    return sampleMemoryReload.reload();
}

//=================================================================================================
//=================================================================================================
void cFileManager::autoSaveProjectToWorkspace()
{
    if (autoSaveTimer > 10000 || changesFlags.save_asap)
    {
        autoSaveTimer = 0;
        saveProjectToWorkspace();
    }
}

void cFileManager::stopAutosave()
{
    workspaceProjectSave.stop();
}

void cFileManager::moveToNextOperationStep()
{
    currentOperationStep++;
}

void cFileManager::moveToPrevOperationStep()
{
    currentOperationStep--;
}

void cFileManager::stopOperationWithError(uint8_t error)
{
#ifdef FILEMANAGER_DEBUGLOG
    LOGF("File manager error (%d)(%d)(%d)", currentOperation, currentOperationStep, error);

    debugLog.setMaxLineCount(3);
    snprintf(errorText,
             100,
             "File manager error (%d)(%d)(%d)",
             currentOperation,
             currentOperationStep,
             error);
    debugLog.addLine(errorText);
    debugLog.forceRefresh();
#endif

    currentInstrument = 0;
    currentPattern = 1;

    startAudioEngine();

    if (error < fmError)
    {
        error = fmError;
    }

    status = error;
    currentOperationStep = 0;
    currentOperation = fmNoOperation;
}

//-----------------------------------------------------------------------------------------------------
//-------------------------------------   FLAGI ZMIAN
//-----------------------------------------------
//-----------------------------------------------------------------------------------------------------

void cFileManager::clearChangeFlags()
{
    changesFlags.project = 0;
    for (uint16_t i = 0; i < PATTERN_INDEX_MAX + 1; i++)
    {
        changesFlags.pattern[i] = 0;
    }
    for (uint8_t i = 0; i < kSampleInstrumentsCount; i++)
    {
        changesFlags.instrument[i] = 0;
        changesFlags.sample[i] = 0;
    }
}

bool cFileManager::isProjectChanged()
{
    if (changesFlags.project)
    {
        return true;
    }
    for (uint16_t i = 0; i < PATTERN_INDEX_MAX + 1; i++)
    {
        if (changesFlags.pattern[i])
        {
            return true;
        }
    }
    for (uint8_t i = 0; i < kSampleInstrumentsCount; i++)
    {
        if (changesFlags.instrument[i])
        {
            return true;
        }
    }
    for (uint8_t i = 0; i < kSampleInstrumentsCount; i++)
    {
        if (changesFlags.sample[i])
        {
            return true;
        }
    }

    return false;
}

void cFileManager::setAllChangeFlags()
{
    changesFlags.project = 1;
    for (uint16_t i = 0; i < PATTERN_INDEX_MAX + 1; i++)
    {
        changesFlags.pattern[i] = 1;
    }
    for (uint8_t i = 0; i < kSampleInstrumentsCount; i++)
    {
        changesFlags.instrument[i] = 1;
    }
}

void cFileManager::autosaveProjectStruct_ASAP()
{
    changesFlags.save_asap = 1;
}

void cFileManager::setActualPatternStructChanged()
{
    setPatternStructChanged(mtProject.values.actualPattern);
}

bool cFileManager::isPatternStructChanged(uint8_t pattern)
{
    return changesFlags.pattern[pattern] > 0;
}

void cFileManager::setPatternStructChanged(uint8_t pattern)
{
    changesFlags.pattern[pattern] = 1;
}

void cFileManager::setInstrumentStructChanged(uint8_t instrument)
{
    changesFlags.instrument[instrument] = 1;
}

bool cFileManager::isInstrumentStructChanged(uint8_t instrument)
{
    return changesFlags.instrument[instrument] > 0;
}

void cFileManager::setInstrumentSampleChanged(uint8_t instrument)
{
    changesFlags.sample[instrument] = 1;
}

bool cFileManager::isInstrumentSampleChanged(uint8_t instrument)
{
    return changesFlags.sample[instrument] > 0;
}

bool cFileManager::isProjectStructChanged()
{
    return changesFlags.project > 0;
}

void cFileManager::setProjectStructChanged()
{
    changesFlags.project = 1;
}

void cFileManager::clearProjectChangedFlag()
{
    changesFlags.project = 0;
}

void cFileManager::clearPatternChanged(uint8_t pattern)
{
    changesFlags.pattern[pattern] = 0;
}

void cFileManager::clearInstrumentChanged(uint8_t instrument)
{
    changesFlags.instrument[instrument] = 0;
}

void cFileManager::clearSampleChanged(uint8_t instrument)
{
    changesFlags.sample[instrument] = 0;
}

//-----------------------------------------------------------------------------------------------------
//------------------------------------- -----------------------------------------------
//-----------------------------------------------------------------------------------------------------

void cFileManager::stopAudioEngine()
{
    engine.muteHeadphones();

    engine.cutAllVolumeEnvelopes();

    engine.detachUpdateTimer();
    // muteAudioOutByKey();

    // engine.endAllFx();
    engine.clearDelay();
    engine.clearReverb();

    AudioNoInterrupts();
}

void cFileManager::startAudioEngine()
{

    AudioInterrupts();

    engine.attachUpdateTimer();

    // unmuteAudioOutByKey();

    engine.enableLineInImpact();

    engine.endAllFx();
    engine.clearDelay();
    engine.clearReverb();

    engine.unmuteHeadphones();
}

//-----------------------------------------------------------------------------------------------------
//------------------------------------- -----------------------------------------------
//-----------------------------------------------------------------------------------------------------
bool cFileManager::isPatternLoading()
{
    return (status == fmLoadingPatternFromWorkspace) ||
           (currentOperation == fmLoadWorkspacePattern);
}

bool cFileManager::projectExist(char* path, char* name)
{
    char projFilePath[PATCH_SIZE];
    snprintf(projFilePath, PATCH_SIZE, cProjectFileInProjectsFormat, path, name);

    if (fileSystem->exists(projFilePath))
    {
        return true;
    }

    return false;
}

void cFileManager::loadProjectValues()
{
    projectEditor.loadProjectValues();
}

bool cFileManager::isInstrumentStereo(int idx)
{
    assert(idx >= 0 && idx < kSampleInstrumentsCount);

    if (mtProject.instrument[idx].sample.length == 0)
    {
        return false;
    }

    if (mtProject.sampleMemory[idx].isStreamed)
    {
        return mtProject.sampleMemory[idx].isStereo;
    }

    uint32_t ramFrames = mtProject.sampleMemory[idx].ramFrames > 0
                           ? mtProject.sampleMemory[idx].ramFrames
                           : mtProject.instrument[idx].sample.length;

    if (mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kRight] ==
          mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] + ramFrames ||
        mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kRight] ==
          mtProject.sampleMemory[idx].address[strSampleMemory::Channel::kLeft] +
            mtProject.instrument[idx].sample.length)
    {
        return true;
    }
    return false;
}

bool cFileManager::isWorkspaceInstrumentPtiFileValid(int idx)
{
    char instrFilePath[PATCH_SIZE];
    volatile int pathSize = sizeof(instrFilePath);
    snprintf(instrFilePath, pathSize, cWorkspaceInstrumentFileFormatNew, idx + 1);

    bool stereo = false;

    if (isPtiFileValid(
          instrFilePath, stereo, sizeof(strInstrumentFile), mtProject.instrument[idx].sample.length))
    {
        return true;
    }

    if (!isInstrumentSampleChanged(idx))
    {
        if (status == fmSavingProjectToProjects && fileSystem->exists(instrFilePath))
        {
            return true;
        }

        uint32_t backingOffset = sizeof(strInstrumentFile);
        if (resolveProjectInstrumentPtiPath(idx, instrFilePath, pathSize) &&
            getPtiFileDataOffset(instrFilePath, backingOffset))
        {
            return isPtiFileValid(
              instrFilePath, stereo, backingOffset, mtProject.instrument[idx].sample.length);
        }
    }

    return false;
}

bool cFileManager::getPtiFileDataOffset(const char* path, uint32_t& dataOffsetOut)
{
    if (!fileSystem->exists(path))
    {
        return false;
    }

    auto [isOpen, fileHandle] = fileSystem->openFile(path, FileSystem::FileOpenMode::kRead);
    if (!isOpen)
    {
        return false;
    }

    strInstrumentFile instrumentFile;
    memset(&instrumentFile, 0, sizeof(instrumentFile));
    uint32_t fileSize = fileSystem->getFileSize(fileHandle);
    int32_t bytesRead =
      fileSystem->readFile(&instrumentFile, sizeof(instrumentFile), fileHandle);
    fileSystem->closeFile(fileHandle);

    if (bytesRead < 0)
    {
        return false;
    }

    InstrumentFileValidation::Result validation;
    if (!InstrumentFileValidation::validate((const uint8_t*)&instrumentFile,
                                            bytesRead,
                                            fileSize,
                                            sizeof(strInstrument),
                                            INSTRUMENT_FILE_VERSION,
                                            validation))
    {
        return false;
    }

    dataOffsetOut = validation.dataOffset;
    return true;
}

bool cFileManager::resolveProjectInstrumentPtiPath(int idx, char* outPath, size_t maxLen)
{
    if (idx < 0 || idx >= kSampleInstrumentsCount)
    {
        return false;
    }
    if (currentProjectName[0] == 0 || currentProjectPatch[0] == 0 ||
        (mtProject.values.projectNotSavedFlag & 2) != 0)
    {
        return false;
    }

    if (mtProject.instrument[idx].sample.file_name[0] != 0)
    {
        snprintf(outPath,
                 maxLen,
                 cProjectsInstrumentFileFormatNew,
                 currentProjectPatch,
                 currentProjectName,
                 idx + 1,
                 mtProject.instrument[idx].sample.file_name);
        if (fileSystem->exists(outPath))
        {
            return true;
        }

        snprintf(outPath,
                 maxLen,
                 cProjectsInstrumentFileFormatNewAlt,
                 currentProjectPatch,
                 currentProjectName,
                 idx + 1,
                 mtProject.instrument[idx].sample.file_name);
        if (fileSystem->exists(outPath))
        {
            return true;
        }
    }

    char projectInstrumentPath[PATCH_SIZE];
    snprintf(projectInstrumentPath,
             sizeof(projectInstrumentPath),
             cProjectsInstrumentsPathFormat,
             currentProjectPatch,
             currentProjectName);

    if (!fileSystem->exists(projectInstrumentPath))
    {
        return false;
    }

    char* list[kSampleInstrumentsCount] = { nullptr };
    uint32_t sizeOut = 0;
    int found = fileSystem->createInstrumentsList(
      projectInstrumentPath, list, kSampleInstrumentsCount, &sizeOut);

    bool resolved = false;
    if (found != FileSystem::kListError && list[idx] != nullptr)
    {
        snprintf(outPath,
                 maxLen,
                 cProjectsInstrumentFileFormatNew,
                 currentProjectPatch,
                 currentProjectName,
                 idx + 1,
                 list[idx]);
        if (fileSystem->exists(outPath))
        {
            resolved = true;
        }
        else
        {
            snprintf(outPath,
                     maxLen,
                     cProjectsInstrumentFileFormatNewAlt,
                     currentProjectPatch,
                     currentProjectName,
                     idx + 1,
                     list[idx]);
            if (fileSystem->exists(outPath))
            {
                resolved = true;
            }
        }
    }

    for (int i = 0; i < kSampleInstrumentsCount; i++)
    {
        if (list[i] != nullptr)
        {
            delete[] list[i];
        }
    }

    return resolved;
}

// check if pti file contains audio data
bool cFileManager::isPtiFileValid(char* path,
                                  bool& stereo,
                                  uint32_t instrumentFileDataOffset,
                                  uint32_t expectedSamplesCountPerChannel)
{
    if (!fileSystem->exists(path))
    {
        return false;
    }

    uint32_t instrumentFileSize = fileSystem->getFileSize(path);

    // check if contains audio data
    if (instrumentFileSize <= instrumentFileDataOffset)
    {
        return false;
    }

    uint32_t instrumentSampleDataSize = instrumentFileSize - instrumentFileDataOffset;

    if (instrumentSampleDataSize == expectedSamplesCountPerChannel * 4 &&
        expectedSamplesCountPerChannel > 0)
    {
        stereo = true;
    }
    else
    {
        stereo = false;
    }

    return true;
}

void cFileManager::setCrcCalculator(CrcCalculator* crcCalculator)
{
    cFileManager::crcCalculator = crcCalculator;
}

//-----------------------------------------------------------------------------------------------------
//-------------------------------------------     ERRORS ------------------------------------------
//-----------------------------------------------------------------------------------------------------

void cFileManager::report(const char* text, const char* text2)
{
#ifdef FILEMANAGER_DEBUGLOG

    debugLog.setMaxLineCount(1);
    snprintf(errorText, 100, "File manager: %s %s", text, text2);
    debugLog.addLine(errorText);
    debugLog.forceRefresh();
    // Serial.printf("%s\r\n", errorText);

#endif
}

void cFileManager::report(const char* text, [[maybe_unused]] uint8_t value)
{
#ifdef FILEMANAGER_DEBUGLOG
    debugLog.setMaxLineCount(1);
    if (value > 0)
        snprintf(errorText, 100, "File manager: %s (%d)", text, value);
    else
        snprintf(errorText, 100, "File manager: %s", text);
    debugLog.addLine(errorText);
    debugLog.forceRefresh();
    // Serial.printf("%s\r\n", errorText);
#endif
}
