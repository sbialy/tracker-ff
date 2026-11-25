// Host unit test (NOT firmware) of the Synth play page's voice router (core/source/synthPlayRouter.cpp, ref/synth-play-r1.md). The firmware's own
// router file is compiled against stand-ins for the four functions that put events on the virtual MIDI port of the synth parts (mtMidi.cpp),
// which log what the part would get. The rows:
//   R1  note on and off reach the part on its channel (part + 1), a note off of a note that is not down sends nothing
//   R2  the velocity curves Soft, Linear, Hard and Fixed, a velocity 0 is a note off, the part never gets a 0 or a 128
//   R3  more than 8 notes down: the 9th takes the cell of the oldest (the oldest gets its note off), the table never holds a note twice
//   R4  the sustain pedal holds the note offs, pedal up releases them, a note struck again while the pedal holds it gets a note off first,
//       Sustain off ignores the pedal, switching it off while the pedal holds notes lets go of them
//   R5  CC 123 and CC 120 release every note (table and part), CC 121 centres the bend, lifts the pedal and zeroes the mod wheel macro,
//       the CCs of other channels and other numbers are not taken
//   R6  releaseAll (the exit of the page): note offs for what the table holds, CC 123, bend 0, pedal up, on the part that was played
//   R7  bend: the value goes through (clamped to 14 bit) on the part's channel, resendBend repeats it, CC 1 goes to the macro of the setting
//   R8  Input and Channel filters (USB and DIN, Omni and one channel), the pads (kPad) pass both, transpose moves the note and the off follows it
//   R9  setPart releases the old part first, then plays the new one on its own channel
//   R10 A/B (part B on): notes, offs, bend, mod wheel macro and CC 123 reach both parts, the table stays one entry per key, part B off is the
//       single part page again, setPartB releases both sides first
//   R12 Program Change: the page's hook gets (side, program) for the channel of a side (A = part + 1, B = the Synth B number), the Input and Channel
//       filters apply, another channel and a closed page (no hook) take nothing
//   R11 the morph: CC 11 sets 0..100 and the gains of the two parts (equal power: cos and sin of the quarter turn, at the middle 0.7071 each,
//       the sum of squares 1 at every step, B off leaves part A at 1), the table is monotonic, the ends are exact
#include "synthPlayRouter.h"

#include <algorithm>
#include <stdio.h>
#include <string>
#include <vector>

#include "mtMidi.h"

static std::vector<std::string> g_log;
static void pcHook(int side, uint8_t program)
{
    g_log.push_back("pc side " + std::to_string(side) + " prog " + std::to_string(program));
}
static std::string fmt(const char* what, int a, int b, int ch)
{
    char s[64];
    snprintf(s, sizeof s, "%s %d %d ch%d", what, a, b, ch);
    return s;
}
void sendMidiNoteOn(uint8_t note, uint8_t velo, uint8_t channel, bool toSynth)
{
    g_log.push_back(fmt(toSynth ? "on" : "ON-NOT-TO-SYNTH", note, velo, channel));
}
void sendMidiNoteOff(uint8_t note, uint8_t, uint8_t channel, bool toSynth)
{
    g_log.push_back(fmt(toSynth ? "off" : "OFF-NOT-TO-SYNTH", note, 0, channel));
}
void sendCC(uint8_t index, uint8_t value, uint8_t channel, bool toSynth)
{
    g_log.push_back(fmt(toSynth ? "cc" : "CC-NOT-TO-SYNTH", index, value, channel));
}
void sendPitchbend(int16_t value, int8_t channel, bool toSynth)
{
    g_log.push_back(fmt(toSynth ? "bend" : "BEND-NOT-TO-SYNTH", value, 0, channel));
}

namespace sp = synthplay;

void synthPlaySetGain(uint8_t part, float gain)
{
    char s[48];
    snprintf(s, sizeof s, "gain %d %.4f", part, gain);
    g_log.push_back(s);
}

// MPE in: what the router tells the part, logged in the same order as the sends
void synthPlayMpeNote(uint8_t chan)
{
    if (chan != 0)
        g_log.push_back("mpe " + std::to_string(chan));
}
void synthPlayMpeBend(uint8_t part, uint8_t chan, int raw)
{
    char s[48];
    snprintf(s, sizeof s, "nbend p%d ch%d %d", part, chan, raw);
    g_log.push_back(s);
}
void synthPlayMpePress(uint8_t part, uint8_t chan, uint8_t press)
{
    char s[48];
    snprintf(s, sizeof s, "npress p%d ch%d %d", part, chan, press);
    g_log.push_back(s);
}
void synthPlayMpeConfig(uint8_t part, int range, int pressPercent)
{
    char s[48];
    snprintf(s, sizeof s, "mpecfg p%d %d %d", part, range, pressPercent);
    g_log.push_back(s);
}

