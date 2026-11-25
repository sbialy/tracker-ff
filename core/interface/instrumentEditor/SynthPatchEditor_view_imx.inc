// SynthPatchEditor.cpp, included where this block was: Tracker+ and Mini (P6) have the insert host without the synth engine plugins, no engine has a cycle view
#if defined(FEATURE_PLUG_HOST) && !defined(FEATURE_SYNTH_OPEN)
// Tracker+ and Mini (P6): the insert host without the synth engine plugins, so no engine has a cycle view and the control stays hidden
void cInstrumentEditor::initSynthView()
{
    s_viewData.init(kViewPoints, strTrackerSpectrum::PointsLimit::k200);
    s_viewData.spectrumType = 1;
}

void cInstrumentEditor::showSynthView()
{
    if (synthViewControl >= 0 && s_viewShown)
    {
        display.setControlHide(synthViewControl);
        display.synchronizeRefresh();
    }
    s_viewShown = false;
}

void synthViewTick()
{
}
#endif
