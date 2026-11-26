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
#if defined(FEATURE_PLUG_HOST) && defined(FEATURE_SYNTH_OPEN)   // synth engine plugins (MK66 bank), not the insert host
#include "plug_host.h"
#endif
#include "mtStructs.h"
#if defined(PLATFORM_IMX)
#include "cr_section_macros.h"
#endif
#include <numeric>
#if defined(FEATURE_SYNTH_PATCHFX) && !defined(FEATURE_SYNTH_OPEN)
#include "../../synth-open/open_slotnames.h" // the <P>.names codec of the MK66 slots (plain C++, header only)
#include "SynthVaryRule.h"                      // the MK66 Vary rule per param (host_smoke_test.cpp checks it against patchVary)
#include <algorithm>
#include <cmath>
#include <cstring>
#endif
#ifdef FEATURE_SYNTH_OPEN
#include "open_bankcopy.h"
#include "open_load.h"
#else
#include "../../synth-open/plug_load.h" // the card scan of the stock list and the IMX slot size (the IMX core has no synth-open include path)
#include "engines/PlugSynthVoice.h"
#include <memory>
#include <new>
#include <type_traits>
#if defined(PLATFORM_IMX) && defined(FEATURE_PLUG_HOST)
#include "../../synth-open/plug_logo.h" // card logos of the plugin engines on the stock list (IMX port P7)
#include "display.h"
#endif
#endif

#include "Synth_patchfx_bank.inc"
#include "Synth_ff.inc" // feature hooks of the synth wrapper (patch slots, Patch FX, project load)

static const std::array kDefaultSettings = {
    SynthPartSettings{ .gainDb = Synth::kGainDefault,
                       .voiceCount = 1,
                       .synthType = 3,
                       .panning = Synth::kPanningDefault,
                       .patchName = {},
                       .reverbSend = Synth::kSendDefaultValue,
                       .delaySend = Synth::kSendDefaultValue,
                       .macroValue = { 0 },
                       .nna = 0,
                       .nnaCheck = 0 },
    SynthPartSettings{ .gainDb = Synth::kGainDefault,
                       .voiceCount = 1,
                       .synthType = 0,
                       .panning = Synth::kPanningDefault,
                       .patchName = {},
                       .reverbSend = Synth::kSendDefaultValue,
                       .delaySend = Synth::kSendDefaultValue,
                       .macroValue = { 0 },
                       .nna = 0,
                       .nnaCheck = 0 },
    SynthPartSettings{ .gainDb = Synth::kGainDefault,
                       .voiceCount = 1,
                       .synthType = 2,
                       .panning = Synth::kPanningDefault,
                       .patchName = {},
                       .reverbSend = Synth::kSendDefaultValue,
                       .delaySend = Synth::kSendDefaultValue,
                       .macroValue = { 0 },
                       .nna = 0,
                       .nnaCheck = 0 },
    SynthPartSettings{ .gainDb = -95,
                       .voiceCount = 0,
                       .synthType = 0,
                       .panning = 0,
                       .patchName = {},
                       .reverbSend = 0.0f,
                       .delaySend = 0.0f,
                       .macroValue = { 0 },
                       .nna = 0,
                       .nnaCheck = 0 },
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
    ffSynthPatchFxBoot();
    for (auto& part : parts)
    {
        int partIdx = std::distance(parts.begin(), &part);
        auto& instrument = part.instrument;

#ifdef FEATURE_SYNTH_OPEN
        part.instrumentTypeId = synthTypeAt(partIdx % synthTypeCount());
#else
        part.instrumentTypeId = kSynthTypes[partIdx % kSynthTypes.size()]; // the built-ins, as stock
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
    if (parts[part].missing)
    {
        return true; // the file is the Missing engine's patch: it stays as it is
    }
#if defined(FEATURE_SYNTH_PATCHFX)
    // the open slot's file (<P>.patch for slot 0, <P>-n.patch otherwise) with the open slot's own values, not a Patch FX slot's
    restorePatchFx(part);
    patchStoreWorkspace->savePatch(slotFileName(part, s_bank[part].current), *parts[part].patch);
    patchBankStoreLive(part, s_bank[part].current);
#else
    patchStoreWorkspace->savePatch(fmt::format("{}", part), *parts[part].patch);
#endif
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
    ffSynthPatchFxLoad(this, part);
    bool fileLoaded = true;
    if (overrideName)
    {
        parts[part].patchName = std::move(*overrideName);
    }
#ifndef FEATURE_SYNTH_OPEN
    if (parts[part].missing)
    {
        return true; // the Missing engine's patch is not read into a built-in instrument, the file stays for the engine
    }
#endif

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

#include "Synth_open_bank.inc"

#include "Synth_patchfx_imx.inc"

bool Synth::saveDirtyWorkspacePatches()
{
    bool allSaved = true;
    for (int part = 0; part < kSynthParts; part++)
    {
        if (parts[part].patchDirty && !savePatchToWorkspace(part))
        {
            allSaved = false; // still dirty, the next save tries again
        }
        if (!ffSynthSaveSlotNames(this, part))
        {
            allSaved = false; // the project copy would carry stale or no slot names: the save ends with an error, the next one tries again
        }
    }
    return allSaved;
}

// The sum of the voices of all parts: 8, and the pool budget while the A/B Synth play page has raised it (two parts of 8)
[[maybe_unused]] static int voiceSumCap()
{
#ifdef FEATURE_SYNTH_OPEN
    return std::max<int>(kSynthMaxVoices, polysynth::open::voiceBudget());
#else
    return kSynthMaxVoices;
#endif
}

// The voices a part counts with in the sum: a stored Voices above what a part may have now (5 to 8 outside the Synth play page) counts as
// that limit, as it plays
static int countedVoices(int voiceCount)
{
#ifdef FEATURE_SYNTH_OPEN
    return std::min(voiceCount, polysynth::open::partVoiceLimit());
#else
    return voiceCount;
#endif
}

void Synth::setVoiceCount(int part, int voiceCount)
{
#ifndef FEATURE_SYNTH_OPEN
    if (parts[part].missing)
    {
        voiceCount = 0;
    }
#endif
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
                           { return acc + countedVoices(part.instrument->getVoiceCount()); }) <= voiceSumCap());
}

