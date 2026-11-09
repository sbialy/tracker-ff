// Link stubs for what the MK66 platform and the Teensy runtime provide on the device. NOT firmware.
#include "eve_model.h"
#include "Profiler.h"
#include "host_platform.h"
#include "plug_host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>

// ---- interrupts and mute (platforms/mk66/source/hardware.cpp, interruptsControl.cpp)
void enableAudioInterrupt() {}
void disableAudioInterrupt() {}
static uint8_t s_muted = 0;
void muteAudioOutByKey() { s_muted = 1; }
void unmuteAudioOutByKey() { s_muted = 0; }
uint8_t isAudioOutMuted() { return s_muted; }

// ---- Teensy core
extern "C" volatile uint32_t systick_millis_count;
volatile uint32_t systick_millis_count = 0;

// ---- platform_math.cpp (CMSIS arm_sin_q15)
int16_t sin_q15(int16_t value) { return int16_t(lrintf(sinf(float(uint16_t(value)) * 6.2831853f / 32768.0f) * 32767.0f)); }

// ---- FatFs f_stat, used by core/synth/FileSystemAdapter.cpp on a path relative to the card root
extern "C" unsigned f_stat(const char* path, void*)
{
    HostFaultSuspend keep;   // the device's FatFs f_stat does not allocate
    struct stat st;
    std::string p = path ? path : "";
    while (!p.empty() && p[0] == '/') p.erase(0, 1);
    extern std::string g_sdRootForStat;
    return stat((g_sdRootForStat + "/" + p).c_str(), &st) == 0 ? 0u : 4u;
}

// ---- Profiler.cpp (stack watermark and CPU meter of the device)
Profiler* Profiler::instance = nullptr;
void Profiler::init() {}
size_t Profiler::freeRamBytes() { return 40000; }
size_t Profiler::maxStackBytes() { return 3000; }
// The host has no audio interrupt. The development console's cpu command (CONSOLE=1) gets fixed numbers, UISHOT_FAKE_CPU=avg,max (us) changes them
// (that is how the runner's threshold check is exercised without a device)
// UISHOT_FAKE_CPU_BUILD="ID=avg,max ID=avg,max" picks the numbers by the build id the simulated unit reports (after a simulated firmware update
// it is the new image's), that is how ab_e2e.py gives the two flavours of an A/B run different CPU. UISHOT_FAKE_CPU_JITTER=N adds 0..N us to
// both (a fixed pseudo random sequence), so the repeats of a sweep have a spread.
void Profiler::getAudioProcessingUsageUs(AudioProcessorUsage& u, bool)
{
    int avg = 300, max = 900;
    if (const char* e = getenv("UISHOT_FAKE_CPU")) sscanf(e, "%d,%d", &avg, &max);
    if (const char* e = getenv("UISHOT_FAKE_CPU_BUILD"))
    {
        const std::string id = std::string(hostp::simBuildId()) + "=";
        const std::string list = std::string(" ") + e;
        const size_t at = list.find(" " + id);
        if (at != std::string::npos) sscanf(list.c_str() + at + 1 + id.size(), "%d,%d", &avg, &max);
    }
    if (const char* j = getenv("UISHOT_FAKE_CPU_JITTER"))
    {
        static uint32_t s_rng = 12345u;
        s_rng = s_rng * 1664525u + 1013904223u;
        const int span = atoi(j);
        if (span > 0)
        {
            avg += int((s_rng >> 16) % uint32_t(span + 1));
            max += int((s_rng >> 8) % uint32_t(span + 1));
        }
    }
    u = AudioProcessorUsage{avg / 3, max, max * 2 / 3, avg, avg - 40};
}
void Profiler::getAudioProcessingUsagePercent(AudioProcessorUsage& u, bool) { u = AudioProcessorUsage{}; }
void Profiler::startMeasuringAudio() {}
void Profiler::stopMeasuringAudio() {}
void Profiler::resetMeasuringAudio() {}

// ---- plug_nvm.cpp: the FlexNVM flash controller. The host has no flash, nothing is installed, plug_synth sees an empty bank
namespace plug
{
bool nvmInstall(const uint8_t*, uintptr_t, uint32_t) { return true; }   // the host has no flash to program: the plugin "runs from" its staging copy
uint32_t nvmCommands() { return 0; }
uint64_t nvmCommandCycles() { return 0; }
bool nvmErase(uint32_t) { return false; }
bool nvmProgram(uint32_t, const uint8_t*) { return false; }
}   // namespace plug
