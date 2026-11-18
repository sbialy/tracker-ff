// Capture (Ableton Move style, tracker-ff, phase 1: the transport runs). See ref/capture-feature.md.
// The green mode of the pattern editor keeps what is played (pads and MIDI in) in a ring of 512 events. The under-screen button 3 writes
// the events of the last pass of the pattern into the stock Copy/Paste buffer (copyPastePattern and Sequencer::copySelection, what Copy writes
// and Shift + Copy pastes) as a block on the cursor track (the armed one when exactly one is armed), with the rules of the stock live record:
// the record options (quantise, micro timing, velocity), the chord matcher and the record join windows. The pattern itself is not touched.
#include "core/interface.h"
#include "core/interfaceDefs.h"
#include "core/interfacePopups.h"
#include "interruptsControl.h"
#include "mtCapture.h"
#include "mtChord.h"
#include "mtRoll.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "patternEditor/patternEditor.h"

#include <cr_section_macros.h>
#include <stdio.h>
#include <string.h>

bool captureFeatureOn = false;
uint8_t captureIdleSec = kCaptureIdleDefault;

extern Sequencer::strPattern copyPastePattern; // mtSequencer_patternEdit.cpp: the stock Copy/Paste buffer (SDRAM, emptied at boot only)

namespace
{
constexpr uint16_t kNone = 0xFFFF;

// SDRAM, no static constructor (a plain struct of integers: nothing runs at start, the head and count below say what is valid)
__NOINIT(EXTERNAL_RAM) CaptureEvent s_ring[kCaptureRing];
// the commit scratch: for every event the index of the note off that ends it, and the open note on of each note
struct CaptureScratch
{
    uint16_t off[kCaptureRing];
    uint16_t open[128];
};
__NOINIT(EXTERNAL_RAM) CaptureScratch s_scratch;

volatile uint16_t s_head = 0;  // the next slot to write
volatile uint16_t s_count = 0; // valid events, at most kCaptureRing
volatile uint32_t s_lastTapUs = 0; // seqClockUs of the newest event (SRAM, so the idle test does not read the ring)

// The popup keeps the pointers of its lines (InterfacePopups::show), so the text lives here
char s_popTitle[24];
char s_popLine[40];
} // namespace

uint16_t captureRingCount()
{
    return s_count;
}

bool captureRingGet(uint16_t back, CaptureEvent* out)
{
    if (back >= s_count)
    {
        return false;
    }
    *out = s_ring[uint16_t(s_head + kCaptureRing - 1u - back) & (kCaptureRing - 1u)];
    return true;
}

void captureRingClear()
{
    DISABLE_AUDIO_INTERRUPTS();
    s_count = 0;
    ENABLE_AUDIO_INTERRUPTS();
}

// The one funnel (handleNoteOn / handleNoteOff, before the selection test): the pattern editor in the green mode, not recording, not an
// instrument pick. Pads come from the main loop, which the audio callback can preempt, so their push (and the read of the player position) is
// guarded. MIDI in runs inside the audio callback itself and needs no guard.
void Sequencer::captureTap(uint8_t channel, uint8_t note, uint8_t velocity, bool on, int16_t source)
{
    if (channel == GRID_INSIDE_PATTERN_INSTRUMENT || note > 127 || isRec() || isEditMode() || !mtInterface.isPatternScreenActive())
    {
        return;
    }
    const bool pad = source < 100;
    if (pad)
    {
        DISABLE_AUDIO_INTERRUPTS();
    }
    // the 12 byte event as three words (little endian, the field order of CaptureEvent): every SDRAM access costs about 17 cycles, byte
    // stores would be eight of them
    typedef uint32_t __attribute__((may_alias)) word_t;
    word_t* const d = reinterpret_cast<word_t*>(&s_ring[s_head]);
    const uint32_t w[3] = {
        seqClockUs,
        uint32_t(note) | (uint32_t((velocity & 0x7F) | (on ? kCapOn : 0)) << 8) | (uint32_t(uint8_t(player.globalPos)) << 16) |
          (uint32_t(uint8_t(player.uStep)) << 24),
        uint32_t(mtProject.values.lastUsedInstrument) | (uint32_t(mtProject.values.actualPattern) << 8) |
          (uint32_t((pad ? kCapFlagPads : 0) | (isPlay() ? kCapFlagRunning : 0)) << 16),
    };
    d[0] = w[0];
    d[1] = w[1];
    d[2] = w[2];
    s_head = uint16_t((s_head + 1u) & (kCaptureRing - 1u));
    s_lastTapUs = seqClockUs;
    if (s_count < kCaptureRing)
    {
        s_count = uint16_t(s_count + 1u);
    }
    if (pad)
    {
        ENABLE_AUDIO_INTERRUPTS();
    }
}

// The track the block starts on (the only armed one, else the cursor track: a Paste puts it on the track of its own cursor) and the length
// of one pass of it at the tempo now, the window of a capture.
bool Sequencer::captureWindow(int& track, uint32_t& passUs)
{
    track = getSingleArmedTrack();
    if (track < 0)
    {
        track = getActualTrack();
    }
    if (track < 0 || track >= kAllTracks)
    {
        return false;
    }
    passUs = uint32_t(float(getTrackLastStep(currentPattern, track) + 1) * getStepPeriodMs() * 1000.0f);
    return true;
}

