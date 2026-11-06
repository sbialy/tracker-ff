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
    // A known engine that is not installed (JU6 without ju6.plg): the part keeps its number and patches, silent
    // A listed plugin engine that is not attached (round E: not installed yet, or it could not be loaded) is missing too
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
    // The name of a stored engine number (the list entry's, or the missing placeholder's), "" when neither
    static const char* storedName(int stored)
    {
        const int ui = storedToUi(stored);
        const polysynth::open::EngineDef* d = ui >= 0 ? polysynth::open::engineAt(ui).def : polysynth::open::missingDef(stored);
        return d ? d->name : "";
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
    static constexpr int storedToUi(int stored)
    {
        return (stored >= 0 && stored < static_cast<int>(kSynthTypes.size())) ? stored : -1;
    }
    static constexpr int uiToStored(int ui) { return ui; }
    static constexpr bool isStoredTypeValid(int stored) { return storedToUi(stored) >= 0; }
    static constexpr bool storedTypeIs(int stored, polysynth::InstrumentTypeId id)
    {
        const int ui = storedToUi(stored);
        return ui >= 0 && kSynthTypes[ui] == id;
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

    friend class PolySynth;
};
#endif
