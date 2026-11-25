// Capture, the image's half (ref/capture-move-redesign.md, mtCapture.h): the ring in SDRAM, the tap that writes one event per played note,
// and the forwarders of the take logic (mtSequencer_captureMod.cpp). In the release image (FEATURE_CAPTURE_MODULE) the take logic is the Capture
// tenant of /Plugins/Ext/extras.plg: it is loaded with the bundle a few seconds after start (or by the first MOD/IT use, or a Capture press), the
// indicator stays hidden while it is not loaded, and the tap keeps filling the ring meanwhile.
#include "interruptsControl.h"
#include "mtAudioEngine.h"
#include "mtCapture.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "TrackerImpl.h"
#ifdef FEATURE_CAPTURE_MODULE
#include "core/interfacePopups.h"
#include "ext_hooks.h"
#endif

#include <cr_section_macros.h>

bool captureFeatureOn = false;
__NOINIT(EXTERNAL_RAM) CaptureScratch captureScratch;
// SDRAM, no static constructor (a plain struct of integers: nothing runs at start, the sequence number below says what is valid)
__NOINIT(EXTERNAL_RAM) CaptureEvent captureRing[kCaptureRing];
volatile uint32_t captureSeq = 0; // events ever written: the next one goes to captureSeq % kCaptureRing

namespace
{

#ifdef FEATURE_CAPTURE_MODULE
uint8_t s_ticks;   // indicator ticks since start, up to the load try
bool s_tried;
uint32_t call(uint32_t op) { return ext_hooks.capture ? ext_hooks.capture(op) : 0; }
#else
uint32_t call(uint32_t op) { return Sequencer::captureOp(op); }
#endif
} // namespace

uint16_t captureRingCount()
{
    return uint16_t(call(CAPTURE_OP_COUNT));
}

bool captureRingGet(uint16_t back, CaptureEvent* out)
{
    if (back >= captureRingCount())
    {
        return false;
    }
    *out = captureRingAt(captureSeq - 1u - back);
    return true;
}

void captureRingClear()
{
    call(CAPTURE_OP_END);
}

// The one funnel for every played note: Sequencer::handleNoteOn / handleNoteOff for the pads (main loop, which the audio callback can preempt,
// so the push is guarded), mtMidi.cpp for every MIDI path (inside the audio callback itself, no guard needed). Four word stores to SDRAM.
void Sequencer::captureTap(uint8_t note, uint8_t velocity, bool on, uint8_t src)
{
    if (!captureFeatureOn || note > 127)
    {
        return;
    }
    const bool pad = (src & 7u) == kCapSrcPad;
    if (pad)
    {
        DISABLE_AUDIO_INTERRUPTS();
    }
    // the 16 byte event as four words (little endian, the field order of CaptureEvent): every SDRAM access costs about 17 cycles
    typedef uint32_t __attribute__((may_alias)) word_t;
    const uint32_t n = captureSeq;
    word_t* const d = reinterpret_cast<word_t*>(&captureRing[n & (kCaptureRing - 1u)]);
    d[0] = seqClockUs;
    d[1] = uint32_t(note) | (uint32_t((velocity & 0x7F) | (on ? kCapOn : 0)) << 8) | (uint32_t(src) << 16) |
           (uint32_t(mtProject.values.lastUsedInstrument) << 24);
    d[2] = uint32_t(mtProject.values.actualPattern) | (uint32_t(uint8_t(player.globalPos)) << 8) | (uint32_t(uint8_t(player.uStep)) << 16) |
           (uint32_t((isPlay() ? kCapFlagRunning : 0) | (isRec() ? kCapFlagRec : 0)) << 24);
    d[3] = n & 0xFFFFu;
    captureSeq = n + 1u;
    if (pad)
    {
        ENABLE_AUDIO_INTERRUPTS();
    }
}

// Main loop, every 60 ms (Interface::commonThingsUpdate, so on every screen).
uint8_t Sequencer::captureIndicator()
{
    if (!captureFeatureOn)
    {
        return kCapIndHidden;
    }
#ifdef FEATURE_CAPTURE_MODULE
    // the bundle loads once the plugin host's key window is over (about 4.8 s after start): the image id and the card read are a stall of
    // the main loop that should not land on the first played note. A missing or refused file is tried again by a Capture press only.
    if (!ext_hooks.capture && !s_tried && ++s_ticks >= 80)
    {
        s_tried = true;
        ext_modit_check(nullptr, nullptr);
    }
#endif
    return uint8_t(call(CAPTURE_OP_TICK));
}

void Sequencer::captureClear()
{
    if (captureFeatureOn)
    {
        call(CAPTURE_OP_CLEAR);
    }
}

