// configMenu.cpp, included where this block was: the Project Settings page of the File screen (FEATURE_FF_CORE), its rows
#ifdef FEATURE_FF_CORE
static const char* const fxSlotsDesc =
  "Gives every pattern step two more FX slots, FX3 and FX4. Off keeps their data on the card but does not play it. "
  "Not compatible with stock CE and firmware 1.3.x, which play slots 1 and 2 only. Only while the sequencer is stopped.";
static const char* const strumVolTexts[11] = { "0 %", "10 %", "20 %", "30 %", "40 %", "50 %", "60 %", "70 %", "80 %", "90 %", "100 %" };
static const char* const strumVolDesc =
  "How much the notes of a strummed chord fall in volume, in the order they sound. 0 % is an even strum, 100 % the full ramp. "
  "The Strum FX keeps its own speed and direction.";
const strItemTypeListTextWithAction setupFxSlots{ &projFxSlots, 2, onOffStateTexts, setProjFxSlots };
cMenuItem melFxSlots(menuProjectPage, "4 FX Slots", fxSlotsDesc, menuTypeItemListTextWithAction, &setupFxSlots);
#ifdef FEATURE_SYNTH_OPEN
static const char* const macroModeTexts[3] = { "Per patch", "Add", "Scale" };
static const char* const macroModeDesc =
  "The macro depth mode of a synth patch that is added to the project: a new patch, an Init after a Model change and a patch loaded "
  "from the library. Add moves a parameter by the Amount of its whole range, Scale by the room to its end. "
  "Per patch keeps what each patch does today. Patches already in the project never change.";
const strItemTypeListTextWithAction setupMacroMode{ &projMacroMode, 3, macroModeTexts, setProjMacroMode };
cMenuItem melMacroMode(menuProjectPage, "Macro depth", macroModeDesc, menuTypeItemListTextWithAction, &setupMacroMode);
#endif
const strItemTypeListTextWithAction setupStrumVolume{ &projStrumVolume, 11, strumVolTexts, setProjStrumVolume };
cMenuItem melStrumVolume(menuProjectPage, "Strum volume", strumVolDesc, menuTypeItemListTextWithAction, &setupStrumVolume);
// seeded random takes (mtSequencer.h, seqRandom.h): 0 shows Off, 1 to 255 are the seeds
const strItemTypeListValues setupRandomSeed{ &sequencer.randomSeed, 0, 255, 1, 0, setRandomSeedValue, "Off" };
const strItemTypeActionButton setupRerollSeed{ rerollRandomSeedAction, nullptr };
cMenuItem melRandomSeed(menuProjectPage, "Random seed", randomSeedDesc, menuTypeItemListValues, &setupRandomSeed);
cMenuItem melRerollSeed(menuProjectPage, "Reroll seed", rerollSeedDesc, menuTypeItemActionButton, &setupRerollSeed);

// Scale snap: per track, played notes move to the Notes scale and Root note of Config > Project Settings (the pattern keeps what was written)
cMenuGroup menuScaleSnap(menuProjectPage, "Scale snap", kScaleSnapTracks);
static const char* const scaleSnapTexts[kSnapModeCount] = { "Off", "Nearest", "Down", "Up" };
static const char* const scaleSnapDesc =
  "Notes of this track are moved to the Notes scale and Root note of Config > Project Settings while they play. "
  "Nearest takes the closest scale note (a tie goes down), Down the one at or below, Up the one at or above. "
  "The pattern keeps the notes you wrote. The Chromatic scale changes nothing.";
const strItemTypeListTextWithAction scaleSnapSetup[kScaleSnapTracks] = {
    { &scaleSnapMode[0], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[1], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[2], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[3], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[4], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[5], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[6], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[7], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[8], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[9], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[10], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[11], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[12], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[13], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[14], kSnapModeCount, scaleSnapTexts, setScaleSnap },
    { &scaleSnapMode[15], kSnapModeCount, scaleSnapTexts, setScaleSnap },
};
cMenuItem melScaleSnap1(menuScaleSnap, "Track 1", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[0]);
cMenuItem melScaleSnap2(menuScaleSnap, "Track 2", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[1]);
cMenuItem melScaleSnap3(menuScaleSnap, "Track 3", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[2]);
cMenuItem melScaleSnap4(menuScaleSnap, "Track 4", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[3]);
cMenuItem melScaleSnap5(menuScaleSnap, "Track 5", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[4]);
cMenuItem melScaleSnap6(menuScaleSnap, "Track 6", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[5]);
cMenuItem melScaleSnap7(menuScaleSnap, "Track 7", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[6]);
cMenuItem melScaleSnap8(menuScaleSnap, "Track 8", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[7]);
cMenuItem melScaleSnap9(menuScaleSnap, "Track 9", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[8]);
cMenuItem melScaleSnap10(menuScaleSnap, "Track 10", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[9]);
cMenuItem melScaleSnap11(menuScaleSnap, "Track 11", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[10]);
cMenuItem melScaleSnap12(menuScaleSnap, "Track 12", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[11]);
cMenuItem melScaleSnap13(menuScaleSnap, "Track 13", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[12]);
cMenuItem melScaleSnap14(menuScaleSnap, "Track 14", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[13]);
cMenuItem melScaleSnap15(menuScaleSnap, "Track 15", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[14]);
cMenuItem melScaleSnap16(menuScaleSnap, "Track 16", scaleSnapDesc, menuTypeItemListTextWithAction, &scaleSnapSetup[15]);

