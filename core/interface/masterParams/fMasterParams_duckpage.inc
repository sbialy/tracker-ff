// fMasterParams.cpp, included where this block was: button 5 of the limiter screen toggles the send ducking page (FEATURE_SEND_DUCK)
#ifdef FEATURE_SEND_DUCK
static void functToggleDuckPage()
{
    MP->duckPage = !MP->duckPage;
    MP->selectedPlaceLimiter = 0;
    MP->switchToLimiterScreen();
}
#endif
