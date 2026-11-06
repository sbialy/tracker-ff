// Project sidecar of the plugin host on a card: load, Workspace save, project open and save copies, crash and card error
// rules. Plain C++ templates over a small file operations type (below), so tools/plug_sidecar_test.cpp runs the same code on an
// in-memory card that fails or loses power at any step. plug_sidecar.h has the format.
#pragma once

#include "open_fileops.h"
#include "plug_sidecar.h"

#include <stdio.h>
#include <string.h>

namespace plug
{

// ---- the file rules, over any file system Fs with
//   Presence stat(const char* path)        kAbsent only when the card found no such entry, kUnknown on any card error
//   int readFile(const char* path, char* buf, int cap)   the whole file, bytes read, -1 when it cannot be opened or read or
//                                          holds more than cap bytes
//   bool writeNew(const char* path, const char* buf, int n)   creates or truncates path with the n bytes, true only when the
//                                          card holds them (plug_host.cpp reads them back)
//   bool remove(const char* path), bool rename(const char* from, const char* to)   rename fails when `to` exists (FatFs)
// plug_host.cpp runs them on the SD card, tools/plug_sidecar_test.cpp on an in-memory one that fails or loses power at any step.
//
// A replace goes through <path>.tmp and open_fileops.h's replaceFileOps (the old file steps aside as <path>.bak until the new
// one is in place), every read through recoverBackupOps first (an interrupted replace left the old file at <path>.bak: it is
// moved back). A .tmp is never promoted: a write the card did not acknowledge may be half done. A card error is never taken for
// "no file": the settings it may hold are not overwritten or removed until the project is loaded again.

using Presence = polysynth::open::Presence;

// What the project load found, and so what a later save may do with the file
enum class SidecarState : uint8_t
{
    kNone,         // the card says there is no file: no plugin, exactly as stock. A save writes one when a slot is used
    kLoaded,       // read and parsed
    kForeign,      // a file that is not a sidecar (no "tffplug 1" line, empty): read as empty, kept until a slot is used
    kUnreadable,   // a card error: the file may hold settings. Read as empty, never overwritten or removed (a save with a
                   // slot in use fails), loading the project again retries
    kStale,        // project open could not bring the project's file to the Workspace: as kUnreadable, and the project save
                   // does not copy the Workspace file back over the project's
};

inline bool sidecarBlocked(SidecarState st)
{
    return st == SidecarState::kUnreadable || st == SidecarState::kStale;
}

// The file at path, after moving back an interrupted replace. kPresent: n bytes (the whole file). kAbsent: no such file (n 0).
// kUnknown: the card failed, or the file is larger than cap: buf says nothing.
template <class Fs> Presence sidecarFetch(Fs& fs, const char* path, char* buf, int cap, int& n)
{
    n = 0;
    const Presence p = polysynth::open::recoverBackupOps(fs, path);
    if (p != Presence::kPresent) return p;
    const int got = fs.readFile(path, buf, cap);
    if (got < 0 || got > cap) return Presence::kUnknown;
    n = got;
    return Presence::kPresent;
}

// path becomes the n bytes: written to <path>.tmp, then replaceFileOps. False: path is as it was, or absent with the old file at
// <path>.bak (the next fetch moves it back).
template <class Fs> bool sidecarReplace(Fs& fs, const char* path, const char* buf, int n)
{
    char tmp[80];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= int(sizeof(tmp))) return false;
    if (fs.writeNew(tmp, buf, n) && polysynth::open::replaceFileOps(fs, tmp, path)) return true;
    if (fs.stat(tmp) == Presence::kPresent) fs.remove(tmp);   // best effort, a .tmp is never read
    return false;
}

// No file at path and none for a fetch to move back: <path>.bak goes first (a cut after it leaves path, never a resurrected
// older file). False: the card failed, something may be left.
template <class Fs> bool sidecarRemove(Fs& fs, const char* path)
{
    char bak[80];
    if (snprintf(bak, sizeof(bak), "%s.bak", path) >= int(sizeof(bak))) return false;
    const char* const order[2] = {bak, path};
    for (const char* q : order)
    {
        const Presence p = fs.stat(q);
        if (p == Presence::kUnknown || (p == Presence::kPresent && !fs.remove(q))) return false;
    }
    return true;
}

