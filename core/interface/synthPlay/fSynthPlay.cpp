#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)

#include "synthPlay/synthPlay.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "PolySynth/Patch.h"
#include "Synth.h"
#include "core/interface.h"
#include "core/interfacePopups.h"
#ifdef TRACKER_MINI
#include "core/interfaceMenuPopup.h"
#endif
#include "cr_section_macros.h"
#include "interruptsControl.h"
#include "mtAudioEngine.h"
#include "mtConfig.h"
#include "mtMidi.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "performanceMode/performanceMode.h"
#include "mtStructs.h"
#ifdef FEATURE_SYNTH_OPEN
#include "open_load.h"
#endif
#include "synthPlayRouter.h"
#include "synthPlayRec.h"
#include "VoiceStreamManager.h"
#include "fileManager.h"
#ifdef FEATURE_EXT_HOOKS
#include "features.h"
#endif

namespace sp = synthplay;

cSynthPlay synthPlay;
static cSynthPlay* SPL = &synthPlay;

// The part calls of the page. MK66: the open synth parts (open_load.h). Tracker+ and Mini (IMX port P12): the stock polysynth:: parts
// have no bend range, gain or voice pool setters and no sounding query, so the page does it on the tracker side:
// - bend range: the router scales the wheel by the Bend setting (synthPlayRouter.cpp sendBend, kLibBendSemis), nothing to set per part
// - gain: the morph gain goes onto the part's Volume (the project's gainDb plus the gain in dB, the Volume the instrument editor sets, the post fader
//   sends follow it), 1 puts the project's Volume back
// - voice pool: the stock pool is kSynthMaxVoices for the three parts (Synth::getMaxVoiceCount), the page gives the voices to its parts
//   and 0 to the other one (applyVoices), there is no pool to raise
// - sounding: the router's held and sustained notes, at most the part's voices on the page (release tails are not counted)
// - bank: Synth::patchBankFilled (the P12 bank of 16 slots)
namespace part
{
#ifdef FEATURE_SYNTH_OPEN
constexpr int kBankSlots = polysynth::open::kPatchBankSlots;
inline void bendRange(int p, float semitones) { polysynth::open::partBendRange(p, semitones); }
inline void gain(int p, float g) { polysynth::open::partGain(p, g); }
inline void mpeConfig(int p, int range, int pressPercent) { polysynth::open::partMpe(p, float(range), float(pressPercent) * 0.01f); }
inline void mpeNote(uint8_t chan) { polysynth::open::mpeNote(chan); }
inline void mpeBend(int p, uint8_t chan, int raw) { polysynth::open::partNoteBend(p, chan, raw); }
inline void mpePress(int p, uint8_t chan, uint8_t press) { polysynth::open::partNotePress(p, chan, press); }
inline int sounding(int p) { return polysynth::open::partSounding(p); }
inline void pagePool(bool on) { polysynth::open::setVoiceBudget(on ? polysynth::open::kMaxVoiceBudget : polysynth::open::kDefaultVoiceBudget); }
inline bool bankHas(int p, int slot) { return polysynth::open::patchBankHas(p, slot); }
#else
constexpr int kBankSlots = Synth::kPatchSlots;
inline void bendRange(int, float) {}
// MPE in is for the open parts: the stock parts of Tracker+ and Mini have no per note access (ref/mpe-scope.md section 6)
inline void mpeConfig(int, int, int) {}
inline void mpeNote(uint8_t) {}
inline void mpeBend(int, uint8_t, int) {}
inline void mpePress(int, uint8_t, uint8_t) {}
void gain(int p, float g)
{
    if (p < 0 || p >= kSynthInstrumentsCount)
    {
        return;
    }
    const float db = g >= 1.0f ? 0.0f : (g > 0.00002f ? 20.0f * log10f(g) : Synth::kGainSilence);
    engine.setSynthParam(audioEngine::SynthParam::kVolume, p, std::max(mtProject.values.synthPart[p].gainDb + db, Synth::kGainSilence),
                         audioEngine::SynthValuesSource::kInstrument);
}
int sounding(int p);   // below the page (it reads the voices the page gave)
inline void pagePool(bool) {}
inline bool bankHas(int p, int slot) { return p >= 0 && slot >= 0 && slot < kBankSlots && (SPL->partSynth()->patchBankFilled(p) >> slot & 1u); }
#endif
}   // namespace part
static void programHook(int side, uint8_t program)
{
    synthPlay.programIn(side, program);
}

static_assert(sp::kMacroCC0 == polysynth::Patch::Metadata::Macro::kMacroCCStart, "macro 1 of a part is CC 102");

static void functColumn(uint8_t button);
static void functLeft();
static void functRight();
static void functUp();
static void functDown();
static void functEncoder(int16_t value);
static void functDelete();
static void functNothing();
static void functPads(uint8_t pad, uint8_t state, int16_t velo);
static void functSwitchModule(uint8_t button);
static void leavePage(uint8_t button);
static void functConfirmCancel();
static void functConfirmStop();
static void functRec();
#ifdef TRACKER_MINI
static void functMiniNote(uint8_t state);
static void functMiniMenu(uint8_t state);
#endif

