// MPE out (ref/mpe-out.md, design ref/mpe-scope.md section 8): the notes of every MIDI instrument go out through ONE MPE lower zone. Master channel 1,
// member channels 2..(1+members), one member channel per sounding note, so every note has its own pitch bend (the per note tuning of a microtonal
// project, the M FX). The map (source channel, note) -> member channel lives behind sendMidiNoteOn and sendMidiNoteOff (mtMidi.cpp): no
// sequencer call site changes, a chord out gets one member per chord note.
// With members == 0 (Off, the default, no project.mtff line) nothing here runs and the MIDI out is the byte stream it always was.
// Pure logic over a sink (a type with noteOn, noteOff, bend, cc, pressure) and no firmware includes: mtMidi.cpp passes the USB and DIN drivers, the host
// fuzz test (synth-open/tools/mpe_out_test.cpp) a recorder.
#ifndef MT_MPE_OUT_H
#define MT_MPE_OUT_H

#include <stdint.h>
#include <cmath>
#include "mtTuning.h"

namespace mpeout
{
constexpr int kMax = 15;              // member channels 2..16
constexpr int kRange = 48;            // the pitch bend range of a member channel in semitones (the MPE default, sent as RPN 0 to every member)
constexpr int kMasterRange = 2;       // the range the M FX bend value means on a plain channel: kept, the value is rescaled to the member range

struct Slot
{
    uint8_t src;     // source channel 1..16 (the MIDI instrument), 0 = the member is free
    uint8_t note;    // the note as the sequencer passed it (the key of the map)
    uint8_t wire;    // the key that went out
    uint8_t pad;
    int16_t tune;    // the bend of the tuning (14 bit, centred on 0)
    int16_t fx;      // the M FX bend of the note, in member bend units
    uint16_t age;    // note on counter, wrap safe compare
};

inline uint8_t members;              // 0 = Off, 1..15 = lower zone with that many member channels
inline Slot slots[kMax];
inline int16_t lastBend[16];         // the bend last sent on channel 1..16 (index channel - 1)
inline int16_t chanFx[16];           // the M FX bend of a source channel 1..16 in member bend units: what the next notes of the instrument start with
inline uint8_t rr;                   // the member the last note on took
inline uint16_t clk;
inline uint8_t cfgStep;              // the next configuration message (a restart sets 0, done at cfgTotal())
inline uint8_t cfgMode;              // the notes out mode the configuration went to
inline bool needCfg;                 // send the configuration again (a restart of the messages)
inline bool busLost;                 // the USB bus went idle: the configuration goes out again before the next note

inline int cfgTotal() { return members ? 5 + 4 * int(members) : 0; }
inline int chanOf(int slot) { return slot + 2; }

// The key and the 14 bit bend of note k of the project: the nearest key of its pitch plus the rest as a bend of kRange semitones. The default
// tuning (12 notes of 1200 cents) is the note itself and 0
inline int roundf2i(float x) { return int(x < 0 ? x - 0.5f : x + 0.5f); }   // to the nearest, a tie away from 0 (no libm call)
inline void pitchOf(int note, int transp, uint8_t& wire, int16_t& bend)
{
    int key = note, b = 0;
    float p = 0;
    const bool tuned = !tuning::isDefault();
    if (tuned)
    {
        p = 60.0f + tuning::rel(note);
        key = roundf2i(p);
    }
    key += transp;
    key = key < 0 ? 0 : (key > 127 ? 127 : key);
    wire = uint8_t(key);
    if (tuned)
    {
        b = roundf2i((p + float(transp) - float(key)) * (8192.0f / float(kRange)));
        b = b < -8192 ? -8192 : (b > 8191 ? 8191 : b);
    }
    bend = int16_t(b);
}

template <class O>
inline void sendBend(O& o, int ch, int v)
{
    v = v < -8192 ? -8192 : (v > 8191 ? 8191 : v);
    if (lastBend[ch - 1] == v) return;
    lastBend[ch - 1] = int16_t(v);
    o.bend(ch, v);
}

// message i of the configuration: the Manager Configuration Message (RPN 6, members) on the master channel, RPN 0 (bend range) and a centred bend
// on every member. The null RPN after the master message keeps a later CC 6 from changing the zone
template <class O>
inline void cfgMsg(O& o, int i)
{
    if (i < 5)
    {
        static constexpr uint8_t k[5][2] = {{101, 0}, {100, 6}, {6, 0}, {101, 127}, {100, 127}};
        o.cc(1, k[i][0], i == 2 ? members : k[i][1]);
        return;
    }
    const int m = (i - 5) >> 2, ch = m + 2;
    switch ((i - 5) & 3)
    {
        case 0: o.cc(ch, 101, 0); break;
        case 1: o.cc(ch, 100, 0); break;
        case 2: o.cc(ch, 6, kRange); break;
        default: lastBend[ch - 1] = 0; o.bend(ch, 0); break;
    }
}

// mode: the notes out mode now. A new mode (the output was switched on or moved to the other port) sends the configuration to it
template <class O>
inline void pump(O& o, uint8_t mode, int maxMsgs)
{
    if (!members) return;
    if (needCfg || mode != cfgMode)
    {
        needCfg = false;
        cfgMode = mode;
        cfgStep = 0;
    }
    for (; cfgStep < cfgTotal() && maxMsgs > 0; maxMsgs--) cfgMsg(o, cfgStep++);
}

// Every member that sounds is let go, the used channels go back to a centred bend. Called on Stop, on a mode change and on a port change
template <class O>
inline void release(O& o)
{
    for (int i = 0; i < kMax; i++)
    {
        Slot& s = slots[i];
        if (!s.src) continue;
        o.noteOff(chanOf(i), s.wire);
        s.src = 0;
    }
    for (int ch = 1; ch <= 16; ch++)
    {
        chanFx[ch - 1] = 0;
        if (lastBend[ch - 1]) { lastBend[ch - 1] = 0; o.bend(ch, 0); }
    }
}

// n members (0 Off). A change lets everything go first. Off after On switches the zone off at the receiver (RPN 6 with 0 members)
template <class O>
inline void setMembers(O& o, uint8_t n, uint8_t mode)
{
    n = n > kMax ? kMax : n;
    if (n == members) return;
    release(o);
    const bool wasOn = members != 0;
    members = n;
    rr = uint8_t(n ? n - 1 : 0);   // the first note takes the first member
    cfgStep = 0;
    cfgMode = mode;
    needCfg = false;
    if (!n)
    {
        if (wasOn)
            for (int i = 0; i < 5; i++) cfgMsg(o, i);   // members is 0 now: the master message carries 0
        return;
    }
    // the messages go out in pump(), a few every audio block (the DIN port is slow), before the first note at the latest
}

template <class O>
inline void noteOn(O& o, uint8_t note, uint8_t velo, uint8_t src, int transp, uint8_t mode)
{
    if (busLost) { busLost = false; needCfg = members != 0; }   // the receiver may be another one now (a cable pulled and put back)
    pump(o, mode, 255);   // a configuration that is not out yet goes first
    int pick = -1;
    for (int i = 0; i < members; i++)
        if (slots[i].src == src && slots[i].note == note)
        {   // the same note again: its off goes first, on its member
            o.noteOff(chanOf(i), slots[i].wire);
            slots[i].src = 0;
        }
    for (int k = 1; k <= members; k++)
    {   // the next free member after the last one used: the longest released one gets reused
        const int i = (rr + k) % members;
        if (!slots[i].src) { pick = i; break; }
    }
    if (pick < 0)
    {   // all members sound: the oldest note is stolen, its off goes out before the new note
        pick = 0;
        for (int i = 1; i < members; i++)
            if (int16_t(slots[i].age - slots[pick].age) < 0) pick = i;
        o.noteOff(chanOf(pick), slots[pick].wire);
    }
    Slot& s = slots[pick];
    s.src = src;
    s.note = note;
    s.fx = chanFx[src - 1];
    s.age = ++clk;
    pitchOf(note, transp, s.wire, s.tune);
    rr = uint8_t(pick);
    sendBend(o, chanOf(pick), s.tune + s.fx);   // the bend before the note on: a receiver never sounds the note at the old bend
    o.noteOn(chanOf(pick), s.wire, velo);
}

template <class O>
inline void noteOff(O& o, uint8_t note, uint8_t src)
{
    for (int i = 0; i < members; i++)
    {
        Slot& s = slots[i];
        if (s.src == src && s.note == note)
        {
            o.noteOff(chanOf(i), s.wire);
            s.src = 0;
            return;
        }
    }
    // no note on for it (stolen, or sent before the zone was switched on): it is dropped, the receiver has no such note
}

// The M FX: value is the 14 bit bend of a plain channel (+-8192 = +-kMasterRange semitones), a state of the source channel that stays until the next M
// FX (or Stop), as the bend of a channel always did, and it comes before the note of its step. Here it is rescaled to member bend units and ADDED to
// the tuning bend of every note: the notes that sound on the source channel move now, the next notes of it start with it. The master channel is not used
template <class O>
inline void fxBend(O& o, int value, uint8_t src)
{
    const int fx = (value * kMasterRange + (value >= 0 ? kRange / 2 : -kRange / 2)) / kRange;
    chanFx[src - 1] = int16_t(fx);
    for (int i = 0; i < members; i++)
    {
        Slot& s = slots[i];
        if (s.src != src) continue;
        s.fx = int16_t(fx);
        sendBend(o, chanOf(i), s.tune + s.fx);
    }
}

// poly pressure of a note (a CC FX target): the member channel pressure of that note
template <class O>
inline void notePressure(O& o, uint8_t note, uint8_t v, uint8_t src)
{
    for (int i = 0; i < members; i++)
        if (slots[i].src == src && slots[i].note == note) { o.pressure(chanOf(i), v); return; }
}
}   // namespace mpeout

#endif
