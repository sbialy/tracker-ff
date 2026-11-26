#pragma once
#include "config.h"
#ifdef FEATURE_SYNTH
#include "AudioStream.h"
#include "Platform.h"
#include "PolySynth/Patch.h"
#include "PolySynth/config.h"
#include "PolyUtil/misc.h"

#include "mtStructs.h"
#ifdef FEATURE_SYNTH_OPEN
#include "open_engine.h"
#include "open_load.h"
#include "open_mod.h"
#else
#include "synth_types.h"
#endif
#include <array>
#include <cassert>
#include <memory>
#include <span>

namespace polysynth
{
class Synth;
class PatchStore;
class Instrument;
class Mixer;
class PatchPool;
};

class MidiAdapter;

class Synth
{
  public:
    static constexpr float kGainSilence = -96;
    static constexpr float kGainMax = 12;
    static constexpr float kGainDefault = -9;
    static constexpr int kPanningMax = 100;
    static constexpr int kPanningDefault = 0;
    static constexpr int kSendMaxValue = 100;
    static constexpr float kSendDefaultValue = 0.0f;
#ifdef FEATURE_SYNTH_OPEN
    // SYNTH_OPEN (MK66): the run time engine list (synth-open/open_engine.h): the engines picked at build time (SYNTH_ENGINES,
    // engine_list.h, PERC only when picked, no Shaping) and the installed plugin engines (PLUG_HOST), merged by stored number.
    // The model list on screen is this list. Projects store an engine as its canonical number, not as its position here, so a
    // project keeps its engines when it is opened by a build with another engine set or other plugins.
    static int synthTypeCount() { return polysynth::open::engineCount(); }
    static polysynth::InstrumentTypeId synthTypeAt(int ui)
    {
        return static_cast<polysynth::InstrumentTypeId>(polysynth::open::engineAt(ui).def->typeId);
    }
    // Position in the list of a stored engine number, -1 when this build has not got the engine (or it is missing)
    static int storedToUi(int stored) { return polysynth::open::engineUiForStored(stored); }
    static int uiToStored(int ui) { return polysynth::open::engineAt(ui).stored; }
    // A plugin engine number this build has not got (JU6 without ju6.plg, or any later engine): the part keeps its number and
    // patches, silent
    // A listed plugin engine that is not attached (not installed yet, or it could not be loaded) is missing too
    static bool storedIsMissing(int stored)
    {
        const int ui = storedToUi(stored);
        if (ui < 0)
        {
            return polysynth::open::missingDef(stored) != nullptr;
        }
        const polysynth::open::EngineRef& r = polysynth::open::engineAt(ui);
        return r.builtin < 0 && !polysynth::open::engineOf(r.def);
    }
    // The name of a stored engine number (the list entry's, or the one remembered for a missing number, else "#<number>"), "" when
    // neither
    static const char* storedName(int stored)
    {
        const int ui = storedToUi(stored);
        return ui >= 0 ? polysynth::open::engineAt(ui).def->name : polysynth::open::missingName(stored);
    }
    static bool isStoredTypeValid(int stored) { return polysynth::open::typeForStored(stored) >= 0; }
    static bool storedTypeIs(int stored, polysynth::InstrumentTypeId id)
    {
        return polysynth::open::typeForStored(stored) == static_cast<int>(id);
    }
#else
    static constexpr std::array kSynthTypes = { polysynth::InstrumentTypeId::kSimpleAnalogSynth,
                                                polysynth::InstrumentTypeId::kFatAnalogSynth,
                                                polysynth::InstrumentTypeId::kVASynth,
                                                polysynth::InstrumentTypeId::kFMSynth,
                                                polysynth::InstrumentTypeId::kDrumSynth,
                                                polysynth::InstrumentTypeId::kShapingSynth,
                                                polysynth::InstrumentTypeId::kChiptuneSynth
                                              };
    // The built-ins of the stock list (their list icons, registry.cpp). The list on screen is typeList() (core/synth/synth_types.h): these
    // under stored 0..6, then the plugin engines by stored number, Missing for a plugin number that is not there (IMX port P7 H6)
    static synthtypes::List& typeList();
    static int synthTypeCount() { return typeList().count(); }
    static polysynth::InstrumentTypeId synthTypeAt(int ui)
    {
        return static_cast<polysynth::InstrumentTypeId>(typeList().at(ui).typeId);
    }
    static int storedToUi(int stored) { return typeList().uiForStored(stored); }
    static int uiToStored(int ui) { return typeList().at(ui).stored; }
    static bool storedIsMissing(int stored) { return typeList().missing(stored); }
    static const char* storedName(int stored) { return typeList().name(stored); }
    static bool isStoredTypeValid(int stored) { return synthtypes::List::storedValid(stored); }
    static bool storedTypeIs(int stored, polysynth::InstrumentTypeId id)
    {
        const int ui = storedToUi(stored);
        return ui >= 0 && !storedIsMissing(stored) && synthTypeAt(ui) == id;
    }
#endif
    static constexpr auto kInitPatchName = "Init";

