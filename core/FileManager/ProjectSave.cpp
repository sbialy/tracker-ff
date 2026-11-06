#include "ProjectSave.h"
#ifdef FEATURE_PLUG_HOST
#include "plug_host.h"
#endif

#include "VoiceStreamManager.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"

#if FILEMANAGER_HANDLE_SYNTH == 1
#include "Synth.h"
#endif

#include "log.h"

#include <algorithm>
#include <cstring>

void ProjectSave::update()
{
    ProjectSaveStep nextStep = operationStep;

    switch (operationStep)
    {
        case ProjectSaveStep::initStep:
        {
            saveProjectInit();
            nextStep = ProjectSaveStep::copyProjectFile;
            break;
        }
        case ProjectSaveStep::copyProjectFile:
        {
            copyProject();
            nextStep = ProjectSaveStep::copyPatternFiles;
            break;
        }
        case ProjectSaveStep::copyPatternFiles:
        {
            if (copyPaterns(currentPattern))
            {
                currentPattern++;
                if (currentPattern > PATTERN_INDEX_MAX)
                {
                    nextStep = ProjectSaveStep::copyInstrumentFiles;
                }
            }
            break;
        }
        case ProjectSaveStep::copyInstrumentFiles:
        {
            if (copyInstruments(currentInstrument, copyStep))
            {
                currentInstrument++;
                copyStep = 0;
                if (currentInstrument >= kSampleInstrumentsCount)
                {
                    nextStep = ProjectSaveStep::finishStep;
                }
                instrumentsSizeCopied += fileTransfer.getBytesComplited();
                fileTransfer.clearBytesComplited();
            }
            break;
        }
        case ProjectSaveStep::finishStep:
        {
            saveProjectFinish();
            break;
        }
        default: operationError(); break;
    }

    operationStep = nextStep;
}

float ProjectSave::getProgress()
{
    float progress = 0;
    switch (operationStep)
    {
        case initStep:
        {
            progress = 0.1;
            break;
        }
        case copyProjectFile:
        {
            progress = 0.1;
            break;
        }
        case copyPatternFiles:
        {
            float patternsProgress = (currentPattern * 0.0004);
            progress = 0.1 + (patternsProgress < 0.1 ? patternsProgress : 0.1);
            break;
        }
        case copyInstrumentFiles:
        {
            float complited = instrumentsSizeCopied + fileTransfer.getBytesComplited();
            float memoryProgress =
              instrumentsSizeTotal > 0 ? ((float)complited / instrumentsSizeTotal * 0.8f) : 0;
            progress = 0.2 + memoryProgress;
            break;
        }
        default: break;
    }

    //     LOGF("Load op: %d, pr: %.2f | Done: %d / %d \n",
    //         operationStep,
    //         progress,
    //		 instrumentsSizeCopied,
    //		 instrumentsSizeTotal);

    return progress < 1.0f ? progress : 1.0f;
}

//=================================================================================================
//=================================================================================================
void ProjectSave::saveProjectInit()
{
    newFileManager.createCurrentProjectFolderTree();

    char projectSavePath[PATCH_SIZE];
    volatile int pathSize = sizeof(projectSavePath);
    snprintf(projectSavePath,
             pathSize,
             cProjectsPatternsPathFormat,
             newFileManager.getCurrentProjectPath(),
             newFileManager.getCurrentProjectName());
    if (!fileSystem->exists(projectSavePath))
    {
        fileSystem->mkdir(0, projectSavePath);
    }

    snprintf(projectSavePath,
             pathSize,
             cProjectsInstrumentsPathFormat,
             newFileManager.getCurrentProjectPath(),
             newFileManager.getCurrentProjectName());
    clearExistingInstrumentList();
    if (!fileSystem->exists(projectSavePath))
    {
        fileSystem->mkdir(0, projectSavePath);
        isIncrementalSave = false;
    }
    else if (isIncrementalSave)
    {
        listProjectExistingInstruments();
    }
    else
    {
        fileSystem->removeOnlyDirFiles(projectSavePath);
    }

    snprintf(projectSavePath,
             pathSize,
             cProjectsSamplesPathFormat,
             newFileManager.getCurrentProjectPath(),
             newFileManager.getCurrentProjectName());
    if (fileSystem->exists(projectSavePath))
    {
        fileSystem->removeDirWithFiles(projectSavePath);
    }

    instrumentsSizeTotal = calcWorkspaceInstrumentsSize();
    instrumentsSizeCopied = 0;
    currentInstrument = 0;
    currentPattern = 1;
    copyStep = 0;
}

