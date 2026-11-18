// PLUG_EXT_FILES (ABI 1.2, host level 2, plug_abi.h): the list of the single cycle waves in /Waves and the .wav reader behind it.
// Header only and templated on the card access, so tools/plug_files_test.cpp runs the same code on the host against a folder.
// plug_synth.cpp holds the instance (SDRAM) and the plug_ext_files functions, which call these only from plug_ext_synth.service.
//
// The list: the .wav files of /Waves and of its subfolders one level down, paths relative to /Waves, sorted by bytes (strcmp). Hidden
// and system entries and names starting with '.' (the "._" AppleDouble files a Mac writes) are skipped, a path of PLUG_FILES_PATH bytes
// or more is skipped. With more than PLUG_FILES_MAX files the list keeps the PLUG_FILES_MAX smallest paths, whatever order the card
// returns them in, so an index means the same file on any copy of the same folder.
//
// By path (ABI 1.4 tail, waveReadPath and waveNext): a file is named by its path relative to /Waves and opened directly, nothing is listed, so
// any file of the card is reachable, whatever the 1,024 entry cap of the list does. waveNext is the list's order over all files of the card.
//
// Fs (the platform's single directory walker and file calls, main thread only):
//   bool dirOpen(const char* path); bool dirNext(char* name256, bool& isDir, bool& hidden); void dirClose();
//   bool open(const char* path); int read(void* dst, int n); bool seek(uint32_t pos); void close();
#pragma once

