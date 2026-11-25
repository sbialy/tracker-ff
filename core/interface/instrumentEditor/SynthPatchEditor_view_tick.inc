// SynthPatchEditor.cpp, included where this block was: the cycle view of a plugin engine (plug_ext_synth.view), its tick in the main loop and its control (MK66 engine plugins)
#if defined(FEATURE_PLUG_HOST) && defined(FEATURE_SYNTH_OPEN)   // the cycle view needs the engine plugins (MK66), IMX: end of file
namespace
{
uint32_t s_viewMs;
extern "C" volatile uint32_t systick_millis_count;
} // namespace

void cInstrumentEditor::initSynthView()
{
    s_viewData.init(kViewPoints, strTrackerSpectrum::PointsLimit::k200);
    s_viewData.spectrumType = 1;   // a line through the points with a centre line
    strControlProperties prop;
    prop.x = 184;
    prop.y = 41;
    prop.w = kViewPoints;
    prop.h = 50;
    prop.data = &s_viewData;
    synthViewControl = display.createControl<cSpectrum>(&prop);
    synthViewOn = false;
    s_viewShown = false;
}

// The current view of the part's engine, drawn when it changed. Hidden when the engine has no view (a built-in engine, a 1.1 plugin)
void cInstrumentEditor::showSynthView()
{
    if (synthViewControl < 0)
    {
        return;
    }
    const int part = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
    const float* values = nullptr;
    int count = 0;
    const polysynth::open::EngineDef* d = polysynth::open::partPatch(part, &values, &count);
    if (!plug::synthView(d, values, count, s_viewNew, kViewPoints))
    {
        if (s_viewShown)
        {
            display.setControlHide(synthViewControl);
            display.synchronizeRefresh();
        }
        s_viewShown = false;
        return;
    }
    bool same = s_viewShown;
    for (int i = 0; i < kViewPoints; i++)
    {
        const int8_t v = int8_t(int(s_viewNew[i]) * 109 / 127);
        const int8_t up = v > 0 ? v : 0, lo = v < 0 ? v : 0;
        same = same && s_viewData.upperData[i] == up && s_viewData.lowerData[i] == lo;
        s_viewData.upperData[i] = up;
        s_viewData.lowerData[i] = lo;
    }
    if (same)
    {
        return;
    }
    display.setControlShow(synthViewControl);
    display.refreshControl(synthViewControl);
    display.synchronizeRefresh();
    s_viewShown = true;
}

// Main loop (Tracker.cpp, next to synthModelTick): keeps the view of the open patch editor page current
void synthViewTick()
{
    if (!IE->synthViewOn || IE->mode != mtInstEditModeSynthPatchEdit || !mtInterface.isInstrumentParamsScreenActive() ||
        systick_millis_count - s_viewMs < kViewMs)
    {
        return;
    }
    s_viewMs = systick_millis_count;
    IE->showSynthView();
}
#endif