    Synth();
    ~Synth();

    void init(Platform* platform, std::function<tiny::optional<float>()> getTempo);

    ::Midi* getVirtualMidiPort();

    std::vector<std::string_view> getPatchList(int part);
    [[nodiscard]] bool loadPatch(int part, const char* name);
    bool savePatch(int part, const char* name);
    bool savePatch(int part);
    void initPatch(int part);
    bool loadPatchFromWorkspace(int part, std::optional<std::string> overrideName = std::nullopt);
    // False: the patch could not be written, the part stays dirty. The caller reports it and does not go on with what needed the file.
    bool savePatchToWorkspace(int part);
    bool saveDirtyWorkspacePatches();
    // False: a file could not be copied or removed (SYNTH_OPEN: the files already in the destination are as they were, or complete
    // copies). The caller reports it and does not call the project saved or loaded.
    // `projectOpen` (SYNTH_OPEN): the copy is the project open one (project folder -> Workspace), which also removes the Workspace slot
    // files that the project does not have. The save copy (Workspace -> project folder) never removes a file in the project folder.
    bool copyWorkspacePatches(const char* sourceProjectPath, const char* destinationProjectPath, bool projectOpen = false);
    void setPatchDirty(int part) { parts[part].patchDirty = true; };
    bool isAnyPatchDirty();

