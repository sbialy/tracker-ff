// USB test console, Mac side. NOT firmware. The console core and its binding to the UI (core/source/UsbTest*.cpp) are the
// firmware's own. What is here is the platform: the serial port is stdin and stdout, the key and pad queue is the firmware's
// real Keys class (platforms/mk66/3rdparty/Buttons/Keys.cpp, hold repeat and Shift state included), time is the virtual clock.
// ui_shot --serve drives it, see ui_shot_main.cpp.
#include "host_platform.h"

#include "UsbTest.h"

#include "Keys.h"
#include "Tracker.h"

#include <stdarg.h>
#include <deque>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>

void onButtonPush(uint8_t n);
void onButtonRelease(uint8_t n);
void onButtonHold(uint8_t n);
void onPadPush(uint8_t n);
void onPadRelease(uint8_t n);
void onPadHold(uint8_t n);
void onPotChange(uint8_t n, int16_t value);

// stand-ins for core_pins.h (host_stubs/)
uint32_t millis() { return hostp::g_nowMs; }
void pinMode(uint8_t, uint8_t) {}
void attachInterrupt(uint8_t, void (*)(void), int) {}
void detachInterrupt(uint8_t) {}

namespace hostp
{
namespace
{
Keys g_buttons;
Keys g_pads;
bool g_keysReady;
std::deque<uint8_t> g_rx;
std::string g_tx;
bool g_link = true;
size_t g_txPerStep = 4096;
size_t g_txBudget = 4096;
int32_t g_encoderDelta;
uint32_t g_encoderTimer;

void readyKeys()
{
    if (g_keysReady) return;
    g_keysReady = true;
    g_buttons.setOnPush(onButtonPush);
    g_buttons.setOnRelease(onButtonRelease);
    g_buttons.setOnHold(onButtonHold);
    g_pads.setOnPush(onPadPush);
    g_pads.setOnRelease(onPadRelease);
    g_pads.setOnHold(onPadHold);
}
}   // namespace

bool keyPressed(bool pad, int idx)
{
    readyKeys();
    return idx >= 0 && idx < BUTTON_MAX && (pad ? g_pads : g_buttons).isButtonPressed(uint8_t(idx)) != 0;
}

// One main loop pass of the device (updateHardware, then Tracker::update) after 5 ms of the virtual clock
void usbtestStep()
{
    readyKeys();
    advance(5);
    g_buttons.update();
    g_pads.update();
    if (g_nowMs - g_encoderTimer > 100)   // EncoderImpl::update: the knob is read every 100 ms
    {
        g_encoderTimer = g_nowMs;
        if (g_encoderDelta != 0)
        {
            const int32_t d = g_encoderDelta;
            g_encoderDelta = 0;
            onPotChange(0, int16_t(d));
        }
    }
    g_txBudget = g_txPerStep;
    Tracker::getInstance()->update();
}

void usbtestFeed(const void* data, size_t n)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    g_rx.insert(g_rx.end(), p, p + n);
}

size_t usbtestRxPending() { return g_rx.size(); }
std::string& usbtestTx() { return g_tx; }
void usbtestSetLink(bool up) { g_link = up; }
void usbtestSetTxRate(size_t perStep) { g_txPerStep = perStep; g_txBudget = perStep; }

// ------------------------------------------------------------------------------------------------ simulated bootloader
// The device hands a .ptf to the Polyend bootloader and comes back as the new build. Here the hand-off ends the process (exit code 77,
// the same as a reset) and the next start reads /Firmware/_fwinfo, finds the build id in the named image and reports it from then on.
namespace
{
char s_buildId[24] = "uishot";

bool readAll(const std::string& path, std::string& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

// Intel HEX text to the bytes of the image (types 00 and 02, gaps are 0xFF)
std::vector<uint8_t> decodeHex(const std::string& text)
{
    std::vector<uint8_t> image;
    uint32_t upper = 0;
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.size() < 11 || line[0] != ':') continue;
        auto byteAt = [&](size_t i) { return unsigned(strtoul(line.substr(1 + 2 * i, 2).c_str(), nullptr, 16)); };
        const unsigned count = byteAt(0), type = byteAt(3);
        const uint32_t addr = (byteAt(1) << 8) | byteAt(2);
        if (type == 2) upper = ((byteAt(4) << 8) | byteAt(5)) << 4;
        if (type != 0) continue;
        if (upper + addr + count > image.size()) image.resize(upper + addr + count, 0xFF);
        for (unsigned i = 0; i < count; i++) image[upper + addr + i] = uint8_t(byteAt(4 + i));
    }
    return image;
}

// The build id is a NUL delimited string of seven lower case hex digits (BUILD_TAG is a short git hash). Exactly one such string or none.
std::string findBuildId(const std::vector<uint8_t>& image)
{
    std::string found;
    for (size_t i = 0; i + 8 <= image.size(); i++)
    {
        if (i > 0 && image[i - 1] >= 0x20 && image[i - 1] < 0x7F) continue;
        if (image[i + 7] != 0) continue;
        bool hex = true;
        for (size_t k = 0; k < 7; k++) hex = hex && ((image[i + k] >= '0' && image[i + k] <= '9') || (image[i + k] >= 'a' && image[i + k] <= 'f'));
        if (!hex) continue;
        const std::string id(reinterpret_cast<const char*>(&image[i]), 7);
        if (!found.empty() && found != id) return "";
        found = id;
    }
    return found;
}
}   // namespace

