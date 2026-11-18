// The Parts view of the Song screen (core/source/parts.h, ref/parts.md, design build/parts-ui/DESIGN.md). A table: Parts as rows
// (16 on screen), the 16 tracks as columns, a Bars column and the song map (core/libs/NewDisplay/partsTable.cpp). Shift + F8 or
// Shift + Song switches between the views, the view is what Play Song plays (the Parts mode of the list). Rec splits the F-row:
//
// The title bar says which: "Parts - ARRANGE" or "Parts - BUILD". Play Song stops after the last Part, unless Loop Song is on (F6 in
// Arrange, the kFlagLoop bit of Part 0). Loop Part (F1, Shift + Play) loops one Part as before
// Arrange (Rec off)
//   F1  Play Part (loop it) / while playing: Loop Part <-> Continue Song
//   F2  Play Song from the selected Part / while playing: Queue the selected Part (Shift: at the end of the pass), on the playing or
//       queued Part: Stop
//   F3  Go to Start (Shift: the Part that played last)   F4  Go to End   F5  Bake   F6  Loop Song on / off
//   F8  Tempo (stock, Shift + F8 the view)
//   Up / Down, encoder: select a Part (Shift + Up / Down: a range for Copy)   Play: Play Song / Stop, Shift + Play: loop the Part
//   Pads: the stock note preview. While a song plays and no key came for 4 s, the window follows the playing Part
// Build (Rec on)
//   F1-F8  track buttons of the half the cursor is in (1-8 or 9-16): hold + pad N = pattern N, hold + encoder = pattern +-1, a tap
//          alone = that track Off / back on in the selected Part
//   Pads   pattern N (bank of 48) for the held tracks, else the cursor cell(s). The lit pad: Off. Shift + pad 1-6: bank 1-6
//   Left / Right: cursor track, past track 16 onto the Length and the Repeats fields (Left goes back), the encoder changes the field there,
//   Up / Down: cursor Part (Shift + Up / Down: a range), encoder on a track cell: cursor Part
//   Shift + encoder: Length, one step per click (Auto, 1-128 steps)   Shift + pad 12-19: Length 1-8 bars, Shift + pad 12: Auto
//   Shift + Left / Right: Repeats (End, 1-64)
//   Length and Repeats are separate fields: a Part of 3 bars x 5 plays 240 steps, no repeat is hidden in the length
//   Hold a track button + Shift + encoder: the start step of that track in its source pattern (+-1), + Shift + pad 12-19: start at bar 1-8.
//   A track plays its source from the start step to the end of the pattern, and tiles that window, not the whole pattern
//   Shift + Copy: Unlink, the cursor track of the selected Part becomes a pattern of its own (its window, tiled, from step 1)
//   Copy: copy the selected Part(s)   Insert / Shift + Insert: paste after / before (nothing copied: a duplicate of the selected
//   Part, an empty list: Part 1 from the pattern on screen, tracks with no notes Off)   Delete: cells Off, Shift + Delete: Part(s)
#ifdef FEATURE_PARTS

#include "songEditor/songEditor.h"

#include "core/interface.h"
#include "core/interfacePopups.h"
#include "fileManager.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "parts.h"
#include "partsTableControl.h"

#include <cr_section_macros.h>
#include <stdio.h>
#include <string.h>

