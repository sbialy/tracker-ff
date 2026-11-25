#include "ThemeManagerGlue.h"

#include "ThemeManager.h"

#ifdef FEATURE_THEME_MODULE
#include "ext_hooks.h"

bool themeParseFile(Platform* platform, const char* json, size_t len, Theme& out)
{
    if (!ext_hooks.theme)
    {
        ext_extras_bind(platform);
        ext_modit_check(nullptr, nullptr);
    }
    return ext_hooks.theme && ext_hooks.theme(json, uint32_t(len), &out) != 0;
}
#else
bool themeParseFile(Platform*, const char* json, size_t len, Theme& out)
{
    return ThemeManager::parseThemeJson(json, len, out);
}
#endif
