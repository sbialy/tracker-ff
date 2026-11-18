// Follow chord (tracker-ff): the project steps and the UI steps. mtFollowChord.h has the rules, project.mtff holds the setting.
#include "mtFollowChord.h"

#include "fileManager.h"
#include "fileManagerDefs.h"
#include "mtStructs.h"

#include <string.h>

namespace followchord
{
static_assert(kTracks == kAllTracks, "Follow chord keeps one setting per track");

void workspaceLoaded(bool /*newProject*/)
{
    memset(&setting, 0, sizeof(setting));
    recalc();
    resetLive();
}

namespace
{
void changed()
{
    recalc();
    newFileManager.setProjectStructChanged(); // the Workspace save writes the track.N.follow keys of project.mtff (plug::projectSave)
    mtProject.values.projectNotSavedFlag |= 1;
}
} // namespace

void cycleMode(uint8_t track)
{
    if (track >= kAllTracks)
        return;
    uint8_t& m = setting.mode[track];
    if (m == kOff && (setting.source[track] == track || setting.source[track] >= kAllTracks))
    {
        // no usable chord track yet: the one another follower uses (one chord track is the usual case), else track 1 or 2
        uint8_t src = track == 0 ? 1 : 0;
        for (int t = 0; t < kAllTracks; t++)
        {
            if (t != track && setting.mode[t] != kOff && setting.source[t] != track)
            {
                src = setting.source[t];
                break;
            }
        }
        setting.source[track] = src;
    }
    m = uint8_t((m + 1) % kModeCount);
    changed();
}

void cycleSource(uint8_t track)
{
    if (track >= kAllTracks)
        return;
    uint8_t s = setting.source[track];
    do
    {
        s = uint8_t((s + 1) % kAllTracks);
    } while (s == track);
    setting.source[track] = s;
    if (setting.mode[track] == kOff)
        setting.mode[track] = kSnap; // picking a chord track turns following on
    changed();
}

} // namespace followchord
