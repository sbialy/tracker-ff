// interface.cpp, included where this block was: Shift + Song opens the Parts view of the Song screen (FEATURE_PARTS)
#ifdef FEATURE_PARTS
// Shift + Song: the press goes to the page as a plain Song press, so the page runs its own checks and switches. On the Song screen the
// view switches to Parts (and back to Patterns), with the Song screen's own refusal while something plays
bool Interface::tryPartsEntry()
{
    if (operatingMode != mtOperatingModeRun || specialScreenMode != specialScreenModeDisabled || onScreenModule == nullptr)
    {
        return false;
    }
    if (onScreenModule == &songEditor)
    {
        if (!songEditor.isBusy) songEditor.partsEnterView(!songEditor.partsView);
        return true;
    }
    ip->processButtonsInput(interfaceButtonSong, buttonPress);
    if (onScreenModule != &songEditor) return true;   // the page refused (its popup says why), the release is swallowed all the same
    if (!songEditor.partsView) songEditor.partsEnterView(true);
    return true;
}
#endif
