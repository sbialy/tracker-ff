// The module's names for the five entry points of the three file manager sources. Included first (-include) when
// core/FileManager/fileManagerImportMod.cpp, fileManagerImportIt.cpp, fileManagerExportToMod.cpp and modit_dispatch.cpp are compiled
// for modit.plg (build_modit.sh) or for the ui_shot host (the Makefile there): the image has forwarders of the same names
// (core/FileManager/fileManagerModit.cpp), so the sources stay byte for byte what the stock firmware has and the module's members
// get other names. Only member functions are renamed (the class layout does not change).
#pragma once

#define updateImportModFile moditUpdateImport
#define updateExportModFile moditUpdateExport
#define importMod_getProgress moditProgressImport
#define exportItFile_getProgress moditProgressExport
#define exportItFile moditExportStart