// Main loop, before the USB drive mode lends the card (Interface::handleFileTransferMode): the take file writer (extras.plg) writes what waits
void Sequencer::captureFlushNow()
{
    if (captureFeatureOn)
    {
        call(CAPTURE_OP_FLUSHNOW);
    }
}

#ifdef FEATURE_CAPTURE_MODULE
// Image functions only the take logic calls (extras.plg): referenced here so --gc-sections keeps them (ext_module.cpp keepForModule takes the
// address of this function), never called
void Sequencer::captureKeep()
{
    static volatile uint8_t sink;
    sink = sequencer.recVelocity();
}
#endif

void Sequencer::captureCommit()
{
    if (!captureFeatureOn)
    {
        return;
    }
#ifdef FEATURE_CAPTURE_MODULE
    const char* line1 = nullptr;
    const char* line2 = nullptr;
    int st = 0;
    if (!ext_hooks.capture && ((st = ext_modit_check(&line1, &line2)) != 0 || !ext_hooks.capture))
    {
        if (st == 1)   // ext::ModitState::kMissing: the loader's text names MOD/IT, a Capture press names Capture
        {
            mtPopups.show(0, "Capture needs extras.plg", "Copy it to Plugins/Ext");
        }
        else if (line2)
        {
            mtPopups.show(0, line1, line2);
        }
        else
        {
            mtPopups.show(0, line1 ? line1 : "Capture needs extras.plg");
        }
        return;
    }
#endif
    call(CAPTURE_OP_COMMIT);
}

// ---- the spill (mtCapture.h capspill): the new patterns of a press longer than the pattern, the song slots they take and the one Undo
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "ff_config.h"
#ifdef FEATURE_PARTS
#include "parts.h"
#endif
#include <stdio.h>
#include <string.h>

extern Sequencer::strPattern songTrackCopy[2]; // fileManagerSong.cpp: main loop scratch of the Song screen (it has its extra FX set)

