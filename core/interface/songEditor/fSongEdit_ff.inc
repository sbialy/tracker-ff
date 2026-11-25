// fSongEdit.cpp, included first: the Parts view of the Song screen (FEATURE_PARTS, parts.h, fSongParts.cpp) and its hooks into the stock Song screen. The hooks are statement macros (as in devhooks.h), they expand to the very tokens the inline blocks had. Without Parts they are empty
#ifdef FEATURE_PARTS
#include "parts.h"
#endif

#ifdef FEATURE_PARTS
// cSongEditor::update: the Parts view refreshes its own rows, the caller returns
#define FF_SONG_PARTS_UPDATE_OR_RETURN()                                                                              \
    if (partsView)                                                                                                    \
    {                                                                                                                 \
        if (songPlayerRefreshTimer > 100)                                                                             \
        {                                                                                                             \
            songPlayerRefreshTimer = 0;                                                                               \
            partsUpdate();                                                                                            \
            if (!editMode) refreshTempoField(); /* Build: F8 is the track 8 / 16 button, not Tempo */                 \
        }                                                                                                             \
        tapTempo_update();                                                                                            \
        return;                                                                                                       \
    }
// the page binds its keys: Arrange / Build keys over the stock ones (fSongParts.cpp)
#define FF_SONG_PARTS_BIND() \
    if (partsView) partsBind()
// Shift + F8: Patterns <-> Parts, the caller returns
#define FF_SONG_PARTS_TOGGLE_OR_RETURN(state)                                         \
    if ((state) == buttonPress && SE->buttons->isButtonPressed(interfaceButtonShift)) \
    {                                                                                 \
        SE->partsEnterView(!SE->partsView);                                           \
        return;                                                                       \
    }
// Rec: Arrange <-> Build
#define FF_SONG_PARTS_MODE_CHANGED() \
    if (SE->partsView) SE->partsModeChanged()
// an empty Parts list has no position to move, the caller returns
#define FF_SONG_PARTS_EMPTY_OR_RETURN() \
    if (partsView && songLength == 0)   \
    {                                   \
        return;                         \
    }
// the Parts view keeps its own rows (fSongParts.cpp), the caller returns
#define FF_SONG_PARTS_ROWS_OR_RETURN() \
    if (partsView)                     \
    {                                  \
        return;                        \
    }
#else
#define FF_SONG_PARTS_UPDATE_OR_RETURN()
#define FF_SONG_PARTS_BIND() ((void)0)
#define FF_SONG_PARTS_TOGGLE_OR_RETURN(state)
#define FF_SONG_PARTS_MODE_CHANGED() ((void)0)
#define FF_SONG_PARTS_EMPTY_OR_RETURN()
#define FF_SONG_PARTS_ROWS_OR_RETURN()
#endif
