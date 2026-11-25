// configMenu.cpp, included where this block was: the menu group of the Project Settings page (FEATURE_FF_CORE)
#ifdef FEATURE_FF_CORE
// The Project Settings page: the settings of project.mtff, opened by the File screen (kOpenProjectSettings) on this one group. The last
// group of the base list, so Config hides it by the count (cConfigEditor::createConfigMenu). Rows: 4 FX Slots, Macro depth, Strum
// volume, Random seed, Reroll seed, Scale snap (a group of its own: one row per track), Stereo pairs (a group, one row per pair), Tuning (a group: notes per
// period, period, 1/1 key, 1/1 Hz), MPE out
// Stereo pairs are MK66 only (IMX plays a stereo .pti natively): no row on Tracker+ and Mini
#ifdef PLATFORM_IMX
constexpr uint8_t kPairRows = 0;
#else
constexpr uint8_t kPairRows = 1;
#endif
#ifdef FEATURE_SYNTH_OPEN
constexpr uint8_t projectPageRows = 8 + kPairRows;
#else
constexpr uint8_t projectPageRows = 7 + kPairRows;
#endif
cMenuGroup menuProjectPage(menuBase, "Project Settings", projectPageRows);
#endif
