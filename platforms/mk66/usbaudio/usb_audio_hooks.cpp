// Ported from OpenTracker (https://github.com/syamajala/polyend-tracker-re, commit ef7a4bbce247,
// src/hal/usb_audio.cpp), Apache-2.0, see licenses/LICENSE-OpenTracker-Apache-2.0.txt.
// Changed from the original (Apache-2.0 section 4b): Tracker -> computer only (no OUT endpoint, no feedback
// endpoint), the ring in SDRAM and the packet buffers in SRAM, the block hook takes the two channel buffers of the
// I2S interrupt, the function switched at start from an EEPROM boot flag (Config > Features > USB audio), statistics
// for the development console. Compiled only with USB_AUDIO=1 (-DFEATURE_USB_AUDIO).
//
// The hooks usb_dev.c calls from the USB interrupt (FF_USB_AUDIO_TX in usb_desc.h). The transmit buffer
// holds one packet per BDT (even, odd), both kept armed and refilled in place.
#include "UsbAudioOut.h"

#include <cstring>

#include "avr_functions.h"   // eeprom_read_byte / eeprom_write_byte, usable before main (eeprom.c initialises itself)
#include "cr_section_macros.h"
#include "memcpy_audio.h"
#include "usb_desc.h"
#include "usb_dev.h"
#include "usb_audio_ff.h"

#ifndef FF_USB_AUDIO_TX
#error "FEATURE_USB_AUDIO needs the FF_USB_AUDIO_TX descriptor (usb_desc.h, USB_MIDI)"
#endif

namespace
{
// SDRAM, no constructor (the class is trivial, check_static_init.py): cleared by ffUsbAudioStart once the SDRAM is up.
// The USB interrupt and the audio block both touch it, neither does before s_ready.
__NOINIT(EXTERNAL_RAM) ff::usbaudio::UsbAudioOut g_out;
volatile bool s_ready;

// EEPROM: byte 9 is the last of the 9 spare bytes between the start state (byte 0) and the config (byte 10,
// platforms/mk66/config/config.h). Read only when the config id is valid, so a blank or foreign EEPROM is off.
constexpr uint32_t kFlagAddr = 9;
constexpr uint8_t kFlagOn = 0x5A;
constexpr uint32_t kConfigIdAddr = 10;
} // namespace

extern "C" {
uint16_t usb_audio_transmit_buffer[2 * AUDIO_TX_SIZE / 2] __attribute__((aligned(4)));
uint8_t usb_audio_transmit_setting;

// USB interrupt: the IN packet after next into buf, 43..45 frames at the codec's pace.
unsigned int usb_audio_transmit_callback(uint16_t* buf)
{
    static_assert(AUDIO_TX_SIZE / 4 == ff::usbaudio::kOutMaxPacket, "IN packet size");
    if (!s_ready)
    {
        memset(buf, 0, 176); // the SDRAM is not up yet: one nominal packet of silence
        return 176;
    }
    return g_out.packet(reinterpret_cast<int16_t*>(buf)) * 4;
}

int ffUsbAudioBootFlag(void)
{
    static const char kId[] = "EEPROM"; // mtConfig.header.id with its terminator, as PlatformImpl::configValid
    for (uint32_t i = 0; i < sizeof kId; i++)
    {
        if (eeprom_read_byte(reinterpret_cast<const uint8_t*>(kConfigIdAddr + i)) != uint8_t(kId[i])) return 0;
    }
    return eeprom_read_byte(reinterpret_cast<const uint8_t*>(kFlagAddr)) == kFlagOn;
}

void ffUsbAudioSetBootFlag(int on)
{
    const uint8_t v = on ? kFlagOn : 0;
    if (eeprom_read_byte(reinterpret_cast<const uint8_t*>(kFlagAddr)) != v)
    {
        eeprom_write_byte(reinterpret_cast<uint8_t*>(kFlagAddr), v); // one write per change
    }
}

int ffUsbAudioActive(void)
{
    return ff_usb_audio_on;
}

void ffUsbAudioStart(void)
{
    if (!ff_usb_audio_on) return;
    memset(&g_out, 0, sizeof g_out);
    s_ready = true;
}

void ffUsbAudioBlock(const int16_t* left, const int16_t* right)
{
    if (!s_ready) return; // off (or before the start): one load and a branch in the audio block
    int16_t* dst = g_out.buffer();
    if (dst != nullptr)
    {
        memcpy_tointerleaveLR(dst, left, right);
        g_out.commit();
    }
}

void ffUsbAudioGetStats(uint32_t* s)
{
    s[0] = g_out.packets();
    s[1] = g_out.underruns();
    s[2] = g_out.overflows();
    s[3] = g_out.fill();
    s[4] = g_out.step();
    s[5] = (g_out.playing() ? 1u : 0u) | (g_out.streaming() ? 2u : 0u);
    s[6] = usb_audio_transmit_setting;
    s[7] = g_out.blocks();
}
}
