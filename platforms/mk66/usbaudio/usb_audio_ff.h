// tracker-ff USB audio (USB_AUDIO=1, -DFEATURE_USB_AUDIO): the platform side of the Tracker -> computer stream.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Audio block (I2S interrupt): the block just rendered, 128 samples per channel.
void ffUsbAudioBlock(const int16_t* left, const int16_t* right);

// Boot-time switch (Config > Features > USB audio): the EEPROM flag usb_init reads, its writer, the state this session
// started with, and the clear of the SDRAM ring once the SDRAM is up (initHardware).
int ffUsbAudioBootFlag(void);
void ffUsbAudioSetBootFlag(int on);
int ffUsbAudioActive(void);
void ffUsbAudioStart(void);

// Development console: packets, under-runs, overflows, ring fill, packet step (10.14 frames per ms),
// flags (1 playing, 2 streaming), alternate setting, blocks written.
#define FF_USB_AUDIO_STATS 8
void ffUsbAudioGetStats(uint32_t* stats);

#ifdef __cplusplus
}
#endif
