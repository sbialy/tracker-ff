// Parts: playback, assembly and bake. See parts.h. The list is stored in project.mtff (synth-open/plug_host.cpp, plug_mtff.h).
#ifdef FEATURE_PARTS

#include "parts.h"

#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fx34.h"
#include "mtSequencer.h"
#include "mtStructs.h"

#include "core/interface.h"
#include "core/interfacePopups.h"
#include "patternEditor/patternEditor.h"
#include "performanceMode/performanceMode.h"


#include <cr_section_macros.h>
#include <stdio.h>
#include <string.h>

extern Sequencer::strPattern songTrackCopy[2];   // fileManagerSong.cpp: main loop scratch of the Song screen track copy

namespace parts
{
namespace
{
// SDRAM, no static constructor (the SDRAM is not up when constructors run): filled by projectLoaded before any use
__NOINIT(EXTERNAL_RAM) List s_list;

// SRAM, zero at boot
bool s_ready;                       // s_list holds a project's list
char s_note[48];

// playback, shared with the audio interrupt
volatile bool s_active;             // a Part image is the playing pattern
volatile uint8_t s_cur;             // the Part that plays
volatile uint8_t s_passLeft;        // its passes still to play, this one included
volatile int16_t s_queued = -1;
volatile bool s_queueAtPass;
volatile bool s_nextReady;          // the buffer pattern holds Part s_nextIdx
volatile uint8_t s_nextIdx;
volatile bool s_nextWraps;          // s_nextIdx is the first Part after the end of the song
volatile uint32_t s_gen;            // bumped when the buffer is swapped in or the next Part changes: the assembly restarts
uint32_t s_late;                    // Parts that repeated once more because the next image was not ready (diagnostics)
uint16_t s_written;                 // pattern files written by live edits (diagnostics)
uint16_t s_saveCalls;               // storeEdits() calls, the Workspace save while a Part plays (diagnostics)

// assembly (main loop)
uint32_t s_asmGen = ~0u;
int16_t s_asmTarget = -1;
bool s_asmWraps;
uint16_t s_asmDone;                 // tracks already in the image
uint8_t s_asmLen;                   // Part length in steps, 0 until the first source is read

// live edits (write-through, ref/parts-live.md). The pattern editor edits the playing image in place, as stock edits the playing
// pattern, so an edit is heard at once. Its steps go back to the source patterns from here, main loop only: a per track checksum of
// each image as built says which tracks were edited, a diff against the source file says which steps
struct Image
{
    Sequencer::strPattern* pat;     // one of the sequencer's two pattern buffers
    Part part;                      // the Part it was built from (a copy: a list edit while it plays does not move its sources)
    uint8_t start[kTracks];         // and the start steps of its tracks (the same copy)
    uint8_t len;                    // its length in steps
    bool valid;                     // pat holds that image (false while an assembly or a stock load writes the buffer)
    uint32_t sum[kTracks];          // per track checksum as built or last stored
};
__NOINIT(EXTERNAL_RAM) Image s_img[2];
volatile bool s_prevPending;        // the image that stopped playing (now the buffer) still has to be compared with its sources
bool s_editPending;                 // the pattern editor changed the playing image
uint32_t s_editAt;                  // ms of that change
constexpr uint32_t kSettleMs = 300; // edits are stored this long after the last one (the next Part hears them, tiled copies follow)

struct Irq
{
    Irq() { DISABLE_GLOBAL_INTERRUPTS(); }
    ~Irq() { ENABLE_GLOBAL_INTERRUPTS(); }
};

uint32_t nowMs()
{
    return mtInterface.getPlatform()->getTimer()->millis();
}

void setNote(const char* t)
{
    snprintf(s_note, sizeof(s_note), "%s", t);
}

// ---- assembly

bool readSource(uint8_t pattern, Sequencer::strPattern* dst)
{
    if (!newFileManager.readWorkspacePatternTo(pattern, dst))
    {
        sequencer.clearPattern(dst);   // a missing or damaged pattern plays as empty tracks (as the Performance loader does)
        fx34::clearAll(fx34::setOf(dst));
        return false;
    }
    return true;
}

void copyTrack(Sequencer::strPattern* dst, const Sequencer::strPattern* src, int t, int len, bool tile, int start)
{
    // features-140 (idea-polymeter): a marked source tiles each track at its own length, an unmarked one at the pattern length
    const int srcLen = Sequencer::getTrackLastStep(src, t) + 1;
    // FX3 and FX4 (the 4 FX switch) go with their steps. The 256 entries of an image are shared by all its tracks: a full set says so
    fx34::Set* const dx = fx34::setOf(dst);
    const fx34::Set* const sx = fx34::setOf(src);
    fx34::clearTrack(dx, uint8_t(t));
    for (int s = 0; s <= Sequencer::MAXSTEP; s++)
    {
        const int k = sourceStep(s, srcLen, len, tile, start);
        if (k < 0) sequencer.clearStep(&dst->track[t].step[s]);
        else
        {
            dst->track[t].step[s] = src->track[t].step[k];
            if (dx && fx34::copyStep(dx, uint8_t(t), uint8_t(s), sx, uint8_t(t), uint8_t(k)) == fx34::kFull)
                setNote("FX3/FX4 limit of 256 reached in a Part");
        }
    }
}

void clearTrack(Sequencer::strPattern* dst, int t)
{
    for (int s = 0; s <= Sequencer::MAXSTEP; s++) sequencer.clearStep(&dst->track[t].step[s]);
    fx34::clearTrack(fx34::setOf(dst), uint8_t(t));
}

// One step of building Part `p` into dst: the next source pattern is read and every track that names it is copied. done: tracks
// in the image so far (0 at the start), len: the Part length (0 at the start). True when the image is complete.
bool assembleStep(const Part& p, const uint8_t* st, Sequencer::strPattern* dst, uint16_t& done, uint8_t& len)
{
    Sequencer::strPattern* scratch = &songTrackCopy[0];
    int first = -1;
    for (int t = 0; t < kTracks && t < kAllTracks; t++)
        if (!(done & (1u << t)) && p.src[t] != 0)
        {
            first = t;
            break;
        }
    if (first < 0)
    {
        if (len == 0)
        {   // no source at all: an empty image of the Part length (default 16 steps)
            sequencer.clearPattern(dst);
            fx34::clearAll(fx34::setOf(dst));
            len = p.length ? p.length : 16;
        }
        for (int t = 0; t < kAllTracks; t++)
            if (t >= kTracks || p.src[t] == 0) clearTrack(dst, t);
        dst->track[0].length = uint8_t(len - 1);
        sequencer.updateTrackLengths(dst);
        return true;
    }
    const uint8_t pat = p.src[first];
    readSource(pat, scratch);
    if (len == 0)
    {   // the first source gives the header, swing and (length 0) the length
        memcpy(dst, scratch, sizeof(*dst));
        fx34::copySet(fx34::setOf(dst), fx34::setOf(scratch));
        // Auto: the window of the first source (its steps from the start step of that track on)
        len = p.length ? p.length : uint8_t(windowLen(scratch->track[0].length + 1, st[first]));
        partsNoteSourceLength(pat, uint8_t(scratch->track[0].length + 1));
        if (len > kMaxLength) len = kMaxLength;
        // features-140: the polymeter mark of the first source does not describe the other sources' tracks. The image plays every
        // track at the Part length, the per-track lengths are already in the tiling (copyTrack), so the image is unmarked
        dst->rezerwa1 = 0;
        dst->rezerwa2 = 0;
        dst->rezerwa3 = 0;
    }
    for (int t = first; t < kTracks && t < kAllTracks; t++)
        if (!(done & (1u << t)) && p.src[t] == pat)
        {
            copyTrack(dst, scratch, t, len, p.flags & kFlagTile, st[t]);
            done |= uint16_t(1u << t);
        }
    return false;
}

void assembleAll(const Part& p, const uint8_t* st, Sequencer::strPattern* dst)
{
    uint16_t done = 0;
    uint8_t len = 0;
    while (!assembleStep(p, st, dst, done, len)) {}
}

Image* imageOf(const Sequencer::strPattern* p)
{
    for (Image& i : s_img)
        if (i.pat == p) return &i;
    for (Image& i : s_img)
        if (!i.pat)
        {
            i.pat = const_cast<Sequencer::strPattern*>(p);
            return &i;
        }
    return nullptr;
}

uint32_t trackSum(const Sequencer::strPattern* p, int t, int len)
{
    uint32_t h = 2166136261u;
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&p->track[t].step[0]);
    for (size_t i = 0, n = size_t(len) * sizeof(p->track[t].step[0]); i < n; i++) h = (h ^ b[i]) * 16777619u;
    const fx34::Set* x = fx34::setOf(p);
    if (fx34::count(x))
        for (int s = 0; s < len; s++)
        {
            fx34::Fx f[2];
            if (fx34::getStep(x, uint8_t(t), uint8_t(s), f))
                h = (h ^ (uint32_t(s) << 24 | f[0].type << 16 | f[0].value << 8 | f[1].type) ^ (uint32_t(f[1].value) << 4)) * 16777619u;
        }
    return h;
}

void markBuilt(Sequencer::strPattern* p, const Part& part, const uint8_t* st)
{
    Image* i = imageOf(p);
    if (!i) return;
    i->part = part;
    memcpy(i->start, st, sizeof(i->start));
    i->len = uint8_t(p->track[0].length + 1);
    for (int t = 0; t < kTracks; t++) i->sum[t] = trackSum(p, t, i->len);
    i->valid = true;
}

void invalidate(const Sequencer::strPattern* p)
{
    if (Image* i = imageOf(p)) i->valid = false;
}

bool stepEqual(const Sequencer::strPattern* a, int t, int sa, const Sequencer::strPattern* b, int sb)
{
    if (memcmp(&a->track[t].step[sa], &b->track[t].step[sb], sizeof(a->track[t].step[0])) != 0) return false;
    fx34::Fx fa[2], fb[2];
    fx34::getStep(fx34::setOf(a), uint8_t(t), uint8_t(sa), fa);
    fx34::getStep(fx34::setOf(b), uint8_t(t), uint8_t(sb), fb);
    return memcmp(fa, fb, sizeof(fa)) == 0;
}

void copyStep(Sequencer::strPattern* dst, int t, int sd, const Sequencer::strPattern* src, int ss)
{
    dst->track[t].step[sd] = src->track[t].step[ss];
    fx34::copyStep(fx34::setOf(dst), uint8_t(t), uint8_t(sd), fx34::setOf(src), uint8_t(t), uint8_t(ss));
}

// The source steps in `mask` (track t of pattern `pat`, now `src`) into an image built from Part `part` with length `len`
void patchSteps(Sequencer::strPattern* dst, const Part& part, int start, int len, int t, uint8_t pat, const uint32_t* mask,
                const Sequencer::strPattern* src, int srcLen)
{
    if (part.src[t] != pat) return;
    for (int s = 0; s < len; s++)
    {
        const int k = sourceStep(s, srcLen, len, part.flags & kFlagTile, start);
        if (k >= 0 && (mask[k >> 5] >> (k & 31) & 1)) copyStep(dst, t, s, src, k);
    }
}

// The changed steps of a source track into every image that plays it: the playing one (tiled copies), the next one (ready, or the
// tracks an assembly already copied). A ready next image is held back while it is patched, so the pass end never swaps in half of it
void patchImages(int t, uint8_t pat, const uint32_t* mask, const Sequencer::strPattern* src, int srcLen)
{
    for (Image& i : s_img)
    {
        if (!i.valid) continue;
        bool held = false;
        {
            Irq irq;
            if (i.pat == sequencer.getPatternToLoadFromFile() && s_nextReady)
            {
                s_nextReady = false;
                held = true;
            }
        }
        patchSteps(i.pat, i.part, i.start[t], i.len, t, pat, mask, src, srcLen);
        i.sum[t] = trackSum(i.pat, t, i.len);
        if (held) s_nextReady = true;
    }
    if (s_asmTarget >= 0 && s_asmLen && (s_asmDone >> t & 1) && !imageOf(sequencer.getPatternToLoadFromFile())->valid)
        patchSteps(sequencer.getPatternToLoadFromFile(), s_list.part[s_asmTarget], s_list.start[s_asmTarget][t], s_asmLen, t, pat, mask,
                   src, srcLen);
}

// The edits of image `im` into its source patterns: every track whose checksum moved is compared step by step with its source
// (the first copy of a source step that differs is the edit), the changed steps go to the pattern file and into the other images.
// Steps with no source step (a track that is off, past a source that does not tile) cannot be stored and stay image only.
// Returns the number of pattern files written, -1 when a write failed
int storeImage(Image* im)
{
    if (!im || !im->valid) return 0;
    Sequencer::strPattern* const img = im->pat;
    Sequencer::strPattern* const scratch = &songTrackCopy[0];
    uint16_t edited = 0;
    for (int t = 0; t < kTracks; t++)
        if (im->part.src[t] && trackSum(img, t, im->len) != im->sum[t]) edited |= uint16_t(1u << t);
    int written = 0;
    for (int t = 0; t < kTracks && edited; t++)
    {
        if (!(edited >> t & 1)) continue;
        const uint8_t pat = im->part.src[t];
        readSource(pat, scratch);   // missing: an empty pattern, as the Part played it
        bool any = false;
        for (int u = t; u < kTracks; u++)
        {
            if (!(edited >> u & 1) || im->part.src[u] != pat) continue;
            edited &= uint16_t(~(1u << u));
            uint32_t mask[4] = {};
            const int srcLen = Sequencer::getTrackLastStep(scratch, u) + 1;
            // the source steps the track plays are its window start..srcLen-1, image step s shows source step start + s (tiled by the window)
            const int st = im->start[u] < srcLen ? im->start[u] : 0, win = srcLen - st;
            for (int k = st; k < srcLen; k++)
                for (int s = k - st; s < im->len; s += win)
                {
                    if (!stepEqual(img, u, s, scratch, k))
                    {
                        copyStep(scratch, u, k, img, s);
                        mask[k >> 5] |= 1u << (k & 31);
                        break;
                    }
                    if (!(im->part.flags & kFlagTile)) break;
                }
            if (mask[0] | mask[1] | mask[2] | mask[3])
            {
                any = true;
                patchImages(u, pat, mask, scratch, srcLen);
            }
            im->sum[u] = trackSum(img, u, im->len);
        }
        if (!any) continue;
        if (!newFileManager.writeWorkspacePatternFrom(pat, scratch))
        {
            setNote("pattern write failed");
            return -1;
        }
        if (newFileManager.updatePatternBitmask(pat, scratch)) newFileManager.setProjectStructChanged();
        mtProject.values.projectNotSavedFlag |= 1;
        written++;
        s_written++;
    }
    if (written) newFileManager.clearPatternUndo();   // the undo snapshots predate what the files hold now
    return written;
}

// a card operation that reads or replaces pattern files is under way: the edits wait (an autosave of the Workspace is fine)
bool cardBusy()
{
    const uint8_t op = newFileManager.getCurrentOperation();
    return op != fmNoOperation && op != fmSaveWorkspaceProject;
}

// The image that stopped playing (the buffer now) goes to its sources before anything else writes the buffer. A stock pattern switch
// that loads into the buffer (a pattern change while the Part plays) wins: its edits are dropped, never compared with another pattern
void storePrevious()
{
    if (!s_prevPending) return;
    if (sequencer.ptrPlayer->sequencialSwitch_isArmed)
    {
        s_prevPending = false;
        invalidate(sequencer.getPatternToLoadFromFile());
        return;
    }
    if (cardBusy()) return;
    s_prevPending = false;
    if (!s_active) invalidate(sequencer.getActualPattern());   // a stock switch put the user's pattern there: never patched
    Image* const im = imageOf(sequencer.getPatternToLoadFromFile());
    storeImage(im);
    if (im) im->valid = false;   // the next assembly writes it
}

void storeCurrent()
{
    s_editPending = false;
    Image* im;
    {
        Irq irq;
        im = imageOf(sequencer.getActualPattern());
    }
    storeImage(im);
}

// The Part to preload after the current one: the queued one, else the next in song order. After the last Part (or an End Part) the song
// stops, unless it loops (the flag of Part 0): then Part 0 is next
int target(bool& wraps)
{
    wraps = false;
    if (s_queued >= 0 && s_queued < s_list.count) return s_queued;
    int n = nextIndex(s_list, s_cur);
    if (n < 0 && songLoops(s_list))
    {
        wraps = true;
        n = partIsEnd(s_list.part[0]) ? -1 : 0;
    }
    return n;
}

void finishPlayback()
{
    if (!cardBusy())
    {   // the edits of the last images go to their patterns before the user's pattern is loaded back
        storePrevious();
        storeCurrent();
    }
    s_prevPending = false;
    s_editPending = false;
    s_img[0].valid = s_img[1].valid = false;
    s_active = false;
    s_nextReady = false;
    s_queued = -1;
    // the playing pattern buffer held the Part image: the user's pattern comes back (nothing of the image is ever stored)
    newFileManager.loadWorkspacePatternNow(mtProject.values.actualPattern);
    Irq irq;
    sequencer.switchRamPatternsNow();
}

}   // namespace