namespace
{
constexpr int kColumns = kSynthPlayColumns;
constexpr int kSubItems = 3;
constexpr int kMorphColumn = 2;
uint8_t s_shownMpe;                        // synthplay::mpeChanged as the page last showed it
volatile uint8_t s_programChanged;         // counts Program Change slots set by programIn (audio IRQ), update() redraws the Bank columns
uint8_t s_shownProgram;                    // s_programChanged as the page last showed it
constexpr uint32_t kSaveDelayMs = 1500;   // a change is written this long after the last one, at once when the page closes

// The send items (ref/synth-play-state-sends.md): not synthplay values, they are the Delay send and the Reverb send of the part, the same
// mtProject.values.synthPart[].delaySend / reverbSend the Instrument params page edits, saved with the project
constexpr uint8_t kDelayA = sp::kValues;
constexpr uint8_t kReverbA = sp::kValues + 1;
constexpr uint8_t kDelayB = sp::kValues + 2;
constexpr uint8_t kReverbB = sp::kValues + 3;
constexpr uint8_t kItemCount = sp::kValues + 4;
constexpr uint8_t kNone = 0xFF;

// The columns: the items each button edits. The button of the selected column again steps on to the next item of its row (the last one goes back to
// the first): Synth A, Voices - Bank A, Delay A, Reverb A - Bank B, Delay B, Reverb B - Velocity, Transpose - Bend, Mod wheel - Channel, Sustain.
// Input and Fixed vel have no column, they are read from the file only
// The MPE rows (ref/mpe-in-p1.md) take the free third place of the Velocity, Bend and Channel columns: Press > Vol, MPE zone, MPE range
const uint8_t kItems[kColumns][kSubItems] = {
    { sp::kPart, sp::kVoices, kNone },     { sp::kBankA, kDelayA, kReverbA },   { sp::kMorph, kNone, kNone },
    { sp::kBankB, kDelayB, kReverbB },     { sp::kPartB, kNone, kNone },        { sp::kVelCurve, sp::kTranspose, sp::kMpePress },
    { sp::kBend, sp::kMod, sp::kMpeZone }, { sp::kChannel, sp::kSustain, sp::kMpeRange },
};
const char* const kNames[kColumns][kSubItems] = {
    { "Synth A", "Voices", "" },           { "Bank A", "Delay A", "Reverb A" }, { "Morph", "", "" },
    { "Bank B", "Delay B", "Reverb B" },   { "Synth B", "", "" },               { "Velocity", "Transpose", "Press > Vol" },
    { "Bend", "Mod wheel", "MPE zone" },   { "Channel", "Sustain", "MPE range" },
};
// The value text, one format per item (the number is value + 1 for part A), the Velocity column shows the curve name, the Bank columns the slot,
// Off and Omni are the formats of a 0
//                                      input channel voices curve fixed bend mod sustain part transpose partB morph bankA bankB
//                                      input channel voices curve fixed bend mod sustain part transpose partB morph bankA bankB zone range press sends
const char* const kFmt[kItemCount] = { "", "%d", "%d", "", "", "%d st", "Macro %d", "Hold", "Synth %d", "%+d", "Synth %d", "%d", "", "", "Lower %d", "%d st", "%d%%", "%d", "%d", "%d", "%d" };
const char* const kFmt0[kItemCount] = { "", "Omni", "", "", "", "", "Off", "Off", "", "", "Off", "", "", "", "Off", "", "Off", "Off", "Off", "Off", "Off" };
const char* const kCurve[4] = { "Soft", "Linear", "Hard", "Fixed" };
// /Features/synthplay.cfg: "synthplay 1" and one "key value" line per setting, in the order of synthplay::Param, the keys never renamed
const char* const kFile = "/Features/synthplay.cfg";
const char* const kKey[sp::kParams] = { "input", "channel", "voices", "curve", "fixed", "bend", "mod", "sustain" };
// The page settings after the eight (written after them, an older file has none and an older firmware skips them): the file keeps them across a power cycle
struct ExtraKey
{
    const char* key;
    uint8_t item;
};
const ExtraKey kExtra[] = { { "transpose", sp::kTranspose }, { "morph", sp::kMorph }, { "mpezone", sp::kMpeZone }, { "mperange", sp::kMpeRange }, { "mpepress", sp::kMpePress } };
// The MPE keys are written only when they differ from the default (the file of a project that never touched them is what it was): the first three extras always
constexpr int kExtraAlways = 2;
const int8_t kMpeDefault[3] = { 15, 48, 0 };
//                                         input channel voices curve fixed bend mod sustain part transpose partB morph bankA bankB zone range press
const int8_t kMin[kItemCount] = { 0, 0, 1, 0, 1, 0, 0, 0, 0, -24, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };
const int8_t kMax[kItemCount] = { 2, 16, 8, 3, 127, 12, 6, 1, 2, 24, 3, 100, 15, 15, 15, 96, 100, 100, 100, 100, 100 };

bool isSend(int item)
{
    return item >= kDelayA && item < kItemCount;
}

// What the controls point at, in the SDRAM (not cleared at boot, set up in start())
struct Texts
{
    char col[kColumns][14];
    char title[20];               // "Synth Play 16 vc" (A/B mode)
    char counts[24];              // the first line of the voice line in A/B mode: "A 3/8  B 3/8"
    char notes[sp::kCells * 5];   // the voice line: "C4 E4 g4", a lower case note is held by the pedal
};
__NOINIT(EXTERNAL_RAM) Texts s_t;

const uint16_t kFramePlaces[kColumns][4] = {
    { 1, 95, 97, 327 },   { 101, 95, 97, 327 }, { 201, 95, 97, 327 }, { 301, 95, 97, 327 },
    { 401, 95, 97, 327 }, { 501, 95, 97, 327 }, { 601, 95, 97, 327 }, { 701, 95, 97, 327 },
};

// The page keys: each one leaves the page for its own page
const uint8_t kPageKeys[] = { interfaceButtonPerformance, interfaceButtonParams,     interfaceButtonFile,       interfaceButtonConfig,
                              interfaceButtonMaster,      interfaceButtonSamplePlay, interfaceButtonSampleEdit, interfaceButtonSampleRec,
                              interfaceButtonSampleLoad,  interfaceButtonSong,       interfaceButtonPattern,    interfaceButtonGame };

// "C4", "C#4", "A#-1": appended to out, at most 4 characters, returns the end
char* noteName(uint8_t note, char* out)
{
    static const char kNames[] = "C C#D D#E F F#G G#A A#B ";
    const int n = note % 12;
    *out++ = kNames[n * 2];
    if (kNames[n * 2 + 1] != ' ')
    {
        *out++ = kNames[n * 2 + 1];
    }
    const int octave = note / 12 - 1;
    if (octave < 0)
    {
        *out++ = '-';
    }
    *out++ = char('0' + (octave < 0 ? 1 : octave));
    return out;
}

bool shiftDown()
{
    return ModuleBase::buttons->isButtonPressed(interfaceButtonShift);
}

// The voices per side while the SAME engine plays on A and B (a pair), one row per engine of the 1.4.0 set: the stored engine number of the project
// (engines/engines.json "stored", engine_list.h) and the cap. To change a voicing edit the number in its row, nothing else reads it. An engine that
// is not listed gets kPairCapDefault. The cap only lowers the Voices setting and the engine's own cap (8 on this page), it never raises them.
// The final CHIP voicing (8+8 or 6+6) is decided in the pre-release device round: CHIP 8+8 is a CPU question (E 190 % of the block on the default
// patch, ref/synth-ab-8p8.md section 6), the pool of 16 voice objects is not the limit.
constexpr int kPairCapDefault = 8;
#ifdef FEATURE_SYNTH_OPEN
struct PairCap
{
    uint8_t stored;
    uint8_t cap;
};
constexpr PairCap kPairCap[] = {
    { 0, 8 },    // ACD
    { 1, 8 },    // FAT
    { 2, 8 },    // VAP
    { 3, 8 },    // WTFM
    { 4, 8 },    // PERC
    { 6, 8 },    // CHIP, 16 voice objects since CHIP 1.1.0 (CHIP 1.1.0). 8+8 or 6+6 is decided in the device round
    { 7, 8 },    // JU6
    { 8, 8 },    // PHZ
    { 9, 8 },    // FM4
    { 10, 8 },   // D-Type
    { 11, 8 },   // CS-Type (8+8 is 101 % of the block on the default patch, ref/cstype-pair.md)
    { 12, 8 },   // A-Type
    { 13, 8 },   // T-Type
    { 14, 8 },   // 8-Type
    { 15, 8 },   // Hand-Type
    { 16, 8 },   // B-Type
    { 17, 8 },   // Arc-Type
    { 18, 4 },   // PLK: 4 is a POOL limit, not a CPU choice. One pool of 8 delay lines (pluck.cpp kLines) is shared by all its parts, 8+8 needs
                 // 8,704 B more SDRAM than the 2,304 B free (ref/synth-ab-8p8.md section 4)
    { 22, 8 },   // ALCO, 16 extension blocks since ALCO 1.1.0 (engines set 1.4.1)
};
#else
// Tracker+ and Mini (stock polysynth:: parts): the stock voice pool is kSynthMaxVoices for all three parts (Synth::getMaxVoiceCount, the
// stock lib's own rule, there is no larger pool to raise), so a pair has half of it per side for every engine, the same engine or not.
// That is also the CPU bound: the stock lib renders at most kSynthMaxVoices voices in the sequencer, the pair never renders more.
// The engine's own maxVoices (patch metadata) may still lower a side.
constexpr int kPairCapImx = kSynthMaxVoices / 2;
#endif

#ifdef UI_SHOT_HOST
// The host sim overrides one row to prove that the table is what the page reads (synth_play_sim.sh, "poke paircap"). 0xFF: none
int s_pairCapSimStored = 0xFF;
int s_pairCapSimCap = 0;
#endif

int pairCapOf(uint32_t stored)
{
#ifdef UI_SHOT_HOST
    if (stored == uint32_t(s_pairCapSimStored))
    {
        return s_pairCapSimCap;
    }
#endif
#ifdef FEATURE_SYNTH_OPEN
    for (const PairCap& row : kPairCap)
    {
        if (row.stored == stored)
        {
            return row.cap;
        }
    }
    return kPairCapDefault;
#else
    (void)stored;
    return kPairCapImx;
#endif
}

// The same engine on both sides (B on): its row of kPairCap is the cap per side. 0: not a same engine pair, no pair cap
int samePairCap()
{
    const int b = sp::value[sp::kPartB] - 1;
    if (b < 0)
    {
        return 0;
    }
    const uint32_t t = uint32_t(mtProject.values.synthPart[sp::value[sp::kPart]].synthType);
#ifdef FEATURE_SYNTH_OPEN
    return t == uint32_t(mtProject.values.synthPart[b].synthType) ? pairCapOf(t) : 0;
#else
    return pairCapOf(t);   // every pair: the stock pool is shared by the two sides
#endif
}

// True while the pair cap takes voices away from the default 8 (the title then shows it as N+N)
bool pairLimited()
{
    const int c = samePairCap();
    return c > 0 && c < kPairCapDefault;
}

int partB()
{
    return sp::value[sp::kPartB] - 1;   // -1: off
}

// The part and the value of a send item. Part -1: the side is off (B), the item shows "-" and does nothing
int sendPart(int item)
{
    return item == kDelayA || item == kReverbA ? sp::value[sp::kPart] : partB();
}

float& sendValue(int item, int part)
{
    SynthPartSettings& s = mtProject.values.synthPart[part];
    return item == kDelayA || item == kDelayB ? s.delaySend : s.reverbSend;
}

#ifdef FEATURE_SYNTH_REC
constexpr uint32_t kCodecRate = 44118;     // Hz, the title clock
constexpr uint32_t kLandWaitMs = 5000;     // the file manager busy (autosave): the landing waits this long for it
char s_recMsg[24];                         // "AB_001 in slot 49", the popup after a take
#endif
}   // namespace

