// Synth.cpp, included where this block was: the patch slot bank of the open synth (FEATURE_SYNTH_OPEN, original Tracker)
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
    polysynth::open::patchBankSetMorphTarget(part, names->morph);         // -1 (no m= line): the default
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
    names->morph = int8_t(getMorphTarget(part));
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

// The name of slot 0..15 of the part (the open slot has the live name, the others the names the part keeps), for the Synth play page
const char* Synth::getSlotName(int part, int slot)
{
    return slot == getPatchSlot(part) ? parts[part].patchName.c_str() : parts[part].slotNames[slot].c_str();
}

bool Synth::applyPatchBankSlot(int part, int slot)
{
    return part >= 0 && part < kSynthParts && polysynth::open::patchBankApply(part, slot);
}

bool Synth::applyPatchMorph(int part, int slot, int amount)
{
    return part >= 0 && part < kSynthParts && polysynth::open::patchBankMorph(part, slot, amount);
}

void Synth::setMorphTarget(int part, int slot)
{
    if (part < 0 || part >= kSynthParts || slot == getMorphTarget(part))
    {
        return;
    }
    polysynth::open::patchBankSetMorphTarget(part, slot);
    persistSlotNames(part); // a failed write is tried again by the next project save (saveDirtyWorkspacePatches)
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

void Synth::setModSlot(int part, int slot, const polysynth::open::ModSlot& value)
{
    assert(part >= 0 && part < kSynthParts && slot >= 0 && slot < polysynth::open::kModSlots);
    DISABLE_AUDIO_INTERRUPTS();
    polysynth::open::modSlotSet(*parts[part].patch, slot, value);
    ENABLE_AUDIO_INTERRUPTS();
    setPatchDirty(part);
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
