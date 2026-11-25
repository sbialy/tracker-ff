#pragma once
// The image's side of the custom theme parser. In the release image of the original Tracker (FEATURE_THEME_MODULE) the JSON parser
// (ThemeManagerJson.cpp) is the theme tenant of /Plugins/Ext/extras.plg: the module is loaded on the first custom theme and a refused or
// missing file returns false (ThemeManager::applyTheme falls back to the built-in theme). Elsewhere it calls the resident parser.
#include <stddef.h>

class Platform;
struct Theme;

bool themeParseFile(Platform* platform, const char* json, size_t len, Theme& out);