int Synth::getMaxVoiceCount(int part)
{
    int otherPartsVoiceCount = std::accumulate(parts.begin(),
                                               parts.end(),
                                               -countedVoices(parts[part].instrument->getVoiceCount()),
                                               [](int acc, const Part& part)
                                               { return acc + countedVoices(part.instrument->getVoiceCount()); });

    assert(parts[part].patch);
#ifdef FEATURE_SYNTH_OPEN
    return std::min(voiceSumCap() - otherPartsVoiceCount,
                    polysynth::open::pageVoiceCap(parts[part].patch->getMetadata().maxVoices));
#else
    return std::min(kSynthMaxVoices - otherPartsVoiceCount,
                    parts[part].patch->getMetadata().maxVoices);
#endif
}

void Synth::setInstrumentType(int partIdx, polysynth::InstrumentTypeId typeId)
{
    auto& part = parts[partIdx];
#if defined(FEATURE_PLUG_HOST) && defined(FEATURE_SYNTH_OPEN)   // synth engine plugins (MK66 bank), not the insert host
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
#elif defined(FEATURE_SYNTH_PATCHFX)
        // the new engine starts on slot 0, the slot files of the old engine stay on disk and get no row until it comes back
        reloadPatchBank(partIdx);
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
#elif defined(FEATURE_SYNTH_PATCHFX)
    // the Patch FX (audio callback) reads the bank's plain pointer, swapped with the shared_ptr with audio off
    polysynth::Patch* const raw = patch.get();
    DISABLE_AUDIO_INTERRUPTS();
    parts[part].patch = std::move(patch);
    s_bank[part].live = raw;
    s_bank[part].applied = -1; // a new patch holds no other slot's values
    ENABLE_AUDIO_INTERRUPTS();
    forgetVary();
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
static void resetToBuiltins(synthtypes::List& list)
{
    static constexpr const char* kNames[synthtypes::kBuiltins] = { "ACD", "FAT", "VAP", "WTFM", "PERC", "DIRT", "CHIP" };
    static_assert(Synth::kSynthTypes.size() == synthtypes::kBuiltins, "the built-ins of the stock list");
    int32_t types[synthtypes::kBuiltins];
    for (int i = 0; i < synthtypes::kBuiltins; i++)
    {
        types[i] = static_cast<int32_t>(Synth::kSynthTypes[i]);
    }
    list.reset(types, kNames);
}

synthtypes::List& Synth::typeList()
{
    static synthtypes::List list;
    if (list.count() == 0)
    {
        resetToBuiltins(list);
    }
    return list;
}

namespace
{
// The names of the listed plugin engines (the list keeps pointers), their files in kCardEngineDir and stored numbers (attach on pick)
#if defined(PLATFORM_IMX)
// In SDRAM on IMX (user decision 2026-11-20, DTCM had 1,136 B free on Plus after P7 H5), each name is written before list.add keeps it
__NOINIT(EXTERNAL_RAM) char s_cardEngineNames[synthtypes::kMax - synthtypes::kBuiltins][plug::kNameLen];
#else
char s_cardEngineNames[synthtypes::kMax - synthtypes::kBuiltins][plug::kNameLen];
#endif
#if defined(PLATFORM_IMX)
// SRAM_OC on IMX (CPU only, never a DMA target): the Mini had 296 B of DTCM left after the microtuning merge, this table is 2,280 B
OCRAM_BSS_SECTION char s_cardEngineFiles[synthtypes::kMax - synthtypes::kBuiltins][sizeof(plug::IndexEntry::file)];
#else
char s_cardEngineFiles[synthtypes::kMax - synthtypes::kBuiltins][sizeof(plug::IndexEntry::file)];
#endif
int16_t s_cardEngineStored[synthtypes::kMax - synthtypes::kBuiltins];
int s_cardEngines;
#if defined(PLATFORM_IMX) && defined(FEATURE_PLUG_HOST)
// Card logos of the listed plugin engines (IMX port P7, synth-open/plug_logo.h, the MK66 plug_synth.cpp rules): the free RAM_G window
// 128,780..150,000 is a cache of 32 list images and 2 editor images, keyed by the engine's stored number (it survives a rescan of the
// card). A .plgi sidecar wins over a .plg trailer, an engine without a good record shows its name. Main loop only, bitmapAt only finds
// the slot. All of it in SDRAM (2,324 B with the returned bitmap): the cache, the trailer offset and probe state of every card file, one record buffer.
struct CardLogos
{
    plug::LogoCache cache;
    plug::LogoState state[synthtypes::kMax - synthtypes::kBuiltins]; // of s_cardEngineFiles[i], probed again after a rescan
    uint32_t trailerOff[synthtypes::kMax - synthtypes::kBuiltins];
    uint8_t rec[plug::kLogoRecord + 1];
    int editorStored; // the engine whose editor image the patch editor shows (bitmap id kPlugLogoFirst)
    strBitmap shown;  // what synthCardLogoAt returns a pointer to (the MK66 bitmapAt pattern), here and not in DTCM
};
// __NOINIT: no constructor may run for it (SDRAM is not up when the static constructors run)
static_assert(std::is_trivially_default_constructible_v<CardLogos>, "s_logos must not need a constructor");
__NOINIT(EXTERNAL_RAM) CardLogos s_logos;
bool s_logosUp; // s_logos.cache is valid: reset by the first scan (SDRAM is not cleared at boot)
static_assert(kPlugLogoFirst <= 255, "the editor logo id fits the label's uint8_t bitmapIndex");
#endif
#if defined(PLATFORM_IMX)
// What each code slot of the IMX host plays (P7 H5): the stored number and type of its engine, -1 for none. An engine stays loaded
// until a pick needs its slot, so a rescan of the card (project load, the Model list) finds it attached still.
struct SlotUse
{
    int stored = -1;
    int32_t type = -1;
};
SlotUse s_slotUse[polysynth::plugimx::kSlots];
// Bytes of a whole .plg read for a load: the slot (image + bss) plus header, manifest and rebase table
constexpr uint32_t kPlugFileMax = plug::kImxSynthSlotBytes + 16384;
#endif

// FileSystem for scanIndex (plug_index.h), as plug_host.cpp's ScanFs on the MK66
struct CardScanFs
{
    FileSystem* fs;
    void list(const char* dir, bool (*filter)(const char*), char* names, int namesSize, int maxNameLen, int& count, char** list)
    {
        fs->listDirContents(dir, FileSystem::kFiles, filter, names, namesSize, maxNameLen, count, list);
    }
    int read(const char* p, void* buf, int cap)
    {
        auto [ok, f] = fs->openFile(p, FileSystem::FileOpenMode::kRead);
        if (!ok)
        {
            return -1;
        }
        const int n = int(fs->readFile(buf, size_t(cap), f));
        fs->closeFile(f);
        return n;
    }
};
}   // namespace

int Synth::scanCardEngines()
{
    synthtypes::List& list = typeList();
    resetToBuiltins(list);
    if (!filesystem)
    {
        return 0;
    }
    // The scan's buffers live on the heap for the scan only (about 19 KB): the index of up to kMax engines and the directory listing
    struct ScanBuf
    {
        plug::IndexEntry index[synthtypes::kMax];
        char names[plug::kScanMax * 40];
        char* nameList[plug::kScanMax];
    };
    std::unique_ptr<ScanBuf> b(new (std::nothrow) ScanBuf);
    if (!b)
    {
        return 0;
    }
    CardScanFs fs{ filesystem };
    plug::ScanStats st;
#if defined(PLATFORM_IMX) && defined(FEATURE_PLUG_HOST)
    if (!s_logosUp)
    {
        s_logos.cache.reset();
        s_logos.editorStored = -1;
        s_logosUp = true;
    }
#endif
    const int n = plug::scanIndex(fs, kCardEngineDir, b->index, synthtypes::kMax, b->names, b->nameList, st, PLUG_KIND_SYNTH);
    int added = 0;
    for (int i = 0; i < n && added < synthtypes::kMax - synthtypes::kBuiltins; i++)
    {
        const plug::IndexEntry& e = b->index[i];
        if (e.synthStored < 0)
        {
            continue; // built for a host without the synth manifest: it has no number to list it under
        }
        snprintf(s_cardEngineNames[added], sizeof(s_cardEngineNames[added]), "%s", e.name);
        // Attached when its code is in a slot already (IMX, P7 H5: an engine loads when a part picks it, attachCardEngine). chip.plg
        // (stored 6) and a taken number or type are refused by the list
        bool attached = false;
#if defined(PLATFORM_IMX)
        for (const SlotUse& u : s_slotUse)
        {
            attached |= u.stored == e.synthStored && u.type == e.synthType;
        }
#endif
        if (list.add(e.synthStored, e.synthType, s_cardEngineNames[added], attached))
        {
            snprintf(s_cardEngineFiles[added], sizeof(s_cardEngineFiles[added]), "%s", e.file);
            s_cardEngineStored[added] = int16_t(e.synthStored);
#if defined(PLATFORM_IMX) && defined(FEATURE_PLUG_HOST)
            s_logos.trailerOff[added] = e.logoOff;
            s_logos.state[added] = plug::LogoState{};
#endif
            added++;
        }
    }
    s_cardEngines = added;
    return added;
}

bool Synth::attachCardEngine([[maybe_unused]] int part, int stored)
{
    synthtypes::List& list = typeList();
    const int ui = list.uiForStored(stored);
    if (ui < 0 || list.at(ui).builtin >= 0 || list.at(ui).attached)
    {
        return ui >= 0 && list.at(ui).attached;
    }
#if defined(PLATFORM_IMX)
    const int32_t type = list.at(ui).typeId;
    int file = -1;
    for (int i = 0; i < s_cardEngines; i++)
    {
        if (s_cardEngineStored[i] == stored)
        {
            file = i;
        }
    }
    if (file < 0 || !filesystem)
    {
        return false;
    }
    // A slot no other part plays: 3 slots, at most 2 other parts, so there is always one. An empty slot first.
    int slot = -1;
    for (int s = 0; s < polysynth::plugimx::kSlots && slot < 0; s++)
    {
        slot = s_slotUse[s].stored < 0 ? s : -1;
    }
    for (int s = 0; s < polysynth::plugimx::kSlots && slot < 0; s++)
    {
        bool used = false;
        for (int p = 0; p < kSynthParts; p++)
        {
            used |= p != part && !parts[p].missing && static_cast<int32_t>(parts[p].instrumentTypeId) == s_slotUse[s].type;
        }
        slot = used ? -1 : s;
    }
    if (slot < 0)
    {
        return false;
    }
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[kPlugFileMax]);
    if (!buf)
    {
        return false;
    }
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", kCardEngineDir, s_cardEngineFiles[file]);
    auto [ok, f] = filesystem->openFile(path, FileSystem::FileOpenMode::kRead);
    if (!ok)
    {
        return false;
    }
    const int n = int(filesystem->readFile(buf.get(), kPlugFileMax, f));
    filesystem->closeFile(f);
    if (n <= 0)
    {
        return false;
    }
    // the slot's old engine goes: nothing of it may run while its code is overwritten (its parts, if any, are Missing from here)
    if (s_slotUse[slot].stored >= 0)
    {
        list.setAttached(s_slotUse[slot].stored, false);
    }
    s_slotUse[slot] = SlotUse{};
    DISABLE_AUDIO_INTERRUPTS();
    const int r = polysynth::plugimx::attachFile(slot, buf.get(), uint32_t(n));
    ENABLE_AUDIO_INTERRUPTS();
    if (r != type)
    {
        return false; // a load or attach error, or the file says another type than its manifest: Missing
    }
    s_slotUse[slot] = SlotUse{ stored, type };
    list.setAttached(stored, true);
    return true;
#else
    return false;
#endif
}

