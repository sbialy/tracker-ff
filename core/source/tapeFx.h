// Tape step FX (tracker-ff, FX_TYPE_TAPE, id 50): Stop and Start in ONE FX. Pure arithmetic shared by the sample voice
// (AudioPlayMemory), the synth parts (synth-open/open_synth.cpp), the pattern editor label and the host test
// (synth-open/tools/tape_fx_test.cpp). No includes of the firmware: it builds on the Mac as is.
//
// The FX value picks the direction and the length in quarters of a step: 1..64 = Stop (run-down of 0.25 to 16 steps), 65..128 =
// Start (run-up of v - 64 quarters). 0 reads as 1, 129..255 as 128 (reserved, clamped on playback, never wrapped). The editor walks
// 1..128 in one range, so turning the value runs Stop into Start. The old Start id 51 (dev projects only) is migrated on load to
// id 50 with the value + 64 (core/source/fileManagerPattern.cpp, fx34_io.cpp).
// The tape speed is a ratio: Stop 1 -> 0 and Start 0 -> 1 in a straight line over that time (a motor that brakes or spins up at a
// constant rate), so the pitch falls as 12 log2(ratio) semitones, slowly at first and ever faster at the end. The level follows
// the speed: full down to half of the run, a straight fade to silence after that (Stop), mirrored for Start.
#pragma once

#include <cmath>
#include <cstdint>

namespace tapefx
{
constexpr uint8_t kValueMin = 1;
constexpr uint8_t kRunMax = 64;      // longest run in quarters of a step
constexpr uint8_t kValueMax = 128;   // 1..64 Stop, 65..128 Start
constexpr uint8_t kValueDefault = 4; // one step
constexpr uint32_t kBlockFrames = 128;
constexpr float kFramesPerMs = 44.1176470588f; // MK66 audio rate 44,117.647 Hz
// the slowest speed a voice crawls at before it is silent: 5 octaves down, the same floor as the lowest sample note (note 0)
constexpr float kFloorRatio = 1.0f / 32.0f;

enum Mode : uint8_t
{
    kOff = 0,
    kStop = 1,
    kStart = 2,
};

constexpr uint8_t clampValue(uint8_t v) { return v < kValueMin ? kValueMin : (v > kValueMax ? kValueMax : v); }
constexpr bool isStart(uint8_t v) { return clampValue(v) > kRunMax; }
// run length in quarters of a step, 1..64, of either direction
constexpr uint8_t runValue(uint8_t v) { return isStart(v) ? uint8_t(clampValue(v) - kRunMax) : clampValue(v); }
constexpr float steps(uint8_t v) { return float(runValue(v)) * 0.25f; }
inline float lengthMs(uint8_t v, float stepMs) { return steps(v) * stepMs; }
inline uint32_t lengthFrames(float ms)
{
    const float f = ms * kFramesPerMs;
    return f < 1.0f ? 1u : uint32_t(f);
}

// Tape speed after `pos` of `total` frames: Stop 1 -> 0, Start 0 -> 1
inline float ratio(Mode mode, uint32_t pos, uint32_t total)
{
    if (mode == kOff || total == 0)
    {
        return 1.0f;
    }
    const float t = pos >= total ? 1.0f : float(pos) / float(total);
    return mode == kStop ? 1.0f - t : t;
}
// Level for a speed: full from half speed up, a straight fade to 0 below
constexpr float gainFor(float r) { return r >= 0.5f ? 1.0f : (r <= 0.0f ? 0.0f : 2.0f * r); }
// Pitch offset in semitones for a speed (never below the floor, so a run-down reads -60 st at the end)
inline float semitonesFor(float r) { return 12.0f * std::log2(r < kFloorRatio ? kFloorRatio : r); }

// Grid letter of the Tape FX: W for a Stop, Z for a Start (the list entry says "W - Tape")
constexpr char letter(uint8_t v) { return isStart(v) ? 'Z' : 'W'; }

// Label of the FX value in the pattern grid: "1", "0.25", "1.5" (quarters of a step as steps)
inline int labelWhole(uint8_t v) { return runValue(v) / 4; }
inline int labelQuarter(uint8_t v) { return (runValue(v) % 4) * 25; }

// Load migration of the dev projects (id 51 = the former Tape Start, never released): FX id 51 -> 50 with the value + 64 (the old
// playback clamp 0 -> 1 and 65..255 -> 64 is applied first, so it sounds as before). `type` and `value` are the bytes of one FX slot.
// Idempotent: a migrated slot holds id 50. An old id 50 value above 64 is NOT touched: it is a Start now (the old editor never wrote
// one, only a hand made file can hold it, and the new values 65..128 must survive every load).
constexpr uint8_t kIdTape = 50;
constexpr uint8_t kIdRetiredStart = 51;
inline void migrateSlot(uint8_t& type, uint8_t& value)
{
    if (type == kIdRetiredStart)
    {
        type = kIdTape;
        value = uint8_t(kRunMax + (value < kValueMin ? kValueMin : (value > kRunMax ? kRunMax : value)));
    }
}
} // namespace tapefx
