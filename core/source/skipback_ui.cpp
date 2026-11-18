// Skip-back capture, the firmware side (skipback.h): the card through the Platform file system, the Shift + Sample Rec shortcut and the
// popups. The Features descriptor is in features_builtin.cpp.

#include "skipback.h"

#if defined(FEATURE_SKIPBACK) && !defined(SKIPBACK_TEST)

#include "skipback_ui.h"

#include "Platform.h"
#include "TrackerImpl.h"
#include "core/interface.h"
#include "core/interfacePopups.h"

#include <stdio.h>

namespace skipback
{
namespace
{
FileSystem* fs()
{
    return TrackerImpl::getInstance()->getPlatform()->getFileSystem();
}

void* cardCreate(const char* path)
{
    auto r = fs()->openFile(path, FileSystem::kWriteCreateNew);
    return std::get<0>(r) ? std::get<1>(r) : nullptr;
}

bool cardWrite(void* f, const void* data, uint32_t n)
{
    return fs()->writeFile(data, n, f) == int32_t(n);
}

void cardClose(void* f)
{
    fs()->closeFile(f);
}

bool cardExists(const char* path)
{
    return fs()->exists(path);
}

void cardMkdir(const char* path)
{
    fs()->mkdir(0, path);
}

const Io kCard = { cardCreate, cardWrite, cardClose, cardExists, cardMkdir };

bool s_swallow;          // the Sample Rec press was the shortcut: its hold and release go nowhere either
char s_line[40];         // popup lines keep the pointer

void popup(const char* line1, const char* line2, uint8_t seconds)
{
    mtPopups.show(0, line1, line2, seconds);
}
}   // namespace

void uiSave()
{
    switch (trigger(&kCard))
    {
        case Result::Started:
            snprintf(s_line, sizeof s_line, "Saving %s", lastName());
            popup("Skip-back", s_line, 3);
            break;
        case Result::Empty: popup("Skip-back", "Nothing captured yet", 2); break;
        case Result::NoFile: popup("Skip-back", "Cannot create the file", 2); break;
        case Result::Busy: break;   // the popup of the running save is up
        case Result::Off: break;
    }
}

void uiTick()
{
    switch (tick(&kCard))
    {
        case kDone:
            snprintf(s_line, sizeof s_line, "%s/%s", kDir, lastName());
            popup("Skip-back saved", s_line, 2);
            break;
        case kFailed: popup("Skip-back", "Card write failed", 2); break;
        default: break;
    }
}

bool uiButton(uint8_t button, uint8_t state)
{
    if (button != interfaceButtonSampleRec) return false;
    if (state == buttonPress && on() && mtInterface.getPlatform()->getButtons()->isButtonPressed(interfaceButtonShift))
    {
        s_swallow = true;
        uiSave();
        return true;
    }
    if (s_swallow)
    {
        if (state == buttonRelease) s_swallow = false;
        return true;
    }
    return false;
}
}   // namespace skipback

#endif