// Main loop, every 60 ms (Interface::commonThingsUpdate, so on every screen). The idle time empties the ring (an old phrase is not what a
// Capture should take) and the return value is the state of the title bar indicator. "Ready": transport running, a usable note on of this
// pattern inside the window of one pass. The scan runs from the newest event and stops at the first one outside the window, so the usual
// cases (nothing played, or only old notes) read one event. Nothing is redrawn here, the title bar redraws only when the state changes.
uint8_t Sequencer::captureIndicator()
{
    if (!captureFeatureOn)
    {
        return kCapIndHidden;
    }
    uint16_t head, count;
    uint32_t last;
    DISABLE_AUDIO_INTERRUPTS();
    head = s_head;
    count = s_count;
    last = s_lastTapUs;
    ENABLE_AUDIO_INTERRUPTS();
    if (count == 0)
    {
        return kCapIndEmpty;
    }
    const uint32_t now = seqClockUs;
    if (now - last > uint32_t(captureIdleSec) * 1000000u)
    {
        captureRingClear();
        return kCapIndEmpty;
    }
    int track;
    uint32_t passUs;
    if (!isPlay() || isRec() || player.preRecMetronomeActive || !captureWindow(track, passUs))
    {
        return kCapIndStale;
    }
    const uint8_t pattern = mtProject.values.actualPattern;
    if (count > kCaptureReadMax)
    {
        count = kCaptureReadMax;
    }
    for (uint16_t back = 0; back < count; back++)
    {
        const CaptureEvent& e = s_ring[uint16_t(head + kCaptureRing - 1u - back) & (kCaptureRing - 1u)];
        if (now - e.tUs > passUs)
        {
            break;
        }
        if ((e.vel & kCapOn) && e.pattern == pattern && (e.flags & kCapFlagRunning) != 0 && e.note <= 127)
        {
            return kCapIndReady;
        }
    }
    return kCapIndStale;
}

// The long press of button 3: nothing is captured, the ring is emptied and the indicator goes hollow at the next tick.
void Sequencer::captureClear()
{
    if (!captureFeatureOn)
    {
        return;
    }
    captureRingClear();
    mtPopups.show(0, "Capture cleared");
}