// Project load: the slots of the file (every slot empty for anything but kLoaded, as stock firmware without a file).
template <class Fs> SidecarState sidecarLoad(Fs& fs, const char* path, char* buf, int cap, Sidecar& s)
{
    memset(&s, 0, sizeof(s));
    int n = 0;
    switch (sidecarFetch(fs, path, buf, cap, n))
    {
        case Presence::kAbsent: return SidecarState::kNone;
        case Presence::kPresent: break;
        default: return SidecarState::kUnreadable;
    }
    if (n > 0 && sidecarRead(buf, n, s)) return SidecarState::kLoaded;
    memset(&s, 0, sizeof(s));
    return SidecarState::kForeign;
}

// Workspace save: the file when a slot is used, no file at all otherwise (a stale one is removed). st is what the load found
// (updated: kLoaded after a write, kNone after a removal). A blocked state touches nothing: true when there is nothing to save,
// false when a slot is used (the settings cannot go over a file the card could not read). False: the settings are not on the card.
template <class Fs> bool sidecarSave(Fs& fs, const char* path, const Sidecar& s, SidecarState& st, char* buf, int cap)
{
    const int n = sidecarWrite(s, buf, cap);
    if (n < 0) return false;
    if (sidecarBlocked(st)) return n == 0;
    if (n == 0)
    {
        if (st == SidecarState::kForeign) return true;   // not ours, nothing of ours to store: kept
        if (!sidecarRemove(fs, path)) return false;
        st = SidecarState::kNone;
        return true;
    }
    if (!sidecarReplace(fs, path, buf, n)) return false;
    st = SidecarState::kLoaded;
    return true;
}

// Project open (project folder to Workspace) and save (Workspace to project folder): the destination ends up with the source's
// file or with none. keepDst: a missing source does not remove the destination (the source's state was not read). False: the
// card failed. A source that cannot be read leaves the destination as it was.
template <class Fs> bool sidecarCopy(Fs& fs, const char* src, const char* dst, char* buf, int cap, bool keepDst = false)
{
    int n = 0;
    switch (sidecarFetch(fs, src, buf, cap, n))
    {
        case Presence::kPresent: return sidecarReplace(fs, dst, buf, n);
        case Presence::kAbsent: return keepDst || sidecarRemove(fs, dst);
        default: return false;
    }
}

// ---- the project steps (plug_host.cpp: sidecarOpen, projectLoaded, sidecarToProject), state kept by the caller

// Project open: the project's file to the Workspace. A failed copy sets stale (the Workspace file is not the project's): the
// previous project's file is removed if the card lets it, and the next load starts empty (kStale).
template <class Fs> bool sidecarOpenStep(Fs& fs, const char* projectPath, const char* wsPath, char* buf, int cap, bool& stale)
{
    const bool ok = sidecarCopy(fs, projectPath, wsPath, buf, cap);
    stale = !ok;
    if (!ok) sidecarRemove(fs, wsPath);
    return ok;
}

// Workspace load after an open (stale from sidecarOpenStep, cleared here)
template <class Fs> SidecarState sidecarLoadStep(Fs& fs, const char* wsPath, char* buf, int cap, Sidecar& s, bool& stale)
{
    SidecarState st = sidecarLoad(fs, wsPath, buf, cap, s);
    if (stale)
    {
        memset(&s, 0, sizeof(s));
        st = SidecarState::kStale;
        stale = false;
    }
    return st;
}

// Project save (after the Workspace save): the Workspace file to the project folder. kStale copies nothing (the project folder
// keeps its own file; false when a slot is used). kUnreadable copies the Workspace file when it reads now, and a missing one
// never removes the project's.
template <class Fs> bool sidecarProjectStep(Fs& fs, const char* wsPath, const char* projectPath, const Sidecar& s, SidecarState st,
                                            char* buf, int cap)
{
    if (st == SidecarState::kStale) return sidecarWrite(s, buf, cap) == 0;
    return sidecarCopy(fs, wsPath, projectPath, buf, cap, sidecarBlocked(st));
}

}   // namespace plug
