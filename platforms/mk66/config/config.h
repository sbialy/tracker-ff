#ifndef CONFIG_H_
#define CONFIG_H_
#include "stdint.h"
// turn-on mode depending on build and device
#if defined(DEBUG) || defined(TRACKER_MINI)
static constexpr bool START_STATE_SAVE = false;
#else // RELEASE
static constexpr bool START_STATE_SAVE = true;
#endif

const uint32_t START_STATE_EEPROM_ADRESS = 0; // 1st byte = start_state
const uint32_t CONFIG_EEPROM_ADRESS = 10;     // 9 bytes backup, then config
// Features (ref/features-design.md), two of the 9 spare bytes, read only with a valid config id: the Power return option of the
// restart mode (checkPowerState keeps the start state at 1, POWER_RETURN_ON) and the USB audio boot flag (usbaudio/, 0x5A)
const uint32_t POWER_RETURN_EEPROM_ADRESS = 8;
static constexpr uint8_t POWER_RETURN_ON = 0x5A;

static constexpr int kAudioBlockSamples = 128; // sync w config.inc

#endif /* CONFIG_CONFIG_H_ */
