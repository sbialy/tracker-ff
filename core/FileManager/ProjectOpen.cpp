#include "ProjectOpen.h"
#ifdef FEATURE_PLUG_HOST
#include "plug_host.h"
#endif

#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"

#if FILEMANAGER_HANDLE_SYNTH == 1
#include "Synth.h"
#endif

#include "log.h"

#ifdef FEATURE_SYNTH_OPEN
#include "open_bankcopy.h"
#endif

#include <cstring>

#ifdef FEATURE_SYNTH_OPEN
#include "mainLoopScratch.h"
#include <new>
#include <type_traits>
#endif

void ProjectOpen::update()
{
    ProjectOpenStep nextStep = operationStep;

    switch (operationStep)
    {
        case ProjectOpenStep::initStep:
        {
            openProjectInit();
            nextStep = ProjectOpenStep::loadProjectFile;
            break;
        }
        case ProjectOpenStep::loadProjectFile:
        {
            loadProjectFileFromSd();
            // clean flag because project is always updated in this moment
            mtProject.values.projectNotSavedFlag = 0;
            //
            memoryToCopyTotal += countProjectPatterns() * sizeof(Sequencer::strPattern);
            nextStep = ProjectOpenStep::saveProjectFile;
            break;
        }
        case ProjectOpenStep::saveProjectFile:
        {
            newFileManager.saveProjectFileToWorkspace();
            nextStep = ProjectOpenStep::copyPatternFiles;
            break;
        }
        case ProjectOpenStep::copyPatternFiles:
        {
            copyPaterns(currentPattern);
            memoryToCopyDone += sizeof(Sequencer::strPattern);
            currentPattern++;
            if (currentPattern > PATTERN_INDEX_MAX)
            {
                nextStep = ProjectOpenStep::copyInstrumentFiles;
            }
            break;
        }
        case ProjectOpenStep::copyInstrumentFiles:
        {
            if (copyInstruments(currentInstrument, copyStep))
            {
                currentInstrument++;
                copyStep = 0;
                if (currentInstrument >= kSampleInstrumentsCount)
                {
                    currentInstrument = 0;
                    nextStep = ProjectOpenStep::copySampleFiles;
                }
                memoryToCopyDone += fileTransfer.getBytesComplited();
                fileTransfer.clearBytesComplited();
            }
            break;
        }
        case ProjectOpenStep::copySampleFiles:
        {
            // old instruments/samples support
            if (copySamples(currentInstrument, copyStep))
            {
                currentInstrument++;
                copyStep = 0;
                if (currentInstrument >= kSampleInstrumentsCount)
                {
                    nextStep = ProjectOpenStep::finishStep;
                }
                memoryToCopyDone += fileTransfer.getBytesComplited();
                fileTransfer.clearBytesComplited();
            }
            break;
        }
        case ProjectOpenStep::finishStep:
        {
            openProjectFinish();
            break;
        }
        default: operationError(); break;
    }

    operationStep = nextStep;
}

float ProjectOpen::getProgress()
{
    float progress = 0;
    float complited = memoryToCopyDone;

    switch (operationStep)
    {
        case initStep:
        case loadProjectFile:
        case saveProjectFile:
        {
            progress = 0.05;
            break;
        }
        case copySampleFiles:
        case copyInstrumentFiles:
        {
            complited += fileTransfer.getBytesComplited();
            [[fallthrough]];
        }
        case copyPatternFiles:
        {
            float memoryProgress =
              memoryToCopyTotal > 0 ? ((float)complited / memoryToCopyTotal * 0.95f) : 0;
            progress = 0.05 + memoryProgress;
            break;
        }
        default: break;
    }

    // LOGF("ProjectOpen - op: %d, pr: %.2f\n", operationStep, progress);

    return progress < 1.0f ? progress : 1.0f;
}

//=================================================================================================
//=================================================================================================
void ProjectOpen::openProjectInit()
{
    currentInstrument = 0;
    currentPattern = 1;
    sampleMemoryCopied = 0;
    copyStep = 0;

    newFileManager.stopAudioEngine();

    listProjectNewInstruments();

    newFileManager.clearWorkspace();
    newFileManager.createWorkspaceDirs();

    memoryToCopyTotal = calcProjectInstrumentsSize();
    memoryToCopyDone = 0;
}

