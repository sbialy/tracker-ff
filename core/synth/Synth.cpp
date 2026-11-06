#ifdef FEATURE_SYNTH

#include "Synth.h"
#include "FileSystemAdapter.h"
#include "MidiAdapter.h"
#include "config.h"

#include "fileTransfer.h"




#include "PolySynth/Instrument.h"
#include "PolySynth/Mixer.h"
#include "PolySynth/PatchPool.h"
#include "PolySynth/PatchStore.h"
#include "PolySynth/Synth.h"
#include "PolySynth/config.h"

#include "PolyUtil/misc.h"
#include "fmt/core.h"

#include "util.h"
#include "util_math.h"

#include "PolySynth/Patch.h"
#include "fileManager.h"
#include "interruptsControl.h"
#ifdef FEATURE_PLUG_HOST
#include "plug_host.h"
#endif
#include "mtStructs.h"
#include <numeric>
#ifdef FEATURE_SYNTH_OPEN
#include "open_bankcopy.h"
#include "open_load.h"
#endif

static const std::array kDefaultSettings = {
    SynthPartSettings{ .gainDb = Synth::kGainDefault,
                       .voiceCount = 1,
                       .synthType = 3,
                       .panning = Synth::kPanningDefault,
                       .patchName = {},
                       .reverbSend = Synth::kSendDefaultValue,
                       .delaySend = Synth::kSendDefaultValue,
                       .macroValue = { 0 } },
    SynthPartSettings{ .gainDb = Synth::kGainDefault,
                       .voiceCount = 1,
                       .synthType = 0,
                       .panning = Synth::kPanningDefault,
                       .patchName = {},
                       .reverbSend = Synth::kSendDefaultValue,
                       .delaySend = Synth::kSendDefaultValue,
                       .macroValue = { 0 } },
    SynthPartSettings{ .gainDb = Synth::kGainDefault,
                       .voiceCount = 1,
                       .synthType = 2,
                       .panning = Synth::kPanningDefault,
                       .patchName = {},
                       .reverbSend = Synth::kSendDefaultValue,
                       .delaySend = Synth::kSendDefaultValue,
                       .macroValue = { 0 } },
    SynthPartSettings{ .gainDb = -95,
                       .voiceCount = 0,
                       .synthType = 0,
                       .panning = 0,
                       .patchName = {},
                       .reverbSend = 0.0f,
                       .delaySend = 0.0f,
                       .macroValue = { 0 } },
};

#ifdef FEATURE_SYNTH

static constexpr int kFirstSendCC = MidiAdapter::kTrackCCRange.first;
static constexpr std::array<uint8_t, kSynthCCs> defaultCCs = {
    105, 106, 102, 103, kFirstSendCC + 1, kFirstSendCC, 107, 104
};

static constexpr std::array kMacroKnobNames = { "Sample Attack", "Sample Decay",     "Bit Depth",
                                                "Filter Cutoff", "Filter Resonance", "Overdrive",
                                                "Volume" };

static constexpr int kInstrumentTypeMult = 1000;
static constexpr auto kPatchNameFormat = "%02d-%02d "; // TT-PP type-program
static constexpr auto kPatchNameTypeLen = 3;           // 2 chars of type idx + '-'

// required because of forward declarations of polysynth classes in the header
Synth::Synth() = default;
Synth::~Synth() = default;

