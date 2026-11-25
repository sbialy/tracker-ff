// tracker-ff USB audio (USB_AUDIO=1, -DFEATURE_USB_AUDIO): the platform side of the Tracker -> computer stream.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Audio block (I2S interrupt): the block just rendered, 128 samples per channel.
void ffUsbAudioBlock(const int16_t* left, const int16_t* right);
// 8+3 mono (audio block, from the tap at the end of the graph): FF_USB_AUDIO_MULTI_INPUTS block pointers, tracks 1-8, synth part
// 1-3 left, synth part 1-3 right, null = silence. Returns at once unless this session streams 8+3 mono.
#define FF_USB_AUDIO_MULTI_INPUTS 14
void ffUsbAudioMultiBlock(const int16_t* const* in);

// Boot-time switch (Config > Features > USB audio): the EEPROM flag usb_init reads, its writer, the state this session
// started with, and the clear of the SDRAM ring once the SDRAM is up (initHardware).
// The boot flag reads 0 off, 1 stereo, 2 8+3 mono. The mode (EEPROM byte 7) has its own writer and the session's mode reads
// 0 stereo, 1 8+3 mono.
int ffUsbAudioBootFlag(void);
void ffUsbAudioSetBootFlag(int on);
void ffUsbAudioSetBootMode(int multi);
int ffUsbAudioMode(void);
int ffUsbAudioActive(void);
// The computer records the stream now (IN packets within the last kOutIdleBlocks blocks): 0 no, 1 stereo, 2 8+3 mono.
int ffUsbAudioStreaming(void);
void ffUsbAudioStart(void);

// Development console: packets, under-runs, overflows, ring fill, packet step (10.14 frames per ms),
// flags (1 playing, 2 streaming, 4 the stream is 8+3 mono), alternate setting, blocks written, then the DWT cycle stamps summed
// over the session: [8] block side (interleaveMulti or the stereo copy) [9] longest block side [10] USB packet callback
// [11] the part of [10] that ran inside a block side (steal). Blocks [7] and packets [0] are the counts.
#define FF_USB_AUDIO_STATS 12
void ffUsbAudioGetStats(uint32_t* stats);

#ifdef FEATURE_USB_AUDIO_IN
// FEATURE_USB_AUDIO_IN (computer -> Tracker): the recorder's USB source. ffUsbAudioInWant(1) while the recorder listens to it (the
// OUT packets then fill the ring, 0 otherwise: the USB interrupt drops them at once). ffUsbAudioInBlock(): audio block (I2S
// interrupt), the 128 interleaved stereo frames for the audio graph (silence until the ring is primed), nullptr while not wanted,
// the USB audio function is off, or before the SDRAM is up.
void ffUsbAudioInWant(int on);
const int16_t* ffUsbAudioInBlock(void);
// ring fill, packets, overflows, underruns, repeated frames, skipped frames (host test and console)
void ffUsbAudioInStats(uint32_t* s6);
#endif

#ifdef __cplusplus
}
#endif
