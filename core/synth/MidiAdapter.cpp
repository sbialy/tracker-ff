#include "MidiAdapter.h"
#include "config.h"
#include "ff_hooks.h"

#ifdef FEATURE_SYNTH

void MidiAdapter::subscribeMidiEvents(int /* instrumentId */,
                                      std::weak_ptr<MidiEventCallback> callback)
{
    this->callbacks.emplace_back(std::move(callback));
};

void MidiAdapter::sendRealTime(MessageType messageType)
{
    using MidiEvent = polysynth::MidiEvent;
    MidiEvent eventOut;

    switch (messageType)
    {
        case MessageType::kClock: eventOut.type = MidiEvent::Type::kClock; break;
        case MessageType::kStart: eventOut.type = MidiEvent::Type::kStart; break;
        case MessageType::kStop: eventOut.type = MidiEvent::Type::kStop; break;
        case MessageType::kContinue: eventOut.type = MidiEvent::Type::kContinue; break;
        default: assert(false); break;
    }

    for (auto& callback : callbacks)
    {
        (*callback.lock())(eventOut, 0);
    }
}

void MidiAdapter::sendControlChange(uint8_t index, uint8_t value, uint8_t channel)
{
    translateChannel(channel);
    if (index >= kTrackCCRange.first && index < kTrackCCRange.second)
    {
        trackedCCValues[channel][index - kTrackCCRange.first] = value;
    }
    else
    {
        using MidiEvent = polysynth::MidiEvent;
        MidiEvent eventOut = { .type = MidiEvent::Type::kControlChange,
                               .channel =
                                 static_cast<uint8_t>(channel % MidiEvent::kChannelsPerPort),
                               .data = {} };
        eventOut.data.controlChange.id = index;
        eventOut.data.controlChange.value = value;

        for (auto& callback : callbacks)
        {
            (*callback.lock())(eventOut, 0);
        }
    }
}

void MidiAdapter::sendProgramChange(uint8_t programNumber, uint8_t channel)
{
    translateChannel(channel);
    if (onProgramChange != nullptr)
    {
        onProgramChange(channel, programNumber);
        return;
    }
}

void MidiAdapter::sendAfterTouch(uint8_t velocity, uint8_t channel)
{
    translateChannel(channel);
    using MidiEvent = polysynth::MidiEvent;
    MidiEvent eventOut = { .type = MidiEvent::Type::kChannelAftertouch,
                           .channel = static_cast<uint8_t>(channel % MidiEvent::kChannelsPerPort),
                           .data = {} };

    eventOut.data.channelAftertouch.velocity = velocity;

    for (auto& callback : callbacks)
    {
        (*callback.lock())(eventOut, 0);
    }
}

void MidiAdapter::sendPolyPressure(uint8_t note, uint8_t value, uint8_t channel)
{
    translateChannel(channel);
    using MidiEvent = polysynth::MidiEvent;
    MidiEvent eventOut = { .type = MidiEvent::Type::kPolyAftertouch,
                           .channel = static_cast<uint8_t>(channel % MidiEvent::kChannelsPerPort),
                           .data = {} };

    eventOut.data.rawData.value1 = note;
    eventOut.data.rawData.value2 = value;
    for (auto& callback : callbacks)
    {
        (*callback.lock())(eventOut, 0);
    }
}

void MidiAdapter::sendPitchBend(int value, uint8_t channel)
{
    translateChannel(channel);
    using MidiEvent = polysynth::MidiEvent;
    MidiEvent eventOut = { .type = MidiEvent::Type::kPitchBendChange,
                           .channel = static_cast<uint8_t>(channel % MidiEvent::kChannelsPerPort),
                           .data = {} };

    eventOut.data.pitchBendChange.value = value;

    for (auto& callback : callbacks)
    {
        (*callback.lock())(eventOut, 0);
    }
}

void MidiAdapter::sendNoteOn(uint8_t note, uint8_t velocity, uint8_t channel)
{
    translateChannel(channel);
    ffHookSynthNote(channel, note, velocity > 0); // the Synth Automation slots of the part (Tracker+ and Mini)
    using MidiEvent = polysynth::MidiEvent;
    MidiEvent eventOut = { .type = MidiEvent::Type::kNoteOn,
                           .channel = static_cast<uint8_t>(channel % MidiEvent::kChannelsPerPort),
                           .data = {} };
    eventOut.data.note.note = note;
    eventOut.data.note.velocity = velocity;

    for (auto& callback : callbacks)
    {
        (*callback.lock())(eventOut, 0);
    }
}

void MidiAdapter::sendNoteOff(uint8_t note, uint8_t velocity, uint8_t channel)
{
    translateChannel(channel);
    ffHookSynthNote(channel, note, false); // the Synth Automation slots of the part (Tracker+ and Mini)
    using MidiEvent = polysynth::MidiEvent;
    MidiEvent eventOut = { .type = MidiEvent::Type::kNoteOff,
                           .channel = static_cast<uint8_t>(channel % MidiEvent::kChannelsPerPort),
                           .data = {} };
    eventOut.data.note.note = note;
    eventOut.data.note.velocity = velocity;

    for (auto& callback : callbacks)
    {
        (*callback.lock())(eventOut, 0);
    }
}

uint8_t MidiAdapter::getTrackedCCValue(int channel, int ccId)
{
    int idx = ccId - kTrackCCRange.first;
    assert(idx >= 0 && idx < kTrackCCRange.second - kTrackCCRange.first);

    return trackedCCValues[channel][idx];
}

#endif
