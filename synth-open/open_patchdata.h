// .patch file model and codec interface of the open PolySynth shim (codec in open_patchio.cpp).
// Plain data, no PolySynth includes, so the codec object stays small.
#pragma once

#include <cstddef>
#include <cstdint>

namespace polysynth::open
{

constexpr int kMaxParams = 84; // PERC has 84


// ---- .patch file model (codec in open_patchio.cpp, see there)

constexpr int kPatchMacros = 8;          // 6 user + Velocity + Pressure
constexpr int kMacroTargets = 6;         // targets per macro in the file
constexpr int kMacroNameMax = 20;        // Patch::Metadata::Macro::kMaxDisplayNameLen
constexpr size_t kPatchFileMax = 8192;   // larger files are refused (as OpenTracker)
constexpr size_t kRawMax = 768;          // names, unknown and extension fields kept verbatim

struct PatchMacroData
{
    char name[kMacroNameMax + 1] = {};
    bool bipolar = false;
    int8_t count = 0;                    // live targets
    int16_t param[kMacroTargets] = {};
    float amount[kMacroTargets] = {};
};

// Macro depth mode (per patch). Add: a macro moves a parameter by amount * macro * (max - min). Scale: the depth is
// relative to the headroom, +100 % sweeps base -> max and -100 % base -> min over the full macro travel.
// File: top-level field 100 (varint), 1 = Scale. Absent = Add, so every existing file loads as Add and an Add patch
// is written without the field (byte identical to what older firmware wrote).
constexpr uint32_t kMacroModeField = 100;
constexpr uint8_t kMacroModeAdd = 0;
constexpr uint8_t kMacroModeScale = 1;

struct PatchExtra
{
    uint16_t len = 0;
    bool hasNames = false;               // the raw bytes hold the four name fields
    uint8_t bytes[kRawMax] = {};
};

// ---- synth automation (.patch field 64, synth-open/open_mod.h has the run time and the rules)

constexpr uint32_t kModField = 64;       // top level field of the block: stock uses 1..9, OpenTracker 2-5, 10, 11, 16
constexpr int kModSlots = 4;             // slots per patch (the file has a repeated slot list, so more later is no format change)
constexpr int kModNameMax = 20;          // destination name kept in the file (as ParamDef::name, cut to 20)
constexpr size_t kModBlockMax = 256;     // the whole field 64 at most: 4 slots with 20 character names and every field at its top are 202 B

enum class ModType : uint8_t
{
    kOff,
    kLfo,
    kEnv,
};
constexpr int kModTypes = 3;

enum class ModKind : uint8_t
{
    kParam,   // an engine parameter, by index + name + occurrence of the name
    kMacro,   // user macro M1..M6, by number
};

constexpr int kLfoShapes = 5;            // Rev Saw, Saw, Triangle, Square, Random (the sample page's list)
constexpr int kLfoSpeeds = 29;           // the sample page's tempo synced speeds, 128 steps .. 1/64 step
constexpr int kModTimeMax = 10000;       // envelope times in ms, sample page law
constexpr int kModAmountMax = 100;       // +-100 % of the destination's range (the file is sint32, so more later is a UI change)

// One slot as the file has it and the patch keeps it. `index`, `name` and `nth` are the stored identity, written back as they are, so a
// destination that cannot be found (a plugin changed its table) comes back with the old plugin. `dest` is the run time resolution:
// the param index (kParam) or macro 0..5 (kMacro), -1 when there is none (unresolved or no destination picked yet). Plain data.
struct ModSlot
{
    ModType type = ModType::kOff;
    ModKind kind = ModKind::kParam;
    uint8_t shape = 2;                   // Triangle
    uint8_t speed = 6;                   // 16 steps
    int16_t index = -1;                  // -1: no destination picked yet
    int16_t dest = -1;
    uint8_t nth = 0;                     // which occurrence of `name` (WTFM has three "Attack")
    int8_t amount = 50;                  // %
    uint8_t sustain = 50;                // %
    uint16_t attack = 10;                // ms
    uint16_t decay = 500;                // ms
    uint16_t release = 500;              // ms
    char name[kModNameMax + 1] = {};
};

struct PatchData
{
    int32_t engine = 0;                  // OpenTracker / Polyend engine id (ACD 1, FAT 2, VAP 4, WTFM 5, PERC 6)
    int nParams = 0;
    float values[kMaxParams] = {};       // NaN = absent (the parameter default)
    int nMacros = 0;
    uint8_t macroMode = kMacroModeAdd;   // field 100, absent = Add
    PatchMacroData macros[kPatchMacros];
    ModSlot mod[kModSlots];              // all Off: field 64 is not written (a patch without automation is as before)
    PatchExtra extra;
};

int engineIdForType(int typeId);         // 0 when unknown
int typeForEngineId(int engine);         // -1 when unknown
bool decodePatch(const uint8_t* data, size_t len, PatchData& out);   // out must be fresh (default constructed)
size_t encodePatch(const PatchData& p, uint8_t* out, size_t cap);    // bytes written, 0 on failure
bool modAnyOn(const ModSlot* slots);    // some slot is not Off
#ifdef OPEN_HOST_TEST
extern bool gModDecodeOff;              // host test: decode as an image without automation (field 64 stays raw)
#endif

} // namespace polysynth::open
