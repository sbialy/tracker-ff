// host_imxmidi.cpp: the thin host stub BELOW the real Tracker+ / Mini MIDI driver (PLATFORM=IMX sims only, empty on the original Tracker).
// Real (compiled from platforms/imx, not replaced): usb_midi.c (the USB MIDI packet parser, the receive queue, midiUsbSend / midiUsbTx and the
// send buffer ring), UsbMidiImpl.cpp (message dispatch, the 14 bit pitch bend, channel pressure, program change, the 1 based channel),
// UartMidiImpl.cpp and the Arduino MIDI library (running status, channel messages, its handlers). Stubbed here: the two USB endpoint transfers
// (the host side of the bus: USB_DeviceMidiRecv hands out the receive buffer, USB_DeviceMidiSend is the packet sink), the interrupt mask and
// the LPUART byte ring (MidiUart: init / read / available / write, the hardware ring of MidiUart.cpp).
#include "host_imxmidi.h"
#ifdef UISHOT_PLUS
#include "imx_midi_stub.h"
#include "usb_midi.h"
#include "MidiUart.h"
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

extern double g_audioUs;
extern bool g_midiLog;
void midiUpdate();

LPUART_Type g_hostLpuart5;
extern "C" usb_device_endpoint_struct_t g_MidiEndpoints[2] = {{512}, {512}};

namespace
{
usb_device_midi_struct_t g_dev;
usb_device_composite_struct_t g_comp;
uint8_t* g_rxBuf = nullptr;
uint32_t g_rxMax = 0;
std::vector<uint8_t> g_inflight;
std::deque<uint8_t> g_uartRx;

// the device is configured at boot on the unit (USB_DeviceMidiImplInit and SetConfigure): before the first note, as a static constructor
struct Boot
{
    Boot()
    {
        g_comp.midi.midiHandle = &g_dev;
        USB_DeviceMidiImplInit(&g_comp);
        USB_DeviceMidiSetConfigure(&g_dev, 1);
    }
} g_boot;

void logPacket(const uint8_t* p)
{
    if (!g_midiLog) return;
    const unsigned cin = p[0] & 15, ch = (p[1] & 15) + 1, d1 = p[2], d2 = p[3];
    const double t = g_audioUs / 1000.0;
    switch (cin)
    {
        case 0x8: printf("midi off %u ch %u t %.1f\n", d1, ch, t); break;
        case 0x9: printf("midi on %u %u ch %u t %.1f\n", d1, d2, ch, t); break;
        case 0xA: printf("midi pp %u %u ch %u t %.1f\n", d1, d2, ch, t); break;
        case 0xB: printf("midi cc %u %u ch %u t %.1f\n", d1, d2, ch, t); break;
        case 0xC: printf("midi pc %u ch %u t %.1f\n", d1, ch, t); break;
        case 0xD: printf("midi at %u ch %u t %.1f\n", d1, ch, t); break;
        case 0xE: printf("midi pb %d ch %u t %.1f\n", int(d1 | (d2 << 7)) - 8192, ch, t); break;
        default: break;   // real time and system messages: the OG stand-in logs none either
    }
}
}   // namespace

extern "C"
{
    usb_status_t USB_DeviceMidiRecv(class_handle_t, uint8_t, uint8_t* buffer, uint32_t length)
    {
        g_rxBuf = buffer;
        g_rxMax = length;
        return kStatus_USB_Success;
    }
    usb_status_t USB_DeviceMidiSend(class_handle_t, uint8_t, uint8_t* buffer, uint32_t length)
    {
        g_inflight.assign(buffer, buffer + length);
        g_dev.bulkIn.isBusy = true;
        return kStatus_USB_Success;
    }
    uint32_t DisableGlobalIRQ(void) { return 0; }
    void EnableGlobalIRQ(uint32_t) {}
}

void uishotImxMidiPump()
{
    // the host reads the IN endpoint: each completed transfer calls the real callback, which starts the next one while buffers are queued
    for (int guard = 0; g_dev.bulkIn.isBusy && guard < 1000; guard++)
    {
        const std::vector<uint8_t> sent = g_inflight;
        g_dev.bulkIn.isBusy = false;
        for (size_t i = 0; i + 4 <= sent.size(); i += 4) logPacket(&sent[i]);
        usb_device_endpoint_callback_message_struct_t msg = {uint32_t(sent.size()), nullptr, 0};
        USB_DeviceMidiCallback(&g_dev, kUSB_DeviceMidiEventSendResponse, &msg);
    }
}

void uishotImxUsbIn(const uint8_t* m, int n)
{
    if (n < 1 || !g_rxBuf) return;
    const uint8_t s = m[0], d1 = n > 1 ? m[1] : 0, d2 = n > 2 ? m[2] : 0;
    uint8_t cin;
    if (s >= 0xF8) cin = 0x0F;
    else if (s == 0xF2) cin = 0x03;
    else if (s >= 0xF0) cin = 0x02;
    else cin = s >> 4;
    const uint8_t pkt[4] = {cin, s, d1, d2};   // cable 0
    memcpy(g_rxBuf, pkt, 4);
    usb_device_endpoint_callback_message_struct_t msg = {4, g_rxBuf, 0};
    USB_DeviceMidiCallback(&g_dev, kUSB_DeviceMidiEventRecvResponse, &msg);
    midiUpdate();
}

void uishotImxDinIn(const uint8_t* m, int n)
{
    for (int i = 0; i < n; i++) g_uartRx.push_back(m[i]);
    midiUpdate();
}

// the LPUART ring of MidiUart.cpp: receive bytes come from uishotImxDinIn, transmit bytes are dropped (a DIN out sink is not modelled)
void MidiUart::init(LPUART_Type* base) { uartBase = base; }
void MidiUart::update() {}
void MidiUart::write(uint8_t) {}
uint8_t MidiUart::read()
{
    if (g_uartRx.empty()) return 0;
    const uint8_t b = g_uartRx.front();
    g_uartRx.pop_front();
    return b;
}
size_t MidiUart::available() { return g_uartRx.size(); }
#endif
