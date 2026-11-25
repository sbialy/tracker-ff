#include "feature_flags.h"
#ifdef FEATURE_FEATURES

#include "midiRemote.h"

#include "Platform.h"
#include "core/interface.h"

namespace midi_remote
{
namespace
{
bool s_on;
uint8_t s_channel = 16;
uint8_t s_base = 36;
// Which source holds which key and pad (bit n = key or pad n), so a lost cable lets go of what came over it and nothing else. Both
// sources of one key or pad make it one remote hold that ends with the last release.
uint64_t s_heldKeys[kSources];
uint64_t s_heldPads[kSources];

// One key or pad of the remote goes down or up. The switch group keeps the held state next to the finger's (isButtonPressed, the hold
// repeat) and says whether the combined state changed, so a note on over a finger that holds the same key or pad sends no second press.
void send(uint8_t index, bool down, uint8_t velocity)
{
    Platform* platform = mtInterface.getPlatform();
    if (platform == nullptr)
    {
        return;
    }
    if (index < kKeys)
    {
        if (platform->getButtons()->setRemoteHeld(index, down))   // held before the press and until the release, as a finger is
        {
            mtInterface.buttonChange(index, down ? buttonPress : buttonRelease);
        }
    }
    else if (platform->getPads()->setRemoteHeld(index - kKeys, down))
    {
        if (down)
        {
            mtInterface.padPressed(uint8_t(index - kKeys), 0, 0, velocity);
        }
        else
        {
            mtInterface.padReleased(uint8_t(index - kKeys));
        }
    }
}

// A source presses or releases one key or pad: its own bit follows, the switch group hears about the change only when no other
// source holds the same key or pad.
void apply(Source source, uint8_t index, bool down, uint8_t velocity)
{
    const bool isKey = index < kKeys;
    const uint8_t bit = isKey ? index : uint8_t(index - kKeys);
    uint64_t* held = isKey ? s_heldKeys : s_heldPads;
    const uint64_t mask = uint64_t(1) << bit;
    if (down)
    {
        held[source] |= mask;
    }
    else
    {
        held[source] &= ~mask;
        if (held[source ^ 1] & mask)
        {
            return;   // the other source still holds it
        }
    }
    send(index, down, velocity);
}
}   // namespace

void releaseSource(Source source)
{
    for (uint8_t i = 0; i < kKeys + kPads; i++)
    {
        const bool isKey = i < kKeys;
        const uint8_t bit = isKey ? i : uint8_t(i - kKeys);
        if ((isKey ? s_heldKeys : s_heldPads)[source] & (uint64_t(1) << bit))
        {
            apply(source, i, false, 0);
        }
    }
}

void releaseAll()
{
    for (uint8_t s = 0; s < kSources; s++)
    {
        s_heldKeys[s] = 0;
        s_heldPads[s] = 0;
    }
    for (uint8_t i = 0; i < kKeys + kPads; i++)
    {
        send(i, false, 0);   // a release for a key the remote does not hold changes nothing
    }
}

void set(bool on, uint8_t channel, uint8_t baseNote)
{
    releaseAll(); // a note off for the old channel or base note never comes, a key held by it would stay held and repeat
    s_on = on;
    s_channel = channel;
    s_base = baseNote;
}

bool note(uint8_t channel, uint8_t pitch, uint8_t velocity, bool down, Source source)
{
    if (!s_on || channel != s_channel || pitch < s_base)
    {
        return false;
    }
    const int index = pitch - s_base;
    if (index < kKeys + kPads)
    {
        apply(source, uint8_t(index), down, velocity);
    }
    return true;   // the channel is the remote's, a note outside the range is dropped, not played
}

bool cc(uint8_t channel, uint8_t number, uint8_t value)
{
    if (!s_on || channel != s_channel)
    {
        return false;
    }
    if ((number == kCcUp || number == kCcDown) && value >= 64)
    {
        mtInterface.potChange(interfacePot0, number == kCcUp ? 1 : -1);
    }
    return true;
}
}   // namespace midi_remote

#endif