//=============================================================================================================
// controls
//=============================================================================================================
void cSynthPlay::initDisplayControls()
{
    strControlProperties prop;
    frameData.placesCount = kColumns;
    frameData.startPlace = 0;
    for (int i = 0; i < kColumns; i++)
    {
        frameData.places[i] = &kFramePlaces[i][0];
    }
    prop.style = 0;
    prop.value = 0;
    prop.colors = nullptr;
    prop.data = &frameData;
    frame = display.createControl<cFrame>(&prop);

    for (uint8_t i = 0; i < kColumns; i++)
    {
        prop.data = nullptr;
        prop.text = nullptr;
        prop.colors = display.getTheme().activeLabelsColors;
        prop.style = controlStyleCenterX | controlStyleCenterY | controlStyleFont3;
        prop.x = (800 / 8) * i + (800 / 16);
        prop.y = 424;
        prop.w = 800 / 8 - 6;
        prop.h = 55;
        prop.value = 1;
        label[i] = display.createControl<cLabel>(&prop);

        prop.colors = display.getTheme().activeBarColors;
        prop.value = 0;
        prop.x = (800 / 8) * i + 1;
        prop.y = 93;
        prop.w = 800 / 8 - 3;
        prop.h = 330;
        prop.style = controlStyleValue_0_100 | controlStyleBackground |
                     (display.getTheme().barShadow > 0 ? controlStyleGradient : 0) |
                     (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
        bar[i] = display.createControl<cBar>(&prop);

    }

    prop.colors = display.getTheme().activeLabelsColors;
    prop.style = controlStyleCenterX | controlStyleCenterY | controlStyleFont2;
    prop.x = 400;
    prop.y = 31;
    prop.w = 800 - 6;
    prop.h = 58;
    prop.value = 1;
    voiceLine = display.createControl<cLabel>(&prop);

    prop.text = nullptr;
    prop.colors = nullptr;
    prop.value = 255;
    prop.style = controlStyleNoTransparency | controlStyleShow;
    prop.x = 0;
    prop.w = 800;
    prop.y = 425;
    prop.h = 54;
    bgLabel = display.createControl<cBgLabel>(&prop);
}

// The item a column edits now: the one of its row that the button was stepped to
const char* cSynthPlay::columnText(int column) const
{
    return s_t.col[column];
}

int cSynthPlay::columnItem(int column) const
{
    return kItems[column][sub[column]];
}

//=============================================================================================================
// life cycle
//=============================================================================================================
void cSynthPlay::start(uint32_t)
{
    memset(&s_t, 0, sizeof(s_t));
    moduleRefresh = 1;
    entered = false;
    confirming = false;
    // selected and sub (the column and its item) stay as the user left them, so does the rest of the page state (enter)
    shownChange = 0xFF;
    shownCounts = 0xFFFF;
    shownMorph = 0xFF;
    rec = kRecIdle;
    recPendingKey = 0xFF;
    recPopup1 = recPopup2 = nullptr;
    recShownSec = 0xFFFF;

    if (sequencer.isPlay())
    {
        // the same stop confirm as the Sample Recorder: the song stops only when the user says yes (the Play stop lock lets Shift + Play stop it,
        // this screen is the deliberate gesture), Cancel goes back to the page that was open with the song still playing
        confirming = true;
        showConfirm();
        return;
    }
    enter();
    setFunctions();
    showAll();
}

void cSynthPlay::stop()
{
#ifdef FEATURE_SYNTH_REC
    // The page keys wait for the take (leaveKey), so this is another way out: a take still running is dropped, one that lands is finished first
    recPendingKey = 0xFF;
    if (rec == kRecTake)
    {
        synthplayrec::end();
        rec = kRecIdle;
    }
    for (const uint32_t t0 = platform->getTimer()->millis(); rec == kRecLanding && platform->getTimer()->millis() - t0 < 30000;)
    {
        newFileManager.update();
        recUpdate();
    }
#endif
    leave();
    confirming = false;
    mtPopups.hideInfoPopup();
}

void cSynthPlay::update()
{
    if (confirming)
    {
        return;
    }
#ifdef FEATURE_SYNTH_REC
    if (rec != kRecIdle)
    {
        recUpdate();
    }
    else if (recPopup1)
    {
        recShowPopup();
    }
#endif
    const uint16_t counts = partB() < 0 ? 0 : uint16_t(part::sounding(sp::value[sp::kPart]) | part::sounding(partB()) << 8);
    if (sp::changed != shownChange || counts != shownCounts)
    {
        shownChange = sp::changed;
        shownCounts = counts;
        showVoices();
    }
    if (sp::mpeChanged != s_shownMpe)   // an MPE Configuration Message or RPN 0 set the zone or the range
    {
        s_shownMpe = sp::mpeChanged;
        touchSettings();
        for (uint8_t i = 0; i < kColumns; i++)
        {
            showColumn(i);
        }
        display.synchronizeRefresh();
    }
    if (s_programChanged != s_shownProgram)   // a Program Change set a Bank slot (programIn runs in the audio IRQ, the display is drawn here)
    {
        s_shownProgram = s_programChanged;
        for (int i = 0; i < kColumns; i++)
        {
            const int item = columnItem(i);
            if (item == sp::kBankA || item == sp::kBankB)
            {
                showColumn(i);
            }
        }
        display.synchronizeRefresh();
    }
    if (uint8_t(sp::value[sp::kMorph]) != shownMorph)   // CC 11 moved it
    {
        shownMorph = uint8_t(sp::value[sp::kMorph]);
        showColumn(kMorphColumn);
        display.synchronizeRefresh();
    }
    if (settingsDirty && platform->getTimer()->millis() - settingsDirtyMs >= kSaveDelayMs)
    {
        saveSettings();
    }
}

void cSynthPlay::enter()
{
    savedInstrument = entryInstrument;   // as it was at the key press, the page the key left may have reset the live one (the Pattern page stop)
    // The page as it was left (parts, banks, column, items) is back when the instrument is the one the page was entered with the last time: nothing
    // has picked another part since. The Synth A part follows another instrument, and a new session opens as the single part page
    const bool again = pageUsed && entryInstrument == lastEntry;
    pageUsed = true;
    lastEntry = entryInstrument;
    loadSettings();   // Transpose and Morph are settings of the file (or of the session where there is no file)
    if (!again)
    {
        sp::value[sp::kPart] = isSynthInstrument(savedInstrument) ? savedInstrument - SYNTH_INSTRUMENTS_START_IDX : 0;
        sp::value[sp::kPartB] = 0;   // the page opens as the single part page, B is turned on in its column
        sp::value[sp::kBankA] = int8_t(synth->getPatchSlot(sp::value[sp::kPart]));
        sp::value[sp::kBankB] = 0;
        selected = 0;
        memset(sub, 0, sizeof(sub));
    }
    else
    {
        // a project that was loaded meanwhile: the parts are the same three, a slot that has no file of the part now is the open slot again
        if (uint8_t(sp::value[sp::kPart]) >= kSynthInstrumentsCount)
        {
            sp::value[sp::kPart] = 0;
        }
        if (uint8_t(sp::value[sp::kPartB]) > kSynthInstrumentsCount || sp::value[sp::kPartB] - 1 == sp::value[sp::kPart])
        {
            sp::value[sp::kPartB] = 0;
        }
        if (!part::bankHas(sp::value[sp::kPart], sp::value[sp::kBankA]))
        {
            sp::value[sp::kBankA] = int8_t(synth->getPatchSlot(sp::value[sp::kPart]));
        }
        if (partB() < 0 || !part::bankHas(partB(), sp::value[sp::kBankB]))
        {
            sp::value[sp::kBankB] = partB() < 0 ? 0 : int8_t(synth->getPatchSlot(partB()));
        }
    }
    mtProject.values.lastUsedInstrument = SYNTH_INSTRUMENTS_START_IDX + sp::value[sp::kPart];   // the CC 110 to 119 macros of the instrument editor follow it

    sequencer.setMidiInVoiceMode(Sequencer::midiInVoiceMode_SynthPlay);
    part::pagePool(true);
    sp::setProgramHook(programHook);
    applyVoices();
    part::bendRange(sp::value[sp::kPart], sp::value[sp::kBend]);
    if (partB() >= 0)
    {
        part::bendRange(partB(), sp::value[sp::kBend]);
    }
    sp::applyMpe(false);
    s_shownMpe = sp::mpeChanged;
    s_shownProgram = s_programChanged;
    for (int side = 0; side < 2; side++)   // the slots the user chose on the page go on the parts again (the project's slot is back on leave)
    {
        const int part = side ? partB() : sp::value[sp::kPart];
        const int slot = sp::value[side ? sp::kBankB : sp::kBankA];
        if (part >= 0 && slot != synth->getPatchSlot(part))
        {
            DISABLE_AUDIO_INTERRUPTS();
            synth->applyPatchBankSlot(part, slot);
            ENABLE_AUDIO_INTERRUPTS();
        }
    }
    sp::applyMorph();
    morphAtEntry = sp::value[sp::kMorph];

    DISABLE_AUDIO_INTERRUPTS();
    sp::releaseAll();   // a clean table, a part with nothing held
    ENABLE_AUDIO_INTERRUPTS();
    entered = true;
}

// Exit: every held note gets a note off, CC 123 goes to the parts, the bend is back at 0, the macro the mod wheel moved is back at the project's
// value, the bend range is 1 semitone again, the gain of both parts is 1, both parts are back on the slot the editor has open, the voice counts
// are the project's and the voice budget is 4. Nothing resumes.
void cSynthPlay::leave()
{
    if (!entered)
    {
        return;
    }
    entered = false;
    sp::setProgramHook(nullptr);
    DISABLE_AUDIO_INTERRUPTS();
    sp::releaseAll();
    ENABLE_AUDIO_INTERRUPTS();
    restorePart(sp::value[sp::kPart]);
    if (partB() >= 0)
    {
        restorePart(uint8_t(partB()));
    }
    for (uint8_t p = 0; p < kSynthInstrumentsCount; p++)   // while the pool is 16: two parts of 8 sum to more than the 8 of the project
    {
        synth->setVoiceCount(p, mtProject.values.synthPart[p].voiceCount);
    }
    part::pagePool(false);
    sequencer.setMidiInVoiceMode(Sequencer::midiInVoiceMode_default);
    mtProject.values.lastUsedInstrument = savedInstrument;
    padsBacklight.clearGlobalPads(1, 0, 0);
    if (sp::value[sp::kMorph] != morphAtEntry)   // CC 11 moved it, no encoder step marked the file
    {
        settingsDirty = true;
    }
    if (settingsDirty)
    {
        saveSettings();
    }
}

// The part the page leaves: bend range, gain, patch slot and mod macro back to what the project has
void cSynthPlay::restorePart(uint8_t p)
{
    part::bendRange(p, 1.0f);
    part::mpeConfig(p, 48, 0);   // pressure does nothing outside the page, the member range is the MPE default
    part::gain(p, 1.0f);
    DISABLE_AUDIO_INTERRUPTS();
    synth->applyPatchBankSlot(p, synth->getPatchSlot(p));   // the values of the slot the editor has open, with its macros
    ENABLE_AUDIO_INTERRUPTS();
    const int macro = sp::value[sp::kMod] - 1;
    if (macro >= 0)
    {
        sendCC(uint8_t(sp::kMacroCC0 + macro), mtProject.values.synthPart[p].macroValue[macro], uint8_t(p + 1), true);
    }
}

// The voices a side gets: the Voices setting up to the engine's own cap and, for a same engine pair, the pair cap of its row in kPairCap. The
// setting itself stays what the user asked for
int cSynthPlay::voiceCap(int part) const
{
    const int cap = std::max(synth->getMaxVoiceCount(part), 1);
    const int pair = samePairCap();
    return pair > 0 ? std::min(cap, pair) : cap;
}

int cSynthPlay::effectiveVoices() const
{
    return std::min<int>(sp::value[sp::kVoices], voiceCap(sp::value[sp::kPart]));
}

// The two parts of the page have the voices (8 each, the pool is 16), the other part none
void cSynthPlay::applyVoices()
{
    for (uint8_t p = 0; p < kSynthInstrumentsCount; p++)
    {
        if (p != sp::value[sp::kPart] && p != partB())
        {
            synth->setVoiceCount(p, 0);
        }
    }
    sideVoices[0] = sideVoices[1] = 0;
    for (int side = 0; side < 2; side++)
    {
        const int part = side ? partB() : sp::value[sp::kPart];
        if (part >= 0)
        {
            const int cap = synth->getMaxVoiceCount(part);
            const int pair = samePairCap();
            sideVoices[side] = uint8_t(cap > 0 ? std::min(std::min<int>(sp::value[sp::kVoices], cap), pair > 0 ? pair : cap) : 0);
            synth->setVoiceCount(part, sideVoices[side]);
        }
    }
}

//=============================================================================================================
// settings file
//=============================================================================================================
void cSynthPlay::loadSettings()
{
    static const int8_t kDefault[sp::kParams] = { 0, 0, 8, 1, 100, 2, 1, 1 };
    if (!settingsLoaded)   // a build with no file keeps what the session has set
    {
        memcpy(sp::value, kDefault, sizeof(kDefault));
        sp::value[sp::kTranspose] = 0;
        sp::value[sp::kMorph] = 50;
        memcpy(&sp::value[sp::kMpeZone], kMpeDefault, sizeof(kMpeDefault));
        settingsLoaded = true;
    }
    settingsDirty = false;
#ifdef FEATURE_EXT_HOOKS
    const features::Io* io = features::cardIo();
    char buf[224];
    const int n = io ? io->read(kFile, buf, sizeof(buf) - 1) : -1;
    if (n <= 0)
    {
        return;
    }
    buf[n] = 0;
    for (char* line = buf; line && *line;)
    {
        char* next = strchr(line, '\n');
        if (next)
        {
            *next++ = 0;
        }
        char* sep = strchr(line, ' ');
        if (sep)
        {
            *sep = 0;
            const int v = atoi(sep + 1);   // "synthplay 1" is no key: skipped like any unknown line
            for (int k = 0; k < sp::kParams; k++)
            {
                if (v >= kMin[k] && v <= kMax[k] && strcmp(line, kKey[k]) == 0)
                {
                    sp::value[k] = int8_t(v);
                }
            }
            for (const ExtraKey& e : kExtra)
            {
                if (v >= kMin[e.item] && v <= kMax[e.item] && strcmp(line, e.key) == 0)
                {
                    sp::value[e.item] = int8_t(v);
                }
            }
        }
        line = next;
    }
#endif
}

void cSynthPlay::touchSettings()
{
    settingsDirty = true;
    settingsDirtyMs = platform->getTimer()->millis();
}

void cSynthPlay::saveSettings()
{
    settingsDirty = false;
#ifdef FEATURE_EXT_HOOKS
    const features::Io* io = features::cardIo();
    if (!io)
    {
        return;
    }
    char buf[224];
    int n = snprintf(buf, sizeof(buf), "synthplay 1\n");
    for (int k = 0; k < sp::kParams; k++)
    {
        n += snprintf(buf + n, sizeof(buf) - size_t(n), "%s %d\n", kKey[k], sp::value[k]);
    }
    for (int x = 0; x < int(sizeof(kExtra) / sizeof(kExtra[0])); x++)
    {
        const ExtraKey& e = kExtra[x];
        if (x < kExtraAlways || sp::value[e.item] != kMpeDefault[e.item - sp::kMpeZone])
        {
            n += snprintf(buf + n, sizeof(buf) - size_t(n), "%s %d\n", e.key, sp::value[e.item]);
        }
    }
    if (!io->exists("/Features"))
    {
        io->mkdir("/Features");
    }
    io->write(kFile, buf, n);
#endif
}

//=============================================================================================================
// screen
//=============================================================================================================
void cSynthPlay::setFunctions()
{
    ip->clearAll();
    for (uint8_t i = interfaceButton0; i <= interfaceButton7; i++)
    {
        ip->setButtonObj(i, buttonPress, functColumn);
    }
    for (uint8_t key : kPageKeys)
    {
        ip->setButtonObj(key, buttonPress, functSwitchModule);
    }
    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, functDelete);
    ip->setPotObj(interfacePot0, functEncoder);
    ip->setPadsGlobal(functPads);
    // Play and Shift + Play do nothing here, the song stays stopped (the same as the image viewer)
    ip->setButtonObj(interfaceButtonPlay, buttonPress, functNothing);
    ip->setButtonObj(interfaceButtonRec, buttonPress, functRec);   // REC records the master output as an instrument (ref/synth-ab-rec.md)
#ifdef TRACKER_MINI
    // The Mini has no pads and no page keys: Middle plays C4 (the preview pad note, + Transpose) at the Fixed vel, held while held,
    // Menu shows the page popup (a box leaves the page, the Synth Play box is this page), +1 / -1 / +10 / -10 are the encoder (Interface)
    ip->setButtonObj(interfaceButtonMiniMiddle, functMiniNote);
    ip->setButtonObj(interfaceButtonMiniMenu, functMiniMenu);
#endif
}

