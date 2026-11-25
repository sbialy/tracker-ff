// fPatternEdit.cpp, included first: the FF hooks of the Pattern screen. Statement macros (as in devhooks.h), they expand to the very tokens the inline blocks had and are empty without the feature
#ifdef FEATURE_PARTS
#include "parts.h"
// the screen shows the playing Part: its length is the Part's, the caller returns (fPatternEdit_parts.inc)
#define FF_PE_PART_LENGTH_OR_RETURN() \
    if (partLengthLocked()) return
#else
#define FF_PE_PART_LENGTH_OR_RETURN()
#endif

#ifdef FEATURE_FF_CORE
// page 3 Groove (fPatternEdit_groove.inc), the groove is kept in project.mtff (track.N.groove), written by the plugin host's project calls (plug_host.cpp)
static void functGroove(uint8_t state);
#define FF_PE_GROOVE_BUTTON() ip->setButtonObj(interfaceButton2, functGroove)
#else
#define FF_PE_GROOVE_BUTTON()
#endif
