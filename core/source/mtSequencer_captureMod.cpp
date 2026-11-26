// Capture, the take logic (ref/capture-move-redesign.md, mtCapture.h). In the release image this file is the Capture tenant of the card module
// /Plugins/Ext/extras.plg (ext_abi.h EXT_SUB_CAPTURE, synth-open/modit/extras.ld), linked against the exact image, the image reaches it
// through ext_hooks.capture (Sequencer::captureOp). Elsewhere (the ui_shot host, an image without the module) it is linked in.
// The image's tap writes every played note into the ring (mtSequencer_capture.cpp). Here, from the main loop only:
//  - the take: the run of notes since the last gap. A note on that comes kCaptureGapSec after the moment no note was held any more starts a
//    new take (decision D3), so does one kCaptureLostOffSec after the last event of any kind (a lost note off). Nothing ends a take by itself:
//    the old take stays capturable until that note.
//  - the indicator state (4 states, decision D9)
//  - the take file: the ring is copied to /Capture/take.cap from the 60 ms tick (the writer and the reader are the second tenant of the module,
//    mtSequencer_captureFile.cpp, this file only tells it where takes start and calls it)
//  - the commit (button 3): the take, from its first note on, is written straight into the destination track of the pattern on screen (the armed
//    track when exactly one is armed, else the track the first note came in on, else the cursor track), the first note on step 1, the rest by
//    the time since it, with the rules of the stock live record: the record options (quantise, micro timing, velocity), the chord matcher and
//    the record join windows. One pattern Undo revision per press. The take is not used up: a second press (also in another pattern) writes
//    the same notes on the same steps again. A take longer than the pattern spills: the steps past it go into new empty patterns that the
//    press puts into the song right after the slot of the pattern (mtCapture.h capspill, at most 16 patterns in all, the rest is left out and
//    counted in the popup), and the one Undo of the press takes them and their song slots back too. The Copy buffer is not touched.
#include "core/interface.h"
#include "core/interfaceDefs.h"
#include "core/interfacePopups.h"
#include "fileManager.h"
#include "mtCapture.h"
#include "mtChord.h"
#include "mtRoll.h"
#include "mtSequencer.h"
#include "stereoPairs.h"
#include "mtStructs.h"
#include "patternEditor/patternEditor.h"

#include <stdio.h>
#include <string.h>

namespace
{
constexpr uint16_t kNone = 0xFFFF;
constexpr uint32_t kGapUs = kCaptureGapSec * 1000000u;
constexpr uint32_t kLostOffUs = kCaptureLostOffSec * 1000000u;

// The take (main loop only). The image's ring is read from s_cons up to its sequence number at each call.
struct Take
{
    uint32_t cons;      // the next event to look at
    uint32_t start;     // the first event of the take
    uint32_t usedTo;    // the first event of the take (every event before it is not part of it: the take was cleared)
    uint32_t lastAllUp; // tUs when the held count last came back to 0
    uint32_t lastEv;    // tUs of the newest event
    uint32_t held[4];   // the notes held now (bit per note)
    uint8_t heldN;
    uint8_t id;         // 1..255, 0: no take since start
    bool live;          // a take exists (cleared by the long press)
} s_take;

// The popup keeps the pointers of its lines (InterfacePopups::show), so the text lives here
char s_popTitle[24];
char s_popLine[40];

// Every event the tap wrote since the last call. The reader stays kCaptureReadMax behind the head: a lap of the ring while the take logic was
// not loaded (or a MIDI flood of 1,016 events within one tick) skips the lost part and forgets which notes are held.
void consume()
{
    Take& t = s_take;
    const uint32_t end = captureRingSeq();
    if (end - t.cons > kCaptureReadMax)
    {
        t.cons = end - kCaptureReadMax;
        memset(t.held, 0, sizeof(t.held));
        t.heldN = 0;
    }
    for (; t.cons != end; t.cons++)
    {
        const CaptureEvent& e = captureRingAt(t.cons);
        const uint32_t bit = 1u << (e.note & 31u);
        uint32_t& word = t.held[(e.note >> 5) & 3u];
        if (e.vel & kCapOn)
        {
            if (!t.live || (t.heldN == 0 && e.tUs - t.lastAllUp >= kGapUs) || e.tUs - t.lastEv >= kLostOffUs)
            {
                t.start = t.usedTo = t.cons;
                capfile::takeStart(t.cons, e.tUs);
                t.id = uint8_t(t.id == 255 ? 1 : t.id + 1);
                t.live = true;
                memset(t.held, 0, sizeof(t.held));
                t.heldN = 0;
            }
            if (!(word & bit))
            {
                word |= bit;
                t.heldN++;
            }
        }
        else if (word & bit)
        {
            word &= ~bit;
            if (--t.heldN == 0)
            {
                t.lastAllUp = e.tUs;
            }
        }
        t.lastEv = e.tUs;
    }
}

// events of the take not used yet, at most what the reader may see (the newest ones)
uint16_t available()
{
    const Take& t = s_take;
    if (!t.live)
    {
        return 0;
    }
    const uint32_t end = t.cons;
    uint32_t n = end - t.usedTo;
    return uint16_t(n > kCaptureReadMax ? kCaptureReadMax : n);
}

void endTake()
{
    s_take.live = false;
    s_take.usedTo = s_take.cons;
    capfile::takeEnd();
}
} // namespace