namespace
{
cSongEditor* const SE = &songEditor;

// All state of the view lives here, in SDRAM (no new SRAM, no static constructor: plain data, set up by uiInit)
struct Ui
{
    uint32_t magic;
    parts_table_data_t table;
    uint8_t patLen[256];      // steps of each source pattern as last seen (Auto lengths of the song map), 0 = not known
    uint8_t offMemory[16];    // Build: the pattern a track had before a tap turned it Off
    uint16_t sel;             // the selected Part (also the snapshot's place when the view is not on screen)
    uint16_t top;             // the first row on screen
    uint8_t selRows;          // rows of the selected range
    uint8_t cursor;           // Build: cursor track
    uint8_t field;            // Build: 0 the cursor cell, 1 the Length field, 2 the Repeats field of the selected Part (right of track 16)
    uint16_t held;            // track buttons held
    uint16_t heldUsed;        // held track buttons that did something (no toggle on release)
    uint8_t bank;             // pad bank 0..5 (patterns 1-48 ... 241-255), 0xFF: follow the cursor cell
    uint16_t copyFrom;        // Copy: the first Part and the count (0 = nothing copied)
    uint8_t copyCount;
    int16_t lastPlayed;       // the Part that played last (Shift + F3)
    uint32_t lastKeyMs;       // the last key of the view (the window follows the song after 4 s)
    uint32_t flashMs;         // snapshot tint start
    char title[32];
    char patt[8][12];         // Build F-row second lines
};
constexpr uint32_t kMagic = 0x50554931;   // "PUI1"
__NOINIT(EXTERNAL_RAM) Ui s_ui;

parts::List& L()
{
    return parts::list();
}

uint32_t now()
{
    return mtInterface.getPlatform()->getTimer()->millis();
}

void uiInit()
{
    if (s_ui.magic == kMagic) return;
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.magic = kMagic;
    s_ui.selRows = 1;
    s_ui.bank = 0xFF;
    s_ui.lastPlayed = -1;
    s_ui.table.playing = -1;
    s_ui.table.queued = -1;
    s_ui.table.flash = -1;
    s_ui.table.len = 16;
}

bool shift()
{
    return SE->buttons->isButtonPressed(interfaceButtonShift);
}

bool build()
{
    return SE->editMode != 0;
}

bool selectionValid()
{
    return L().count > 0 && s_ui.sel < L().count;
}

int rangeRows()
{
    int n = s_ui.selRows ? s_ui.selRows : 1;
    if (s_ui.sel + n > L().count) n = L().count - s_ui.sel;
    return n < 1 ? 1 : n;
}

// the window follows the selection with a 3 row margin
void follow(int part)
{
    const int rows = cPartsTable::kRows, margin = 3;
    int top = s_ui.top;
    if (part < top + margin) top = part - margin;
    if (part > top + rows - 1 - margin) top = part - (rows - 1 - margin);
    const int maxTop = L().count > rows ? L().count - rows : 0;
    if (top > maxTop) top = maxTop;
    if (top < 0) top = 0;
    s_ui.top = uint16_t(top);
}

void select(int part, bool keepRange = false)
{
    const int n = L().count;
    if (n == 0)
    {
        s_ui.sel = 0;
        s_ui.top = 0;
        s_ui.selRows = 1;
        return;
    }
    if (part < 0) part = 0;
    if (part >= n) part = n - 1;
    s_ui.sel = uint16_t(part);
    if (!keepRange) s_ui.selRows = 1;
    follow(part);
    SE->selectedSongPosition = uint8_t(part);
}

void edited()
{
    parts::changed();
    SE->partsRefresh();
}

void key()
{
    s_ui.lastKeyMs = now();
}

// the track a Build F-button stands for: the half the cursor is in
int trackOf(uint8_t button)
{
    return (s_ui.cursor >= 8 ? 8 : 0) + (button - interfaceButton0);
}

// a track of the pattern on screen that holds at least one note
bool trackHasNotes(int t)
{
    const Sequencer::strPattern* p = sequencer.getPatternToSaveToFile();
    if (p == nullptr) return true;
    const int last = Sequencer::getTrackLastStep(p, t);
    for (int s = 0; s <= last; s++)
        if (p->track[t].step[s].note != Sequencer::STEP_NOTE_EMPTY) return true;
    return false;
}

// Play Song loops (the flag of Part 0): kept on Part 0 only, whatever the list edits do to the order
void setSongLoop(bool on)
{
    for (int i = 0; i < L().count; i++) L().part[i].flags &= uint8_t(~parts::kFlagLoop);
    if (on && L().count) L().part[0].flags |= parts::kFlagLoop;
}

// ---- Parts list edits

// insert `n` Parts at `at` copied from `from` (indexes before the insert), or the pattern on screen when from < 0
bool insertParts(int at, int from, int n)
{
    parts::List& l = L();
    const bool loop = parts::songLoops(l);
    if (n < 1) n = 1;
    if (l.count + n > parts::kMax) n = parts::kMax - l.count;
    if (n < 1)
    {
        mtPopups.showInfoPopup("The Parts list is full", "255 Parts");
        return false;
    }
    if (at > l.count) at = l.count;
    memmove(&l.part[at + n], &l.part[at], size_t(l.count - at) * sizeof(parts::Part));
    memmove(l.start[at + n], l.start[at], size_t(l.count - at) * parts::kTracks);
    l.count = uint16_t(l.count + n);
    for (int i = 0; i < n; i++)
    {
        parts::Part& p = l.part[at + i];
        if (from < 0)
        {
            parts::partInit(p, mtProject.values.actualPattern);
            memset(l.start[at + i], 0, parts::kTracks);
            for (int t = 0; t < parts::kTracks; t++)
                if (!trackHasNotes(t)) p.src[t] = 0;
        }
        else
        {
            int k = from + i;
            if (k >= at) k += n;   // the source moved down with the insert
            p = l.part[k];
            memcpy(l.start[at + i], l.start[k], parts::kTracks);
            p.baked = 0;
        }
    }
    setSongLoop(loop);
    select(at);
    s_ui.selRows = uint8_t(n);
    return true;
}

void deleteParts()
{
    parts::List& l = L();
    if (!selectionValid()) return;
    const int at = s_ui.sel, n = rangeRows();
    const bool loop = parts::songLoops(l);
    memmove(&l.part[at], &l.part[at + n], size_t(l.count - at - n) * sizeof(parts::Part));
    memmove(l.start[at], l.start[at + n], size_t(l.count - at - n) * parts::kTracks);
    l.count = uint16_t(l.count - n);
    setSongLoop(loop);
    s_ui.copyCount = 0;
    select(at < l.count ? at : l.count - 1);
}

// the targets of a Build edit: the held tracks, or the cursor track (none while the cursor is on the Length or the Repeats field)
uint16_t targets()
{
    return s_ui.held ? s_ui.held : s_ui.field ? uint16_t(0) : uint16_t(1u << s_ui.cursor);   // a field has no cell: nothing is hit
}

void setCells(uint16_t mask, int pattern)
{
    for (int r = s_ui.sel; r < s_ui.sel + rangeRows(); r++)
        for (int t = 0; t < parts::kTracks; t++)
            if (mask & (1u << t))
            {
                L().part[r].src[t] = uint8_t(pattern);
                L().start[r][t] = 0;   // another pattern: a start step into the old one means nothing there
            }
}

// The steps Auto gives Part r: the window of its first source (the pattern length as last seen, 16 when not known yet)
int autoLength(int r)
{
    for (int t = 0; t < parts::kTracks; t++)
        if (L().part[r].src[t])
        {
            const int n = s_ui.patLen[L().part[r].src[t]];
            return parts::windowLen(n ? n : 16, L().start[r][t]);
        }
    return 16;
}

// Length: Auto (0), then 1 to 128 steps. rel: v steps from the length now (Auto counts as the steps it gives), else v is the length. The repeats
// are a field of their own
void setLength(int v, bool rel)
{
    for (int r = s_ui.sel; r < s_ui.sel + rangeRows(); r++)
    {
        parts::Part& p = L().part[r];
        int len = rel ? (p.length ? p.length : autoLength(r)) + v : v;
        if (len > parts::kMaxLength) len = parts::kMaxLength;
        if (len < 1) len = 0;
        p.length = uint8_t(len);
    }
}

// The start step of the tracks in `mask` in their source pattern (0 = step 1), 0 to 127. rel: v steps from now, else v is the start. A track
// that is Off has none
void setStart(uint16_t mask, int v, bool rel)
{
    for (int r = s_ui.sel; r < s_ui.sel + rangeRows(); r++)
        for (int t = 0; t < parts::kTracks; t++)
            if ((mask & (1u << t)) && L().part[r].src[t])
            {
                int st = (rel ? L().start[r][t] : 0) + v;
                if (st > parts::kMaxStart) st = parts::kMaxStart;
                if (st < 0) st = 0;
                L().start[r][t] = uint8_t(st);
            }
}

// Repeats: End (0), 1 to 64 passes
void stepRepeats(int delta)
{
    for (int r = s_ui.sel; r < s_ui.sel + rangeRows(); r++)
    {
        int rep = L().part[r].repeats + delta;
        if (rep > parts::kMaxRepeats) rep = parts::kMaxRepeats;
        if (rep < 0) rep = 0;
        L().part[r].repeats = uint8_t(rep);
    }
}

void stopPlayback()
{
    sequencer.stop();
}

// F1 while playing: Loop Part <-> Continue Song at the next step that is not the last of the pass (as cbkSongPlaybackModeToggle)
void cbkPartsLoopToggle()
{
    if (sequencer.isLastPatternStep()) return;   // retry in one step
    if (sequencer.isSongMode())
        sequencer.loopCurrentPattern(mtProject.song.playlistPos);
    else
        sequencer.continueSong(mtProject.song.playlistPos);
    sequencer.setOnGlobalStep(nullCallback);
}

void playFromSelection(bool loop)
{
    if (!sequencer.isStop())
        stopPlayback();
    else if (selectionValid() && parts::start(s_ui.sel, loop))
        SE->lastPartPos = -2;
}

// ---- pad lights in Build: bright = a target track plays this pattern, dim = another track of the Part does
int padBank()
{
    if (s_ui.bank != 0xFF) return s_ui.bank;
    if (!selectionValid()) return 0;
    const int v = L().part[s_ui.sel].src[s_ui.cursor];
    return v > 0 ? (v - 1) / 48 : 0;
}

void lightPads()
{
    padsBacklight.clearGlobalPads(0, 1, 1);
    if (!build() || !selectionValid()) return;
    const parts::Part& p = L().part[s_ui.sel];
    const int base = padBank() * 48;
    const uint16_t tg = targets();
    for (int t = 0; t < parts::kTracks; t++)
    {
        const int v = p.src[t] - 1 - base;
        if (p.src[t] == 0 || v < 0 || v >= 48 || (tg & (1u << t))) continue;
        padsBacklight.setBackLayer(1, mtConfig.values.padsLightBackDim, uint8_t(v));
    }
    for (int t = 0; t < parts::kTracks; t++)
    {
        const int v = p.src[t] - 1 - base;
        if (p.src[t] == 0 || v < 0 || v >= 48 || !(tg & (1u << t))) continue;
        padsBacklight.setBackLayer(1, mtConfig.values.padsLightBack, uint8_t(v));
    }
}

// ---- keys

void fKey(uint8_t button, uint8_t state)
{
    if (SE->isBusy) return;
    key();
    const int n = button - interfaceButton0;
    if (build())
    {
        const int t = trackOf(button);
        const uint16_t bit = uint16_t(1u << t);
        if (state == buttonPress)
        {
            s_ui.held |= bit;
            s_ui.heldUsed &= uint16_t(~bit);
            SE->partsRefresh();
        }
        else if (state == buttonRelease)
        {
            if ((s_ui.held & bit) && !(s_ui.heldUsed & bit) && selectionValid())
            {   // a tap alone: the track Off, or back to the pattern it had
                for (int r = s_ui.sel; r < s_ui.sel + rangeRows(); r++)
                {
                    uint8_t& v = L().part[r].src[t];
                    if (v)
                    {
                        s_ui.offMemory[t] = v;
                        v = 0;
                    }
                    else
                        v = s_ui.offMemory[t] ? s_ui.offMemory[t] : mtProject.values.actualPattern;
                }
                s_ui.held &= uint16_t(~bit);
                edited();
                return;
            }
            s_ui.held &= uint16_t(~bit);
            SE->partsRefresh();
        }
        return;
    }
    if (state != buttonPress) return;
    const bool sh = shift();
    switch (n)
    {
        case 0:   // Play Part / Loop Part <-> Continue Song
            if (parts::playing())
                sequencer.setOnGlobalStep(cbkPartsLoopToggle);
            else
                playFromSelection(true);
            break;
        case 1:   // Play Song / Queue
            if (parts::playing())
            {
                const int at = s_ui.sel;
                if (at == parts::currentPart() || at == parts::queuedPart() || !selectionValid())
                    stopPlayback();
                else
                    parts::queue(at, sh);
            }
            else
                playFromSelection(false);
            break;
        case 2:   // Go to Start, Shift: the Part that played last
            if (sh && s_ui.lastPlayed >= 0)
                select(s_ui.lastPlayed);
            else
                select(0);
            SE->partsRefresh();
            break;
        case 3:   // Go to End: the last Part before End
        {
            const int n2 = parts::songLength(L());
            select(n2 > 0 ? n2 - 1 : 0);
            SE->partsRefresh();
            break;
        }
        case 4:   // Bake
        {
            if (!sequencer.isStop())
            {
                mtPopups.showInfoPopup("Stop the playback", "to bake the Parts");
                break;
            }
            const int b = parts::bake();
            static char line[32];
            if (b < 0)
                mtPopups.showInfoPopup("Bake failed", parts::note());
            else
            {
                snprintf(line, sizeof(line), "%d patterns written", b);
                mtPopups.showInfoPopup("Parts baked to the song", line);
                SE->partsEnterView(false);   // the playlist now holds the baked song
            }
            break;
        }
        case 5:   // Loop Song on / off
            if (L().count)
            {
                setSongLoop(!parts::songLoops(L()));
                edited();
            }
            break;
        default: break;
    }
}

void encoder(int16_t value)
{
    if (SE->isBusy) return;
    key();
    if (!build() && SE->selectedPlace == 2)
    {
        SE->changeGlobalTempo((shift() ? 0.1f : 1.0f) * float(value));
        return;
    }
    if (L().count == 0) return;
    if (build() && s_ui.held && selectionValid() && shift())
    {   // held tracks + Shift: the start step of those tracks in their source, +-1 per click
        setStart(s_ui.held, value, true);
        s_ui.heldUsed |= s_ui.held;
        edited();
        return;
    }
    if (build() && s_ui.held && selectionValid())
    {   // held tracks: pattern +-1 (an Off cell starts at the pattern on screen)
        for (int t = 0; t < parts::kTracks; t++)
            if (s_ui.held & (1u << t))
                for (int r = s_ui.sel; r < s_ui.sel + rangeRows(); r++)
                {
                    uint8_t& v = L().part[r].src[t];
                    int x = v ? v + value : mtProject.values.actualPattern;
                    if (x < 1) x = 1;
                    if (x > PATTERN_INDEX_MAX) x = PATTERN_INDEX_MAX;
                    if (x != v) L().start[r][t] = 0;
                    v = uint8_t(x);
                }
        s_ui.heldUsed |= s_ui.held;
        edited();
        return;
    }
    if (build() && s_ui.field && selectionValid())
    {   // the cursor is on the Length or the Repeats field: the same edit as Shift + encoder / Shift + Left, Right
        if (s_ui.field == 1)
            setLength(value, true);
        else
            stepRepeats(value);
        edited();
        return;
    }
    if (build() && shift() && selectionValid())
    {
        setLength(value, true);
        edited();
        return;
    }
    select(s_ui.sel + value);
    SE->partsRefresh();
}

void upDown(int d)
{
    if (SE->isBusy) return;
    key();
    if (!build() && SE->selectedPlace == 2)
    {
        SE->changeGlobalTempo((shift() ? 0.1f : 1.0f) * float(-d));
        return;
    }
    if (L().count == 0) return;
    if (shift())
    {   // a range for Copy / Delete: grows down from the selected Part
        int rows = s_ui.selRows + d;
        if (rows < 1) rows = 1;
        if (s_ui.sel + rows > L().count) rows = L().count - s_ui.sel;
        s_ui.selRows = uint8_t(rows);
        follow(s_ui.sel + rows - 1);
    }
    else
        select(s_ui.sel + d);
    SE->partsRefresh();
}

void fUp()
{
    upDown(-1);
}
void fDown()
{
    upDown(1);
}

void leftRight(int d)
{
    if (SE->isBusy) return;
    key();
    if (!build())
    {   // Arrange: the table and the Tempo field
        SE->selectedPlace = d > 0 ? 2 : 1;
        SE->activateLabelsBorder();
        return;
    }
    if (shift())
    {   // Shift + Left / Right: Repeats -1 / +1 of the selected Part(s), a held key repeats (the interface turns it into presses)
        if (selectionValid())
        {
            stepRepeats(d);
            edited();
        }
        return;
    }
    if (s_ui.field)
    {   // on Length / Repeats: Right goes to the next field (Repeats is the last), Left back to Length, then to track 16
        s_ui.field = uint8_t(s_ui.field + (d > 0 ? (s_ui.field < 2 ? 1 : 0) : -1));
        SE->partsRefresh();
        return;
    }
    if (d > 0 && s_ui.cursor == 15)
    {   // past track 16: the Length field
        s_ui.field = 1;
        SE->partsRefresh();
        return;
    }
    int c = s_ui.cursor + d;
    if (c < 0) c = 0;
    if (c > 15) c = 15;
    if ((c >= 8) != (s_ui.cursor >= 8)) s_ui.held = 0;   // the F-row flips to the other half
    s_ui.cursor = uint8_t(c);
    s_ui.bank = 0xFF;
    SE->partsRefresh();
}

void fLeft()
{
    leftRight(-1);
}
void fRight()
{
    leftRight(1);
}

void fPlay()
{
    if (SE->isBusy) return;
    key();
    playFromSelection(shift());
}

// Shift + Copy: the cursor track of the selected Part becomes a pattern of its own (parts::unlink)
void unlinkCursor()
{
    if (!sequencer.isStop())
    {
        mtPopups.showInfoPopup("Stop the playback", "to unlink a track");
        return;
    }
    const int n = parts::unlink(s_ui.sel, s_ui.cursor);
    static char line[28];
    if (n == 0)
    {
        mtPopups.showInfoPopup("Unlink failed", parts::note());
        return;
    }
    snprintf(line, sizeof(line), "Track %d is pattern %d", s_ui.cursor + 1, n);
    mtPopups.showInfoPopup("Unlinked", line);
    edited();
}

void fCopy()
{
    if (SE->isBusy || !build() || !selectionValid()) return;
    key();
    if (shift())
    {
        if (!s_ui.field) unlinkCursor();   // a field has no cursor track
        return;
    }
    s_ui.copyFrom = s_ui.sel;
    s_ui.copyCount = uint8_t(rangeRows());
    static char line[24];
    if (s_ui.copyCount == 1)
        snprintf(line, sizeof(line), "Part %d", s_ui.sel + 1);
    else
        snprintf(line, sizeof(line), "Parts %d-%d", s_ui.sel + 1, s_ui.sel + s_ui.copyCount);
    mtPopups.showInfoPopup("Copied", line);
}

void fInsert()
{
    if (SE->isBusy || !build()) return;
    key();
    const bool before = shift();
    bool ok;
    if (L().count == 0)
        ok = insertParts(0, -1, 1);
    else
    {
        const int at = before ? s_ui.sel : s_ui.sel + rangeRows();
        if (s_ui.copyCount && s_ui.copyFrom + s_ui.copyCount <= L().count)
            ok = insertParts(at, s_ui.copyFrom, s_ui.copyCount);
        else
            ok = insertParts(at, s_ui.sel, 1);
    }
    if (ok)
    {
        SE->partsReadList();
        edited();
    }
}

void fDelete()
{
    if (SE->isBusy || !build() || !selectionValid()) return;
    key();
    if (shift())
        deleteParts();
    else
        setCells(targets(), 0);
    SE->partsReadList();
    edited();
}

void pads(uint8_t pad, uint8_t state, int16_t)
{
    if (SE->isBusy || state != buttonPress) return;
    key();
    if (shift())
    {
        if (pad < 6)
        {
            s_ui.bank = pad;
            lightPads();
        }
        else if (pad >= 11 && pad < 20 && selectionValid())
        {   // the bar pads: Length 1-8 bars (pad 12-19, pad 11 Auto), with tracks held their start at bar 1-8 (pad 11 and 12: step 1)
            const int bars = pad - 11;
            if (s_ui.held)
            {
                setStart(s_ui.held, bars ? (bars - 1) * 16 : 0, false);
                s_ui.heldUsed |= s_ui.held;
            }
            else
                setLength(bars * 16, false);
            edited();
        }
        return;
    }
    if (!selectionValid()) return;
    const int pattern = padBank() * 48 + pad + 1;
    if (pattern > PATTERN_INDEX_MAX) return;
    const uint16_t tg = targets();
    if (!tg) return;   // the cursor is on the Length / Repeats field
    bool all = true;
    for (int t = 0; t < parts::kTracks; t++)
        if ((tg & (1u << t)) && L().part[s_ui.sel].src[t] != pattern) all = false;
    setCells(tg, all ? 0 : pattern);
    s_ui.heldUsed |= s_ui.held;
    edited();
}

}   // namespace

