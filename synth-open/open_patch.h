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
    OpenPatch(const OpenPatch&) = delete; // `cfg` points into the object
    OpenPatch& operator=(const OpenPatch&) = delete;

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
    // (cold). setValuesRaw / getValuesRaw swap the values on the audio callback, adoptCfg the macros and automation (a pointer).
    void valuesFromData(const PatchData& d, float* out) const;
    void setValuesRaw(const float* v)
    {
        std::memcpy(values, v, size_t(count) * sizeof(float));
        effSer++;
    }
    void getValuesRaw(float* out) const { std::memcpy(out, values, size_t(count) * sizeof(float)); }
    int typeId() const { return metadata.instrumentTypeId; }

    // Shim internal: macro CC (0..127) written by the instrument from MIDI
    void setMacroCC(int macro, uint8_t cc)
    {
        macroCC[macro] = cc;
        effSer++;
    }

    // Values with macros M1..M6 applied and clamped. Render path. Macro depth, Add mode: amount * macro * (max - min). Scale mode:
    // amount * macro * headroom, headroom = (max - base) for a positive product, (base - min) for a negative one. `modOut` (kModSlots values, the
    // instrument's automation outputs of this block) adds the slots on top: a macro slot moves its macro, a param slot adds
    // out * range to the param, every hit param is clamped once. nullptr: no automation (the editor's final value).
    void effective(float* out, const float* modOut = nullptr) const;
    // True when effective(out, modOut) would only copy the plain values: no automation output applies (modOut is null or the live config has
    // the automation off) and no user macro has a target. Render path: refreshParams then reads values() directly, no copy.
    bool plainValues(bool hasModOut) const
    {
        const SlotCfg& live = *cfg;
        if (hasModOut && live.modOn)
        {
            return false;
        }
        for (int m = 0; m < int(kUserMacroCount); m++)
        {
            if (live.macro[m].count != 0)
            {
                return false;
            }
        }
        return true;
    }
    const float* values_() const { return values; }
    // Counts every change of what effective() reads from this patch: the values, the macro positions, the macros and automation slots
    // (rebuildOwn), the live config pointer. With effStatic() (the open slot's own config and the automation off, so nothing else feeds
    // effective(): a Patch FX bank row is rewritten by the bank in place, the automation outputs move every block) an unchanged serial means
    // an unchanged result, and the instrument skips the refresh (OpenInstrument::refreshParams).
    uint32_t effSerial() const { return effSer; }
    bool effStatic() const { return cfg == &own && own.modOn == 0; }

    // Synth automation (open_mod.h). The slots belong to the patch (its .patch field 64) and the editor shows and edits the open slot's
    // (modSlot, metadata macros). What the render path reads is liveCfg(): the open slot's own compact copy (ownCfg) or, while the Patch FX
    // has another slot of the bank on, that slot's row (adoptCfg swaps a plain pointer, nothing is copied or allocated, so it can run in
    // the audio callback). A setter needs audio interrupts disabled by the caller. modSerial changes on every import (the instrument
    // restarts its modulators: another file came in), cfgSerial on every swap of the live config (the instrument keeps the modulators
    // whose slot has the same type and starts the others, Instrument::modSwitch).
    const ModSlot& modSlot(int s) const { return mod[s]; }
    void setModSlot(int s, const ModSlot& v);
    uint8_t modSerial() const { return modSer; }
    uint8_t cfgSerial() const { return cfgSer; }
    const SlotCfg& live() const { return *cfg; }                    // render path
    bool modActive() const { return cfg->modOn != 0; }              // render path
    void liveCfg(SlotCfg& out) const { out = *cfg; }                // the stress test compares it with the slot file
    const SlotCfg* ownCfg() const { return &own; }
    // The live config becomes `next` (a row of the Patch FX bank or ownCfg()). A macro whose bipolar flag differs goes to the resting
    // position of the new one (centre / zero), as an import does. Audio callback or audio interrupts disabled.
    void adoptCfg(const SlotCfg* next);
    void useOwnCfg() { adoptCfg(&own); }
    // The config a .patch file gives this patch, exactly what importData would make of it (cold, no allocation). `out` is overwritten.
    void cfgFromData(const PatchData& d, SlotCfg& out) const;

    // Macro depth mode: Add for every patch loaded from a file without the flag, Scale for a new patch.
    bool macroScale() const { return macroScaleMode; }
    void setMacroScale(bool scale)       // audio interrupts disabled by the caller, like the other setters
    {
        macroScaleMode = scale;
        rebuildOwn();
    }

    // Gain slope of Macro 7 (velocity) on the Volume param: gain = vol * (1 + k * (vel - 1))
    float velocityAmount() const
    {
        float k = 0.0f;
        const MacroRun& v = cfg->macro[kVelocityMacro];
        for (int t = 0; def && t < v.count; t++)
        {
            if (v.param[t] == def->volumeParam)
            {
                k += v.amount[t];
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
    void buildCfg(SlotCfg& out) const; // own: from the metadata macros and the slots (cold)
    void rebuildOwn();                 // after every change of the macros or the slots, under the caller's audio lock; the live config follows when it is the own
    ModSlot resolveMod(const ModSlot& in) const; // range checks and destination lookup of a slot read from a file (cold)
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
    uint8_t cfgSer = 0;
    uint32_t effSer = 0;                 // see effSerial()
    bool macroScaleMode = true;          // new patches Scale, importData sets it from the file (own.macroScale follows, buildCfg)
    SlotCfg own;                         // macros and slots of this patch in the form the render path reads (rebuildOwn)
    const SlotCfg* cfg = &own;           // the live one: own, or a row of the Patch FX bank
    PatchExtra extra;                    // raw .patch fields kept for the round trip
    PatchTail tail;                      // values and macro targets of a newer engine release kept for the round trip (open_patchdata.h)
};

// The data of a new patch of the engine (what exportData of a fresh OpenPatch gives, cold), made from the engine definition alone: no
// OpenPatch, no metadata, so no heap. `d` must be fresh (default constructed).
void initPatchData(const EngineDef& def, PatchData& d);

// The patch store (cold, open_cold.cpp)
std::unique_ptr<PatchStore> makePatchStore(const InstrumentTypeRegistry& registry,
                                           FileSystem* fs,
                                           bool typeFolders);

} // namespace polysynth::open