// The voice line: the notes of the table, a held note upper case, a note only the pedal holds lower case
void cSynthPlay::showVoices()
{
    const sp::Cell* c = sp::cells();
    char* end = s_t.notes;
    for (int i = 0; i < sp::kCells; i++)
    {
        if (c[i].state != sp::kFree)
        {
            char* start = end;
            end = noteName(c[i].out, end);
            if (c[i].state != sp::kHeld)
            {
                *start |= 0x20;
            }
            *end++ = ' ';
        }
    }
    *end = 0;
    s_t.counts[0] = 0;
    if (partB() >= 0)   // A/B: the sounding voices of each side against what it may have
    {
        snprintf(s_t.counts, sizeof(s_t.counts), "A %d/%d  B %d/%d", part::sounding(sp::value[sp::kPart]), sideVoices[0],
                 part::sounding(partB()), sideVoices[1]);
    }
    display.setControlText(voiceLine, s_t.counts);
    display.setControlText2(voiceLine, s_t.notes);   // the second line of a label is the bright one
    display.refreshControl(voiceLine);
}

// "Synth Play", and in A/B mode the voices of both sides: "16 vc", "N+N vc" on a same engine pair whose cap in kPairCap is under 8 (PLK 4+4)
void cSynthPlay::showTitle()
{
#ifdef FEATURE_SYNTH_REC
    if (rec != kRecIdle)
    {
        showRecTitle();
        return;
    }
#endif
    if (partB() < 0)
    {
        titleBar->setTitle("Synth Play");
        return;
    }
    if (pairLimited())
    {
        snprintf(s_t.title, sizeof(s_t.title), "Synth Play %d+%d vc", samePairCap(), samePairCap());
    }
    else
    {
        snprintf(s_t.title, sizeof(s_t.title), "Synth Play %d vc", sideVoices[0] + sideVoices[1]);
    }
    titleBar->setTitle(s_t.title);
}

