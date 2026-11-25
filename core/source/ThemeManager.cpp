#include <stdio.h>
#include <string.h>

#include "ThemeManager.h"
#include "Platform.h"
#include "core/interface.h"
#include "display.h"

#include "cr_section_macros.h"
#include "ThemeManagerGlue.h"

Platform* ThemeManager::platform = nullptr;

uint8_t ThemeManager::themeCount = 3;
__NOINIT(EXTERNAL_RAM) char ThemeManager::themeNames[ThemeManager::kMaxCustomThemes][ThemeManager::kMaxNameLength];
__NOINIT(EXTERNAL_RAM) const char* ThemeManager::themeNamePointers[ThemeManager::kMaxCustomThemes];
__NOINIT(EXTERNAL_RAM) char ThemeManager::themeFilePaths[ThemeManager::kMaxCustomThemes][64];
uint8_t ThemeManager::currentThemeIndex = 0;

uint8_t ThemeManager::fontCount = 2;
__NOINIT(EXTERNAL_RAM) char ThemeManager::fontNames[ThemeManager::kMaxCustomFonts][ThemeManager::kMaxNameLength];
__NOINIT(EXTERNAL_RAM) const char* ThemeManager::fontNamePointers[ThemeManager::kMaxCustomFonts];
__NOINIT(EXTERNAL_RAM) char ThemeManager::fontFilePaths[ThemeManager::kMaxCustomFonts][64];
uint8_t ThemeManager::currentFontIndex = 0;

__NOINIT(EXTERNAL_RAM) static char s_themeNamesBuffer[ThemeManager::kMaxCustomThemes * ThemeManager::kMaxNameLength];
__NOINIT(EXTERNAL_RAM) static char* s_themeFileList[ThemeManager::kMaxCustomThemes];

__NOINIT(EXTERNAL_RAM) static char s_fontNamesBuffer[ThemeManager::kMaxCustomFonts * ThemeManager::kMaxNameLength];
__NOINIT(EXTERNAL_RAM) static char* s_fontFileList[ThemeManager::kMaxCustomFonts];

__NOINIT(EXTERNAL_RAM) __attribute__((aligned(32))) static char s_fileReadBuffer[8192]; // SD read target, word aligned for the multi block path

void ThemeManager::ensureDefaultsInitialized()
{
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    strcpy(themeNames[0], "Original");
    strcpy(themeNames[1], "Monochrome");
    strcpy(themeNames[2], "Anniversary");
    themeNamePointers[0] = themeNames[0];
    themeNamePointers[1] = themeNames[1];
    themeNamePointers[2] = themeNames[2];

    strcpy(fontNames[0], "Original");
    strcpy(fontNames[1], "New");
    fontNamePointers[0] = fontNames[0];
    fontNamePointers[1] = fontNames[1];
}

static bool hasExtension(const char* filename, const char* ext)
{
    if (!filename || !ext)
        return false;
    size_t flen = strlen(filename);
    size_t elen = strlen(ext);
    if (flen < elen)
        return false;
    return strcasecmp(filename + flen - elen, ext) == 0;
}


// The JSON parser (ThemeManagerJson.cpp): in the release image of the original Tracker (FEATURE_THEME_MODULE) the theme tenant of
// /Plugins/Ext/extras.plg, loaded on the first custom theme. A missing, foreign or damaged extras.plg is a failed parse: applyTheme falls
// back to the built in default theme, as it does for a bad file.
void ThemeManager::init(Platform* p)
{
    platform = p;
    scanThemesAndFonts();
}

void ThemeManager::scanThemesAndFonts()
{
    scanThemes();
    scanFonts();
}

static void scanAssetDirectory(
  Platform* platform,
  const char* dirName,
  const char* dirNameSlash,
  bool (*filterFn)(const char*),
  const char* ext,
  size_t extLen,
  uint8_t& count,
  int maxCount,
  char names[][ThemeManager::kMaxNameLength],
  const char* namePointers[],
  char filePaths[][64],
  char* namesBuffer,
  size_t namesBufferSize,
  char* fileList[])
{
    if (!platform)
        return;
    FileSystem* fs = platform->getFileSystem();
    if (!fs || !fs->isSdCardInserted())
        return;

    const char* dirToScan = nullptr;
    if (fs->exists(dirName))
    {
        dirToScan = dirName;
    }
    else if (fs->exists(dirNameSlash))
    {
        dirToScan = dirNameSlash;
    }
    else
    {
        fs->mkdir(0, dirName);
        if (fs->exists(dirName))
            dirToScan = dirName;
        else if (fs->exists(dirNameSlash))
            dirToScan = dirNameSlash;
    }

    if (!dirToScan)
        return;

    int outCount = maxCount - count;
    fs->listDirContents(
      dirToScan,
      FileSystem::DirContentType::kFiles,
      filterFn,
      namesBuffer,
      namesBufferSize,
      ThemeManager::kMaxNameLength,
      outCount,
      fileList);

    for (int i = 0; i < outCount && count < maxCount; i++)
    {
        const char* fileName = fileList[i];
        if (!fileName || strlen(fileName) == 0)
            continue;

        snprintf(filePaths[count], 64, "%s/%s", dirToScan, fileName);

        strncpy(names[count], fileName, ThemeManager::kMaxNameLength - 1);
        names[count][ThemeManager::kMaxNameLength - 1] = '\0';
        size_t nlen = strlen(names[count]);
        if (nlen > extLen && strcasecmp(names[count] + nlen - extLen, ext) == 0)
        {
            names[count][nlen - extLen] = '\0';
        }

        namePointers[count] = names[count];
        count++;
    }
}