void Synth::init(Platform* platform, std::function<tiny_optional<float>()> getTempo)
{
    synth = polysynth::Synth::createSynth(
      [](size_t)
      {
          // We don't want to use modules requiring allocation
          assert(false);
          return nullptr;
      });

    FileSystemAdapter::staticInit();

    filesystem = platform->getFileSystem();
    synth->addFileSystem(
      polysynth::Synth::FileSystemId::kPatches,
      std::make_unique<FileSystemAdapter>(std::string(kPatchesFolder) + "/", filesystem));
    synth->addFileSystem(
      polysynth::Synth::FileSystemId::kWorkspace,
      std::make_unique<FileSystemAdapter>(std::string(kWorkspaceFolder) + "/", filesystem));

    patchStore = synth->createPatchStore(polysynth::Synth::FileSystemId::kPatches, true);
    patchStoreWorkspace =
      synth->createPatchStore(polysynth::Synth::FileSystemId::kWorkspace, false);

    midiAdapter = std::make_unique<MidiAdapter>(nullptr);

    auto instruments = std::array<std::shared_ptr<polysynth::Instrument>, kSynthParts>{};
    for (auto& part : parts)
    {
        int partIdx = std::distance(parts.begin(), &part);
        auto& instrument = part.instrument;

#ifdef FEATURE_SYNTH_OPEN
        part.instrumentTypeId = synthTypeAt(partIdx % synthTypeCount());
#else
        part.instrumentTypeId = kSynthTypes[partIdx % kSynthTypes.size()];
#endif
        part.patchName = kInitPatchName;
        setPatch(partIdx, patchStore->createPatch(*part.instrumentTypeId), part.patchName);

        instrument = synth->createInstrument(partIdx);
        instrument->assignPatch(part.patch);
        instrument->connect(*midiAdapter);
        instrument->setVoiceCount(1);
        instrument->setMidiChannel(partIdx);

        instruments[partIdx] = instrument;
    }

    synth->setup(instruments, {}, nullptr, std::move(getTempo));

    for (int part = 0; part < kSynthParts; part++)
    {
        Synth::initSynthPartSettings(part, mtProject.values.synthPart[part]);
    }

    updateToSettings(mtProject.values);
}

::Midi* Synth::getVirtualMidiPort()
{
    return &*midiAdapter;
}

std::vector<std::string_view> Synth::getPatchList(int part)
{
    newFileManager.createPatchesFolder();

    return patchStore->listPatchesByType(*parts[part].instrumentTypeId);
}

bool Synth::loadPatch(int part, const char* name)
{
    assert(part < kSynthParts);

    if (patchStore->loadPatch(name, *parts[part].patch, true))
    {
        setPatch(part, std::move(parts[part].patch), name);
        return true;
    }

    return false;
}

bool Synth::savePatch(int part, const char* name)
{
    assert(part < kSynthParts);

    newFileManager.createPatchesFolder();

    if (name != nullptr)
    {
        parts[part].patchName = name;
    }

    return patchStore->savePatch(parts[part].patchName, *parts[part].patch);
}

bool Synth::savePatch(int part)
{
    assert(part < kSynthParts);

    return savePatch(part, nullptr);
}

void Synth::initPatch(int part)
{
    assert(part < kSynthParts);

    std::shared_ptr<polysynth::Patch> newPatch =
      patchStore->createPatch(*parts[part].instrumentTypeId);

    DISABLE_AUDIO_INTERRUPTS();
    parts[part].instrument->assignPatch(newPatch);
    ENABLE_AUDIO_INTERRUPTS();

    setPatch(part, std::move(newPatch), kInitPatchName);
#ifdef FEATURE_SYNTH_OPEN
    if (!savePatchToWorkspace(part))
    {
        parts[part].patchDirty = true; // the Init patch is not in its file yet: the next save writes it
    }
#else
    savePatchToWorkspace(part);
#endif
}

bool Synth::savePatchToWorkspace(int part)
{
#ifdef FEATURE_SYNTH_OPEN
    // The open slot's file: <P>.patch for slot 0, <P>-n.patch otherwise. A slot the Patch FX replaced goes back and the patch is copied
    // out in one audio-free window, the file is written from that copy (old file kept until the new one is complete), and the dirty flag
    // stays when it was not written: the edits are in memory only.
    const bool saved = polysynth::open::patchSaveOpenSlot(part, *parts[part].patch, *patchStoreWorkspace);
    polysynth::open::settleDirtyAfterSave(saved, parts[part].patchDirty);
    return saved;
#else
    patchStoreWorkspace->savePatch(fmt::format("{}", part), *parts[part].patch);
    parts[part].patchDirty = false;
    return true;
#endif
}

