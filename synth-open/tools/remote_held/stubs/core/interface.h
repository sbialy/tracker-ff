// Host stand-in for core/interface/core/interface.h: what core/source/midiRemote.cpp calls, recorded by remote_held_test.cpp. NOT firmware.
#pragma once
#include "Platform.h"

enum { buttonRelease = 0, buttonPress, buttonHold };
enum { interfacePot0 = 0 };

class Interface
{
  public:
    Platform* getPlatform();
    void buttonChange(uint8_t n, uint8_t value);
    void padPressed(uint8_t n, int8_t x, int8_t y, uint8_t velo);
    void padReleased(uint8_t n);
    void potChange(uint8_t n, int16_t value);
};
extern Interface mtInterface;