#if defined(PLATFORM_IMX)
bool Synth::plugChordPara(int part, uint8_t chord)
{
    if (part < 0 || part >= kSynthParts || parts[part].missing || !parts[part].patch)
    {
        return false;
    }
    const int info = polysynth::plugimx::chordInfo(static_cast<int>(parts[part].instrumentTypeId));
    if (info < 0 || chord > uint8_t(info >> 24))
    {
        return false; // no PLUG_EXT_CHORD engine on the part, or a chord past its Chord enum: plain notes (open_synth.cpp chordPara)
    }
    const auto mode = parts[part].patch->getParamValue((info >> 8) & 0xFF);
    return mode && *mode >= 0.5f;
}

void Synth::plugNoteChord(int part, uint8_t note, uint8_t chord, int8_t strum)
{
    polysynth::plugimx::noteChord(part, note, chord, strum);
}

bool Synth::plugPart(int part) const
{
    if (part < 0 || part >= kSynthParts || parts[part].missing)
    {
        return false;
    }
    for (const SlotUse& u : s_slotUse)
    {
        if (u.stored >= 0 && u.type == static_cast<int32_t>(parts[part].instrumentTypeId))
        {
            return true;
        }
    }
    return false;
}
#endif

int Synth::readCardText(const char* path, char* buf, int cap)
{
    if (!filesystem || cap <= 0)
    {
        return -1;
    }
    auto [ok, f] = filesystem->openFile(path, FileSystem::FileOpenMode::kRead);
    if (!ok)
    {
        return -1;
    }
    const int n = int(filesystem->readFile(buf, size_t(cap - 1), f));
    filesystem->closeFile(f);
    buf[n < 0 ? 0 : n] = 0;
    return n;
}

