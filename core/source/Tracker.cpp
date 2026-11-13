#include "Platform.h"
#include "Profiler.h"
#include "TrackerImpl.h"
#include "devhooks.h"
#ifdef FEATURE_EXT_HOOKS
#include "features.h"
#endif
#include "mtWavetableEnvelopes.h"
#ifdef FEATURE_PLUG_HOST
#include "plug_host.h"
#endif
#include "core/interface.h"
#include "elapsedMillis.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtAudioExport.h"
#include "mtConfig.h"
#include "mtHardware.h"
#include "mtMidi.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "mtSleep.h"
#include "mtSliceDetector.h"

#include <cr_section_macros.h>

//-----------------------------------  EXTERNAL RAM CONFIGURATION --------------------------------
// clang-format off
__attribute__((section(".sample_memory"), used)) int16_t externalRamSampleMemory[SAMPLE_MEMORY_SIZE / 2];
__attribute__((section(".delay_memory"), used)) int16_t externalRamDelayBuff[DELAY_MEMORY_SIZE / 2];
__attribute__((section(".scratch_memory"), aligned(32), used)) uint8_t externalRamScratchBuff[SHARED_SCRATCH_MEMORY_SIZE];

// clang-format on
int16_t* externalRamEffectsBuff = externalRamSampleMemory + (EDIT_BUFF_OFFSET / 2);
int16_t* externalRamSampleMemoryBegin = externalRamSampleMemory;

//--------------------------------------------------------------------------------------------------------------
static TrackerImpl instance;

extern void onPowerButtonChange(int value);
extern void onSDCardSlotChange(bool state);

void startDisplay(Platform* platform)
{
    [[maybe_unused]] bool powerButtonState = platform->getPowerButtonState();

    display.begin(platform->getDisplayDriver(), platform->getTimer());
#ifdef PLATFORM_IMX
    // delay to hide white screen before driver initialize the screen
    platform->getTimer()->delay(200);
#endif

#ifdef TRACKER_MINI
    // only in release build mode (always start device immediately in debug mode)
#if !defined(DEBUG) && !defined(FORCED_AUTOSTART)
    if (platform->readForceStartUp() == false &&
        (!powerButtonState || platform->getBattery()->isEmpty()))
    {
        Battery* battery = platform->getBattery();
        display.showBatteryScreen(
          battery->isEmpty(), battery->getCharging(), battery->getLevel(), battery->getVoltage());
        platform->getTimer()->delay(100);
        display.turnOn();
        display.showBatteryScreen(
          battery->isEmpty(), battery->getCharging(), battery->getLevel(), battery->getVoltage());
        platform->getTimer()->delay(2000);
        platform->setPowerMode(false);
    }
#endif
#endif

    display.turnOn();
}

Tracker* Tracker::getInstance()
{
    return &instance;
}

TrackerImpl* TrackerImpl::getInstance()
{
    return &instance;
}

Platform* TrackerImpl::getPlatform()
{
    return platform;
}

#ifdef FEATURE_SYNTH
Synth& TrackerImpl::getSynth()
{
    return synth;
}
#endif

const char* Tracker::getVersion()
{
    return "1.0";
}

