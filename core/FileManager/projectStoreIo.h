// The file operations on the SD card and the path buffers of the two project sidecars, project.mtff (projectSidecar.cpp) and plugins.txt
// (synth-open/plug_host.cpp). The hooks of both run in the main loop only and never nest, so they share one set of buffers.
#pragma once

#include "plug_sidecar_io.h"
#include "FileSystemAdapter.h"
#include "Platform.h"

#include <string.h>

namespace projstore
{
using plug::Presence;

extern FileSystem* g_fs;   // null until boot (no project file is touched before)
extern char g_pathA[plug::kPathMax];
extern char g_pathB[plug::kPathMax];
extern char g_pathScratch[plug::kPathBuf];   // the scratch the replace builds <path>.tmp in

// The card's answer for a full path: kAbsent only on FatFs FR_NO_FILE, kUnknown on any card error. With the open synth stack this is
// FileSystemAdapter::statPath. A build without it (Tracker+, Mini) has the store's own copy (projectSidecar.cpp), the same f_stat call
#ifdef FEATURE_SYNTH_OPEN
inline Presence cardStat(const char* p) { return FileSystemAdapter::statPath(p); }
#else
Presence cardStat(const char* p);
#endif

// plug_sidecar_io.h's file operations on the SD card
struct SdOps
{
    char* pathScratch() { return g_pathScratch; }
    Presence stat(const char* p) { return cardStat(p); }
    bool remove(const char* p) { return g_fs->remove(p); }
    bool rename(const char* from, const char* to) { return g_fs->rename(from, to); }
    int readFile(const char* p, char* buf, int cap)
    {
        auto [ok, f] = g_fs->openFile(p, FileSystem::FileOpenMode::kRead);
        if (!ok) return -1;
        int n = int(g_fs->readFile(buf, size_t(cap), f));
        char more;
        if (n == cap && g_fs->readFile(&more, 1, f) != 0) n = -1;   // larger than cap (or a read error): not this file
        g_fs->closeFile(f);
        return n;
    }
    // FatFs reports a failed flush only to f_close, which the platform's closeFile does not return: the bytes are read back
    bool writeNew(const char* p, const char* buf, int n)
    {
        auto [ok, f] = g_fs->openFile(p, FileSystem::FileOpenMode::kWriteCreateNew);
        if (!ok) return false;
        const bool wrote = g_fs->writeFile(buf, size_t(n), f) == n;
        g_fs->closeFile(f);
        if (!wrote) return false;
        auto [rok, r] = g_fs->openFile(p, FileSystem::FileOpenMode::kRead);
        if (!rok) return false;
        bool same = true;
        char chunk[64];   // the main stack, which the audio interrupt shares: small
        for (int at = 0; same && at < n; at += int(sizeof(chunk)))
        {
            const int want = n - at < int(sizeof(chunk)) ? n - at : int(sizeof(chunk));
            same = g_fs->readFile(chunk, size_t(want), r) == want && memcmp(chunk, buf + at, size_t(want)) == 0;
        }
        same = same && g_fs->readFile(chunk, 1, r) == 0;
        g_fs->closeFile(r);
        return same;
    }
};

// The card file layer of plug_sidecar_io.h over SdOps, instantiated once (here, projectSidecar.cpp) for both sidecars and the text files
// of the features (plug::readText, plug::writeText)
bool writeText(const char* path, const char* text, int n);
int readText(const char* path, char* buf, int cap);
bool removeFile(const char* path);
plug::SidecarState pluginsLoadStep(const char* wsPath, char* buf, int cap, plug::Sidecar& s, bool& stale);
bool pluginsSave(const char* path, const plug::Sidecar& s, plug::SidecarState& st, char* buf, int cap);
bool pluginsOpenStep(const char* projectPath, const char* wsPath, char* buf, int cap, bool& stale);
bool pluginsProjectStep(const char* wsPath, const char* projectPath, const plug::Sidecar& s, plug::SidecarState st, char* buf, int cap);
}   // namespace projstore
