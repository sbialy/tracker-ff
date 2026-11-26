#pragma once
// Capture (Move style redesign, tracker-ff, ref/capture-move-redesign.md): every played note goes into a ring of events, always on.
// Two halves:
//  - the image (mtSequencer_capture.cpp): the ring in SDRAM and the tap that writes one event (pads from Sequencer::handleNoteOn in the
//    main loop, MIDI from mtMidi.cpp inside the audio callback), and the three forwarders (indicator tick, commit, clear)
//  - the take logic (mtSequencer_captureMod.cpp): the take and gap rule, the indicator state, the commit. In the release image it is the
//    Capture tenant of the card module /Plugins/Ext/extras.plg (ext_abi.h EXT_SUB_CAPTURE), elsewhere it is linked in.
// There is no idle timeout (Move has none either): a take ends only when a note starts after kCaptureGapSec with no note held, and the old
// take stays capturable until that note.
#include <stdint.h>

constexpr uint16_t kCaptureRing = 1024;                // events, 16 B each (16,384 B of SDRAM)
constexpr uint16_t kCaptureReadMax = kCaptureRing - 8; // the reader stays this far behind the head: a push during a commit never tears an event it reads
constexpr uint32_t kCaptureGapSec = 30;                // decision D3: no note held for this long, the next note on starts a new take
constexpr uint32_t kCaptureLostOffSec = 120;           // a note on this long after the last event of any kind starts a new take even with notes held (a lost note off)

constexpr uint8_t kCapOn = 0x80;        // CaptureEvent::vel bit 7: note on (clear: note off)
constexpr uint8_t kCapFlagRunning = 1;  // CaptureEvent::flags: the transport ran at the event
constexpr uint8_t kCapFlagRec = 2;      // Rec was on at the event (decision D4: still part of the take)

// CaptureEvent::src bits 0..2: where the note came from. Bits 3..6: the destination hint (a track 0..7 for track channel MIDI), 15 = none
enum CaptureSource : uint8_t
{
    kCapSrcPad = 0,
    kCapSrcDin = 1,
    kCapSrcUsb = 2,
    kCapSrcTrackDin = 3, // a per track listening channel (mtMidi.cpp handleTrackMidiNoteOn), the hint is the track
    kCapSrcTrackUsb = 4,
    kCapSrcSynthDin = 5, // the Synth play page (synthplay::noteOn)
    kCapSrcSynthUsb = 6,
};
constexpr uint8_t kCapDestNone = 15;
constexpr uint8_t capSrc(uint8_t source, uint8_t dest = kCapDestNone) { return uint8_t((source & 7u) | ((dest & 15u) << 3)); }

// The 16 B record of the ring, the same layout the take file of phase 2 uses (ref/capture-move-redesign.md 4.4, with the instrument in byte 7
// and source + destination sharing byte 6). Written by the tap as four words.
struct CaptureEvent
{
    uint32_t tUs;       // Sequencer::seqClockUs, only differences are used
    uint8_t note;       // 0..127
    uint8_t vel;        // bit 7 kCapOn, 0..127 velocity
    uint8_t src;        // capSrc()
    uint8_t instrument; // mtProject.values.lastUsedInstrument
    uint8_t pattern;    // mtProject.values.actualPattern
    uint8_t pos;        // player.globalPos at the event
    uint8_t uStep;      // player.uStep at the event, 1..48 (0 before the first tick)
    uint8_t flags;      // kCapFlag*
    uint16_t seq;       // the event number (mod 65,536), what orders the records of a take in the file
    uint8_t take;       // the take id (filled by the take file writer, 0 in the ring)
    uint8_t crc;        // CRC-8 of bytes 0..14 (filled by the take file writer, 0 in the ring)
};
static_assert(sizeof(CaptureEvent) == 16, "the capture ring is 1,024 x 16 B");

// Config > Features > Capture (features_builtin.cpp): listed and on by default on every card and image (no /.develop gate, 2.0.0).
// Off: the tap does nothing and the button 3 of the pattern editor is the stock one.
extern bool captureFeatureOn;