    void setGain(int part, float gainDb);
    void setPanning(int part, float panning);
    void setVoiceCount(int part, int voiceCount);
    int getMaxVoiceCount(int part);
    void setInstrumentType(int part, polysynth::InstrumentTypeId typeId);
#ifndef FEATURE_SYNTH_OPEN
    // The engine of a stored number (synth_types.h): its type, or for a Missing number a silent built-in and the Missing flag
    void setPartEngine(int part, int stored);
    bool partMissing(int part) const { return parts[part].missing; }
    // The plugin engines of the card (kCardEngineDir, plug_index.h scanIndex) after the built-ins of typeList(), by stored number. Main
    // loop only (card reads), at the first project load and when the Model list opens. Returns the engines listed.
    static constexpr const char* kCardEngineDir = "/Plugins/Synth";
    int scanCardEngines();
    // A listed plugin engine that is not attached yet is loaded from the card into a code slot no other part plays and attached
    // (IMX, P7 H5, PlugSynthVoice.h). True when the engine can play (built-ins always), false: it stays Missing. Main loop only.
    bool attachCardEngine(int part, int stored);
#if defined(PLATFORM_IMX)
    // Paraphony of a plugin engine (PLUG_EXT_CHORD, PlugSynthVoice.h chordInfo), the IMX twin of open::partChordPara and partNoteChord:
    // true when the part plays an engine that answers the query, its patch is at Para and chord is inside the engine's Chord enum.
    // plugNoteChord then gives the part's next note on of `note` the chord and strum. Sequencer (audio thread), no allocation.
    bool plugChordPara(int part, uint8_t chord);
    void plugNoteChord(int part, uint8_t note, uint8_t chord, int8_t strum);
    // The part plays an attached plugin engine (PlugSynthVoice.cpp): New Note Action and note ids apply (the stock engines keep Release)
    bool plugPart(int part) const;
#endif
    // A text file of the card into buf (cap - 1 bytes at most, terminated), the bytes read or -1 (the synth order file of the list)
    int readCardText(const char* path, char* buf, int cap);
    // text (n bytes) as the whole file path on the card, false when it cannot be written (no folder, a card error): the synth order
    // file of the list's edit mode (Shift + F1, P12)
    bool writeCardText(const char* path, const char* text, int n);
#if defined(PLATFORM_IMX) && defined(FEATURE_PLUG_HOST)
    // Card logos of the plugin engines (IMX port P7, synth-open/plug_logo.h). cardLogoId: the bitmap id of list entry ui's image (list)
    // or editor image (editor, loaded now from the card when it is not in RAM_G), -1 for a built-in or an engine without a logo.
    // cardLogoPrefetch loads the list images of entries uis[0..n) (nearest the selection first). Main loop only (card reads).
    int cardLogoId(int ui, bool editor);
    void cardLogoPrefetch(const int* uis, int n);
    int cardLogoSlot(int stored, bool editor);                 // the RAM_G slot of stored's image, loaded when needed, -1 none
    static int cardLogoResident(int stored, bool editor);      // the slot holding it now (no card read), -1 none (sim poke logos)
#endif
#endif
#ifdef FEATURE_SYNTH_OPEN
    // Patch slots (SYNTH_OPEN): a part has kPatchSlots patches in the project, slot 0 is <P>.patch (stock opens it), slot n is
    // <P>-n.patch. The live patch is the slot that is open in the editor, the Patch step FX copies another slot's values over it.
    // Everything here is main thread only except applyPatchBankSlot (audio callback, copies floats, no shared_ptr).
    static constexpr int kPatchSlots = polysynth::open::kPatchBankSlots;
    int getPatchSlot(int part);
    // Saves the open slot when it is dirty, then opens `slot` (a slot without a file starts as a copy of the open one). kSaveFailed: the
    // open slot could not be saved, nothing changed.
    polysynth::open::SlotSelect selectPatchSlot(int part, int slot);
    // Cancel of the library browser: back to what the open slot was before the browsing
    void revertCurrentPatch(int part, const std::string& previousName);
    // If the Patch FX left another slot's values in the part, put the open slot's values back (call before showing or editing)
    void restorePatchFx(int part) { polysynth::open::patchBankRestore(part); }
    // Writes the open slot to its Workspace file when it was edited (before the engine of the part changes). False: it could not be written.
    bool flushPatchSlot(int part);
    // keepNames: an engine change, the slot files (and so the names) of the old engine stay, only slot 0's name goes
    void reloadPatchBank(int part, bool keepNames = false);
    // Engine change: before setInstrumentType, keeps the slot 0 file as <P>-b.patch (see patchBackupSave). `slotZeroName` is the name the
    // project stores for slot 0. After setInstrumentType, restoreSlotZeroBackup puts the backup back as slot 0 when it is a patch of the
    // new engine (true, patch and name are the backup's), else false and the caller loads Init. backupSlotZero is false when the edits of
    // the open slot could not be written: the engine must not change then.
    bool backupSlotZero(int part, const char* slotZeroName);
    bool restoreSlotZeroBackup(int part);
    bool applyPatchBankSlot(int part, int slot);
    const char* getSlotName(int part, int slot);
    // Morph step FX (audio callback): blend of the open slot and `slot` (-1: the part's Morph target), amount 0..100
    bool applyPatchMorph(int part, int slot, int amount);
    // The slot a Morph FX blends to (main thread). setMorphTarget writes the names file when the target changes.
    int getMorphTarget(int part) { return polysynth::open::patchBankMorphTarget(part); }
    void setMorphTarget(int part, int slot);
    // Names of slots 1..15 live in the side file <P>.names next to the slot files (see synth-open/open_slotnames.h). Written whenever
    // a name changes and on project save, read on project load.
    // False: the names file could not be written (or read before): the caller does not call the names saved.
    bool persistSlotNames(int part);
    void patchNameChanged(int part) // the open slot got another name (library load, save as, Init)
    {
        if (getPatchSlot(part) > 0)
        {
            persistSlotNames(part);
        }
    }
    // Synth automation (synth-open/open_mod.h): the 4 slots of the open patch (they belong to its slot file; the Patch FX plays another slot's, the editor always shows these).
    // setModSlot disables audio interrupts and marks the patch dirty (the slot file is written with the patch, the project structure
    // does not change). modDestinations fills `out` (open::kModPickMax) with the picker order and returns the count.
    const polysynth::open::ModSlot& getModSlot(int part, int slot) { return polysynth::open::modSlotOf(*parts[part].patch, slot); }
    void setModSlot(int part, int slot, const polysynth::open::ModSlot& value);
    const polysynth::open::EngineDef* getModEngine(int part) { return polysynth::open::modEngineOf(*parts[part].patch); }
    int modDestinations(int part, int16_t* out) { return polysynth::open::modPicker(getModEngine(part), out, polysynth::open::waveFilesAbsent()); }
    const char* modDestName(int part, int code) { return polysynth::open::modDestName(*parts[part].patch, code); }
#endif
#if defined(FEATURE_SYNTH_PATCHFX) && !defined(FEATURE_SYNTH_OPEN)
    // Patch slots on Tracker+ and Mini (FEATURE_SYNTH_PATCHFX, IMX port P12) over the stock polysynth:: patch API: slot 0 is the
    // Workspace file <P>.patch (as stock), slot n is <P>-n.patch. The bank keeps every slot's values as lossless float rows in SDRAM
    // (3 parts x (16 slots + one shadow row of the open slot's edits) x 84 floats = 17,136 B), the Patch step FX copies a row over the
    // part's live patch in the audio callback (setParamValue of the params that differ). Plugin engines get the values through the
    // same patch (PlugSynthVoice). The macros and automation of a slot are not switched (they stay the open slot's, D4).
    // Main thread only except applyPatchBankSlot (audio callback, no allocation).
    static constexpr int kPatchSlots = 16;
    enum class SlotSelect : uint8_t
    {
        kSame,       // the slot is already open
        kLoaded,     // the slot's file is the open patch now
        kCopied,     // the slot had no file: it starts as a copy of the open one
        kSaveFailed, // the open slot's edits could not be written, nothing changed
        kError,      // the slot's file could not be read or written
        kOtherEngine // the slot's file is a patch of another engine
    };
    int getPatchSlot(int part);
    SlotSelect selectPatchSlot(int part, int slot);
    // If the Patch FX left another slot's values in the part, put the open slot's values back (call before showing, editing or saving)
    void restorePatchFx(int part);
    bool applyPatchBankSlot(int part, int slot);
    // Morph step FX (audio callback): the part's values become a blend of the open slot (amount 0) and `slot` (100, -1: the part's Morph
    // target). The MK66 rule (open_cold.cpp setValuesMorph): enum params and the integral ones the open ports switch (WTFM Ratio, PERC
    // Pitch and Body Wave) switch at 50, the integral ones the open ports round (VAP Tune, PERC Timbre) are the blend rounded to the nearest
    // integer, the others are a + (b - a) * amount / 100. 0 and 100 are the Patch FX exactly.
    static constexpr int kMorphTarget = 1; // the Morph target of a part whose <P>.names has no m= line (open_slotnames.h kMorphDefault)
    static constexpr int kMorphApplied = kPatchSlots; // patchBankApplied after a blend
    bool applyPatchMorph(int part, int slot, int amount);
    // The slot a Morph FX without a Patch FX next to it blends to (main thread). setMorphTarget writes <P>.names when the target changes
    // (Shift + Left / Right on the Synth screen).
    int getMorphTarget(int part);
    void setMorphTarget(int part, int slot);
    // The name of a slot: the open one's is the patch name, the others' come from <P>.names (empty: never named)
    const char* getSlotName(int part, int slot);
    // Names of slots 1..15 and the Morph target live in the side file Workspace/<P>.names (the MK66 format, synth-open/open_slotnames.h),
    // copied with the slot files. Written when a slot is left, when the target changes and on project save. False: not written.
    bool persistSlotNames(int part);
    // The Morph kind of param i of the part's engine as the bank holds it: 'l' blend, 'r' rounded blend, 's' switch at 50 (sim reads it)
    char morphKind(int part, int param);
    // Project load and engine change: slot 0 opens, the rows of slots 1..15 come from the Workspace slot files of the part's engine
    void reloadPatchBank(int part);
    void loadSlotNames(int part); // project load: Workspace/<P>.names into the bank (names, Morph target), public for ffSynthPatchFxLoad
    // Bit n set: slot n has a row of the part's engine (the sim reads it, plus_patchfx_sim.sh)
    uint32_t patchBankFilled(int part);
    // The slot the Patch FX left in the part (-1: the open slot's own values, kMorphApplied: a Morph blend)
    int patchBankApplied(int part);
    // Patch Vary (the MK66 patch editor F4, synth-open/open_cold.cpp patchVary): every param of the open patch moves by up to +- the
    // amount of its knob range in its edit curve (100 %: every param drawn anew), enums only from 50 % (each with p = amount), whole
    // steps for the integral params of the Morph rule, the "Volume" param stays. Main thread. entropy is mixed into the generator
    // as the MK66 mixes the micros timer. Returns the params changed (0: nothing to undo).
    int varyPatch(int part, uint32_t entropy);
    // Puts back the values before the last Vary of this patch (false: none, or the patch, slot or engine changed since)
    bool undoVary(int part);
    void forgetVary();
    int getVaryPercent();
    void setVaryPercent(int percent); // 5..100
#endif