#ifdef FEATURE_SYNTH_OPEN
// Project load: slot 0 (<P>.patch) opens and the bank of the part is read from the Workspace slot files
bool Synth::loadPatchFromWorkspace(int part, std::optional<std::string> overrideName)
{
    polysynth::open::patchBankToSlotZero(part); // an Init fallback must not land on a slot file of the previous project
    const bool fileLoaded = loadWorkspacePatch(part, std::move(overrideName));
    reloadPatchBank(part);
    loadSlotNames(part);
    return fileLoaded;
}

bool Synth::loadWorkspacePatch(int part, std::optional<std::string> overrideName)
#else
bool Synth::loadPatchFromWorkspace(int part, std::optional<std::string> overrideName)
#endif
{
    bool fileLoaded = true;
    if (overrideName)
    {
        parts[part].patchName = std::move(*overrideName);
    }

#ifdef FEATURE_SYNTH_OPEN
    // Clean unless the Init fallback could not be written (initPatch sets the flag then: the Init patch is in memory only)
    parts[part].patchDirty = false;
#endif
    if (!patchStoreWorkspace->loadPatch(fmt::format("{}", part), *parts[part].patch, true))
    {
        if (!loadPatch(part, parts[part].patchName.c_str()))
        {
            initPatch(part);
            fileLoaded = false;
        }
    }

#ifndef FEATURE_SYNTH_OPEN
    parts[part].patchDirty = false;
#endif
    return fileLoaded;
}

#ifdef FEATURE_SYNTH_OPEN
void Synth::reloadPatchBank(int part, bool keepNames)
{
    static_assert(kSynthParts <= polysynth::open::kPatchBankParts, "one patch bank per synth part");
    assert(part < kSynthParts);

    if (keepNames)
    {
        // Engine change: the slot files 1..15 of the old engine stay on disk and so do their names (an autosave would write an
        // empty <P>.names otherwise and the names would be gone when the part is switched back). Slot 0 is the new Init.
        parts[part].slotNames[0].clear();
    }
    else
    {
        for (auto& name : parts[part].slotNames)
        {
            name.clear();
        }
        parts[part].backupName.clear();
    }
    if (parts[part].patch && patchStoreWorkspace)
    {
        polysynth::open::patchBankPreload(part, *parts[part].patch, *patchStoreWorkspace);
    }
}

// Project load: the names of slots 1..15 from Workspace/<P>.names, "Unnamed" (empty here) for any slot the file does not name
void Synth::loadSlotNames(int part)
{
    std::unique_ptr<polysynth::open::SlotNames> names(new (std::nothrow) polysynth::open::SlotNames()); // 512 B, off the shared stack
    if (!names)
    {
        return; // heap short: the slots show Unnamed
    }
    polysynth::open::patchNamesRead(part, *patchStoreWorkspace, *names); // missing or damaged: all empty
    for (int slot = 1; slot < kPatchSlots; slot++)
    {
        parts[part].slotNames[slot] = names->name[slot];
    }
    parts[part].backupName = names->name[0];
}

