// Host shim of the Teensy wiring.h for fs_host_test.cpp: the card detect pin reads 0 (card in) unless the test pulls the card.
#pragma once
#include <stdint.h>
#define INPUT_PULLUP 2
extern int g_fakeCardOut;
// pinMode and digitalRead are defined in fake_sd.cpp with C linkage: FileSystemImpl.cpp declares them itself since 38dd035e
