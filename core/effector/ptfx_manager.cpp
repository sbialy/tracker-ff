#include "ptfx_manager.h"
#include "Platform.h"
#include "TrackerImpl.h"
#include "mtAudioEngine.h"
#include "sampleEditor/sampleEditor.h"

#include <cr_section_macros.h>
#include <cstdio>
#include <cstring>
#include <strings.h>

extern uint8_t externalRamScratchBuff[];

__NOINIT(EXTERNAL_RAM) static PtfxEntry s_ptfxEntries[kMaxModularPlugins];
__NOINIT(EXTERNAL_RAM) PtfxHeader PtfxManager::s_activeHeader;
__NOINIT(EXTERNAL_RAM) char* PtfxManager::s_effectDisplayNames[kBuiltInEffectsCount + kMaxModularPlugins];

uint16_t PtfxManager::s_pluginCount = 0;
bool PtfxManager::s_scanned = false;

#ifdef FW_SE_PTFX
// The sample editor effects moved out of the image. slot = the built in list position (enSampleEditorEffect), file = /Effects/<file>
// (built in tracker-effects-os, src/ptfx). The generic scan skips these names so an effect is never listed twice.
struct MovedEffect
{
    uint8_t     slot;
    const char* file;
};
static const MovedEffect kMovedEffects[] = {
    { editorEffectAmplifier, "se_amplifier.ptfx" },
    { editorEffectOverdrive, "se_overdrive.ptfx" },
    { editorEffectDelay, "se_delay.ptfx" },
    { editorEffectBitcrusher, "se_bitcrusher.ptfx" },
    { editorEffectChorus, "se_chorus.ptfx" },
    { editorEffectFlanger, "se_flanger.ptfx" },
    { editorEffectEQ, "se_equalizer.ptfx" },
    { editorEffectLimiter, "se_limiter.ptfx" },
    { editorEffectCompresor, "se_compressor.ptfx" },
};
static constexpr uint8_t kMovedCount = sizeof(kMovedEffects) / sizeof(kMovedEffects[0]);

__NOINIT(EXTERNAL_RAM) static char s_missingNames[kMovedCount][24];
static uint16_t s_movedAvailable = 0; // bit n = kMovedEffects[n] is on the card

static const MovedEffect* findMoved(uint16_t listIdx, uint8_t* bit = nullptr)
{
    for (uint8_t i = 0; i < kMovedCount; i++)
    {
        if (kMovedEffects[i].slot == listIdx)
        {
            if (bit)
                *bit = i;
            return &kMovedEffects[i];
        }
    }
    return nullptr;
}

static bool isMovedFileName(const char* name)
{
    for (uint8_t i = 0; i < kMovedCount; i++)
    {
        if (strcasecmp(name, kMovedEffects[i].file) == 0)
            return true;
    }
    return false;
}

bool PtfxManager::isMovedSlot(uint16_t listIdx)
{
    return findMoved(listIdx) != nullptr;
}

bool PtfxManager::isMovedAvailable(uint16_t listIdx)
{
    uint8_t bit = 0;
    return findMoved(listIdx, &bit) && (s_movedAvailable & (1u << bit));
}

const char* PtfxManager::getMovedFileName(uint16_t listIdx)
{
    const MovedEffect* m = findMoved(listIdx);
    return m ? m->file : "";
}

bool PtfxManager::loadMoved(uint16_t listIdx, mtEffectModular& modularEffect)
{
    const MovedEffect* m = findMoved(listIdx);
    char filepath[64];
    if (m)
        snprintf(filepath, sizeof(filepath), "/Effects/%s", m->file);

    PtfxInitFunc         initFunc         = nullptr;
    PtfxSetParamFunc     setParamFunc     = nullptr;
    PtfxProcessBlockFunc processBlockFunc = nullptr;

    if (!m || !PtfxLoader::loadPlugin(filepath, s_activeHeader, initFunc, setParamFunc, processBlockFunc))
    {
        // no stale plugin: the next process would run the previous effect's code
        memset(&s_activeHeader, 0, sizeof(s_activeHeader));
        modularEffect.configurePlugin(s_activeHeader, nullptr, 0, PtfxLoader::getHostApi(), nullptr, nullptr, nullptr);
        return false;
    }

    modularEffect.configurePlugin(s_activeHeader,
                                  PtfxLoader::getScratchBuffer(),
                                  PtfxLoader::getMaxScratchSize(),
                                  PtfxLoader::getHostApi(),
                                  initFunc,
                                  setParamFunc,
                                  processBlockFunc);
    return true;
}
#endif

static PtfxEntry* getPluginEntry(uint16_t index)
{
    if (index >= kMaxModularPlugins)
        return nullptr;
    return &s_ptfxEntries[index];
}

