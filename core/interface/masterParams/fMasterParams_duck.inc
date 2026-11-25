// fMasterParams.cpp, included where this block was: the send ducking page of the limiter screen (FEATURE_SEND_DUCK), its value edits
#ifdef FEATURE_SEND_DUCK
static void duckEdit(int field, int16_t value, bool toDefault)
{
    DuckParams d = engine.getSendDuck();
    uint8_t* const f[5] = { &d.depth, &d.release, &d.thresh, nullptr, &d.source };
    const uint8_t dflt[5] = { kDuckDefaults.depth, kDuckDefaults.release, kDuckDefaults.thresh, 0, kDuckDefaults.source };
    const int hi = field == 4 ? kDuckSourceMax : kDuckPctMax;
    if (field < 0 || field > 4 || !f[field])
        return;
    *f[field] = toDefault ? dflt[field] : (uint8_t)clamp<int>((int)*f[field] + value, 0, hi);
    plug::duckSet(d); // the engine now, the sidecar at the next save, the project marked changed
    switch (field)
    {
        case 0: MP->showDuckDepth(); break;
        case 1: MP->showDuckRelease(); break;
        case 2: MP->showDuckThreshold(); break;
        default: MP->showDuckSource(); break;
    }
}

void cMasterParams::duckMeterHandler()
{
    static elapsedMillis meterTimer;
    if (meterTimer < 60)
        return;
    meterTimer = 0;
    // how far the returns are down now, in percent of full scale
    const float down = (1.0f - engine.getSendDuckGain()) * 100.0f;
    const uint8_t v = (uint8_t)clamp<int>((int)(down + 0.5f), 0, 100);
    if (v != duckMeterValue)
    {
        duckMeterValue = v;
        showDuckMeter();
        display.synchronizeRefresh();
    }
}
#endif