    std::string getInstrumentTypeName(int typeIdx);
    int getInstrumentTypeIdx(int part);
    std::string getPatchName(int part);
    std::string getSynthName(int part);
    std::string getParamFullName(int part, int paramId);
    const char* getMacroKnobName(int macroId);
    const char* getDefault(int macroId);

    int getSend(int part, int sendIdx); // in percent
    float getPanning(int part);         // +- percent

    // use AudioCS when modyfing the patch object
    polysynth::Patch& getPatch(int part);

    void triggerTestNote(int part, int note, bool noteOn);

    void updateToSettings(const strMtValues& settings, bool resetAllVoices = false);

    void initSynthPartSettings(int part, SynthPartSettings& settings);
    void initSynthPartMacrosValues(int part, SynthPartSettings& settings);
    void initSynthPartMacroValue(int part, int macroIdx, SynthPartSettings& settings);
    void validateSynthPartSettings(int part, SynthPartSettings& settings);

  private:
    struct Part
    {
        polysynth::InstrumentTypeId instrumentTypeId = {};
        std::shared_ptr<polysynth::Instrument> instrument = nullptr;
        std::shared_ptr<polysynth::Patch> patch = nullptr;
        std::string patchName = {};
        bool patchDirty = false;
#ifndef FEATURE_SYNTH_OPEN
        bool missing = false; // the part's stored engine is Missing (synth_types.h): silent, its patch files are not read or written
#endif
#ifdef FEATURE_SYNTH_OPEN
        std::array<std::string, polysynth::open::kPatchBankSlots> slotNames = {}; // name of each slot while it is not the open one
        std::string backupName = {}; // name of the slot 0 patch kept in <P>-b.patch by the last engine change
#endif
    };
    std::array<Part, kSynthParts> parts;
    std::shared_ptr<polysynth::Synth> synth;
    std::unique_ptr<polysynth::PatchPool> patchPool;
    std::shared_ptr<polysynth::PatchStore> patchStore;
    std::shared_ptr<polysynth::PatchStore> patchStoreWorkspace;
    std::unique_ptr<MidiAdapter> midiAdapter;
    FileSystem* filesystem;

    void setPatch(int part, std::shared_ptr<polysynth::Patch> patch, std::string name);
#ifdef FEATURE_SYNTH_OPEN
    bool loadWorkspacePatch(int part, std::optional<std::string> overrideName);
    void loadSlotNames(int part);
#endif
#if defined(FEATURE_SYNTH_PATCHFX) && !defined(FEATURE_SYNTH_OPEN)
    void patchBankStoreLive(int part, int row); // main thread, audio off: the live values into a bank row
#endif

    friend class PolySynth;
};
#endif
