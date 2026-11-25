// Synth.cpp, included where this block was: the Patch FX of Tracker+ and Mini (FEATURE_SYNTH_PATCHFX without the open synth, IMX port P12), its slot rows and the audio callback copy
#if defined(FEATURE_SYNTH_PATCHFX) && !defined(FEATURE_SYNTH_OPEN)

// Audio callback: copies the slot's row over the live patch, only the params that differ (a copy of equal values sends nothing)
bool Synth::applyPatchBankSlot(int part, int slot)
{
    if (part < 0 || part >= kSynthParts || slot < 0 || slot >= kPatchSlots)
    {
        return false;
    }
    PatchBank& b = s_bank[part];
    polysynth::Patch* const p = b.live;
    if (p == nullptr || b.params == 0 || parts[part].missing || (slot != b.current && !(b.filled & (1u << slot))))
    {
        return false; // no row of this engine: ignored, as on the MK66
    }
    const int n = b.params;
    if (slot == b.current && b.applied < 0)
    {
        return true; // the open slot's values are in the part already
    }
    if (b.applied < 0)
    {
        rowFromPatch(*p, b.row[kPatchSlots], n); // the first Patch FX keeps the open slot's live values
    }
    const float* const row = slot == b.current ? b.row[kPatchSlots] : b.row[slot];
    // One setParamValue per param that differs and no start/finishBatchUpdate: the stock finishBatchUpdate notifies every param of
    // the patch, which re-sends the unchanged ones to the voices and changes the sound of a Patch FX that keeps a value (the MK66
    // bank changes only the values that differ, run.py --events replays exactly these changes)
    for (int i = 0; i < n; i++)
    {
        const uint32_t want = bitsOf(row[i]);
        if (want != valueBits(p->getParamValue(i)))
        {
            p->setParamValue(i, want == kUnsetBits ? polysynth::Patch::optional_paramvalue_t{} : polysynth::Patch::optional_paramvalue_t{ row[i] },
                             polysynth::Patch::ParamChangeSource::kKnob);
        }
    }
    b.applied = int8_t(slot == b.current ? -1 : slot);
    return true;
}

bool Synth::applyPatchMorph(int part, int slot, int amount)
{
    if (part < 0 || part >= kSynthParts)
    {
        return false;
    }
    PatchBank& b = s_bank[part];
    if (slot < 0)
    {
        slot = b.names.morph >= 0 && b.names.morph < kPatchSlots ? b.names.morph : kMorphTarget; // the part's target (<P>.names m=)
    }
    if (amount <= 0)
    {
        return applyPatchBankSlot(part, b.current);
    }
    if (amount >= 100)
    {
        return applyPatchBankSlot(part, slot);
    }
    polysynth::Patch* const p = b.live;
    if (slot >= kPatchSlots || p == nullptr || b.params == 0 || parts[part].missing || (slot != b.current && !(b.filled & (1u << slot))))
    {
        return false; // no row of this engine: ignored like the Patch FX
    }
    const int n = b.params;
    if (b.applied < 0)
    {
        rowFromPatch(*p, b.row[kPatchSlots], n); // A: the open slot's live values, as the Patch FX keeps them
    }
    const float* const a = b.row[kPatchSlots];
    const float* const r = slot == b.current ? b.row[kPatchSlots] : b.row[slot];
    const float t = float(amount) * 0.01f;
    const bool second = amount * 2 >= 100;
    // no batch, as applyPatchBankSlot: only the params whose value changes are sent
    for (int i = 0; i < n; i++)
    {
        const uint32_t ab = bitsOf(a[i]);
        const uint32_t bb = bitsOf(r[i]);
        uint32_t want;
        if (ab == bb || ab == kUnsetBits || bb == kUnsetBits || maskBit(b.switchMask, i))
        {
            want = second ? bb : ab; // enums and the integral params the MK66 switches (setMorphKinds)
        }
        else if (maskBit(b.roundMask, i))
        {
            want = bitsOf(std::nearbyint(a[i] + (r[i] - a[i]) * t)); // the MK66 kRound: the blend to the nearest integer
        }
        else
        {
            want = bitsOf(a[i] + (r[i] - a[i]) * t);
        }
        if (want != valueBits(p->getParamValue(i)))
        {
            float v;
            memcpy(&v, &want, sizeof(v));
            p->setParamValue(i, want == kUnsetBits ? polysynth::Patch::optional_paramvalue_t{} : polysynth::Patch::optional_paramvalue_t{ v },
                             polysynth::Patch::ParamChangeSource::kKnob);
        }
    }
    b.applied = int8_t(kMorphApplied);
    return true;
}

