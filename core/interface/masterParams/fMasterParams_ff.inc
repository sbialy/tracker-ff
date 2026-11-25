// Feature hooks of the Master screens (fMasterParams.cpp, included once after MP and the prototypes): send ducking on the limiter screen's
// second page (FEATURE_SEND_DUCK) and the Master Inserts page of the plugin host (FEATURE_PLUG_HOST, plug_ui.h). Without the features
// every hook is empty and the stock code runs.
// A hook that may end the calling function (..._OR_RETURN) is a statement macro, as in devhooks.h: it expands to the very tokens the
// inline block had, so the image stays byte identical (as an always inline bool function GCC lays out some of these early returns
// differently under -Os). The others are always inline functions.
#define FF_MP_HOOK static inline __attribute__((always_inline))

#ifdef FEATURE_SEND_DUCK
// send ducking (the limiter screen's second page): field 0 depth, 1 release, 2 threshold, 4 source
static void duckEdit(int field, int16_t value, bool toDefault);

// a limiter value key or encoder on the ducking page edits the ducking field instead, and the caller returns
#define FF_MP_DUCK_EDIT_OR_RETURN(field, value, toDefault) \
    if (MP->duckPage)                                       \
    {                                                       \
        duckEdit(field, value, toDefault);                  \
        return;                                             \
    }

// the Sidechain key on the ducking page: steps the ducking source (with a wrap) or selects it, and the caller returns
#define FF_MP_DUCK_SIDECHAIN_OR_RETURN()                                                                                   \
    if (MP->duckPage)                                                                                                      \
    {                                                                                                                      \
        if (MP->selectedPlaceLimiter == 4)                                                                                 \
            duckEdit(4, engine.getSendDuck().source >= kDuckSourceMax ? -kDuckSourceMax : 1, false); /* with a wrap */     \
        else                                                                                                               \
        {                                                                                                                  \
            MP->selectedPlaceLimiter = 4;                                                                                  \
            MP->refreshLimiterFrame();                                                                                     \
        }                                                                                                                  \
        return;                                                                                                            \
    }
#else
#define FF_MP_DUCK_EDIT_OR_RETURN(field, value, toDefault)
#define FF_MP_DUCK_SIDECHAIN_OR_RETURN()
#endif

#ifdef FEATURE_PLUG_HOST
// a key or the encoder while the Master Inserts page is up: the page takes it (`call`) and the caller returns
#define FF_MP_PLUG_OR_RETURN(call) \
    if (plugUiActive())             \
    {                               \
        call;                       \
        return;                     \
    }

// the Saturation list runs on into the master insert plugins (insert 1): `pos` is the new list position, the caller returns
#define FF_MP_PLUG_SAT_OR_RETURN(pos) \
    plugUiSatSet(pos);                 \
    MP->showSaturationType();          \
    return;

// Shift + Saturation opens the Master Inserts page (the two plugin slots and their parameters), the caller returns
#define FF_MP_PLUG_SHIFT_SATURATION_OR_RETURN() \
    if (plugUiShiftHeld())                       \
    {                                            \
        plugUiOpen();                            \
        return;                                  \
    }

// the Master key cycle, last screen: the Inserts page, built on top of the Master values screen, the caller returns
#define FF_MP_PLUG_CYCLE_INSERTS_OR_RETURN() \
    if (plugUiInCycle())                      \
    {                                         \
        switchToMaster();                     \
        plugUiOpen();                         \
        return;                               \
    }
#else
#define FF_MP_PLUG_OR_RETURN(call)
#define FF_MP_PLUG_SAT_OR_RETURN(pos)
#define FF_MP_PLUG_SHIFT_SATURATION_OR_RETURN()
#define FF_MP_PLUG_CYCLE_INSERTS_OR_RETURN()
#endif

// cMasterParams::update on the limiter screen: the ducking page shows its own meter. False: the gain reduction meter
FF_MP_HOOK bool ffMpDuckMeter(cMasterParams* mp)
{
#ifdef FEATURE_SEND_DUCK
    if (mp->duckPage)
    {
        mp->duckMeterHandler();
        return true;
    }
#endif
    (void)mp;
    return false;
}

// back on the first limiter page (switching screens)
FF_MP_HOOK void ffMpDuckPageOff(cMasterParams* mp)
{
#ifdef FEATURE_SEND_DUCK
    mp->duckPage = false;
#else
    (void)mp;
#endif
}

// the limiter screen's buttons: button 5 toggles the ducking page
FF_MP_HOOK void ffMpDuckPageButton(InputProcessor* ip)
{
#ifdef FEATURE_SEND_DUCK
    ip->setButtonObj(interfaceButton5, buttonPress, functToggleDuckPage);
#else
    (void)ip;
#endif
}

// cMasterParams::update: the Master Inserts page's own refresh
FF_MP_HOOK void ffMpPlugUpdate(Platform* platform)
{
#ifdef FEATURE_PLUG_HOST
    plugUiUpdate(platform->getTimer()->millis());
#else
    (void)platform;
#endif
}

// leaving the Master values screen for another Master screen: the Inserts page goes
FF_MP_HOOK void ffMpPlugLeave()
{
#ifdef FEATURE_PLUG_HOST
    plugUiLeave();
#endif
}
#undef FF_MP_HOOK
