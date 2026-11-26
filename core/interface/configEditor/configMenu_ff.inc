// configMenu.cpp, included where this block was: the group counts of tracker-ff (Project Settings, Features) and the hooks of the Config menu. The hooks are statement macros (as in devhooks.h), they expand to the very tokens the inline blocks had
#ifdef FEATURE_FF_CORE
constexpr uint8_t snapGroupCount = 1;
constexpr uint8_t projectMenuGroupCount = 1;    // the Config group Project Settings: one row that opens the Project Settings page (File F7)
constexpr uint8_t ffGeneralRows = 1;            // + Performance presets (a config value, it moved from that group to General)
#else
constexpr uint8_t snapGroupCount = 0;
constexpr uint8_t projectMenuGroupCount = 10;   // stock CE: the ten rows
constexpr uint8_t ffGeneralRows = 0;
#endif

#ifdef FEATURE_FEATURES
constexpr uint8_t featuresGroupCount = 1;   // on Tracker+ and Mini too (FEATURE_FF_FEATURES), the Features group between Metronome and Firmware
#else
constexpr uint8_t featuresGroupCount = 0;
#endif

#ifdef FEATURE_FF_CORE
// the Config group Project Settings: its nine per-project rows are on the Project Settings page (configMenu_projpage.inc), the group is one row that opens it
static const char* const moveProjDesc =
  "The project settings of this group (Notes scale, Root note, Notes layout, Pattern divider, Anti-aliasing, Limiter mode, Sends mode, "
  "Extra headroom, Snap to zero) are on the Project Settings page now, with the other settings of the project. "
  "The File screen opens it with F7. Press the button to open it from here. Performance presets moved to General.";
#define FF_CFG_PROJECT_GROUP_ROWS()                                                                                                          \
    const strItemTypeActionButton openProjectPageSetup{ openProjectPageAction, nullptr };                                                    \
    cMenuItem melOpenProjectPage(menuProject, "Open the page", moveProjDesc, menuTypeItemActionButton, &openProjectPageSetup);
#define FF_CFG_GENERAL_PERF_PRESETS() PROJ_ROW_PERF_PRESETS(menuGeneral);
#else
#define FF_CFG_PROJECT_GROUP_ROWS()      \
    PROJ_ROW_PERF_PRESETS(menuProject);  \
    PROJ_ROW_PATTERN_DIV(menuProject);   \
    PROJ_ROW_PADS_SCALE(menuProject);    \
    PROJ_ROW_PADS_ROOT(menuProject);     \
    PROJ_ROW_PADS_LAYOUT(menuProject);   \
    PROJ_ROW_ANTIALIASING(menuProject);  \
    PROJ_ROW_LIMITER(menuProject);       \
    PROJ_ROW_SENDS(menuProject);         \
    PROJ_ROW_HEADROOM(menuProject);      \
    PROJ_ROW_SNAP_TO_ZERO(menuProject);
#define FF_CFG_GENERAL_PERF_PRESETS()
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