bool Synth::persistSlotNames(int part)
{
    std::unique_ptr<polysynth::open::SlotNames> names(new (std::nothrow) polysynth::open::SlotNames()); // 512 B, off the shared stack
    if (!names)
    {
        return false; // heap short: the file keeps the names it had
    }
    const int cur = getPatchSlot(part);
    snprintf(names->name[0], sizeof(names->name[0]), "%s", parts[part].backupName.c_str());
    for (int slot = 1; slot < kPatchSlots; slot++)
    {
        const std::string& name = slot == cur ? parts[part].patchName : parts[part].slotNames[slot];
        if (name != "Unnamed")
        {
            snprintf(names->name[slot], sizeof(names->name[slot]), "%s", name.c_str());
        }
    }
    // when the load could not read the names file this reads it again and merges (open_load.h), the names on the card are not lost
    const bool ok = polysynth::open::patchNamesPersist(part, *patchStoreWorkspace, *names);
    for (int slot = 1; slot < kPatchSlots; slot++)
    {
        if (slot != cur && names->name[slot][0] && (parts[part].slotNames[slot].empty() || parts[part].slotNames[slot] == "Unnamed"))
        {
            parts[part].slotNames[slot] = names->name[slot]; // a name that the memory lacked and the card had
        }
    }
    if (parts[part].backupName.empty() && names->name[0][0])
    {
        parts[part].backupName = names->name[0];
    }
    return ok;
}

bool Synth::backupSlotZero(int part, const char* slotZeroName)
{
    if (parts[part].patchDirty && !savePatchToWorkspace(part))
    {
        return false; // the edits of the open slot are in its file before slot 0's file is copied, else the engine does not change
    }
    const polysynth::open::Backup backup = polysynth::open::patchBackupSave(part, *parts[part].patch, *patchStoreWorkspace);
    if (!polysynth::open::backupAllowsEngineChange(backup))
    {
        return false; // the card failed: slot 0 may be a patch that only exists there, the new engine's Init would replace it
    }
    if (backup == polysynth::open::Backup::kSaved)
    {
        parts[part].backupName = slotZeroName;
        persistSlotNames(part); // a failed write is tried again by the next project save (saveDirtyWorkspacePatches)
    }
    return true;
}

bool Synth::restoreSlotZeroBackup(int part)
{
    if (!polysynth::open::patchBackupRestore(part, *parts[part].patch, *patchStoreWorkspace))
    {
        return false;
    }
    parts[part].patchName = parts[part].backupName.empty() ? "Unnamed" : parts[part].backupName;
    parts[part].patchDirty = false;
    return true;
}

bool Synth::applyPatchBankSlot(int part, int slot)
{
    return part >= 0 && part < kSynthParts && polysynth::open::patchBankApply(part, slot);
}

int Synth::getPatchSlot(int part)
{
    return polysynth::open::patchBankCurrent(part);
}

polysynth::open::SlotSelect Synth::selectPatchSlot(int part, int slot)
{
    assert(part >= 0 && part < kSynthParts);
    using polysynth::open::SlotSelect;

    const int previous = getPatchSlot(part);
    if (slot == previous)
    {
        return SlotSelect::kSame;
    }

    if (parts[part].patchDirty && !savePatchToWorkspace(part)) // a dirty slot is written when it is left
    {
        return SlotSelect::kSaveFailed; // the edits exist only here: the slot stays open and edited
    }

    const SlotSelect result = polysynth::open::patchBankSelect(part, slot, *parts[part].patch, *patchStoreWorkspace);
    if (result == SlotSelect::kLoaded || result == SlotSelect::kCopied)
    {
        // The slot files carry no name: each slot's name is kept here while the slot is not the open one. Slot 0 is the name the
        // project stores. A slot 1..15 never named in this session (project just loaded) shows "Unnamed".
        parts[part].slotNames[previous] = parts[part].patchName;
        if (slot == 0)
        {
            parts[part].patchName = parts[part].slotNames[0].empty() ? kInitPatchName : parts[part].slotNames[0];
        }
        else if (result == SlotSelect::kLoaded)
        {
            parts[part].patchName = parts[part].slotNames[slot].empty() ? "Unnamed" : parts[part].slotNames[slot];
        }
        parts[part].patchDirty = false;
        persistSlotNames(part);
    }
    return result;
}

bool Synth::flushPatchSlot(int part)
{
    return !(parts[part].patchDirty && getPatchSlot(part) > 0) || savePatchToWorkspace(part);
}

