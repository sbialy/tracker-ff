// Plugin host UI (FEATURE_PLUG_HOST): the master insert plugins in the master screen's Saturation list and the generic
// "Master Inserts" page. See plug_host.h and synth-open/plug/README.md (Device use).
//
// Saturation list: the built-in entries, then one "Inserts" entry (shown once a plugin is on the card or in the project).
// Inserts plays both master insert slots on top of hard clip (saturationType = plug::kSatInserts, stock firmware loads it as
// Hard Clip), its value text names what is loaded ("Ins: Tape", "Tape+Dirt"). A built-in entry bypasses the inserts and keeps
// them (loaded, in the sidecar): Inserts again brings both back. Plugins are picked on the Inserts page only.
//
// Inserts page (Shift + the Saturation button on the master screen): column 1 and 2 pick the plugin of insert 1 and 2 (the
// encoder, Delete empties), columns 3 to 7 are the parameters of the focused insert, five at a time (press the insert's
// button again for the next five), the last column goes back. The plugin columns and every enum parameter are the stock list
// control (the master screen's own cList controls at their columns, two more for columns 4 and 7), numeric parameters a bar.
// Picking a plugin while a built-in Saturation entry is on switches the Saturation to Inserts.
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
constexpr int kListLines = 14;  // rows of a 394 px list (27 px each), as the delay screen's sync rate list

const char* s_sat[kSaturationTypeEnd + 1];
const char* s_pick[plug::kIndexMax + 1];   // "None", then the index names: both plugin columns
strList s_list[7];                          // the data of the list in column c
hControl s_extra[2] = {-1, -1};             // the lists of columns 3 and 6 (created with the module's controls)
const char* const* s_bound[7];              // the entries list c was bound to (null: not bound, setData next time)
bool s_listsMine;                           // the master screen's lists carry s_list data
char s_satText[12];
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

// The master screen's list controls sit at columns 0, 1, 2, 4 and 5 already (gMasterParams.cpp)
hControl listAt(int col)
{
    switch (col)
    {
        case 0: return MP->delayPingpongEnableList;
        case 1: return MP->delaySyncEnableList;
        case 2: return MP->delaySyncRateList;
        case 3: return s_extra[0];
        case 4: return MP->limiterSidechainList;
        case 5: return MP->saturationList;
        case 6: return s_extra[1];
        default: return -1;
    }
}

// Column col as a list of n entries with entry pos highlighted (-1: none), or no list (n 0)
void showList(int col, const char* const* names, int n, int pos)
{
    const hControl h = listAt(col);
    if (h < 0) return;
    if (n <= 0)
    {
        s_bound[col] = nullptr;   // the plugin may unload: never keep its names bound
        display.setControlHide(h);
        display.refreshControl(h);
        return;
    }
    strList& l = s_list[col];
    if (n > LIST_LENGTH_MAX) n = LIST_LENGTH_MAX;
    if (s_bound[col] != names || l.length != n)
    {   // new entries: bind them with the window on pos. Same entries: only the value, the list animates to it as stock
        l.data = const_cast<char**>(names);
        l.length = uint16_t(n);
        l.linesCount = kListLines;
        l.start = int16_t(pos < 0 ? 0 : (pos >= n ? n - 1 : pos));
        display.setControlData(h, &l);
        s_bound[col] = names;
    }
    display.setControlValue(h, pos);
    display.setControlShow(h);
    display.refreshControl(h);
}

// Back to the master screen's own list data (its show functions set the values again)
void restoreLists()
{
    if (!s_listsMine) return;
    s_listsMine = false;
    memset(s_bound, 0, sizeof(s_bound));
    display.setControlData(MP->delayPingpongEnableList, &MP->pingpongEnableData);
    display.setControlData(MP->delaySyncEnableList, &MP->syncEnableData);
    display.setControlData(MP->delaySyncRateList, &MP->syncRateData);
    display.setControlData(MP->limiterSidechainList, &MP->limiterSidechainData);
    display.setControlData(MP->saturationList, &MP->saturationListData);
    for (int c = 0; c < 7; c++)
    {
        display.setControlHide(listAt(c));
        display.refreshControl(listAt(c));
    }
}

void showFrame()
{
    display.setControlValue(MP->frameControl, s_place);
    display.setControlShow(MP->frameControl);
    display.refreshControl(MP->frameControl);
}

