// The image's side of the factory self test, see mtTestGlue.h
#include "mtTestGlue.h"
#include "core/modulesBase.h"   // interfaceButtonsCount (mtTest.h)
#include "mtTest.h"

#ifdef FEATURE_SELFTEST_MODULE
#include "core/interfacePopups.h"
#include "display.h"
#include "ext_hooks.h"
#include "mtAudioEngine.h"
#include "mtMidi.h"
#include "mtSleep.h"

namespace
{
Platform* s_platform;
bool s_setup;   // the module has the platform's parts
uint8_t s_pos;  // the next digit of the start code (cTest::runTestByCombinaion has the same matcher)
} // namespace

void selftestSetup(Platform* platform) { s_platform = platform; }

void selftestLoop()
{
    if (ext_hooks.selftest) ext_hooks.selftest(EXT_SELFTEST_LOOP, nullptr);
}

bool selftestCombo(uint8_t pad, bool mini)
{
    const uint8_t* code = mini ? startCodeMini : startCode;
    if (code[s_pos++] != pad)
        s_pos = 0;
    else if (s_pos == 4)
    {
        s_pos = 0;
        return true;
    }
    return false;
}

bool selftestReady()
{
    const char* line2 = "";
    if (!ext_hooks.selftest)
    {
        ext_extras_bind(s_platform);
        ext_modit_check(nullptr, &line2);
    }
    if (!ext_hooks.selftest)
    {
        mtPopups.showInfoPopup("Self test needs extras.plg", line2 && *line2 ? line2 : "Copy extras.plg to Plugins/Ext");
        return false;
    }
    if (!s_setup)
    {
        ext_hooks.selftest(EXT_SELFTEST_SETUP, s_platform);
        s_setup = true;
    }
    return true;
}

void selftestStart(InputProcessor* ip)
{
    if (ext_hooks.selftest) ext_hooks.selftest(EXT_SELFTEST_START, ip);
}

// Image functions only the self test calls (gc-sections drops them once mtTest.cpp is the module's): referenced here so the image keeps them
// and extras.plg links against them. Never called, synth-open/ext_module.cpp keepForModule references it.
void selftestKeep()
{
    auto volatile a = &InputProcessor::setPadsGlobal;
    auto volatile b = static_cast<void (InputProcessor::*)(uint8_t, void (*)(int16_t), bool)>(&InputProcessor::setPotObj);
    auto volatile c = &InputProcessor::setPowerButtonObj;
    auto volatile d = &audioEngine::startTestSignal;
    auto volatile e = &audioEngine::stopTestSignal;
    auto volatile f = static_cast<void (cDisplay::*)()>(&cDisplay::clear);
    auto volatile g = &mtSleep::resetMCU;
    (void)a, (void)b, (void)c, (void)d, (void)e, (void)f, (void)g;
    asm volatile("" ::"r"(&resetMidi), "r"(&sendNoteOnRaw), "r"(&setHandleNoteOnAction));
}
#else
#include "Platform.h"

void selftestSetup(Platform* platform)
{
    mtTest.setCodecAudio(platform->getCodecAudio());
    mtTest.setRadio(platform->getRadio());
    mtTest.setButtons(platform->getButtons());
    mtTest.setEncoder(platform->getEncoder());
    mtTest.setTimer(platform->getTimer());
    mtTest.setBattery(platform->getBattery());
    mtTest.setFileSystem(platform->getFileSystem());
}
void selftestLoop() { mtTest.testLoop(); }
bool selftestCombo(uint8_t pad, bool mini) { return (mini ? mtTest.runTestByMiniCombinaion(pad) : mtTest.runTestByCombinaion(pad)) != 0; }
bool selftestReady() { return true; }
void selftestStart(InputProcessor* ip) { mtTest.runTestingProcedure(ip); }
#endif