void Synth::restorePatchFx(int part)
{
    if (part < 0 || part >= kSynthParts || s_bank[part].applied < 0)
    {
        return;
    }
    DISABLE_AUDIO_INTERRUPTS();
    applyPatchBankSlot(part, s_bank[part].current);
    ENABLE_AUDIO_INTERRUPTS();
}

int Synth::getPatchSlot(int part)
{
    return s_bank[part].current;
}

uint32_t Synth::patchBankFilled(int part)
{
    return s_bank[part].filled;
}

int Synth::patchBankApplied(int part)
{
    return s_bank[part].applied;
}

namespace
{
// the stock knob curve as the Vary rule's (SynthVaryRule.h): kLinear, kExponential, kExponentialSteep, kPower are the open kLinear, kExp,
// kSteep, kPower
synthvary::Curve varyCurve(polysynth::Patch::Metadata::Param::Curve c)
{
    using StockCurve = polysynth::Patch::Metadata::Param::Curve;
    switch (c)
    {
        case StockCurve::kExponential: return synthvary::Curve::kExp;
        case StockCurve::kExponentialSteep: return synthvary::Curve::kSteep;
        case StockCurve::kPower: return synthvary::Curve::kPower;
        default: return synthvary::Curve::kLinear;
    }
}
} // namespace

int Synth::getVaryPercent()
{
    return s_vary.percent;
}

void Synth::setVaryPercent(int percent)
{
    s_vary.percent = uint8_t(std::clamp(percent, 5, 100));
}

void Synth::forgetVary()
{
    s_vary.valid = 0;
    s_vary.patch = nullptr;
}

// The MK66 patchVary over the stock patch: discrete are the enums (the MK66 kEnum and kOnOff), whole steps are the enums, the Morph rule's
// rounded params (VAP Tune, PERC Timbre: kSemi, kInt) and its switched "Ratio" params (WTFM kRatio). PERC Pitch and Body Wave glide as on
// the MK66 (kNote and kBodyWave are not whole there). Only the params whose value changes are sent (the undo too).
int Synth::varyPatch(int part, uint32_t entropy)
{
    if (part < 0 || part >= kSynthParts || !parts[part].patch || parts[part].missing)
    {
        return 0;
    }
    restorePatchFx(part); // the Vary moves the open slot's values
    polysynth::Patch& p = *parts[part].patch;
    const int n = int(p.getMetadata().params.size());
    if (n <= 0 || n > kBankParams)
    {
        return 0;
    }
    const PatchBank& b = s_bank[part];
    const bool kinds = b.params == n; // the Morph masks are of this engine
    s_vary.rng = synthvary::seed(s_vary.rng, entropy);
    uint32_t rng = s_vary.rng;
    if (rng == 0)
    {
        rng = 0x9E3779B9u;
    }
    const int percent = std::clamp(int(s_vary.percent), 1, 100);
    int changed = 0;
    for (int i = 0; i < n; i++)
    {
        // the metadata again for every param: a stock param with invalidatesOtherMetadata (a Sync) changes the ranges of others
        const auto& d = p.getMetadata().params[size_t(i)];
        const auto now = p.getParamValue(i);
        const uint32_t keep = valueBits(now);
        memcpy(&s_vary.undo[i], &keep, sizeof(keep));
        synthvary::Param q;
        q.lo = float(d.minValue);
        q.hi = float(d.maxValue);
        q.before = now.value_or(d.defaultValue.value_or(q.lo));
        q.curve = varyCurve(d.editCurve);
        q.discrete = d.isEnum;
        q.whole = kinds && (maskBit(b.roundMask, i) || (maskBit(b.switchMask, i) && d.displayName.rfind("Ratio", 0) == 0));
        q.skip = d.displayName == "Volume";
        float v = q.before;
        if (synthvary::draw(q, percent, rng, v))
        {
            DISABLE_AUDIO_INTERRUPTS();
            p.setParamValue(i, v, polysynth::Patch::ParamChangeSource::kKnob);
            ENABLE_AUDIO_INTERRUPTS();
            changed++;
        }
    }
    s_vary.rng ^= rng;
    s_vary.count = int16_t(n);
    s_vary.patch = &p;
    s_vary.valid = changed > 0;
    if (changed > 0)
    {
        setPatchDirty(part);
    }
    return changed;
}