void showSlot(int k)
{
    const int n = plug::indexCount();
    s_pick[0] = "None";
    for (int i = 0; i < n; i++) s_pick[i + 1] = plug::indexAt(i)->name;
    // the assigned plugin highlighted, None when empty, no highlight when it is not on the card (the label says Missing)
    const int ix = plug::slotIndex(k);
    showList(k, s_pick, n + 1, ix >= 0 ? ix + 1 : (plug::slotName(k)[0] ? -1 : 0));
    if (plug::slotState(k) == plug::SlotState::kRunning)
        snprintf(s_val[k], sizeof(s_val[k]), "%s", plug::slotName(k));
    else if (plug::slotState(k) == plug::SlotState::kBypassed)
        snprintf(s_val[k], sizeof(s_val[k]), "Bypassed");
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
        showList(col, nullptr, 0, 0);
    }
    else
    {
        const float v = plug::paramValue(s_focus, i);
        if (pp->enum_count)
        {   // every entry in the stock list control, the current one highlighted
            const int e = int(v + 0.5f);
            snprintf(s_val[col], sizeof(s_val[col]), "%s", e >= 0 && e < pp->enum_count ? pp->enum_names[e] : "?");
            display.setControlHide(MP->barControl[col]);
            showList(col, pp->enum_names, pp->enum_count, e);
        }
        else
        {
            showList(col, nullptr, 0, 0);
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
        snprintf(s_title, sizeof(s_title), "Master Inserts%s%s", plug::paramCount(s_focus) > kParamCols ? (s_page ? " 2/2" : " 1/2") : "",
                 mtProject.values.saturationType == plug::kSatInserts ? "" : ", bypassed");
    MP->titleBar->setTitle(s_title);
}

void showAll()
{
    s_listsMine = true;
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
    // a plugin picked under a built-in Saturation entry: the Saturation goes to Inserts, or the pick would not be heard
    if (pos > 0 && mtProject.values.saturationType != plug::kSatInserts) plugUiSatSet(int(kSaturationTypeEnd));
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
namespace
{
bool satHasInserts()
{
    return plug::indexCount() > 0 || plug::slotName(0)[0] || plug::slotName(1)[0] ||
           mtProject.values.saturationType == plug::kSatInserts;
}
}   // namespace

int plugUiSatCount()
{
    return int(kSaturationTypeEnd) + (satHasInserts() ? 1 : 0);
}

int plugUiSatPos()
{
    const uint8_t t = mtProject.values.saturationType;
    return t == plug::kSatInserts ? int(kSaturationTypeEnd) : (t < kSaturationTypeEnd ? int(t) : 0);
}

const char* plugUiSatText()
{
    const uint8_t t = mtProject.values.saturationType;
    if (t != plug::kSatInserts) return saturationListTexts[t < kSaturationTypeEnd ? t : 0];
    plug::insertsLabel(plug::slotName(0), plug::slotName(1), s_satText, 9);   // 9 characters fit the 94 px value field
    return s_satText;
}

void plugUiSatList(strList* data)
{
    for (int i = 0; i < int(kSaturationTypeEnd); i++) s_sat[i] = saturationListTexts[i];
    s_sat[kSaturationTypeEnd] = "Inserts";
    data->data = const_cast<char**>(s_sat);
    data->length = uint16_t(plugUiSatCount());
}

void plugUiSatSet(int pos)
{
    pos = pos < 0 ? 0 : (pos >= plugUiSatCount() ? plugUiSatCount() - 1 : pos);
    // Inserts: the reserved value, the engine runs hard clip under it. A built-in entry bypasses the inserts, they stay.
    mtProject.values.saturationType = pos < int(kSaturationTypeEnd) ? uint8_t(pos) : plug::kSatInserts;
    engine.setSaturationType(SaturationType(mtProject.values.saturationType));
    plug::setInsertsOn(mtProject.values.saturationType == plug::kSatInserts);
    markChanged();
}

// ------------------------------------------------------------------------------------------------ Inserts page
bool plugUiActive()
{
    return s_active;
}

void plugUiLeave()
{
    if (s_active) restoreLists();   // the master screen's lists get their own data back
    s_active = false;
}

void plugUiInitControls()
{   // the master module's controls were just created: the page is closed, two more lists for columns 3 and 6
    s_active = false;
    s_listsMine = false;
    memset(s_bound, 0, sizeof(s_bound));
    for (int i = 0; i < 2; i++)
    {
        strList& l = s_list[i ? 6 : 3];
        l.data = const_cast<char**>(s_sat);
        l.length = 1;
        l.linesCount = kListLines;
        l.start = 0;
        strControlProperties p;
        p.style = controlStyleBackground | (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
        p.x = (800 / 8) * (i ? 6 : 3) + 2;
        p.y = 29;
        p.w = 800 / 8 - 3;
        p.h = 394;
        p.data = &l;
        p.colors = nullptr;
        s_extra[i] = display.createControl<cList>(&p);   // -1 when the pool is full: that column keeps the value text only
        display.setControlHide(s_extra[i]);
    }
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
