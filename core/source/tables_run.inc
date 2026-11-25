// Tables (Table lite): the runner of each track, Sequencer::table* and namespace tables, included at the end of mtSequencer.cpp (the runner calls
// stay inlined into the audio tick, as they were when the code lived in that file). The hooks into the stock sequencer are in ff_hooks.h
// (ffHookSeqTable*). Without Table lite (ff_config.h FEATURE_TABLES) this file is empty.
#ifdef FEATURE_TABLES   // ff_config.h (via ff_hooks.h)
// ---- Tables (tables.h, tables_format.h): one runner per track, Table lite (ref/m8-tables-feasibility.md section 4)
namespace tables
{
NOINIT_PLAIN(Pool);
NOINIT_PLAIN(Run);
#ifdef TABLE_DWT
NOINIT_PLAIN(DwtBlock);
#endif
__NOINIT(EXTERNAL_RAM) static Pool s_pool;
__NOINIT(EXTERNAL_RAM) static Run s_run[kAllTracks];
uint16_t g_runMask;
__NOINIT(EXTERNAL_RAM) uint8_t s_idleTicks;
__NOINIT(EXTERNAL_RAM) float s_idleSec; // time kept for the idle advance without clock pulses
#ifdef TABLE_DWT
__NOINIT(EXTERNAL_RAM) DwtBlock g_dwt;
#endif

Pool& pool()
{
    return s_pool;
}
Run& run(int track)
{
    return s_run[track];
}
void init()
{
    poolClear(s_pool);
    memset(s_run, 0, sizeof(s_run));
    g_runMask = 0;
    s_idleTicks = 0;
    s_idleSec = 0.0f;
}
bool rowFxAllowed(uint8_t type)
{
    using F = Sequencer::strFxConsts;
    switch (type)
    {
        case F::FX_TYPE_OFF:
        case F::FX_TYPE_VOLUME:
        case F::FX_TYPE_RANDOM_VOLUME:
        case F::FX_TYPE_PANNING:
        case F::FX_TYPE_MICROTUNING:
        case F::FX_TYPE_GLIDE:
        case F::FX_TYPE_POSITION:
        case F::FX_TYPE_SAMPLE_SLICE:
        case F::FX_TYPE_REVERSE_PLAYBACK:
        case F::FX_TYPE_FILTER_LOWPASS:
        case F::FX_TYPE_FILTER_HIGHPASS:
        case F::FX_TYPE_FILTER_BANDPASS:
        case F::FX_TYPE_DELAY_SEND:
        case F::FX_TYPE_REVERB_SEND:
        case F::FX_TYPE_OVERDRIVE:
        case F::FX_TYPE_BIT_DEPTH:
        case F::FX_TYPE_TUNE:
        case F::FX_TYPE_SLIDE_UP:
        case F::FX_TYPE_SLIDE_DOWN:
        case F::FX_TYPE_VOLUME_LFO:
        case F::FX_TYPE_PANNING_LFO:
        case F::FX_TYPE_FILTER_LFO:
        case F::FX_TYPE_POSITION_LFO:
        case F::FX_TYPE_FINETUNE_LFO:
        case F::FX_TYPE_AMP_ENV:
        case F::FX_TYPE_FILTER_ENV:
        case F::FX_TYPE_GRANULAR_LENGTH:
        case F::FX_TYPE_SEND_CC_A:
        case F::FX_TYPE_SEND_CC_B:
        case F::FX_TYPE_SEND_CC_C:
        case F::FX_TYPE_SEND_CC_D:
        case F::FX_TYPE_SEND_CC_E:
        case F::FX_TYPE_SEND_CC_F: return true;
        default: return false;
    }
}
} // namespace tables

// TBL on a step without a note (play_microStep, FX_TYPE_TABLE): 0 stops the table of the track (the voice gets back what the table changed),
// 1..16 starts that table on the note that sounds (noteSounds: nothing when the track has none)
AUDIO_CORE_RAM void Sequencer::tableFxStep(uint8_t row, int fxIndex, uint8_t value, bool noteSounds, int instrument)
{
    killFxOnSlot(row, fxIndex);
    if (value == 0 || value > tables::kTables)
    {
        if (tables::run(row).table)
            tableStop(row, true);
    }
    else if (noteSounds)
    {
        tableStart(row, value, instrument);
    }
}

