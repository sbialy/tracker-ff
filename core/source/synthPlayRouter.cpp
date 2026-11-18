#if defined(FEATURE_SYNTH_OPEN) || defined(SYNTHPLAY_TEST)

#include "synthPlayRouter.h"

#include "mtMidi.h"

namespace synthplay
{
int8_t value[kValues] = { 0, 0, 8, 1, 100, 2, 1, 1, 0, 0, 0, 50, 0, 0 };   // the defaults: USB + DIN, Omni, 8 voices, Linear, fixed 100, bend 2 st, mod to macro 1, pedal on, Synth 1, no transpose, B off, morph 50, banks 1
uint8_t changed = 0;

namespace
{
Cell s_cell[kCells];
uint8_t s_seq;
bool s_pedal;
int s_bend;

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
    return value[kChannel] == 0 || value[kChannel] == channel;
}

Cell* find(uint8_t note)
{
    for (Cell& c : s_cell)
    {
        if (c.state != kFree && c.in == note)
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

void stop(Cell& c)
{
    uint8_t cs[2];
    for (int i = 0, n = channels(cs); i < n; i++)
    {
        sendMidiNoteOff(c.out, 0, cs[i], true);
    }
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

void sendBend()
{
    uint8_t cs[2];
    for (int i = 0, n = channels(cs); i < n; i++)
    {
        sendPitchbend(int16_t(s_bend), int8_t(cs[i]), true);
    }
}

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

void pedal(bool down)
{
    if (down == s_pedal)
    {
        return;
    }
    s_pedal = down;
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
    Cell* c = find(note);
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
    changed++;
    uint8_t cs[2];
    for (int i = 0, n = channels(cs); i < n; i++)
    {
        sendMidiNoteOn(c->out, c->vel, cs[i], true);
    }
}

void noteOff(uint8_t src, uint8_t channel, uint8_t note)
{
    if (!accept(src, channel))
    {
        return;
    }
    Cell* c = find(note);
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
    s_bend = bend < -8192 ? -8192 : (bend > 8191 ? 8191 : bend);
    sendBend();
}

bool controlChange(uint8_t src, uint8_t channel, uint8_t number, uint8_t val)
{
    if (number != 1 && number != 11 && number != 64 && number != 120 && number != 121 && number != 123)
    {
        return false;
    }
    if (!accept(src, channel))
    {
        return false;   // another port or channel: the Config CC routes see it as on every page
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
                pedal(val >= 64);
            }
            break;
        case 121:   // reset controllers: wheel centred, pedal up, mod wheel at its low end
            s_bend = 0;
            sendBend();
            pedal(false);
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
            for (int i = 0; i < n; i++)
            {
                sendMidiNoteOff(c.out, 0, cs[i], true);
            }
            c.state = kFree;
        }
    }
    sendMacro(123, 0);   // what no table knows about: the parts release every voice they have
    s_pedal = false;
    s_bend = 0;
    sendBend();
    changed++;
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