bool Synth::writeCardText(const char* path, const char* text, int n)
{
    if (!filesystem || n < 0)
    {
        return false;
    }
    auto [ok, f] = filesystem->openFile(path, FileSystem::FileOpenMode::kWriteCreateNew);
    if (!ok)
    {
        return false;
    }
    const bool written = n == 0 || filesystem->writeFile(text, size_t(n), f) == int32_t(n);
    filesystem->closeFile(f);
    return written;
}

std::string Synth::getInstrumentTypeName(int typeIdx)
{
    const synthtypes::Entry& e = typeList().at(typeIdx);
    return e.builtin >= 0 ? synth->getInstrumentName(*kSynthTypes.at(e.builtin)) : std::string(e.name);
}

int Synth::getInstrumentTypeIdx(int part)
{
    if (parts[part].missing)
    {
        return -1; // a Missing part plays nothing, it stands on no row of the list
    }
    for (int i = 0; i < synthTypeCount(); i++)
    {
        if (synthTypeAt(i) == parts[part].instrumentTypeId)
        {
            return i;
        }
    }
    return -1;
}

void Synth::setPartEngine(int part, int stored)
{
    attachCardEngine(part, stored); // a plugin engine loads into a code slot when a part picks it (IMX, P7 H5)
    const int ui = storedToUi(stored);
    const bool missing = storedIsMissing(stored);
    // A Missing part keeps a built-in instrument with no voices (the lib needs a registered type), its stored number stays in the project
    parts[part].missing = missing;
    setInstrumentType(part, missing ? kSynthTypes[0] : synthTypeAt(ui));
    if (missing)
    {
        setVoiceCount(part, 0);
    }
}