// The value text and the bar of one column
void cSynthPlay::showColumn(int i)
{
    const int item = columnItem(i);
    int v = item == sp::kVoices ? effectiveVoices() : isSend(item) ? 0 : sp::value[item];
    if (isSend(item))
    {
        const int part = sendPart(item);
        v = part < 0 ? 0 : int(sendValue(item, part));
    }
    const char* fmt = v || !*kFmt0[item] ? kFmt[item] : kFmt0[item];
    if (item == sp::kVelCurve)
    {
        fmt = kCurve[v];
    }
    if (isSend(item) && sendPart(item) < 0)   // B is off
    {
        snprintf(s_t.col[i], sizeof(s_t.col[0]), "-");
    }
    else if (item == sp::kBankA || item == sp::kBankB)   // the slot and its name, "-" when B is off
    {
        const int part = item == sp::kBankA ? sp::value[sp::kPart] : partB();
        if (part < 0)
        {
            snprintf(s_t.col[i], sizeof(s_t.col[0]), "-");
        }
        else
        {
            snprintf(s_t.col[i], sizeof(s_t.col[0]), "%d %s", v + 1, synth->getSlotName(part, v));
        }
    }
    else
    {
        snprintf(s_t.col[i], sizeof(s_t.col[0]), fmt, item == sp::kPart ? v + 1 : v);
    }
    display.setControlText(label[i], kNames[i][sub[i]]);
    display.setControlText2(label[i], s_t.col[i]);
    display.refreshControl(label[i]);
    display.setControlValue(bar[i], (v - kMin[item]) * 100 / (kMax[item] - kMin[item]));
    display.refreshControl(bar[i]);
}

void cSynthPlay::showAll()
{
    titleBar->show(true);
    showTitle();
    titleBar->showCurrentInstrumentOnRightLabel();   // lastUsedInstrument is the part the page plays
    for (uint8_t i = 0; i < kColumns; i++)
    {
        display.setControlColors(label[i], display.getTheme().activeLabelsColors);
        display.setControlValue(label[i], 1);
        display.setRemoveControlStyle(label[i], controlStyleBackground);
        display.setControlShow(label[i]);
        display.setControlShow(bar[i]);
        showColumn(i);
    }
    display.setControlShow(voiceLine);
    display.setControlValue(frame, selected);
    display.setControlShow(frame);
    display.refreshControl(frame);
    shownChange = 0xFF;
    showVoices();
    display.setControlValue(bgLabel, 255);
    display.setControlShow(bgLabel);
    display.refreshControl(bgLabel);
    display.synchronizeRefresh();
}

