// Host unit test (NOT firmware) of the MIDI remote's key and pad state: the firmware's own Keys, ButtonsBaseImpl and midiRemote.cpp
// compiled with host stand-ins for the Teensy headers and for mtInterface, driven the way the unit drives them: a USB note on or off
// into midi_remote::note, the main loop's Keys::update for the hold repeat, the matrix events into the same callbacks the hardware
// registers (hardware.cpp: onButtonPush .. onPadHold, here they log into the same event list as the remote's events).
//   H1  remote key 32 (Right, the 33rd key) counts as held, and so does every other key 0..32
//   H2  a remote pad counts as held on the pad group, all 48 of them, the ones above 31 too
//   H3  a held remote key and pad repeat (hold events) as a finger's do
//   H4  a note on twice sends one press, a note off of a key the remote does not hold sends nothing
//   H5  a finger and the remote on one key or pad: one press, one release, in either order
//   H6  set() (channel or base note change, feature off) lets go of what the remote holds
//   H7  the USB cable is pulled with notes held (releaseSource): keys and pads go up and stop repeating, DIN holds stay
// H1 to H4 run against the tree before the fix too (no API of the fix used), H5 and H6 need it (REMOTE_HELD_FIX).
#include "ButtonsBaseImpl.h"
#include "core/interface.h"
#include "midiRemote.h"

#include <stdio.h>
#include <string>
#include <vector>

static uint32_t g_ms = 1000;
uint32_t millis() { return g_ms; }
void pinMode(uint8_t, uint8_t) {}
void attachInterrupt(uint8_t, void (*)(void), int) {}
void detachInterrupt(uint8_t) {}

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL line %d  %s\n", __LINE__, #c); } } while (0)

// ---- the event log: what reaches the interface, from the remote and from the matrix callbacks alike
static std::vector<std::string> g_log;
static std::string ev(const char* what, int n) { return std::string(what) + std::to_string(n); }

class TestSwitch : public ButtonsBaseImpl
{
  public:
    void physical(int n, bool down)
    {
#ifdef REMOTE_HELD_FIX
        buttonsDriver.physical(uint8_t(n), down);
#else
        (void)n, (void)down;
#endif
    }
    void hold() { buttonsDriver.update(); }
};
static TestSwitch g_keys, g_pads;

class TestPlatform : public Platform
{
  public:
    void setPowerMode(bool) override {}
    void resetMCU() override {}
    void runBootloader(char*) override {}
    void setPowerButtonCallback(PowerButtonCallback) override {}
    bool getPowerButtonState() override { return false; }
    bool readForceStartUp() override { return false; }
    Sgtl5000* getCodecAudio() override { return nullptr; }
    Radio* getRadio() override { return nullptr; }
    SwitchGroup* getButtons() override { return &g_keys; }
    SwitchGroup* getPads() override { return &g_pads; }
    Leds* getLeds() override { return nullptr; }
    Battery* getBattery() override { return nullptr; }
    Timer* getTimer() override { return nullptr; }
    Encoder* getEncoder() override { return nullptr; }
    DisplayDriver* getDisplayDriver() override { return nullptr; }
    UsbAudio* getUsbAudio() override { return nullptr; }
    UartMidi* getUartMidi() override { return nullptr; }
    UsbMidi* getUsbMidi() override { return nullptr; }
    ConfigManager* getConfigManager() override { return nullptr; }
    FileSystem* getFileSystem() override { return nullptr; }
    CrcCalculator* getCrcCalculator() override { return nullptr; }
    SystemInfo* getSystemInfo() override { return nullptr; }
    Usb* getUsb() override { return nullptr; }
};
static TestPlatform g_platform;

Interface mtInterface;
Platform* Interface::getPlatform() { return &g_platform; }
void Interface::buttonChange(uint8_t n, uint8_t value) { g_log.push_back(ev(value == buttonPress ? "K+" : value == buttonRelease ? "K-" : "Kh", n)); }
void Interface::padPressed(uint8_t n, int8_t, int8_t, uint8_t) { g_log.push_back(ev("P+", n)); }
void Interface::padReleased(uint8_t n) { g_log.push_back(ev("P-", n)); }
void Interface::potChange(uint8_t, int16_t) {}