#if defined(PLATFORM_IMX) && defined(FEATURE_PLUG_HOST)
namespace
{
// FileSystem for logoFind (plug_logo.h), as plug_synth.cpp's LogoFs on the MK66
struct CardLogoFs
{
    FileSystem* fs;
    int readAt(const char* p, uint32_t off, void* b, int cap)
    {
        auto [ok, f] = fs->openFile(p, FileSystem::FileOpenMode::kRead);
        if (!ok)
        {
            return -1;
        }
        const int n = !off || fs->seekFile(off, f) ? int(fs->readFile(b, size_t(cap), f)) : -1;
        fs->closeFile(f);
        return n;
    }
};

struct CardLogoRam
{
    DisplayDriver* dd;
    void write(const uint8_t* p, uint32_t n, uint32_t at) { dd->writeDataRAMG(p, n, at); }
};

int cardFileOfStored(int stored)
{
    for (int i = 0; i < s_cardEngines; i++)
    {
        if (s_cardEngineStored[i] == stored)
        {
            return i;
        }
    }
    return -1;
}
}   // namespace

int Synth::cardLogoSlot(int stored, bool editor)
{
    const int file = cardFileOfStored(stored);
    DisplayDriver* const dd = display.getDisplayDriver();
    if (!s_logosUp || file < 0 || !filesystem || !dd)
    {
        return -1;
    }
    CardLogoFs fs{ filesystem };
    CardLogoRam ram{ dd };
    return plug::logoLoad(s_logos.cache, stored, editor, fs, ram, [] { return uint32_t(0); }, kCardEngineDir, s_cardEngineFiles[file],
                          s_logos.trailerOff[file], s_logos.rec, s_logos.state[file]);
}