#ifndef PLATFORM_IMX
// Stereo pairs: tracks 1+2 3+4 5+6 7+8 play as one stereo voice (the follower column is dimmed, it plays nothing)
cMenuGroup menuStereoPairs(menuProjectPage, "Stereo pairs", 4);
static const char* const pairTexts[2] = { "Mono", "Stereo" };
static const char* const pairDesc =
  "Links two tracks to one stereo voice: the odd track plays a stereo sample on both outputs, the even track stays silent and keeps "
  "its pattern data. Mixer, mute and Performance act on the pair as one. Only while the sequencer is stopped.";
const strItemTypeListTextWithAction pairSetup[4] = {
    { &projPair[0], 2, pairTexts, setProjPair },
    { &projPair[1], 2, pairTexts, setProjPair },
    { &projPair[2], 2, pairTexts, setProjPair },
    { &projPair[3], 2, pairTexts, setProjPair },
};
cMenuItem melPair1(menuStereoPairs, "Tracks 1+2", pairDesc, menuTypeItemListTextWithAction, &pairSetup[0]);
cMenuItem melPair2(menuStereoPairs, "Tracks 3+4", pairDesc, menuTypeItemListTextWithAction, &pairSetup[1]);
cMenuItem melPair3(menuStereoPairs, "Tracks 5+6", pairDesc, menuTypeItemListTextWithAction, &pairSetup[2]);
cMenuItem melPair4(menuStereoPairs, "Tracks 7+8", pairDesc, menuTypeItemListTextWithAction, &pairSetup[3]);
#endif

// Tuning (core/source/mtTuning.h, project.mtff tune.*): a tuning of N notes per period for samples and synth parts. The default (12, 2/1, key 60,
// 261.6256 Hz) is today's pitch and is not stored. With N not 12 the note names are degrees, Scale snap works in N degrees and the pads play every key
cMenuGroup menuTuning(menuProjectPage, "Tuning", 14);
static const char* const tunePeriodTexts[8] = { "2/1 1200", "3/1 1902", "3/2 702", "4/1 2400", "5/2 1586", "1205", "1210", "Custom" };
static const char* const tuneHzTexts[9] = { "261.6256", "264.0000", "256.0000", "256.8687", "220.0000", "432.0000", "440.0000", "442.0000", "Custom" };
static const char* const tuneNDesc =
  "Notes per period, 1 to 128. 12 is the usual tuning. 19, 22, 24 or 31 give an equal division of the octave with that many keys: pads, "
  "MIDI keys and pattern notes play the next degree on the next key. Applies to sample and synth tracks (PERC and T-Type stay as they are). "
  "Only while the sequencer is stopped. Not 12: note names show the degree and octave (17.2), Scale snap works in the degrees of the tuning.";
static const char* const tunePeriodDesc =
  "The size of the period in cents: 2/1 is the octave (1200). 3/1 (1902) with 13 notes is Bohlen-Pierce. Custom is a value that is not in "
  "the list: set it with the four Period rows below (1901.955 is 19, 1, 95, 5). Only while the sequencer is stopped.";
static const char* const tunePerDesc =
  "One digit group of the period in cents: 100 cents (0 to 48), cents (0 to 99), 0.01 cent (0 to 99), 0.001 cent (0 to 9). The period is the sum, "
  "1901.955 is 19, 1, 95, 5. 10 cents is the least and 4800 the most. Only while the sequencer is stopped.";
static const char* const tuneHzFineDesc =
  "One digit group of the 1/1 frequency in Hz: 100 Hz (0 to 40), Hz (0 to 99), 0.01 Hz (0 to 99), 0.0001 Hz (0 to 99). The frequency is the sum, "
  "264.0000 Hz is 2, 64, 0, 0. 8 Hz is the least and 4000 Hz the most. Only while the sequencer is stopped.";
