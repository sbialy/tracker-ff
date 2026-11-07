// Link stubs for what the MK66 platform and the Teensy runtime provide on the device. NOT firmware.
#include "Profiler.h"
#include "host_platform.h"
#include "plug_host.h"

#include <math.h>
#include <string.h>
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
void Profiler::getAudioProcessingUsageUs(AudioProcessorUsage& u, bool) { u = AudioProcessorUsage{}; }
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
