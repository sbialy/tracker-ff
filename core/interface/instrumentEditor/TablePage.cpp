// Table page of the instrument editor (Table lite, core/source/tables.h): the last page of a sample instrument (Parameters, Automation,
// Table) and of a synth part (Parameters, Macros, Automation, Table). The header holds the instrument's table (the binding, 0 none) and
// that table's Tic, Len and Loop/Once. The grid holds its 16 rows: N (transpose), V (volume %), FX and its value.
//   F1-F4    the header field to the encoder (Table, Tic, Len, Mode)
//   F5 Copy  the selected rows (grid) or the whole table (header)
//   F6 Paste the copied rows at the cursor row, or the copied table over this one
//   F7 Clear the selected rows (grid) or the whole table (header)
//   arrows   move the cursor (Down from the header enters the grid, Up from row 1 goes back), Shift+Up/Down selects rows
//   encoder  the value under the cursor, Delete empties the cell or resets the field
//   Fx, Vol, F8 held  the FX picker (every FX at once: encoder, arrows or a pad, the release sets it, Delete none)
//   pads     play the instrument (a live note starts its table, mtSequencer.cpp tableLiveNoteOn)
// The page writes the pool (SDRAM) directly, a byte at a time for a cell (the runner reads it in the audio interrupt), and marks the
// project changed: project.mtff gets the tables on the next save. The page state and the copy buffer are in SDRAM, no internal RAM.
#include "instrumentEditor/instrumentEditor.h"
#if GUI_TABLES

#include "core/interface.h"
#include "core/interfacePopups.h"
#include "fileManager.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "tableGridControl.h"
#include "tables.h"

#include <cr_section_macros.h>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include "noinit_check.h"

static cInstrumentEditor* IE = &instrumentEditor;

