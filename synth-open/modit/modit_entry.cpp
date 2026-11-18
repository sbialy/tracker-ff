// modit.plg: the entry (ext_abi.h ext_entry_fn). Runs the one static constructor of the three sources (their file scope data has default
// member initialisers, there is no startup code in a module) and gives the image its hook. A table that predates the modit field
// (an older image) is refused: the loader refuses such a module by the image id first, this is the second line.
#include "ext_abi.h"
#include "fileManager.h"

#include <stddef.h>

extern "C"
{
extern void (*__init_array_start[])(void);   // modit.ld
extern void (*__init_array_end[])(void);

int32_t ext_entry(ext_hooks_t* hooks, uint32_t abi, uint32_t /*develop*/)
{
    if (abi != EXT_ABI_VERSION || hooks->struct_size < offsetof(ext_hooks_t, modit) + sizeof(hooks->modit)) return 1;
    for (void (**init)(void) = __init_array_start; init < __init_array_end; init++) (*init)();
    hooks->modit = cFileManager::moditDispatch;
    return 0;
}
}
