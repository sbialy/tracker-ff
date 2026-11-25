// interface.cpp, included where this block was: Shift + Perform opens the Synth play page (FEATURE_SYNTH_OPEN or FEATURE_SYNTH_PATCHFX)
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
// Shift + Perform, on every page except the Synth play page itself, the Performance page (Shift + Perform is its track page flip there)
// and the two that finish work of their own before they let go (the Sample Recorder and the Sample Editor). The press goes to the page as the press of another page key (Pattern, Song on the Pattern page)
// so the page runs its own checks (a popup, unsaved edits, Performance mode, a busy Song page) and then asks for a switch: the
// switch lands in switchModuleByButton, which opens the Synth play page instead of the page of that key. A page that does not switch
// (it refused, or it waits for an answer) leaves the user where they are.
bool Interface::trySynthPlayEntry()
{
    if (operatingMode != mtOperatingModeRun || specialScreenMode != specialScreenModeDisabled || onScreenModule == nullptr ||
        onScreenModule == &synthPlay || onScreenModule == &performanceMode || onScreenModule == &sampleRecorder ||
        onScreenModule == &sampleEditor)
    {
        return false;
    }
    synthPlayEntry = true;
    synthPlay.entryInstrument = mtProject.values.lastUsedInstrument;
    ip->processButtonsInput(onScreenModule == &patternEditor ? interfaceButtonSong : interfaceButtonPattern, buttonPress);
    const bool taken = !synthPlayEntry;
    synthPlayEntry = false;
    return taken;
}

// Perform on page 2 of Performance mode (the third page of the Perform key): the Synth play page opens straight from the page that asks, without the
// Pattern key press of trySynthPlayEntry. The caller keeps its own state (Performance mode keeps its FX and track states, see
// cPerformanceMode::openSynthPlay), a refusal leaves everything as it was
bool Interface::openSynthPlayFrom(hModule module)
{
    if (operatingMode != mtOperatingModeRun || specialScreenMode != specialScreenModeDisabled || onScreenModule != module || module == &synthPlay)
    {
        return false;
    }
    synthPlay.entryInstrument = mtProject.values.lastUsedInstrument;
    synthPlayEntry = true;
    switchModuleByButton(module, interfaceButtonPattern, 0);
    const bool taken = !synthPlayEntry;
    synthPlayEntry = false;
    return taken;
}
#endif
