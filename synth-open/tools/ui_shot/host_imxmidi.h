// host_imxmidi.h: the byte level of the Tracker+ / Mini MIDI in the plus and mini sims (PLATFORM=IMX). The real driver files run above it:
// usb_midi.c (USB MIDI event packets <-> the message queue), UsbMidiImpl.cpp and UartMidiImpl.cpp with the Arduino MIDI library. See host_imxmidi.cpp.
#ifndef HOST_IMXMIDI_H_
#define HOST_IMXMIDI_H_
#include <cstdint>
#ifdef UISHOT_PLUS
// one raw MIDI message (status byte and its data bytes, 1..3 bytes) as the USB host would send it (a 4 byte USB MIDI event packet into the
// endpoint buffer, then the real receive callback), then the audio interrupt's midiUpdate
void uishotImxUsbIn(const uint8_t* msg, int n);
// the same message as DIN bytes in the UART receive ring, then midiUpdate
void uishotImxDinIn(const uint8_t* msg, int n);
// completes the USB IN transfers the driver started (the host reads the packets): "midi ..." lines for midilog, the way the OG sim logs them
void uishotImxMidiPump();
#endif
#endif