#include "plug/plug_abi.h"
#include "wave_path.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace plug
{

constexpr const char* kWavesDir = "/Waves";
constexpr int kWaveFolders = 128;   // subfolders of /Waves that are walked (AKWF-FREE has 65), the rest are skipped
constexpr int kWaveName = 256;      // a directory entry name as the walker returns it (FatFs FF_MAX_LFN 255 + NUL)

// Plain data (it lives in the SDRAM, no constructor may run there): clear() before the first use
struct WaveList
{
    char path[PLUG_FILES_MAX][PLUG_FILES_PATH];
    uint16_t order[PLUG_FILES_MAX];      // indices into path, sorted by path
    char folder[kWaveFolders][PLUG_FILES_PATH];
    char name[kWaveName];                // the walker's scratch
    int n;
    int folders;
    bool built;
    uint32_t skipped;                    // files not listed: path too long, over the cap, folders over kWaveFolders
    void clear()
    {
        n = 0;
        folders = 0;
        built = false;
        skipped = 0;
    }
    const char* at(int i) const { return path[order[i]]; }
};

inline bool waveName(const char* s)
{
    const size_t n = strlen(s);
    if (n < 5 || s[0] == '.') return false;
    const char* e = s + n - 4;
    return e[0] == '.' && (e[1] | 0x20) == 'w' && (e[2] | 0x20) == 'a' && (e[3] | 0x20) == 'v';
}

// Inserts p (shorter than PLUG_FILES_PATH) at its sorted place. A full list drops its largest path when p is smaller, else p.
inline void waveInsert(WaveList& w, const char* p)
{
    int lo = 0, hi = w.n;
    while (lo < hi)
    {
        const int mid = (lo + hi) / 2;
        if (strcmp(w.at(mid), p) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < w.n && strcmp(w.at(lo), p) == 0) return;   // the same path twice (a card that returns an entry again)
    int slot;
    if (w.n < int(PLUG_FILES_MAX))
        slot = w.n++;
    else
    {
        w.skipped++;
        if (lo >= w.n) return;               // larger than every kept path
        slot = w.order[w.n - 1];             // the largest goes, its slot takes p
        memmove(&w.order[lo + 1], &w.order[lo], size_t(w.n - 1 - lo) * sizeof(w.order[0]));
        w.order[lo] = uint16_t(slot);
        memcpy(w.path[slot], p, strlen(p) + 1);
        return;
    }
    memmove(&w.order[lo + 1], &w.order[lo], size_t(w.n - 1 - lo) * sizeof(w.order[0]));
    w.order[lo] = uint16_t(slot);
    memcpy(w.path[slot], p, strlen(p) + 1);
}

// Walks /Waves (base) and its subfolders. A missing folder is an empty list. Main thread, a few hundred ms for a thousand files.
template <class Fs>
void waveBuild(Fs& fs, WaveList& w, const char* base = kWavesDir)
{
    w.clear();
    w.built = true;
    char dir[sizeof(w.folder[0]) + 16];
    bool isDir = false, hidden = false;
    if (!fs.dirOpen(base)) return;
    while (fs.dirNext(w.name, isDir, hidden))
    {
        w.name[kWaveName - 1] = 0;
        if (hidden || w.name[0] == '.') continue;
        const size_t len = strlen(w.name);
        if (isDir)
        {
            if (w.folders < kWaveFolders && len + 6 < PLUG_FILES_PATH)   // room for "/x.wav"
                memcpy(w.folder[w.folders++], w.name, len + 1);
            else
                w.skipped++;
        }
        else if (waveName(w.name))
        {
            if (len < PLUG_FILES_PATH)
                waveInsert(w, w.name);
            else
                w.skipped++;
        }
    }
    fs.dirClose();
    char p[PLUG_FILES_PATH];
    for (int f = 0; f < w.folders; f++)
    {
        snprintf(dir, sizeof(dir), "%s/%s", base, w.folder[f]);
        if (!fs.dirOpen(dir)) continue;
        const size_t fl = strlen(w.folder[f]);
        while (fs.dirNext(w.name, isDir, hidden))
        {
            w.name[kWaveName - 1] = 0;
            if (isDir || hidden || !waveName(w.name)) continue;
            const size_t len = strlen(w.name);
            if (fl + 1 + len >= PLUG_FILES_PATH)
            {
                w.skipped++;
                continue;
            }
            memcpy(p, w.folder[f], fl);
            p[fl] = '/';
            memcpy(p + fl + 1, w.name, len + 1);
            waveInsert(w, p);
        }
        fs.dirClose();
    }
}

// The format of an open .wav: where its samples are and how to read them
struct WaveFmt
{
    uint32_t data;     // file offset of the first frame
    uint32_t frames;
    uint16_t align;    // bytes per frame
    uint16_t bits;
    bool isFloat;
};

inline uint32_t le32(const uint8_t* b)
{
    return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
}
inline uint16_t le16(const uint8_t* b)
{
    return uint16_t(b[0] | b[1] << 8);
}

// RIFF WAVE header of the open file: 0 and fmt, or PLUG_FILES_E_*. Chunks before "data" are walked (at most 16), fmt must come first.
template <class Fs>
int32_t waveParse(Fs& fs, WaveFmt& f)
{
    uint8_t b[40];
    if (fs.read(b, 12) != 12) return PLUG_FILES_E_FORMAT;
    if (memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) return PLUG_FILES_E_FORMAT;
    uint32_t pos = 12;
    bool haveFmt = false;
    uint16_t channels = 0;
    for (int chunk = 0; chunk < 16; chunk++)
    {
        if (!fs.seek(pos) || fs.read(b, 8) != 8) return PLUG_FILES_E_FORMAT;
        const uint32_t size = le32(b + 4);
        if (memcmp(b, "fmt ", 4) == 0)
        {
            if (size < 16) return PLUG_FILES_E_FORMAT;
            const int want = size >= 26 ? 26 : 16;
            if (fs.read(b, want) != want) return PLUG_FILES_E_FORMAT;
            uint16_t tag = le16(b);
            channels = le16(b + 2);
            f.align = le16(b + 12);
            f.bits = le16(b + 14);
            if (tag == 0xFFFE && want == 26) tag = le16(b + 24);   // WAVE_FORMAT_EXTENSIBLE: the sub format GUID starts with the tag
            f.isFloat = tag == 3;
            if ((tag != 1 && tag != 3) || channels < 1 || channels > 2) return PLUG_FILES_E_FORMAT;
            if (f.isFloat ? f.bits != 32 : (f.bits != 8 && f.bits != 16 && f.bits != 24 && f.bits != 32)) return PLUG_FILES_E_FORMAT;
            if (f.align != channels * (f.bits / 8)) return PLUG_FILES_E_FORMAT;
            haveFmt = true;
        }
        else if (memcmp(b, "data", 4) == 0)
        {
            if (!haveFmt) return PLUG_FILES_E_FORMAT;
            f.data = pos + 8;
            f.frames = size / f.align;
            if (f.frames < 1 || f.frames > PLUG_FILES_FRAMES) return PLUG_FILES_E_LONG;
            return 0;
        }
        if (size > 0x00FFFFFFu) return PLUG_FILES_E_FORMAT;   // no single cycle file has a chunk this large
        pos += 8 + size + (size & 1u);
    }
    return PLUG_FILES_E_FORMAT;
}

// The first channel of one frame as int16
inline int16_t waveSample(const uint8_t* q, const WaveFmt& f)
{
    if (f.isFloat)
    {
        float v;
        memcpy(&v, q, 4);
        v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);   // NaN stays NaN here and becomes 0 below
        return v == v ? int16_t(v * 32767.0f) : int16_t(0);
    }
    switch (f.bits)
    {
        case 8: return int16_t((int(q[0]) - 128) * 256);
        case 16: return int16_t(le16(q));
        case 24: return int16_t(le16(q + 1));
        default: return int16_t(le16(q + 2));
    }
}

// The frames [first, first + max) of the .wav at the card path `full` as mono int16, the frame count of the whole file or PLUG_FILES_E_*.
// An open that fails is `openErr` (E_CARD for a file of the list, which is there, E_NONE for a path a plugin made up).
template <class Fs>
int32_t waveReadFile(Fs& fs, const char* full, uint32_t first, int16_t* dst, uint32_t max, int32_t openErr)
{
    if (!fs.open(full)) return openErr;
    WaveFmt f{};
    int32_t r = waveParse(fs, f);
    if (r == 0)
    {
        r = int32_t(f.frames);
        uint32_t n = first < f.frames ? f.frames - first : 0;
        n = n < max ? n : max;
        if (n && (!dst || !fs.seek(f.data + first * f.align))) r = PLUG_FILES_E_CARD;
        uint8_t b[48];   // the main stack, which the audio interrupt shares: small. 48 bytes hold whole frames of every format
        const uint32_t per = sizeof(b) / f.align;
        for (uint32_t done = 0; r > 0 && done < n;)
        {
            const uint32_t k = n - done < per ? n - done : per;
            if (fs.read(b, int(k * f.align)) != int(k * f.align))
            {
                r = PLUG_FILES_E_CARD;
                break;
            }
            for (uint32_t i = 0; i < k; i++) dst[done + i] = waveSample(b + i * f.align, f);
            done += k;
        }
    }
    fs.close();
    return r;
}

// plug_ext_files.read_wave on an index of the list: frames of the file or PLUG_FILES_E_*
template <class Fs>
int32_t waveRead(Fs& fs, const WaveList& w, uint32_t index, uint32_t first, int16_t* dst, uint32_t max, const char* base = kWavesDir)
{
    if (!w.built || index >= uint32_t(w.n)) return PLUG_FILES_E_NONE;
    char p[PLUG_FILES_PATH + 16];
    snprintf(p, sizeof(p), "%s/%s", base, w.at(int(index)));
    return waveReadFile(fs, p, first, dst, max, PLUG_FILES_E_CARD);
}

// A path that names a file in a subfolder ("AKWF_bw_sin/AKWF_sin_0001.wav") also reaches the same file name directly in /Waves, the flat folder
// of the release zip (AKWF names are unique across the whole set, so the base name names one wave). The exact path wins. Returns the
// base name part of rel when rel has a folder and the base name is a valid path itself, else nullptr.
inline const char* waveFlatName(const char* rel)
{
    const char* s = strrchr(rel, '/');
    return s && plug::wavePathOk(s + 1) ? s + 1 : nullptr;
}

// plug_ext_files.read_path: the same on the file `rel` (wavePathOk, relative to /Waves). PLUG_FILES_E_NONE for a path that is not valid or
// not on the card, the list is not involved. A file that is not at its path is looked for by its base name directly in /Waves (waveFlatName).
template <class Fs>
int32_t waveReadPath(Fs& fs, const char* rel, uint32_t first, int16_t* dst, uint32_t max, const char* base = kWavesDir)
{
    if (!plug::wavePathOk(rel)) return PLUG_FILES_E_NONE;
    char p[PLUG_FILES_PATH + 16];
    snprintf(p, sizeof(p), "%s/%s", base, rel);
    const int32_t r = waveReadFile(fs, p, first, dst, max, PLUG_FILES_E_NONE);
    if (r != PLUG_FILES_E_NONE) return r;
    const char* flat = waveFlatName(rel);
    if (!flat) return r;
    snprintf(p, sizeof(p), "%s/%s", base, flat);
    return waveReadFile(fs, p, first, dst, max, PLUG_FILES_E_NONE);
}

// True when the list holds the file `rel` (exact path) or, for a path with a folder, a file of that base name directly in /Waves, the same two
// lookups waveReadPath makes. A full list (PLUG_FILES_MAX entries) may not hold every file, so it answers true: the label never calls a file missing
// that might be there.
inline bool waveListHas(const WaveList& w, const char* rel)
{
    if (w.n >= int(PLUG_FILES_MAX)) return true;
    const char* flat = waveFlatName(rel);
    for (int pass = 0; pass < 2; pass++)
    {
        const char* key = pass == 0 ? rel : flat;
        if (!key) break;
        int lo = 0, hi = w.n - 1;
        while (lo <= hi)
        {
            const int mid = (lo + hi) / 2;
            const int c = strcmp(w.at(mid), key);
            if (c == 0) return true;
            if (c < 0) lo = mid + 1; else hi = mid - 1;
        }
    }
    return false;
}

// plug_ext_files.next_path: the path that follows `cur` in the order of the list (strcmp of the relative paths) among ALL the wave files of /Waves
// and of its subfolders one level down: no cap and no table, so a card with 4,358 files answers like one with ten. `cur` need not be on the card.
// Returns the length of the path written to out (cap bytes, cut to fit), PLUG_FILES_E_NONE when no path follows or `cur` is not valid.
// Method: the path order is the order of the folder keys "<folder>/" (a root file has no '/'), and every path of a folder is above its key, so
// the answer is the smaller of the best root file and the first non empty folder whose key is above `cur` (or is `cur`'s own folder). Each
// folder candidate is one pass over /Waves (the walker holds one directory) and one over that folder: a few passes for a normal card, a pass per
// empty folder in the worst case. `name` is the walker's scratch (kWaveName bytes).
template <class Fs>
int32_t waveNext(Fs& fs, char* name, const char* cur, char* out, uint32_t cap, const char* base = kWavesDir)
{
    if (!plug::wavePathOk(cur)) return PLUG_FILES_E_NONE;
    const char* slash = strchr(cur, '/');
    const size_t curKeyLen = slash ? size_t(slash - cur) + 1 : 0;   // length of cur's folder key "<folder>/", 0 for a root file
    char best[PLUG_FILES_PATH];
    char key[PLUG_FILES_PATH];       // the folder tried next
    char last[PLUG_FILES_PATH];      // the folder tried before
    char dir[PLUG_FILES_PATH + 16];
    char p[PLUG_FILES_PATH];
    best[0] = 0;
    last[0] = 0;
    bool haveBest = false;
    bool isDir = false, hidden = false;
    for (int round = 0; round < kWaveFolders + 1; round++)
    {
        // One pass over /Waves: the root files above cur (first round only) and the smallest relevant folder key above `last`
        key[0] = 0;
        if (!fs.dirOpen(base)) break;
        while (fs.dirNext(name, isDir, hidden))
        {
            name[kWaveName - 1] = 0;
            if (hidden || name[0] == '.') continue;
            const size_t len = strlen(name);
            if (isDir)
            {
                if (len + 6 >= PLUG_FILES_PATH) continue;
                memcpy(p, name, len);
                p[len] = '/';
                p[len + 1] = 0;
                const bool own = curKeyLen && strncmp(p, cur, curKeyLen) == 0 && strlen(p) == curKeyLen;
                if (!(own || strcmp(p, cur) > 0)) continue;       // every path of the folder is below cur
                if (round && strcmp(p, last) <= 0) continue;      // tried already
                if (!key[0] || strcmp(p, key) < 0) memcpy(key, p, len + 2);
            }
            else if (round == 0 && waveName(name) && len < PLUG_FILES_PATH && strcmp(name, cur) > 0)
            {
                if (!haveBest || strcmp(name, best) < 0) memcpy(best, name, len + 1);
                haveBest = true;
            }
        }
        fs.dirClose();
        if (!key[0]) break;                                       // no folder left
        if (haveBest && strcmp(best, key) < 0) break;             // the root file is below every path of the folder
        const size_t kl = strlen(key);
        snprintf(dir, sizeof(dir), "%s/%.*s", base, int(kl - 1), key);
        bool found = false;
        if (fs.dirOpen(dir))
        {
            while (fs.dirNext(name, isDir, hidden))
            {
                name[kWaveName - 1] = 0;
                if (isDir || hidden || !waveName(name)) continue;
                const size_t len = strlen(name);
                if (kl + len >= PLUG_FILES_PATH) continue;
                memcpy(p, key, kl);
                memcpy(p + kl, name, len + 1);
                if (strcmp(p, cur) <= 0) continue;
                found = true;
                if (!haveBest || strcmp(p, best) < 0)
                {
                    memcpy(best, p, strlen(p) + 1);   // the smallest of this folder, or the root file when that is smaller
                    haveBest = true;
                }
            }
            fs.dirClose();
        }
        if (found) break;                                         // later folders have larger keys, so larger paths
        memcpy(last, key, kl + 1);
    }
    if (!haveBest) return PLUG_FILES_E_NONE;
    if (out && cap) snprintf(out, cap, "%s", best);
    return int32_t(strlen(best));
}

}   // namespace plug
