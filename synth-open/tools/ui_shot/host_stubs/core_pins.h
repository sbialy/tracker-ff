// Host stand-in for the Teensy core_pins.h, just enough for platforms/mk66/3rdparty/Buttons/Keys.cpp (compiled on the Mac so the
// USB test console drives the real key queue and hold repeat). NOT firmware.
#pragma once
#include <stdint.h>

#define INPUT_PULLUP 2
#define FALLING 2

uint32_t millis();   // host_usbtest.cpp: the virtual clock
void pinMode(uint8_t pin, uint8_t mode);
void attachInterrupt(uint8_t pin, void (*function)(void), int mode);
void detachInterrupt(uint8_t pin);