int Synth::cardLogoId(int ui, bool editor)
{
    const synthtypes::Entry& e = typeList().at(ui);
    const int file = e.builtin >= 0 ? -1 : cardFileOfStored(e.stored);
    if (file < 0 || !s_logosUp)
    {
        return -1;
    }
    if (editor)
    {
        if (cardLogoSlot(e.stored, true) < 0)
        {
            return -1;
        }
        s_logos.editorStored = e.stored; // one editor image on screen: its id names the engine the patch editor opened last
        return kPlugLogoFirst;
    }
    // a list image not loaded yet comes with cardLogoPrefetch, without one the list draws the name
    return s_logos.state[file].probed && s_logos.state[file].src == plug::kLogoNone ? -1 : plug::logoListId(kPlugLogoFirst, e.stored);
}

void Synth::cardLogoPrefetch(const int* uis, int n)
{
    int stored[plug::kLogoMiniSlots / 2];
    int m = 0;
    for (int i = 0; i < n && m < plug::kLogoMiniSlots / 2; i++)
    {
        const synthtypes::Entry& e = typeList().at(uis[i]);
        if (e.builtin < 0)
        {
            stored[m++] = e.stored;
        }
    }
    plug::logoPrefetch(s_logos.cache, stored, m, [this](int s) { cardLogoSlot(s, false); });
}

int Synth::cardLogoResident(int stored, bool editor)
{
    return s_logosUp ? s_logos.cache.find(stored, editor) : -1;
}

