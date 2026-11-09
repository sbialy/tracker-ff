#include "PlatformImpl.h"
#include "Tracker.h"
#include "config.h"

void initHardware(PlatformImpl* platform,
                  const char* version,
                  void (*updateAudioBuffersFunc)(void* left, void* right),
                  void (*updateAudioInputFunc)(void* data),
                  void (*startDisplayFunc)(Platform*));
extern void updateHardware();

Tracker* trackerInstance;
PlatformImpl platform;

extern "C" void resetToOffState(); // PlatformImpl.cpp

// The stock emergency restart (the SD driver when SysTick stopped, REBOOT_ON_FAULT). During the power-off sequence or after it, it
// must not write the start state 1 of a power-on (resetMCU), that would start a unit that was going off: reset into off instead.
extern "C" void restartMCU()
{
    if (!platform.getPowerMode() || platform.powerOffGuardArmed())
    {
        resetToOffState();
    }
    platform.resetMCU();
}

void updateBuffers(void* bufLeft, void* bufRight)
{
    if (trackerInstance != nullptr)
    {
        trackerInstance->updateAudioBuffers(bufLeft, bufRight, kAudioBlockSamples);
    }
}

void updateInputBuffers(void* data)
{
    if (trackerInstance != nullptr)
    {
        trackerInstance->updateInputAudioBuffers(data, nullptr, kAudioBlockSamples);
    }
}

//=======================================================================
void setup()
{
    trackerInstance = Tracker::getInstance();

    initHardware(&platform,
                 Tracker::getVersion(),
                 updateBuffers,
                 updateInputBuffers,
                 trackerInstance->getStartDisplayFunc());

    platform.init();

    trackerInstance->init(&platform);
}

//=======================================================================
void loop()
{
    updateHardware();

    platform.update();

    trackerInstance->update();
}