List& list()
{
    if (!s_ready)
    {   // before the first project load (nothing reads the list then, but the SDRAM holds garbage)
        memset(&s_list, 0, sizeof(s_list));
        memset(s_img, 0, sizeof(s_img));
        s_ready = true;
    }
    return s_list;
}

bool enabled()
{
    return s_ready && s_list.mode == kModeParts && parts::songLength() > 0;
}

void changed()
{
    newFileManager.setProjectStructChanged();   // the Workspace save writes the pat.part. keys of project.mtff (plug::projectSave)
    if (s_active)
    {
        Irq irq;
        if (s_cur >= s_list.count) s_cur = uint8_t(s_list.count ? s_list.count - 1 : 0);
        if (s_queued >= s_list.count) s_queued = -1;
        s_nextReady = false;
        s_gen++;
    }
}

const char* note()
{
    return s_note;
}

// ------------------------------------------------------------------------------------------------ project files
// A project load (or a new project, or an import that starts one): the playback state and the list are emptied, then plug::projectLoaded
// fills the list from the pat.part. keys of project.mtff (it runs after this, see WorkspaceProjectLoad.cpp)
void projectLoaded(const char* /*workspaceDir*/, bool /*newProject*/)
{
    if (s_active)
    {   // a project load stops the sequencer first: drop the Part state without touching the new project's pattern
        s_active = false;
        s_nextReady = false;
    }
    s_queued = -1;
    s_prevPending = false;
    s_editPending = false;
    memset(&s_list, 0, sizeof(s_list));
    memset(s_img, 0, sizeof(s_img));
    partsNoteSourceLength(0, 0);
    s_ready = true;
    s_note[0] = 0;
}

