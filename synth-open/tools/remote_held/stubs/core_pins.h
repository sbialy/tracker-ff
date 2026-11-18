// Host stand-in for the Teensy core_pins.h, just enough for platforms/mk66/3rdparty/Buttons/Keys.cpp. NOT firmware.
#pragma once
#include <stdint.h>

#define INPUT_PULLUP 2
#define FALLING 2

uint32_t millis();   // remote_held_test.cpp: the test clock
void pinMode(uint8_t pin, uint8_t mode);
void attachInterrupt(uint8_t pin, void (*function)(void), int mode);
void detachInterrupt(uint8_t pin);
