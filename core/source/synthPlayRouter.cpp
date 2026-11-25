#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX) || defined(SYNTHPLAY_TEST)

#include "synthPlayRouter.h"

#include "mtMidi.h"

namespace synthplay
{
int8_t value[kValues] = { 0, 0, 8, 1, 100, 2, 1, 1, 0, 0, 0, 50, 0, 0, 15, 48, 0 };   // the defaults: USB + DIN, Omni, 8 voices, Linear, fixed 100, bend 2 st, mod to macro 1, pedal on, Synth 1, no transpose, B off, morph 50, banks 1, MPE lower zone of 15 members (the channels 2..16 of a ROLI Seaboard), 48 st, pressure off
uint8_t changed = 0;
uint8_t mpeChanged = 0;

namespace
{
Cell s_cell[kCells];
uint8_t s_seq;
bool s_pedal;
uint8_t s_pedalSrc;   // the source of the last pedal CC that put it down
int s_bend;
uint8_t s_bendSrc;    // the source of the last bend that moved the wheel off centre
void (*s_programHook)(int side, uint8_t program);
// RPN selection per channel group (0: the master channel 1, 1: any other): MSB, LSB, 127 127 is the null RPN. MPE Configuration Message (RPN 6) and the member bend range (RPN 0)
uint8_t s_rpn[2][2] = { { 127, 127 }, { 127, 127 } };

// MPE lower zone: the member channel (2..N+1) a channel stands for, 0 for the master channel, a channel outside the zone and the zone off
uint8_t memberOf(uint8_t channel)
{
    return value[kMpeZone] != 0 && channel >= 2 && channel <= 1 + value[kMpeZone] ? channel : uint8_t(0);
}

uint8_t ch()
{
    return uint8_t(value[kPart] + 1);
}

// The MIDI channels of the parts that play: part A, and part B when it is on (the channel of a part is its number)
int channels(uint8_t (&out)[2])
{
    out[0] = ch();
    out[1] = uint8_t(value[kPartB]);
    return value[kPartB] ? 2 : 1;
}

bool accept(uint8_t src, uint8_t channel)
{
    if (src == kPad)
    {
        return true;
    }
    if (value[kInput] != 0 && value[kInput] != src)
    {
        return false;
    }
    if (value[kChannel] == 0 || value[kChannel] == channel)
    {
        return true;
    }
    return value[kChannel] == 1 && memberOf(channel) != 0;   // the page listens to the master channel: its zone is part of it
}

Cell* find(uint8_t note, uint8_t mch)
{
    for (Cell& c : s_cell)
    {
        if (c.state != kFree && c.in == note && c.ch == mch)
        {
            return &c;
        }
    }
    return nullptr;
}

// The velocity the part gets: the curve of the page, 1..127
uint8_t shape(uint8_t vel)
{
    int v = vel;
    switch (value[kVelCurve])
    {
        case 0: v = 127 - (127 - v) * (127 - v) / 127; break;   // Soft: the middle comes out louder
        case 2: v = v * v / 127; break;                         // Hard: the middle comes out quieter
        case 3: v = value[kFixedVel]; break;
        default: break;
    }
    return uint8_t(v < 1 ? 1 : (v > 127 ? 127 : v));
}

void sendOff(const Cell& c, const uint8_t (&cs)[2], int n)
{
    synthPlayMpeNote(c.ch);   // a member note ends the voice of its channel, not every voice of the pitch
    for (int i = 0; i < n; i++)
    {
        sendMidiNoteOff(c.out, 0, cs[i], true);
    }
    synthPlayMpeNote(0);
}

void stop(Cell& c)
{
    uint8_t cs[2];
    sendOff(c, cs, channels(cs));
    c.state = kFree;
    changed++;
}

void releaseSustained()
{
    for (Cell& c : s_cell)
    {
        if (c.state == kSustained)
        {
            stop(c);
        }
    }
}

#if defined(FEATURE_SYNTH_OPEN) || defined(SYNTHPLAY_TEST)
void sendBend()
{
    uint8_t cs[2];
    for (int i = 0, n = channels(cs); i < n; i++)
    {
        sendPitchbend(int16_t(s_bend), int8_t(cs[i]), true);
    }
}
#else
// Tracker+ and Mini: the stock parts have a fixed bend range of their own (kLibBendSemis for a full wheel) and no setter, so the router
// scales the wheel by Bend / kLibBendSemis and clips it at the full deflection (a Bend above the lib's range ends at the lib's range).
// The open parts scale it themselves (open::partBendRange, set by the page)
constexpr int kLibBendSemis = 2;
void sendBend()
{
    const int b = s_bend * value[kBend] / kLibBendSemis;
    const int16_t out = int16_t(b < -8192 ? -8192 : (b > 8191 ? 8191 : b));
    uint8_t cs[2];
    for (int i = 0, n = channels(cs); i < n; i++)
    {
        sendPitchbend(out, int8_t(cs[i]), true);
    }
}
#endif

// CC `number` to every part that plays
void sendMacro(uint8_t number, uint8_t val)
{
    uint8_t cs[2];
    for (int i = 0, n = channels(cs); i < n; i++)
    {
        sendCC(number, val, cs[i], true);
    }
}

// cos and sin of a quarter turn, 17 points of 32767 sin(k pi / 32)
const int16_t kQuarterSine[17] = { 0, 3212, 6393, 9512, 12539, 15446, 18204, 20787, 23170, 25329, 27245, 28898, 30273, 31356, 32137, 32609, 32767 };

void pedal(bool down, uint8_t src = kPad)
{
    if (down == s_pedal)
    {
        return;
    }
    s_pedal = down;
    s_pedalSrc = src;
    if (!down)
    {
        releaseSustained();
    }
    changed++;
}
}   // namespace

void noteOn(uint8_t src, uint8_t channel, uint8_t note, uint8_t velocity)
{
    if (!accept(src, channel))
    {
        return;
    }
    if (velocity == 0)
    {
        noteOff(src, channel, note);
        return;
    }
    const uint8_t mch = memberOf(channel);
    if (mch != 0)
    {
        for (Cell& o : s_cell)   // one note per member channel: what the channel still holds ends first (a lost note off is no stuck note)
        {
            if (o.state != kFree && o.ch == mch && o.in != note)
            {
                stop(o);
            }
        }
    }
    Cell* c = find(note, mch);
    if (c)
    {
        stop(*c);   // played again while it sounds (held, or the pedal holds it): the part gets a note off first, one voice per note
    }
    else
    {
        for (Cell& f : s_cell)
        {
            if (f.state == kFree)
            {
                c = &f;
                break;
            }
        }
        if (!c)   // more than 8 notes down: the oldest goes (the part steals its voice the same way)
        {
            c = &s_cell[0];
            for (Cell& f : s_cell)
            {
                if (uint8_t(s_seq - f.seq) > uint8_t(s_seq - c->seq))
                {
                    c = &f;
                }
            }
            stop(*c);
        }
    }
    const int out = note + value[kTranspose];
    c->in = note;
    c->out = uint8_t(out < 0 ? 0 : (out > 127 ? 127 : out));
    c->vel = shape(velocity);
    c->state = kHeld;
    c->seq = ++s_seq;
    c->src = src;
    c->ch = mch;
    changed++;
    uint8_t cs[2];
    synthPlayMpeNote(mch);
    for (int i = 0, n = channels(cs); i < n; i++)
    {
        sendMidiNoteOn(c->out, c->vel, cs[i], true);
    }
    synthPlayMpeNote(0);
}

void noteOff(uint8_t src, uint8_t channel, uint8_t note)
{
    if (!accept(src, channel))
    {
        return;
    }
    Cell* c = find(note, memberOf(channel));
    if (!c || c->state != kHeld)
    {
        return;
    }
    if (s_pedal && value[kSustain])
    {
        c->state = kSustained;
        changed++;
        return;
    }
    stop(*c);
}

void pitchBend(uint8_t src, uint8_t channel, int bend)
{
    if (!accept(src, channel))
    {
        return;
    }
    const int raw = bend < -8192 ? -8192 : (bend > 8191 ? 8191 : bend);
    if (const uint8_t m = memberOf(channel))
    {
        uint8_t cs[2];
        for (int i = 0, n = channels(cs); i < n; i++)
        {
            synthPlayMpeBend(uint8_t(cs[i] - 1), m, raw);   // that note only, the master wheel stays
        }
        return;
    }
    s_bend = raw;
    s_bendSrc = src;
    sendBend();
}

bool channelPressure(uint8_t src, uint8_t channel, uint8_t pressure)
{
    if (!accept(src, channel) || value[kMpeZone] == 0 || (channel != 1 && memberOf(channel) == 0))
    {
        return false;
    }
    uint8_t cs[2];
    for (int i = 0, n = channels(cs); i < n; i++)
    {
        synthPlayMpePress(uint8_t(cs[i] - 1), channel, pressure);
    }
    return true;
}

bool programChange(uint8_t src, uint8_t channel, uint8_t program)
{
    if (!accept(src, channel))
    {
        return false;
    }
    uint8_t cs[2];
    for (int side = 0, n = channels(cs); side < n; side++)
    {
        if (cs[side] == channel)
        {
            if (s_programHook != nullptr)
            {
                s_programHook(side, program);
            }
            return true;
        }
    }
    return false;
}

void setProgramHook(void (*hook)(int side, uint8_t program))
{
    s_programHook = hook;
}

// RPN 6 is the MPE Configuration Message (data MSB = the number of member channels of the lower zone, 0 switches it off), sent on the master channel 1.
// RPN 0 on a member channel is the member bend range in semitones (MSB). Both come in as CC 101, 100, 6 (and 38, the cents, not used). The Input filter
// applies, the Channel filter does not (a controller configures its zone on the channels it owns). True: taken
static bool rpnCC(uint8_t src, uint8_t channel, uint8_t number, uint8_t val)
{
    if (value[kInput] != 0 && value[kInput] != src)
    {
        return false;
    }
    uint8_t(&r)[2] = s_rpn[channel == 1 ? 0 : 1];
    if (number == 101 || number == 100)
    {
        r[number == 101 ? 0 : 1] = val;
        return true;
    }
    if (r[0] != 0 || (r[1] != 6 && r[1] != 0))
    {
        return false;   // another RPN, or none: the data entry is for the Config CC routes
    }
    if (number == 38)
    {
        return true;   // the cents (and the LSB of the zone message) are not used
    }
    if (r[1] == 6 && channel == 1)   // lower zone: N members, 0 off. The upper zone (channel 16) is not supported
    {
        const int n = val > 15 ? 15 : val;
        if (n != value[kMpeZone])
        {
            value[kMpeZone] = int8_t(n);
            mpeChanged++;
            applyMpe(true);
        }
    }
    else if (r[1] == 0 && memberOf(channel) != 0)
    {
        const int n = val < 1 ? 1 : (val > 96 ? 96 : val);
        if (n != value[kMpeRange])
        {
            value[kMpeRange] = int8_t(n);
            mpeChanged++;
            applyMpe(false);
        }
    }
    return true;
}

bool controlChange(uint8_t src, uint8_t channel, uint8_t number, uint8_t val)
{
    if (number == 101 || number == 100 || number == 6 || number == 38)
    {
        return rpnCC(src, channel, number, val);
    }
    if (number != 1 && number != 11 && number != 64 && number != 74 && number != 120 && number != 121 && number != 123)
    {
        return false;
    }
    if (!accept(src, channel))
    {
        return false;   // another port or channel: the Config CC routes see it as on every page
    }
    const uint8_t member = memberOf(channel);
    if (member != 0)
    {
        // MPE member channel. Slide (CC 74) goes to the Mod wheel macro (a part wide macro, the last finger wins), the pedal of a member is not the player's
        // sustain (the master channel's is), CC 121 resets that channel's note expression only, the rest as on any channel
        if (number == 74)
        {
            if (!value[kMod])
            {
                return false;
            }
            sendMacro(uint8_t(kMacroCC0 + value[kMod] - 1), val);
            return true;
        }
        if (number == 64)
        {
            return true;
        }
        if (number == 121)
        {
            uint8_t cs[2];
            for (int i = 0, n = channels(cs); i < n; i++)
            {
                synthPlayMpeBend(uint8_t(cs[i] - 1), member, 0);
                synthPlayMpePress(uint8_t(cs[i] - 1), member, 127);
            }
            return true;
        }
    }
    else if (number == 74)
    {
        return false;
    }
    switch (number)
    {
        case 1:
            if (value[kMod])
            {
                sendMacro(uint8_t(kMacroCC0 + value[kMod] - 1), val);
            }
            break;
        case 11:   // expression: the morph, 0..127 -> 0..100
            value[kMorph] = int8_t(val * 100 / 127);
            applyMorph();
            break;
        case 64:
            if (value[kSustain])
            {
                pedal(val >= 64, src);
            }
            break;
        case 121:   // reset controllers: wheel centred, pedal up, mod wheel at its low end
            s_bend = 0;
            sendBend();
            if (value[kMpeZone] != 0 && channel == 1)   // the master channel: the note expression of every member goes back too
            {
                uint8_t cs[2];
                for (int i = 0, n = channels(cs); i < n; i++)
                {
                    for (uint8_t m = 2; m <= 1 + value[kMpeZone]; m++)
                    {
                        synthPlayMpeBend(uint8_t(cs[i] - 1), m, 0);
                        synthPlayMpePress(uint8_t(cs[i] - 1), m, 127);
                    }
                }
            }
            pedal(false, src);
            if (value[kMod])
            {
                sendMacro(uint8_t(kMacroCC0 + value[kMod] - 1), 0);
            }
            break;
        default:   // 120 all sound off and 123 all notes off: both release the part, the part has no hold of its own
            releaseAll();
            break;
    }
    return true;
}

void releaseAll()
{
    uint8_t cs[2];
    const int n = channels(cs);
    for (Cell& c : s_cell)
    {
        if (c.state != kFree)
        {
            sendOff(c, cs, n);
            c.state = kFree;
        }
    }
    sendMacro(123, 0);   // what no table knows about: the parts release every voice they have
    s_pedal = false;
    s_bend = 0;
    sendBend();
    changed++;
}

void releaseSource(uint8_t src)
{
    if (s_pedal && s_pedalSrc == src)
    {
        pedal(false);   // its pedal is gone: the sustained notes go (of any source, the pedal was theirs)
    }
    if (s_bend != 0 && s_bendSrc == src)
    {
        s_bend = 0;
        sendBend();
    }
    for (Cell& c : s_cell)
    {
        if (c.state == kHeld && c.src == src)
        {
            if (s_pedal && value[kSustain])   // a pedal of the other source: the note off the cable never sent would have been held by it
            {
                c.state = kSustained;
                changed++;
            }
            else
            {
                stop(c);
            }
        }
    }
}

void applyMpe(bool release)
{
    if (release)
    {
        releaseAll();   // a note keyed under the old zone has no note off under the new one
    }
    uint8_t cs[2];
    for (int i = 0, n = channels(cs); i < n; i++)
    {
        synthPlayMpeConfig(uint8_t(cs[i] - 1), value[kMpeRange], value[kMpePress]);
    }
}

void setPart(uint8_t newPart)
{
    if (newPart == value[kPart])
    {
        return;
    }
    releaseAll();
    value[kPart] = int8_t(newPart);
}

void setPartB(uint8_t newB)
{
    if (newB == value[kPartB])
    {
        return;
    }
    releaseAll();
    value[kPartB] = int8_t(newB);
}

float morphGain(int morph)
{
    const int pos = (morph < 0 ? 0 : (morph > 100 ? 100 : morph)) * 16;
    const int i = pos / 100, f = pos % 100;
    const int a = kQuarterSine[i], b = kQuarterSine[i < 16 ? i + 1 : 16];
    return float(a * 100 + (b - a) * f) * (1.0f / (32767.0f * 100.0f));
}

void applyMorph()
{
    synthPlaySetGain(uint8_t(value[kPart]), value[kPartB] ? morphGain(100 - value[kMorph]) : 1.0f);
    if (value[kPartB])
    {
        synthPlaySetGain(uint8_t(value[kPartB] - 1), morphGain(value[kMorph]));
    }
}

void pedalOff()
{
    pedal(false);
}

void resendBend()
{
    sendBend();
}

const Cell* cells()
{
    return s_cell;
}

uint8_t sounding()
{
    uint8_t n = 0;
    for (const Cell& c : s_cell)
    {
        n += c.state != kFree;
    }
    return n;
}

bool pedalDown()
{
    return s_pedal;
}
}   // namespace synthplay

#endif
