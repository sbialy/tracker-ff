// Plugin host UI (FEATURE_PLUG_HOST): the master insert plugins in the master screen's Saturation list and the generic
// "Master Inserts" page. See plug_host.h and synth-open/plug/README.md (Device use).
//
// Saturation list: the built-in entries, then one entry per plugin in /Plugins/Master. Picking a plugin entry puts it into
// insert 1 and runs the mixers at hard clip (the same rule as the built-in Dirt entries), picking a built-in entry empties
// insert 1. The project file keeps saturationType (hard clip for a plugin), the plugin itself lives in the sidecar.
//
// Inserts page (Shift + the Saturation button on the master screen): column 1 and 2 pick the plugin of insert 1 and 2 (the
// encoder, Delete empties), columns 3 to 7 are the parameters of the focused insert, five at a time (press the insert's
// button again for the next five), the last column goes back.
#ifdef FEATURE_PLUG_HOST

#include "plug_host.h"
#include "plug_ui.h"

#include "core/interface.h"
#include "fileManager.h"
#include "masterParams/masterParams.h"
#include "mtAudioEngine.h"

#include <stdio.h>
#include <string.h>

namespace
{
cMasterParams* const MP = &masterParams;

const uint16_t kPlaces[8][4] = {
    {0 + 1, 31, 800 / 8 - 3, 391},
    {(800 / 8) * 1 + 1, 31, 800 / 8 - 3, 391},
    {(800 / 8) * 2 + 1, 31, 800 / 8 - 3, 391},
    {(800 / 8) * 3 + 1, 31, 800 / 8 - 3, 391},
    {(800 / 8) * 4 + 1, 31, 800 / 8 - 3, 391},
    {(800 / 8) * 5 + 1, 31, 800 / 8 - 3, 391},
    {(800 / 8) * 6 + 1, 31, 800 / 8 - 3, 391},
    {(800 / 8) * 7 + 1, 31, 800 / 8 - 3, 391},
};
constexpr int kParamCols = 5;   // columns 2..6

const char* s_sat[kSaturationTypeEnd + plug::kIndexMax];
bool s_active;
uint8_t s_place, s_focus, s_page;
char s_val[8][20];
char s_title[48];
uint32_t s_lastDraw;
plug::SlotState s_drawn[plug::kSlots];
uint32_t s_drawnCount[plug::kSlots];

const char* stateText(int k)
{
    switch (plug::slotState(k))
    {
        case plug::SlotState::kEmpty: return "None";
        case plug::SlotState::kPending: return "Loading";
        case plug::SlotState::kMissing: return "Missing";
        case plug::SlotState::kFailed: return "Failed";
        case plug::SlotState::kSafe: return "Safe mode";
        case plug::SlotState::kTripped: return "Over CPU";
        default: return plug::slotName(k);
    }
}

void markChanged()
{
    newFileManager.setProjectStructChanged();
}

void showFrame()
{
    display.setControlValue(MP->frameControl, s_place);
    display.setControlShow(MP->frameControl);
    display.refreshControl(MP->frameControl);
}

void showSlot(int k)
{
    if (plug::slotState(k) == plug::SlotState::kRunning)
        snprintf(s_val[k], sizeof(s_val[k]), "%s", plug::slotName(k));
    else if (plug::slotName(k)[0] && plug::slotState(k) != plug::SlotState::kPending)
        snprintf(s_val[k], sizeof(s_val[k]), "%.8s %s", stateText(k), plug::slotName(k));
    else
        snprintf(s_val[k], sizeof(s_val[k]), "%s", stateText(k));
    display.setControlText(MP->label[k], k == 0 ? (s_focus == 0 ? "*Insert 1" : "Insert 1") : (s_focus == 1 ? "*Insert 2" : "Insert 2"));
    display.setControlText2(MP->label[k], s_val[k]);
    display.refreshControl(MP->label[k]);
}

void showParam(int col)
{
    const uint32_t i = uint32_t(s_page * kParamCols + col - 2);
    const plug_param* pp = plug::param(s_focus, i);
    if (!pp)
    {
        display.setControlText(MP->label[col], " ");
        display.setControlText2(MP->label[col], " ");
        display.setControlHide(MP->barControl[col]);
    }
    else
    {
        const float v = plug::paramValue(s_focus, i);
        if (pp->enum_count)
        {
            const int e = int(v + 0.5f);
            snprintf(s_val[col], sizeof(s_val[col]), "%s", e >= 0 && e < pp->enum_count ? pp->enum_names[e] : "?");
            display.setControlHide(MP->barControl[col]);
        }
        else
        {
            const bool whole = pp->step >= 1.0f;
            snprintf(s_val[col], sizeof(s_val[col]), whole ? "%.0f%s" : "%.2f%s", double(v), pp->unit ? pp->unit : "");
            const float span = pp->max - pp->min;
            display.setControlValue(MP->barControl[col], span > 0.0f ? int((v - pp->min) * 100.0f / span + 0.5f) : 0);
            display.setControlShow(MP->barControl[col]);
        }
        display.setControlText(MP->label[col], pp->name);
        display.setControlText2(MP->label[col], s_val[col]);
    }
    display.refreshControl(MP->barControl[col]);
    display.refreshControl(MP->label[col]);
}

void showTitle()
{
    if (plug::safeMode())
        snprintf(s_title, sizeof(s_title), "%s", plug::safeReason());
    else if (interfaceGlobals.devMode)   // the CPU gate readout: last block of each insert, percent of 522,240 cycles
        snprintf(s_title, sizeof(s_title), "Inserts %lu.%lu%% %lu.%lu%%", (unsigned long)(plug::slotCycles(0) / 5222),
                 (unsigned long)(plug::slotCycles(0) / 522 % 10), (unsigned long)(plug::slotCycles(1) / 5222),
                 (unsigned long)(plug::slotCycles(1) / 522 % 10));
    else
        snprintf(s_title, sizeof(s_title), "Master Inserts%s", plug::paramCount(s_focus) > kParamCols ? (s_page ? " 2/2" : " 1/2") : "");
    MP->titleBar->setTitle(s_title);
}

void showAll()
{
    showTitle();
    showSlot(0);
    showSlot(1);
    for (int c = 2; c < 2 + kParamCols; c++) showParam(c);
    for (int k = 0; k < plug::kSlots; k++)
    {
        s_drawn[k] = plug::slotState(k);
        s_drawnCount[k] = plug::paramCount(k);
    }
}

void clampPlace()
{
    const int n = int(plug::paramCount(s_focus)) - s_page * kParamCols;
    const int last = 1 + (n < 0 ? 0 : (n > kParamCols ? kParamCols : n));
    if (s_place > last && s_place < 7) s_place = uint8_t(last);
}

void pickPlace(uint8_t place)
{
    if (place < 2)
    {
        if (s_place == place && s_focus == place && plug::paramCount(place) > kParamCols)
            s_page ^= 1;   // second press on the focused insert: the next five parameters
        else if (s_focus != place)
            s_page = 0;
        s_focus = place;
    }
    s_place = place;
    clampPlace();
    showAll();
    showFrame();
}

void btn0() { pickPlace(0); }
void btn1() { pickPlace(1); }
void btn2() { pickPlace(2); }
void btn3() { pickPlace(3); }
void btn4() { pickPlace(4); }
void btn5() { pickPlace(5); }
void btn6() { pickPlace(6); }
void btnBack()
{
    plugUiLeave();
    MP->switchToMaster();
    MP->activateLabelsBorder();
}

void changePlugin(int k, int delta)
{
    // the order: None, then the index entries
    int pos = plug::slotIndex(k) + 1;   // a missing plugin counts as None here
    pos += delta;
    const int n = plug::indexCount() + 1;
    pos = pos < 0 ? 0 : (pos >= n ? n - 1 : pos);
    plug::assign(k, pos - 1);
    s_page = 0;
    markChanged();
    showAll();
}

void changeParam(int col, int delta)
{
    const uint32_t i = uint32_t(s_page * kParamCols + col - 2);
    const plug_param* pp = plug::param(s_focus, i);
    if (!pp) return;
    float step = pp->enum_count ? 1.0f : pp->step;
    if (step <= 0.0f) step = (pp->max - pp->min) / 100.0f;
    plug::setParam(s_focus, i, plug::paramValue(s_focus, i) + step * float(delta));
    markChanged();
    showParam(col);
}
}   // namespace