// a source pattern's length, as the assembly or the pattern on screen saw it (the Auto lengths of the song map). pat 0: forget all
void partsNoteSourceLength(uint8_t pat, uint8_t len)
{
    uiInit();
    if (pat == 0)
        memset(s_ui.patLen, 0, sizeof(s_ui.patLen));
    else
        s_ui.patLen[pat] = len;
}

void cSongEditor::partsBind()
{
    uiInit();
    ip->setPotObj(interfacePot0, encoder, false);
    for (uint8_t b = interfaceButton0; b <= interfaceButton6; b++) ip->setButtonObj(b, fKey);
    if (editMode)
    {
        ip->setButtonObj(interfaceButton7, fKey);
        ip->setPadsGlobal(pads);
    }
    ip->setButtonObj(interfaceButtonPlay, buttonPress, fPlay);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, fLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, fRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, fUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, fDown);
    ip->setButtonObj(interfaceButtonInsert, buttonPress, fInsert);
    ip->setButtonObj(interfaceButtonCopy, buttonPress, fCopy);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, fDelete);
}

void cSongEditor::partsModeChanged()
{
    s_ui.held = 0;
    s_ui.heldUsed = 0;
    s_ui.bank = 0xFF;
    if (editMode && selectedPlace == 2)
    {
        selectedPlace = 1;
        activateLabelsBorder();
    }
    setDefaultScreenFunct();
    if (!editMode) lightUpPadBoard();
    lastPartsLabels = 0xFF;
    partsRefresh();
}