// ------------------------------------------------------------------------------------------------ playback
bool playing()
{
    return s_active;
}

int currentPart()
{
    return s_active ? s_cur : -1;
}

int passesLeft()
{
    return s_active ? s_passLeft : 0;
}

int queuedPart()
{
    return s_active ? s_queued : -1;
}

int progressPos()
{
    return s_active ? s_cur : 0;
}

int songLength()
{
    return parts::songLength(list());
}

bool start(int from, bool loop)
{
    List& l = list();
    if (from < 0 || from >= l.count || partIsEnd(l.part[from]))
    {
        mtPopups.showInfoPopup("Nothing to play here", "add a Part or pick one that is not End");
        return false;
    }
    if (sequencer.isPerformanceMode())
    {
        mtPopups.showInfoPopup("Parts do not play", "in Performance mode");
        return false;
    }
    if (sequencer.isPlay()) sequencer.stop();
    if (s_active) finishPlayback();

    // pattern edits not stored yet go to their file first (as stock song play does), then edits are off
    newFileManager.saveWorkspacePatternNow(mtProject.values.actualPattern);
    if (sequencer.isRec()) sequencer.recEnable(false);
    if (patternEditor.editMode)
    {
        patternEditor.editMode = 0;
        patternEditor.refreshEditStateFromOutsideModule();
    }

    newFileManager.forgetPreloadedPattern(); // the buffer holds a Part image now, not the pattern a load left in it
    assembleAll(l.part[from], l.start[from], sequencer.getPatternToLoadFromFile());
    markBuilt(sequencer.getPatternToLoadFromFile(), l.part[from], l.start[from]);
    {
        Irq irq;
        sequencer.switchToPartImage();
        s_cur = uint8_t(from);
        s_passLeft = l.part[from].repeats;
        s_queued = -1;
        s_nextReady = false;
        s_gen++;
        s_active = true;
    }
    invalidate(sequencer.getPatternToLoadFromFile());   // the user's pattern, back after the playback
    sequencer.playPartImage(!loop);
    return true;
}

