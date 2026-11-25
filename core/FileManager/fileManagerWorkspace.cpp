#include "fileManagerDefs.h"
#include "fx34_io.h"
#include "mtFollowChord.h"
#include "ff_hooks.h"
#include "mtStructs.h"

#include "mtSequencer.h"

#include "fileManager.h"

#include <cstring>
#ifdef FEATURE_PARTS
#include "parts.h"
#endif

bool cFileManager::createNewProjectInWorkspace()
{
    addCurrentProjectToRecentList();

    clearWorkspace();
    createWorkspaceDirs();
    fx34io::projectClosing(); // a new project has the 4 FX switch off
    ffHookProjectClosing(); // and each patch as it always was (no macro depth mode), before the new project's patches are made
    followchord::workspaceLoaded(true); // a new project: no track follows, no sidecar
#ifdef FEATURE_PARTS
    parts::projectLoaded(cWorkspacePath, true); // a new project (and a .it / .mod import, which starts one) has no Parts
#endif
    ffHookProjectLoaded(cWorkspacePath, true); // no sidecar in a new project: both insert slots empty, every project.mtff setting at its default

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
    ffHookNewProjectSaved(cWorkspacePath); // project.mtff of the new project (Strum ramp 100), stamped with the project.mt just written
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
    if (fileSystem->exists(cWorkspacePath)) // backupDiscardedWorkspace() may have renamed it away
    {
        fileSystem->removeDirWithFiles(cWorkspacePath);
    }
}

// "Don't save" on the save-last-project window: the Workspace holds the work the user is about to discard. The pending autosave is
// finished first, then the folder is renamed to Workspace.prev (one generation, the one kept before is deleted): one rename, no copy.
// The project that opens next builds a new Workspace. Restore by hand: power off, rename Workspace.prev to Workspace on the card.
void cFileManager::backupDiscardedWorkspace()
{
    flushWorkspaceSave();
    if (!fileSystem->exists(cWorkspacePath))
    {
        return;
    }
    if (fileSystem->exists(cWorkspacePrevPath))
    {
        fileSystem->removeDirWithFiles(cWorkspacePrevPath);
    }
    fileSystem->rename(cWorkspacePath, cWorkspacePrevPath); // on failure the Workspace stays and is deleted as before
}