void ProjectSave::copyProject()
{
    char destProjectFilePath[PATCH_SIZE];
    volatile int pathSize = sizeof(destProjectFilePath);

    snprintf(destProjectFilePath,
             pathSize,
             cProjectFileInProjectsFormat,
             newFileManager.getCurrentProjectPath(),
             newFileManager.getCurrentProjectName());

    uint8_t loadStatus = fileTransfer.copyFile(cProjectFileNameInWorkspace, destProjectFilePath);

    if (loadStatus != fileTransferEnd)
    {
        operationError();
    }
}

bool ProjectSave::copyPaterns(int& idx)
{
    char srcPatternFilePath[PATCH_SIZE];
    char destPatternFilePath[PATCH_SIZE];
    volatile int pathSize = sizeof(srcPatternFilePath);
    uint8_t loadStatus = fileTransferIdle;

    while (idx <= PATTERN_INDEX_MAX &&
           (loadStatus == fileTransferIdle || loadStatus == fileTransferFileNoExist))
    {

        snprintf(srcPatternFilePath, pathSize, cWorkspacePatternFileFormat, idx);
        snprintf(destPatternFilePath,
                 pathSize,
                 cProjectsPatternFileFormat,
                 newFileManager.getCurrentProjectPath(),
                 newFileManager.getCurrentProjectName(),
                 idx);

        loadStatus = fileTransfer.copyFile(srcPatternFilePath, destPatternFilePath);

        if (loadStatus == fileTransferFileNoExist)
        {
            idx++;
        }
    }

    if (loadStatus == fileTransferEnd)
    {
        return true;
    }
    else if (loadStatus == fileTransferFileNoExist)
    {
        return true;
    }
    else if (loadStatus >= fileTransferError)
    {
        return true;
    }

    return false;
}