bool plugUiShiftHeld()
{
    return ModuleBase::buttons && ModuleBase::buttons->isButtonPressed(interfaceButtonShift);
}

// ------------------------------------------------------------------------------------------------ Saturation list
int plugUiSatCount()
{
    return int(kSaturationTypeEnd) + plug::indexCount();
}

int plugUiSatPos()
{
    const int ix = plug::slotIndex(0);
    return ix >= 0 ? int(kSaturationTypeEnd) + ix : int(mtProject.values.saturationType);
}

const char* plugUiSatText()
{
    return plug::slotName(0)[0] ? stateText(0) : saturationListTexts[mtProject.values.saturationType];
}

void plugUiSatList(strList* data)
{
    for (int i = 0; i < int(kSaturationTypeEnd); i++) s_sat[i] = saturationListTexts[i];
    for (int i = 0; i < plug::indexCount(); i++) s_sat[kSaturationTypeEnd + i] = plug::indexAt(i)->name;
    data->data = const_cast<char**>(s_sat);
    data->length = uint16_t(plugUiSatCount());
}

void plugUiSatSet(int pos)
{
    pos = pos < 0 ? 0 : (pos >= plugUiSatCount() ? plugUiSatCount() - 1 : pos);
    if (pos < int(kSaturationTypeEnd))
    {
        if (plug::slotName(0)[0]) plug::assign(0, -1);
        mtProject.values.saturationType = uint8_t(pos);
    }
    else
    {   // a plugin in insert 1, the mixers at hard clip under it (stock firmware plays this project with hard clip)
        mtProject.values.saturationType = uint8_t(SaturationType::kHardClip);
        plug::assign(0, pos - int(kSaturationTypeEnd));
    }
    engine.setSaturationType(SaturationType(mtProject.values.saturationType));
    markChanged();
}

