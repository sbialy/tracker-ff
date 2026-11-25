// host stand-in, see kinetis.h: the pin functions of the Teensy core that serial5.c calls outside the code under test
#pragma once
#include <stdint.h>
#define OUTPUT 1
#define INPUT 0
#define LOW 0
#define HIGH 1
#define CORE_NUM_DIGITAL 40
static inline void pinMode(uint8_t p, uint8_t m) { (void)p; (void)m; }
static inline void digitalWrite(uint8_t p, uint8_t v) { (void)p; (void)v; }
static inline volatile uint8_t* portOutputRegister(uint8_t p) { static volatile uint8_t r; (void)p; return &r; }