static int g_fail = 0, g_checks = 0;
static void expect(const char* name, const std::string& want, const std::string& got)
{
    g_checks++;
    if (want != got)
    {
        g_fail++;
        printf("FAIL  %s\n      want: %s\n      got:  %s\n", name, want.c_str(), got.c_str());
    }
}
static std::string take()
{
    std::string s;
    for (const std::string& e : g_log)
    {
        s += (s.empty() ? "" : ", ") + e;
    }
    g_log.clear();
    return s;
}
static std::string table()   // "60:1 64:2": out note and state of the cells in use
{
    std::string s;
    for (int i = 0; i < sp::kCells; i++)
    {
        if (sp::cells()[i].state)
        {
            s += (s.empty() ? "" : " ") + std::to_string(sp::cells()[i].out) + ":" + std::to_string(sp::cells()[i].state);
        }
    }
    return s;
}
// the defaults of the page: USB + DIN, Omni, Linear, mod wheel on macro 1, pedal on, part 0, no transpose
static void reset()
{
    sp::releaseAll();
    g_log.clear();
    const int8_t def[sp::kValues] = { 0, 0, 8, 1, 100, 2, 1, 1, 0, 0, 0, 50, 0, 0, 0, 48, 0 };   // the MPE zone is off here: rows R1..R12 are the page before MPE
    for (int i = 0; i < sp::kValues; i++) sp::value[i] = def[i];
    sp::releaseAll();
    g_log.clear();
}

