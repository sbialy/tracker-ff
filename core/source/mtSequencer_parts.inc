// mtSequencer.cpp, included where this block was: the sequencer side of Parts (FEATURE_PARTS), a Part image in the pattern buffer
#ifdef FEATURE_PARTS
void Sequencer::switchToPartImage()
{
    // the buffer holds a Part image (parts.cpp): a pointer swap as switchRamPatternsNow, but the pattern number, the song
    // position and the pattern of the pattern screen stay what the user had. A Part counts as its own pattern for the pass
    // counters (risk R5): they start at 0 with every new Part and go on across its repeats
    cancelFxes();
    std::swap(currentPattern, bufferPattern);
    player.activePatternIdx = -2;
    player.perfMixedTracks = 0;
    for (int row = 0; row < kAllTracks; row++)
    {
        player.track[row].passCount = 0;
        SNAP_FX_RESET(row);
    }
}

void Sequencer::playPartImage(bool songMode)
{
    player.songMode = songMode;
    play();
}
#endif