// The button: the events of the last pass of the pattern become the content of the copy buffer. Main loop.
void Sequencer::captureCommit()
{
    if (!captureFeatureOn)
    {
        return;
    }
    if (isRec())
    {
        mtPopups.show(0, "Stop recording first");
        return;
    }
    if (!isPlay() || player.preRecMetronomeActive)
    {
        mtPopups.show(0, "Capture needs playback", "start the pattern first");
        return;
    }

    uint16_t head, count;
    DISABLE_AUDIO_INTERRUPTS();
    head = s_head;
    count = s_count;
    ENABLE_AUDIO_INTERRUPTS();
    if (count && seqClockUs - s_lastTapUs > uint32_t(captureIdleSec) * 1000000u)
    {   // the idle time ran out since the last tick of the indicator
        captureRingClear();
        count = 0;
    }
    if (count > kCaptureReadMax)
    {
        count = kCaptureReadMax;
    }

    int track;
    uint32_t passUs; // one pass at the tempo now
    if (!captureWindow(track, passUs))
    {
        return;
    }
    const uint8_t len = uint8_t(getTrackLastStep(currentPattern, track) + 1);
    const uint8_t pattern = mtProject.values.actualPattern;
    const uint32_t now = seqClockUs;
    const bool quant = !recMicrotiming();
    const bool withVelocity = recVelocity() != 0;

    // j counts from the oldest event the reader may see (0) to the newest (count - 1)
    const auto ev = [&](uint16_t j) -> const CaptureEvent&
    { return s_ring[uint16_t(head + kCaptureRing - count + j) & (kCaptureRing - 1u)]; };
    const auto usable = [&](const CaptureEvent& e) { return e.pattern == pattern && (e.flags & kCapFlagRunning) != 0; };

    // the window: events younger than one pass
    uint16_t w0 = count;
    while (w0 > 0 && now - ev(uint16_t(w0 - 1u)).tUs <= passUs)
    {
        w0--;
    }

    // every note on gets the index of the note off that ends it (kNone: still held, or the off is older than the window)
    uint16_t* const off = s_scratch.off;
    uint16_t* const open = s_scratch.open;
    memset(open, 0xFF, sizeof(s_scratch.open));
    uint16_t onsets = 0;
    for (uint16_t j = w0; j < count; j++)
    {
        const CaptureEvent& e = ev(j);
        off[j] = kNone;
        if (!usable(e) || e.note > 127)
        {
            continue;
        }
        if (e.vel & kCapOn)
        {
            open[e.note] = j;
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

    // the buffer: a copy of the pattern as the stock Copy makes it (so a whole pattern Paste keeps length and the other tracks), the block
    // (one pass, the start track and for a sample chord the sample tracks behind it) emptied, the notes go in below
    copyPastePattern = *currentPattern;
    strPattern* const pat = &copyPastePattern;
    const auto wipe = [&](strPattern::strTrack::strStep& st)
    {
        clearStep(&st);
        st.fx[0].value = st.fx[1].value = 0; // the stock clear leaves the values of a removed FX behind
    };
    uint8_t lastTrack = uint8_t(track); // the last track of the block
    for (uint8_t tr = uint8_t(track); tr <= (isSampleTrack(uint8_t(track)) ? kSampleTracks - 1 : track); tr++)
    {
        for (uint8_t s = 0; s < len; s++)
        {
            wipe(pat->track[tr].step[s]);
        }
    }

    // step of an event, the stock live record rules (getDestRecordingStep, handleNoteOn): quantised to the nearest step (the second half
    // of a step goes to the next one), or the step it was played in with the micro move of the 48 sub steps
    const auto stepOf = [&](const CaptureEvent& e, int8_t* micro) -> uint8_t
    {
        uint16_t s = e.pos;
        *micro = -1;
        if (quant)
        {
            s += (e.uStep > 24) ? 1u : 0u;
        }
        else
        {
            *micro = int8_t(clamp<int>(mapClamped<int>(e.uStep, 1, 48, 0, 100), getFxMin(fx.FX_TYPE_MICROMOVE), getFxMax(fx.FX_TYPE_MICROMOVE)));
        }
        return uint8_t(s % len);
    };

    uint16_t placed = 0, left = 0; // notes in the block, notes of a chord that could not be placed

    // a note at a step of a track: the instrument as the stock record stamps it, the velocity and micro move the record options take
    const auto putNote = [&](uint8_t tr, uint8_t stepIdx, uint8_t note, const CaptureEvent& e, int8_t micro)
    {
        strPattern::strTrack::strStep& st = pat->track[tr].step[stepIdx];
        const int inst = changeInstrumentInSpecificOrder(int8_t(e.instrument), 0, !isSampleTrack(tr));
        wipe(st); // a note played twice on one step: the later one wins, with its own velocity and micro move
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

    // the release: the OFF goes to the step of the note off, only when that step is empty and not the step of the onset (as recGroupOff)
    const auto putOff = [&](uint8_t tr, uint8_t onStep, const CaptureEvent& eo)
    {
        int8_t micro;
        uint8_t s = stepOf(eo, &micro);
        bool natural = true;
        if (s == onStep)
        {
            s = uint8_t((s + 1u) % len);
            natural = false;
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
        if (e0.instrument >= kTotalInstrumentsCount)
        {
            gN = 0;
            return;
        }
        int8_t micro;
        const uint8_t s0 = stepOf(e0, &micro);
        const int inst0 = changeInstrumentInSpecificOrder(int8_t(e0.instrument), 0, !isSampleTrack(uint8_t(track)));
        if (gN > 1 && isSampleInstrument(inst0))
        {   // a chord on sample instruments: one note per track, the following free sample tracks, the surplus is dropped
            uint8_t tr = uint8_t(track);
            putNote(tr, s0, ev(gIdx[0]).note, e0, micro);
            if (off[gIdx[0]] != kNone)
            {
                putOff(tr, s0, ev(off[gIdx[0]]));
            }
            for (uint8_t k = 1; k < gN; k++)
            {
                do
                {
                    tr++;
                } while (tr < kSampleTracks &&
                         !(pat->track[tr].step[s0].isNoteEmpty() && !pat->track[tr].step[s0].hasAnyFx()));
                if (tr >= kSampleTracks)
                {
                    left = uint16_t(left + (gN - k));
                    break;
                }
                lastTrack = tr > lastTrack ? tr : lastTrack;
                const CaptureEvent& ek = ev(gIdx[k]);
                putNote(tr, s0, ek.note, ek, micro);
                if (off[gIdx[k]] != kNone)
                {
                    putOff(tr, s0, ev(off[gIdx[k]]));
                }
            }
        }
        else
        {
            putNote(uint8_t(track), s0, e0.note, e0, micro);
            if (gN > 1)
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

    for (uint16_t j = w0; j < count; j++)
    {
        const CaptureEvent& e = ev(j);
        if (!usable(e) || !(e.vel & kCapOn) || e.note > 127)
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

    // the events are used up. Notes pushed meanwhile (a MIDI note during the commit) stay for the next press
    DISABLE_AUDIO_INTERRUPTS();
    s_count = uint16_t((s_head + kCaptureRing - head) & (kCaptureRing - 1u));
    ENABLE_AUDIO_INTERRUPTS();

    // the block is the selection a Paste takes: one pass tall, from the start track to the last track used
    setCopySelection(0, uint8_t(track), uint8_t(len - 1), lastTrack);

    snprintf(s_popTitle, sizeof(s_popTitle), placed == 1 ? "Captured %d note" : "Captured %d notes", int(placed));
    if (left)
    {
        snprintf(s_popLine, sizeof(s_popLine), "%d left out", int(left));
        mtPopups.show(0, s_popTitle, s_popLine);
    }
    else
    {
        mtPopups.show(0, s_popTitle);
    }
}
