// Plugin host UI (FEATURE_PLUG_HOST): the master insert plugins in the master screen's Saturation list and the generic
// "Master Inserts" page. See plug_host.h and synth-open/plug/README.md (Device use).
//
// Saturation list: the built-in entries (Soft Knee after Drive+ with FEATURE_SAT_SOFTKNEE, no Dirt entries without
// FEATURE_FF_DIRT: list positions are not saturationType values), then one "Inserts" entry (shown once a plugin is on the
// card or in the project). Inserts plays both master insert slots on top of the mixer stage of the Inserts page
// (saturationType = plug::kSatInserts, stock firmware loads it as Hard Clip), its value text names what is loaded
// ("Ins: Tape", "Tape+Dirt"). A built-in entry bypasses the inserts and keeps them (loaded, in the sidecar): Inserts again
// brings both back. Plugins are picked on the Inserts page only.
//
// Inserts page (Shift + the Saturation button on the master screen, or the last stop of the Master button repeat-press
// cycle while a plugin is installed, plugUiInCycle): column 1 is the mixer stage before the inserts (Hard
// Clip, Soft Clip, Soft Knee, Delete: Hard Clip), columns 2 and 3 pick the plugin of insert 1 and 2 (the encoder, Delete
// empties), columns 4 to 7 are the parameters of the focused insert, four at a time (press the insert's button again for
// the next four), the last column goes back (plug_ui_nav.h has the column numbers, 0 based). The plugin columns, the mixer column and every enum parameter are the stock list
// control (the master screen's own cList controls at their columns, two more for columns 4 and 7), numeric parameters a bar.
// Picking a plugin while a built-in Saturation entry is on switches the Saturation to Inserts.
//
// Lists move as the stock master screen's (plug_ui_nav.h): Down is the next entry below, Up the one above, the encoder
// follows the Invert list setting, and a press on the button of the selected Mixer or enum parameter column steps to the
// next entry, from the last back to the first. The plugin columns keep their second press for the parameter pages.
#ifdef FEATURE_PLUG_HOST

#include "plug_host.h"
#include "plug_ui.h"
#include "plug_ui_nav.h"

#include "core/interface.h"
#include "fileManager.h"
#include "masterParams/masterParams.h"
#include "mtAudioEngine.h"
#include "cr_section_macros.h"

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
using plugnav::colOfInsert;
using plugnav::insertOfCol;
using plugnav::kMixerCol;
using plugnav::kParamCol0;
using plugnav::kParamCols;
constexpr int kListLines = 14;  // rows of a 394 px list (27 px each), as the delay screen's sync rate list

const char* s_sat[kSaturationTypeEnd + 1];
uint8_t s_satVal[kSaturationTypeEnd];       // list position -> saturationType of the built-in entries
int s_satN;                                 // built-in entries in the list
// the mixer stages under Inserts (Inserts page column 3)
#ifdef FEATURE_SAT_SOFTKNEE
const uint8_t kMixerVals[] = {plug::kSatHardClip, plug::kSatSoftClip, plug::kSatSoftKnee};
#else
const uint8_t kMixerVals[] = {plug::kSatHardClip, plug::kSatSoftClip};
#endif
constexpr int kMixerN = int(sizeof(kMixerVals));
const char* s_mixerNames[kMixerN];
constexpr int kPickMax = plug::kIndexMax + 1;
// "None", then the index names: both plugin columns. In the SDRAM (1 KB, never constructed, showSlot fills it before it is bound)
__NOINIT(EXTERNAL_RAM) const char* s_pick[kPickMax];
strList s_list[7];                          // the data of the list in column c
hControl s_extra[2] = {-1, -1};             // the lists of columns 3 and 6 (created with the module's controls)
const char* const* s_bound[7];              // the entries list c was bound to (null: not bound, setData next time)
const char* s_blank[1] = {" "};             // what a column without a list holds: never a plugin's names (see showList)
bool s_listsMine;                           // the master screen's lists carry s_list data
char s_satText[12];
bool s_active;
uint8_t s_place, s_focus, s_page;
char s_val[8][20];
char s_pname[8][plug::kNameLen];             // the parameter names shown: copies, a label keeps the pointer it is given
char s_title[48];
uint32_t s_lastDraw;
plug::SlotState s_drawn[plug::kSlots];
uint32_t s_drawnCount[plug::kSlots];