bool Synth::undoVary(int part)
{
    if (part < 0 || part >= kSynthParts || !parts[part].patch || !s_vary.valid || s_vary.patch != parts[part].patch.get()
        || s_vary.count != int(parts[part].patch->getMetadata().params.size()))
    {
        return false;
    }
    restorePatchFx(part);
    polysynth::Patch& p = *parts[part].patch;
    // Passes until every value is back: a param set while a param it depends on (a Sync) still has the varied value lands elsewhere,
    // the next pass sets it again with that param restored (the stock batch would notify every param instead, see applyPatchBankSlot)
    for (int pass = 0; pass < 3; pass++)
    {
        int differ = 0;
        for (int i = 0; i < s_vary.count; i++)
        {
            const uint32_t want = bitsOf(s_vary.undo[i]);
            if (valueBits(p.getParamValue(i)) != want)
            {
                DISABLE_AUDIO_INTERRUPTS();
                p.setParamValue(i, want == kUnsetBits ? polysynth::Patch::optional_paramvalue_t{} : polysynth::Patch::optional_paramvalue_t{ s_vary.undo[i] },
                                polysynth::Patch::ParamChangeSource::kKnob);
                ENABLE_AUDIO_INTERRUPTS();
                differ++;
            }
        }
        if (differ == 0)
        {
            break;
        }
    }
    s_vary.valid = 0;
    setPatchDirty(part);
    return true;
}

void Synth::patchBankStoreLive(int part, int row)
{
    PatchBank& b = s_bank[part];
    if (b.params > 0 && parts[part].patch)
    {
        DISABLE_AUDIO_INTERRUPTS();
        rowFromPatch(*parts[part].patch, b.row[row], b.params);
        if (row < kPatchSlots)
        {
            b.filled |= 1u << row;
        }
        ENABLE_AUDIO_INTERRUPTS();
    }
}

void Synth::reloadPatchBank(int part)
{
    PatchBank& b = s_bank[part];
    DISABLE_AUDIO_INTERRUPTS();
    b.filled = 0;
    b.applied = -1;
    b.current = 0;
    b.params = 0;
    ENABLE_AUDIO_INTERRUPTS();
    forgetVary();
    if (parts[part].missing || !parts[part].patch || !patchStoreWorkspace)
    {
        return; // a Missing engine's slot files stay for the engine, nothing of them plays
    }
    const int typeId = static_cast<int>(*parts[part].instrumentTypeId);
    const int n = bankParams(*parts[part].patch);
    if (n == 0)
    {
        return;
    }
    std::unique_ptr<polysynth::Patch> scratch = patchStoreWorkspace->createPatch(typeId);
    if (!scratch)
    {
        return;
    }
    int builtin = -1;
    for (size_t k = 0; k < kSynthTypes.size(); k++)
    {
        if (static_cast<int>(*kSynthTypes[k]) == typeId)
        {
            builtin = int(k);
        }
    }
    setMorphKinds(b, *parts[part].patch, builtin, n); // params is 0 here: the audio callback does not read the kinds
    b.params = int16_t(n);
    for (int slot = 1; slot < kPatchSlots; slot++)
    {
        const std::string name = slotFileName(part, slot);
        const auto type = patchStoreWorkspace->loadPatchInstrumentType(name);
        if (!type || static_cast<int>(*type) != typeId || !patchStoreWorkspace->loadPatch(name, *scratch, false))
        {
            continue; // no file, or a slot of another engine: no row, the Patch FX to it is ignored
        }
        rowFromPatch(*scratch, b.row[slot], n); // the audio callback reads a row only once its bit is set
        DISABLE_AUDIO_INTERRUPTS();
        b.filled |= 1u << slot;
        ENABLE_AUDIO_INTERRUPTS();
    }
}

Synth::SlotSelect Synth::selectPatchSlot(int part, int slot)
{
    assert(part >= 0 && part < kSynthParts);
    PatchBank& b = s_bank[part];
    const int previous = b.current;
    if (slot == previous)
    {
        return SlotSelect::kSame;
    }
    if (slot < 0 || slot >= kPatchSlots || parts[part].missing || b.params == 0)
    {
        return SlotSelect::kError;
    }
    restorePatchFx(part);
    forgetVary(); // the undo of a Vary belongs to the slot that was open
    // a dirty slot is written when it is left, and so is one with no file yet (slot 0 of a new project): going back must find it
    const bool hasFile = patchStoreWorkspace->loadPatchInstrumentType(slotFileName(part, previous)).has_value();
    if ((parts[part].patchDirty || !hasFile) && !savePatchToWorkspace(part))
    {
        return SlotSelect::kSaveFailed;
    }
    const std::string name = slotFileName(part, slot);
    const auto type = patchStoreWorkspace->loadPatchInstrumentType(name);
    if (type && static_cast<int>(*type) != static_cast<int>(*parts[part].instrumentTypeId))
    {
        return SlotSelect::kOtherEngine;
    }
    patchBankStoreLive(part, previous); // the slot that is left keeps its values as its row
    if (previous == 0)
    {
        snprintf(mtProject.values.synthPart[part].patchName, sizeof(SynthPartSettings::patchName), "%s", parts[part].patchName.c_str());
    }
    else
    {   // the slot that is left keeps its name ("Unnamed" is no name)
        snprintf(b.names.name[previous], sizeof(b.names.name[previous]), "%s", parts[part].patchName == "Unnamed" ? "" : parts[part].patchName.c_str());
    }
    SlotSelect result = SlotSelect::kCopied;
    if (type)
    {
        if (!patchStoreWorkspace->loadPatch(name, *parts[part].patch, true))
        {
            return SlotSelect::kError;
        }
        result = SlotSelect::kLoaded;
    }
    b.current = int8_t(slot);
    if (result == SlotSelect::kCopied && !patchStoreWorkspace->savePatch(name, *parts[part].patch))
    {
        b.current = int8_t(previous);
        return SlotSelect::kError;
    }
    // the slot files carry no name: slot 0 has the name the project stores, the others the one in <P>.names ("Unnamed" when there is
    // none). A slot that starts as a copy keeps the open one's name, as on the MK66
    if (slot == 0)
    {
        parts[part].patchName = mtProject.values.synthPart[part].patchName;
    }
    else if (result == SlotSelect::kLoaded)
    {
        parts[part].patchName = b.names.name[slot][0] ? b.names.name[slot] : "Unnamed";
    }
    patchBankStoreLive(part, slot);
    parts[part].patchDirty = false;
    persistSlotNames(part); // a failed write is tried again by the next project save (saveDirtyWorkspacePatches)
    return result;
}