int main()
{
    // R1
    reset();
    sp::noteOn(sp::kUsb, 1, 60, 100);
    sp::noteOn(sp::kUsb, 1, 64, 90);
    expect("R1 two note ons, part 0 = channel 1", "on 60 100 ch1, on 64 90 ch1", take());
    sp::noteOff(sp::kUsb, 1, 60);
    expect("R1 note off", "off 60 0 ch1", take());
    sp::noteOff(sp::kUsb, 1, 61);
    expect("R1 note off of a note that is not down sends nothing", "", take());
    sp::noteOff(sp::kUsb, 1, 60);
    expect("R1 the same note off twice sends once", "", take());
    expect("R1 table", "64:1", table());
    sp::value[sp::kPart] = 2;
    sp::noteOn(sp::kDin, 5, 67, 50);
    expect("R1 part 2 = channel 3", "on 67 50 ch3", take());

    // R2
    reset();
    const int8_t curves[4] = { 0, 1, 2, 3 };
    const char* names[4] = { "soft", "linear", "hard", "fixed" };
    const int vel[5] = { 1, 32, 64, 100, 127 };
    const char* want[4] = { "on 60 2 ch1, on 60 56 ch1, on 60 96 ch1, on 60 122 ch1, on 60 127 ch1",
                            "on 60 1 ch1, on 60 32 ch1, on 60 64 ch1, on 60 100 ch1, on 60 127 ch1",
                            "on 60 1 ch1, on 60 8 ch1, on 60 32 ch1, on 60 78 ch1, on 60 127 ch1",
                            "on 60 100 ch1, on 60 100 ch1, on 60 100 ch1, on 60 100 ch1, on 60 100 ch1" };
    for (int c = 0; c < 4; c++)
    {
        reset();
        sp::value[sp::kVelCurve] = curves[c];
        for (int v : vel)
        {
            sp::noteOn(sp::kUsb, 1, 60, uint8_t(v));
            // a note struck again gets its note off first: only the note on lines are compared
        }
        std::string only;
        for (const std::string& e : g_log)
        {
            if (e.compare(0, 2, "on") == 0) only += (only.empty() ? "" : ", ") + e;
        }
        g_log.clear();
        expect((std::string("R2 curve ") + names[c]).c_str(), want[c], only);
    }
    reset();
    sp::value[sp::kFixedVel] = 7;
    sp::value[sp::kVelCurve] = 3;
    sp::noteOn(sp::kUsb, 1, 60, 127);
    expect("R2 fixed velocity follows its setting", "on 60 7 ch1", take());
    reset();
    sp::noteOn(sp::kUsb, 1, 60, 0);
    expect("R2 note on with velocity 0 is a note off (nothing down: nothing sent)", "", take());
    sp::noteOn(sp::kUsb, 1, 60, 80);
    take();
    sp::noteOn(sp::kUsb, 1, 60, 0);
    expect("R2 note on with velocity 0 ends a held note", "off 60 0 ch1", take());
    for (int c = 0; c < 4; c++)
    {
        reset();
        sp::value[sp::kVelCurve] = int8_t(c);
        bool ok = true;
        for (int v = 1; v <= 127; v++)
        {
            g_log.clear();
            sp::noteOn(sp::kUsb, 1, 61, uint8_t(v));
            int sent = 0;
            sscanf(g_log.back().c_str(), "on %*d %d", &sent);
            ok = ok && sent >= 1 && sent <= 127;
            sp::noteOff(sp::kUsb, 1, 61);
        }
        g_checks++;
        if (!ok) { g_fail++; printf("FAIL  R2 curve %d left the range 1..127\n", c); }
    }

    // R3
    reset();
    for (int n = 0; n < 8; n++) sp::noteOn(sp::kUsb, 1, uint8_t(60 + n), 100);
    expect("R3 eight notes down", "60:1 61:1 62:1 63:1 64:1 65:1 66:1 67:1", table());
    take();
    sp::noteOn(sp::kUsb, 1, 70, 100);
    expect("R3 the ninth: the oldest note gets its off, then the new note", "off 60 0 ch1, on 70 100 ch1", take());
    expect("R3 table after the ninth", "70:1 61:1 62:1 63:1 64:1 65:1 66:1 67:1", table());
    sp::noteOn(sp::kUsb, 1, 63, 100);
    expect("R3 a note that is down struck again: off then on, still one cell", "off 63 0 ch1, on 63 100 ch1", take());
    expect("R3 the table holds 63 once", "70:1 61:1 62:1 63:1 64:1 65:1 66:1 67:1", table());
    sp::noteOn(sp::kUsb, 1, 71, 100);   // the oldest now is 61 (63 was struck again, 70 is newer)
    expect("R3 the next oldest goes", "off 61 0 ch1, on 71 100 ch1", take());
    sp::sounding();
    expect("R3 sounding count", "8", std::to_string(sp::sounding()));

    // R4
    reset();
    sp::controlChange(sp::kUsb, 1, 64, 127);
    expect("R4 pedal down sends nothing to the part", "", take());
    sp::noteOn(sp::kUsb, 1, 60, 100);
    sp::noteOn(sp::kUsb, 1, 64, 100);
    take();
    sp::noteOff(sp::kUsb, 1, 60);
    expect("R4 note off with the pedal down: held, nothing sent", "", take());
    expect("R4 table shows 60 sustained", "60:2 64:1", table());
    sp::controlChange(sp::kUsb, 1, 64, 0);
    expect("R4 pedal up releases the sustained note only", "off 60 0 ch1", take());
    expect("R4 table after", "64:1", table());
    sp::controlChange(sp::kUsb, 1, 64, 127);
    sp::noteOff(sp::kUsb, 1, 64);
    sp::noteOn(sp::kUsb, 1, 64, 100);
    expect("R4 a sustained note struck again: off then on", "off 64 0 ch1, on 64 100 ch1", take());
    take();
    sp::controlChange(sp::kUsb, 1, 64, 63);
    sp::noteOff(sp::kUsb, 1, 64);
    expect("R4 pedal value 63 counts as up: the note off goes out", "off 64 0 ch1", take());
    reset();
    sp::value[sp::kSustain] = 0;
    sp::controlChange(sp::kUsb, 1, 64, 127);
    sp::noteOn(sp::kUsb, 1, 60, 100);
    sp::noteOff(sp::kUsb, 1, 60);
    expect("R4 Sustain off: the pedal is ignored", "on 60 100 ch1, off 60 0 ch1", take());
    reset();
    sp::controlChange(sp::kUsb, 1, 64, 127);
    sp::noteOn(sp::kUsb, 1, 60, 100);
    sp::noteOff(sp::kUsb, 1, 60);
    take();
    sp::value[sp::kSustain] = 0;
    sp::pedalOff();
    expect("R4 Sustain switched off lets the held notes go", "off 60 0 ch1", take());
    expect("R4 pedal state after", "0", std::to_string(sp::pedalDown() ? 1 : 0));

    // R5
    reset();
    sp::noteOn(sp::kUsb, 1, 60, 100);
    sp::noteOn(sp::kUsb, 1, 64, 100);
    sp::controlChange(sp::kUsb, 1, 64, 127);
    sp::noteOff(sp::kUsb, 1, 64);
    take();
    const bool took = sp::controlChange(sp::kUsb, 1, 123, 0);
    expect("R5 CC 123 is taken", "1", std::to_string(took));
    expect("R5 CC 123: note offs for the table, CC 123 to the part, bend centred", "off 60 0 ch1, off 64 0 ch1, cc 123 0 ch1, bend 0 0 ch1", take());
    expect("R5 table and pedal empty after CC 123", "", table() + (sp::pedalDown() ? "pedal" : ""));
    sp::noteOn(sp::kUsb, 1, 62, 100);
    take();
    sp::controlChange(sp::kDin, 1, 120, 0);
    expect("R5 CC 120 does the same", "off 62 0 ch1, cc 123 0 ch1, bend 0 0 ch1", take());
    sp::pitchBend(sp::kUsb, 1, 4000);
    sp::controlChange(sp::kUsb, 1, 64, 127);
    take();
    sp::controlChange(sp::kUsb, 1, 121, 0);
    expect("R5 CC 121: bend centred, mod wheel macro to 0, pedal lifted", "bend 0 0 ch1, cc 102 0 ch1", take());
    expect("R5 CC 121 lifted the pedal", "0", std::to_string(sp::pedalDown() ? 1 : 0));
    expect("R5 CC 7 is not the page's", "0", std::to_string(sp::controlChange(sp::kUsb, 1, 7, 100)));
    sp::value[sp::kChannel] = 3;
    expect("R5 CC 123 of another channel is not taken", "0", std::to_string(sp::controlChange(sp::kUsb, 1, 123, 0)));
    take();

    // R6
    reset();
    sp::value[sp::kPart] = 1;
    sp::noteOn(sp::kUsb, 1, 60, 100);
    sp::noteOn(sp::kUsb, 1, 72, 100);
    sp::controlChange(sp::kUsb, 1, 64, 127);
    sp::noteOff(sp::kUsb, 1, 72);
    sp::pitchBend(sp::kUsb, 1, -5000);
    take();
    sp::releaseAll();
    expect("R6 exit: every note off (held and sustained), CC 123, bend 0 on part 1 = channel 2", "off 60 0 ch2, off 72 0 ch2, cc 123 0 ch2, bend 0 0 ch2", take());
    expect("R6 nothing left", "", table() + (sp::pedalDown() ? "pedal" : ""));
    sp::releaseAll();
    expect("R6 a second exit repeats only CC 123 and the bend", "cc 123 0 ch2, bend 0 0 ch2", take());

    // R7
    reset();
    sp::pitchBend(sp::kUsb, 1, 8191);
    sp::pitchBend(sp::kUsb, 1, -8192);
    sp::pitchBend(sp::kUsb, 1, 9000);
    sp::pitchBend(sp::kUsb, 1, -9000);
    expect("R7 bend goes through, clamped to 14 bit", "bend 8191 0 ch1, bend -8192 0 ch1, bend 8191 0 ch1, bend -8192 0 ch1", take());
    sp::pitchBend(sp::kUsb, 1, 1234);
    take();
    sp::resendBend();
    expect("R7 resendBend repeats the wheel", "bend 1234 0 ch1", take());
    sp::controlChange(sp::kUsb, 1, 1, 77);
    expect("R7 mod wheel to macro 1 = CC 102", "cc 102 77 ch1", take());
    sp::value[sp::kMod] = 4;
    sp::controlChange(sp::kUsb, 1, 1, 12);
    expect("R7 mod wheel to macro 4 = CC 105", "cc 105 12 ch1", take());
    sp::value[sp::kMod] = 0;
    expect("R7 mod wheel off: taken, nothing sent", "1", std::to_string(sp::controlChange(sp::kUsb, 1, 1, 50)));
    expect("R7 mod wheel off sends nothing", "", take());

    // R8
    reset();
    sp::value[sp::kInput] = 1;
    sp::noteOn(sp::kDin, 1, 60, 100);
    sp::noteOn(sp::kUsb, 1, 61, 100);
    expect("R8 Input USB: DIN ignored", "on 61 100 ch1", take());
    sp::value[sp::kInput] = 2;
    sp::noteOn(sp::kUsb, 1, 62, 100);
    sp::noteOn(sp::kDin, 1, 63, 100);
    sp::pitchBend(sp::kUsb, 1, 100);
    sp::pitchBend(sp::kDin, 1, 200);
    expect("R8 Input DIN: USB notes and bend ignored", "on 63 100 ch1, bend 200 0 ch1", take());
    sp::value[sp::kInput] = 0;
    sp::value[sp::kChannel] = 5;
    sp::noteOn(sp::kUsb, 4, 64, 100);
    sp::noteOn(sp::kUsb, 5, 65, 100);
    sp::noteOff(sp::kUsb, 4, 65);
    expect("R8 Channel 5: other channels ignored (on and off)", "on 65 100 ch1", take());
    sp::noteOn(sp::kPad, 0, 66, 100);
    expect("R8 pads pass the input and channel filters", "on 66 100 ch1", take());
    reset();
    sp::value[sp::kInput] = 2;
    sp::value[sp::kChannel] = 9;
    sp::noteOn(sp::kPad, 0, 40, 100);
    sp::noteOff(sp::kPad, 0, 40);
    expect("R8 pad on and off with a filter that matches no port", "on 40 100 ch1, off 40 0 ch1", take());
    reset();
    sp::value[sp::kTranspose] = 12;
    sp::noteOn(sp::kUsb, 1, 60, 100);
    sp::value[sp::kTranspose] = -5;   // changed while the note is down: the off still goes to the note that was sent
    sp::noteOff(sp::kUsb, 1, 60);
    expect("R8 transpose +12, then the off of the same key after a change to -5", "on 72 100 ch1, off 72 0 ch1", take());
    sp::noteOn(sp::kUsb, 1, 3, 100);
    sp::value[sp::kTranspose] = 24;
    sp::noteOn(sp::kUsb, 1, 120, 100);
    expect("R8 transpose clamps to 0..127", "on 0 100 ch1, on 127 100 ch1", take());

    // R9
    reset();
    sp::noteOn(sp::kUsb, 1, 60, 100);
    take();
    sp::setPart(2);
    expect("R9 setPart releases the old part (channel 1)", "off 60 0 ch1, cc 123 0 ch1, bend 0 0 ch1", take());
    sp::noteOn(sp::kUsb, 1, 62, 100);
    expect("R9 new notes go to channel 3", "on 62 100 ch3", take());
    sp::setPart(2);
    expect("R9 setPart to the same part does nothing", "", take());

    // R10
    reset();
    sp::value[sp::kPartB] = 2;   // Synth 2 = channel 2
    sp::noteOn(sp::kUsb, 1, 60, 100);
    sp::noteOn(sp::kUsb, 1, 64, 90);
    expect("R10 two note ons reach part A (ch1) then part B (ch2)", "on 60 100 ch1, on 60 100 ch2, on 64 90 ch1, on 64 90 ch2", take());
    expect("R10 the table holds one entry per key", "60:1 64:1", table());
    sp::noteOff(sp::kUsb, 1, 60);
    expect("R10 note off reaches both", "off 60 0 ch1, off 60 0 ch2", take());
    sp::pitchBend(sp::kUsb, 1, 1000);
    expect("R10 bend reaches both", "bend 1000 0 ch1, bend 1000 0 ch2", take());
    sp::resendBend();
    expect("R10 resendBend reaches both", "bend 1000 0 ch1, bend 1000 0 ch2", take());
    sp::controlChange(sp::kUsb, 1, 1, 77);
    expect("R10 mod wheel macro reaches both", "cc 102 77 ch1, cc 102 77 ch2", take());
    sp::controlChange(sp::kUsb, 1, 123, 0);
    expect("R10 CC 123 releases both", "off 64 0 ch1, off 64 0 ch2, cc 123 0 ch1, cc 123 0 ch2, bend 0 0 ch1, bend 0 0 ch2", take());
    sp::noteOn(sp::kUsb, 1, 67, 100);
    sp::noteOn(sp::kUsb, 1, 67, 100);
    expect("R10 a key struck again: off to both, then on to both", "on 67 100 ch1, on 67 100 ch2, off 67 0 ch1, off 67 0 ch2, on 67 100 ch1, on 67 100 ch2", take());
    sp::setPartB(3);
    expect("R10 setPartB releases both sides first (part B still channel 2), then plays on 1 and 3", "off 67 0 ch1, off 67 0 ch2, cc 123 0 ch1, cc 123 0 ch2, bend 0 0 ch1, bend 0 0 ch2", take());
    sp::noteOn(sp::kUsb, 1, 70, 100);
    expect("R10 after setPartB(3) the notes go to channels 1 and 3", "on 70 100 ch1, on 70 100 ch3", take());
    sp::setPartB(3);
    expect("R10 setPartB to the same part does nothing", "", take());
    sp::setPartB(0);
    expect("R10 B off: releases both, nothing more", "off 70 0 ch1, off 70 0 ch3, cc 123 0 ch1, cc 123 0 ch3, bend 0 0 ch1, bend 0 0 ch3", take());
    sp::noteOn(sp::kUsb, 1, 72, 100);
    sp::pitchBend(sp::kUsb, 1, 5);
    expect("R10 B off: the single part page again (channel 1 only)", "on 72 100 ch1, bend 5 0 ch1", take());
    reset();
    sp::value[sp::kPart] = 1;
    sp::value[sp::kPartB] = 1;
    sp::noteOn(sp::kUsb, 2, 60, 100);
    expect("R10 part A = Synth 2 (ch2), part B = Synth 1 (ch1): the order is A then B", "on 60 100 ch2, on 60 100 ch1", take());

    // R11
    reset();
    sp::controlChange(sp::kUsb, 1, 11, 0);
    expect("R11 CC 11 = 0: morph 0, A at 1, B at 0 (B off: only part A, 1)", "0|gain 0 1.0000", std::to_string(sp::value[sp::kMorph]) + "|" + take());
    sp::value[sp::kPartB] = 2;
    sp::controlChange(sp::kUsb, 1, 11, 0);
    expect("R11 B on, CC 11 = 0: A at 1, B at 0", "gain 0 1.0000, gain 1 0.0000", take());
    sp::controlChange(sp::kUsb, 1, 11, 127);
    expect("R11 CC 11 = 127: morph 100, A at 0, B at 1", "100|gain 0 0.0000, gain 1 1.0000", std::to_string(sp::value[sp::kMorph]) + "|" + take());
    sp::controlChange(sp::kUsb, 1, 11, 64);
    expect("R11 CC 11 = 64: morph 50, both at 0.7071", "50|gain 0 0.7071, gain 1 0.7071", std::to_string(sp::value[sp::kMorph]) + "|" + take());
    sp::value[sp::kChannel] = 3;
    take();
    expect("R11 CC 11 on another channel than the page's is not taken, the morph stays", "0|50", std::to_string(sp::controlChange(sp::kUsb, 5, 11, 0)) + "|" + std::to_string(sp::value[sp::kMorph]));
    sp::value[sp::kChannel] = 0;
    float prev = -1.0f, worst = 0.0f;
    bool mono = true;
    for (int m = 0; m <= 100; m++)
    {
        const float a = sp::morphGain(100 - m), b = sp::morphGain(m);
        mono = mono && b >= prev;
        prev = b;
        const float err = a * a + b * b - 1.0f;
        worst = std::max(worst, err < 0 ? -err : err);
    }
    expect("R11 the sin table is monotonic over 0..100", "1", std::to_string(mono));
    expect("R11 equal power: gA^2 + gB^2 within 0.3 % of 1 at every step (worst error printed in 1e-4)", "1", std::to_string(worst < 0.003f));
    expect("R11 the ends are exact", "0.0000 1.0000 0.0000 1.0000", [&] {
        char t[64];
        snprintf(t, sizeof t, "%.4f %.4f %.4f %.4f", sp::morphGain(0), sp::morphGain(100), sp::morphGain(-5), sp::morphGain(120));
        return std::string(t);
    }());
    sp::value[sp::kPartB] = 0;
    sp::value[sp::kMorph] = 80;
    take();
    sp::applyMorph();
    expect("R11 applyMorph with B off: part A at 1 whatever the morph is", "gain 0 1.0000", take());

    // R12
    reset();
    take();
    sp::setProgramHook(pcHook);
    expect("R12 part A on channel 1: the hook gets side 0 and the program, the router takes it", "pc side 0 prog 5|1", [&] {
        const bool took = sp::programChange(sp::kUsb, 1, 5);
        return take() + "|" + std::to_string(took);
    }());
    expect("R12 channel 2 with B off: not a side, nothing happens", "|0", [&] {
        const bool took = sp::programChange(sp::kDin, 2, 5);
        return take() + "|" + std::to_string(took);
    }());
    sp::value[sp::kPartB] = 3;
    expect("R12 B on Synth 3: channel 3 is side 1, channel 1 still side 0, channel 2 nothing", "pc side 1 prog 0|pc side 0 prog 127|", [&] {
        sp::programChange(sp::kUsb, 3, 0);
        const std::string a = take();
        sp::programChange(sp::kDin, 1, 127);
        const std::string b = take();
        sp::programChange(sp::kUsb, 2, 9);
        return a + "|" + b + "|" + take();
    }());
    sp::value[sp::kPart] = 1;
    expect("R12 Synth A is part 1: channel 2 is side 0", "pc side 0 prog 4", [&] {
        sp::programChange(sp::kUsb, 2, 4);
        return take();
    }());
    sp::value[sp::kPart] = 0;
    sp::value[sp::kPartB] = 0;
    sp::value[sp::kInput] = 2;
    sp::programChange(sp::kUsb, 1, 1);
    sp::programChange(sp::kDin, 1, 2);
    expect("R12 Input DIN: the USB program is ignored", "pc side 0 prog 2", take());
    sp::value[sp::kInput] = 0;
    sp::value[sp::kChannel] = 5;
    sp::programChange(sp::kUsb, 1, 3);
    expect("R12 Channel 5 setting: a program on channel 1 is ignored", "", take());
    sp::value[sp::kChannel] = 0;
    sp::setProgramHook(nullptr);
    expect("R12 no hook (page closed): the channel matches and nothing is logged", "|1", [&] {
        const bool took = sp::programChange(sp::kUsb, 1, 7);
        return take() + "|" + std::to_string(took);
    }());

    // R14 releaseSource: the USB cable is pulled, only a plain note off per USB note goes out (no CC 123, no bend reset of a centred wheel)
    reset();
    sp::noteOn(sp::kUsb, 1, 60, 100);
    sp::noteOn(sp::kDin, 1, 64, 100);
    sp::noteOn(sp::kPad, 0, 67, 100);
    sp::noteOn(sp::kUsb, 1, 72, 100);
    take();
    sp::releaseSource(sp::kUsb);
    expect("R14 USB notes get their note off, DIN and pad notes stay, nothing else is sent", "off 60 0 ch1, off 72 0 ch1", take());
    expect("R14 table", "64:1 67:1", table());
    sp::releaseSource(sp::kUsb);
    expect("R14 a second pull sends nothing", "", take());
    reset();
    sp::noteOn(sp::kUsb, 1, 60, 100);
    sp::noteOn(sp::kDin, 1, 64, 100);
    sp::controlChange(sp::kUsb, 1, 64, 127);
    sp::noteOff(sp::kDin, 1, 64);
    sp::pitchBend(sp::kUsb, 1, 1000);
    take();
    sp::releaseSource(sp::kUsb);
    expect("R14 a USB pedal: the sustained DIN note goes with it, the bend is centred, the USB note gets its off", "off 64 0 ch1, bend 0 0 ch1, off 60 0 ch1", take());
    expect("R14 table empty, pedal up", "|0", table() + "|" + std::to_string(sp::pedalDown()));
    reset();
    sp::noteOn(sp::kUsb, 1, 60, 100);
    sp::controlChange(sp::kDin, 1, 64, 127);
    take();
    sp::releaseSource(sp::kUsb);
    expect("R14 a DIN pedal holds the USB note: no note off yet", "", take());
    expect("R14 it is sustained", "60:2", table());
    sp::controlChange(sp::kDin, 1, 64, 0);
    expect("R14 the DIN pedal up releases it", "off 60 0 ch1", take());
    // R14 the cable pull lets go of the MPE member notes too (mpe-in-p1 with synthplay-usbpull): the note off goes out under the member tag of its note
    reset();
    sp::value[sp::kMpeZone] = 15;
    sp::noteOn(sp::kUsb, 2, 60, 100);
    sp::noteOn(sp::kUsb, 3, 64, 100);
    sp::noteOn(sp::kDin, 1, 67, 100);
    take();
    sp::releaseSource(sp::kUsb);
    expect("R14 a USB cable pull ends the member notes on their own voices, a DIN note stays", "mpe 2, off 60 0 ch1, mpe 3, off 64 0 ch1|67:1", take() + "|" + table());
    sp::releaseAll();
    take();
    sp::value[sp::kMpeZone] = 0;

    // R13 MPE in, lower zone (ref/mpe-in-p1.md). reset() leaves the zone off, every row turns it on itself
    auto zone = [](int n) { sp::value[sp::kMpeZone] = int8_t(n); };
    reset();
    zone(15);
    sp::noteOn(sp::kUsb, 2, 60, 100);
    expect("R13 a note on member channel 2 is tagged for the part, then sent on the part's channel", "mpe 2, on 60 100 ch1", take());
    sp::noteOn(sp::kUsb, 3, 60, 90);
    expect("R13 the same pitch on channel 3 is its own cell (the key is channel and note)", "mpe 3, on 60 90 ch1|60:1 60:1", take() + "|" + table());
    sp::noteOff(sp::kUsb, 2, 60);
    expect("R13 the note off of channel 2 ends channel 2's note only", "mpe 2, off 60 0 ch1|60:1", take() + "|" + table());
    sp::noteOn(sp::kUsb, 3, 62, 90);
    expect("R13 a new note on a member channel that still holds one ends the old first (one note per channel)", "mpe 3, off 60 0 ch1, mpe 3, on 62 90 ch1|62:1", take() + "|" + table());
    sp::pitchBend(sp::kUsb, 3, 1000);
    sp::pitchBend(sp::kDin, 1, -500);
    expect("R13 a member bend is that note's (raw value), the master channel's is the part's wheel", "nbend p0 ch3 1000, bend -500 0 ch1", take());
    sp::controlChange(sp::kUsb, 1, 64, 127);
    sp::noteOff(sp::kUsb, 3, 62);
    expect("R13 the master channel's pedal holds a member's note off", "|62:2", take() + "|" + table());
    sp::controlChange(sp::kUsb, 1, 64, 0);
    expect("R13 pedal up lets it go on its channel", "mpe 3, off 62 0 ch1|", take() + "|" + table());
    sp::controlChange(sp::kUsb, 4, 64, 127);
    expect("R13 a member channel's own pedal is not the sustain", "0", std::to_string(sp::pedalDown()));
    expect("R13 slide (CC 74) of a member goes to the Mod wheel macro (macro 1 = CC 102)", "cc 102 77 ch1|1", [&] {
        const bool took = sp::controlChange(sp::kUsb, 5, 74, 77);
        return take() + "|" + std::to_string(took);
    }());
    sp::value[sp::kMod] = 3;
    sp::controlChange(sp::kUsb, 5, 74, 5);
    expect("R13 Mod wheel set to macro 3: slide follows (CC 104)", "cc 104 5 ch1", take());
    sp::value[sp::kMod] = 0;
    expect("R13 Mod wheel off: slide is not taken (the Config CC routes see it)", "|0", [&] {
        const bool took = sp::controlChange(sp::kUsb, 5, 74, 5);
        return take() + "|" + std::to_string(took);
    }());
    sp::value[sp::kMod] = 1;
    expect("R13 CC 74 on the master channel is not slide", "|0", [&] {
        const bool took = sp::controlChange(sp::kUsb, 1, 74, 9);
        return take() + "|" + std::to_string(took);
    }());
    expect("R13 channel pressure of a member is that note's, of the master channel the part's (channel 1 as the page's part)", "npress p0 ch4 50, npress p0 ch1 9|1 1", [&] {
        const bool a = sp::channelPressure(sp::kUsb, 4, 50);
        const bool b = sp::channelPressure(sp::kDin, 1, 9);
        return take() + "|" + std::to_string(a) + " " + std::to_string(b);
    }());
    expect("R13 pressure outside the zone is dropped", "|0", [&] {
        zone(3);   // members are 2..4
        const bool took = sp::channelPressure(sp::kUsb, 5, 50);
        zone(15);
        return take() + "|" + std::to_string(took);
    }());
    sp::controlChange(sp::kUsb, 6, 121, 0);
    expect("R13 CC 121 on a member resets that channel only (bend 0, pressure full), the master wheel and pedal stay", "nbend p0 ch6 0, npress p0 ch6 127", take());
    sp::controlChange(sp::kUsb, 1, 121, 0);
    const std::string r121 = take();
    expect("R13 CC 121 on the master channel resets the master wheel and every member 2..16 (14 more calls)", "1|1|1", std::to_string(r121.rfind("bend 0 0 ch1, nbend p0 ch2 0, npress p0 ch2 127, nbend p0 ch3 0", 0) == 0) + "|" +
           std::to_string(r121.find("nbend p0 ch16 0, npress p0 ch16 127") != std::string::npos) + "|" + std::to_string(std::count(r121.begin(), r121.end(), 'n') > 40));
    sp::noteOn(sp::kUsb, 2, 50, 100);
    sp::noteOn(sp::kUsb, 9, 52, 100);
    g_log.clear();
    sp::controlChange(sp::kDin, 1, 123, 0);
    expect("R13 CC 123 on the master channel lets go of every member note and the part", "mpe 2, off 50 0 ch1, mpe 9, off 52 0 ch1, cc 123 0 ch1, bend 0 0 ch1|", take() + "|" + table());

    // zone Off and the channel filter
    zone(0);
    sp::noteOn(sp::kUsb, 2, 60, 100);
    sp::pitchBend(sp::kUsb, 2, 700);
    expect("R13 zone Off: channel 2 is a plain note and its bend is the part's wheel, no MPE call", "on 60 100 ch1, bend 700 0 ch1", take());
    expect("R13 zone Off: pressure and slide are dropped", "|0|0", [&] {
        const bool a = sp::channelPressure(sp::kUsb, 2, 50);
        const bool b = sp::controlChange(sp::kUsb, 2, 74, 5);
        return take() + "|" + std::to_string(a) + "|" + std::to_string(b);
    }());
    sp::releaseAll();
    g_log.clear();
    zone(2);   // members 2..3
    sp::value[sp::kChannel] = 1;
    sp::noteOn(sp::kUsb, 3, 60, 100);
    sp::noteOn(sp::kUsb, 4, 61, 100);
    sp::noteOn(sp::kUsb, 1, 62, 100);
    expect("R13 Channel 1 (the master) takes the whole zone: channel 3 yes, channel 4 outside the zone no, channel 1 plain", "mpe 3, on 60 100 ch1, on 62 100 ch1", take());
    sp::value[sp::kChannel] = 5;
    sp::noteOn(sp::kUsb, 2, 64, 100);
    expect("R13 Channel 5 setting: the zone is not heard", "", take());
    sp::value[sp::kChannel] = 0;
    sp::releaseAll();
    g_log.clear();

    // MPE Configuration Message and RPN 0
    zone(15);
    const uint32_t chg0 = sp::mpeChanged;
    sp::controlChange(sp::kUsb, 1, 101, 0);
    sp::controlChange(sp::kUsb, 1, 100, 6);
    sp::noteOn(sp::kUsb, 2, 60, 100);
    g_log.clear();
    const bool d1 = sp::controlChange(sp::kUsb, 1, 6, 4);
    expect("R13 RPN 6 data 4 on channel 1: zone is 4 members, the notes that sounded under the old zone go, the parts get the range again",
           "4|1|mpe 2, off 60 0 ch1, cc 123 0 ch1, bend 0 0 ch1, mpecfg p0 48 0|1|", std::to_string(sp::value[sp::kMpeZone]) + "|" + std::to_string(d1) + "|" + take() + "|" + std::to_string(sp::mpeChanged - chg0) + "|" + table());
    sp::controlChange(sp::kUsb, 1, 6, 4);
    expect("R13 the same message again changes nothing and lets go of nothing", "|1", take() + "|" + std::to_string(sp::mpeChanged - chg0));
    sp::controlChange(sp::kUsb, 1, 6, 40);
    expect("R13 more than 15 members is 15", "15", std::to_string(sp::value[sp::kMpeZone]));
    sp::controlChange(sp::kUsb, 16, 101, 0);
    sp::controlChange(sp::kUsb, 16, 100, 6);
    sp::controlChange(sp::kUsb, 16, 6, 3);
    expect("R13 the upper zone (channel 16) is not supported: zone unchanged", "15", std::to_string(sp::value[sp::kMpeZone]));
    sp::controlChange(sp::kDin, 1, 101, 0);
    sp::controlChange(sp::kDin, 1, 100, 6);
    sp::controlChange(sp::kDin, 1, 6, 0);
    expect("R13 data 0 switches the zone off (DIN works too)", "0", std::to_string(sp::value[sp::kMpeZone]));
    sp::controlChange(sp::kUsb, 1, 6, 7);
    expect("R13 RPN stays selected: another data entry sets it again, then the null RPN deselects", "7|0", [&] {
        const int z = sp::value[sp::kMpeZone];
        sp::controlChange(sp::kUsb, 1, 101, 127);
        sp::controlChange(sp::kUsb, 1, 100, 127);
        const bool took = sp::controlChange(sp::kUsb, 1, 6, 1);
        return std::to_string(z) + "|" + std::to_string(took);
    }());
    sp::value[sp::kMpeZone] = 7;
    g_log.clear();
    sp::controlChange(sp::kUsb, 2, 101, 0);
    sp::controlChange(sp::kUsb, 2, 100, 0);
    sp::controlChange(sp::kUsb, 2, 6, 24);
    expect("R13 RPN 0 on a member channel sets the member bend range", "24|mpecfg p0 24 0", std::to_string(sp::value[sp::kMpeRange]) + "|" + take());
    sp::controlChange(sp::kUsb, 1, 101, 0);
    sp::controlChange(sp::kUsb, 1, 100, 0);
    sp::controlChange(sp::kUsb, 1, 6, 12);
    expect("R13 RPN 0 on the master channel leaves the page's Bend and the range alone", "2|24|", std::to_string(sp::value[sp::kBend]) + "|" + std::to_string(sp::value[sp::kMpeRange]) + "|" + take());
    sp::value[sp::kInput] = 2;
    sp::controlChange(sp::kUsb, 1, 101, 0);
    sp::controlChange(sp::kUsb, 1, 100, 6);
    sp::controlChange(sp::kUsb, 1, 6, 3);
    expect("R13 Input DIN: a zone message over USB is ignored", "7", std::to_string(sp::value[sp::kMpeZone]));
    sp::value[sp::kInput] = 0;
    sp::controlChange(sp::kUsb, 1, 101, 0);
    sp::controlChange(sp::kUsb, 1, 100, 1);
    expect("R13 another RPN's data entry is not taken (left to the Config CC routes)", "0", std::to_string(sp::controlChange(sp::kUsb, 1, 6, 5)));
    sp::controlChange(sp::kUsb, 1, 101, 127);
    sp::controlChange(sp::kUsb, 1, 100, 127);

    // A/B: both parts get the member's notes, bend and pressure
    sp::value[sp::kPartB] = 2;
    sp::releaseAll();
    g_log.clear();
    sp::noteOn(sp::kUsb, 5, 60, 100);
    sp::pitchBend(sp::kUsb, 5, 300);
    sp::channelPressure(sp::kUsb, 5, 20);
    expect("R13 A/B (part B on Synth 2): the note, bend and pressure go to both parts", "mpe 5, on 60 100 ch1, on 60 100 ch2, nbend p0 ch5 300, nbend p1 ch5 300, npress p0 ch5 20, npress p1 ch5 20", take());
    sp::applyMpe(false);
    sp::value[sp::kMpeRange] = 12;
    sp::value[sp::kMpePress] = 60;
    g_log.clear();
    sp::applyMpe(false);
    expect("R13 applyMpe puts the range and pressure amount on both parts, a range change lets go of nothing", "mpecfg p0 12 60, mpecfg p1 12 60", take());

    printf("%s (%d checks, %d failures)\n", g_fail ? "FAILED" : "ok", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