// A step note went out on the track: its TBL FX (the last slot read, 0 = no table even when the instrument names one), else the
// instrument's table. The old runner ends first (the note-on already reset the sample voice, a synth part is reset here)
AUDIO_CORE_RAM void Sequencer::tableNoteOn(uint8_t track, const strPlayer::strPlayerTrack::strSendStep& step, int fxSlots)
{
    int t = -1;
    for (int k = fxSlots - 1; k >= 0; k--)
    {
        if (step.fx[k].type == fx.FX_TYPE_TABLE)
            t = step.fx[k].value;
    }
    if (t < 0)
        t = step.instrument < tables::kInstruments ? tables::s_pool.bind[step.instrument] : 0;
    if (t < 1 || t > tables::kTables || step.note < 0)
    {
        if (tables::s_run[track].table)
            tableStop(track, false);
        return;
    }
    tableStart(track, uint8_t(t), step.instrument);
}

#ifdef TABLE_DWT
namespace
{
struct DwtScope
{
    tables::DwtStamp& st;
    uint32_t t0;
    explicit DwtScope(tables::DwtStamp& s) : st(s), t0(*reinterpret_cast<volatile uint32_t*>(0xE0001004)) {}
    ~DwtScope()
    {
        const uint32_t d = *reinterpret_cast<volatile uint32_t*>(0xE0001004) - t0;
        st.count++;
        st.sum += d;
        st.blockSum += d;
        if (d > st.max)
            st.max = d;
    }
};
void dwtBlockEnd(tables::DwtStamp& s)
{
    if (s.blockSum > s.blockMax)
        s.blockMax = s.blockSum;
    s.blockSum = 0;
}
} // namespace

// once per audio block (Sequencer::tick): the longest per block sum, and the first call zeroes the block
void Sequencer::tableDwtBlockEnd()
{
    if (tables::g_dwt.magic != 0x54445754)
    {
        memset(&tables::g_dwt, 0, sizeof(tables::g_dwt));
        tables::g_dwt.magic = 0x54445754;
    }
    tables::g_dwt.blocks++;
    dwtBlockEnd(tables::g_dwt.advance);
    dwtBlockEnd(tables::g_dwt.apply);
    dwtBlockEnd(tables::g_dwt.live);
}
#define TABLE_DWT_SCOPE(field) DwtScope dwtScope_(tables::g_dwt.field)
#else
#define TABLE_DWT_SCOPE(field)
#endif

// A live note (pads, MIDI in on a track, edit preview) went out on the track: the instrument's table from row 0 (user 2026-11-20). USB
// and DIN note-ons run here inside the audio callback (Tracker.cpp:247): one SDRAM byte (the binding) when the instrument has no table
AUDIO_CORE_RAM_COLD void Sequencer::tableLiveNoteOn(uint8_t track, int instrument)
{
    TABLE_DWT_SCOPE(live);
    const uint8_t t = instrument >= 0 && instrument < tables::kInstruments ? tables::s_pool.bind[instrument] : 0;
    if (!(t >= 1 && t <= tables::kTables) && !tables::s_run[track].table)
        return; // no table to start or stop: the usual case costs this byte only
    // pads come from the main loop, which the audio callback (tableAdvance) preempts: the runner changes with interrupts masked. PRIMASK is
    // saved and restored, not enabled (USB and DIN MIDI notes arrive inside the callback), as NnaIrqLock does
#if defined(__arm__)
    uint32_t primask;
    __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r"(primask)::"memory");
#endif
    if (t >= 1 && t <= tables::kTables)
        tableStart(track, t, instrument);
    else
        tableStop(track, false);
#if defined(__arm__)
    __asm__ volatile("msr primask, %0" ::"r"(primask) : "memory");
#endif
}

// The live note on the track went off (pad release, MIDI note off): its table runs on through the release and ends once the voice is
// silent (tableReleaseCheck, only while the sequencer is stopped: a playing sequencer ends it with its own steps, Stop or Off like before)
AUDIO_CORE_RAM_COLD void Sequencer::tableLiveNoteOff(uint8_t track)
{
    if (track < kAllTracks && tables::s_run[track].table)
        tables::s_run[track].rel = 1;
}

// A voice that is on (a releasing one too) keeps the table. Not a synth part or sample track: nothing to wait for
AUDIO_CORE_RAM_COLD bool Sequencer::tableVoiceSilent(uint8_t track)
{
    const tables::Run& r = tables::s_run[track];
    if (isSynthInstrument(r.instr))
    {
#if defined(FEATURE_SYNTH) && defined(FEATURE_SYNTH_OPEN)
        const int part = instrumentIdxToMidiChannel(r.instr) - 1;
        return !polysynth::open::partActive(part);
#else
        return true;
#endif
    }
    if (isSampleInstrument(r.instr) && isSampleTrack(track))
        return instrumentPlayer[track].getWaveStartAddressIfPlaying(0) == nullptr;
    return true;
}

AUDIO_CORE_RAM_COLD void Sequencer::tableReleaseCheck()
{
    for (uint8_t t = 0; t < kAllTracks; t++)
    {
        if ((tables::g_runMask & (1u << t)) && tables::s_run[t].rel && tableVoiceSilent(t))
            tableStop(t, true);
    }
}