void update()
{
    storePrevious();   // also after a stock pattern switch ended the Parts playback (imageReplaced)
    if (!s_active) return;
    if (!sequencer.isPlay())
    {
        finishPlayback();
        return;
    }
    if (s_prevPending) return;   // the buffer still holds the image that just played, with edits not stored yet
    if (s_editPending && nowMs() - s_editAt >= kSettleMs && !cardBusy()) storeCurrent();
    // a stock pattern switch is on its way into the buffer (a pattern change while the Part plays): leave the buffer to it, and the
    // image there (ready or not) is no image any more
    if (sequencer.ptrPlayer->sequencialSwitch_isArmed)
    {
        invalidate(sequencer.getPatternToLoadFromFile());
        return;
    }
    if (s_nextReady) return;
    bool restart;
    {
        Irq irq;
        restart = s_asmGen != s_gen;
        if (restart)
        {
            s_asmGen = s_gen;
            s_asmTarget = int16_t(target(s_asmWraps));
            s_asmDone = 0;
            s_asmLen = 0;
        }
    }
    if (restart) invalidate(sequencer.getPatternToLoadFromFile());
    if (s_asmTarget < 0) return;   // nothing to play after this Part: it repeats
    newFileManager.forgetPreloadedPattern();
    const bool done = assembleStep(s_list.part[s_asmTarget], s_list.start[s_asmTarget], sequencer.getPatternToLoadFromFile(), s_asmDone,
                                   s_asmLen);
    if (!done) return;
    markBuilt(sequencer.getPatternToLoadFromFile(), s_list.part[s_asmTarget], s_list.start[s_asmTarget]);
    Irq irq;
    if (s_asmGen == s_gen)
    {
        s_nextIdx = uint8_t(s_asmTarget);
        s_nextWraps = s_asmWraps;
        s_nextReady = true;
    }
}

