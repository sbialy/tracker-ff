// MOD import, IT import and IT export as a card module (FEATURE_MODIT_MODULE, synth-open/modit/, ref/modit-as-plugin.md): the image side.
// fileManagerImportMod.cpp, fileManagerImportIt.cpp and fileManagerExportToMod.cpp are not part of this image. /Plugins/Ext/modit.plg
// holds them, compiled as members of cFileManager against this exact image, and loads into the extension slot on the first use
// (synth-open/ext_module.cpp, ext_modit_check). Its one hook, ext_hooks.modit, runs one step of the state machine (moditDispatch,
// synth-open/modit/modit_dispatch.cpp). What stays here: the five entry points of the three files as forwarders, the start of the
// export (a few lines of state, the same as the file's own), and two pins.
#ifdef FEATURE_MODIT_MODULE

#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"
#include "ext_hooks.h"

#include <cstring>

namespace
{
inline uint32_t call(uint32_t op) { return ext_hooks.modit ? ext_hooks.modit(op) : EXT_MODIT_NONE; }
}

// The module went away while a step was due (power off clears the hooks): the operation ends with the error status it ends with
// when a file is bad.
void cFileManager::updateImportModFile()
{
    if (call(EXT_MODIT_UPDATE_IMPORT) == EXT_MODIT_NONE)
    {
        importModFileAfterNewProject = 0;
        stopOperationWithError(fmImportModError);
    }
}

void cFileManager::updateExportModFile()
{
    if (call(EXT_MODIT_UPDATE_EXPORT) == EXT_MODIT_NONE)
        stopOperationWithError(fmExportModError);
}

float cFileManager::importMod_getProgress()
{
    const uint32_t bits = call(EXT_MODIT_PROGRESS_IMPORT);
    float progress = 0;
    if (bits != EXT_MODIT_NONE)
        memcpy(&progress, &bits, sizeof(progress));
    return progress;
}

uint8_t cFileManager::exportItFile_getProgress()
{
    const uint32_t progress = call(EXT_MODIT_PROGRESS_EXPORT);
    return progress == EXT_MODIT_NONE ? 0 : uint8_t(progress);
}

// the start of the export: fileManagerExportToMod.cpp's exportItFile, with the module as a precondition
uint8_t cFileManager::exportItFile()
{
    if (!ext_hooks.modit)
        return false;
    if (status != fmIdle && status != fmSavingProjectToWorkspace)
        return false;
    if (currentOperation != fmNoOperation && currentOperation != fmSaveWorkspaceProject)
        return false;

    currentOperationStep = 0;
    status = fmExportingMod;
    currentOperation = fmExportModFile;

    return true;
}

// Two image functions only the module calls: cFileManager::moveToPrevOperationStep and the 5 argument cFileTransfer::loadFileToMemory.
// Referenced from here so --gc-sections keeps them (ext_module.cpp keepForModule takes the address of this function), never called.
void cFileManager::moditKeep(cFileTransfer* transfer)
{
    newFileManager.moveToPrevOperationStep();
    transfer->loadFileToMemory(nullptr, nullptr, 0, 0, fileWholeOnce);
}

#endif