// The sequencer is stopped (internal clock still ticks): a table a live note started advances on its own uStep count, 12 ticks each
AUDIO_CORE_RAM_COLD void Sequencer::tableIdleTick()
{
    if (++tables::s_idleTicks < 12)
        return;
    tables::s_idleTicks = 0;
    tableReleaseCheck();
    for (uint8_t t = 0; t < kAllTracks; t++)
    {
        if (tables::g_runMask & (1u << t))
            tableAdvance(t);
    }
}

// The same advance for an external clock that has gone quiet: one table step per uStep (60 / (tempo x 192) s) of the last known tempo
AUDIO_CORE_RAM_COLD void Sequencer::tableIdleFallback(float intervalSeconds)
{
    const float tempo = lastKnownExternalTempo > 1.0f ? lastKnownExternalTempo : DEFAULT_TEMPO;
    const float uStepSec = 60.0f / (tempo * float(kStepsPerBeat * kuStepsInStep));
    tables::s_idleSec += intervalSeconds;
    tableReleaseCheck();
    for (int guard = 0; tables::s_idleSec >= uStepSec && guard < 16; guard++)
    {
        tables::s_idleSec -= uStepSec;
        for (uint8_t t = 0; t < kAllTracks; t++)
        {
            if (tables::g_runMask & (1u << t))
                tableAdvance(t);
        }
    }
    if (tables::s_idleSec >= uStepSec)
        tables::s_idleSec = 0.0f;
}

// An Arp, Roll or Stutter note: the running table (or the instrument's) from row 0, as a new note of a table instrument
AUDIO_CORE_RAM void Sequencer::tableRetrigger(uint8_t track, const strPlayer::strPlayerTrack::strSendStep& step)
{
    const uint8_t t = tables::s_run[track].table ? tables::s_run[track].table
                                                  : (step.instrument < tables::kInstruments ? tables::s_pool.bind[step.instrument] : 0);
    if (t >= 1 && t <= tables::kTables && step.note >= 0)
        tableStart(track, t, step.instrument);
}

AUDIO_CORE_RAM_COLD void Sequencer::tableStart(uint8_t track, uint8_t table, int instrument)
{
    if (stereopairs::isFollower(track) || !(isSynthInstrument(instrument) || isSampleInstrument(instrument)))
        return; // the second track of a linked pair plays nothing, a MIDI instrument has no table
    tables::Run& r = tables::s_run[track];
    const bool synth = isSynthInstrument(instrument);
    if (r.table && (!synth || r.instr != instrument))
        tableStop(track, false); // another part, or a sample voice (the note-on has reset its tune, FX and table gain)
    r.table = table;
    r.row = 0;
    r.rel = 0;
    r.timer = 0xFF; // the advance of this very uStep makes it 0: row 1 starts tic uSteps after the note
    r.instr = uint8_t(instrument);
    r.fx = 0;
    // a synth part keeps its transpose across notes: row 0 always applies. A sample voice's tune is the note's own after the note-on,
    // its table gain is 1 (tableStop) or the last table's: V always applies
    r.semis = synth ? tables::kNoteEmpty : 0;
    r.vol = tables::kVolEmpty;
    tables::g_runMask = uint16_t(tables::g_runMask | (1u << track));
    tableApply(track);
}

AUDIO_CORE_RAM void Sequencer::tableAdvance(uint8_t track)
{
    TABLE_DWT_SCOPE(advance);
    tables::Run& r = tables::s_run[track];
    const tables::Table& tb = tables::s_pool.table[r.table - 1];
    if (++r.timer < (tb.tic < tables::kTicMin ? tables::kTicMin : tb.tic))
        return;
    r.timer = 0;
    if (r.row + 1 >= tb.len)
    {
        if (!tb.loop)
        {   // Once: the last row holds until the next note, a TBL FX, an Off step or Stop
            tables::g_runMask = uint16_t(tables::g_runMask & ~(1u << track));
            return;
        }
        r.row = 0;
    }
    else
    {
        r.row++;
    }
    tableApply(track);
}

#if defined(FEATURE_SYNTH) && !defined(FEATURE_SYNTH_OPEN) && defined(PLATFORM_IMX)
// Tracker+ and Mini: the stock synth parts have no table transpose and no bend range setter, a full wheel is kLibBendSemis semitones
// (core/source/synthPlayRouter.cpp), so the row's transpose goes out as the part's pitch bend and ends at that range (the row's volume
// is not taken: the stock part has no per note gain the table could own)
static constexpr int kTableLibBendSemis = 2;
AUDIO_CORE_RAM static void tableSynthTranspose(int part, int semis)
{
    const int b = semis * 8192 / kTableLibBendSemis;
    sendPitchbend(int16_t(b < -8192 ? -8192 : (b > 8191 ? 8191 : b)), int8_t(part + 1), true);
}
#endif