plugnav::Shown shownOf(int k)
{
    using plugnav::Shown;
    switch (plug::slotState(k))
    {
        case plug::SlotState::kEmpty: return Shown::kNone;
        case plug::SlotState::kPending: return Shown::kLoading;
        case plug::SlotState::kMissing: return Shown::kMissing;
        case plug::SlotState::kFailed: return Shown::kFailed;
        case plug::SlotState::kSafe: return Shown::kSafe;
        case plug::SlotState::kTripped: return Shown::kOverCpu;
        case plug::SlotState::kBypassed: return Shown::kBypassed;
        case plug::SlotState::kNoRoom: return Shown::kNoRoom;
        default: return Shown::kName;
    }
}

// text cut to what the value font draws inside a column (the 9 characters of slotLabel fit, a wide name may not)
void clipToColumn(char* text)
{
    const uint32_t style = controlStyleFont2;
    const int len = getMaxTextLengthThatFitSpecificWidth(&fonts[FONT_INDEX_FROM_STYLE], text, 800 / 8 - 7);
    if (len >= 0 && len < int(strlen(text))) text[len] = 0;
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

// Column col bound to the one blank entry. The display draws every refreshed control, hidden ones too (cDisplay::step1 runs
// cList::update, which strlen()s each entry), so a hidden list must not keep an unloaded plugin's enum names: the next plugin
// is copied over them (the SRAM_L slot of both inserts) and the old char* array turns into code words, a fault on the
// first refresh (the hang on Up and Down through the plugin list, each step loading the next plugin).
void blankList(int col)
{
    const hControl h = listAt(col);
    s_bound[col] = nullptr;
    if (h < 0) return;
    strList& l = s_list[col];
    l.data = const_cast<char**>(s_blank);
    l.length = 1;
    l.linesCount = kListLines;
    l.start = 0;
    display.setControlData(h, &l);
}

// Column col as a list of n entries with entry pos highlighted (-1: none), or no list (n 0)
void showList(int col, const char* const* names, int n, int pos)
{
    const hControl h = listAt(col);
    if (h < 0) return;
    if (n <= 0)
    {
        blankList(col);   // the plugin may unload: never keep its names bound
        display.setControlHide(h);
        display.refreshControl(h);
        return;
    }
    strList& l = s_list[col];
    if (n > kPickMax) n = kPickMax;   // the insert picker: None and every index entry (the list control counts rows in int16)
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
    blankList(3);   // the two extra lists stay with the module: off any plugin's names
    blankList(6);
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
    const int col = colOfInsert(k);
    const int n = plug::indexCount();
    s_pick[0] = "None";
    for (int i = 0; i < n; i++) s_pick[i + 1] = plug::indexAt(i)->name;
    // the assigned plugin highlighted, None when empty, no highlight when it is not on the card (the label says Miss:)
    const int ix = plug::slotIndex(k);
    showList(col, s_pick, n + 1, ix >= 0 ? ix + 1 : (plug::slotName(k)[0] ? -1 : 0));
    plugnav::slotLabel(shownOf(k), plug::slotName(k), s_val[col]);   // "Safe:BBD", at most 9 characters
    clipToColumn(s_val[col]);
    display.setControlText(MP->label[col], k == 0 ? (s_focus == 0 ? "*Insert 1" : "Insert 1") : (s_focus == 1 ? "*Insert 2" : "Insert 2"));
    display.setControlText2(MP->label[col], s_val[col]);
    display.refreshControl(MP->label[col]);
}

int mixerPos()
{
    for (int i = 0; i < kMixerN; i++)
        if (kMixerVals[i] == plug::mixerStage()) return i;
    return 0;
}

void showMixer()
{
    for (int i = 0; i < kMixerN; i++) s_mixerNames[i] = saturationListTexts[kMixerVals[i]];
    const int pos = mixerPos();
    showList(kMixerCol, s_mixerNames, kMixerN, pos);
    display.setControlHide(MP->barControl[kMixerCol]);
    display.refreshControl(MP->barControl[kMixerCol]);
    display.setControlText(MP->label[kMixerCol], "Mixer");
    display.setControlText2(MP->label[kMixerCol], s_mixerNames[pos]);
    display.refreshControl(MP->label[kMixerCol]);
}

int pageCount()
{
    const int n = int(plug::paramCount(s_focus));
    return n > kParamCols ? (n + kParamCols - 1) / kParamCols : 1;
}

void showParam(int col)
{
    const uint32_t i = uint32_t(s_page * kParamCols + col - kParamCol0);
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
            snprintf(s_val[col], sizeof(s_val[col]), "%s", e >= 0 && e < int(pp->enum_count) ? pp->enum_names[e] : "?");
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
        snprintf(s_pname[col], sizeof(s_pname[col]), "%s", pp->name);   // never a pointer into the plugin image
        clipToColumn(s_pname[col]);
        clipToColumn(s_val[col]);
        display.setControlText(MP->label[col], s_pname[col]);
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
    {
        char pg[8] = "";
        if (pageCount() > 1) snprintf(pg, sizeof(pg), " %d/%d", s_page + 1, pageCount());
        snprintf(s_title, sizeof(s_title), "Master Inserts%s%s", pg, mtProject.values.saturationType == plug::kSatInserts ? "" : ", bypassed");
    }
    MP->titleBar->setTitle(s_title);
}

void showAll()
{
    s_listsMine = true;
    showTitle();
    showSlot(0);
    showSlot(1);
    showMixer();
    for (int c = kParamCol0; c < kParamCol0 + kParamCols; c++) showParam(c);
    for (int k = 0; k < plug::kSlots; k++)
    {
        s_drawn[k] = plug::slotState(k);
        s_drawnCount[k] = plug::paramCount(k);
    }
}

void clampPlace()
{
    const int n = int(plug::paramCount(s_focus)) - s_page * kParamCols;
    const int last = kParamCol0 - 1 + (n < 0 ? 0 : (n > kParamCols ? kParamCols : n));
    if (s_place > last && s_place < 7) s_place = uint8_t(last);
}

void changePlugin(int k, int delta)
{
    // the order: None, then the index entries. The load waits until the selection rests (plug::tick, kDebounceMs)
    const int n = plug::indexCount() + 1;
    const int pos = plugnav::clampStep(plug::slotIndex(k) + 1, n, delta);   // a missing plugin counts as None here
    plug::assign(k, pos - 1);
    // a plugin picked under a built-in Saturation entry: the Saturation goes to Inserts, or the pick would not be heard
    if (pos > 0 && mtProject.values.saturationType != plug::kSatInserts) plugUiSatSet(plugUiSatCount() - 1);   // the last entry: Inserts (a plugin is on the card)
    s_page = 0;
    markChanged();
    showAll();
}

void setMixer(int pos)
{
    if (kMixerVals[pos] == plug::mixerStage()) return;
    plug::setMixerStage(kMixerVals[pos]);
    markChanged();
    showMixer();
}

void changeMixer(int delta)
{
    setMixer(plugnav::clampStep(mixerPos(), kMixerN, delta));
}

void changeParam(int col, int delta)
{
    const uint32_t i = uint32_t(s_page * kParamCols + col - kParamCol0);
    const plug_param* pp = plug::param(s_focus, i);
    if (!pp) return;
    float step = pp->enum_count ? 1.0f : pp->step;
    if (step <= 0.0f) step = (pp->max - pp->min) / 100.0f;
    plug::setParam(s_focus, i, plug::paramValue(s_focus, i) + step * float(delta));
    markChanged();
    showParam(col);
}

// The parameter in column col (null: none)
const plug_param* paramAt(int col)
{
    return col >= kParamCol0 && col < kParamCol0 + kParamCols ? plug::param(s_focus, uint32_t(s_page * kParamCols + col - kParamCol0))
                                                              : nullptr;
}

// Column col is a stock list: both plugin columns, Mixer and every enum parameter
bool isList(int col)
{
    if (insertOfCol(col) >= 0 || col == kMixerCol) return true;
    const plug_param* pp = paramAt(col);
    return pp && pp->enum_count;
}

// Column col by delta (lists: entries, +1 is the next one below)
void change(int col, int delta)
{
    if (insertOfCol(col) >= 0)
        changePlugin(insertOfCol(col), delta > 0 ? 1 : -1);
    else if (col == kMixerCol)
        changeMixer(delta > 0 ? 1 : -1);
    else if (col < 7)
        changeParam(col, delta);
}

// A press on the button of the selected list column: the next entry, the first after the last (functSelectSaturation)
void stepNext(int col)
{
    if (col == kMixerCol)
    {
        setMixer(plugnav::nextWrap(mixerPos(), kMixerN));
        return;
    }
    const plug_param* pp = paramAt(col);
    if (!pp || !pp->enum_count) return;
    const uint32_t i = uint32_t(s_page * kParamCols + col - kParamCol0);
    plug::setParam(s_focus, i, float(plugnav::nextWrap(int(plug::paramValue(s_focus, i) + 0.5f), int(pp->enum_count))));
    markChanged();
    showParam(col);
}

void pickPlace(uint8_t place)
{
    const int k = insertOfCol(place);
    if (k >= 0)
    {   // second press on the focused insert: the next four parameters (the plugin list moves with the encoder or arrows)
        if (s_place == place && s_focus == k && plug::paramCount(k) > kParamCols)
            s_page = uint8_t((s_page + 1) % pageCount());
        else if (s_focus != k)
            s_page = 0;
        s_focus = uint8_t(k);
    }
    else if (s_place == place && isList(place))
    {   // second press on the selected Mixer or enum column: the next entry, as the master screen's Saturation button
        stepNext(place);
        return;
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
}   // namespace

void plugUiSlotGone(int k)
{   // plug_host.cpp unload(): before the slot's memory is reused (a new plugin copied in, the FlexNVM sector erased), no
    // control may still point into it. Lists keep the char* array they are bound to (labels get copies, showParam).
    if (!s_active || s_focus != k) return;
    for (int c = kParamCol0; c < kParamCol0 + kParamCols; c++) blankList(c);
}

void plugUiSynthLoading(const char* name)
{   // plug_synth.cpp synthEnsure, before an install: the main loop stops until it is done, so the popup is drawn here and now.
    // Before the display is up (the boot project's engines) there is nothing to draw on.
    if (!plug::synthUiUp()) return;
    static char text[24];
    snprintf(text, sizeof(text), "Loading %s", name);
    mtPopups.show(0, text);
    display.processDisplayUpdateImmediate();
}

bool plugUiShiftHeld()
{
    return ModuleBase::buttons && ModuleBase::buttons->isButtonPressed(interfaceButtonShift);
}

bool plugUiInCycle()
{
    return plugnav::insertsInCycle(plug::indexCount(), plug::slotName(0)[0] || plug::slotName(1)[0]);
}

const char* plugUiCycleTitle(int stop)
{   // N follows the cycle: the Inserts stop counts as page 4 when it is reachable
    static char title[3][32];
    const int n = plugnav::cyclePages(plugUiInCycle());
    const char* const base[3] = {"Master",
#ifdef FEATURE_SYNTH
                                 "Sample Track Mixer",
#else
                                 "Track Mixer",
#endif
                                 "Global Mixer"};
    if (stop < 0 || stop > 2) stop = 0;
    snprintf(title[stop], sizeof(title[stop]), "%s %d/%d", base[stop], stop + 1, n);
    return title[stop];
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

namespace
{
void satBuild()
{   // the built-in entries of this build in value order, the Dirt holes (5..9 without FEATURE_FF_DIRT) left out
    s_satN = 0;
    for (int v = 0; v < int(kSaturationTypeEnd); v++)
    {
#ifndef FEATURE_FF_DIRT
        if (v >= plug::kSatDirtFirst && v <= plug::kSatDirtLast) continue;
#endif
        s_satVal[s_satN] = uint8_t(v);
        s_sat[s_satN++] = saturationListTexts[v];
    }
    s_sat[s_satN] = "Inserts";
}
}   // namespace

int plugUiSatCount()
{
    satBuild();
    return s_satN + (satHasInserts() ? 1 : 0);
}

int plugUiSatPos()
{
    satBuild();
    const uint8_t t = mtProject.values.saturationType;
    if (t == plug::kSatInserts) return s_satN;
    for (int i = 0; i < s_satN; i++)
        if (s_satVal[i] == t) return i;
    return 0;
}

const char* plugUiSatText()
{
    const uint8_t t = mtProject.values.saturationType;
    if (t != plug::kSatInserts) return saturationListTexts[plugUiSatPos() < s_satN ? s_satVal[plugUiSatPos()] : 0];
    plug::insertsLabel(plug::slotName(0), plug::slotName(1), s_satText, 9);   // 9 characters fit the 94 px value field
    return s_satText;
}

void plugUiSatList(strList* data)
{
    satBuild();
    data->data = const_cast<char**>(s_sat);
    data->length = uint16_t(plugUiSatCount());
}

void plugUiSatSet(int pos)
{
    pos = pos < 0 ? 0 : (pos >= plugUiSatCount() ? plugUiSatCount() - 1 : pos);
    // Inserts: the reserved value, the engine runs hard clip under it. A built-in entry bypasses the inserts, they stay.
    mtProject.values.saturationType = pos < s_satN ? s_satVal[pos] : plug::kSatInserts;
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
    s_place = uint8_t(colOfInsert(0));
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
{   // as the master screen's encoder: a list follows the Invert list setting, a number does not
    change(s_place, plugnav::encoderDelta(value, isList(s_place), interfaceGlobals.invertValForList));
}

void plugUiArrow(int dir)   // 0 left, 1 right, 2 up, 3 down
{
    if (dir == plugnav::kUp || dir == plugnav::kDown)
    {   // as the master screen's arrows: Down is the next list entry below, Up a higher number
        change(s_place, plugnav::arrowDelta(dir, isList(s_place)));
        return;
    }
    uint8_t p = s_place;
    if (dir == 0 && p > 0) p--;
    if (dir == 1 && p < 7) p++;
    s_place = p;
    clampPlace();
    if (dir == 1 && s_place < p) s_place = 7;   // past the last parameter: Back
    if (insertOfCol(s_place) >= 0 && s_focus != insertOfCol(s_place))
    {
        s_focus = uint8_t(insertOfCol(s_place));
        s_page = 0;
        showAll();
    }
    showFrame();
}

void plugUiDelete()
{
    const int k = insertOfCol(s_place);
    if (k >= 0)
    {
        if (plug::slotName(k)[0])
        {
            plug::assign(k, -1);
            markChanged();
            showAll();
        }
        return;
    }
    if (s_place == kMixerCol)
    {   // Delete: Hard Clip, as stock Inserts ran
        changeMixer(-kMixerN);
        return;
    }
    const uint32_t i = uint32_t(s_page * kParamCols + s_place - kParamCol0);
    const plug_param* pp = plug::param(s_focus, i);
    if (!pp || s_place > 6) return;
    plug::setParam(s_focus, i, pp->def);
    markChanged();
    showParam(s_place);
}

#endif   // FEATURE_PLUG_HOST