void queue(int at, bool atPassEnd)
{
    if (!s_active || at < 0 || at >= s_list.count) return;
    Irq irq;
    s_queued = int16_t(at);
    s_queueAtPass = atPassEnd;
    s_nextReady = false;
    s_gen++;
}

bool onPassEnd(bool& swap, bool exporting)
{
    swap = false;
    if (!s_active) return true;
    const bool queued = s_queued >= 0;
    if (!(queued && s_queueAtPass) && s_passLeft > 1)
    {   // another pass of the same image: pass counters go on (Chance and the other pass FX count across repeats)
        s_passLeft--;
        return true;
    }
    if (!queued && nextIndex(s_list, s_cur) < 0 && (exporting || !songLoops(s_list)))
        return false;   // the last Part ended: the song stops here (an export never loops)
    if (!s_nextReady)
    {   // the next image is not built yet: one more pass of this Part, the screen shows it (passesLeft stays 1)
        s_late++;
        return true;
    }
    swap = true;
    s_prevPending = true;   // the image that played is the buffer after the swap: update() stores its edits before reusing it
    const bool songEnd = s_nextWraps;
    s_cur = s_nextIdx;
    s_passLeft = s_list.part[s_cur].repeats ? s_list.part[s_cur].repeats : 1;
    if (s_queued == s_cur) s_queued = -1;
    s_nextReady = false;
    s_gen++;
    return !songEnd;
}

