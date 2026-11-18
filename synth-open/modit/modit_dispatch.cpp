// modit.plg: the one hook of the module. The image forwards the four calls of the file manager to ext_hooks.modit
// (core/FileManager/fileManagerModit.cpp), this runs them on the real file manager object. A static member of cFileManager so
// the private state machine is in reach. Compiled with -include modit_names.h, see there. Also in the ui_shot host build, which
// has no module and sets ext_hooks.modit to this function itself (synth-open/ext_module.cpp).
#include "ext_hooks.h"
#include "fileManager.h"

#include <cstring>

uint32_t cFileManager::moditDispatch(uint32_t op)
{
    switch (op)
    {
    case EXT_MODIT_UPDATE_IMPORT: newFileManager.updateImportModFile(); return 0;
    case EXT_MODIT_UPDATE_EXPORT: newFileManager.updateExportModFile(); return 0;
    case EXT_MODIT_PROGRESS_IMPORT:
    {
        const float progress = newFileManager.importMod_getProgress();
        uint32_t bits;
        memcpy(&bits, &progress, sizeof(bits));
        return bits;
    }
    case EXT_MODIT_PROGRESS_EXPORT: return newFileManager.exportItFile_getProgress();
    default: return EXT_MODIT_NONE;
    }
}
