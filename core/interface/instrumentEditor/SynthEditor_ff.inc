// Feature hooks of the Synth screen (SynthEditor.cpp, included once after IE): the patch slots and the Patch FX (FEATURE_SYNTH_OPEN on the
// original Tracker, FEATURE_SYNTH_PATCHFX on Tracker+ and Mini), the NNA column. Each hook is always inline and gets the editor pointer the
// stock code used at its site, so the image compiles the same calls the inline blocks made. Without the features they compile to nothing
// (a bool hook returns the stock answer).
#define FF_SE_HOOK static inline __attribute__((always_inline))

// Tracker+ and Mini: Shift + Up / Down opens the next / previous patch slot (no encoder on the Mini). True: the key is used
FF_SE_HOOK bool ffSeShiftSlot(cInstrumentEditor* ed, int16_t dir)
{
#if !defined(FEATURE_SYNTH_OPEN) && defined(FEATURE_SYNTH_PATCHFX)
    if (ed->buttons->isButtonPressed(interfaceButtonShift))
    {
        ed->changeSynthSlot(dir);
        return true;
    }
#endif
    (void)ed;
    (void)dir;
    return false;
}

// Tracker+ and Mini: Shift + Left / Right, the Morph target one slot down / up. True: the key is used
FF_SE_HOOK bool ffSeShiftMorph(cInstrumentEditor* ed, int16_t dir)
{
#if !defined(FEATURE_SYNTH_OPEN) && defined(FEATURE_SYNTH_PATCHFX)
    if (ed->buttons->isButtonPressed(interfaceButtonShift))
    {
        ed->changeSynthMorphTarget(dir);
        return true;
    }
#endif
    (void)ed;
    (void)dir;
    return false;
}

// the project stores the patch name of slot 0 only (stock: one patch per part, always stored)
FF_SE_HOOK bool ffSeStoresPatchName(cInstrumentEditor* ed, int part)
{
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
    return ed->getSynth()->getPatchSlot(part) == 0;
#else
    (void)ed;
    (void)part;
    return true;
#endif
}

// the patch name of the part changed (load, init): the open synth keeps the slot's name
FF_SE_HOOK void ffSePatchNameChanged(cInstrumentEditor* ed, int part)
{
#ifdef FEATURE_SYNTH_OPEN
    ed->getSynth()->patchNameChanged(part);
#else
    (void)ed;
    (void)part;
#endif
}

// a song that ran the Patch FX may have left another slot's values in the part: the editor shows the open slot's own
FF_SE_HOOK void ffSeRestorePatchFx(cInstrumentEditor* ed, int part)
{
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
    ed->getSynth()->restorePatchFx(part);
#else
    (void)ed;
    (void)part;
#endif
}

// the last column the cursor can reach on the Synth screen: the New note block of Tracker+ and Mini is the 7th and only a plugin part has it
FF_SE_HOOK int ffSeLastPlace(cInstrumentEditor* ed)
{
#ifdef SE_NNA_IMX
    const int part = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
    return part >= 0 && part < kSynthParts && ed->getSynth()->plugPart(part) ? ed->frameData.placesCount - 1 : ed->frameData.placesCount - 2;
#else
    return ed->frameData.placesCount - 1;
#endif
}

// the NNA column of the Synth screen
FF_SE_HOOK void ffSeShowNna(cInstrumentEditor* ed, uint8_t part)
{
#if defined(FEATURE_SYNTH_OPEN) || defined(SE_NNA_IMX)
    ed->showSynthNna(part);
#else
    (void)ed;
    (void)part;
#endif
}

#undef FF_SE_HOOK