// ------------------------------------------------------------------------------------------------ Inserts page
bool plugUiActive()
{
    return s_active;
}

void plugUiLeave()
{
    s_active = false;
}

void plugUiOpen()
{
    plug::rescan();   // files copied since boot show up
    s_active = true;
    s_place = 0;
    s_page = 0;
    s_focus = 0;
    MP->ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    MP->ip->clearButton(interfaceButtonShift);
    MP->ip->setButtonObj(interfaceButton0, buttonPress, btn0);
    MP->ip->setButtonObj(interfaceButton1, buttonPress, btn1);
    MP->ip->setButtonObj(interfaceButton2, buttonPress, btn2);
    MP->ip->setButtonObj(interfaceButton3, buttonPress, btn3);
    MP->ip->setButtonObj(interfaceButton4, buttonPress, btn4);
    MP->ip->setButtonObj(interfaceButton5, buttonPress, btn5);
    MP->ip->setButtonObj(interfaceButton6, buttonPress, btn6);
    MP->ip->setButtonObj(interfaceButton7, buttonPress, btnBack);

    display.setControlHide(MP->delayPingpongEnableList);
    display.setControlHide(MP->delaySyncEnableList);
    display.setControlHide(MP->delaySyncRateList);
    display.setControlHide(MP->limiterSidechainList);
    display.setControlHide(MP->saturationList);
    MP->titleBar->show();
    MP->resizeToDefaultMaster();
    MP->resizeBarControl(cMasterParams::displayMode::masterValues);
    for (uint8_t i = 0; i < 8; i++)
    {
        MP->barData[i].value = display.getTheme().white;
        display.setControlValue(MP->label[i], 1);
        display.setControlStyle2(MP->label[i], controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlShow(MP->label[i]);
        display.setControlHide(MP->barControl[i]);
        display.refreshControl(MP->barControl[i]);
        display.setControlHide(MP->trackNameLabel[i]);
        display.refreshControl(MP->trackNameLabel[i]);
        display.setControlHide(MP->trackVolumeBar[i]);
        display.refreshControl(MP->trackVolumeBar[i]);
    }
    display.setControlValue(MP->label[0], 0);
    display.setControlValue(MP->label[1], 0);
    display.setControlText(MP->label[7], "Back");
    display.setControlText2(MP->label[7], " ");
    display.refreshControl(MP->label[7]);
    for (int i = 0; i < 3; i++) display.setControlHide(MP->blankSpaceCover[i]);
    display.refreshControl(MP->bgLabel);
    display.setControlValue(MP->bgLabel, 255);
    MP->frameData.placesCount = 8;
    MP->frameData.startPlace = 0;
    for (int i = 0; i < 8; i++) MP->frameData.places[i] = &kPlaces[i][0];
    showAll();
    showFrame();
    display.synchronizeRefresh();
}

void plugUiUpdate(uint32_t nowMs)
{
    if (!s_active || nowMs - s_lastDraw < 250) return;
    s_lastDraw = nowMs;
    bool changed = false;
    for (int k = 0; k < plug::kSlots; k++)
        changed = changed || s_drawn[k] != plug::slotState(k) || s_drawnCount[k] != plug::paramCount(k);
    if (changed)
    {   // a load finished (or failed, or tripped): new name and new parameter columns
        clampPlace();
        showAll();
        showFrame();
    }
    else if (interfaceGlobals.devMode)
        showTitle();
}

void plugUiEncoder(int16_t value)
{
    if (s_place < 2)
        changePlugin(s_place, value > 0 ? 1 : -1);
    else if (s_place < 7)
        changeParam(s_place, value);
}

void plugUiArrow(int dir)   // 0 left, 1 right, 2 up, 3 down
{
    if (dir == 2 || dir == 3)
    {
        plugUiEncoder(dir == 2 ? 1 : -1);
        return;
    }
    uint8_t p = s_place;
    if (dir == 0 && p > 0) p--;
    if (dir == 1 && p < 7) p++;
    s_place = p;
    clampPlace();
    if (dir == 1 && s_place < p) s_place = 7;   // past the last parameter: Back
    if (s_place < 2 && s_focus != s_place)
    {
        s_focus = s_place;
        s_page = 0;
        showAll();
    }
    showFrame();
}

void plugUiDelete()
{
    if (s_place < 2)
    {
        if (plug::slotName(s_place)[0])
        {
            plug::assign(s_place, -1);
            markChanged();
            showAll();
        }
        return;
    }
    const uint32_t i = uint32_t(s_page * kParamCols + s_place - 2);
    const plug_param* pp = plug::param(s_focus, i);
    if (!pp || s_place > 6) return;
    plug::setParam(s_focus, i, pp->def);
    markChanged();
    showParam(s_place);
}

#endif   // FEATURE_PLUG_HOST
