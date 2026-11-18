#ifndef SOURCE_USBMIDIIMPL_H_
#define SOURCE_USBMIDIIMPL_H_
#include "Platform.h"

class UsbMidiImpl : public UsbMidi
{
  public:
    virtual bool isReadAvailable() override;
    virtual void read() override;
    virtual void sendRealTime(MessageType messageType) override;
    virtual void sendControlChange(uint8_t index, uint8_t value, uint8_t channel) override;
    virtual void sendProgramChange(uint8_t programNumber, uint8_t channel) override;
    virtual void sendAfterTouch(uint8_t velocity, uint8_t channel) override;
    virtual void sendPolyPressure(uint8_t note, uint8_t value, uint8_t channel) override;
    virtual void sendPitchBend(int value, uint8_t channel) override;
    virtual void sendNoteOn(uint8_t note, uint8_t velocity, uint8_t channel) override;
    virtual void sendNoteOff(uint8_t note, uint8_t velocity, uint8_t channel) override;
    virtual void setReceiveActions(MidiReceiveActions* receiveActions) override;
    virtual bool takeBusLost() override;
};

#endif /* SOURCE_USBMIDIIMPL_H_ */
