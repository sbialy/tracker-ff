#pragma once
// The hook functions of the extras.plg tenants that are image sources compiled into the module (FEATURE_THEME_MODULE, FEATURE_SELFTEST_MODULE):
// synth-open/modit/modit_entry.cpp puts them into ext_hooks (ext_abi.h), the ui_shot host (ext_module.cpp enterModit) does the same with its
// own copies. The image itself never defines them.
#include <stdint.h>

uint32_t themeParseOp(const char* json, uint32_t len, void* theme);   // core/source/ThemeManagerJson.cpp: ThemeManager::parseThemeJson
uint32_t selftestDispatch(uint32_t op, void* arg);                    // core/source/mtTest.cpp: EXT_SELFTEST_* on the one cTest