// The row of the runner onto the voice: N and V only when they change, the FX every time it is there (an empty FX cell keeps the last)
AUDIO_CORE_RAM void Sequencer::tableApply(uint8_t track)
{
    TABLE_DWT_SCOPE(apply);
    tables::Run& r = tables::s_run[track];
    const tables::Row& row = tables::s_pool.table[r.table - 1].row[r.row];
    const int8_t semis = row.note == tables::kNoteEmpty ? 0 : row.note;
    const uint8_t vol = row.vol == tables::kVolEmpty ? 100 : row.vol;
    const bool fxOk = row.fxType && tables::rowFxAllowed(row.fxType);
    if (isSynthInstrument(r.instr))
    {
#if defined(FEATURE_SYNTH) && (defined(FEATURE_SYNTH_OPEN) || defined(PLATFORM_IMX))
        const int part = instrumentIdxToMidiChannel(r.instr) - 1;
        if (part < 0 || part >= kSynthParts)
            return;
#ifdef FEATURE_SYNTH_OPEN
        if (semis != r.semis || vol != r.vol)
            polysynth::open::partTable(part, semis, float(vol) * 0.01f);
#else
        if (semis != r.semis)
            tableSynthTranspose(part, semis);
#endif
        if (fxOk && engine.isSynthFx(row.fxType))
        {
            engine.seqSynthFx(row.fxType, row.fxVal, tables::kFxSlot, part);
            r.fx = 1;
        }
#endif
    }
    else if (isSampleInstrument(r.instr) && isSampleTrack(track))
    {
        playerEngine& pl = instrumentPlayer[track];
        if (semis != r.semis)
        {
            if (semis == 0)
                pl.seqFx(0, 0, tables::kTuneSlot); // back to the instrument's tune
            else
            {   // the Tune FX is a forced tune (it replaces the instrument's): the instrument's tune plus the row, -24..24
                const int tune = clamp<int>(mtProject.instrument[r.instr].tune + semis, MIN_INSTRUMENT_TUNE, MAX_INSTRUMENT_TUNE);
                pl.seqFx(fx.FX_TYPE_TUNE, uint8_t(tune - MIN_INSTRUMENT_TUNE), tables::kTuneSlot);
            }
        }
        if (vol != r.vol)
            pl.setTableGain(vol); // linear: V % of the voice gain, like a synth part (user 2026-11-20)
        const bool cc = (row.fxType >= fx.FX_TYPE_SEND_CC_A && row.fxType <= fx.FX_TYPE_SEND_CC_E) || row.fxType == fx.FX_TYPE_SEND_CC_F;
        if (fxOk && !cc) // CC A-F are synth macros (or MIDI CCs): nothing on a sample voice
        {
            pl.seqFx(row.fxType, row.fxVal, tables::kFxSlot);
            r.fx = 1;
        }
    }
    r.semis = semis;
    r.vol = vol;
}

AUDIO_CORE_RAM_COLD void Sequencer::tableStop(uint8_t track, bool restore)
{
    tables::Run& r = tables::s_run[track];
    tables::g_runMask = uint16_t(tables::g_runMask & ~(1u << track));
    if (!r.table)
        return;
    if (isSynthInstrument(r.instr))
    {
#if defined(FEATURE_SYNTH) && (defined(FEATURE_SYNTH_OPEN) || defined(PLATFORM_IMX))
        const int part = instrumentIdxToMidiChannel(r.instr) - 1;
        if (part >= 0 && part < kSynthParts)
        {
#ifdef FEATURE_SYNTH_OPEN
            if (r.semis != 0 || r.vol != 100)
                polysynth::open::partTable(part, 0, 1.0f);
#else
            if (r.semis != 0)
                tableSynthTranspose(part, 0);
#endif
            if (restore && r.fx)
                engine.seqSynthFx(fx.FX_TYPE_OFF, 0, tables::kFxSlot, part);
        }
#endif
    }
    else if (isSampleInstrument(r.instr) && isSampleTrack(track))
    {
        playerEngine& pl = instrumentPlayer[track];
        if (r.vol != 100)
            pl.setTableGain(100); // back to the voice's own gain (a stop without a new note)
        if (restore)
        {
            if (r.semis != 0)
                pl.seqFx(0, 0, tables::kTuneSlot);
            if (r.fx)
                pl.seqFx(0, 0, tables::kFxSlot);
        }
    }
    r.table = 0;
    r.fx = 0;
}

AUDIO_CORE_RAM_COLD void Sequencer::tableStopAll()
{
    for (uint8_t t = 0; t < kAllTracks; t++)
    {
        if (tables::s_run[t].table)
            tableStop(t, true);
    }
}
#endif
