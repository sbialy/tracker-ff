// Ported from OpenTracker (https://github.com/syamajala/polyend-tracker-re, commit ef7a4bbce247,
// src/hal/usb_audio.cpp), Apache-2.0, see licenses/LICENSE-OpenTracker-Apache-2.0.txt.
// Changed from the original (Apache-2.0 section 4b): Tracker -> computer only (no OUT endpoint, no feedback
// endpoint), the ring in SDRAM and the packet buffers in SRAM, the block hook takes the two channel buffers of the
// I2S interrupt, the function switched at start from an EEPROM boot flag (Config > Features > USB audio), statistics
// for the development console, the 8+3 mono mode (11 channels: tracks 1-8 and synth parts 1-3, written by the tap at the end of the
// audio graph, core/AudioEngine/UsbMultiTap). Compiled only with USB_AUDIO=1 (-DFEATURE_USB_AUDIO).
//
// The hooks usb_dev.c calls from the USB interrupt (FF_USB_AUDIO_TX in usb_desc.h). The transmit buffer
// holds one packet per BDT (even, odd), both kept armed and refilled in place.
#include "UsbAudioOut.h"
#ifdef FEATURE_USB_AUDIO_IN
#include "UsbAudioIn.h"
#endif

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
#if defined(FEATURE_USB_AUDIO_IN) && !defined(FF_USB_AUDIO_RX)
#error "FEATURE_USB_AUDIO_IN needs the FF_USB_AUDIO_RX descriptor (usb_desc.h)"
#endif

namespace
{
// SDRAM, no constructor (the class is trivial, check_static_init.py): cleared by ffUsbAudioStart once the SDRAM is up.
// The USB interrupt and the audio block both touch it, neither does before s_ready.
__NOINIT(EXTERNAL_RAM) ff::usbaudio::UsbAudioOut g_out;
// 8+3: the synth parts' 2 block delay, 1,540 B. In SRAM (zero initialised .bss, trivial class, no constructor): the block side
// reads and writes 384 words of it per block, in SDRAM that was a third of the block side's SDRAM accesses.
ff::usbaudio::MultiDelay g_delay;
bool s_delayLive; // g_delay holds the blocks just before this one
volatile bool s_ready;
#ifdef FEATURE_USB_AUDIO_IN
// computer -> Tracker: the ring and the block the audio graph reads, in SDRAM (cleared by ffUsbAudioStart), filled only while the
// recorder listens (s_inWanted)
struct InSide
{
    ff::usbaudio::UsbAudioIn ring;
    alignas(4) int16_t block[ff::usbaudio::kInBlockFrames * 2];
};
__NOINIT(EXTERNAL_RAM) InSide g_in;
volatile bool s_inWanted;
#endif
bool s_multi; // this session streams 8+3 mono (set with s_ready)

// DWT cycle stamps for the development console (`uac`): the block side (interleaveMulti, or the stereo copy) and the USB packet
// callback. One writer each (the audio block, the USB interrupt), summed over the session, the reader takes two readings and
// divides by the blocks / packets counters. The block stamp is wall clock, so a USB interrupt landing inside it is counted in it:
// s_cbIn adds the callback cycles that ran while s_inBlock was set, so the block side net of the steal is s_blkCyc - s_cbIn.
// The interrupt entry and the token handling outside the callback are not in s_cbIn (a few hundred cycles per packet at most).
#define FF_CYCCNT (*reinterpret_cast<volatile uint32_t*>(0xE0001004)) // ARM_DWT_CYCCNT, switched on by ClockCyclesCounter::init
uint32_t s_blkCyc;                                                      // sum of the block side cycles
uint32_t s_blkMax;                                                      // longest block side
uint32_t s_cbCyc;                                                       // sum of the USB packet callback cycles
uint32_t s_cbIn;                                                        // the part of s_cbCyc that ran inside a block side
volatile bool s_inBlock;

// EEPROM: byte 9 is the last of the 9 spare bytes between the start state (byte 0) and the config (byte 10,
// platforms/mk66/config/config.h). Read only when the config id is valid, so a blank or foreign EEPROM is off.
constexpr uint32_t kFlagAddr = 9;
constexpr uint8_t kFlagOn = 0x5A;
constexpr uint32_t kConfigIdAddr = 10;
// The mode (Features > USB audio > Mode) in byte 7, another spare byte: only kModeMulti selects 8+3 mono, every other value
// (0xFF of a blank EEPROM, what older images leave) is stereo. Older images never read it, so they start in stereo.
constexpr uint32_t kModeAddr = 7;
constexpr uint8_t kModeMulti = 0xA5;
constexpr uint8_t kModeStereo = 0xFF; // written only to leave 8+3 mono
} // namespace