void cSongEditor::partsReadList()
{
    uiInit();
    parts::List& l = L();
    songLength = l.count;
    if (s_ui.sel >= l.count) s_ui.sel = l.count ? uint16_t(l.count - 1) : 0;
    if (s_ui.sel + s_ui.selRows > l.count) s_ui.selRows = 1;
    follow(s_ui.sel);
    selectedSongPosition = uint8_t(s_ui.sel);
    if (sequencer.isStop() && !parts::playing())
        s_ui.patLen[mtProject.values.actualPattern] = uint8_t(sequencer.getPatternLength());
    // the stock grid is hidden in this view: an empty list for it, its names are freed (cleanPatternsListResources)
    patternsList.length = 0;
    patternsList.data = nullptr;
    patternsList.names = nullptr;
    songPlayerData.songLength = 0;
    parts_table_data_t& d = s_ui.table;
    d.rec = l.part[0].src;
    d.stride = sizeof(parts::Part);
    d.start = l.start[0];
    d.patLen = s_ui.patLen;
    display.setControlData(partsTableControl, &d);
}

void cSongEditor::partsRefresh()
{
    uiInit();
    parts_table_data_t& d = s_ui.table;
    d.count = L().count;
    d.top = s_ui.top;
    d.sel = s_ui.sel;
    d.selRows = uint8_t(rangeRows());
    d.cursor = s_ui.cursor;
    d.field = s_ui.field;
    d.build = editMode ? 1 : 0;
    d.held = s_ui.held;
    d.tempo = mtProject.values.globalTempo;
    d.songLoop = parts::songLoops(L()) ? 1 : 0;
    if (d.flash >= 0 && now() - s_ui.flashMs > 3000) d.flash = -1;
    lastPartsLabels = 0xFF;
    partsShowLabels();
    display.refreshControl(partsTableControl);
    lightPads();
}