const char* simBuildId() { return s_buildId; }

long simFreeBytes()
{
    if (const char* forced = getenv("UISHOT_FREE_BYTES")) return atol(forced);
    struct statvfs sv;
    if (statvfs(sdRoot().c_str(), &sv) != 0) return -1;
    const unsigned long long bytes = (unsigned long long)sv.f_bavail * sv.f_frsize;
    return bytes > 0x7FFFFFFFull ? 0x7FFFFFFF : long(bytes);
}

void simBootloaderOnStart()
{
    const std::string state = sdRoot() + "/.uishot_flash";   // what is in the "flash" survives the restarts of this process
    std::string text;
    if (readAll(state, text) && !text.empty()) snprintf(s_buildId, sizeof(s_buildId), "%s", text.c_str());
    const std::string info = sdRoot() + "/Firmware/_fwinfo";
    std::string name;
    if (!readAll(info, name)) return;
    if (getenv("UISHOT_BOOTLOADER_STUCK"))
    {
        fprintf(stderr, "ui_shot: the simulated bootloader does not return\n");
        exit(79);
    }
    remove(info.c_str());
    std::string hex;
    if (!readAll(sdRoot() + "/Firmware/" + name, hex)) return;
    const std::string id = findBuildId(decodeHex(hex));
    if (id.empty()) return;
    snprintf(s_buildId, sizeof(s_buildId), "%s", id.c_str());
    if (FILE* f = fopen(state.c_str(), "wb")) { fputs(id.c_str(), f); fclose(f); }
}

void simBootloaderHandoff(const char* name)
{
    if (getenv("UISHOT_BOOTLOADER_FAIL")) return;
    const std::string info = sdRoot() + "/Firmware/_fwinfo";
    remove(info.c_str());
    if (FILE* f = fopen(info.c_str(), "wb")) { fwrite(name, 1, strlen(name), f); fclose(f); }
    if (const char* log = getenv("UISHOT_BOOTLOADER_LOG"))
        if (FILE* f = fopen(log, "ab")) { fprintf(f, "runBootloader %s\n", name); fclose(f); }
    fwrite(g_tx.data(), 1, g_tx.size(), stdout);   // what the console queued before it gave up the CPU
    fflush(stdout);
    _exit(77);
}
}   // namespace hostp

// ------------------------------------------------------------------------------------------------ UsbTest.h platform hooks
int UsbTestPlatform_RxByte()
{
    if (hostp::g_rx.empty()) return -1;
    const int b = hostp::g_rx.front();
    hostp::g_rx.pop_front();
    return b;
}
bool UsbTestPlatform_LinkUp() { return hostp::g_link; }
size_t UsbTestPlatform_TxFree() { return hostp::g_txBudget; }
size_t UsbTestPlatform_TxWrite(const uint8_t* data, size_t length)
{
    if (length > hostp::g_txBudget) length = hostp::g_txBudget;
    hostp::g_tx.append(reinterpret_cast<const char*>(data), length);
    hostp::g_txBudget -= length;
    return length;
}
uint32_t UsbTestPlatform_IrqLock() { return 0; }
void UsbTestPlatform_IrqUnlock(uint32_t) {}
bool UsbTestPlatform_ValidPointer(const void* p) { return p != nullptr; }
bool UsbTestPlatform_InjectButton(uint8_t id, bool down) { return hostp::g_buttons.inject(id, down); }
bool UsbTestPlatform_InjectPad(uint8_t id, bool down) { return hostp::g_pads.inject(id, down); }
void UsbTestPlatform_InjectEncoder(int delta) { hostp::g_encoderDelta += delta; }
bool UsbTestPlatform_InputPending()
{
    return hostp::g_buttons.injectPending() || hostp::g_pads.injectPending() || hostp::g_encoderDelta != 0;
}
uint32_t UsbTestPlatform_ResetStatus() { return 0; }
// UISHOT_FAKE_FAULT_BUILD=ID: the simulated unit that runs build ID reports a fault record (markers fault=1), for the crash marker case of ab_e2e.py
bool UsbTestPlatform_FaultRecord(uint32_t* pc, uint32_t* lr, uint32_t* psr, uint32_t* cfsr)
{
    const char* id = getenv("UISHOT_FAKE_FAULT_BUILD");
    if (!id || strcmp(id, hostp::simBuildId()) != 0) return false;
    *pc = 0x0001A2B4; *lr = 0x0001A2A1; *psr = 0x61000000; *cfsr = 0x00000100;
    return true;
}
long UsbTestPlatform_FreeBytes() { return hostp::simFreeBytes(); }

// LOG() of the core (core/source/log.h): into the console ring, like on the device
void _debuglog(const char* msg) { UsbTest_Log(msg); }
void _debuglogf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    UsbTest_LogV(format, args);
    va_end(args);
}