// the callbacks hardware.cpp registers
static void onKeyPush(uint8_t n) { mtInterface.buttonChange(n, buttonPress); }
static void onKeyRelease(uint8_t n) { mtInterface.buttonChange(n, buttonRelease); }
static void onKeyHold(uint8_t n) { mtInterface.buttonChange(n, buttonHold); }
static void onPadPush(uint8_t n) { mtInterface.padPressed(n, 0, 0, 0); }
static void onPadRelease(uint8_t n) { mtInterface.padReleased(n); }
static void onPadHold(uint8_t n) { g_log.push_back(ev("Ph", n)); }

constexpr uint8_t kChannel = 16, kBase = 36;
static void remoteKey(int key, bool down) { midi_remote::note(kChannel, uint8_t(kBase + key), down ? 100 : 0, down); }
static void remotePad(int pad, bool down) { midi_remote::note(kChannel, uint8_t(kBase + midi_remote::kKeys + pad), down ? 100 : 0, down); }

// What the firmware does when the USB bus goes idle (usb_dev.c flag, UsbMidiImpl::takeBusLost, midiUpdate): the USB source lets go.
// REMOTE_HELD_UNPLUG is the hook of the fix, a tree without it does nothing at that point (the base behaviour).
static void usbUnplugged()
{
#ifdef REMOTE_HELD_UNPLUG
    midi_remote::releaseSource(midi_remote::kSourceUsb);
#endif
}
static void dinKey(int key, bool down)
{
#ifdef REMOTE_HELD_UNPLUG
    midi_remote::note(kChannel, uint8_t(kBase + key), down ? 100 : 0, down, midi_remote::kSourceDin);
#else
    remoteKey(key, down);
#endif
}

static int count(const char* tag)
{
    int n = 0;
    for (const std::string& e : g_log) n += e == tag;
    return n;
}
static std::string joined()
{
    std::string s;
    for (const std::string& e : g_log) s += (s.empty() ? "" : " ") + e;
    return s;
}

