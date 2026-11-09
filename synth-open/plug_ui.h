// Plugin host UI glue for the master screen (FEATURE_PLUG_HOST, synth-open/plug_ui.cpp). Called from
// core/interface/masterParams/*.cpp under the same define.
#pragma once

#include <stdint.h>

struct strList;

// Saturation list: the built-in entries, then "Inserts" (both master insert slots, plug::kSatInserts in the project)
int plugUiSatCount();
int plugUiSatPos();
const char* plugUiSatText();
void plugUiSatList(strList* data);   // points the list at the built-in texts plus "Inserts"
void plugUiSatSet(int pos);

// The Master Inserts page (Shift + Saturation button, or the last stop of the Master repeat-press cycle)
void plugUiInitControls();   // end of cMasterParams::initDisplayControls: the page's two extra lists
bool plugUiActive();
void plugUiOpen();
void plugUiLeave();
void plugUiUpdate(uint32_t nowMs);
void plugUiEncoder(int16_t value);
void plugUiArrow(int dir);   // 0 left, 1 right, 2 up, 3 down
void plugUiDelete();
bool plugUiShiftHeld();

// The Master button repeat-press cycle: Inserts is its last stop while a plugin is on the card or an insert slot is
// assigned (the host's in-memory index, no card access). plug_ui_nav.h has the order.
bool plugUiInCycle();
const char* plugUiCycleTitle(int stop);   // the Master screen titles "Master 1/N", "... Mixer 2/N", "Global Mixer 3/N", N 3 or 4. stop 0..2