namespace capspill
{
namespace
{
// The group of the last spill: the Undo slot of the first pattern's snapshot (kNoSlot: that snapshot is gone, the block stays), the song slot of
// that pattern, the song length after the press and the new patterns (the block), in song order. SRAM (zero at start, like the Undo ring's
// counters it belongs to)
constexpr uint8_t kNoSlot = 0xFF;
struct Group
{
    uint8_t slot;
    uint8_t pos;
    uint8_t len;
    uint8_t n;     // 0: no group
    uint8_t number[kMaxPatterns - 1];
} s_group;
uint8_t s_lastSlot;

// Pacing of the spill's file writes. A press writes up to 15 pattern files in the main loop, which also refills the sample streams: while it
// writes, the streamed voices drain their rings (371 ms) and drop out. With a loaded audio ISR each pass (compute + write) takes much longer
// (about 85 to 130 ms with 8 streamed voices, 29 ms without), so after a pass that took longer than kPaceOverMs the main loop serves the streams for as
// long as that pass took (at most kPaceMaxMs) before the next file. A project with no streams never reaches the threshold, nothing is added.
constexpr uint32_t kPaceOverMs = 45;
constexpr uint32_t kPaceMaxMs = 400;
uint32_t s_mark; // tickMs() when the last pass ended
uint32_t tickMs() { return uint32_t(TrackerImpl::getInstance()->getPlatform()->getTimer()->millis()); }

uint8_t songLength()
{
    uint8_t n = 0;
    while (n < SONG_MAX && mtProject.song.playlist[n]) n++;
    return n;
}

// the block of the last spill is still in the song after its pattern (the song was not edited since)
bool intact()
{
    const Group& g = s_group;
    const uint8_t* const pl = mtProject.song.playlist;
    bool ok = g.n && songLength() == g.len;
    for (uint8_t k = 0; k < g.n && ok; k++) ok = pl[g.pos + 1 + k] == g.number[k];
    return ok;
}

// the size of the block when it follows the pattern on screen (a press on the same pattern reuses it), else 0
uint8_t block()
{
    return intact() && sequencer.findSlotWithPattern() == s_group.pos ? s_group.n : 0;
}

bool taken(uint8_t c)
{
#ifdef FEATURE_PARTS
    return parts::patternTaken(c);
#else
    if (mtProject.values.allPatternsBitmask[c - 1] || mtProject.values.allPatternsBitmaskMidi[c - 1] || c == mtProject.values.actualPattern) return true;
    for (uint8_t s = 0; s < SONG_MAX && mtProject.song.playlist[s]; s++)
        if (mtProject.song.playlist[s] == c) return true;
    return false;
#endif
}
} // namespace

#ifdef UI_SHOT_HOST
bool paceAlways; // the host test: every pass counts as a slow one (capture_sim.sh counts them), the wait itself is as long as the pass took
uint16_t pacedPasses;
#endif

uint8_t freePatterns(uint8_t want, uint8_t* number)
{
#ifdef FEATURE_PARTS
    if (parts::playing()) return 0;
#endif
    if (sequencer.isPerformanceImageMixed() || sequencer.findSlotWithPattern() < 0) return 0;
    s_mark = tickMs();
    const uint8_t room = uint8_t(SONG_MAX - songLength());
    // a second press on the same pattern writes the same block again (pattern numbers and song slots stay, one more slot or less is no matter)
    const uint8_t old = block();
    memcpy(number, s_group.number, old);
    want = want < room + old ? want : uint8_t(room + old);
    want = want > old ? want : old;
    uint8_t n = old;
    for (int c = PATTERN_INDEX_MIN; c <= PATTERN_INDEX_MAX && n < want; c++)
        if (!taken(uint8_t(c))) number[n++] = uint8_t(c);
    return n;
}

Sequencer::strPattern* scratch(const Sequencer::strPattern* like)
{
    Sequencer::strPattern* p = &songTrackCopy[1];
    sequencer.clearPattern(p);
    p->rezerwa1 = like->rezerwa1; // the polymeter mark and the first track's own length (mtSequencer.h isPolymeter)
    p->rezerwa2 = like->rezerwa2;
    p->rezerwa3 = like->rezerwa3;
    for (int t = 0; t < kAllTracks; t++) p->track[t].length = like->track[t].length;
    return p;
}

void save(uint8_t number, Sequencer::strPattern* p)
{
    newFileManager.writeWorkspacePatternFrom(number, p);
    newFileManager.updatePatternBitmask(number, p);
    uint32_t now = tickMs();
    const uint32_t spent = now - s_mark;
#ifdef UI_SHOT_HOST
    if (paceAlways) pacedPasses++;
    if (spent > kPaceOverMs || paceAlways)
#else
    if (spent > kPaceOverMs)
#endif
    {
        const uint32_t hold = spent < kPaceMaxMs ? spent : kPaceMaxMs;
        while (tickMs() - now < hold) engine.updateSdStream();
        now = tickMs();
    }
    s_mark = now;
}

void land(uint8_t n, const uint8_t* number)
{
    if (!n) return;
    const int16_t pos = sequencer.findSlotWithPattern();
    uint8_t* const pl = mtProject.song.playlist;
    const uint8_t len = songLength();
    const uint8_t old = block(); // the first `old` numbers are in the song already
    memmove(pl + pos + 1 + n, pl + pos + 1 + old, size_t(len - pos - 1 - old));
    memcpy(pl + pos + 1 + old, number + old, n - old);
    s_group = Group{ s_lastSlot, uint8_t(pos), uint8_t(len + n - old), n, {} };
    memcpy(s_group.number, number, n);
    newFileManager.setProjectStructChanged();
    newFileManager.autosaveProjectStruct_ASAP();
}

void undoStored(uint8_t slot)
{
    s_lastSlot = slot;
    if (s_group.slot == slot) s_group.slot = kNoSlot; // the snapshot the group was tied to is overwritten, Undo leaves the block
}

// The new patterns were empty and unused before the press: their files go, their pattern bits clear. The song gets its order back when the
// slots after the pattern still hold the new patterns (the song was not edited since), else the order is left as the user made it
bool undone(uint8_t slot)
{
    Group& g = s_group;
    if (!g.n || g.slot != slot) return false;
    const uint8_t n = g.n;
    const bool run = intact();
    g.n = 0;
    uint8_t* const pl = mtProject.song.playlist;
    const uint8_t len = songLength();
    if (run)
    {
        memmove(pl + g.pos + 1, pl + g.pos + 1 + n, size_t(len - g.pos - 1 - n));
        memset(pl + len - n, 0, n);
        uint8_t& at = mtProject.song.playlistPos;
        at = uint8_t(at > g.pos + n ? at - n : (at > g.pos ? g.pos : at));
    }
    char path[PATCH_SIZE];
    for (uint8_t k = 0; k < n; k++)
    {
        const uint8_t c = g.number[k];
        snprintf(path, sizeof(path), cWorkspacePatternFileFormat, c);
        newFileManager.getFileSystem()->remove(path);
        newFileManager.clearPatternChanged(c);
        mtProject.values.allPatternsBitmask[c - 1] = 0;
        mtProject.values.allPatternsBitmaskMidi[c - 1] = 0;
    }
    newFileManager.setProjectStructChanged();
    newFileManager.autosaveProjectStruct_ASAP();
    return true;
}

void forget()
{
    s_group.n = 0;
}
} // namespace capspill