bool ProjectSave::copyInstruments(int idx, uint8_t& copyState)
{
    static char srcInstrFilePath[PATCH_SIZE];
    static char destInstrFilePath[PATCH_SIZE];

    if (copyState == 0)
    {
        volatile int pathSize = sizeof(srcInstrFilePath);
        char existingInstrFilePath[PATCH_SIZE] = { 0 };

        if (isIncrementalSave && existingInstrumentsList != nullptr &&
            existingInstrumentsList[idx] != nullptr)
        {
            snprintf(existingInstrFilePath,
                     pathSize,
                     cProjectsInstrumentFileFormatNew,
                     newFileManager.getCurrentProjectPath(),
                     newFileManager.getCurrentProjectName(),
                     idx + 1,
                     existingInstrumentsList[idx]);

            if (!fileSystem->exists(existingInstrFilePath))
            {
                snprintf(existingInstrFilePath,
                         pathSize,
                         cProjectsInstrumentFileFormatNewAlt,
                         newFileManager.getCurrentProjectPath(),
                         newFileManager.getCurrentProjectName(),
                         idx + 1,
                         existingInstrumentsList[idx]);
            }
        }

        if (isIncrementalSave)
        {
            char oldMtiPath[PATCH_SIZE];
            snprintf(oldMtiPath,
                     pathSize,
                     cProjectsInstrumentFileFormatOld,
                     newFileManager.getCurrentProjectPath(),
                     newFileManager.getCurrentProjectName(),
                     idx + 1);
            if (fileSystem->exists(oldMtiPath))
            {
                fileSystem->remove(oldMtiPath);
            }
        }

        if (mtProject.instrument[idx].isActive == 0)
        {
            if (existingInstrFilePath[0] != 0 && fileSystem->exists(existingInstrFilePath))
            {
                fileSystem->remove(existingInstrFilePath);
            }
            return true;
        }

        snprintf(srcInstrFilePath, pathSize, cWorkspaceInstrumentFileFormatNew, idx + 1);
        snprintf(destInstrFilePath,
                 pathSize,
                 cProjectsInstrumentFileFormatNew,
                 newFileManager.getCurrentProjectPath(),
                 newFileManager.getCurrentProjectName(),
                 idx + 1,
                 mtProject.instrument[idx].sample.file_name);

        uint32_t wsFileSize =
          fileSystem->exists(srcInstrFilePath) ? fileSystem->getFileSize(srcInstrFilePath) : 0;

        if (wsFileSize > sizeof(strInstrumentFile))
        {
            if (existingInstrFilePath[0] != 0 &&
                strcmp(existingInstrFilePath, destInstrFilePath) != 0 &&
                fileSystem->exists(existingInstrFilePath))
            {
                fileSystem->remove(existingInstrFilePath);
            }
            copyState = 1;
        }
        else
        {
            bool canUpdateHeaderInPlace = false;
            strInstrumentFile existingHeader;

            if (isIncrementalSave && existingInstrFilePath[0] != 0 &&
                fileSystem->exists(existingInstrFilePath) &&
                fileSystem->getFileSize(existingInstrFilePath) > sizeof(strInstrumentFile))
            {
                memset(&existingHeader, 0, sizeof(existingHeader));
                uint8_t hdrLoadStatus = fileTransfer.loadFileToMemory(existingInstrFilePath,
                                                                      (uint8_t*)&existingHeader,
                                                                      sizeof(strInstrumentFile),
                                                                      fileWholeOnce);
                if (hdrLoadStatus == fileTransferEnd &&
                    existingHeader.instrumentDataAndHeader.instrHeader.id_file[0] == 'T' &&
                    existingHeader.instrumentDataAndHeader.instrHeader.id_file[1] == 'I' &&
                    existingHeader.instrumentDataAndHeader.instrHeader.size ==
                      sizeof(strInstrument))
                {
                    canUpdateHeaderInPlace = true;
                }
            }

            if (canUpdateHeaderInPlace)
            {
                if (strcmp(existingInstrFilePath, destInstrFilePath) != 0)
                {
                    if (fileSystem->exists(destInstrFilePath))
                    {
                        fileSystem->remove(destInstrFilePath);
                    }
                    fileSystem->rename(existingInstrFilePath, destInstrFilePath);
                }

                strInstrumentFile newHeader;
                memset(&newHeader, 0, sizeof(newHeader));
                bool hasWsHeader = false;
                if (wsFileSize == sizeof(strInstrumentFile))
                {
                    if (fileTransfer.loadFileToMemory(srcInstrFilePath,
                                                      (uint8_t*)&newHeader,
                                                      sizeof(strInstrumentFile),
                                                      fileWholeOnce) == fileTransferEnd)
                    {
                        hasWsHeader = true;
                    }
                }
                if (!hasWsHeader)
                {
                    newFileManager.writeInstrumentToFileStruct(&mtProject.instrument[idx],
                                                               &newHeader);
                }

                if (memcmp(&existingHeader, &newHeader, sizeof(strInstrumentFile)) != 0)
                {
                    uint32_t outFileSize = 0;
                    uint8_t saveStatus = fileTransfer.saveInstrumentStructToInstrumentFile(
                      (uint8_t*)&newHeader,
                      sizeof(strInstrumentFile),
                      destInstrFilePath,
                      &outFileSize);
                    if (saveStatus >= fileTransferError)
                    {
                        operationError();
                        return false;
                    }
                }

                instrumentsSizeCopied += sizeof(strInstrumentFile);
                return true;
            }
            else
            {
                if (existingInstrFilePath[0] != 0 && fileSystem->exists(existingInstrFilePath))
                {
                    fileSystem->remove(existingInstrFilePath);
                }
                if (fileSystem->exists(destInstrFilePath))
                {
                    fileSystem->remove(destInstrFilePath);
                }

                strInstrumentFile newHeader;
                newFileManager.writeInstrumentToFileStruct(&mtProject.instrument[idx], &newHeader);

                uint32_t outFileSize = 0;
                uint8_t saveStatus = fileTransfer.saveInstrumentStructToInstrumentFile(
                  (uint8_t*)&newHeader, sizeof(strInstrumentFile), destInstrFilePath, &outFileSize);
                if (saveStatus != fileTransferEnd)
                {
                    operationError();
                    return false;
                }

                if (wsFileSize < sizeof(strInstrumentFile))
                {
                    fileTransfer.saveInstrumentStructToInstrumentFile(
                      (uint8_t*)&newHeader,
                      sizeof(strInstrumentFile),
                      srcInstrFilePath,
                      &outFileSize);
                }

                instrumentsSizeCopied += sizeof(strInstrumentFile);
                copyState = 2;
            }
        }
    }

    if (copyState == 1)
    {
        uint8_t loadStatus = fileTransfer.copyFile(srcInstrFilePath, destInstrFilePath);

        if (loadStatus == fileTransferEnd)
        {
            return true;
        }
        else if (loadStatus == fileTransferFileNoExist)
        {
            return true;
        }
        else if (loadStatus >= fileTransferError)
        {
            operationError();
        }
        return false;
    }

    if (copyState == 2)
    {
        uint32_t dataSizeBytes = mtProject.instrument[idx].sample.length *
                                 (newFileManager.isInstrumentStereo(idx) ? 4 : 2);

        if (mtProject.sampleMemory[idx].isStreamed)
        {
            const char* streamSrcPath = voiceStreamManager.getInstrumentStreamPath(idx);
            if (streamSrcPath && streamSrcPath[0] != 0 &&
                strcmp(streamSrcPath, destInstrFilePath) != 0 &&
                fileSystem->exists(streamSrcPath))
            {
                auto [srcOpen, srcHandle] =
                  fileSystem->openFile(streamSrcPath, FileSystem::FileOpenMode::kRead);
                if (srcOpen)
                {
                    auto [dstOpen, dstHandle] =
                      fileSystem->openFile(destInstrFilePath, FileSystem::FileOpenMode::kWriteOpen);
                    if (dstOpen)
                    {
                        fileSystem->seekFile(mtProject.sampleMemory[idx].fileDataOffset, srcHandle);
                        fileSystem->seekFile(sizeof(strInstrumentFile), dstHandle);

                        uint8_t copyBuf[4096];
                        uint32_t remaining = dataSizeBytes;
                        while (remaining > 0)
                        {
                            uint32_t toRead = std::min<uint32_t>(remaining, sizeof(copyBuf));
                            int32_t bytesRead = fileSystem->readFile(copyBuf, toRead, srcHandle);
                            if (bytesRead <= 0)
                            {
                                break;
                            }
                            int32_t bytesWritten =
                              fileSystem->writeFile(copyBuf, bytesRead, dstHandle);
                            if (bytesWritten != bytesRead)
                            {
                                break;
                            }
                            remaining -= static_cast<uint32_t>(bytesRead);
                        }
                        fileSystem->closeFile(dstHandle);
                    }
                    fileSystem->closeFile(srcHandle);

                    voiceStreamManager.setupInstrumentStream(idx,
                                                             destInstrFilePath,
                                                             sizeof(strInstrumentFile),
                                                             mtProject.instrument[idx].sample.length,
                                                             newFileManager.isInstrumentStereo(idx));
                }
            }
            instrumentsSizeCopied += dataSizeBytes;
            return true;
        }

        uint8_t* dataSourceAddress = (uint8_t*)mtProject.instrument[idx].sample.address;

        uint8_t saveStatus = fileTransfer.saveSampleRawDataToFile(
          dataSourceAddress, dataSizeBytes, destInstrFilePath, sizeof(strInstrumentFile));

        if (saveStatus == fileTransferEnd)
        {
            return true;
        }
        else if (saveStatus >= fileTransferError)
        {
            operationError();
        }
        return false;
    }

    return false;
}

