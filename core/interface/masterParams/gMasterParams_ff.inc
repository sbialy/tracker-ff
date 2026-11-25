// gMasterParams.cpp, included where this block was: the Saturation list names without the plugin host and the hooks of the Master screens' drawing. The hooks are statement macros (as in devhooks.h), they expand to the very tokens the inline blocks had. Without the features they are empty
#if defined(FEATURE_SAT_SOFTKNEE) && !defined(FEATURE_PLUG_HOST) && !defined(FEATURE_FF_DIRT)
const char* const saturationListNames[kSatListCount] = { "Hard Clip", "Soft Clip", "Saturate", "Overdrive", "Drive+", "Soft Knee" };
#endif

#ifdef FEATURE_PLUG_HOST
// the Master Inserts page's two extra list columns, the page starts closed
#define FF_GMP_PLUG_INIT_CONTROLS() plugUiInitControls()
// the Saturation list is the built-in entries plus the master insert plugins (synth-open/plug_ui.cpp), the caller returns
#define FF_GMP_PLUG_SAT_SHOW_OR_RETURN()                              \
    plugUiSatList(&saturationListData);                               \
    display.setControlValue(saturationList, plugUiSatPos());          \
    display.refreshControl(saturationList);                           \
                                                                      \
    display.setControlText2(label[5], plugUiSatText());               \
    display.refreshControl(label[5]);                                 \
    return;
#else
#define FF_GMP_PLUG_INIT_CONTROLS() ((void)0)
#define FF_GMP_PLUG_SAT_SHOW_OR_RETURN()
#endif
