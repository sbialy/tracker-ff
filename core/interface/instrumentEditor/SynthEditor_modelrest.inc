// Model list rest before a plugin engine installs (SynthEditor.cpp, included where this block was): stepping onto an engine only moves
// the list, synthModelTick (main loop) picks it once the list has rested. Without the plugin host synthModelPendClear is empty.
#if defined(FEATURE_PLUG_HOST) && defined(FEATURE_SYNTH_OPEN)   // synth engine plugins (MK66 bank), not the insert host
// The model list over plugin engines that are not installed: stepping onto one only moves the list, the engine is
// installed and picked once the list rests there for kModelRestMs (synthModelTick), so scrolling past engines writes no flash.
namespace
{
// One second, not half: every install erases and programs 3 or more 4 KB FlexNVM sectors (rated 10,000 cycles) and can glitch
// the audio, so a longer rest means fewer installs while scrolling, less flash wear and fewer install glitches.
// Only this Model-list pick uses it, a project load still installs at once.
constexpr uint32_t kModelRestMs = 1000;
struct PendingModel
{
    int part = -1;
    int ui = -1;
};
PendingModel s_pendModel;
bool s_applyModel;     // synthModelTick applies the pending one: no new wait
bool s_initMsgUp;      // initCurrentPatch raised its message and nothing took it down since
elapsedMillis s_pendTimer;
} // namespace

static void synthModelPendClear()
{
    s_pendModel = {};
}

// Main loop (Tracker.cpp, after plug::tick)
void synthModelTick()
{
    if (s_pendModel.ui < 0 || s_pendTimer < kModelRestMs)
    {
        return;
    }
    const int part = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
    const PendingModel p = s_pendModel;
    s_pendModel = {};
    if (part != p.part || IE->mode != mtInstEditModeSynth || !mtInterface.isInstrumentParamsScreenActive())
    {
        return; // the screen or the part changed while the list moved: nothing is picked
    }
    const int cur = synthPartPos(part);
    s_applyModel = true;
    IE->changeSynthModel(int16_t(synthViewPosOf(p.ui) - cur)); // steps are rows of the list
    s_applyModel = false;
}
#elif defined(FEATURE_SYNTH_OPEN)
static void synthModelPendClear() {}
#endif

// initCurrentPatch raised its "Init patch loaded" message (the next step of the list takes it down)
static inline __attribute__((always_inline)) void ffSeInitMsgRaised()
{
#if defined(FEATURE_PLUG_HOST) && defined(FEATURE_SYNTH_OPEN)   // synth engine plugins (MK66 bank), not the insert host
    s_initMsgUp = true;
#endif
}
