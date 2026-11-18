#pragma once

// Skip-back capture, the firmware side (skipback_ui.cpp). FEATURE_SKIPBACK builds only.

#include "skipback.h"

#include <stdint.h>

namespace skipback
{
void uiSave();                                  // what Shift + Sample Rec does: start the save, popup
void uiTick();                                  // main loop: the next chunk, the popup at the end
bool uiButton(uint8_t button, uint8_t state);   // Interface::buttonChange: true = the shortcut took the event
}   // namespace skipback