int main()
{
    g_keys.setActionCallback(SwitchGroup::kPress, onKeyPush);
    g_keys.setActionCallback(SwitchGroup::kRelease, onKeyRelease);
    g_keys.setActionCallback(SwitchGroup::kHold, onKeyHold);
    g_pads.setActionCallback(SwitchGroup::kPress, onPadPush);
    g_pads.setActionCallback(SwitchGroup::kRelease, onPadRelease);
    g_pads.setActionCallback(SwitchGroup::kHold, onPadHold);
    midi_remote::set(true, kChannel, kBase);

    // H1 every key 0..32, key 32 is the one past a 32 bit mask
    for (int k = 0; k < midi_remote::kKeys; k++)
    {
        g_log.clear();
        remoteKey(k, true);
        const bool held = g_keys.isButtonPressed(k);
        remoteKey(k, false);
        if (!held) printf("      key %d is not held\n", k);
        CHECK(held);
        CHECK(!g_keys.isButtonPressed(k));
        CHECK(joined() == ev("K+", k) + " " + ev("K-", k));
    }
    // H2 every pad 0..47
    for (int p = 0; p < midi_remote::kPads; p++)
    {
        g_log.clear();
        remotePad(p, true);
        const bool held = g_pads.isButtonPressed(p);
        remotePad(p, false);
        if (!held) printf("      pad %d is not held\n", p);
        CHECK(held);
        CHECK(!g_pads.isButtonPressed(p));
        CHECK(joined() == ev("P+", p) + " " + ev("P-", p));
    }
    CHECK(!g_keys.isButtonPressed(5));   // a pad does not set a key

    // H3 hold repeat, Right (32) and pad 40
    g_log.clear();
    remoteKey(32, true);
    remotePad(40, true);
    for (int i = 0; i < 700; i++) { g_ms++; g_keys.hold(); g_pads.hold(); }
    CHECK(count("Kh32") >= 2);
    CHECK(count("Ph40") >= 2);
    remoteKey(32, false);
    remotePad(40, false);
    const size_t n = g_log.size();
    for (int i = 0; i < 700; i++) { g_ms++; g_keys.hold(); g_pads.hold(); }
    CHECK(g_log.size() == n);   // no repeat after the release

    // H4 a note on twice is one press, a stray note off is nothing
    g_log.clear();
    remoteKey(32, true);
    remoteKey(32, true);
    remoteKey(32, false);
    remoteKey(31, false);
    remotePad(12, true);
    remotePad(12, true);
    remotePad(12, false);
    remotePad(13, false);
    CHECK(joined() == "K+32 K-32 P+12 P-12");

#ifdef REMOTE_HELD_FIX
    // H5 a finger and the remote on the same key and the same pad, either order, one press and one release
    for (int pad = 0; pad < 2; pad++)
    {
        TestSwitch& sw = pad ? g_pads : g_keys;
        const int b = pad ? 40 : 32;
        auto remote = [&](bool down) { pad ? remotePad(b, down) : remoteKey(b, down); };
        const std::string press = ev(pad ? "P+" : "K+", b), release = ev(pad ? "P-" : "K-", b);

        g_log.clear();   // finger first, remote joins, finger lets go first
        sw.physical(b, true);
        remote(true);
        CHECK(sw.isButtonPressed(b));
        sw.physical(b, false);
        CHECK(sw.isButtonPressed(b));   // the remote still holds it
        remote(false);
        CHECK(!sw.isButtonPressed(b));
        CHECK(joined() == press + " " + release);

        g_log.clear();   // remote first, finger joins, remote lets go first
        remote(true);
        sw.physical(b, true);
        remote(false);
        CHECK(sw.isButtonPressed(b));   // the finger still holds it
        sw.physical(b, false);
        CHECK(!sw.isButtonPressed(b));
        CHECK(joined() == press + " " + release);

        g_log.clear();   // remote and finger let go in the same order they came
        remote(true);
        sw.physical(b, true);
        sw.physical(b, false);
        remote(false);
        CHECK(joined() == press + " " + release);
    }

    // H6 set() lets go: a key and a pad the remote holds, the channel changes
    g_log.clear();
    remoteKey(32, true);
    remotePad(47, true);
    midi_remote::set(true, 3, kBase);
    CHECK(!g_keys.isButtonPressed(32) && !g_pads.isButtonPressed(47));
    CHECK(joined() == "K+32 P+47 K-32 P-47");
    midi_remote::set(true, kChannel, kBase);

    // H7 the USB cable is pulled with notes held: a key (Right, 32 bit edge) and a pad go up, a repeating hold stops
    g_log.clear();
    remoteKey(32, true);
    remotePad(40, true);
    usbUnplugged();
    CHECK(!g_keys.isButtonPressed(32) && !g_pads.isButtonPressed(40));
    CHECK(joined() == "K+32 P+40 K-32 P-40");
    g_log.clear();
    for (int i = 0; i < 700; i++) { g_ms++; g_keys.hold(); g_pads.hold(); }
    CHECK(count("Kh32") == 0 && count("Ph40") == 0);   // no repeat after the cable is gone
    remoteKey(32, false);   // a late note off changes nothing
    CHECK(joined() == "");

    // H7b a key held over DIN stays through a USB unplug, the USB key goes, and a key both sources hold lasts until DIN lets go
    g_log.clear();
    remoteKey(25, true);   // USB
    dinKey(26, true);      // DIN
    usbUnplugged();
    CHECK(!g_keys.isButtonPressed(25));
    CHECK(g_keys.isButtonPressed(26));
    dinKey(26, false);
    CHECK(!g_keys.isButtonPressed(26));
    g_log.clear();
    remoteKey(30, true);
    dinKey(30, true);
    usbUnplugged();
    CHECK(g_keys.isButtonPressed(30));
    CHECK(joined() == "K+30");   // one press, the unplug sent nothing
    dinKey(30, false);
    CHECK(!g_keys.isButtonPressed(30) && joined() == "K+30 K-30");
    usbUnplugged();   // nothing held: nothing to send
    CHECK(joined() == "K+30 K-30");
#endif

    printf("%s (%d checks, %d failing)\n", g_fail ? "RED" : "GREEN", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
