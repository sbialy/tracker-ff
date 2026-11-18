
#ifndef FILEMANAGER_FILEMANAGERDEFS_H_
#define FILEMANAGER_FILEMANAGERDEFS_H_

#include "mtStructs.h"

#include <cr_section_macros.h>
#include <stdint.h>
#include <string.h>

// The longest file name a save writes below the project folder: "/instruments/48 " + an instrument name (INSTRUMENT_NAME_SIZE - 1)
// + ".pti" = 38 (patterns, their .mtx, patternsMetadata, project.mt, project.mtff, plugins.txt and the synth patches are shorter).
// Every path of a save is built in a PATCH_SIZE buffer, so a project folder longer than PATCH_SIZE - 1 - this would have its files
// written under cut-off names (patterns/patte, pro, ins) while the save still reported success.
constexpr int cProjectLongestFileSuffix = 1 + 11 + 1 + 2 + 1 + (INSTRUMENT_NAME_SIZE - 1) + 4;

// True when the project folder path/name leaves room for every file a save writes into it.
inline bool projectFolderFits(const char* path, const char* name)
{
    return strlen(path) + 1 + strlen(name) + cProjectLongestFileSuffix < PATCH_SIZE;
}

struct strProjectFileHeader
{
    char id_file[2];              // typ pliku, np MT = plik projektu, TI = instrument
    uint16_t type;                // typ w liczbie
    char fwVersion[4];            // wersja fw w ktorej zapisano plik
    char fileStructureVersion[4]; // wersja struktury pliku
    uint16_t size;                // rozmiar
};

struct strInstrumentFile
{
    struct strInstrumentDataAndHeader
    {
        strProjectFileHeader instrHeader;
        strInstrument instrument;

    } instrumentDataAndHeader;

    uint32_t crc;
};

struct strProjectFile
{
    struct strProjectDataAndHeader
    {
        strProjectFileHeader projectHeader;
        strMtProjectRemote project;
    } projectDataAndHeader;

    uint32_t crc;
};

//----------------------------------------------------------------------------------

enum enFileManagerErrorSource
{
    errorSourceUnknow = 0,
    errorSourceProject,
    errorSourcePattern,
    errorSourceInstrument,
    errorSourceSample,

};

inline const char cNewProjectName[] = "New Project";

inline const char cWorkspacePath[] = "Workspace";
inline const char cWorkspaceKeepPath[] = "Workspace.keep"; // the Workspace of the last normal session while Demo start is on
inline const char cWorkspacePrevPath[] = "Workspace.prev"; // the Workspace that a project open or a new project replaced while it held unsaved work
inline const char cProjectFileNameInWorkspace[] = "Workspace/project.mt";
inline const char cWorkspacePatternsPath[] = "Workspace/patterns";
inline const char cWorkspacePatternFileFormat[] = "Workspace/patterns/pattern_%02d.mtp";
inline const char cWorkspaceInstrumentsPath[] = "Workspace/instruments";
inline const char cWorkspaceInstrumentFileFormatOld[] = "Workspace/instruments/instrument_%02d.mti";
inline const char cWorkspaceInstrumentFileFormatNew[] = "Workspace/instruments/instrument_%02d.pti";
inline const char cWorkspaceSamplesPath[] = "Workspace/samples";
inline const char cWorkspaceSamplesFilesFormat[] = "Workspace/samples/instr%02d.wav";
inline const char cWorkspacePatternsMetadataFilePath[] = "Workspace/patterns/patternsMetadata";

inline const char cDefaultProjectsPath[] = "Projects";
inline const char cProjectsPathFormat[] = "%s/%s";
inline const char cProjectFileInProjectsFormat[] = "%s/%s/project.mt";
inline const char cProjectsPatternsPathFormat[] = "%s/%s/patterns";
inline const char cProjectsPatternFileFormat[] = "%s/%s/patterns/pattern_%02d.mtp";
inline const char cProjectsPatternsMetadataFileFormat[] = "%s/%s/patterns/patternsMetadata";
inline const char cProjectsInstrumentsPathFormat[] = "%s/%s/instruments";
inline const char cProjectsInstrumentFileFormatOld[] = "%s/%s/instruments/instrument_%02d.mti";
inline const char cProjectsInstrumentFileFormatNew[] = "%s/%s/instruments/%d %s.pti";
inline const char cProjectsInstrumentFileFormatNewAlt[] = "%s/%s/instruments/%02d %s.pti";
inline const char cProjectsSamplesFilesFormat[] = "%s/%s/samples/instr%02d.wav";
inline const char cProjectsSamplesPathFormat[] = "%s/%s/samples";

inline const char cProjectSamplesExportFilesFormat[] = "%s/%s/export/samples/%s%s.wav";
inline const char cProjectSamplesExportPathFormat[] = "%s/%s/export/samples";
inline const char cProjectExportPathFormat[] = "%s/%s/export";

inline const char cExportPath[] = "Export";
inline const char cExportProjectPathFormat[] = "Export/%s";

inline const char cModsPath[] = "Mod";
inline const char cModsPathFormat[] = "Mod/%s";

inline const char cExportModPath[] = "Mod";
inline const char cExportModFilePathFormat[] = "Mod/%s.it";

inline const char cGamesPath[] = "Games";
inline const char cGamesPathFormat[] = "Games/%s";

inline const char cProjectFileName[] = "project.mt";

inline const char cProjectsRecentFilePath[] = "Projects/recents";

inline const char cDefaultPatchesPath[] = "Patches";

const uint8_t WORKSPACE_SAMPLES_FORMAT_MAX_LENGTH = 38;

const uint8_t WORKSPACE_INSTRUMENTS_FORMAT_MAX_LENGTH = 47;

#endif /* FILEMANAGER_FILEMANAGERDEFS_H_ */
