// The project state store (FEATURE_PROJECT_STORE): project.mtff, the Tracker FF settings of a project that the binary project.mt has no
// room for (Strum ramp depth, seed, scale snap, track groove, Follow chord, send ducking, stereo pairs, FX3/FX4, macro mode, Parts). The
// codec and the file rules are mtff_codec.h, the card and the buffers projectStoreIo.h. The file manager calls it through ff_hooks.h, the
// plugin host (plugins.txt) is a separate file with its own hooks. Compat rule: a file without a key plays as a project of a build without
// the key, and a key is written only when its setting differs from that.
#pragma once

#include <stdint.h>

#include "../libs/Audio/duck_math.h"

class Platform;
class FileSystem;

namespace projstore
{
void boot(Platform* platform);   // the card and the build id for the header, before any project loads
void useFs(FileSystem* fs);
void setStatusWriter(void (*writer)());   // the plugin host's host.txt (it names note() and state())

// dir is "Workspace" or a project folder. False: the card failed (the caller reports a failed save)
void loaded(const char* dir, bool newProject);   // after a project load: read, check the stamp, apply (defaults without a file)
bool save(const char* dir);                      // Workspace save: the file when a setting differs from the old behaviour
bool open(const char* projectDir, const char* workspaceDir);       // project open, before loaded: the project's file to the Workspace
bool toProject(const char* workspaceDir, const char* projectDir);  // Save As: the Workspace file to the project folder
void newProjectSaved(const char* dir);           // a new project's first project.mt is written: its project.mtff with that stamp
void stamp(uint32_t crc, uint32_t size);         // the project.mt just read or written (CRC32 of its last four bytes, size)
// A project open has just read the project.mt of the project folder (stamp ran for it): that stamp, not the one of the Workspace copy (which
// gets the project's name and path written into it), is what that open's project.mtff is compared with
void openStampKeep();
uint8_t pairsPreload(const char* dir);           // the stereo pair links, before the instruments load (stereoPairs.h)
void projectClosing();                           // a project is closing: settings that act while files load go back to the old behaviour

// The header of the file the last load read: writer (major << 16) | (minor << 8) | patch, 0 none, and its format, 0 none
uint32_t writerVersion();
int formatVersion();
// The on screen notice of a user's open that found a file of a newer FF: true once, fills two popup lines
bool takeOpenNotice(char* line1, int cap1, char* line2, int cap2);

int strumRampDepth();
void setStrumRampDepth(int pct);
int macroMode();
void setMacroMode(int mode);
const DuckParams& duck();
void duckStore(const DuckParams& p);   // already clamped (duckClamp)

const char* note();   // the one status line of the last load, "" for none
int state();          // what the last load found (plug_sidecar_io.h SidecarState)
}   // namespace projstore