void imageReplaced()
{
    if (s_active) s_prevPending = true;   // the Part image is the buffer now: its edits go to the sources (update)
    s_active = false;
    s_nextReady = false;
    s_queued = -1;
}

uint32_t diag(int what)
{
    return what == 0 ? s_late : what == 1 ? s_written : s_saveCalls;
}

void imageEdited()
{
    if (!s_active) return;
    s_editPending = true;
    s_editAt = nowMs();
}

void storeEdits()
{
    s_saveCalls++;
    if (cardBusy()) return;
    storePrevious();
    if (s_active) storeCurrent();
}

// ------------------------------------------------------------------------------------------------ bake (option C)
namespace
{
bool patternUsed(uint8_t pattern)
{
    const uint8_t i = pattern - 1;
    if (mtProject.values.allPatternsBitmask[i] != 0 || mtProject.values.allPatternsBitmaskMidi[i] != 0) return true;
    if (pattern == mtProject.values.actualPattern) return true;
    for (int s = 0; s < SONG_MAX && mtProject.song.playlist[s]; s++)
        if (mtProject.song.playlist[s] == pattern) return true;
    for (int k = 0; k < s_list.count; k++)
        for (int t = 0; t < kTracks; t++)
            if (s_list.part[k].src[t] == pattern) return true;
    return false;
}
}   // namespace