void ProjectOpen::loadProjectFileFromSd()
{
    char projectFilePath[PATCH_SIZE];
#ifdef FEATURE_SYNTH_OPEN
    // The 2.3 KB file image off the stack (mainLoopScratch.h): main loop only, loadFileToMemory and
    // loadProjectFileFormFileStruct reach no other scratch user (the old version recovery has its own buffer).
    // Constructed in place: a file shorter than the struct keeps the member defaults past its end, as the local did.
    static_assert(std::is_trivially_destructible<strProjectFile>::value &&
                    sizeof(strProjectFile) <= kMainLoopScratchSize,
                  "the project file image must fit the main-loop scratch");
    strProjectFile& fileManagerProjectBuffer = *new (g_mainLoopScratch) strProjectFile;
#else
    strProjectFile fileManagerProjectBuffer;
#endif
    volatile int pathSize = sizeof(projectFilePath);

    snprintf(projectFilePath,
             pathSize,
             cProjectFileInProjectsFormat,
             newFileManager.getCurrentProjectPath(),
             newFileManager.getCurrentProjectName());

    uint8_t loadStatus = fileTransfer.loadFileToMemory(
      projectFilePath, (uint8_t*)&fileManagerProjectBuffer, sizeof(strProjectFile), fileWholeOnce);

    if (loadStatus == fileTransferEnd)
    {
        if (!newFileManager.loadProjectFileFormFileStruct(&mtProject, &fileManagerProjectBuffer))
        {
            operationError();
        }
    }
    else if (loadStatus >= fileTransferError)
    {
        operationError();
    }
}