// The same stop confirm as the Sample Recorder (the controls the page does not use stay hidden, they are created hidden)
void cSynthPlay::showConfirm()
{
    titleBar->show();
    titleBar->setTitle("Synth Play");
    for (uint8_t i = 6; i < kColumns; i++)
    {
        display.setControlText(label[i], i == 6 ? "Cancel" : "Stop");
        display.setControlColors(label[i], display.getTheme().highlightedButtonLabelsColors);
        display.setAddControlStyle(label[i], controlStyleBackground);
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);
    }
    mtPopups.showInfoPopup("Song is playing.", "Stop it and play the synth?");
    display.setControlValue(bgLabel, 255);
    display.setControlShow(bgLabel);
    display.refreshControl(bgLabel);

    ip->clearAll();
    ip->setButtonObj(interfaceButton6, buttonPress, functConfirmCancel);
    ip->setButtonObj(interfaceButton7, buttonPress, functConfirmStop);
    display.synchronizeRefresh();
}

//=============================================================================================================
// input
//=============================================================================================================
void cSynthPlay::selectColumn(int column)
{
    if (column < 0 || column >= kColumns)
    {
        return;
    }
    selected = uint8_t(column);
    display.setControlValue(frame, selected);
    display.refreshControl(frame);
}

// The button of the selected column again: the column steps on to the next item of its row (see kItems), the label shows which
void cSynthPlay::pressColumn(int column)
{
    if (column == selected && column >= 0 && column < kColumns && kItems[column][1] != kNone)
    {
        sub[column] = uint8_t(sub[column] + 1 < kSubItems && kItems[column][sub[column] + 1] != kNone ? sub[column] + 1 : 0);
        showColumn(column);
        display.synchronizeRefresh();
        return;
    }
    selectColumn(column);
}

// A Bank column: the encoder steps over the slots that have no file of the part's engine, the slot's patch goes on the part at once (the
// function of the Patch step FX, held notes keep sounding and the values ramp over a block). False: nothing changed.
bool cSynthPlay::stepBank(int item, int delta)
{
    const int part = item == sp::kBankA ? sp::value[sp::kPart] : partB();
    if (part < 0)
    {
        return false;
    }
    const int dir = delta < 0 ? -1 : 1;
    int slot = sp::value[item];
    for (int n = delta < 0 ? -delta : delta; n > 0; n--)
    {
        int s = slot;
        do
        {
            s += dir;
        } while (s >= 0 && s < part::kBankSlots && !part::bankHas(part, s));
        if (s < 0 || s >= part::kBankSlots)
        {
            break;
        }
        slot = s;
    }
    if (slot == sp::value[item])
    {
        return false;
    }
    DISABLE_AUDIO_INTERRUPTS();
    const bool ok = synth->applyPatchBankSlot(part, slot);
    ENABLE_AUDIO_INTERRUPTS();
    if (ok)
    {
        sp::value[item] = int8_t(slot);
    }
    return ok;
}

// A MIDI Program Change on the channel of a side (Config > MIDI > Program in): the Bank column of the side steps to that slot at once, the same
// call as stepBank. A program above 15 and a slot that has no file of the part's engine (the bank only holds the part's own) change nothing.
// Runs in the audio IRQ (midiUpdate is called from TrackerImpl::updateAudioBuffers): the slot is applied here, the column is redrawn by update()
void cSynthPlay::programIn(int side, uint8_t program)
{
    const int item = side ? sp::kBankB : sp::kBankA;
    const int part = side ? partB() : sp::value[sp::kPart];
    if (!entered || confirming || part < 0 || program >= part::kBankSlots || !part::bankHas(part, program))
    {
        return;
    }
    DISABLE_AUDIO_INTERRUPTS();
    const bool ok = synth->applyPatchBankSlot(part, program);
    ENABLE_AUDIO_INTERRUPTS();
    if (!ok)
    {
        return;
    }
    sp::value[item] = int8_t(program);
    s_programChanged = uint8_t(s_programChanged + 1);
}

// A send column: the part's send in the project, applied the way the Instrument params page does (the engine's instrument source, the project is
// marked changed so it is saved with the next autosave). The sends are mixed whatever the sequencer does, the page needs nothing of its own
void cSynthPlay::changeSend(int item, int delta)
{
    const int part = sendPart(item);
    if (part < 0)
    {
        return;
    }
    float& v = sendValue(item, part);
    const float next = std::clamp<float>(roundf(v) + float(delta), 0.0f, 100.0f);
    if (next == v)
    {
        return;
    }
    v = next;
    engine.setSynthParam(item == kDelayA || item == kDelayB ? audioEngine::SynthParam::kDelaySend : audioEngine::SynthParam::kReverbSend, part, next,
                         audioEngine::SynthValuesSource::kInstrument);
    newFileManager.setProjectStructChanged();
    showColumn(selected);
    display.synchronizeRefresh();
}

void cSynthPlay::changeValue(int delta)
{
    const int item = columnItem(selected);
    if (isSend(item))
    {
        changeSend(item, delta);
        return;
    }
    if (item == sp::kBankA || item == sp::kBankB)
    {
        if (stepBank(item, delta))
        {
            showColumn(selected);
            display.synchronizeRefresh();
        }
        return;
    }
    const bool voices = item == sp::kVoices;   // the column shows, and steps from, the voices the part really has
    const bool isPart = item == sp::kPart || item == sp::kPartB;
    const int old = voices ? effectiveVoices() : sp::value[item];
    const int step = item == sp::kMorph && !shiftDown() ? delta * 5 : delta;   // Shift: single steps
    const int top = voices ? voiceCap(sp::value[sp::kPart]) : kMax[item];
    int next = std::clamp<int>(old + step, kMin[item], top);
    if (isPart)
    {
        // never the same part on both sides: the encoder steps over the part of the other side
        const int dir = delta < 0 ? -1 : 1;
        for (int n = 0; n < 2 && (item == sp::kPart ? next == partB() : (next && next == sp::value[sp::kPart] + 1)); n++)
        {
            next = std::clamp<int>(next + dir, kMin[item], top);
        }
        if (next == old || (item == sp::kPart ? next == partB() : (next && next == sp::value[sp::kPart] + 1)))
        {
            return;
        }
        DISABLE_AUDIO_INTERRUPTS();
        if (item == sp::kPart)
        {
            sp::setPart(uint8_t(next));   // releases the old part first
        }
        else
        {
            sp::setPartB(uint8_t(next));
        }
        ENABLE_AUDIO_INTERRUPTS();
        const int oldPart = item == sp::kPart ? old : old - 1;
        if (oldPart >= 0)
        {
            restorePart(uint8_t(oldPart));
        }
        const int newPart = item == sp::kPart ? next : next - 1;
        if (newPart >= 0)
        {
            part::bendRange(newPart, sp::value[sp::kBend]);
            part::mpeConfig(newPart, sp::value[sp::kMpeRange], sp::value[sp::kMpePress]);
            sp::value[item == sp::kPart ? sp::kBankA : sp::kBankB] = int8_t(synth->getPatchSlot(newPart));
        }
        if (item == sp::kPart)
        {
            mtProject.values.lastUsedInstrument = SYNTH_INSTRUMENTS_START_IDX + next;
        }
        sp::applyMorph();
    }
    else
    {
        sp::value[item] = int8_t(next);
    }
    switch (item)
    {
        case sp::kPart:
        case sp::kPartB:
        case sp::kVoices:
            applyVoices();
            showTitle();
            titleBar->showCurrentInstrumentOnRightLabel();   // after the voices: a part with none is named Off
            break;
        case sp::kMorph: sp::applyMorph(); break;
        case sp::kBend:
            part::bendRange(sp::value[sp::kPart], next);
            if (partB() >= 0)
            {
                part::bendRange(partB(), next);
            }
            DISABLE_AUDIO_INTERRUPTS();
            sp::resendBend();   // the wheel stays where it is, the part scales it again
            ENABLE_AUDIO_INTERRUPTS();
            break;
        case sp::kMpeZone:   // notes keyed under the old zone are let go first
            DISABLE_AUDIO_INTERRUPTS();
            sp::applyMpe(true);
            ENABLE_AUDIO_INTERRUPTS();
            break;
        case sp::kMpeRange:
        case sp::kMpePress: sp::applyMpe(false); break;
        case sp::kSustain:
            if (!next)
            {
                DISABLE_AUDIO_INTERRUPTS();
                sp::pedalOff();
                ENABLE_AUDIO_INTERRUPTS();
            }
            break;
        default: break;
    }
    touchSettings();
    for (uint8_t i = 0; i < kColumns; i++)
    {
        showColumn(i);
    }
    shownChange = 0xFF;   // the voice line has the voices of the sides
    display.synchronizeRefresh();
}

