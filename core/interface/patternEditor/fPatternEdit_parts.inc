// fPatternEdit.cpp: the Parts length lock (FEATURE_PARTS), used by FF_PE_PART_LENGTH_OR_RETURN
#ifdef FEATURE_PARTS
// the screen shows the playing Part: its length is the Part's (the Parts list), the source patterns keep theirs
static bool partLengthLocked()
{
    if (!parts::playing()) return false;
    mtPopups.showInfoPopup("A Part is playing", "set its length in the Parts list");
    return true;
}
#endif
