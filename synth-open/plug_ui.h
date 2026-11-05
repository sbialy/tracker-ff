// Plugin host UI glue for the master screen (FEATURE_PLUG_HOST, synth-open/plug_ui.cpp). Called from
// core/interface/masterParams/*.cpp under the same define.
#pragma once

#include <stdint.h>

struct strList;

// Saturation list: the built-in entries, then the master insert plugins (a plugin entry is insert 1)
int plugUiSatCount();
int plugUiSatPos();
const char* plugUiSatText();
void plugUiSatList(strList* data);   // points the list at the built-in texts plus the plugin names
void plugUiSatSet(int pos);

// The Master Inserts page (Shift + Saturation button)
bool plugUiActive();
void plugUiOpen();
void plugUiLeave();
void plugUiUpdate(uint32_t nowMs);
void plugUiEncoder(int16_t value);
void plugUiArrow(int dir);   // 0 left, 1 right, 2 up, 3 down
void plugUiDelete();
bool plugUiShiftHeld();