void TrackerImpl::init(Platform* platform)
{
    this->platform = platform;

    initWavetableEnvelopes();

    Timer* timer = getPlatform()->getTimer();
    elapsedMicros::init(timer);
    elapsedMillis::init(timer);
    elapsedMicrosAudio::init(timer);
    elapsedMillisAudio::init(timer);
#ifdef PLATFORM_IMX
    AudioStream::setUsbAudio(platform->getUsbAudio());
#endif

    midiInit(platform->getUartMidi(), platform->getUsbMidi());

    platform->setPowerButtonCallback(onPowerButtonChange);
    platform->getFileSystem()->setMountEventCallback(onSDCardSlotChange);

#ifdef FEATURE_PLUG_HOST
    // the synth engine plugins: installed into the FlexNVM bank and added to the engine list before the synth starts
    plug::synthBoot(platform->getFileSystem());
#endif
#ifdef FEATURE_SYNTH
    synth.init(platform, [this]() { return sequencer.getCurrentTempo(); });

    engine.init(platform, &synth);
#else
    engine.init(platform, nullptr);
#endif
    sequencer.init();
    sequencer.setCrcCalculator(platform->getCrcCalculator());
    padsBacklight.init(500, platform->getLeds());
    exporter.init(platform->getFileSystem());
    Profiler::getInstance()->init();
    newFileManager.setCrcCalculator(platform->getCrcCalculator());

    initConfig(platform);
    // UI init at the end
#ifdef FEATURE_SYNTH
    mtInterface.begin(platform, &synth);
#else
    mtInterface.begin(platform, nullptr);
#endif

    bool stereoSampleMode = false;
#ifdef PLATFORM_IMX
    stereoSampleMode = true;
#endif

#ifdef FEATURE_SYNTH
    newFileManager.init(platform->getFileSystem(), &synth, stereoSampleMode);
#else
    newFileManager.init(platform->getFileSystem(), nullptr, stereoSampleMode);
#endif

#ifdef FEATURE_EXT_HOOKS
    plug::useFs(platform->getFileSystem()); // features read and write their files through plug::readText / writeText
    features::boot(platform);               // Config > Features: restart mode, developer mode, USB audio, module features
    const int develop = features::find("develop");
    // /.develop stays an override: a card can always reach develop mode without the menu
    interfaceGlobals.devMode = platform->getFileSystem()->exists("/.develop") || features::on(develop);
    const bool extLoad = interfaceGlobals.devMode && (!features::on(develop) || features::value(develop, 0) != 0);
#else
    interfaceGlobals.devMode = platform->getFileSystem()->exists("/.develop");
#endif
    lowPower.initRestartMode();
#ifdef FEATURE_EXT_HOOKS
    {
        // a reset sent the unit into the off state since the last start that ran (a power off that did not end in the sleep, or a
        // reset while off): what reset it, for the device rows of the power off test. Develop mode only, one small file.
        uint32_t srs = 0, marker = 0, count = 0;
        if (platform->takeOffStartRecord(srs, marker, count) && interfaceGlobals.devMode)
        {
            char t[96];
            const int n = snprintf(t, sizeof t, "off starts %lu, last: srs %04lx marker %s\n", (unsigned long)count,
                                   (unsigned long)srs, marker == 1 ? "off" : marker == 2 ? "restart" : "none");
            plug::writeText("/Plugins/Ext/offstart.txt", t, n > 0 && n < int(sizeof t) ? n : 0);
        }
    }
#endif
#ifdef FEATURE_PLUG_HOST
    plug::boot(platform);
#endif

#ifdef FEATURE_EXT_HOOKS
    DEVHOOK_TRACKER_INIT(platform, extLoad);
#else
    DEVHOOK_TRACKER_INIT(platform, interfaceGlobals.devMode);
#endif

#if defined(PLATFORM_IMX) && defined(ENABLE_DEV_CONSOLE)
    if (interfaceGlobals.devMode)
    {
        // The serial port carries the log and the development console.
        Usb* usb = platform->getUsb();
        usb->setClassState(Usb::ClassType::kSerial, true);
        usb->reset();
    }
#endif
}

void TrackerImpl::update()
{
    if (lowPower.getLowPowerState() == shutdownStateSleep)
    {
        return;
    }

    engine.updateSdStream();
    display.update();
    engine.updateSdStream();
    padsBacklight.update();
    exporter.update();
    sliceDetector.update();
    newFileManager.update();
    mtInterface.update();
    if (lowPower.getLowPowerState() == shutdownStateSleep)
    {
        return; // the shutdown ended in this pass (interrupts off, SDRAM unpowered on the original)
    }
#ifdef FEATURE_PLUG_HOST
    plug::tick();
    synthModelTick(); // SynthEditor.cpp: the model list rested on a plugin engine that is not installed
#endif
    engine.updateSdStream();
    sequencer.update();
#ifdef FEATURE_EXT_HOOKS
    features::tick();
#endif
    DEVHOOK_TRACKER_UPDATE();
}

void TrackerImpl::updateAudioBuffers(void* left, void* right, int length)
{
    if (AudioStream::renderMode)
    {
        return;
    }
    if (platform != nullptr)
    {
        midiSetAudioBlockStartUs(platform->getTimer()->micros());
    }
    Profiler::getInstance()->startMeasuringAudio();
    midiUpdate();

    // Exact hardware I2S sample rate:
    // - IMX (Tracker+ and Tracker Mini): PLL4 = 24 MHz * (37 + 15168/24000) / 2 / 4 / 10 / 256 = 44100.0 Hz
    // - MK66 (Original Tracker): F_CPU = 180 MHz * 16 / 255 / 256 = 44117.6470588 Hz
#ifdef PLATFORM_IMX
    static constexpr float kAudioSampleRate = 44100.0f;
#else
    static constexpr float kAudioSampleRate = 44117.6470588f;
#endif

    float updateIntervalSeconds = length / kAudioSampleRate;
    sequencer.tick(updateIntervalSeconds);

    engine.update();

    engine.updateAudioBuffers(left, right, length);
    Profiler::getInstance()->stopMeasuringAudio();
}

void TrackerImpl::updateInputAudioBuffers(void* dataI2s, void* dataUsb, int length)
{
    engine.updateInputAudioBuffers(dataI2s, dataUsb, length);
}

void (*TrackerImpl::getStartDisplayFunc())(Platform*)
{
    return startDisplay;
}
