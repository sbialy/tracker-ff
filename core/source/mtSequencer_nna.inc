// mtSequencer.cpp, included where this block was: the New Note Action of the open synth (FEATURE_SYNTH_OPEN), parked notes of a track and their release
#ifdef FEATURE_SYNTH_OPEN
// Continue: the note the track has open (stepSent, noteId[track]) is parked instead of ended. open: false when the note is not sounding any more
// (its off went out already): nothing to park. A note without an id (live paths clear it) cannot be told from another note of its pitch: ended as before
void Sequencer::nnaParkNote(uint8_t track, bool open)
{
    auto& t = player.track[track];
    if (!open || noteId[track] == 0)
    {
        sendNoteOff(track, &t.stepSent);
        return;
    }
    if (nnaParkCount == kNnaParkMax)
    {
        nnaRelease(nnaParked[0]); // full: the oldest ends now
        memmove(&nnaParked[0], &nnaParked[1], sizeof(NnaPark) * (kNnaParkMax - 1));
        nnaParkCount--;
    }
    NnaPark& e = nnaParked[nnaParkCount++];
    e.id = noteId[track];
    e.track = track;
    e.step = t.stepSent;
    e.para = track < 32 && (chordParaTracks & (1u << track));
    chordParaTracks &= ~(1u << (track & 31)); // the flag is the parked note's now, the next MIDI Chord note-on sets its own
    if (strumPending)
    {
        strumCancel(track); // the strummed notes of the old chord that have not started never start (as the Release path does)
    }
}

void Sequencer::nnaRelease(const NnaPark& e)
{
    polysynth::open::noteTag(e.id);
    strPlayer::strPlayerTrack::strSendStep step = e.step;
    if (playingChordSlot(&step) >= 0)
    {
        sendMidiChordOff(e.track, &step, e.para ? 1 : 0);
    }
    else
    {
        sendMidiNoteOff(step.note, 0, instrumentIdxToMidiChannel(step.instrument), true);
    }
    polysynth::open::noteTag(0);
}

// The audio callback (tick: the replace sites, the tick net, the MIDI transport Play and Stop) preempts the main loop (Stop, Play and record
// start from the UI, clearSelected), never the other way round. Both take entries out of the park list in steps (copy, shift, count), so the
// main loop's steps run with interrupts masked. PRIMASK is saved and restored, not enabled: nnaPop also runs inside the callback.
// The append (nnaParkNote) runs in the callback only and needs no mask. A host build has no interrupts: nnaPreempt stands in for them.
namespace
{
struct NnaIrqLock
{
#if defined(__arm__)
    uint32_t primask;
    explicit NnaIrqLock(uint8_t*) { __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r"(primask)::"memory"); }
    ~NnaIrqLock() { __asm__ volatile("msr primask, %0" ::"r"(primask) : "memory"); }
#else
    uint8_t* depth;
    explicit NnaIrqLock(uint8_t* d) : depth(d) { (*depth)++; }
    ~NnaIrqLock() { (*depth)--; }
#endif
};
} // namespace

#if !defined(__arm__)
void Sequencer::nnaPreempt()
{
    if (!nnaPreemptOn || nnaLockDepth || nnaInPreempt)
    {
        return;
    }
    nnaInPreempt = true;
    for (int row = 0; row < kAllTracks; row++)
    {
        if (nnaParkCount && !player.track[row].noteOpen && !player.track[row].recOpen)
        {
            nnaFlushTrack(row); // the tick net, as the callback would run it
        }
    }
    nnaInPreempt = false;
}
#endif

bool Sequencer::nnaPop(int track, NnaPark& out)
{
#if defined(__arm__)
    NnaIrqLock lock(nullptr);
#else
    NnaIrqLock lock(&nnaLockDepth);
#endif
    bool found = false;
    for (int i = 0; i < nnaParkCount && !found; i++)
    {
        if (track >= 0 && nnaParked[i].track != track)
        {
            continue;
        }
        out = nnaParked[i];
        nnaPreempt();
        memmove(&nnaParked[i], &nnaParked[i + 1], sizeof(NnaPark) * (nnaParkCount - i - 1));
        nnaPreempt();
        nnaParkCount--;
        found = true;
    }
    return found;
}

void Sequencer::nnaFlushTrack(uint8_t track)
{
    NnaPark e;
    while (nnaPop(track, e))
    {
        nnaRelease(e);
    }
}

void Sequencer::nnaFlushAll()
{
    NnaPark e;
    while (nnaPop(-1, e))
    {
        nnaRelease(e);
    }
}
#endif
