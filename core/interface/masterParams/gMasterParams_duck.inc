// gMasterParams.cpp, included where this block was: the send ducking page of the limiter screen (FEATURE_SEND_DUCK), its values and meter
#ifdef FEATURE_SEND_DUCK
// Send ducking page (a second page of the limiter screen, same places: 0 depth, 1 release, 2 threshold, 3 meter, 4 source)
void cMasterParams::showDuckDepth()
{
    const DuckParams& d = engine.getSendDuck();
    snprintf(duckDepthVal, sizeof(duckDepthVal), "%d%%", d.depth);
    display.setControlValue(barControl[0], d.depth);
    display.refreshControl(barControl[0]);
    display.setControlText2(label[0], duckDepthVal);
    display.refreshControl(label[0]);
}

void cMasterParams::showDuckRelease()
{
    const DuckParams& d = engine.getSendDuck();
    snprintf(duckReleaseVal, sizeof(duckReleaseVal), "%d ms", (int)(duckReleaseMs(d.release) + 0.5f));
    display.setControlValue(barControl[1], d.release);
    display.refreshControl(barControl[1]);
    display.setControlText2(label[1], duckReleaseVal);
    display.refreshControl(label[1]);
}

void cMasterParams::showDuckThreshold()
{
    const DuckParams& d = engine.getSendDuck();
    snprintf(duckThresholdVal, sizeof(duckThresholdVal), "%d dB", (int)lroundf(duckThresholdDb(d.thresh)));
    display.setControlValue(barControl[2], d.thresh);
    display.setControlShow(barControl[2]);
    display.refreshControl(barControl[2]);
    display.setControlText2(label[2], duckThresholdVal);
    display.refreshControl(label[2]);
}

void cMasterParams::showDuckMeter()
{
    display.setControlValue(barControl[3], duckMeterValue);
    display.setControlShow(barControl[3]);
    display.refreshControl(barControl[3]);
}

void cMasterParams::showDuckSource()
{
    const uint8_t src = engine.getSendDuck().source;
    display.setControlValue(limiterSidechainList, src);
    display.refreshControl(limiterSidechainList);

    display.setControlText2(label[4], limiterSidechainListTxt[src]);
    display.refreshControl(label[4]);
}
#endif