void ThemeManager::scanThemes()
{
    ensureDefaultsInitialized();
    themeCount = 3;
    themeNamePointers[0] = themeNames[0];
    themeNamePointers[1] = themeNames[1];
    themeNamePointers[2] = themeNames[2];

    scanAssetDirectory(
      platform,
      "Themes",
      "/Themes",
      [](const char* name) { return hasExtension(name, ".json"); },
      ".json",
      5,
      themeCount,
      kMaxCustomThemes,
      themeNames,
      themeNamePointers,
      themeFilePaths,
      s_themeNamesBuffer,
      sizeof(s_themeNamesBuffer),
      s_themeFileList);
}

void ThemeManager::scanFonts()
{
    ensureDefaultsInitialized();
    fontCount = 2;
    fontNamePointers[0] = fontNames[0];
    fontNamePointers[1] = fontNames[1];

    scanAssetDirectory(
      platform,
      "Fonts",
      "/Fonts",
      [](const char* name) { return hasExtension(name, ".ptfont"); },
      ".ptfont",
      7,
      fontCount,
      kMaxCustomFonts,
      fontNames,
      fontNamePointers,
      fontFilePaths,
      s_fontNamesBuffer,
      sizeof(s_fontNamesBuffer),
      s_fontFileList);
}

uint8_t ThemeManager::getThemeCount()
{
    ensureDefaultsInitialized();
    return themeCount;
}

const char* const* ThemeManager::getThemeNames()
{
    ensureDefaultsInitialized();
    return themeNamePointers;
}

const char* ThemeManager::getCurrentThemeName()
{
    ensureDefaultsInitialized();
    if (currentThemeIndex < themeCount)
        return themeNamePointers[currentThemeIndex];
    return "Original";
}

bool ThemeManager::applyTheme(uint8_t index)
{
    ensureDefaultsInitialized();
    if (index < 3)
    {
        currentThemeIndex = index;
        display.setTheme(index);
        mtInterface.refreshInterfaceControlsColors();
        return true;
    }

    if (index >= themeCount || !platform)
    {
        currentThemeIndex = 0;
        display.setTheme(0);
        mtInterface.refreshInterfaceControlsColors();
        return false;
    }

    FileSystem* fs = platform->getFileSystem();
    if (!fs || !fs->isSdCardInserted())
    {
        currentThemeIndex = 0;
        display.setTheme(0);
        mtInterface.refreshInterfaceControlsColors();
        return false;
    }

    auto [isOpen, file] = fs->openFile(themeFilePaths[index], FileSystem::FileOpenMode::kRead);
    if (!isOpen || !file)
    {
        const char* altPath = (themeFilePaths[index][0] == '/') ? &themeFilePaths[index][1] : themeFilePaths[index];
        std::tie(isOpen, file) = fs->openFile(altPath, FileSystem::FileOpenMode::kRead);
    }
    if (!isOpen || !file)
    {
        currentThemeIndex = 0;
        display.setTheme(0);
        mtInterface.refreshInterfaceControlsColors();
        return false;
    }

    int32_t bytesRead = fs->readFile(s_fileReadBuffer, sizeof(s_fileReadBuffer) - 1, file);
    fs->closeFile(file);

    if (bytesRead <= 0)
    {
        currentThemeIndex = 0;
        display.setTheme(0);
        mtInterface.refreshInterfaceControlsColors();
        return false;
    }

    s_fileReadBuffer[bytesRead] = '\0';

    if (!themeParseFile(platform, s_fileReadBuffer, static_cast<size_t>(bytesRead), themeCustom))
    {
        currentThemeIndex = 0;
        display.setTheme(0);
        mtInterface.refreshInterfaceControlsColors();
        return false;
    }

    currentThemeIndex = index;
    display.setCustomTheme(themeCustom);
    mtInterface.refreshInterfaceControlsColors();
    return true;
}

uint8_t ThemeManager::getFontCount()
{
    ensureDefaultsInitialized();
    return fontCount;
}

const char* const* ThemeManager::getFontNames()
{
    ensureDefaultsInitialized();
    return fontNamePointers;
}

const char* ThemeManager::getCurrentFontName()
{
    ensureDefaultsInitialized();
    if (currentFontIndex < fontCount)
        return fontNamePointers[currentFontIndex];
    return "Original";
}

bool ThemeManager::applyFont(uint8_t index)
{
    if (index < 2)
    {
        currentFontIndex = index;
        display.setFont(index);
        return true;
    }

    if (index >= fontCount || !platform)
    {
        currentFontIndex = 0;
        display.setFont(0);
        return false;
    }

    FileSystem* fs = platform->getFileSystem();
    if (!fs || !fs->isSdCardInserted())
    {
        currentFontIndex = 0;
        display.setFont(0);
        return false;
    }

    if (display.loadCustomFontFromSD(fs, fontFilePaths[index]))
    {
        currentFontIndex = index;
        return true;
    }

    currentFontIndex = 0;
    display.setFont(0);
    return false;
}