__attribute__((noinline)) void PtfxManager::scanEffects(bool force)
{
    if (s_scanned && !force)
        return;

    if (!s_scanned)
    {
        memset(&s_activeHeader, 0, sizeof(s_activeHeader));
    }
    s_pluginCount = 0;
    memset(s_effectDisplayNames, 0, sizeof(s_effectDisplayNames));

    // Initialize built-in effect names (0 .. 16)
    for (uint16_t i = 0; i < kBuiltInEffectsCount; i++)
    {
        s_effectDisplayNames[i] = effectNamesLabels[i];
    }

#ifdef FW_SE_PTFX
    s_movedAvailable = 0;
    for (uint8_t i = 0; i < kMovedCount; i++)
    {
        snprintf(s_missingNames[i], sizeof(s_missingNames[i]), "Missing: %s", effectNamesLabels[kMovedEffects[i].slot]);
        s_effectDisplayNames[kMovedEffects[i].slot] = s_missingNames[i];
    }
#endif

    TrackerImpl* tracker = TrackerImpl::getInstance();
    if (!tracker)
        return;
    Platform* platform = tracker->getPlatform();
    if (!platform)
        return;
    FileSystem* fs = platform->getFileSystem();
    if (!fs || !fs->isSdCardInserted())
        return;

    if (!fs->exists("/Effects"))
    {
        fs->mkdir(0, "/Effects");
        s_scanned = true;
        return;
    }

    // Use externalRamScratchBuff temporarily during scan to avoid static RAM overhead
    engine.getSdPlayer()->stop();
    char** scanFileList = reinterpret_cast<char**>(externalRamScratchBuff);
    char*  scanNamesBuffer = reinterpret_cast<char*>(externalRamScratchBuff + kMaxModularPlugins * sizeof(char*));
    int    scanNamesBufferSize = kMaxModularPlugins * 48;
    int    filesCount = kMaxModularPlugins;

    fs->listDirContents(
      "/Effects",
      FileSystem::DirContentType::kFiles,
      [](const char* name) {
          size_t len = strlen(name);
          return len > 5 && strcasecmp(name + len - 5, ".ptfx") == 0;
      },
      scanNamesBuffer,
      scanNamesBufferSize,
      48,
      filesCount,
      scanFileList);

    for (int i = 0; i < filesCount && s_pluginCount < kMaxModularPlugins; i++)
    {
#ifdef FW_SE_PTFX
        if (isMovedFileName(scanFileList[i]))
        {
            // a built in slot: listed under its own name, and only when the file has a valid header
            char movedPath[64];
            snprintf(movedPath, sizeof(movedPath), "/Effects/%s", scanFileList[i]);
            PtfxHeader movedHeader;
            if (PtfxLoader::readHeader(movedPath, movedHeader))
            {
                for (uint8_t k = 0; k < kMovedCount; k++)
                {
                    if (strcasecmp(scanFileList[i], kMovedEffects[k].file) == 0)
                    {
                        s_movedAvailable |= (1u << k);
                        s_effectDisplayNames[kMovedEffects[k].slot] = effectNamesLabels[kMovedEffects[k].slot];
                    }
                }
            }
            continue;
        }
#endif
        char filepath[64];
        snprintf(filepath, sizeof(filepath), "/Effects/%s", scanFileList[i]);

        PtfxHeader tempHeader;
        if (PtfxLoader::readHeader(filepath, tempHeader))
        {
#ifndef PLATFORM_IMX
            if (tempHeader.flags & PTFX_FLAG_IMX_ONLY)
                continue;
#endif
            PtfxEntry* entry = getPluginEntry(s_pluginCount);
            if (!entry)
                break;

            strncpy(entry->filename, scanFileList[i], sizeof(entry->filename) - 1);
            entry->filename[sizeof(entry->filename) - 1] = '\0';
            entry->flags = tempHeader.flags;

            if (tempHeader.display_name[0] != '\0')
            {
                strncpy(entry->displayName, tempHeader.display_name, sizeof(entry->displayName) - 1);
            }
            else
            {
                // Fallback to filename without extension
                strncpy(entry->displayName, entry->filename, sizeof(entry->displayName) - 1);
                size_t nameLen = strlen(entry->displayName);
                if (nameLen > 5 && strcasecmp(entry->displayName + nameLen - 5, ".ptfx") == 0)
                {
                    entry->displayName[nameLen - 5] = '\0';
                }
            }
            entry->displayName[sizeof(entry->displayName) - 1] = '\0';

            s_effectDisplayNames[kBuiltInEffectsCount + s_pluginCount] = entry->displayName;
            s_pluginCount++;
        }
    }

    s_scanned = true;
}

uint16_t PtfxManager::getPluginCount()
{
    return s_pluginCount;
}

const PtfxEntry* PtfxManager::getPlugin(uint16_t index)
{
    return getPluginEntry(index);
}

const char* PtfxManager::getPluginDisplayName(uint16_t index)
{
    PtfxEntry* entry = getPluginEntry(index);
    if (!entry)
        return "";
    return entry->displayName;
}

const PtfxHeader& PtfxManager::getActiveHeader()
{
    return s_activeHeader;
}

bool PtfxManager::loadPlugin(uint16_t index, mtEffectModular& modularEffect)
{
    if (index >= s_pluginCount)
        return false;

    PtfxEntry* entry = getPluginEntry(index);
    if (!entry)
        return false;

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/Effects/%s", entry->filename);

    PtfxInitFunc         initFunc         = nullptr;
    PtfxSetParamFunc     setParamFunc     = nullptr;
    PtfxProcessBlockFunc processBlockFunc = nullptr;

    bool loaded = PtfxLoader::loadPlugin(
      filepath, s_activeHeader, initFunc, setParamFunc, processBlockFunc);

    if (!loaded)
        return false;

    modularEffect.configurePlugin(s_activeHeader,
                                 PtfxLoader::getScratchBuffer(),
                                 PtfxLoader::getMaxScratchSize(),
                                 PtfxLoader::getHostApi(),
                                 initFunc,
                                 setParamFunc,
                                 processBlockFunc);

    return true;
}

char** PtfxManager::getEffectListNames()
{
    return s_effectDisplayNames;
}

uint16_t PtfxManager::getTotalEffectListCount()
{
    return kBuiltInEffectsCount + s_pluginCount;
}