int bake()
{
    List& l = list();
    const int len = parts::songLength(l);
    if (len == 0)
    {
        setNote("no Parts to bake");
        return -1;
    }
    if (sequencer.isPlay())
    {
        setNote("stop the playback first");
        return -1;
    }
    int slots = 0;
    for (int i = 0; i < len; i++) slots += l.part[i].repeats;
    if (slots > SONG_MAX - 1)
    {   // the playlist ends at its first 0, so 254 slots at most
        setNote("song too long for the playlist");
        return -1;
    }
    newFileManager.saveWorkspacePatternNow(mtProject.values.actualPattern);

    // one pattern per distinct Part (equal records share it). A Part baked before keeps its number when that pattern is still
    // not used by anything else, else the next free pattern is taken
    uint8_t number[kMax] = {};
    int fresh = 0;
    uint8_t nextFree = 1;
    for (int i = 0; i < len; i++)
    {
        Part key = l.part[i];
        key.baked = 0;
        key.flags &= uint8_t(~kFlagLoop);   // the song property of Part 0 is not part of what a Part plays
        for (int j = 0; j < i && !number[i]; j++)
        {
            Part other = l.part[j];
            other.baked = 0;
            other.flags &= uint8_t(~kFlagLoop);
            if (memcmp(&key, &other, sizeof(key)) == 0 && memcmp(l.start[i], l.start[j], kTracks) == 0) number[i] = number[j];
        }
        if (number[i]) continue;
        uint8_t n = l.part[i].baked;
        bool reuse = n != 0;
        for (int j = 0; j < i && reuse; j++)
            if (number[j] == n) reuse = false;
        if (reuse)
        {   // the pattern of the last bake: only if nothing but that bake uses it (its bitmask says nothing about who)
            for (int s = 0; s < l.count && reuse; s++)
                for (int t = 0; t < kTracks; t++)
                    if (l.part[s].src[t] == n) reuse = false;
            if (n == mtProject.values.actualPattern) reuse = false;
        }
        if (!reuse)
        {
            n = 0;
            for (int c = nextFree; c <= PATTERN_INDEX_MAX && !n; c++)
            {
                bool taken = patternUsed(uint8_t(c));
                for (int j = 0; j < i && !taken; j++)
                    if (number[j] == c) taken = true;
                if (!taken) n = uint8_t(c);
            }
            if (!n)
            {
                setNote("no free pattern left");
                return -1;
            }
            nextFree = uint8_t(n + 1);
        }
        number[i] = n;
    }

    Sequencer::strPattern* img = &songTrackCopy[1];
    for (int i = 0; i < len; i++)
    {
        bool first = true;
        for (int j = 0; j < i && first; j++)
            if (number[j] == number[i]) first = false;
        l.part[i].baked = number[i];
        if (!first) continue;
        assembleAll(l.part[i], l.start[i], img);
        if (!newFileManager.writeWorkspacePatternFrom(number[i], img))
        {
            setNote("pattern write failed");
            return -1;
        }
        newFileManager.updatePatternBitmask(number[i], img);
        fresh++;
    }
    int s = 0;
    for (int i = 0; i < len; i++)
        for (int r = 0; r < l.part[i].repeats; r++) mtProject.song.playlist[s++] = number[i];
    memset(&mtProject.song.playlist[s], 0, size_t(SONG_MAX - s));
    mtProject.song.playlistPos = 0;
    l.mode = kModePlaylist;   // the Song screen shows and plays the baked playlist now
    changed();

    // a re-bake may have rewritten the pattern on screen
    newFileManager.loadWorkspacePatternNow(mtProject.values.actualPattern);
    {
        Irq irq;
        sequencer.switchRamPatternsNow();
    }
    s_note[0] = 0;
    return fresh;
}