namespace
{
constexpr uint32_t kMagic = 0x54424C31; // "TBL1": the SDRAM block below is set up (it is not cleared at boot)
enum Place : uint8_t
{
    kTable,
    kTic,
    kLen,
    kMode,
    kGrid,
};
struct PageState
{
    uint32_t magic;
    table_grid_data_t grid;
    uint8_t place, row, col, anchor;
    uint8_t pick;         // 1: the FX picker is open (an FX key is held)
    uint8_t pickPos;      // its entry
    uint8_t clipRows;     // rows in clip (0 = empty)
    uint8_t clipWhole;    // 1: clipTable holds a whole table
    tables::Row clip[tables::kRows];
    tables::Table clipTable;
    char value[8][12];    // the label values (the labels keep the pointer)
};
NOINIT_PLAIN(PageState);
__NOINIT(EXTERNAL_RAM) PageState gTbl;

PageState& st()
{
    if (gTbl.magic != kMagic)
    {
        memset(&gTbl, 0, sizeof(gTbl));
        gTbl.magic = kMagic;
        gTbl.place = kTable;
    }
    return gTbl;
}

int instr()
{
    const int i = mtProject.values.lastUsedInstrument;
    return i >= 0 && i < tables::kInstruments ? i : 0;
}

uint8_t boundTable()
{
    const uint8_t t = tables::pool().bind[instr()];
    return t <= tables::kTables ? t : 0;
}

tables::Table* curTable()
{
    const uint8_t t = boundTable();
    return t ? &tables::pool().table[t - 1] : nullptr;
}

void changed()
{
    newFileManager.setProjectStructChanged();
}

// The FX a row may take, in the order of the FX picker: position 0 = none, then four columns filled down (kTablePickRows each): level and
// pitch | filter, sends and envelopes | LFOs and sample | CC. Right and Left in the picker keep the row, so the order puts the common FX
// at the top. It holds the FX of tables::rowFxAllowed exactly (tables_sim checks both ways).
using FxT = Sequencer::strFxConsts;
constexpr uint8_t kPickFx[] = {
    0, FxT::FX_TYPE_VOLUME, FxT::FX_TYPE_PANNING, FxT::FX_TYPE_RANDOM_VOLUME, FxT::FX_TYPE_MICROTUNING, FxT::FX_TYPE_TUNE, FxT::FX_TYPE_GLIDE,
    FxT::FX_TYPE_SLIDE_UP, FxT::FX_TYPE_SLIDE_DOWN,
    FxT::FX_TYPE_DELAY_SEND, FxT::FX_TYPE_REVERB_SEND, FxT::FX_TYPE_FILTER_LOWPASS, FxT::FX_TYPE_FILTER_HIGHPASS, FxT::FX_TYPE_FILTER_BANDPASS,
    FxT::FX_TYPE_OVERDRIVE, FxT::FX_TYPE_BIT_DEPTH, FxT::FX_TYPE_AMP_ENV, FxT::FX_TYPE_FILTER_ENV,
    FxT::FX_TYPE_VOLUME_LFO, FxT::FX_TYPE_PANNING_LFO, FxT::FX_TYPE_FILTER_LFO, FxT::FX_TYPE_POSITION_LFO, FxT::FX_TYPE_FINETUNE_LFO,
    FxT::FX_TYPE_POSITION, FxT::FX_TYPE_SAMPLE_SLICE, FxT::FX_TYPE_REVERSE_PLAYBACK, FxT::FX_TYPE_GRANULAR_LENGTH,
    FxT::FX_TYPE_SEND_CC_A, FxT::FX_TYPE_SEND_CC_B, FxT::FX_TYPE_SEND_CC_C, FxT::FX_TYPE_SEND_CC_D, FxT::FX_TYPE_SEND_CC_E, FxT::FX_TYPE_SEND_CC_F,
};
constexpr int kPickCount = sizeof(kPickFx);
static_assert(kPickCount <= 40, "table_grid_data_t pickName");
// FX id -> position in that list (0 none), and back
int fxToPos(uint8_t type)
{
    for (int i = 1; i < kPickCount; i++)
        if (kPickFx[i] == type) return i;
    return 0;
}
uint8_t posToFx(int pos)
{
    return pos > 0 && pos < kPickCount ? kPickFx[pos] : 0;
}

// the row a track plays of this table (the first such track), -1 none
int8_t playingRow(uint8_t t)
{
    for (int k = 0; t && k < 16; k++)
    {
        const tables::Run& r = tables::run(k);
        if (r.table == t && r.instr == instr())
            return int8_t(r.row);
    }
    return -1;
}

void format()
{
    PageState& s = st();
    table_grid_data_t& g = s.grid;
    const uint8_t t = boundTable();
    const tables::Table* tb = curTable();
    if (t) snprintf(g.head[0], sizeof(g.head[0]), "Table %d", t);
    else snprintf(g.head[0], sizeof(g.head[0]), "Table Off");
    snprintf(g.head[1], sizeof(g.head[1]), "Tic %d", tb ? tb->tic : tables::kTicDefault);
    snprintf(g.head[2], sizeof(g.head[2]), "Len %d", tb ? tb->len : tables::kRows);
    snprintf(g.head[3], sizeof(g.head[3]), "%s", (tb ? tb->loop : 1) ? "Loop" : "Once");
    g.bound = t ? 1 : 0;
    g.rows = tb ? tb->len : tables::kRows;
    g.place = s.place;
    g.row = s.row;
    g.col = s.col;
    g.selA = s.anchor < s.row ? s.anchor : s.row;
    g.selB = s.anchor < s.row ? s.row : s.anchor;
    g.playing = playingRow(t);
    g.pickN = s.pick ? kPickCount : 0;
    g.pickSel = s.pickPos;
    if (s.pick)
    {
        for (int i = 0; i < kPickCount; i++)
        {
            const uint8_t n = interfaceGlobals.fxIdToName(kPickFx[i]);
            g.pickName[i] = interfaceGlobals.ptrAllFxNamesShort[1 + n];
            g.pickLetter[i] = interfaceGlobals.ptrFxNames[n][0];
        }
        snprintf(g.pickLong, sizeof(g.pickLong), "%.31s", interfaceGlobals.ptrFxNames[interfaceGlobals.fxIdToName(kPickFx[s.pickPos])]);
        snprintf(g.pickHint, sizeof(g.pickHint), "Row %d FX", s.row + 1);
    }
    for (int r = 0; r < tables::kRows; r++)
    {
        char(&c)[4][8] = g.cell[r];
        const tables::Row* row = tb ? &tb->row[r] : nullptr;
        c[0][0] = c[1][0] = c[2][0] = c[3][0] = 0;
        g.fx[r][0] = 0;
        if (!row) continue;
        if (row->note != tables::kNoteEmpty) snprintf(c[0], sizeof(c[0]), "%+d", row->note);
        if (row->vol != tables::kVolEmpty) snprintf(c[1], sizeof(c[1]), "%d%%", row->vol);
        if (row->fxType)
        {
            const uint8_t n = interfaceGlobals.fxIdToName(row->fxType);
            snprintf(g.fx[r], sizeof(g.fx[r]), "%c %.14s", interfaceGlobals.ptrFxNames[n][0], interfaceGlobals.ptrAllFxNamesShort[1 + n]);
            snprintf(c[3], sizeof(c[3]), "%d", row->fxVal);
        }
    }
}

void labels()
{
    PageState& s = st();
    static const char* const kNames[8] = { "Table", "Tic", "Len", "Mode", "Copy", "Paste", "Clear", "Fx list" };
    char(&v)[8][12] = s.value;
    const tables::Table* tb = curTable();
    const uint8_t t = boundTable();
    if (t) snprintf(v[0], sizeof(v[0]), "%d", t);
    else snprintf(v[0], sizeof(v[0]), "Off");
    snprintf(v[1], sizeof(v[1]), "%d", tb ? tb->tic : tables::kTicDefault);
    snprintf(v[2], sizeof(v[2]), "%d", tb ? tb->len : tables::kRows);
    snprintf(v[3], sizeof(v[3]), "%s", (tb ? tb->loop : 1) ? "Loop" : "Once");
    if (s.place < kGrid) snprintf(v[4], sizeof(v[4]), "table");
    else snprintf(v[4], sizeof(v[4]), s.grid.selB > s.grid.selA ? "%d rows" : "%d row", s.grid.selB - s.grid.selA + 1);
    if (s.clipWhole) snprintf(v[5], sizeof(v[5]), "table");
    else if (s.clipRows) snprintf(v[5], sizeof(v[5]), s.clipRows > 1 ? "%d rows" : "%d row", s.clipRows);
    else v[5][0] = 0;
    snprintf(v[6], sizeof(v[6]), "%s", v[4]);
    snprintf(v[7], sizeof(v[7]), "%s", s.place == kGrid && boundTable() ? "hold Fx" : "");
    for (int i = 0; i < 8; i++)
    {
        display.setControlText(IE->label[i], kNames[i]);
        display.setControlText2(IE->label[i], v[i]);
        display.setControlColors(IE->label[i],
                                 (i < kGrid && i == s.place) ? display.getTheme().activeLabelsColors : display.getTheme().inactiveLabelsColors);
        display.refreshControl(IE->label[i]);
    }
}

void redraw()
{
    format();
    labels();
    display.setControlData(IE->tableGridControl, &st().grid);
    display.refreshControl(IE->tableGridControl);
}

int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void changeField(int place, int delta)
{
    tables::Table* tb = curTable();
    switch (place)
    {
        case kTable: tables::pool().bind[instr()] = uint8_t(clampi(boundTable() + delta, 0, tables::kTables)); break;
        case kTic:
            if (tb) tb->tic = uint8_t(clampi(tb->tic + delta, tables::kTicMin, tables::kTicMax));
            break;
        case kLen:
            if (tb) tb->len = uint8_t(clampi(tb->len + delta, 1, tables::kRows));
            break;
        case kMode:
            if (tb && delta) tb->loop = delta > 0 ? 1 : 0;
            break;
        default: return;
    }
    changed();
}

// a new FX starts at its smallest value (Volume at the instrument's level is a value away)
void setFx(tables::Row& r, uint8_t type)
{
    if (type == r.fxType) return;
    r.fxVal = type ? uint8_t(clampi(sequencer.getFxMin(type), 0, 255)) : 0;
    r.fxType = type;
}

void changeCell(int delta)
{
    PageState& s = st();
    tables::Table* tb = curTable();
    if (!tb) return;
    tables::Row& r = tb->row[s.row & 15];
    switch (s.col)
    {
        case 0: r.note = int8_t(clampi((r.note == tables::kNoteEmpty ? 0 : r.note) + delta, tables::kNoteMin, tables::kNoteMax)); break;
        case 1: r.vol = uint8_t(clampi((r.vol == tables::kVolEmpty ? 100 : r.vol) + delta, 0, tables::kVolMax)); break;
        case 2: setFx(r, posToFx(clampi(fxToPos(r.fxType) + delta, 0, kPickCount - 1))); break;
        case 3:
            if (r.fxType) r.fxVal = uint8_t(clampi(r.fxVal + delta, sequencer.getFxMin(r.fxType), sequencer.getFxMax(r.fxType)));
            break;
    }
    changed();
}

void clearCell()
{
    PageState& s = st();
    tables::Table* tb = curTable();
    if (!tb) return;
    tables::Row& r = tb->row[s.row & 15];
    switch (s.col)
    {
        case 0: r.note = tables::kNoteEmpty; break;
        case 1: r.vol = tables::kVolEmpty; break;
        default:
            r.fxType = 0;
            r.fxVal = 0;
            break;
    }
    changed();
}

void resetField(int place)
{
    tables::Table* tb = curTable();
    switch (place)
    {
        case kTable: tables::pool().bind[instr()] = 0; break;
        case kTic: if (tb) tb->tic = tables::kTicDefault; break;
        case kLen: if (tb) tb->len = tables::kRows; break;
        case kMode: if (tb) tb->loop = 1; break;
        default: return;
    }
    changed();
}

void copy()
{
    PageState& s = st();
    const tables::Table* tb = curTable();
    if (!tb) return;
    if (s.place < kGrid)
    {
        s.clipTable = *tb;
        s.clipWhole = 1;
        s.clipRows = 0;
    }
    else
    {
        const int a = s.grid.selA, b = s.grid.selB;
        for (int i = a; i <= b; i++) s.clip[i - a] = tb->row[i];
        s.clipRows = uint8_t(b - a + 1);
        s.clipWhole = 0;
    }
}

void paste()
{
    PageState& s = st();
    tables::Table* tb = curTable();
    if (!tb) return;
    if (s.clipWhole)
        *tb = s.clipTable;
    else if (s.clipRows)
    {
        const int at = s.place < kGrid ? 0 : s.row;
        for (int i = 0; i < s.clipRows && at + i < tables::kRows; i++) tb->row[at + i] = tables::rowSanitize(s.clip[i]);
    }
    else
        return;
    changed();
}

void clear()
{
    PageState& s = st();
    tables::Table* tb = curTable();
    if (!tb) return;
    if (s.place < kGrid)
        tables::tableClear(*tb);
    else
        for (int i = s.grid.selA; i <= s.grid.selB; i++) tables::rowClear(tb->row[i]);
    changed();
}

bool shiftHeld()
{
    return mtInterface.getPlatform()->getButtons()->isButtonPressed(interfaceButtonShift);
}

void moveRow(int delta)
{
    PageState& s = st();
    if (s.place < kGrid)
    {
        if (delta > 0 && boundTable())
        {
            s.place = kGrid;
            s.anchor = s.row;
        }
        return;
    }
    if (delta < 0 && s.row == 0 && !shiftHeld())
    {
        s.place = uint8_t(s.col < 4 ? int(s.col) : int(kTable));
        return;
    }
    s.row = uint8_t(clampi(s.row + delta, 0, tables::kRows - 1));
    if (!shiftHeld()) s.anchor = s.row;
}

// The FX picker (an FX key held: Fx, Vol or F8, as the pattern editor opens its FX list by a held key): every FX a row takes at once, the
// cursor starts on the row's FX. The encoder, the arrows (Up Down one, Left Right a column) and a pad (pad N = entry N, as in the pattern
// editor's list) move it, Delete picks none, the release sets the FX on the row. The page state keeps the picker, the grid control draws it
void pickLight()
{
    padsBacklight.clearGlobalPads(0, 1, 1);
    padsBacklight.setBackLayer(1, mtConfig.values.padsLightBack, st().pickPos);
}

void pickOpen()
{
    PageState& s = st();
    tables::Table* tb = curTable();
    if (!tb || s.pick) return;
    if (s.place < kGrid)
    {
        s.place = kGrid;
        s.anchor = s.row;
    }
    s.col = 2;
    s.pickPos = uint8_t(fxToPos(tb->row[s.row & 15].fxType));
    s.pick = 1;
    pickLight();
}

void pickMove(int pos)
{
    st().pickPos = uint8_t(clampi(pos, 0, kPickCount - 1));
    pickLight();
}

void pickClose()
{
    PageState& s = st();
    if (!s.pick) return;
    s.pick = 0;
    IE->lightUpPadBoard();
    tables::Table* tb = curTable();
    if (tb)
    {
        setFx(tb->row[s.row & 15], posToFx(s.pickPos));
        changed();
    }
}

void functPickKey(uint8_t state)
{
    if (state == buttonPress) pickOpen();
    else if (state == buttonRelease) pickClose();
    else return;
    redraw();
}

// ---- input
void functButtons(uint8_t button)
{
    PageState& s = st();
    if (s.pick) return;
    switch (button)
    {
        case 0:
        case 1:
        case 2:
        case 3: s.place = button; break;
        case 4: copy(); break;
        case 5: paste(); break;
        case 6: clear(); break;
        default: return;
    }
    redraw();
}

void functEncoder(int16_t value)
{
    PageState& s = st();
    if (s.pick)
    {
        pickMove(s.pickPos + value);
        redraw();
        return;
    }
    if (s.place < kGrid) changeField(s.place, value);
    else changeCell(value);
    redraw();
}

void functUp()
{
    if (st().pick) pickMove(st().pickPos - 1);
    else moveRow(-1);
    redraw();
}

void functDown()
{
    if (st().pick) pickMove(st().pickPos + 1);
    else moveRow(1);
    redraw();
}

void functLeft()
{
    PageState& s = st();
    if (s.pick) pickMove(s.pickPos - kTablePickRows);
    else if (s.place < kGrid)
    {
        if (s.place > 0) s.place--;
    }
    else if (s.col > 0)
        s.col--;
    redraw();
}

void functRight()
{
    PageState& s = st();
    if (s.pick) pickMove(s.pickPos + kTablePickRows);
    else if (s.place < kGrid)
    {
        if (s.place < kMode) s.place++;
    }
    else if (s.col < 3)
        s.col++;
    redraw();
}

void functDelete()
{
    PageState& s = st();
    if (s.pick) pickMove(0);
    else if (s.place < kGrid) resetField(s.place);
    else clearCell();
    redraw();
}

void functPads(uint8_t pad, uint8_t state, int16_t)
{
    if (st().pick)
    {
        if (state == buttonPress && pad < kPickCount)
        {
            pickMove(pad);
            redraw();
        }
        return;
    }
    if (isSynthInstrument(mtProject.values.lastUsedInstrument))
    {
        IE->playSynthNoteByPad(pad, state);
        return;
    }
    const bool slice = mtProject.values.lastUsedInstrument < kSampleInstrumentsCount &&
                       mtProject.instrument[mtProject.values.lastUsedInstrument].playMode == playModeBeatSlice;
    const uint8_t note = mtPadBoard.getNoteFromPad(pad, slice);
    if (state == buttonPress)
    {
        padsBacklight.setFrontLayer(1, mtConfig.values.padsLightFront, pad);
        sequencer.handleNoteOn(Sequencer::GRID_OUTSIDE_PATTERN, note, -1, pad);
    }
    else if (state == buttonRelease)
    {
        padsBacklight.setFrontLayer(0, 0, pad);
        sequencer.handleNoteOff(Sequencer::GRID_OUTSIDE_PATTERN, note, 0, pad);
    }
}

} // namespace