void ProjectSave::saveProjectFinish()
{
    clearExistingInstrumentList();
    //------------ patterns metadata
    char destFilePath[PATCH_SIZE];
    volatile int pathSize = sizeof(destFilePath);

    snprintf(destFilePath,
             pathSize,
             cProjectsPatternsMetadataFileFormat,
             newFileManager.getCurrentProjectPath(),
             newFileManager.getCurrentProjectName());

    newFileManager.getPatternsMetadata()->copyPatternsMetadata(cWorkspacePatternsMetadataFilePath,
                                                               destFilePath);
    //------------ synth patches

#if FILEMANAGER_HANDLE_SYNTH == 1
    snprintf(destFilePath,
             pathSize,
             cProjectsPathFormat,
             newFileManager.getCurrentProjectPath(),
             newFileManager.getCurrentProjectName());

#ifdef FEATURE_PLUG_HOST
    if (!plug::sidecarToProject(cWorkspacePath, destFilePath)) // the plugin sidecar follows the project (removed when it has none)
    {
        mtProject.values.projectNotSavedFlag |= 1; // the project folder lacks the current plugin settings: as the patches below
        operationError();
        return;
    }
#endif
#ifdef FEATURE_SYNTH_OPEN
    if (!newFileManager.getSynth()->copyWorkspacePatches(cWorkspacePath, destFilePath))
    {
        mtProject.values.projectNotSavedFlag |= 1; // the patch files in the project folder are not all current
        operationError();
        return;
    }
#else
    newFileManager.getSynth()->copyWorkspacePatches(cWorkspacePath, destFilePath);
#endif
#endif

    //-----------------------------------

    mtProject.values.projectNotSavedFlag = 0;

    newFileManager.setStatus(fmSaveEnd);
    newFileManager.setCurrentOperation(fmNoOperation);
}