static const char* const tuneRootDesc =
  "The key that holds the 1/1: the key that plays the 1/1 Hz below, and degree 0 of the names. 60 is the middle key. "
  "A project in a tuning shows 128 keys, move this to choose where the window of the period sits. Only while the sequencer is stopped.";
static const char* const tuneHzDesc =
  "The frequency of the 1/1 in Hz. 261.6256 is the pitch of key 60 today, so the default leaves every sound as it is. "
  "Sample tracks play a sample at its recorded pitch on the 1/1 key when this is 261.6256. Custom is a value that is not in the list: set it "
  "with the four Hz rows below. Only while the sequencer is stopped.";
const strItemTypeListValues setupTuneN{ &projTuneN, 0, 127, 1, 1, setProjTune };
const strItemTypeListTextWithAction setupTunePeriod{ &projTunePeriod, 8, tunePeriodTexts, setProjTune };
const strItemTypeListValues setupTuneRoot{ &projTuneRoot, 0, 127, 1, 0, setProjTune };
const strItemTypeListTextWithAction setupTuneHz{ &projTuneHz, 9, tuneHzTexts, setProjTune };
cMenuItem melTuneN(menuTuning, "Notes/period", tuneNDesc, menuTypeItemListValues, &setupTuneN);
cMenuItem melTunePeriod(menuTuning, "Period", tunePeriodDesc, menuTypeItemListTextWithAction, &setupTunePeriod);
cMenuItem melTuneRoot(menuTuning, "1/1 key", tuneRootDesc, menuTypeItemListValues, &setupTuneRoot);
cMenuItem melTuneHz(menuTuning, "1/1 Hz", tuneHzDesc, menuTypeItemListTextWithAction, &setupTuneHz);
const strItemTypeListValues setupPerH{ &projPerH, 0, 48, 1, 0, setProjTune };
const strItemTypeListValues setupPerC{ &projPerC, 0, 99, 1, 0, setProjTune };
const strItemTypeListValues setupPerD{ &projPerD, 0, 99, 1, 0, setProjTune };
const strItemTypeListValues setupPerM{ &projPerM, 0, 9, 1, 0, setProjTune };
const strItemTypeListValues setupHzH{ &projHzH, 0, 40, 1, 0, setProjTune };
const strItemTypeListValues setupHzU{ &projHzU, 0, 99, 1, 0, setProjTune };
const strItemTypeListValues setupHzD{ &projHzD, 0, 99, 1, 0, setProjTune };
const strItemTypeListValues setupHzE{ &projHzE, 0, 99, 1, 0, setProjTune };
cMenuItem melPerH(menuTuning, "Period 100c", tunePerDesc, menuTypeItemListValues, &setupPerH);
cMenuItem melPerC(menuTuning, "Period 1c", tunePerDesc, menuTypeItemListValues, &setupPerC);
cMenuItem melPerD(menuTuning, "Period .01c", tunePerDesc, menuTypeItemListValues, &setupPerD);
cMenuItem melPerM(menuTuning, "Period .001c", tunePerDesc, menuTypeItemListValues, &setupPerM);
cMenuItem melHzH(menuTuning, "Hz 100", tuneHzFineDesc, menuTypeItemListValues, &setupHzH);
cMenuItem melHzU(menuTuning, "Hz 1", tuneHzFineDesc, menuTypeItemListValues, &setupHzU);
cMenuItem melHzD(menuTuning, "Hz .01", tuneHzFineDesc, menuTypeItemListValues, &setupHzD);
cMenuItem melHzE(menuTuning, "Hz .0001", tuneHzFineDesc, menuTypeItemListValues, &setupHzE);
// Scale file: a Scala .scl of the card (/Tunings) as the tuning. The list is filled when the page opens (mtTuneScale.cpp), the count follows it
static const char* const tuneFileDesc =
  "A Scala tuning file (.scl) from the card folder /Tunings: just intonation, Bohlen-Pierce or any scale of up to 128 notes. Pick EDO to go back "
  "to equal steps. The file's note count and period replace Notes/period and Period, the 1/1 key and 1/1 Hz stay. The project keeps a copy of the "
  "scale, so it opens on a card without the file. Only while the sequencer is stopped. A file that is not a valid Scala scale is refused with the reason.";