extern "C" {
// two BDT buffers (even, odd) of FF_AUDIO_TX_STRIDE bytes: room for the 990 B of an 8+3 packet, 1,984 B of SRAM
uint16_t usb_audio_transmit_buffer[2 * FF_AUDIO_TX_STRIDE / 2] __attribute__((aligned(4)));
uint8_t usb_audio_transmit_setting;

// USB interrupt: the IN packet after next into buf, 43..45 frames at the codec's pace.
unsigned int usb_audio_transmit_callback(uint16_t* buf)
{
    static_assert(AUDIO_TX_SIZE / 4 == ff::usbaudio::kOutMaxPacket, "IN packet size");
    static_assert(FF_AUDIO_MULTI_SIZE == ff::usbaudio::kOutMaxPacket * ff::usbaudio::kOutMultiChannels * 2, "8+3 packet size");
    static_assert(FF_AUDIO_MULTI_CH == ff::usbaudio::kOutMultiChannels && FF_AUDIO_TX_STRIDE >= FF_AUDIO_MULTI_SIZE, "8+3 buffers");
    const unsigned fb = 2u * ff_usb_audio_channels; // bytes per frame
    if (!s_ready)
    {
        memset(buf, 0, 44 * fb); // the SDRAM is not up yet: one nominal packet of silence
        return 44 * fb;
    }
    const uint32_t t0 = FF_CYCCNT;
    const unsigned len = g_out.packet(reinterpret_cast<int16_t*>(buf)) * fb;
    const uint32_t dt = FF_CYCCNT - t0;
    s_cbCyc += dt;
    if (s_inBlock) s_cbIn += dt;
    return len;
}

#ifdef FEATURE_USB_AUDIO_IN
// two BDT buffers (even, odd) of FF_AUDIO_RX_SIZE bytes: 360 B of SRAM
uint16_t usb_audio_receive_buffer[2 * FF_AUDIO_RX_SIZE / 2] __attribute__((aligned(4)));
uint8_t usb_audio_receive_setting;
// the explicit feedback of the OUT endpoint (endpoint 11, 3 bytes 10.14 little endian): internal RAM, the USB controller reads it by
// DMA, the audio block writes it (UsbAudioIn::feedbackStep). Nominal until the recorder listens.
volatile uint32_t usb_audio_feedback __attribute__((aligned(4))) = ff::usbaudio::kFbNominal;

// USB interrupt: one packet of stereo frames from the computer (4 B per frame)
void usb_audio_receive_callback(const int16_t* buf, unsigned int bytes)
{
    if (s_ready && s_inWanted) g_in.ring.push(buf, bytes / 4);
}

void ffUsbAudioInWant(int on)
{
    if (!s_ready) return;
    if (on && !s_inWanted) g_in.ring.restart();
    s_inWanted = on != 0;
    if (!on) usb_audio_feedback = ff::usbaudio::kFbNominal; // nobody reads the ring: the host keeps the nominal pace
}

const int16_t* ffUsbAudioInBlock(void)
{
    if (!s_ready || !s_inWanted) return nullptr;
    return g_in.ring.pullBlock(g_in.block);
}

void ffUsbAudioInStats(uint32_t* s)
{
    s[0] = g_in.ring.fill();
    s[1] = g_in.ring.packets();
    s[2] = g_in.ring.overflows();
    s[3] = g_in.ring.underruns();
    s[4] = g_in.ring.repeats();
    s[5] = g_in.ring.skips();
}
#endif

int ffUsbAudioBootFlag(void)
{
    static const char kId[] = "EEPROM"; // mtConfig.header.id with its terminator, as PlatformImpl::configValid
    for (uint32_t i = 0; i < sizeof kId; i++)
    {
        if (eeprom_read_byte(reinterpret_cast<const uint8_t*>(kConfigIdAddr + i)) != uint8_t(kId[i])) return 0;
    }
    if (eeprom_read_byte(reinterpret_cast<const uint8_t*>(kFlagAddr)) != kFlagOn) return 0;
    return eeprom_read_byte(reinterpret_cast<const uint8_t*>(kModeAddr)) == kModeMulti ? 2 : 1;
}

void ffUsbAudioSetBootMode(int multi)
{
    const uint8_t v = multi ? kModeMulti : kModeStereo;
    const uint8_t now = eeprom_read_byte(reinterpret_cast<const uint8_t*>(kModeAddr));
    // stereo is every value but kModeMulti: a stereo unit never writes the byte (a blank or older EEPROM stays as it is)
    if (multi ? now != kModeMulti : now == kModeMulti)
    {
        eeprom_write_byte(reinterpret_cast<uint8_t*>(kModeAddr), v);
    }
}

int ffUsbAudioMode(void)
{
    return ff_usb_audio_channels == FF_AUDIO_MULTI_CH ? 1 : 0;
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

int ffUsbAudioStreaming(void)
{
    if (!s_ready || !g_out.streaming()) return 0;
    return s_multi ? 2 : 1;
}

void ffUsbAudioStart(void)
{
    if (!ff_usb_audio_on) return;
    memset(&g_out, 0, sizeof g_out);
#ifdef FEATURE_USB_AUDIO_IN
    memset(&g_in, 0, sizeof g_in);
    g_in.ring.setFeedbackOut(&usb_audio_feedback);
#endif
    g_out.setChannels(ff_usb_audio_channels);
    s_multi = ff_usb_audio_channels == FF_AUDIO_MULTI_CH;
    s_ready = true;
}

void ffUsbAudioBlock(const int16_t* left, const int16_t* right)
{
    if (!s_ready || s_multi) return; // off (or before the start), or 8+3 mono (the tap writes): a load or two and a branch
    int16_t* dst = g_out.buffer();
    if (dst != nullptr)
    {
        const uint32_t t0 = FF_CYCCNT;
        s_inBlock = true;
        memcpy_tointerleaveLR(dst, left, right);
        g_out.commit();
        s_inBlock = false;
        const uint32_t dt = FF_CYCCNT - t0;
        s_blkCyc += dt;
        if (dt > s_blkMax) s_blkMax = dt;
    }
}

void ffUsbAudioMultiBlock(const int16_t* const* in)
{
    if (!s_ready || !s_multi) return;
    int16_t* dst = g_out.buffer();
    if (dst == nullptr)
    {
        s_delayLive = false; // not recording (or the ring is full): nothing written, the delay restarts from silence
        return;
    }
    if (!s_delayLive)
    {
        memset(&g_delay, 0, sizeof g_delay);
        s_delayLive = true;
    }
    const uint32_t t0 = FF_CYCCNT;
    s_inBlock = true;
    ff::usbaudio::interleaveMulti(dst, in, g_delay);
    s_inBlock = false;
    const uint32_t dt = FF_CYCCNT - t0;
    s_blkCyc += dt;
    if (dt > s_blkMax) s_blkMax = dt;
    g_out.commit();
}

void ffUsbAudioGetStats(uint32_t* s)
{
    if (!s_ready)
    {
        memset(s, 0, FF_USB_AUDIO_STATS * sizeof(uint32_t)); // off: the SDRAM ring was never cleared, nothing to report
        return;
    }
    s[0] = g_out.packets();
    s[1] = g_out.underruns();
    s[2] = g_out.overflows();
    s[3] = g_out.fill();
    s[4] = g_out.step();
    s[5] = (g_out.playing() ? 1u : 0u) | (g_out.streaming() ? 2u : 0u) | (s_multi ? 4u : 0u);
    s[6] = usb_audio_transmit_setting;
    s[7] = g_out.blocks();
    s[8] = s_blkCyc;
    s[9] = s_blkMax;
    s[10] = s_cbCyc;
    s[11] = s_cbIn;
}
}