bool ProjectOpen::copyPaterns(int& idx)
{
    char srcPatternFilePath[PATCH_SIZE];
    char destPatternFilePath[PATCH_SIZE];
    volatile int pathSize = sizeof(srcPatternFilePath);

    uint8_t loadStatus = fileTransferIdle;

    while (idx <= PATTERN_INDEX_MAX &&
           (loadStatus == fileTransferIdle || loadStatus == fileTransferFileNoExist))
    {
        snprintf(srcPatternFilePath,
                 pathSize,
                 cProjectsPatternFileFormat,
                 newFileManager.getCurrentProjectPath(),
                 newFileManager.getCurrentProjectName(),
                 idx);
        snprintf(destPatternFilePath, pathSize, cWorkspacePatternFileFormat, idx);

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

bool ProjectOpen::copyInstruments(int idx, uint8_t& copyState)
{
    static char srcInstrFilePath[PATCH_SIZE];
    static char destInstrFilePath[PATCH_SIZE];
    bool isNewPtiInstrument = (instrumentsList != nullptr && instrumentsList[idx] != nullptr);

    if (copyState == 0)
    {
        volatile int pathSize = sizeof(srcInstrFilePath);

        if (isNewPtiInstrument)
        {
            snprintf(srcInstrFilePath,
                     pathSize,
                     cProjectsInstrumentFileFormatNew,
                     newFileManager.getCurrentProjectPath(),
                     newFileManager.getCurrentProjectName(),
                     idx + 1,
                     instrumentsList[idx]);

            if (!fileSystem->exists(srcInstrFilePath))
            {
                snprintf(srcInstrFilePath,
                         pathSize,
                         cProjectsInstrumentFileFormatNewAlt,
                         newFileManager.getCurrentProjectPath(),
                         newFileManager.getCurrentProjectName(),
                         idx + 1,
                         instrumentsList[idx]);
            }

            snprintf(destInstrFilePath, pathSize, cWorkspaceInstrumentFileFormatNew, idx + 1);
        }
        else
        {
            snprintf(srcInstrFilePath,
                     pathSize,
                     cProjectsInstrumentFileFormatOld,
                     newFileManager.getCurrentProjectPath(),
                     newFileManager.getCurrentProjectName(),
                     idx + 1);

            snprintf(destInstrFilePath, pathSize, cWorkspaceInstrumentFileFormatOld, idx + 1);
        }

        copyState = 1;
    }

    uint8_t loadStatus =
      isNewPtiInstrument ? fileTransfer.copyInstrumentHeader(srcInstrFilePath, destInstrFilePath)
                         : fileTransfer.copyFile(srcInstrFilePath, destInstrFilePath);

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

bool ProjectOpen::copySamples(int idx, uint8_t& copyState)
{
    char srcSampleFilePath[PATCH_SIZE];
    char destSampleFilePath[PATCH_SIZE];

    if (copyState == 0)
    {
        volatile int pathSize = sizeof(srcSampleFilePath);

        snprintf(srcSampleFilePath,
                 pathSize,
                 cProjectsSamplesFilesFormat,
                 newFileManager.getCurrentProjectPath(),
                 newFileManager.getCurrentProjectName(),
                 idx + 1);
        snprintf(destSampleFilePath, pathSize, cWorkspaceSamplesFilesFormat, idx + 1);

        copyState = 1;
    }

    uint8_t loadStatus = fileTransfer.copySample(srcSampleFilePath,
                                                 destSampleFilePath,
                                                 0,
                                                 std::numeric_limits<uint16_t>::max(),
                                                 defaultSampleRate);

    if (loadStatus == fileTransferEnd)
    {
        return true;
    }
    else if (loadStatus == fileTransferFileNoExist)
    {
        // if no exist just skip
        return true;
    }
    else if (loadStatus >= fileTransferError)
    {
        // incorrect or damaged wav file = skip
        return true;
    }
    return false;
}

void ProjectOpen::openProjectFinish()
{
    clearInstrumentList();

    //------------ patterns metadata
    char srcFilePath[PATCH_SIZE];
    volatile int pathSize = sizeof(srcFilePath);

    snprintf(srcFilePath,
             pathSize,
             cProjectsPatternsMetadataFileFormat,
             newFileManager.getCurrentProjectPath(),
             newFileManager.getCurrentProjectName());

    newFileManager.getPatternsMetadata()->copyPatternsMetadata(srcFilePath,
                                                               cWorkspacePatternsMetadataFilePath);
    //------------ synth patches

#if FILEMANAGER_HANDLE_SYNTH == 1
    snprintf(srcFilePath,
             pathSize,
             cProjectsPathFormat,
             newFileManager.getCurrentProjectPath(),
             newFileManager.getCurrentProjectName());
#endif

    //-----------------------------------

#ifdef FEATURE_SYNTH_OPEN
    static_assert(FILEMANAGER_HANDLE_SYNTH == 1, "the open synth always has the bank copy");
    // As in stock the result of the bank copy never decides the load: the requested project is loaded with the patch files the
    // Workspace has (open_bankcopy.h retries the copy once). There is no popup for a failed copy: this runs in the file manager step
    // and the load that follows owns the status. The audio engine restarts only at the end of that load.
    polysynth::open::openProjectAfterBankCopy(
      [&]()
      {
#ifdef FEATURE_PLUG_HOST
          // the plugin sidecar follows the project (removed when it has none). A failed copy does not decide the open either:
          // the project loads with empty inserts and its own sidecar is kept (no save writes over it)
          plug::sidecarOpen(srcFilePath, cWorkspacePath);
#endif
          return newFileManager.getSynth()->copyWorkspacePatches(srcFilePath, cWorkspacePath, true);
      },
      []()
      {
          newFileManager.setStatus(fmIdle);
          newFileManager.setCurrentOperation(fmNoOperation);
      },
      []() { return newFileManager.openProjectFromWorkspace(); },
      []() { newFileManager.createNewProjectInWorkspace(); });
#else
#if FILEMANAGER_HANDLE_SYNTH == 1
    newFileManager.getSynth()->copyWorkspacePatches(srcFilePath, cWorkspacePath);
#endif

    newFileManager.setStatus(fmIdle);
    newFileManager.setCurrentOperation(fmNoOperation);

    if (!newFileManager.openProjectFromWorkspace())
    {
        newFileManager.createNewProjectInWorkspace();
        newFileManager.openProjectFromWorkspace();
    }
#endif

    newFileManager.setStatus(fmLoadingProjectFromProjects); // force right status
}

//=================================================================================================
//=================================================================================================
bool ProjectOpen::openProject(char* path, char* name)
{
    if (newFileManager.getStatus() != fmIdle)
        return false;
    if (newFileManager.getCurrentOperation() != fmNoOperation)
        return false;

    if (strlen(name) >= PROJECT_NAME_SIZE)
    {
        return false;
    }

    if (!newFileManager.projectExist(path, name))
    {
        return false;
    }

    LOGEF("open %s/%s", path, name);

    newFileManager.addCurrentProjectToRecentList();

    // load project name here
    strcpy(newFileManager.getCurrentProjectName(), name);
    strcpy(newFileManager.getCurrentProjectPath(), path);
    newFileManager.getCurrentProjectName()[PROJECT_NAME_SIZE - 1] = 0;
    newFileManager.getCurrentProjectPath()[PATCH_SIZE - 1] = 0;

    newFileManager.setStatus(fmLoadingProjectFromProjects);
    newFileManager.setCurrentOperation(fmOpenProject);
    operationStep = ProjectOpenStep::initStep;
    return true;
}

//=================================================================================================
//=================================================================================================

void ProjectOpen::listProjectNewInstruments()
{
    clearInstrumentList();

    instrumentsList = new char*[kSampleInstrumentsCount];
    for (int idx = 0; idx < kSampleInstrumentsCount; idx++)
    {
        instrumentsList[idx] = nullptr;
    }

    instrumentsSizeOut = 0;
    char projectInstrumentPath[PATCH_SIZE];
    volatile int pathSize = sizeof(projectInstrumentPath);

    snprintf(projectInstrumentPath,
             pathSize,
             cProjectsInstrumentsPathFormat,
             newFileManager.getCurrentProjectPath(),
             newFileManager.getCurrentProjectName());

    int instrumentsFound = fileSystem->createInstrumentsList(
      projectInstrumentPath, instrumentsList, kSampleInstrumentsCount, &instrumentsSizeOut);

    if (instrumentsFound == FileSystem::kListError)
    {
        // todo: handle error
        return;
    }
}

void ProjectOpen::clearInstrumentList()
{
    if (instrumentsList != nullptr)
    {
        for (int idx = 0; idx < kSampleInstrumentsCount; idx++)
        {
            if (instrumentsList[idx] != nullptr)
            {
                delete[] instrumentsList[idx];
                instrumentsList[idx] = nullptr;
            }
        }

        delete[] instrumentsList;
        instrumentsList = nullptr;
    }
}

void ProjectOpen::operationError()
{
    clearInstrumentList();
    stopOperationWithError(fmLoadError);
}

//=================================================================================================
//=================================================================================================
uint32_t ProjectOpen::calcProjectInstrumentsSize()
{
    char srcInstrFilePath[PATCH_SIZE];
    volatile int pathSize = sizeof(srcInstrFilePath);
    uint32_t size = 0;

    for (uint8_t idx = 0; idx < kSampleInstrumentsCount; idx++)
    {
        if (instrumentsList != nullptr && instrumentsList[idx] != nullptr)
        {
            size += sizeof(strInstrumentFile);
        }
        else
        {
            snprintf(srcInstrFilePath,
                     pathSize,
                     cProjectsInstrumentFileFormatOld,
                     newFileManager.getCurrentProjectPath(),
                     newFileManager.getCurrentProjectName(),
                     idx + 1);
            size += fileSystem->getFileSize(srcInstrFilePath);

            char srcSampleFilePath[PATCH_SIZE];
            snprintf(srcSampleFilePath,
                     pathSize,
                     cProjectsSamplesFilesFormat,
                     newFileManager.getCurrentProjectPath(),
                     newFileManager.getCurrentProjectName(),
                     idx + 1);

            size += newFileManager.getWaveSizeIfValid(srcSampleFilePath);
        }
    }
    return size;
}

uint32_t ProjectOpen::countProjectPatterns()
{
    uint32_t count = 0;

    for (size_t pattern = 0; pattern < PATTERN_INDEX_MAX; pattern++)
    {
        uint8_t midiPatternBitmask = mtProject.values.allPatternsBitmaskMidi[pattern];

        if (mtProject.values.allPatternsBitmask[pattern] > 0 ||
            (midiPatternBitmask > 0 && midiPatternBitmask < (1 << kAllTracks)))
        {
            count++;
        }
    }

    return count;
}