//=================================================================================================
//=================================================================================================
bool ProjectSave::saveProject(char* projectPathToSave, char* projectNameToSave)
{
    if (newFileManager.getStatus() != fmIdle)
    {
        return false;
    }
    if (newFileManager.getCurrentOperation() != fmNoOperation)
    {
        return false;
    }

    bool hadSavedBackingProject =
      (mtProject.values.projectNotSavedFlag & 2) == 0 &&
      newFileManager.getCurrentProjectName()[0] != 0 &&
      newFileManager.projectExist(newFileManager.getCurrentProjectPath(),
                                  newFileManager.getCurrentProjectName());

    // zmien nazwe otwartego projektu w pamieci i w workspace
    if (projectNameToSave != nullptr && projectPathToSave != nullptr)
    {
        if (projectNameToSave[0] == 0)
        {
            return false;
        }

        isIncrementalSave =
          hadSavedBackingProject &&
          strcmp(projectPathToSave, newFileManager.getCurrentProjectPath()) == 0 &&
          strcmp(projectNameToSave, newFileManager.getCurrentProjectName()) == 0;

        newFileManager.addCurrentProjectToRecentList();

        strncpy(newFileManager.getCurrentProjectPath(), projectPathToSave, PATCH_SIZE);
        strncpy(newFileManager.getCurrentProjectName(), projectNameToSave, PROJECT_NAME_SIZE);
        newFileManager.getCurrentProjectPath()[PATCH_SIZE - 1] = 0;
        newFileManager.getCurrentProjectName()[PROJECT_NAME_SIZE - 1] = 0;
    }
    else if (projectNameToSave == nullptr && projectPathToSave == nullptr)
    {
        isIncrementalSave = hadSavedBackingProject;
    }
    else
    {
        return false;
    }

    if (newFileManager.getCurrentProjectName()[0] == 0)
    {
        return false;
    }

    newFileManager.setStatus(fmSavingProjectToProjects);
    operationStep = ProjectSaveStep::initStep;

    // force save project file with new name
    newFileManager.setProjectStructChanged();
    if (newFileManager.saveProjectToWorkspace(false))
    {
        newFileManager.setStatus(fmSavingProjectToProjects);
        return true;
    }

    newFileManager.setStatus(fmSavingProjectToProjects);
    newFileManager.setCurrentOperation(fmCopyWorkspaceToProjects);
    return true;
}

//=================================================================================================
//=================================================================================================
void ProjectSave::listProjectExistingInstruments()
{
    clearExistingInstrumentList();

    existingInstrumentsList = new char*[kSampleInstrumentsCount];
    for (int idx = 0; idx < kSampleInstrumentsCount; idx++)
    {
        existingInstrumentsList[idx] = nullptr;
    }

    existingInstrumentsSizeOut = 0;
    char projectInstrumentPath[PATCH_SIZE];
    volatile int pathSize = sizeof(projectInstrumentPath);

    snprintf(projectInstrumentPath,
             pathSize,
             cProjectsInstrumentsPathFormat,
             newFileManager.getCurrentProjectPath(),
             newFileManager.getCurrentProjectName());

    int instrumentsFound = fileSystem->createInstrumentsList(projectInstrumentPath,
                                                             existingInstrumentsList,
                                                             kSampleInstrumentsCount,
                                                             &existingInstrumentsSizeOut);

    if (instrumentsFound == FileSystem::kListError)
    {
        clearExistingInstrumentList();
    }
}

void ProjectSave::clearExistingInstrumentList()
{
    if (existingInstrumentsList != nullptr)
    {
        for (int idx = 0; idx < kSampleInstrumentsCount; idx++)
        {
            if (existingInstrumentsList[idx] != nullptr)
            {
                delete[] existingInstrumentsList[idx];
                existingInstrumentsList[idx] = nullptr;
            }
        }

        delete[] existingInstrumentsList;
        existingInstrumentsList = nullptr;
    }
}

void ProjectSave::operationError()
{
    clearExistingInstrumentList();
    stopOperationWithError(fmSaveError);
}

uint32_t ProjectSave::calcWorkspaceInstrumentsSize()
{
    char currentPatch[PATCH_SIZE];
    int32_t size = 0;

    for (uint8_t i = 0; i < kSampleInstrumentsCount; i++)
    {
        if (mtProject.instrument[i].isActive == 0)
        {
            continue;
        }

        snprintf(currentPatch, PATCH_SIZE, cWorkspaceInstrumentFileFormatNew, i + 1);
        uint32_t wsSize =
          fileSystem->exists(currentPatch) ? fileSystem->getFileSize(currentPatch) : 0;

        if (wsSize > sizeof(strInstrumentFile))
        {
            size += wsSize;
        }
        else if (isIncrementalSave && existingInstrumentsList != nullptr &&
                 existingInstrumentsList[i] != nullptr)
        {
            size += sizeof(strInstrumentFile);
        }
        else
        {
            size += sizeof(strInstrumentFile) + newFileManager.getSampleMemorySize(i, i);
        }
    }

    return size > 0 ? size : 0;
}

//=================================================================================================
//=================================================================================================