void Synth::revertCurrentPatch(int part, const std::string& previousName)
{
    if (getPatchSlot(part) == 0)
    {
        loadWorkspacePatch(part, previousName); // as before: Workspace file, else the library by name, else Init
    }
    else
    {
        polysynth::open::patchBankRevert(part, *parts[part].patch, *patchStoreWorkspace);
        parts[part].patchName = previousName;
        parts[part].patchDirty = false;
    }
}
#endif

bool Synth::saveDirtyWorkspacePatches()
{
    bool allSaved = true;
    for (int part = 0; part < kSynthParts; part++)
    {
        if (parts[part].patchDirty && !savePatchToWorkspace(part))
        {
            allSaved = false; // still dirty, the next save tries again
        }
#ifdef FEATURE_SYNTH_OPEN
        if (!persistSlotNames(part)) // project save: the names go to the Workspace before it is copied to the project
        {
            allSaved = false; // the project copy would carry stale or no slot names: the save ends with an error, the next one tries again
        }
#endif
    }
    return allSaved;
}

void Synth::setVoiceCount(int part, int voiceCount)
{
    if (parts[part].instrument->getVoiceCount() != voiceCount)
    {
        DISABLE_AUDIO_INTERRUPTS();
        parts[part].instrument->setVoiceCount(voiceCount);
        ENABLE_AUDIO_INTERRUPTS();
    }

    assert(std::accumulate(parts.begin(),
                           parts.end(),
                           0,
                           [](int acc, const Part& part)
                           { return acc + part.instrument->getVoiceCount(); }) <= kSynthMaxVoices);
}

int Synth::getMaxVoiceCount(int part)
{
    int otherPartsVoiceCount = std::accumulate(parts.begin(),
                                               parts.end(),
                                               -parts[part].instrument->getVoiceCount(),
                                               [](int acc, const Part& part)
                                               { return acc + part.instrument->getVoiceCount(); });

    assert(parts[part].patch);
    return std::min(kSynthMaxVoices - otherPartsVoiceCount,
                    parts[part].patch->getMetadata().maxVoices);
}

void Synth::setInstrumentType(int partIdx, polysynth::InstrumentTypeId typeId)
{
    auto& part = parts[partIdx];
#ifdef FEATURE_PLUG_HOST
    // A plugin engine that is not installed is installed now (plug_synth.cpp, "Loading <name>"): the engines of the other parts
    // stay, this part's old engine goes only when nothing else frees room. When it cannot be loaded the part gets its placeholder
    // (silent, "Missing: <name>"). A patch made while the engine was missing is made again once the engine is there.
    {
        int want[kSynthParts], keep = static_cast<int>(part.instrumentTypeId), n = 0;
        want[n++] = static_cast<int>(typeId);
        for (int p = 0; p < kSynthParts; p++)
        {
            const int t = static_cast<int>(parts[p].instrumentTypeId);
            if (p != partIdx && parts[p].patch && (plug::synthLoaded(t) || !plug::synthIsPlugin(t)))
            {
                want[n++] = t; // pinned, never loaded here: a part whose engine is missing stays missing
            }
        }
        plug::synthEnsure(want, n, &keep, part.patch ? 1 : 0);
    }
    const bool stale = part.patch && polysynth::open::patchStale(*part.patch);
    if (part.instrumentTypeId != typeId || stale)
#else
    if (part.instrumentTypeId != typeId)
#endif
    {
        part.instrumentTypeId = typeId;
        setPatch(partIdx, patchStore->createPatch(*typeId), kInitPatchName);

        if (part.instrument->getVoiceCount() > getMaxVoiceCount(partIdx))
        {
            setVoiceCount(partIdx, getMaxVoiceCount(partIdx));
        }

        DISABLE_AUDIO_INTERRUPTS();
        part.instrument->assignPatch(part.patch);
        ENABLE_AUDIO_INTERRUPTS();
#ifdef FEATURE_SYNTH_OPEN
        // The new engine starts on slot 0 (the Init patch, or the slot 0 backup when the editor restores it). The slot files 1..15 of
        // the old engine stay on disk and in the project with their names, they are ignored (and cannot be opened) until the part is
        // switched back to their engine.
        reloadPatchBank(partIdx, true);
#endif
    }
}