void cSongEditor::partsEnterView(bool on)
{
    if (on == partsView) return;
    if (!sequencer.isStop())
    {
        mtPopups.showInfoPopup("Stop the playback", "to switch between Patterns and Parts");
        return;
    }
    uiInit();
    parts::List& l = L();
    if (on)
    {
        stockSongPosition = selectedSongPosition;
        cleanPatternsListResources();
        partsView = true;
        l.mode = parts::kModeParts;
        partsReadList();
    }
    else
    {
        partsView = false;
        l.mode = parts::kModePlaylist;
        selectedSongPosition = stockSongPosition;
        readSong();
    }
    if (l.count) parts::changed();   // the view is the list's mode: stored with the list (no list, no file)
    songPlayerData.selection.isActive = 0;
    if (selectedPlace == 0) selectedPlace = 1;
    lastPartsLabels = 0xFF;
    lastPartPos = -2;
    showDefaultScreen();
    activateLabelsBorder();
    setDefaultScreenFunct();
    if (!partsView) lightUpPadBoard();
}

void cSongEditor::partsShowLabels()
{
    uiInit();
    // 0 stopped, 1 playing a Part in song mode, 2 looping a Part, 3 something else plays, +8 with Shift, +16 Build, +32 Loop Song on
    uint8_t state = 0;
    if (parts::playing())
        state = sequencer.isSongMode() ? 1 : 2;
    else if (!sequencer.isStop())
        state = 3;
    const bool sh = shift();
    const bool loop = parts::songLoops(L());
    state |= (sh ? 8 : 0) | (editMode ? 16 : 0) | (loop ? 32 : 0);

    // title: the mode (always), the selected Part, while playing the playing Part and the queued one
    const int cur = parts::currentPart(), q = parts::queuedPart();
    const char* const mode = editMode ? "BUILD" : "ARRANGE";
    if (cur >= 0 && q >= 0)
        snprintf(s_ui.title, sizeof(s_ui.title), "Parts - %s  Part %d > %d", mode, cur + 1, q + 1);
    else if (cur >= 0 || L().count)
        snprintf(s_ui.title, sizeof(s_ui.title), "Parts - %s  Part %d", mode, (cur >= 0 ? cur : s_ui.sel) + 1);
    else
        snprintf(s_ui.title, sizeof(s_ui.title), "Parts - %s", mode);
    titleBar->setTitle(s_ui.title);

    if (editMode)
    {   // Build: track buttons, name over the pattern of the selected Part ("Off"), held = red. F8 is track 8 / 16 here, in the style of
        // the other labels (the Tempo field is not in this mode)
        display.setControlStyle2(label[7], controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        for (int i = 0; i < 8; i++)
        {
            const int t = (s_ui.cursor >= 8 ? 8 : 0) + i;
            char* name = t < kSampleTracks ? &mtProject.values.TrackNames[t][0] : &mtProject.values.midiTrackNames[t - kSampleTracks][0];
            name[7] = 0;
            const int v = selectionValid() ? L().part[s_ui.sel].src[t] : 0;
            if (v)
                snprintf(s_ui.patt[i], sizeof(s_ui.patt[i]), "Patt: %d", v);
            else
                snprintf(s_ui.patt[i], sizeof(s_ui.patt[i]), "Off");
            display.setControlText(label[i], name);
            display.setControlText2(label[i], s_ui.patt[i]);
            display.setControlColors(label[i], (s_ui.held & (1u << t)) ? display.getTheme().performActiveTrackLabelsColors
                                                                       : display.getTheme().activeButtonLabelsColors);
            display.refreshControl(label[i]);
        }
        lastPartsLabels = state;
        return;
    }
    if (state == lastPartsLabels) return;
    lastPartsLabels = state;
    for (int i = 0; i < 8; i++) display.setControlColors(label[i], display.getTheme().activeButtonLabelsColors);
    display.setControlStyle2(label[7], controlStyleCenterX | controlStyleCenterY | controlStyleFont2);   // the Tempo field's style
    switch (state & 7)
    {
        case 0:
            display.setControlText(label[0], "Play");
            display.setControlText2(label[0], "Part");
            display.setControlText(label[1], "Play");
            display.setControlText2(label[1], "Song");
            break;
        case 1:
            display.setControlText(label[0], "Loop");
            display.setControlText2(label[0], "Part");
            display.setControlText(label[1], "Queue");
            display.setControlText2(label[1], sh ? "after pass" : "Part");
            break;
        case 2:
            display.setControlText(label[0], "Continue");
            display.setControlText2(label[0], "Song");
            display.setControlText(label[1], "Queue");
            display.setControlText2(label[1], sh ? "after pass" : "Part");
            break;
        default:
            display.setControlText(label[0], "Stop");
            display.setControlText2(label[0], "");
            display.setControlText(label[1], "Stop");
            display.setControlText2(label[1], "");
            break;
    }
    display.setControlText(label[2], "Go to");
    display.setControlText2(label[2], sh ? "Last played" : "Start");
    display.setControlText(label[3], "Go to");
    display.setControlText2(label[3], "End");
    display.setControlText(label[4], "Bake");
    display.setControlText2(label[4], "");
    display.setControlText(label[5], "Loop Song");
    display.setControlText2(label[5], loop ? "On" : "Off");
    if (loop) display.setControlColors(label[5], display.getTheme().performActiveTrackLabelsColors);
    display.setControlText(label[6], "");
    display.setControlText2(label[6], "");
    for (int i = 0; i < 7; i++) display.refreshControl(label[i]);
    if (!sh) refreshTempoField();
}

void cSongEditor::partsUpdate()
{
    uiInit();
    parts_table_data_t& d = s_ui.table;
    const int cur = parts::currentPart();
    if (cur >= 0) s_ui.lastPlayed = int16_t(cur);
    if (cur >= 0 && !editMode && now() - s_ui.lastKeyMs > 4000)
    {   // the window follows the playing Part when no key came for 4 s (the selection stays)
        const int top0 = s_ui.top;
        follow(cur);
        if (top0 != s_ui.top) lastPartPos = -2;
    }
    d.playing = int16_t(cur);
    d.queued = int16_t(parts::queuedPart());
    d.loop = cur >= 0 && !sequencer.isSongMode();
    if (cur >= 0)
    {
        d.len = uint8_t(sequencer.getPatternLength());
        d.pos = uint8_t(sequencer.getActualPos());
    }
    if (cur != lastPartPos)
    {
        lastPartPos = int16_t(cur);
        lastPartsLabels = 0xFF;
    }
    d.count = L().count;
    d.top = s_ui.top;
    d.tempo = mtProject.values.globalTempo;
    d.songLoop = parts::songLoops(L()) ? 1 : 0;
    if (d.flash >= 0 && now() - s_ui.flashMs > 3000) d.flash = -1;
    partsShowLabels();
    display.refreshControl(partsTableControl);
}

int partsSelected()
{
    uiInit();
    return s_ui.sel;
}

// Performance screen Insert (fPerformance.cpp): the per-track patterns as a new Part after the selected one, a track muted in
// Performance is Off, length Auto. The Parts view selection moves to it, so the next Insert appends in order. The answer goes to
// the caller's title bar: the Performance screen fills most of the display list, a popup over it does not fit the frame
const char* partsSnapshotFromPerformance()
{
    uiInit();
    parts::List& l = L();
    if (l.count >= parts::kMax)
    {
        return "The Parts list is full";
    }
    const int at = l.count == 0 ? 0 : (s_ui.sel < l.count ? s_ui.sel + 1 : l.count);
    const bool loop = parts::songLoops(l);
    memmove(&l.part[at + 1], &l.part[at], size_t(l.count - at) * sizeof(parts::Part));
    memmove(l.start[at + 1], l.start[at], size_t(l.count - at) * parts::kTracks);
    l.count++;
    parts::Part& p = l.part[at];
    parts::partInit(p, mtProject.values.actualPattern);
    memset(l.start[at], 0, parts::kTracks);
    for (int t = 0; t < parts::kTracks; t++)
    {
        const bool muted = t < kSampleTracks ? mtProject.values.trackMute[t] > 0 : mtProject.values.midiTrackMute[t - kSampleTracks] > 0;
        const uint8_t pat = mtProject.perfTracksPatterns[t];
        // a track with nothing in its pattern (the pattern's bitmask: notes, FX, MIDI steps) is Off, not pattern 1
        const bool used = pat > 0 && (t < kSampleTracks ? (mtProject.values.allPatternsBitmask[pat - 1] >> t)
                                                        : (mtProject.values.allPatternsBitmaskMidi[pat - 1] >> (t - kSampleTracks))) & 1;
        p.src[t] = muted || !used ? 0 : pat;
    }
    setSongLoop(loop);
    select(at);
    s_ui.table.flash = int16_t(at);
    s_ui.flashMs = now();
    parts::changed();
    static char line[24];   // short: the title bar clock starts at x 300
    snprintf(line, sizeof(line), "Saved as Part %d", at + 1);
    return line;
}

uint32_t partsSnapshotMs()
{
    return s_ui.flashMs;
}

#ifdef UI_SHOT_HOST
// the host simulator reads what the view shows in text (ui_shot "partsui"): the title bar text and the cursor track
const char* partsUiTitle()
{
    return s_ui.title;
}
int partsUiCursor()
{
    return s_ui.cursor;
}
#endif

#endif