// The always visible indicator in the title bar (Sequencer::captureIndicator, drawn by TitleBar::setCaptureState)
enum CaptureIndicator : uint8_t
{
    kCapIndHidden = 0, // the feature is off, or the take logic (extras.plg) is not loaded
    kCapIndEmpty = 1,  // on, no take (or the long press cleared it): an empty (hollow) square
    kCapIndStale = 2,  // on, a take that a Capture would not land now (Rec is on): dim filled
    kCapIndReady = 3,  // on, a Capture would write the take now: filled in the green of the theme (a press does not use the take up)
    kCapIndGap = 4,    // on, a take older than the gap: the next note starts a new take (decision D9): green outline
};

// The ring of the image, read by the take logic and the host test (ui_shot "capring"). seq counts every event ever written (32 bits), the
// event of number n sits at n % kCaptureRing while seq - n <= kCaptureRing.
extern CaptureEvent captureRing[kCaptureRing];  // SDRAM (mtSequencer_capture.cpp)
extern volatile uint32_t captureSeq;
inline uint32_t captureRingSeq() { return captureSeq; }
inline const CaptureEvent& captureRingAt(uint32_t n) { return captureRing[n & (kCaptureRing - 1u)]; }
uint16_t captureRingCount();                           // events of the current take in the ring (the take logic's view, 0 without it)
bool captureRingGet(uint16_t back, CaptureEvent* out); // back 0 = the newest event of the current take
void captureRingClear();                               // ends the take (the long press, the host test)

// The commit scratch of the take logic (image SDRAM, __NOINIT, so it costs the 4 KB sub-slot of extras.plg nothing): for every event the index
// of the note off that ends it, and the open note on of each note
struct CaptureScratch
{
    uint16_t off[kCaptureRing];
    uint16_t open[128];
};
extern CaptureScratch captureScratch;

// The calls of the take logic (Sequencer::captureOp, ext_hooks.capture): the image forwards them
#define CAPTURE_OP_TICK 0u   // main loop, every 60 ms: returns the CaptureIndicator state
#define CAPTURE_OP_COMMIT 1u // button 3: the take is written into the pattern, popup
#define CAPTURE_OP_CLEAR 2u  // long press of button 3: the take ends, popup
#define CAPTURE_OP_COUNT 3u  // events of the current take in the ring
#define CAPTURE_OP_TAKE 4u   // the take id (1..255, 0: no take yet)
#define CAPTURE_OP_END 5u    // the take ends, no popup (captureRingClear)
#define CAPTURE_OP_FLUSHNOW 6u // before the card is lent to the computer: write every pending event to the take file now (best effort), returns 1 when none is left
#define CAPTURE_OP_FILE 0x100u // + one of capfile::kStat*: a counter of the take file writer (the host tests), the first op of the writer's own range

