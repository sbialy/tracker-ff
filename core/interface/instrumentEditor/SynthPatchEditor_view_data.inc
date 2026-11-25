// SynthPatchEditor.cpp, included where this block was: the data of the cycle view of a plugin engine (FEATURE_PLUG_HOST) and the hook that shows it
#ifdef FEATURE_PLUG_HOST
// The cycle view of a plugin engine with plug_ext_synth.view (ABI 1.4): a 200 x 50 box at x 184, y 41, right of the model logo (noteLabel,
// 50..158 x 45..85, same vertical centre) and above the section grid (its first row of icons starts at y 113), inside the left half that
// the grid and the logo use. The right half keeps the parameter list and the value column untouched. An engine without view: hidden.
// The view's -127..127 goes to the spectrum control's +-109 steps, its full height for any box height (spectrum.cpp: height / 218 per step).
// The data (1.2 KB) is in the SDRAM like the sample editor's, the control object is created with the editor's other controls.
namespace
{
constexpr int kViewPoints = 200;
constexpr uint32_t kViewMs = 50;   // synthViewTick: a new view at most this often (encoder edits, the Patch FX, a wave the engine loaded)
__NOINIT(EXTERNAL_RAM) strTrackerSpectrum s_viewData;
__NOINIT(EXTERNAL_RAM) int8_t s_viewNew[kViewPoints];
bool s_viewShown;
} // namespace
#endif

// showSynthPatchEditorScreen, the page is up: the cycle view is drawn again whatever the data holds
static inline __attribute__((always_inline)) void ffSpeViewShow(cInstrumentEditor* ed)
{
#ifdef FEATURE_PLUG_HOST
    ed->synthViewOn = true;
    s_viewShown = false;
    ed->showSynthView();
#else
    (void)ed;
#endif
}