void Synth::setPatch(int part, std::shared_ptr<polysynth::Patch> patch, std::string name)
{
#ifdef FEATURE_SYNTH_OPEN
    // The audio callback (Patch FX) never reads parts[].patch: it gets a plain pointer, swapped in the same
    // audio-interrupt-free window as the shared_ptr.
    polysynth::Patch* const raw = patch.get();
    DISABLE_AUDIO_INTERRUPTS();
    parts[part].patch = std::move(patch); // may free the old patch, the pointer below must not outlive it
    polysynth::open::patchBankSetLive(part, raw);
    ENABLE_AUDIO_INTERRUPTS();
#else
    parts[part].patch = std::move(patch);
#endif
    parts[part].patchName = std::move(name);

    //    updateCCDefaults(part);
}

#ifdef FEATURE_SYNTH_OPEN
std::string Synth::getInstrumentTypeName(int typeIdx)
{
    return polysynth::open::engineAt(typeIdx).def->name;
}

int Synth::getInstrumentTypeIdx(int part)
{
    for (int i = 0; i < synthTypeCount(); i++)
    {
        if (synthTypeAt(i) == parts[part].instrumentTypeId)
        {
            return i;
        }
    }
    return -1; // a missing engine's placeholder
}
#else
std::string Synth::getInstrumentTypeName(int typeIdx)
{
    return synth->getInstrumentName(*kSynthTypes.at(typeIdx));
}

int Synth::getInstrumentTypeIdx(int part)
{
    auto it = std::ranges::find(kSynthTypes, parts[part].instrumentTypeId);
    assert(it != kSynthTypes.end());

    return std::distance(kSynthTypes.begin(), it);
}
#endif

int Synth::getSend(int part, int sendIdx)
{
    return midiAdapter->getTrackedCCValue(part, kFirstSendCC + sendIdx) * kSendMaxValue /
           polysynth::MidiEvent::Data::ControlChange::kMaxValue;
}

polysynth::Patch& Synth::getPatch(int part)
{
    return *parts[part].patch;
}

std::string Synth::getPatchName(int part)
{
    return parts[part].patchName;
}

std::string Synth::getSynthName(int part)
{
    assert(part < kSynthParts);
    std::string synthName = parts[part].instrument->getVoiceCount() > 0
                              ? synth->getInstrumentName(*parts[part].instrumentTypeId)
                              : "Off";
    return fmt::format("Synth {} ({})", part + 1, synthName);
}

std::string Synth::getParamFullName(int part, int paramId)
{
    const auto& metadata = parts[part].patch->getMetadata();
    const auto& paramName = !metadata.params[paramId].displayName.empty()
                              ? metadata.params[paramId].displayName
                              : "Empty";

    for (const auto& section : metadata.layoutFlat.sections)
    {
        for (const auto& element : section.elements)
        {
            if (element.id == paramId &&
                element.type ==
                  polysynth::Patch::Metadata::EditorLayout::EditorElement::Type::kParam)
            {
                return (section.shortName.empty() ? section.name : section.shortName) + ' ' +
                       paramName;
            }
        }
        for (const auto& subsection : section.subsections)
        {
            for (const auto& element : subsection.elements)
            {
                if (element.id == paramId &&
                    element.type ==
                      polysynth::Patch::Metadata::EditorLayout::EditorElement::Type::kParam)
                {
                    return section.name + '>' + subsection.name + ' ' + paramName;
                }
            }
        }
    }

    return paramName;
}