// bitmapAt (plug_host.cpp) for an id past the stock bitmaps: the list image of a stored number or the editor image, nullptr when it is
// not in RAM_G (the list draws the name)
const strBitmap* synthCardLogoAt(int id)
{
    if (!s_logosUp)
    {
        return nullptr;
    }
    bool med = false;
    const int stored = plug::logoIdEntry(kPlugLogoFirst, id, synthtypes::kStoredLast + 1, s_logos.editorStored, med);
    if (stored < 0)
    {
        return nullptr;
    }
    const int slot = s_logos.cache.find(stored, med);
    if (slot < 0)
    {
        return nullptr;
    }
    s_logos.cache.touch(slot, med); // on screen: never the next slot to be reused
    // the list image or the editor image (L2 = 17, FT81x.h)
    static const strBitmap kTpl[2] = { { nullptr, 0, plug::kLogoMiniBytes, plug::kLogoMiniW, plug::kLogoMiniH, 17, plug::kLogoMiniStride },
                                       { nullptr, 0, plug::kLogoMedBytes, plug::kLogoMedW, plug::kLogoMedH, 17, plug::kLogoMedStride } };
    strBitmap& b = s_logos.shown;
    b = kTpl[med ? 1 : 0];
    b.address = plug::LogoCache::address(slot, med);
    return &b;
}
#endif
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
    // The id can come from a macro modifier stored in a patch file.
    if (paramId < 0 || paramId >= std::ssize(metadata.params))
    {
        return "Empty";
    }
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
#ifndef FEATURE_SYNTH_OPEN
    if (resetAllVoices)
    {
        scanCardEngines(); // project load: the plugin engines of the card join the list before the parts take their numbers
    }
#endif
    if (resetAllVoices)
    {
        ffSynthProjectLoadReset(); // no voice and no governor state from the previous project
        for (int part = 0; part < kSynthParts; part++)
        {
            setVoiceCount(part, 0);
        }
    }

#if defined(FEATURE_PLUG_HOST) && defined(FEATURE_SYNTH_OPEN)   // synth engine plugins (MK66 bank), not the insert host
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
#ifndef FEATURE_SYNTH_OPEN
        attachCardEngine(part, partSettings.synthType); // the project's plugin engines load before the parts take them (IMX)
        parts[part].missing = storedIsMissing(partSettings.synthType); // before the voice count: a Missing part gets none
#endif
        setVoiceCount(part, partSettings.voiceCount);
#ifdef FEATURE_SYNTH_OPEN
        // a missing engine (a plugin that is not installed) gets its placeholder: silent, the number and patches stay
        setInstrumentType(part, static_cast<polysynth::InstrumentTypeId>(polysynth::open::typeForStored(partSettings.synthType)));
#else
        // a Missing engine (a plugin number this unit has not got, or one not attached): silent, the number and patch files stay
        setPartEngine(part, partSettings.synthType);
#endif
        parts[part].patchName = partSettings.patchName;
    }
}

#endif

void Synth::initSynthPartSettings(int part, SynthPartSettings& settings)
{
    settings = kDefaultSettings.at(part);
    ffSynthDefaultEngine(settings);

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

#include "Synth_bankcopy.inc"

bool Synth::copyWorkspacePatches(const char* sourceProjectPath, const char* destinationProjectPath, [[maybe_unused]] bool projectOpen)
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
#if defined(FEATURE_SYNTH_PATCHFX)
        // the slot files 1..15 (<P>-n.patch) go with slot 0. Project open also removes the Workspace slot files the project has not got,
        // a slot never survives from the previous project
        for (int slot = 1; slot <= kPatchSlots; slot++) // kPatchSlots: the names file <P>.names (slot names and Morph target)
        {
            const std::string src = slot < kPatchSlots ? fmt::format("{}\\{}-{}{}", sourceProjectPath, part, slot, polysynth::PatchStore::kFileExtension)
                                                       : fmt::format("{}\\{}{}", sourceProjectPath, part, polysynth::open::kSlotNamesExt);
            const std::string dst = slot < kPatchSlots ? fmt::format("{}\\{}-{}{}", destinationProjectPath, part, slot, polysynth::PatchStore::kFileExtension)
                                                       : fmt::format("{}\\{}{}", destinationProjectPath, part, polysynth::open::kSlotNamesExt);
            if (filesystem->exists(src.c_str()))
            {
                cFileTransfer::copyFile(filesystem, src.c_str(), dst.c_str());
            }
            else if (projectOpen && filesystem->exists(dst.c_str()))
            {
                filesystem->remove(dst.c_str());
            }
        }
#endif
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
