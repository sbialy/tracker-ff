#include "fx34_io.h"

#include "fileManager.h"
#include "fileTransfer.h"
#include "fileManagerDefs.h"
#include "mtSequencer.h"
#include "log.h"
#include "tapeFx.h"

#include <cr_section_macros.h>
#include <string.h>

// Two image buffers in the external RAM: the card file being read and the one being compared with it
__NOINIT(EXTERNAL_RAM) static uint8_t s_img[fx34::kFileMax];
__NOINIT(EXTERNAL_RAM) static uint8_t s_img2[fx34::kFileMax];
// Path scratch (a frame of 255 B each would break the 512 B stack ratchet): main loop only, none of the entry points nests another that uses
// the same one (applyProjectSwitch holds C while loadInto uses A)
__NOINIT(EXTERNAL_RAM) static char s_pathA[PATCH_SIZE];
__NOINIT(EXTERNAL_RAM) static char s_pathB[PATCH_SIZE];
__NOINIT(EXTERNAL_RAM) static char s_pathC[PATCH_SIZE];

namespace fx34io
{
namespace
{
unsigned s_parked = 0;

FileSystem* fs()
{
    return newFileManager.getFileSystem();
}

// "...pattern_07.mtp" -> "...pattern_07.mtx" (ext 'x') or "...mtx.old"
bool sidecarPath(const char* mtp, char* out, size_t cap, bool old = false)
{
    const size_t n = strlen(mtp);
    if (n < 5 || n + 5 > cap || memcmp(mtp + n - 4, ".mtp", 4) != 0)
        return false;
    memcpy(out, mtp, n);
    out[n - 1] = 'x';
    if (old)
    {
        memcpy(out + n, ".old", 5);
    }
    else
    {
        out[n] = 0;
    }
    return true;
}

// the whole file into buf, its size, or -1 (absent, too large, a card error)
int readAll(const char* path, uint8_t* buf, size_t cap)
{
    FileSystem* f = fs();
    if (!f->exists(path))
        return -1;
    const size_t size = f->getFileSize(path);
    if (size == 0 || size > cap)
        return -1;
    auto [ok, h] = f->openFile(path);
    if (!ok)
        return -1;
    const int32_t n = f->readFile(buf, size, h);
    f->closeFile(h);
    return n == int32_t(size) ? int(size) : -1;
}

bool writeAll(const char* path, const uint8_t* buf, size_t n)
{
    FileSystem* f = fs();
    if (f->exists(path))
        f->remove(path);
    auto [ok, h] = f->openFile(path, FileSystem::FileOpenMode::kWriteCreateNew);
    if (!ok)
        return false;
    const int32_t w = f->writeFile(buf, n, h);
    f->closeFile(h);
    if (w != int32_t(n))
    {
        f->remove(path);
        return false;
    }
    return true;
}

// the crc a sidecar image is bound to (bytes 8..11), false when it is no sidecar image
bool boundCrc(const uint8_t* img, int n, uint32_t& crc)
{
    if (n < 16 || img[0] != 'F' || img[1] != 'F' || img[2] != 'X' || img[3] != '4' || img[4] != fx34::kFileVersion)
        return false;
    crc = uint32_t(img[8]) | (uint32_t(img[9]) << 8) | (uint32_t(img[10]) << 16) | (uint32_t(img[11]) << 24);
    return true;
}

// the crc stored in the last four bytes of a pattern file, false when there is no file
bool fileCrc(const char* mtp, uint32_t& crc)
{
    FileSystem* f = fs();
    if (!f->exists(mtp))
        return false;
    const size_t size = f->getFileSize(mtp);
    if (size < 8)
        return false;
    auto [ok, h] = f->openFile(mtp);
    if (!ok)
        return false;
    uint8_t t[4];
    bool good = f->seekFile(uint32_t(size - 4), h) && f->readFile(t, 4, h) == 4;
    f->closeFile(h);
    if (good)
        crc = uint32_t(t[0]) | (uint32_t(t[1]) << 8) | (uint32_t(t[2]) << 16) | (uint32_t(t[3]) << 24);
    return good;
}
}   // namespace

unsigned parkedCount()
{
    return s_parked;
}

void clearSet(Pattern* p)
{
    if (fx34::enabled())
        fx34::clearAll(fx34::setOf(p));
}

void projectClosing()
{
    fx34::setEnabled(false);
    fx34::resetAll();
    s_parked = 0;
}

static void loadInto(const char* mtp, Pattern* dest)
{
    fx34::Set* set = fx34::setOfAlways(dest);
    if (!set)
        return;
    set->n = 0;
    char* const sc = s_pathA;
    if (!sidecarPath(mtp, sc, PATCH_SIZE))
        return;
    const int n = readAll(sc, s_img, sizeof(s_img));
    if (n < 0)
        return;
    switch (fx34::decode(s_img, size_t(n), dest->crc, set))
    {
        case fx34::kDecodeOk: break;
        case fx34::kDecodeStale:
            s_parked++;
            LOGEF("FX3/FX4 sidecar parked (the pattern file changed): %s", sc);
            break;
        default:
            s_parked++;
            LOGEF("FX3/FX4 sidecar damaged, not used: %s", sc);
            set->n = 0;
            break;
    }
    // dev projects only: the retired Tape Start id 51 becomes Tape (50) with the value + 64. In RAM only, the next save writes it back
    for (uint16_t i = 0; i < set->n; i++)
    {
        const uint32_t e = set->e[i];
        uint8_t type = fx34::entryType(e), value = fx34::entryValue(e);
        tapefx::migrateSlot(type, value);
        set->e[i] = (e & 0xFFFF0000u) | (uint32_t(type) << 8) | value;
    }
}

void load(const char* mtpPath, Pattern* dest)
{
    if (!fx34::enabled())
        return;
    loadInto(mtpPath, dest);
}

void applyProjectSwitch(bool on)
{
    fx34::setEnabled(false);
    fx34::resetAll();
    if (!on)
        return;
    fx34::setEnabled(true);   // init and clear
    const int idx = mtProject.values.actualPattern;
    if (idx > 0 && idx <= PATTERN_INDEX_MAX)
    {
        snprintf(s_pathC, PATCH_SIZE, cWorkspacePatternFileFormat, idx);
        loadInto(s_pathC, sequencer.getActualPattern());
    }
}

bool turnOn()
{
    if (fx34::enabled())
        return false;
    applyProjectSwitch(true);
    return true;
}

bool keptData()
{
    char* const mtp = s_pathA;
    char* const sc = s_pathB;
    for (int i = 1; i <= PATTERN_INDEX_MAX; i++)
    {
        snprintf(mtp, PATCH_SIZE, cWorkspacePatternFileFormat, i);
        if (sidecarPath(mtp, sc, PATCH_SIZE) && fs()->exists(sc))
            return true;
    }
    return false;
}

bool turnOff()
{
    if (!fx34::enabled())
        return false;
    // the pattern on screen goes to its file with its sidecar while the switch is still on, so turning it back on finds it as it is
    newFileManager.setActualPatternStructChanged();
    newFileManager.saveWorkspacePatternNow(mtProject.values.actualPattern);
    const bool kept = keptData();
    fx34::setEnabled(false);
    return kept;
}

Guard before(const char* mtpPath)
{
    Guard g = { false, false };
    if (!fx34::enabled())
        return g;
    char* const sc = s_pathA;
    if (!sidecarPath(mtpPath, sc, PATCH_SIZE) || !fs()->exists(sc))
        return g;
    g.sidecar = true;
    uint32_t bound, have;
    const int n = readAll(sc, s_img2, sizeof(s_img2));
    g.inSync = n > 0 && boundCrc(s_img2, n, bound) && fileCrc(mtpPath, have) && bound == have;
    return g;
}

void after(const char* mtpPath, const fx34::Set* set, uint32_t crc, const Guard& g)
{
    if (!fx34::enabled() || !set)
        return;
    char* const sc = s_pathA;
    if (!sidecarPath(mtpPath, sc, PATCH_SIZE))
        return;
    FileSystem* f = fs();
    if (set->n == 0)
    {
        if (g.sidecar && g.inSync)
            f->remove(sc);   // the last extra FX was cleared. A parked (stale) sidecar is never touched
        return;
    }
    const size_t n = fx34::encode(set, crc, s_img);
    if (n == 0)
        return;
    if (g.sidecar && !g.inSync)
    {   // a parked sidecar is in the way: it steps aside, one generation
        char* const old = s_pathB;
        if (sidecarPath(mtpPath, old, PATCH_SIZE, true))
        {
            if (f->exists(old))
                f->remove(old);
            f->rename(sc, old);
            LOGEF("FX3/FX4 parked sidecar kept as %s", old);
        }
    }
    else if (g.sidecar)
    {   // unchanged text is not written again
        const int m = readAll(sc, s_img2, sizeof(s_img2));
        if (m == int(n) && memcmp(s_img, s_img2, n) == 0)
            return;
    }
    if (!writeAll(sc, s_img, n))
        LOGEF("FX3/FX4 sidecar not written: %s", sc);
}

void copyFile(const char* srcMtp, const char* dstMtp)
{
    char* const s = s_pathA;
    char* const d = s_pathB;
    if (!sidecarPath(srcMtp, s, PATCH_SIZE) || !sidecarPath(dstMtp, d, PATCH_SIZE))
        return;
    FileSystem* f = fs();
    if (f->exists(s))
    {
        cFileTransfer::copyFile(f, s, d);
    }
    else if (fx34::enabled() && f->exists(d))
    {
        f->remove(d);   // the destination pattern has no extra FX now, an older sidecar there must not come back
    }
}

void copyFromBytes(Pattern* dst, const void* srcBuffer)
{
    if (!fx34::enabled())
        return;
    fx34::copySet(fx34::setOf(dst), fx34::setOf((const Pattern*)srcBuffer));
}

void copyPattern(Pattern* dst, const Pattern* src)
{
    *dst = *src;
    if (fx34::enabled())
        fx34::copySet(fx34::setOf(dst), fx34::setOf(src));
}

void copyTrack(Pattern* dst, uint8_t dTrack, const Pattern* src, uint8_t sTrack)
{
    if (!fx34::enabled())
        return;
    fx34::Set* d = fx34::setOf(dst);
    const fx34::Set* s = fx34::setOf(src);
    if (!d)
        return;
    fx34::clearTrack(d, dTrack);
    if (!s || s->n == 0)
        return;
    for (int st = 0; st <= Sequencer::MAXSTEP; st++)
        fx34::copyStep(d, dTrack, uint8_t(st), s, sTrack, uint8_t(st));
}
}   // namespace fx34io