const char* Synth::getMacroKnobName(int macroId)
{
    return macroId < std::ssize(kMacroKnobNames) ? kMacroKnobNames[macroId] : nullptr;
}

void Synth::updateToSettings(const strMtValues& settings, bool resetAllVoices)
{
    if (resetAllVoices)
    {
#ifdef FEATURE_SYNTH_OPEN
        // project load: no voice and no governor state from the previous project
        DISABLE_AUDIO_INTERRUPTS();
        polysynth::open::resetOnProjectLoad();
        ENABLE_AUDIO_INTERRUPTS();
#endif
        for (int part = 0; part < kSynthParts; part++)
        {
            setVoiceCount(part, 0);
        }
    }

#ifdef FEATURE_PLUG_HOST
    // Every plugin engine the project needs is installed before any part changes (plug_synth.cpp): only the project's engines are
    // kept for each other, the ones the parts play now go first when the bank is full. With 3 parts the worst 3 engines and the
    // one being picked fit the bank together (plug_bank_test), an engine that still does not fit stays "Missing: <name>".
    {
        int want[kSynthParts], keep[kSynthParts], nk = 0;
        for (int part = 0; part < kSynthParts; part++)
        {
            SynthPartSettings ps = settings.synthPart[part];
            validateSynthPartSettings(part, ps);
            want[part] = polysynth::open::typeForStored(ps.synthType);
            if (parts[part].patch)
            {
                keep[nk++] = static_cast<int>(parts[part].instrumentTypeId);
            }
        }
        plug::synthEnsure(want, kSynthParts, keep, nk);
    }
#endif
    for (int part = 0; part < kSynthParts; part++)
    {
        // The project is validated only after this (loadProjectValues), but kSynthTypes.at() aborts
        // on a bad index: SYNTH_OPEN has no PERC, so a part saved as PERC (or Shaping/Chip) must
        // fall back to the part defaults here as well.
        SynthPartSettings partSettings = settings.synthPart[part];
        validateSynthPartSettings(part, partSettings);
        setVoiceCount(part, partSettings.voiceCount);
#ifdef FEATURE_SYNTH_OPEN
        // a missing engine (a plugin that is not installed) gets its placeholder: silent, the number and patches stay
        setInstrumentType(part, static_cast<polysynth::InstrumentTypeId>(polysynth::open::typeForStored(partSettings.synthType)));
#else
        setInstrumentType(part, kSynthTypes.at(storedToUi(partSettings.synthType)));
#endif
        parts[part].patchName = partSettings.patchName;
    }
}

#endif

void Synth::initSynthPartSettings(int part, SynthPartSettings& settings)
{
    settings = kDefaultSettings.at(part);
#ifdef FEATURE_SYNTH_OPEN
    // the default engine of this part may not be in this build: first engine that is
    if (!isStoredTypeValid(settings.synthType))
    {
        settings.synthType = uiToStored(0);
    }
#endif

    initSynthPartMacrosValues(part, settings);

    snprintf(settings.patchName, std::size(settings.patchName), "%s", kInitPatchName);
}

void Synth::initSynthPartMacrosValues(int part, SynthPartSettings& settings)
{
    for (int macroIdx = 0; macroIdx < kSynthMacrosCount; macroIdx++)
    {
        initSynthPartMacroValue(part, macroIdx, settings);
    }
}

void Synth::initSynthPartMacroValue(int part, int macroIdx, SynthPartSettings& settings)
{
    const polysynth::Patch::Metadata& metadata = getPatch(part).getMetadata();
    settings.macroValue[macroIdx] = metadata.macros[macroIdx].bipolar ? kCCMidValue : kCCMinValue;
}