// ------------------------------------------------------------------------------------------------ unlink
// Track `t` of Part `at` as a pattern of its own: the window the track plays in the Part (its steps from the start step on, tiled to the Part
// length, so exactly what the Part sounds like) goes into the first free pattern, the cell points at it and starts at step 1. The new
// pattern holds only that track and has the Part's length. Main loop, sequencer stopped. Returns the pattern number, 0 with note() saying why
int unlink(int at, int t)
{
    List& l = list();
    if (at < 0 || at >= l.count || t < 0 || t >= kTracks || l.part[at].src[t] == 0)
    {
        setNote("nothing to unlink here");
        return 0;
    }
    if (sequencer.isPlay())
    {
        setNote("stop the playback first");
        return 0;
    }
    uint8_t n = 0;
    for (int c = 1; c <= PATTERN_INDEX_MAX && !n; c++)
        if (!patternUsed(uint8_t(c))) n = uint8_t(c);
    if (!n)
    {
        setNote("no free pattern left");
        return 0;
    }
    newFileManager.saveWorkspacePatternNow(mtProject.values.actualPattern);
    Sequencer::strPattern* img = &songTrackCopy[1];
    assembleAll(l.part[at], l.start[at], img);
    for (int u = 0; u < kTracks; u++)
        if (u != t) clearTrack(img, u);
    if (!newFileManager.writeWorkspacePatternFrom(n, img))
    {
        setNote("pattern write failed");
        return 0;
    }
    newFileManager.updatePatternBitmask(n, img);
    mtProject.values.projectNotSavedFlag |= 1;
    l.part[at].src[t] = n;
    l.start[at][t] = 0;
    changed();
    s_note[0] = 0;
    return n;
}

}   // namespace parts

#endif
