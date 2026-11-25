// Open PolySynth shim for MK66 - replacing a file on the card, and finding it again after an interruption.
// Plain C++ over a small file operations type, so the host smoke test runs the same code on an in-memory card with FatFs rename rules that
// fails (or loses power) at any step.
#pragma once

#if defined(FEATURE_SYNTH) && !defined(FEATURE_SYNTH_OPEN)
// Tracker+ and Mini core (project.mtff, core/FileManager/projectSidecar.cpp): the closed library's FileSystem has no Presence (it is the open shim's)
#else
#include "PolySynth/FileSystem.h"
#endif

#include <cstdio>

namespace polysynth::open
{

// The longest path the FileManager builds (PATCH_SIZE 255 with the terminator) plus ".bak". The <path>.bak of a file is formed in a static
// buffer of this size, not on the stack: the audio interrupt shares the main stack of the MK66 and the heap is not to be used for it
// (operator new returns null). The buffer is used by one caller at a time: the card operations run on the main thread only, never in an
// interrupt, and neither function below calls the other or itself.
constexpr size_t kBakPathMax = 255 + 4 + 1;

#ifdef POLYSYNTH_FS_PRESENCE
using Presence = polysynth::FileSystem::Presence;
#else
// A build without the open synth stack (Tracker+ and Mini, whose closed library header has no Presence): the project store's card file
// layer (core/FileManager/projectStoreIo.h) runs these templates with the same three answers
enum class Presence
{
    kAbsent,
    kPresent,
    kUnknown,
};
#endif

// `to` becomes the file `from`. FatFs does not rename over a file, so the old file steps aside as <to>.bak and goes back when `from` cannot
// take its place. Ops provides
//   Presence stat(const char* path)    kAbsent only when the card found no such entry, kUnknown on any card error
//   bool remove(const char* path)
//   bool rename(const char* from, const char* to)    fails when `to` exists
// This is not atomic against power loss: between the two renames `to` is absent and the old file is <to>.bak, the new one is `from`.
// Every reader of these files calls recoverBackupOps first (OpenPatchStore) and moves <to>.bak back, so the old file is what is loaded.
// `from` is never promoted by a reader: a write that was not acknowledged may be half done. The recovery is not complete end to end:
// when it fails on slot 0 the core's slot 0 loader only sees "not loaded" and goes on to the library and Init, whose save replaces the
// file and deletes the .bak; and the library browser lists only *.patch, it does not find an orphan *.patch.bak.
// False: nothing was replaced. `to` is as it was, or absent with the old file at <to>.bak (a failed move back, power loss).
template <class Ops>
bool replaceFileOps(Ops& ops, const char* from, const char* to)
{
    static char bak[kBakPathMax];
    if (std::snprintf(bak, sizeof(bak), "%s.bak", to) >= static_cast<int>(sizeof(bak)))
    {
        return false;
    }
    const Presence old = ops.stat(to);
    if (old == Presence::kUnknown)
    {
        return false; // the card does not say whether there is an old file: touch nothing
    }
    const bool hadOld = old == Presence::kPresent;
    if (hadOld)
    {
        // a .bak next to a canonical file is an older version (the delete after an earlier replace failed)
        const Presence stale = ops.stat(bak);
        if (stale == Presence::kUnknown || (stale == Presence::kPresent && !ops.remove(bak)))
        {
            return false;
        }
        if (!ops.rename(to, bak))
        {
            return false;
        }
    }
    if (!ops.rename(from, to))
    {
        if (hadOld)
        {
            ops.rename(bak, to); // when this fails too, `to` is absent and the old file stays at <to>.bak for recoverBackupOps
        }
        return false;
    }
    if (ops.stat(bak) == Presence::kPresent)
    {
        ops.remove(bak); // best effort: the old file, or an orphan of an earlier failed move back, is not needed any more
    }
    return true;
}

// The file `path` is absent and an interrupted replace left the old one at <path>.bak: moves it back.
// kPresent: `path` is there (it was, or it is now). kAbsent: it is not there and there is nothing to restore. kUnknown: the card
// failed or the move back did, the file may exist: the caller must treat it as an error, not as an empty slot.
template <class Ops>
Presence recoverBackupOps(Ops& ops, const char* path)
{
    const Presence now = ops.stat(path);
    if (now != Presence::kAbsent)
    {
        return now;
    }
    static char bak[kBakPathMax];
    if (std::snprintf(bak, sizeof(bak), "%s.bak", path) >= static_cast<int>(sizeof(bak)))
    {
        return Presence::kAbsent; // longer than any path the FileManager makes: there is no file of this name
    }
    const Presence b = ops.stat(bak);
    if (b != Presence::kPresent)
    {
        return b; // kAbsent: nothing to restore, kUnknown: it may be there
    }
    return ops.rename(bak, path) ? Presence::kPresent : Presence::kUnknown;
}

} // namespace polysynth::open