// Shift + Delete: every note off, CC 123 to the part, the bend centred, the pedal up
void cSynthPlay::panic()
{
    DISABLE_AUDIO_INTERRUPTS();
    sp::releaseAll();
    ENABLE_AUDIO_INTERRUPTS();
    padsBacklight.clearGlobalPads(1, 0, 0);
}

void cSynthPlay::padNote(uint8_t pad, bool down)
{
    const uint8_t note = mtPadBoard.getNoteFromPad(pad);
    padsBacklight.setFrontLayer(down, down ? mtConfig.values.padsLightFront : 0, pad);
    if (down)
    {
        sp::noteOn(sp::kPad, 0, note, sp::value[sp::kFixedVel]);   // the pads report no velocity: the Fixed vel setting
    }
    else
    {
        sp::noteOff(sp::kPad, 0, note);
    }
}

//=============================================================================================================
// REC: a take of the master output into the sample memory (ref/synth-ab-rec.md)
//=============================================================================================================
#ifdef FEATURE_SYNTH_REC
void cSynthPlay::recPress()
{
    if (rec == kRecTake)
    {
        endTake();
    }
    else if (rec == kRecIdle)
    {
        startTake();
    }
}

// A page key: while a take runs it stops and lands first, the key is repeated when that is done. True: leave now
bool cSynthPlay::leaveKey(uint8_t button)
{
    if (rec == kRecIdle)
    {
        return true;
    }
    recPendingKey = button;
    if (rec == kRecTake)
    {
        endTake();
    }
    if (rec == kRecIdle)
    {
        recPendingKey = 0xFF;
        return true;
    }
    return false;
}

// The take goes where the sample of a new instrument is packed: right after the samples of the used slots, and the slot is the first one after
// the last used instrument. A used slot is never touched, no sample moves and nothing is reloaded.
void cSynthPlay::startTake()
{
    int last = -1;
    for (int i = 0; i < kSampleInstrumentsCount; i++)
    {
        if (mtProject.instrument[i].isActive)
        {
            last = i;
        }
    }
    if (last + 1 >= kSampleInstrumentsCount)
    {
        mtPopups.show(2, "No free slot after", "the last instrument.");
        return;
    }
    const uint32_t used = newFileManager.getSampleMemorySize(0, kSampleInstrumentsCount - 1);
    const uint32_t size = std::min<uint32_t>(mtProject.max_memory, SAMPLE_POOL_BYTES);
    if (used >= size || (size - used) / 2 < synthplayrec::kMinFrames)
    {
        mtPopups.show(2, "Sample memory full.");
        return;
    }
    if (newFileManager.getStatus() != fmIdle || newFileManager.getCurrentOperation() != fmNoOperation)
    {
        mtPopups.show(2, "Card busy, try again.");
        return;
    }
    recSlot = uint8_t(last + 1);
    recStartBytes = used;
    recFull = false;
    recQueued = false;
    recShownSec = 0xFFFF;
    rec = kRecTake;
    synthplayrec::begin(externalRamSampleMemory + used / 2, (size - used) / 2);
    showRecTitle();
    display.synchronizeRefresh();
}

// Stop: the tail below -60 dBFS goes, the rest is written to the workspace as the instrument of recSlot
void cSynthPlay::endTake()
{
    recFull = synthplayrec::state() == synthplayrec::kFull;
    const uint32_t n = synthplayrec::end();
    recFrames = synthplayrec::trimSilence(externalRamSampleMemory + recStartBytes / 2, n);
    if (recFrames == 0)
    {
        recFinish("Nothing recorded.", nullptr);
        return;
    }
    rec = kRecLanding;
    recQueued = false;
    recWaitMs = platform->getTimer()->millis();
    ip->blockAllInputs();   // the write takes seconds for a long take
    showRecTitle();
    display.synchronizeRefresh();
    recUpdate();
}

// True: the write is queued. False: the file manager is busy (recUpdate tries again) or the take is lost (recFinish ran). The pool must still look as at the start
bool cSynthPlay::landTake()
{
    if (mtProject.instrument[recSlot].isActive || newFileManager.getSampleMemorySize(0, kSampleInstrumentsCount - 1) != recStartBytes)
    {
        recFinish("Take lost.", "The samples changed.");
        return false;
    }
    int16_t* take = externalRamSampleMemory + recStartBytes / 2;
    if (!newFileManager.importSampleFromSampleEditor(take, take, recFrames, recSlot))
    {
        return false;   // the file manager is busy: recUpdate tries again
    }
    // importSampleFromSampleEditor made the slot a default active instrument: the sample is where the take is
    int n = 1;
    for (int i = 0; i < kSampleInstrumentsCount; i++)
    {
        const char* name = mtProject.instrument[i].sample.file_name;
        if (i != recSlot && mtProject.instrument[i].isActive && strncmp(name, "AB_", 3) == 0 && atoi(name + 3) >= n)
        {
            n = atoi(name + 3) + 1;
        }
    }
    strInstrument& in = mtProject.instrument[recSlot];
    snprintf(in.sample.file_name, SAMPLE_NAME_SIZE, "AB_%03d", n);
    in.sample.address = take;
    in.sample.length = recFrames;
    voiceStreamManager.clearInstrumentMetadata(recSlot);
    mtProject.sampleMemory[recSlot] = strSampleMemory();
    mtProject.sampleMemory[recSlot].address[strSampleMemory::kLeft] = take;
    mtProject.sampleMemory[recSlot].address[strSampleMemory::kRight] = take;
    mtProject.sampleMemory[recSlot].ramFrames = recFrames;
    mtProject.sampleMemory[recSlot].preCacheFrames = recFrames;
    mtProject.used_memory = newFileManager.getSampleMemorySize(0, kSampleInstrumentsCount - 1);
    snprintf(s_recMsg, sizeof(s_recMsg), "%s in slot %d", in.sample.file_name, recSlot + 1);
    recQueued = true;
    return true;
}

// The popup of the finished take. The file manager's progress popup is still up on a landed take, and its end hides every popup,
// so it waits until that is gone (the next update)
void cSynthPlay::recShowPopup()
{
    if (recPopup1 == nullptr || mtInterface.fileManagerPopupUp())
    {
        return;
    }
    if (recPopup2)
    {
        mtPopups.show(2, recPopup1, recPopup2);
    }
    else
    {
        mtPopups.show(2, recPopup1);
    }
    recPopup1 = recPopup2 = nullptr;
}

