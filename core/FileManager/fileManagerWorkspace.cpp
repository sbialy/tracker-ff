#include "fileManagerDefs.h"
#ifdef FEATURE_PLUG_HOST
#include "plug_host.h"
#endif
#include "mtStructs.h"

#include "mtSequencer.h"

#include "fileManager.h"

#include <cstring>

bool cFileManager::createNewProjectInWorkspace()
{
    addCurrentProjectToRecentList();

    clearWorkspace();
    createWorkspaceDirs();
#ifdef FEATURE_PLUG_HOST
    plug::projectLoaded(cWorkspacePath, true); // no sidecar in a new project: both insert slots empty
#endif

    report(" crating new project ");

    strcpy(currentProjectName, cNewProjectName);
    currentProjectPatch[0] = 0;

    getDefaultProject(&mtProject);
    mtProject.values.projectNotSavedFlag = 2; // nie zapiany do folderu Projects
    mtProject.values.actualPattern = 1;

    // saveActualPattern(cWorkspacePatternsPath, mtProject.values.actualPattern);
    sequencer.clearAllPatternSlots(); // czyszczenie sekwencji przed pierwszym zapisem
    changesFlags.pattern[mtProject.values.actualPattern] = 1;
    savePatternToWorkspace();
    sequencer.saveToFileDone(); // xxx ?

    changesFlags.project = 1;
    saveProjectFileToWorkspace();
    currentOperationStep = 0;

    return true;
}

bool cFileManager::createWorkspaceDirs()
{
    if (!fileSystem->exists(cWorkspacePath))
    {
        fileSystem->mkdir(1, cWorkspacePath);
    }

    if (!fileSystem->exists(cWorkspaceInstrumentsPath))
    {
        fileSystem->mkdir(1, cWorkspaceInstrumentsPath);
    }

    if (!fileSystem->exists(cWorkspacePatternsPath))
    {
        fileSystem->mkdir(1, cWorkspacePatternsPath);
    }

    if (!fileSystem->exists(cWorkspaceSamplesPath))
    {
        fileSystem->mkdir(1, cWorkspaceSamplesPath);
    }

    return true;
}

void cFileManager::clearWorkspace()
{
    fileSystem->removeDirWithFiles(cWorkspacePath);
}