uint32_t Sequencer::captureOp(uint32_t op)
{
    consume();
    switch (op)
    {
    case CAPTURE_OP_TICK:
    {
        const uint32_t state = sequencer.captureTakeTick();
        capfile::tick(s_take.cons, sequencer.seqClockUs);   // the take file (the second tenant, mtSequencer_captureFile.cpp)
        return state;
    }
    case CAPTURE_OP_COMMIT: sequencer.captureCommitTake(); return 0;
    case CAPTURE_OP_CLEAR:
        endTake();
        mtPopups.show(0, "Capture cleared");
        return 0;
    case CAPTURE_OP_COUNT: return available();
    case CAPTURE_OP_TAKE: return s_take.id;
    case CAPTURE_OP_END: endTake(); return 0;
    case CAPTURE_OP_FLUSHNOW: return capfile::flushNow(s_take.cons) ? 1u : 0u;
    default: return op >= CAPTURE_OP_FILE ? capfile::stat(op - CAPTURE_OP_FILE) : 0;
    }
}

// The destination track of a press (decision D7): the only armed track, else the track the first note of the take came in on (the destination
// hint of a track channel MIDI note), else the cursor track.
bool Sequencer::captureWindow(int& track)
{
    track = getSingleArmedTrack();
    if (track < 0)
    {
        const uint16_t n = available();
        const uint8_t hint = n ? uint8_t((captureRingAt(s_take.cons - n).src >> 3) & 15u) : uint8_t(kCapDestNone);
        track = hint != kCapDestNone ? int(hint) : getActualTrack();
    }
    return track >= 0 && track < kAllTracks;
}

// Every 60 ms. Empty: no take. Gap: no note held for the gap, the next note starts a new take. Ready: not recording (running or stopped), a
// take that a press would write. Stale otherwise (a take is never used up by a press, it stays until the next take starts).
uint8_t Sequencer::captureTakeTick()
{
    if (available() == 0)
    {
        return kCapIndEmpty;
    }
    if (s_take.heldN == 0 && seqClockUs - s_take.lastAllUp >= kGapUs)
    {
        return kCapIndGap;
    }
    int track;
    return (isRec() || player.preRecMetronomeActive || !captureWindow(track)) ? kCapIndStale : kCapIndReady;
}