// The take file (phase 2, ref/capture-move-redesign.md 4.4): /Capture/take.cap on the card. The writer and the reader are the second Capture
// tenant of extras.plg (EXT_SUB_TAKEFILE, mtSequencer_captureFile.cpp), the image only forwards. The main loop copies the ring
// to the file from the 60 ms indicator tick (captureOp TICK): at most 4 sectors per tick, only after 32 events or a 2 s pause (or at a take end),
// never while the file manager, sample streaming, the USB drive or a skip-back save own the card. The file has a fixed size (512 B header sector
// + 32,768 records of 16 B), created once (8 sectors per tick), then only overwritten in place: record n of a take sits in slot n % 32,768, so
// the newest 32,768 events of a take are in the file. The writer fills the take id and the CRC-8 of every record (the ring leaves them 0).
namespace capfile
{
constexpr uint32_t kSlots = 32768u;                  // records in the file
constexpr uint32_t kSectorBytes = 512u;
constexpr uint32_t kRecordsPerSector = kSectorBytes / 16u;
constexpr uint32_t kFileBytes = kSectorBytes + kSlots * 16u; // 524,800 B
constexpr uint32_t kFlushEvents = 32u;               // flush when this many wait
constexpr uint32_t kFlushIdleUs = 2000000u;          // or when the newest waited this long
constexpr uint32_t kFlushSectors = 4u;               // sectors per tick, the most one main loop stall writes
constexpr uint32_t kCreateSectors = 8u;              // zero sectors per tick while the file is made
constexpr uint32_t kHeaderMagic = 0x50434646u;       // "FFCP"
constexpr uint16_t kHeaderVersion = 1;
constexpr const char* kDir = "/Capture";
constexpr const char* kPath = "/Capture/take.cap";

// What the header sector says about the take the file holds (written once when the take starts, again only when events were lost)
struct TakeInfo
{
    uint8_t take;        // take id, 1..255
    uint8_t lostFlag;    // events of the take never reached the file (the ring was lapped while the card was busy)
    uint32_t startUs;    // seqClockUs at the first event
    uint32_t bpm100;     // tempo at the take start, BPM x 100
    uint32_t nameHash;   // FNV-1a of the project name at the take start
    uint32_t lost;       // events lost
    uint16_t newest;     // seq (take relative event number mod 65,536) of the newest valid record
    uint16_t oldest;     // seq of the oldest valid record
    uint32_t span;       // events from the oldest to the newest valid record, both included (at most kSlots)
    uint32_t valid;      // valid records found (span - valid are holes: torn or never written)
};

void takeStart(uint32_t index, uint32_t tUs);        // the take logic: a new take starts at the ring event with this sequence number
void takeEnd();                                      // the take ended (long press, clear): the pending events go out at the next tick
void tick(uint32_t cons, uint32_t now);              // main loop, every 60 ms: cons = the next ring event the take logic has not looked at, now = seqClockUs
bool flushNow(uint32_t cons);                        // every pending event now, true when nothing is left
uint32_t stat(uint32_t which);
enum Stat : uint8_t
{
    kStatState = 0,   // 0 idle, 1 creating, 2 ready, 3 no card or create failed (retry later)
    kStatFlushes,     // flushes that wrote
    kStatSectors,     // sectors written (records, not the header)
    kStatSkips,       // ticks that wanted to flush and found the card busy
    kStatWritten,     // events written
    kStatLost,        // events lost in the ring before they were written
    kStatPending,     // events waiting
    kStatTake,        // file id of the take being written
    kStatHeaders,     // header sectors written
    kStatCreated,     // create steps done
    kStatErrors,      // failed card calls
    kStatCount
};

bool readOpen(TakeInfo* info);                       // the header and the extent of the valid records (reads the whole file once: for a commit of a take the ring no longer holds)
// events of the take, oldest first, from the ordinal `from` (0 = the oldest valid record), at most max into out: an ordinal with no valid
// record (a torn one) comes back as an event with take == 0. Returns how many were written.
uint32_t readEvents(const TakeInfo& info, uint32_t from, CaptureEvent* out, uint32_t max);
uint8_t crc8(const void* data, uint32_t n);          // CRC-8 (poly 0x07, init 0) of a record's bytes 0..14
} // namespace capfile

// The spill of a take longer than the pattern (ref/capture-move-redesign.md "Spill"): the press fills the pattern on screen from step 1, the
// rest goes into new empty patterns put into the song right after the slot of that pattern, at most kMaxPatterns in all (the rest is left
// out). One Undo takes the whole press back: the first pattern by its normal pattern Undo snapshot, the new patterns and the song order by a
// group record tied to that snapshot's slot (fileManagerUndo.cpp, ff_hooks.h). The image half, the take logic calls it.
#include "mtSequencer.h"
namespace capspill
{
constexpr uint8_t kMaxPatterns = 16;                 // the pattern on screen and up to 15 new ones
// up to `want` pattern numbers nobody uses (no notes, not on screen, not in the song, not a Part's source), and only when the pattern on
// screen has a slot in the song and the song has room. 0 while a Part plays or Performance mixes tracks
uint8_t freePatterns(uint8_t want, uint8_t* number);
Sequencer::strPattern* scratch(const Sequencer::strPattern* like); // an empty pattern with the lengths of `like` (the Song screen's scratch)
void save(uint8_t number, Sequencer::strPattern* p);                // to the Workspace, the pattern bits of the song follow
// after the press's Undo snapshot and the saves: the new patterns into the song after the slot of the pattern on screen, and the group record
void land(uint8_t n, const uint8_t* number);
void undoStored(uint8_t slot);                       // fileManagerUndo.cpp: a pattern Undo snapshot went into this slot
bool undone(uint8_t slot);                           // fileManagerUndo.cpp: Undo restored this slot. True: it was a spill, it is gone, no redo
void forget();                                       // a project closes
#ifdef UI_SHOT_HOST
extern bool paceAlways;                              // host test: count every file write as one that was paced
extern uint16_t pacedPasses;
#endif
} // namespace capspill