void cInstrumentEditor::setTablePageFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();
    for (uint8_t i = interfaceButton0; i <= interfaceButton6; i++) ip->setButtonObj(i, buttonPress, functButtons);
    ip->setPotObj(interfacePot0, functEncoder);
    ip->setButtonObj(interfaceButton7, functPickKey);
    ip->setButtonObj(interfaceButtonFx1, functPickKey);
    ip->setButtonObj(interfaceButtonFx2, functPickKey);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, functDelete);
    ip->setPadsGlobal(functPads);
    ip->clearButton(interfaceButtonInsert);
}

// The live playhead (called from update() while the page is shown): the mark follows the runner. Every 40 ms the playing row is read
// (a few SDRAM bytes), the control is redrawn only when the row changed or the runner ended, never otherwise
void cInstrumentEditor::tablePageTick(uint32_t nowMs)
{
    static uint32_t last;
    if (nowMs - last < 40) return;
    last = nowMs;
    PageState& s = st();
    const int8_t p = playingRow(boundTable());
    if (p == s.grid.playing) return;
    s.grid.playing = p;
    display.refreshControl(tableGridControl);
}

void cInstrumentEditor::showTablePage()
{
    PageState& s = st();
    s.pick = 0;
    s.place = kTable;    // the page always opens with the cursor on the Table field (the last visit's field is not kept)

    showTitleBar();

    display.setControlHide(frameControl);
    display.setControlHide(doubleLabelAutomation);
    display.setControlHide(instrumentSynthCCListControl);
    display.setControlHide(instrumentListControl);
    display.setControlHide(filterModeListControl);
    display.setControlHide(blankSpaceCover[0]);
    display.setControlHide(blankSpaceCover[1]);
    display.setControlHide(list1Control);
    display.setControlHide(list2Control);
    display.setControlHide(list3Control);
    display.setControlHide(list4Control);
    display.setControlHide(noteLabel);
    display.setControlHide(nnaLabel);
    display.setControlHide(paramsListControl);
    display.setControlHide(modelListControl);
    display.setControlHide(imgGridControl);
#if GUI_HANDLE_SYNTH
    hideSynthView();
#endif
    for (int i = 0; i < 6; i++) display.setControlHide(ccNameListControl[i]);
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlHide(barControl[i]);
        display.setControlValue(label[i], 1);
        display.setControlStyle2(label[i], controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setRemoveControlStyle(label[i], controlStyleShowBitmap);
        display.setRemoveControlStyle(label[i], controlStyleSpecialFx2);
        display.setControlPosition(label[i], (800 / 8) * i + (800 / 16), -1);
        display.setControlShow(label[i]);
    }

    display.setControlValue(bgLabel, 255);
    display.setControlShow(bgLabel);
    display.refreshControl(bgLabel);

    display.setControlShow(tableGridControl);
    redraw();
    display.synchronizeRefresh();
}

#endif