void cSynthPlay::recUpdate()
{
    if (rec == kRecTake)
    {
        if (synthplayrec::state() == synthplayrec::kFull)
        {
            endTake();   // the pool end: what is there lands
            return;
        }
        if (synthplayrec::frames() / kCodecRate != recShownSec)
        {
            showRecTitle();
            display.synchronizeRefresh();
        }
        return;
    }
    if (rec != kRecLanding)
    {
        return;
    }
    if (!recQueued)
    {
        if (!landTake() && rec == kRecLanding)
        {
            if (platform->getTimer()->millis() - recWaitMs > kLandWaitMs)
            {
                recFinish("Card busy.", "The take is dropped.");
            }
        }
        return;
    }
    const uint8_t st = newFileManager.getStatus();
    if (st == fmImportSampleFromSampleEditorEnd)
    {
        newFileManager.clearStatus();
        recFinish(recFull ? "Sample memory full." : "Take saved.", s_recMsg);
    }
    else if (st >= fmError)
    {
        newFileManager.clearStatus();
        newFileManager.completeLoadedInstrumentStruct(recSlot, false);   // the slot is free again
        mtProject.sampleMemory[recSlot] = strSampleMemory();
        mtProject.used_memory = newFileManager.getSampleMemorySize(0, kSampleInstrumentsCount - 1);
        newFileManager.setInstrumentStructChanged(recSlot);
        recFinish("Take not saved.", "Card error.");
    }
}

// The end of a take, saved or not: the page is as before, the popup says what happened, a page key pressed meanwhile goes on
void cSynthPlay::recFinish(const char* line1, const char* line2)
{
    const bool blocked = rec == kRecLanding;
    rec = kRecIdle;
    recQueued = false;
    if (blocked)
    {
        ip->unblockAllInputs();
    }
    showTitle();
    recPopup1 = line1;
    recPopup2 = line2;
    recShowPopup();
    if (recPendingKey != 0xFF)
    {
        uint8_t button = recPendingKey;
        recPendingKey = 0xFF;
        leavePage(button);
    }
}

// "REC 0:07 left 1:52" while the take runs, "Saving take" while it is written
void cSynthPlay::showRecTitle()
{
    if (rec == kRecTake)
    {
        const uint32_t f = synthplayrec::frames();
        recShownSec = uint16_t(f / kCodecRate);
        const uint32_t left = (synthplayrec::capacity() - f) / kCodecRate;
        snprintf(s_t.title, sizeof(s_t.title), "REC %u:%02u left %u:%02u", unsigned(recShownSec / 60), unsigned(recShownSec % 60), unsigned(left / 60 % 10),
                 unsigned(left % 60));
    }
    else
    {
        snprintf(s_t.title, sizeof(s_t.title), "Saving take");
    }
    titleBar->setTitle(s_t.title);
}
#endif // FEATURE_SYNTH_REC

void cSynthPlay::confirmStop()
{
    sequencer.stop();
    confirming = false;
    mtPopups.hideInfoPopup();
    enter();
    setFunctions();
    showAll();
}

static void functColumn(uint8_t button)
{
    SPL->pressColumn(button - interfaceButton0);
}

static void functLeft()
{
    SPL->selectColumn(SPL->column() - 1);
}

static void functRight()
{
    SPL->selectColumn(SPL->column() + 1);
}

static void functUp()
{
    SPL->changeValue(1);
}

static void functDown()
{
    SPL->changeValue(-1);
}

static void functEncoder(int16_t value)
{
    SPL->changeValue(value);
}

static void functDelete()
{
    if (shiftDown())
    {
        SPL->panic();
    }
}

static void functNothing() {}

static void functPads(uint8_t pad, uint8_t state, int16_t)
{
    if (state == buttonPress || state == buttonRelease)
    {
        SPL->padNote(pad, state == buttonPress);
    }
}

static void functSwitchModule(uint8_t button)
{
    // A running REC take is stopped and landed first, the key is repeated through leavePage() when that is done
    if (SPL->leaveKey(button))
    {
        leavePage(button);
    }
}

// The page key itself, shared by the direct press and the key repeated after a take has landed
static void leavePage(uint8_t button)
{
    if (button == interfaceButtonPerformance)
    {
        // The third page of the Perform key: back to Performance page 1 (options 1), its FX and track states are still there when it was left from there
        uint32_t page1 = 1;
        SPL->eventFunct(eventSwitchModule, SPL, &button, &page1);
        return;
    }
    // The page was opened from Performance mode (Perform on page 2) and that mode is still on: the pages that do not keep it turn it off, as on the Params page
    if (!performanceMode.canPerformanceModeContinueAfterSwitchingTo(button))
    {
        performanceMode.turnOffAtEndOfPattern();
    }
    SPL->eventFunct(eventSwitchModule, SPL, &button, 0);
}

static void functRec()
{
    SPL->recPress();
}

#ifdef TRACKER_MINI
static void functMiniNote(uint8_t state)
{
    constexpr uint8_t kMiniNote = 60;   // C4, the note of the preview pad (mtPadBoard getNoteFromPad), the router adds Transpose
    if (state == buttonPress)
    {
        sp::noteOn(sp::kPad, 0, kMiniNote, uint8_t(sp::value[sp::kFixedVel]));
    }
    else if (state == buttonRelease)
    {
        sp::noteOff(sp::kPad, 0, kMiniNote);
    }
}

static void functMiniMenu(uint8_t state)
{
    if (state == buttonPress)
    {
        mtMenuPopup.showModulePopup(SPL);
    }
    else if (state == buttonRelease && mtMenuPopup.isPopupVisible())
    {
        mtMenuPopup.hideModulePopup(SPL);
    }
}
#endif

static void functConfirmCancel()
{
    SPL->eventFunct(eventSwitchToPreviousModule, SPL, 0, 0);
}

static void functConfirmStop()
{
    SPL->confirmStop();
}

#ifdef UI_SHOT_HOST
// ui_shot only (host_poke "paircap"): CAP >= 0 makes the row of STORED in kPairCap read CAP, CAP -1 clears the override, CAP -2 only reads.
// Returns the cap the page reads for STORED
int synthPlayPairCapSim(int stored, int cap)
{
    if (cap >= 0)
    {
        s_pairCapSimStored = stored;
        s_pairCapSimCap = cap;
    }
    else if (cap == -1)
    {
        s_pairCapSimStored = 0xFF;
    }
    return pairCapOf(uint32_t(stored));
}
#endif

// The router's gain of a part (the morph)
void synthPlaySetGain(uint8_t part, float gain)
{
    part::gain(part, gain);
}
// The router's MPE calls (ref/mpe-in-p1.md)
void synthPlayMpeNote(uint8_t chan)
{
    part::mpeNote(chan);
}
void synthPlayMpeBend(uint8_t p, uint8_t chan, int raw)
{
    part::mpeBend(p, chan, raw);
}
void synthPlayMpePress(uint8_t p, uint8_t chan, uint8_t press)
{
    part::mpePress(p, chan, press);
}
void synthPlayMpeConfig(uint8_t p, int range, int pressPercent)
{
    part::mpeConfig(p, range, pressPercent);
}

#ifndef FEATURE_SYNTH_OPEN
int cSynthPlay::voicesOf(int part) const
{
    if (!entered)
    {
        return 0;
    }
    return part == sp::value[sp::kPart] ? sideVoices[0] : (part >= 0 && part == partB() ? sideVoices[1] : 0);
}

int part::sounding(int p)
{
    if (!synthPlay.isOpen() || p < 0)
    {
        return 0;
    }
    const int voices = synthPlay.voicesOf(p);
    const int held = sp::sounding();
    return held < voices ? held : voices;
}
#endif

#endif // FEATURE_SYNTH_OPEN || FEATURE_SYNTH_PATCHFX