void Synth::validateSynthPartSettings(int part, SynthPartSettings& settings)
{

    bool anyMacroValueIsOurRange = false;
    for (auto macroValue : settings.macroValue)
    {
        if (isOutOfRange<int>(macroValue, 0, kCCMaxValue))
        {
            anyMacroValueIsOurRange = true;
            break;
        }
    }

    if (!isStoredTypeValid(settings.synthType) ||
        isOutOfRange<int>(settings.voiceCount, 0, kSynthMaxVoices) ||
        isOutOfRange<float>(settings.gainDb, kGainSilence, kGainMax) ||
        isOutOfRange<float>(settings.panning, -kPanningMax, kPanningMax) ||
        isOutOfRange<float>(settings.reverbSend, 0, kSendMaxValue) ||
        isOutOfRange<float>(settings.delaySend, 0, kSendMaxValue) || anyMacroValueIsOurRange)
    {
        initSynthPartSettings(part, settings);
    }
}

void Synth::triggerTestNote(int part, int note, bool noteOn)
{
    DISABLE_AUDIO_INTERRUPTS();
    if (noteOn)
    {
        midiAdapter->sendNoteOn(note, 100, part + 1);
    }
    else
    {
        midiAdapter->sendNoteOff(note, 100, part + 1);
    }

    ENABLE_AUDIO_INTERRUPTS();
}

#ifdef FEATURE_SYNTH_OPEN
namespace
{
// The card operations of the bank copy (open_bankcopy.h). A file is copied to <dst>.tmp and replaces the destination only when it is
// complete, so an SD error leaves the destination file as it was.
struct BankCopyOps
{
    FileSystem* fs;

    polysynth::FileSystem::Presence stat(const std::string& path) { return FileSystemAdapter::statPath(path.c_str()); }
    bool copyFile(const std::string& src, const std::string& dst)
    {
        const std::string tmp = dst + ".tmp";
        if (!cFileTransfer::copyFile(fs, src.c_str(), tmp.c_str())) // it deletes tmp itself when it fails
        {
            return false;
        }
        if (!FileSystemAdapter::replaceFile(fs, tmp.c_str(), dst.c_str()))
        {
            fs->remove(tmp.c_str());
            return false;
        }
        return true;
    }
    bool remove(const std::string& path) { return fs->remove(path.c_str()); }
};
} // namespace
#endif

bool Synth::copyWorkspacePatches(const char* sourceProjectPath, const char* destinationProjectPath, bool projectOpen)
{
#ifdef FEATURE_SYNTH_OPEN
    // The bank of each part: slot 0 is <P>.patch (all stock firmware knows), slot n > 0 is <P>-n.patch, then the slot 0 backup of an
    // engine change (<P>-b.patch) and the slot names (<P>.names). A file the source has replaces the destination's whole or not at all.
    // Project open (project folder -> Workspace) then removes the Workspace slot files that the project does not have (a slot never survives
    // from the previous project) once every copy worked. Project save (Workspace -> project folder) never removes anything in Projects.
    const std::string srcDir = sourceProjectPath;
    const std::string dstDir = destinationProjectPath;
    BankCopyOps ops{ filesystem };
    bool allCopied = true;
    for (int part = 0; part < kSynthParts; part++)
    {
        allCopied = polysynth::open::copyPartBank(ops, part, srcDir, dstDir, projectOpen) && allCopied;
    }
    return allCopied;
#else
    for (int part = 0; part < kSynthParts; part++)
    {
        std::string srcFile =
          fmt::format("{}\\{}{}", sourceProjectPath, part, polysynth::PatchStore::kFileExtension);
        std::string dstFile = fmt::format(
          "{}\\{}{}", destinationProjectPath, part, polysynth::PatchStore::kFileExtension);
        cFileTransfer::copyFile(filesystem, srcFile.c_str(), dstFile.c_str());
    }
    return true;
#endif
}

bool Synth::isAnyPatchDirty()
{
    for (int part = 0; part < kSynthParts; part++)
    {
        if (parts[part].patchDirty)
        {
            return true;
        }
    }
    return false;
}

#endif