static strItemTypeListTextWithAction setupTuneFile{ &projTuneFile, 1, tunescale::list(), setProjTuneFile };
cMenuItem melTuneFile(menuTuning, "Scale file", tuneFileDesc, menuTypeItemListTextWithAction, &setupTuneFile);
void tuneFileListSync() { setupTuneFile.count = uint8_t(tunescale::entries()); }
// Note names (phase 3, mtTuning.h noteNameUd): degrees (17.2) or ups and downs (^C#4) for an octave EDO whose fifth names every degree
static const char* const tuneNamesTexts[2] = { "Degrees", "Ups+downs" };
static const char* const tuneNamesDesc =
  "How the notes of a tuning are named. Degrees: the degree and the octave (17.2), for every tuning. Ups+downs: C D E F G A B from the fifth of the "
  "tuning, # and b for the sharp and flat, ^ one step up and v one step down (^C#4, vDb2). Only for an equal division of the octave with a fifth that "
  "names every degree (17, 19, 22, 24, 26, 27, 29, 31, 33, 34, 36, 38, 39, 41 and a few more). A Scala file keeps degrees.";
static const strItemTypeListTextWithAction setupTuneNames{ &projTuneNames, 2, tuneNamesTexts, setProjTuneNames };
cMenuItem melTuneNames(menuTuning, "Note names", tuneNamesDesc, menuTypeItemListTextWithAction, &setupTuneNames);

// MPE out (core/source/mtMpeOut.h, project.mtff mpe.out): the MIDI instruments play through one MPE lower zone, master channel 1 and this many member
// channels 2 and up, one per sounding note, with its own pitch bend. The last row of the page
static const char* const mpeOutDesc =
  "Off sends the MIDI instruments as always, one channel each. A number puts them in one MPE zone: master channel 1 and that many member channels from 2, "
  "a channel per sounding note, so every note of a chord has its own pitch bend (a tuning plays in tune on an MPE synth). The instrument "
  "channels no longer pick the wire channel. The receiver must be MPE, pitch bend range 48.";
const strItemTypeListValues setupMpeOut{ &projMpeOut, 0, 15, 1, 0, setProjMpeOut, "Off" };
cMenuItem melMpeOut(menuProjectPage, "MPE out", mpeOutDesc, menuTypeItemListValues, &setupMpeOut);

// the value column of the Scale snap, Stereo pairs and Tuning rows: how many tracks snap, how many pairs are linked, the tuning
static const char* projectPageRowValue(uint8_t row)
{
    static char text[16];
    // rows from the end: MPE out, Tuning, then Stereo pairs (MK66 only), then Scale snap
    constexpr uint8_t kSnapRow = projectPageRows - 3 - kPairRows;
    if (row == projectPageRows - 2)
    {   // Tuning
        if (tuning::cfg.file)
        {   // the Scala file: its name without the extension, 15 characters at most
            snprintf(text, sizeof(text), "%s", tuning::fileName[0] ? tuning::fileName : "Scala");
            char* dot = strrchr(text, '.');
            if (dot) *dot = 0;
            return text;
        }
        if (tuning::isDefault()) return "12 EDO";
        snprintf(text, sizeof(text), tuning::cfg.periodMc == tuning::kDefPeriodMc ? "%d EDO" : "%d/period", int(tuning::cfg.n));
        return text;
    }
    if (row == kSnapRow && tuning::snapLocked()) return "Off (tuned)";   // Scale snap is locked for a Scala file (a tuning of N notes snaps in N degrees)
#ifndef PLATFORM_IMX
    if (row == projectPageRows - 3)
    {   // Stereo pairs: how many pairs are linked
        int p = 0;
        for (int k = 0; k < 4; ++k) p += stereopairs::pairLinked(k);
        if (!p) return "Mono";
        snprintf(text, sizeof(text), "%d linked", p);
        return text;
    }
#endif
    if (row != kSnapRow) return nullptr;
    int n = 0;
    for (uint8_t t = 0; t < kScaleSnapTracks; t++) n += scaleSnapMode[t] != 0;
    if (!n) return "Off";
    snprintf(text, sizeof(text), "%d tracks", n);
    return text;
}

// Config start, before the lists are made: Config hides the page (the last group of the base list), the page shows only itself. The base
// list and the page group keep their selection while the page is open, so Config opens where it was left
void cConfigEditor::projectPageSetup()
{
    static uint8_t configGroup = 0;   // the base list selection of Config, while the page has it
    constexpr uint8_t page = baseMenuGroupCount - 1;
    const uint8_t shown = page - usbMenuSync();   // the groups Config lists: the USB entry is not one of them while it is hidden
    if (menuBase.getCount() == baseMenuGroupCount) menuBase.selectedItem = configGroup < shown ? configGroup : 0;   // the page was open
    menuBase.setCount(shown);
    menuProjectPage.childValue = projectPageRowValue;
    if (!projectPage) return;
    configGroup = menuBase.selectedItem;
    tunescale::scan();
    tuneFileListSync();
    projectPageSync();
    menuBase.setCount(baseMenuGroupCount);
    menuBase.selectedItem = page;
    menuProjectPage.selectedItem = 0;
}
#endif
