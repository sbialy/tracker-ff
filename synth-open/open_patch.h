// OpenPatch: the shim's Patch implementation, shared by the render path (open_synth.cpp, -O2) and
// the cold code (open_cold.cpp, -Os: metadata, value text, patch store, registry glue).
// Only effective() and velocityAmount() are read on the render path, the virtual Patch API is
// GUI / project load code. Written from the open headers in synth/include/PolySynth.
#pragma once

#include "open_engine.h"
#include "open_mod.h"
#include "open_patchdata.h"

#include "PolySynth/FileSystem.h"
#include "PolySynth/Patch.h"
#include "PolySynth/PatchStore.h"
#include "PolySynth/sdk/InstrumentType.h"
#include "PolySynth/sdk/InstrumentTypeRegistry.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>

namespace polysynth::open
{

constexpr int kVelocityMacro = static_cast<int>(Patch::SystemMacroId::kVelocity);

// Knob position [0,1] <-> value (cold, open_cold.cpp)
float curveToValue(const ParamDef& d, float n);
float valueToCurve(const ParamDef& d, float v);

class OpenPatch final : public Patch
{
  public:
    OpenPatch(int typeId, const InstrumentType* type);

    // The object (about 3 KB) lives in a small pool in the SDRAM (open_cold.cpp), not on the heap: on the device a short heap
    // failed the allocation and the project load faulted. Main thread only. A pool that is full falls back to the heap.
    static void* operator new(size_t n);
    static void operator delete(void* p) noexcept;

    const EngineDef* getDef() const { return def; }
    int paramCount() const { return count; }

    // .patch transfer (cold). importData needs audio interrupts disabled: it rewrites macros the
    // render path reads.
    void exportData(PatchData& d) const;
    void importData(const PatchData& d);
    // Patch bank (patch change FX): the values of a decoded file, same range rules as importData, into out[paramCount()]
    // (cold). setValuesRaw / getValuesRaw are the whole change on the audio callback: floats only, macros and extras stay.
    void valuesFromData(const PatchData& d, float* out) const;
    void setValuesRaw(const float* v) { std::memcpy(values, v, size_t(count) * sizeof(float)); }
    void getValuesRaw(float* out) const { std::memcpy(out, values, size_t(count) * sizeof(float)); }
    int typeId() const { return metadata.instrumentTypeId; }

    // Shim internal: macro CC (0..127) written by the instrument from MIDI
    void setMacroCC(int macro, uint8_t cc) { macroCC[macro] = cc; }

    // Values with macros M1..M6 applied (additive, scaled by the param range, clamped). Render path. `modOut` (kModSlots values, the
    // instrument's automation outputs of this block) adds the slots on top: a macro slot moves its macro, a param slot adds
    // out * range to the param, every hit param is clamped once. nullptr: no automation (the editor's final value).
    void effective(float* out, const float* modOut = nullptr) const;

    // Synth automation (open_mod.h). The slots belong to the patch (its .patch field 64), never to a Patch FX row: the FX swaps values
    // only. A setter needs audio interrupts disabled by the caller. modSerial changes on every import, so the instrument restarts its
    // modulators when another file comes in.
    const ModSlot& modSlot(int s) const { return mod[s]; }
    void setModSlot(int s, const ModSlot& v)
    {
        mod[s] = v;
        modOn = modAnyOn(mod);
    }
    uint8_t modSerial() const { return modSer; }
    bool modActive() const { return modOn; }

    // The macros and automation slots as the render path reads them now (the stress test compares them with the slot file, cold).
    void liveCfg(SlotCfg& out) const;

    // Gain slope of Macro 7 (velocity) on the Volume param: gain = vol * (1 + k * (vel - 1))
    float velocityAmount() const
    {
        float k = 0.0f;
        for (const auto& mod : metadata.macros[kVelocityMacro].modifiers)
        {
            if (def && mod.paramId.has_value() && *mod.paramId == def->volumeParam)
            {
                k += mod.amount;
            }
        }
        return k;
    }

    // The two getters give the edited value of the open slot: while the Patch FX has replaced the values they come from the bank's shadow
    // row. A setter needs audio interrupts disabled by the caller (like importData): it puts the shadow back in that window first, so
    // the edit is made on the edited values.
    optional_paramvalue_t getParamValue(int id) const override;
    tiny_optional<float> getParamValueEditMapped(int id) const override;
    void setParamValueEditMapped(int id, tiny_optional<float> v, ParamChangeSource src) override;
    optional_paramvalue_t getParamFinalValue(int id) const override;
    void setParamValue(int id, optional_paramvalue_t value, ParamChangeSource) override;

    // The instrument polls the patch every block, so no listener plumbing is needed.
    void subscribeParamUpdates(std::weak_ptr<ParamChangeListener>) override {}
    void unsubscribeParamUpdates(const ParamChangeListener&) override {}
    void startBatchUpdate() override {}
    void finishBatchUpdate() override {}

    optional_paramvalue_t getMacroValue(int id) const override;
    void setMacroValue(int id, optional_paramvalue_t value, tiny_optional<int>) override;
    void setMacroModifier(int macroId, int idx, tiny_optional<int> paramId) override;
    void setMacroModifierAmount(int macroId, int idx, float amount) override;
    tiny_optional<float> getMacroModifierAmount(int macroId, int idx) override;
    void clearMacroModifiers() override;
    void changeMacroModifierAmount(int macroId, int idx, paramvalue_t delta) override;
    void setMacroBipolar(int macroId, bool bipolar) override;
    void setMacroDisplayName(int macroId, std::string name) override;

    const Sample* getSample(int) const override { return nullptr; }
    LoadStatus loadSample(int, const std::string&) override { return LoadStatus::kFileError; }

    const Metadata& getMetadata() const override { return metadata; }
    const Sequencer& getSequencer() const override { return sequencer; }
    void setSequence(const Sequencer::Sequence&, int) override {}
    void setSequencerConfig(const Sequencer::Config&) override {}

  private:
    void buildCfg(SlotCfg& out) const; // from the metadata macros and the slots (cold)
    bool inRange(int id) const { return id >= 0 && id < count; }
    bool validMacro(int id) const { return id >= 0 && id < int(kMacroCount); }
    Metadata::Macro::ParamModifier* modifier(int macroId, int idx);

    const EngineDef* def;
    Metadata metadata;
    Sequencer sequencer;
    int count = 0;
    float values[kMaxParams] = {};
    uint8_t macroCC[kMacroCount] = {};
    ModSlot mod[kModSlots];
    uint8_t modSer = 0;
    bool modOn = false;                  // some slot is not Off (the render path skips the automation when not)
    PatchExtra extra;                    // raw .patch fields kept for the round trip
};

// The data of a new patch of the engine (what exportData of a fresh OpenPatch gives, cold), made from the engine definition alone: no
// OpenPatch, no metadata, so no heap. `d` must be fresh (default constructed).
void initPatchData(const EngineDef& def, PatchData& d);

// The patch store (cold, open_cold.cpp)
std::unique_ptr<PatchStore> makePatchStore(const InstrumentTypeRegistry& registry,
                                           FileSystem* fs,
                                           bool typeFolders);

} // namespace polysynth::open
