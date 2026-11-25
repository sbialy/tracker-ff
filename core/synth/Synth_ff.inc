// Feature hooks of the synth wrapper (Synth.cpp, included once after the Patch FX bank of Tracker+ and Mini): the patch slots and the
// Patch FX (FEATURE_SYNTH_OPEN on the original Tracker, FEATURE_SYNTH_PATCHFX on Tracker+ and Mini). Always inline, the image compiles the
// same calls the inline blocks made, without the features they are empty (a bool hook returns the stock answer).
#define FF_SYN_HOOK static inline __attribute__((always_inline))

// Synth::init, before the parts are made: the Patch FX bank of Tracker+ and Mini is in .noinit SDRAM, no rows, slot 0 open
FF_SYN_HOOK void ffSynthPatchFxBoot()
{
#if defined(FEATURE_SYNTH_PATCHFX) && !defined(FEATURE_SYNTH_OPEN)
    s_vary.patch = nullptr;
    s_vary.rng = 0;
    s_vary.count = 0;
    s_vary.percent = 10; // the MK66 default
    s_vary.valid = 0;
    for (PatchBank& b : s_bank) // .noinit SDRAM: no rows, slot 0 open
    {
        b.live = nullptr;
        b.filled = 0;
        b.params = 0;
        b.current = 0;
        b.applied = -1;
        b.namesBad = 0;
        polysynth::open::clearSlotNames(b.names);
        for (int w = 0; w < kKindWords; w++)
        {
            b.roundMask[w] = 0;
            b.switchMask[w] = 0;
        }
    }
#endif
}

// project load of a part's patch (Tracker+ and Mini): slot 0 opens, the rows of slots 1..15 from the Workspace slot files and the names
// of slots 1..15 and the Morph target from <P>.names
FF_SYN_HOOK void ffSynthPatchFxLoad(Synth* synth, int part)
{
#if defined(FEATURE_SYNTH_PATCHFX) && !defined(FEATURE_SYNTH_OPEN)
    synth->reloadPatchBank(part);
    synth->loadSlotNames(part);
#else
    (void)synth;
    (void)part;
#endif
}

// project save: the slot names go to the Workspace before it is copied to the project. False: not written
FF_SYN_HOOK bool ffSynthSaveSlotNames(Synth* synth, int part)
{
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
    return synth->persistSlotNames(part);
#else
    (void)synth;
    (void)part;
    return true;
#endif
}

// project load (updateToSettings with resetAllVoices): no voice and no governor state of the open synth from the previous project
FF_SYN_HOOK void ffSynthProjectLoadReset()
{
#ifdef FEATURE_SYNTH_OPEN
    DISABLE_AUDIO_INTERRUPTS();
    polysynth::open::resetOnProjectLoad();
    ENABLE_AUDIO_INTERRUPTS();
#endif
}

// initSynthPartSettings: the default engine of this part may not be in this build, the first engine that is
FF_SYN_HOOK void ffSynthDefaultEngine(SynthPartSettings& settings)
{
#ifdef FEATURE_SYNTH_OPEN
    if (!Synth::isStoredTypeValid(settings.synthType))
    {
        settings.synthType = Synth::uiToStored(0);
    }
#else
    (void)settings;
#endif
}
#undef FF_SYN_HOOK