// The button: the take is written into the destination track of the pattern on screen. Main loop.
void Sequencer::captureCommitTake()
{
    if (isRec())
    {
        mtPopups.show(0, "Stop recording first");
        return;
    }
    if (player.preRecMetronomeActive)
    {
        mtPopups.show(0, "Stop recording first"); // the count-in of a Rec: the Rec has begun, its notes belong to the stock record
        return;
    }
    const uint32_t head = s_take.cons;
    const uint16_t count = available();

    int track;
    if (!captureWindow(track))
    {
        return;
    }
    const uint8_t len = uint8_t(getTrackLastStep(currentPattern, track) + 1);
    const bool quant = !recMicrotiming();
    const bool withVelocity = recVelocity() != 0;

    // j counts from the oldest event the reader may see (0) to the newest (count - 1)
    const auto ev = [&](uint16_t j) -> const CaptureEvent& { return captureRingAt(head - count + j); };

    // every note on gets the index of the note off that ends it (kNone: still held). The first note on is the reference of the steps
    uint16_t* const off = captureScratch.off;
    uint16_t* const open = captureScratch.open;
    memset(open, 0xFF, sizeof(captureScratch.open));
    uint16_t onsets = 0, first = kNone;
    for (uint16_t j = 0; j < count; j++)
    {
        const CaptureEvent& e = ev(j);
        off[j] = kNone;
        if (e.note > 127)
        {
            continue;
        }
        if (e.vel & kCapOn)
        {
            open[e.note] = j;
            first = first == kNone ? j : first;
            onsets++;
        }
        else if (open[e.note] != kNone)
        {
            off[open[e.note]] = j;
            open[e.note] = kNone;
        }
    }
    if (!onsets)
    {
        mtPopups.show(0, "Nothing to capture", "play, then press Capture");
        return;
    }

    newFileManager.storePatternUndoRevision(); // one undo step for the whole press (the spill's group record is tied to it)
    // the pattern a pass writes and the take step its step 0 is: the pattern on screen from step 0 of the take, then the spill patterns
    strPattern* pat = currentPattern;
    int base = 0;
    bool dry = true; // the first pass only finds the last step of the take (maxS)
    int maxS = 0;
    uint8_t npat = 1;

    // the step of an event: the time since the first note on, in steps at the tempo now, counted from the step grid the first note was played
    // on (its uStep), the first note itself on step 0. The stock live record rules: quantised to the nearest step (the second half of a step
    // goes to the next one), or the step it was played in with the micro move of the 48 sub steps. Counted from step 0 of the take, any length
    const float period = getStepPeriodMs() * 1000.0f;
    const CaptureEvent& f = ev(first);
    // a first note played with the transport stopped has no step grid (player.uStep is the stale one of the last run): the note itself is the grid
    const float u0 = float((f.flags & kCapFlagRunning) && f.uStep ? f.uStep : 1);
    const auto stepOf = [&](const CaptureEvent& e, int8_t* micro) -> int
    {
        float x = float(int32_t(e.tUs - f.tUs)) / period + (u0 - 1.0f) / 48.0f - ((quant && u0 > 24.0f) ? 1.0f : 0.0f);
        *micro = -1;
        int s;
        if (quant)
        {
            s = int(x + 1.0f + 0.5f) - 1; // x can be down to -0.5: floor(x + 0.5)
        }
        else
        {
            s = int(x);
            const int u = int((x - float(s)) * 48.0f) + 1;
            *micro = int8_t(clamp<int>(mapClamped<int>(u, 1, 48, 0, 100), getFxMin(fx.FX_TYPE_MICROMOVE), getFxMax(fx.FX_TYPE_MICROMOVE)));
        }
        return s;
    };

    uint16_t placed = 0, left = 0; // notes written, notes of the take left out (past the end of the pattern, a chord note that did not fit)

    // a note at a step of a track: the instrument as the stock record stamps it, the velocity and micro move the record options take. The captured
    // note wins its step: what the step held of its own (velocity, micro move, MIDI Chord and Strum) goes, any other effect stays. So a second
    // press writes the same steps again
    const auto putNote = [&](uint8_t tr, uint8_t stepIdx, uint8_t note, const CaptureEvent& e, int8_t micro)
    {
        strPattern::strTrack::strStep& st = pat->track[tr].step[stepIdx];
        const int inst = changeInstrumentInSpecificOrder(int8_t(e.instrument), 0, !isSampleTrack(tr));
        st.volume = -1;
        st.micromove = -1;
        for (uint8_t i = 0; i < 2; i++)
        {
            if (st.fx[i].type == strFxConsts::FX_TYPE_MIDI_CHORD || st.fx[i].type == strFxConsts::FX_TYPE_STRUM)
            {
                st.fx[i].type = 0;
                st.fx[i].value = 0;
            }
        }
        st.note = int8_t(note);
        st.instrument = uint8_t(inst);
        if (!quant)
        {
            st.micromove = micro;
        }
        if (withVelocity)
        {
            st.volume = velocityToVolume(int8_t(mapClamped<int>(e.vel & 0x7F, 0, 127, 0, getInstrumentVelo(uint8_t(inst)))));
        }
        placed++;
    };

    // the release: the OFF goes to the step of the note off, only when that step is empty and not the step of the onset (as recGroupOff). Every
    // pass sees every note, so a release in another pattern of the press is written by the pass of that pattern, one past the last pattern
    // ends the note at the end of the last pattern
    const auto putOff = [&](uint8_t tr, int onStep, const CaptureEvent& eo)
    {
        int8_t micro;
        int s = stepOf(eo, &micro) - base;
        bool natural = true;
        if (s == onStep)
        {
            s++;
            natural = false;
        }
        if (s >= len && base + len == len * npat)
        {
            s = len - 1;
            natural = false;
        }
        if (s < 0 || s >= len)
        {
            return;
        }
        strPattern::strTrack::strStep& st = pat->track[tr].step[s];
        if (st.isNoteEmpty())
        {
            st.note = STEP_NOTE_OFF_DEFAULT;
            if (!quant && natural)
            {
                st.micromove = micro;
            }
        }
    };

    // a group of onsets that belong together (the record join windows) lands on the step of its first note
    uint16_t gIdx[8];
    uint8_t gN = 0;
    uint32_t gT0 = 0, gTLast = 0;

    const auto flush = [&]()
    {
        if (!gN)
        {
            return;
        }
        const CaptureEvent& e0 = ev(gIdx[0]);
        int8_t micro;
        const int sa = stepOf(e0, &micro);
        const bool bad = e0.instrument >= kTotalInstrumentsCount;
        if (dry)
        {
            maxS = (!bad && sa > maxS) ? sa : maxS;
            gN = 0;
            return;
        }
        if (bad || sa < 0 || sa >= base + len)
        {
            if (!base && (bad || sa < 0 || sa >= len * npat))
            {
                left = uint16_t(left + gN); // past the last pattern of the press (or no instrument), counted once
            }
            gN = 0;
            return;
        }
        // an onset in an earlier pattern of the press: only its release can be in this pattern (here false, s0 below 0)
        const bool here = sa >= base;
        const int s0 = sa - base;
        const int inst0 = changeInstrumentInSpecificOrder(int8_t(e0.instrument), 0, !isSampleTrack(uint8_t(track)));
        if (gN > 1 && isSampleInstrument(inst0))
        {   // a chord on sample instruments: one note per track, the following free sample tracks, the surplus is dropped
            uint8_t tr = uint8_t(track);
            if (here)
            {
                putNote(tr, uint8_t(s0), ev(gIdx[0]).note, e0, micro);
            }
            if (off[gIdx[0]] != kNone)
            {
                putOff(tr, s0, ev(off[gIdx[0]]));
            }
            for (uint8_t k = 1; k < gN; k++)
            {
                const CaptureEvent& ek = ev(gIdx[k]);
                do
                {
                    tr++;
                } while (tr < kSampleTracks &&
                         (stereopairs::isFollower(tr) || // the hidden second track of a linked pair gets no note
                          // a track that holds a note of its own (or an effect) is skipped, except one that holds this very note from an earlier press
                          (here && !((pat->track[tr].step[s0].isNoteEmpty() && !pat->track[tr].step[s0].hasAnyFx()) ||
                                     pat->track[tr].step[s0].note == int8_t(ek.note)))));
                if (tr >= kSampleTracks)
                {
                    left = uint16_t(left + (here ? gN - k : 0));
                    break;
                }
                if (here)
                {
                    putNote(tr, uint8_t(s0), ek.note, ek, micro);
                }
                if (off[gIdx[k]] != kNone)
                {
                    putOff(tr, s0, ev(off[gIdx[k]]));
                }
            }
        }
        else
        {
            if (here)
            {
                putNote(uint8_t(track), uint8_t(s0), e0.note, e0, micro);
            }
            if (here && gN > 1)
            {
                uint8_t notes[8];
                for (uint8_t k = 0; k < gN; k++)
                {
                    notes[k] = ev(gIdx[k]).note;
                }
                strPattern::strTrack::strStep& st = pat->track[track].step[s0];
                const ChordMatch m = chordMatch(notes, gN);
                const int8_t slot = chordSlot(st, -1);
                if (m.found && m.idx != 0 && slot >= 0)
                {
                    st.note = int8_t(m.bass);
                    st.fx[slot].type = strFxConsts::FX_TYPE_MIDI_CHORD;
                    st.fx[slot].value = m.idx;
                    handroll::strumWrite(st.fx,
                                         slot,
                                         handroll::strumValue(notes[0], notes[gN - 1], gN, gTLast - gT0, kStrumUnitUs),
                                         false,
                                         strFxConsts::FX_TYPE_STRUM);
                    placed = uint16_t(placed + gN - 1);
                }
                else
                {
                    left = uint16_t(left + gN - 1); // no chord, or both FX slots busy: the first note alone, as the record join
                }
            }
            // the release of the group: at its last note off, none when a note is still held at the press
            uint16_t last = kNone;
            bool allUp = true;
            for (uint8_t k = 0; k < gN; k++)
            {
                const uint16_t o = off[gIdx[k]];
                allUp = allUp && o != kNone;
                if (o != kNone && (last == kNone || o > last))
                {
                    last = o;
                }
            }
            if (allUp)
            {
                putOff(uint8_t(track), s0, ev(last));
            }
        }
        gN = 0;
    };

    const auto run = [&]()
    {
        gN = 0;
        for (uint16_t j = 0; j < count; j++)
        {
            const CaptureEvent& e = ev(j);
            if (!(e.vel & kCapOn) || e.note > 127)
            {
                continue;
            }
            bool join = false;
            if (gN && gN < sizeof(gIdx) / sizeof(gIdx[0]) && uint32_t(e.tUs - gTLast) <= Sequencer::kRecJoinWindowUs &&
                uint32_t(e.tUs - gT0) <= Sequencer::kRecJoinSpanUs && e.instrument == ev(gIdx[0]).instrument)
            {
                // the first note is still held when this one starts (its note off comes later in the ring), and this note is not in the group yet
                join = off[gIdx[0]] == kNone || off[gIdx[0]] > j;
                for (uint8_t k = 0; k < gN && join; k++)
                {
                    join = ev(gIdx[k]).note != e.note;
                }
            }
            if (join)
            {
                gIdx[gN++] = j;
                gTLast = e.tUs;
            }
            else
            {
                flush();
                gIdx[0] = j;
                gN = 1;
                gT0 = e.tUs;
                gTLast = e.tUs;
            }
        }
        flush();
    };

    run(); // dry: the last step of the take
    dry = false;
    // the spill: steps past the pattern go into new empty patterns after it in the song, up to capspill::kMaxPatterns in all
    uint8_t number[capspill::kMaxPatterns - 1];
    const int want = maxS / len;
    const uint8_t extra = capspill::freePatterns(uint8_t(want < capspill::kMaxPatterns - 1 ? want : capspill::kMaxPatterns - 1), number);
    npat = uint8_t(1 + extra);
    run();
    for (uint8_t k = 0; k < extra; k++)
    {
        pat = capspill::scratch(currentPattern);
        base = (k + 1) * len;
        run();
        capspill::save(number[k], pat);
    }
    capspill::land(extra, number);

    newFileManager.setActualPatternStructChanged();

    snprintf(s_popTitle, sizeof(s_popTitle), placed == 1 ? "Captured %d note" : "Captured %d notes", int(placed));
    char* q = s_popLine;
    *q = 0;
    if (extra)
    {
        q += snprintf(q, sizeof(s_popLine), "%d patterns", int(npat));
    }
    if (left)
    {
        snprintf(q, sizeof(s_popLine) - size_t(q - s_popLine), extra ? ", %d left out" : "%d left out", int(left));
    }
    if (*s_popLine)
    {
        mtPopups.show(0, s_popTitle, s_popLine);
    }
    else
    {
        mtPopups.show(0, s_popTitle);
    }
}