int Synth::getMorphTarget(int part)
{
    const int8_t m = s_bank[part].names.morph;
    return m >= 0 && m < kPatchSlots ? m : kMorphTarget;
}

void Synth::setMorphTarget(int part, int slot)
{
    if (part < 0 || part >= kSynthParts || slot < 0 || slot >= kPatchSlots || slot == getMorphTarget(part))
    {
        return;
    }
    s_bank[part].names.morph = int8_t(slot); // one byte: the audio callback reads the old or the new target
    persistSlotNames(part);                  // a failed write is tried again by the next project save
}

const char* Synth::getSlotName(int part, int slot)
{
    if (slot == s_bank[part].current)
    {
        return parts[part].patchName.c_str();
    }
    return slot == 0 ? mtProject.values.synthPart[part].patchName : s_bank[part].names.name[slot];
}

char Synth::morphKind(int part, int param)
{
    const PatchBank& b = s_bank[part];
    if (param < 0 || param >= b.params)
    {
        return '-';
    }
    return maskBit(b.switchMask, param) ? 's' : maskBit(b.roundMask, param) ? 'r' : 'l';
}

void Synth::loadSlotNames(int part)
{
    PatchBank& b = s_bank[part];
    polysynth::open::clearSlotNames(b.names); // no file: no names, the default Morph target
    b.namesBad = 0;
    const std::string path = namesFilePath(part);
    if (!filesystem || !filesystem->exists(path.c_str()))
    {
        return;
    }
    auto [ok, f] = filesystem->openFile(path.c_str(), FileSystem::FileOpenMode::kRead);
    const int n = ok ? int(filesystem->readFile(s_namesBuf, sizeof(s_namesBuf), f)) : -1;
    if (ok)
    {
        filesystem->closeFile(f);
    }
    if (n < 0)
    {
        b.namesBad = 1; // the file stays as it is until a project load reads it
        return;
    }
    polysynth::open::decodeSlotNames(s_namesBuf, size_t(n), b.names); // a damaged file gives what it can, a wrong header nothing
    b.names.name[0][0] = 0;                                           // no slot 0 backup on Tracker+ and Mini
}

bool Synth::persistSlotNames(int part)
{
    PatchBank& b = s_bank[part];
    if (!filesystem || b.namesBad)
    {
        return false; // the file there was not read: empty names must not replace it
    }
    const int cur = b.current;
    if (cur > 0)
    {
        snprintf(b.names.name[cur], sizeof(b.names.name[cur]), "%s", parts[part].patchName == "Unnamed" ? "" : parts[part].patchName.c_str());
    }
    const std::string path = namesFilePath(part);
    if (!polysynth::open::anySlotName(b.names) && !polysynth::open::morphTargetSet(b.names) && !filesystem->exists(path.c_str()))
    {
        return true; // nothing to say and no file to correct: a project that never named a slot gets no side file
    }
    const size_t n = polysynth::open::encodeSlotNames(b.names, s_namesBuf, sizeof(s_namesBuf));
    auto [ok, f] = filesystem->openFile(path.c_str(), FileSystem::FileOpenMode::kWriteCreateNew);
    if (!ok)
    {
        return false;
    }
    const bool written = n > 0 && filesystem->writeFile(s_namesBuf, n, f) == int32_t(n);
    filesystem->closeFile(f);
    return written;
}
#endif
