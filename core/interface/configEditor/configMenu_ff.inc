// configMenu.cpp, included where this block was: the group counts of tracker-ff (Project Settings, Features) and the hooks of the Config menu. The hooks are statement macros (as in devhooks.h), they expand to the very tokens the inline blocks had
#ifdef FEATURE_FF_CORE
constexpr uint8_t snapGroupCount = 1;
#else
constexpr uint8_t snapGroupCount = 0;
#endif

#ifdef FEATURE_FEATURES
constexpr uint8_t featuresGroupCount = 1;   // on Tracker+ and Mini too (FEATURE_FF_FEATURES), the Features group between Metronome and Firmware
#else
constexpr uint8_t featuresGroupCount = 0;
#endif

#ifdef FEATURE_FF_CORE
// createMenuBaseList: the Project Settings page lists only the page itself
#define FF_CFG_PROJECT_PAGE_LIST(list)                     \
    if (projectPage)                                      \
    {                                                     \
        (list).start = 0;                                 \
        (list).length = 1;                                \
        (list).data += menuBase.getSelectedItem();        \
    }
#else
#define FF_CFG_PROJECT_PAGE_LIST(list)
#endif

#ifdef FEATURE_FEATURES
#ifdef FEATURE_FEATURES_FULL
#define FF_CFG_FEATURE_PAGE_ACTIVE() (activeSubmenu == &menuFeaturePage || activeSubmenu == &menuUsb)
#else
#define FF_CFG_FEATURE_PAGE_ACTIVE() (activeSubmenu == &menuFeaturePage)
#endif
// a list row with action changed: the State row of a feature page shows or hides the params, the Status row follows, the USB entry and its rows
#define FF_CFG_FEATURE_ROW_CHANGED()        \
    if FF_CFG_FEATURE_PAGE_ACTIVE()         \
    {                                       \
        featurePageRefresh();               \
        createMenuBaseList();               \
    }
#else
#define FF_CFG_FEATURE_ROW_CHANGED()
#endif
