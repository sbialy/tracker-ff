#ifdef FEATURE_PARTS
// The Parts table (partsTableControl.h). Every word of the display list is written here and counted (w()), so the budget holds on the
// chip as on the host model: glyphs are VERTEX2II words (x < 512) or CELL + VERTEX2F, no coprocessor text.
#include "FT812.h"
#include "display.h"
#include "displayControls.h"
#include "partsTableControl.h"

#include <stdio.h>
#include <string.h>

namespace
{
// layout (800 x 480, content y 29-423)
constexpr int kCellX = 44, kCellPitch = 28, kCellW = 26, kHalfGap = 8;
constexpr int kRowY = 54, kRowPitch = 22, kRowH = 20;
constexpr int kHeadY = 34;
constexpr int kLenRight = 594, kRepRight = 668;   // right edges of the Length and Repeats columns
constexpr int kFieldW[2] = {58, 36};                   // widths of the cursor boxes of the Length and Repeats fields ("10 bars", "x64")
constexpr int kMapX0 = 712, kMapX1 = 788, kMapY0 = 40, kMapY1 = 368;
constexpr int kMaxBands = 96;

// colours (theme values of DESIGN.md 2.2)
constexpr uint32_t kLight = 0x909090, kDark = 0x323131, kOutline = 0x575757, kText = 0x777777, kWhite = 0xFFFFFF, kBlack = 0x000000;
constexpr uint32_t kRed = 0xCD1818, kGreen = 0x30CB3F, kOrange = 0xD67022, kBg = 0x0A0A0A, kFlash = 0x173F1C, kBandA = 0x4A4A4A,
                   kBandB = 0x2C2C2C;
constexpr int kFontCell = 4, kFontText = 2;   // Regular 12, Regular 14

// Part record fields after src[16] (parts_format.h)
constexpr int kOffLength = 16, kOffRepeats = 17;

inline int cellX(int t)
{
    return kCellX + t * kCellPitch + (t >= 8 ? kHalfGap : 0);
}
// a track's column: the hidden followers of linked stereo pairs (d->hidden) take no column, the tracks after them move left
inline int colOfT(uint16_t hidden, int t)
{
    return t - __builtin_popcount(hidden & ((1u << t) - 1u));
}
inline int rowY(int r)
{
    return kRowY + r * kRowPitch;
}

char* num(char* b, int v)
{
    char t[6];
    int n = 0;
    if (v <= 0) t[n++] = '0';
    while (v > 0 && n < 5)
    {
        t[n++] = char('0' + v % 10);
        v /= 10;
    }
    for (int i = 0; i < n; i++) b[i] = t[n - 1 - i];
    b[n] = 0;
    return b;
}

// a cell's text: the pattern, and with a start step ":<step>" (1 based: the start 16 reads 3:17)
char* cellText(char* b, int v, int start)
{
    num(b, v);
    if (start > 0)
    {
        char* e = b + strlen(b);
        *e++ = ':';
        num(e, start + 1);
    }
    return b;
}

}   // namespace

cPartsTable::cPartsTable(strControlProperties* properties)
  : cDisplayControl(controlIdPartsTable, 4)
{
    autoRefresh = false;
    colors = nullptr;
    colorsCount = 0;
    if (properties == nullptr)
    {
        posX = posY = 0;
        width = height = 0;
        style = 0;
        value = 0;
        text = nullptr;
        return;
    }
    posX = properties->x;
    posY = properties->y;
    width = properties->w;
    height = properties->h;
    style = properties->style;
    value = properties->value;
    text = properties->text;
}

void cPartsTable::w(uint32_t word)
{
    display.getDisplayDriver()->WriteData32(word);
    display.getDisplayDriver()->IncCMDOffset(4);
    words++;
}

int cPartsTable::rows() const
{
    if (d == nullptr || d->count <= d->top) return 0;
    const int n = d->count - d->top;
    return n < kRows ? n : kRows;
}

int cPartsTable::stepsOf(int part) const
{
    const uint8_t* s = src(part);
    int len = s[kOffLength];
    if (len == 0)
    {   // Auto: the length of the first source pattern (parts.cpp assembleStep), 16 when it is not known yet
        len = 16;
        for (int t = 0; t < 16; t++)
            if (s[t])
            {
                if (d->patLen && d->patLen[s[t]]) len = d->patLen[s[t]];
                const int st = start(part)[t];
                if (st > 0 && st < len) len -= st;   // the window of the first source, from its start step on
                break;
            }
    }
    return len * s[kOffRepeats];
}

int cPartsTable::textWidth(int font, const char* s) const
{
    int x = 0;
    for (; *s; s++) x += fonts[font].data[(uint8_t)*s & 127];
    return x;
}

// inside BEGIN(BITMAPS)
void cPartsTable::glyphs(int x, int y, int font, const char* s, int align)
{
    if (align == 1) x -= textWidth(font, s) / 2;
    else if (align == 2) x -= textWidth(font, s);
    const int h = fonts[font].handle;
    for (; *s; s++)
    {
        const uint8_t c = (uint8_t)*s & 127;
        if (x >= 0 && y >= 0 && x < 512 && y < 512)
        {   // taken as changing the handle and cell state too (String2Bitmaps does the same)
            w(VERTEX2II(x, y, h, c));
            lastCell = lastHandle = -1;
        }
        else if (x >= 0 && y >= 0)
        {
            if (lastHandle != h) { w(BITMAP_HANDLE(h)); lastHandle = h; }
            if (lastCell != c) { w(CELL(c)); lastCell = c; }
            w(VERTEX2F(x, y));
        }
        x += fonts[font].data[c];
    }
}

// stage 2: the cells, stage 3: their numbers (within the budget), stage 4: selection and playing marks
void cPartsTable::update2()
{
    if (d != nullptr) cells();
}

void cPartsTable::update3()
{
    if (d != nullptr) numbers();
}

void cPartsTable::update4()
{
    if (d == nullptr) return;
    marks();
    d->words = words;
}

// stage 1: background, header, Part numbers, Bars column and the song map
void cPartsTable::update()
{
    words = 0;
    lastCell = lastHandle = -1;
    if (d == nullptr) return;
    w(COLOR_RGB(0x0A, 0x0A, 0x0A));
    w(LINE_WIDTH(16));
    w(BEGIN(RECTS));
    w(VERTEX2F(posX, posY));
    w(VERTEX2F(posX + width, posY + height));
    // Build: the 8 tracks the F-row stands for are a tinted column band (header to the last row), held tracks get a white box
    if (d->build)
    {
        const int cc0 = colOfT(d->hidden, d->cursor & 15), h0 = cc0 < 8 ? 0 : 8, nr = rows(), lastCol = 15 - __builtin_popcount(d->hidden);
        const int h7 = h0 + 7 < lastCol ? h0 + 7 : lastCol;
        w(COLOR_RGB(0xCD, 0x18, 0x18));   // a red rule under the title bar: this is Build
        w(VERTEX2F(posX, posY));
        w(VERTEX2F(posX + width, posY + 3));
        w(COLOR_RGB(0x2A, 0x14, 0x14));
        w(VERTEX2II(cellX(h0) - 2, kHeadY - 3, 0, 0));
        w(VERTEX2II(cellX(h7) + kCellW + 2, nr ? rowY(nr - 1) + kRowH + 2 : kHeadY + 17, 0, 0));
        w(COLOR_RGB(0x77, 0x77, 0x77));
        w(VERTEX2II(cellX(h0), kHeadY + 15, 0, 0));
        w(VERTEX2II(cellX(h7) + kCellW, kHeadY + 16, 0, 0));
        if (d->held)
        {
            w(COLOR_RGB(0xFF, 0xFF, 0xFF));
            for (int t = 0; t < 16; t++)
                if ((d->held & (1u << t)) && !(d->hidden & (1u << t)))
                {
                    w(VERTEX2II(cellX(colOfT(d->hidden, t)), kHeadY - 2, 0, 0));
                    w(VERTEX2II(cellX(colOfT(d->hidden, t)) + kCellW, kHeadY + 13, 0, 0));
                }
        }
    }
    // snapshot tint
    if (d->flash >= d->top && d->flash < d->top + rows())
    {
        w(COLOR_RGB(0x17, 0x3F, 0x1C));
        const int y = rowY(d->flash - d->top) - 1;
        w(VERTEX2F(2, y));
        w(VERTEX2F(kRepRight + 4, y + kRowH + 2));
    }
    w(END());

    char b[12];
    w(BEGIN(BITMAPS));
    w(COLOR_RGB(0x57, 0x57, 0x57));
    glyphs(6, kHeadY, kFontCell, "Part", 0);
    glyphs(kLenRight, kHeadY, kFontCell, "Length", 2);
    glyphs(kRepRight, kHeadY, kFontCell, "Repeats", 2);
    for (int t = 0; t < 16; t++)
    {
        if (d->hidden & (1u << t)) continue;
        const int col = colOfT(d->hidden, t);
        const bool held = d->build && (d->held & (1u << t));
        const bool half = d->build && ((col < 8) == (colOfT(d->hidden, d->cursor & 15) < 8));
        const uint32_t c = held ? kBlack : half ? kWhite : 0x575757;
        if (t == 0 || held || half || (col == 8))
            w((4UL << 24) | c);
        if (d->hidden & (2u << t))
            snprintf(b, sizeof(b), "%d+%d", t + 1, t + 2);   // a linked stereo pair: the follower has no column
        else
            num(b, t + 1);
        glyphs(cellX(col) + kCellW / 2, kHeadY, kFontCell, b, 1);
        if (held) w((4UL << 24) | 0x575757);
    }
    const int n = rows();
    for (int r = 0; r < n; r++)
    {
        const int p = d->top + r;
        const uint8_t* s = src(p);
        const int y = rowY(r) + 1;
        w((4UL << 24) | (p == d->sel ? kWhite : kText));
        glyphs(36, y, kFontText, num(b, p + 1), 2);
        const int len = s[kOffLength], rep = s[kOffRepeats];
        // Length: Auto, whole bars (16 steps) or steps for a length that is not a bar. Repeats: x1 to x64 or End (the row is not played)
        if (rep != 0)
        {
            if (len == 0) strcpy(b, "Auto");
            else if (len % 16 == 0) snprintf(b, sizeof(b), len == 16 ? "1 bar" : "%d bars", len / 16);
            else if (len > 16) snprintf(b, sizeof(b), "%db+%d", len / 16, len % 16);
            else snprintf(b, sizeof(b), "%d st", len);
            glyphs(kLenRight, y, kFontText, b, 2);
            snprintf(b, sizeof(b), "x%d", rep);
        }
        else
            strcpy(b, "End");
        glyphs(kRepRight, y, kFontText, b, 2);
    }
    if (d->build)
    {   // what the F-row stands for, under the tinted half
        w((4UL << 24) | 0xE05050);
        {   // the band is 8 columns of the table: tracks counted without the hidden followers
            const int lastCol = 15 - __builtin_popcount(d->hidden), h0 = colOfT(d->hidden, d->cursor & 15) < 8 ? 0 : 8;
            const int cnt = lastCol - h0 + 1 < 8 ? lastCol - h0 + 1 : 8;
            int first = 0, last = 0, k = 0;
            for (int t = 0; t < 16; t++)
                if (!(d->hidden & (1u << t)))
                {
                    if (k == h0) first = t;
                    if (k == h0 + cnt - 1) last = t;
                    k++;
                }
            char t2[28];
            snprintf(t2, sizeof(t2), "F1-F%d = tracks %d-%d", cnt, first + 1, last + 1);
            glyphs(cellX(h0), rowY(kRows) + 2, kFontCell, t2, 0);
        }
        const int r = d->sel - d->top, c = d->cursor & 15;
        if (!d->field && r >= 0 && r < n && src(d->sel)[c] && start(d->sel)[c])
        {   // the cursor cell starts late in its source pattern
            char t[28];
            snprintf(t, sizeof(t), "Track %d: from step %d", c + 1, start(d->sel)[c] + 1);
            w((4UL << 24) | kOrange);
            glyphs(300, rowY(kRows) + 2, kFontCell, t, 0);
        }
    }
    if (d->count > kRows)
    {
        snprintf(b, sizeof(b), "%d-%d of %d", d->top + 1, d->top + n, d->count);
        w((4UL << 24) | 0x575757);
        glyphs(kRepRight, rowY(kRows) + 2, kFontCell, b, 2);
    }
    w(END());
    songMap();
}

// cells. Changed (light), repeat (dark), drop (outline), off (dot), End (a rule)
void cPartsTable::cells()
{
    if (d == nullptr) return;
    const int n = rows();
    w(LINE_WIDTH(16));
    for (int pass = 0; pass < 4; pass++)
    {
        static const uint32_t col[4] = { kLight, kDark, kOutline, kBg };
        bool begun = false;
        for (int r = 0; r < n; r++)
        {
            const int p = d->top + r;
            const uint8_t* s = src(p);
            if (s[kOffRepeats] == 0)
            {
                if (pass == 0)
                {
                    if (!begun) { w(BEGIN(RECTS)); w((4UL << 24) | col[0]); begun = true; }
                    w(VERTEX2II(cellX(0), rowY(r) + kRowH / 2 - 1, 0, 0));
                    w(VERTEX2II(cellX(15 - __builtin_popcount(d->hidden)) + kCellW, rowY(r) + kRowH / 2 + 1, 0, 0));
                }
                continue;
            }
            const uint8_t* a = p > 0 ? src(p - 1) : nullptr;
            const bool aboveEnd = a && a[kOffRepeats] == 0;
            for (int t = 0; t < 16; t++)
            {
                if (d->hidden & (1u << t)) continue;
                const int v = s[t], u = (a && !aboveEnd) ? a[t] : 0;
                int kind;   // 0 light, 1 dark, 2 drop, 3 off
                if (v) kind = (a && !aboveEnd && v == u) ? 1 : 0;
                else kind = u ? 2 : 3;
                const bool draw = (pass == 0 && kind == 0) || (pass == 1 && kind == 1) || ((pass == 2 || pass == 3) && kind == 2);
                if (!draw) continue;
                if (!begun) { w(BEGIN(RECTS)); w((4UL << 24) | col[pass]); begun = true; }
                const int x = cellX(colOfT(d->hidden, t)), y = rowY(r), k = pass == 3 ? 1 : 0;
                w(VERTEX2II(x + k, y + k, 0, 0));
                w(VERTEX2II(x + kCellW - k, y + kRowH - k, 0, 0));
            }
        }
        if (begun) w(END());
    }
    // off cells under off cells: a dot
    bool begun = false;
    for (int r = 0; r < n; r++)
    {
        const int p = d->top + r;
        const uint8_t* s = src(p);
        if (s[kOffRepeats] == 0) continue;
        const uint8_t* a = p > 0 ? src(p - 1) : nullptr;
        const bool aboveEnd = a && a[kOffRepeats] == 0;
        for (int t = 0; t < 16; t++)
        {
            if ((d->hidden & (1u << t)) || s[t] || (a && !aboveEnd && a[t])) continue;
            if (!begun) { w(POINT_SIZE(24)); w(BEGIN(FTPOINTS)); w((4UL << 24) | 0x3A3A3A); begun = true; }
            w(VERTEX2II(cellX(colOfT(d->hidden, t)) + kCellW / 2, rowY(r) + kRowH / 2, 0, 0));
        }
    }
    if (begun) w(END());
}

// cell numbers, changed cells first (black), then repeat cells (grey), each while the budget allows
void cPartsTable::numbers()
{
    if (d == nullptr) return;
    const int n = rows();
    d->dropped = 0;
    char b[10];
    // what is left of the budget once the marks (selection, cursor, playing line: kMarksWords at most) are kept aside
    const int budget = kWordBudget - kMarksWords;
    w(BEGIN(BITMAPS));
    for (int pass = 0; pass < 2; pass++)
    {
        w((4UL << 24) | (pass == 0 ? kBlack : kText));
        for (int r = 0; r < n; r++)
        {
            const int p = d->top + r;
            const uint8_t* s = src(p);
            if (s[kOffRepeats] == 0) continue;
            const uint8_t* a = p > 0 ? src(p - 1) : nullptr;
            const bool aboveEnd = a && a[kOffRepeats] == 0;
            int need = 3 * 16;   // the glyphs of a row at most: 16 cells of 3 digits, 4 more for each start step
            for (int t = 0; t < 16; t++)
                if (s[t] && start(p)[t] && !(d->hidden & (1u << t))) need += 4;
            if (words + need > budget)
            {
                d->dropped = uint8_t(pass + 1);
                break;
            }
            for (int t = 0; t < 16; t++)
            {
                const int v = s[t];
                if (!v || (d->hidden & (1u << t))) continue;
                const bool repeat = a && !aboveEnd && a[t] == v;
                if (repeat != (pass == 1)) continue;
                cellText(b, v, start(p)[t]);
                if (start(p)[t] && textWidth(kFontCell, b) > kCellW + 2)
                {   // too wide for the cell: the pattern and a colon, the exact step is in the line under the table (Build)
                    num(b, v);
                    strcat(b, ":");
                }
                glyphs(cellX(colOfT(d->hidden, t)) + kCellW / 2, rowY(r) + 3, kFontCell, b, 1);
            }
        }
        if (d->dropped) break;
    }
    w(END());
}

// selection, cursor, playing marks and the progress line
void cPartsTable::marks()
{
    const int n = rows();
    // selection: Arrange a white frame over the selected rows, Build a red one plus the cursor cell box
    const int r0 = d->sel - d->top;
    if (n > 0 && r0 >= 0 && r0 < n)
    {
        int r1 = r0 + (d->selRows ? d->selRows : 1) - 1;
        if (r1 >= n) r1 = n - 1;
        const int x0 = cellX(0) - 3, x1 = cellX(15 - __builtin_popcount(d->hidden)) + kCellW + 3, y0 = rowY(r0) - 2, y1 = rowY(r1) + kRowH + 1;
        w((4UL << 24) | (d->build ? kRed : kWhite));
        w(LINE_WIDTH(16));
        w(BEGIN(LINE_STRIP));
        w(VERTEX2II(x0, y0, 0, 0));
        w(VERTEX2II(x1, y0, 0, 0));
        w(VERTEX2II(x1, y1, 0, 0));
        w(VERTEX2II(x0, y1, 0, 0));
        w(VERTEX2II(x0, y0, 0, 0));
        w(END());
        if (d->build)
        {   // the cursor box: a track cell, or the Length / Repeats field (a box around its column text, same line). VERTEX2F: x is past 511
            const int cx = d->field == 1 ? kLenRight - kFieldW[0] : d->field == 2 ? kRepRight - kFieldW[1] : cellX(colOfT(d->hidden, d->cursor & 15));
            const int cw = d->field == 1 ? kFieldW[0] + 2 : d->field == 2 ? kFieldW[1] + 2 : kCellW + 1;
            w(LINE_WIDTH(24));
            w(BEGIN(LINE_STRIP));
            w(VERTEX2F(cx - 1, y0 + 1));
            w(VERTEX2F(cx + cw, y0 + 1));
            w(VERTEX2F(cx + cw, y1 - 1));
            w(VERTEX2F(cx - 1, y1 - 1));
            w(VERTEX2F(cx - 1, y0 + 1));
            w(END());
        }
    }
    // playing (green dot) and queued (orange dot) Part marks, progress line on the playing row
    w(POINT_SIZE(48));
    w(BEGIN(FTPOINTS));
    if (d->playing >= d->top && d->playing < d->top + n)
    {
        w((4UL << 24) | kGreen);
        w(VERTEX2II(6, rowY(d->playing - d->top) + kRowH / 2, 0, 0));
    }
    if (d->queued >= d->top && d->queued < d->top + n)
    {
        w((4UL << 24) | kOrange);
        w(VERTEX2II(6, rowY(d->queued - d->top) + kRowH / 2, 0, 0));
    }
    w(END());
    if (d->playing >= d->top && d->playing < d->top + n && d->len)
    {
        const int y = rowY(d->playing - d->top) + (d->pos * kRowH) / d->len;
        w((4UL << 24) | kGreen);
        w(LINE_WIDTH(24));
        w(BEGIN(LINES));
        w(VERTEX2II(cellX(0), y, 0, 0));
        w(VERTEX2II(cellX(15 - __builtin_popcount(d->hidden)) + kCellW, y, 0, 0));
        w(END());
    }

}

// the song map: every Part of the song a band, height by duration (steps x passes)
void cPartsTable::songMap()
{
    const int n = rows();
    uint32_t total = 0;
    int songN = 0;
    bool hasEnd = false;
    for (int p = 0; p < d->count; p++)
    {
        const uint8_t* s = src(p);
        if (s[kOffRepeats] == 0) { hasEnd = true; break; }
        total += uint32_t(stepsOf(p));
        songN++;
    }
    char b[24];
    if (songN > 0 && total > 0)
    {
        const int H = kMapY1 - kMapY0;
        const int bandsWanted = songN < kMaxBands ? songN : kMaxBands;
        (void)bandsWanted;
        w(LINE_WIDTH(16));
        w(BEGIN(RECTS));
        uint32_t cum = 0;
        int bandStart = kMapY0, band = 0, playY0 = -1, playY1 = -1, queY0 = -1, queY1 = -1, selY = -1, visY0 = -1, visY1 = -1;
        for (int p = 0; p < songN; p++)
        {
            const int y0 = kMapY0 + int(cum * uint32_t(H) / total);
            cum += uint32_t(stepsOf(p));
            const int y1 = kMapY0 + int(cum * uint32_t(H) / total);
            if (p == d->playing) { playY0 = y0; playY1 = y1; }
            if (p == d->queued) { queY0 = y0; queY1 = y1; }
            if (p == d->sel) selY = (y0 + y1) / 2;
            if (p == d->top) visY0 = y0;
            if (p == d->top + n - 1) visY1 = y1;
            // a band ends here when it is 2 px or more, or at the last Part (small Parts merge into one band)
            if (y1 - bandStart >= 2 || p == songN - 1)
            {
                if (band < kMaxBands || p == songN - 1)
                {
                    w((4UL << 24) | ((band & 1) ? kBandB : kBandA));
                    w(VERTEX2F(kMapX0, bandStart));
                    w(VERTEX2F(kMapX1, y1 - (y1 - bandStart >= 3 ? 1 : 0)));
                    band++;
                }
                bandStart = y1;
            }
        }
        if (playY0 >= 0)
        {
            w((4UL << 24) | 0x1F7A29);
            w(VERTEX2F(kMapX0, playY0));
            w(VERTEX2F(kMapX1, playY1 > playY0 ? playY1 - 1 : playY0 + 1));
        }
        if (selY >= 0)
        {
            w((4UL << 24) | kWhite);
            w(VERTEX2F(kMapX0 - 6, selY - 1));
            w(VERTEX2F(kMapX0 - 2, selY + 1));
        }
        if (visY0 >= 0 && d->count > kRows)
        {   // the rows on screen: a grey bracket right of the map
            w((4UL << 24) | 0x777777);
            w(VERTEX2F(kMapX1 + 3, visY0));
            w(VERTEX2F(kMapX1 + 5, visY1 >= 0 ? visY1 : kMapY1));
        }
        if (hasEnd)
        {
            w((4UL << 24) | kRed);
            w(VERTEX2F(kMapX0, kMapY1));
            w(VERTEX2F(kMapX1, kMapY1 + 2));
        }
        w(END());
        if (queY0 >= 0)
        {
            w((4UL << 24) | kOrange);
            w(LINE_WIDTH(16));
            w(BEGIN(LINE_STRIP));
            w(VERTEX2F(kMapX0, queY0));
            w(VERTEX2F(kMapX1, queY0));
            w(VERTEX2F(kMapX1, queY1));
            w(VERTEX2F(kMapX0, queY1));
            w(VERTEX2F(kMapX0, queY0));
            w(END());
        }
        if (playY0 >= 0 && d->len)
        {
            const int y = playY0 + (playY1 - playY0) * d->pos / d->len;
            w((4UL << 24) | kGreen);
            w(LINE_WIDTH(16));
            w(BEGIN(LINES));
            w(VERTEX2F(kMapX0 - 2, y));
            w(VERTEX2F(kMapX1 + 2, y));
            w(END());
        }
        // below the map: Parts in the song and the song time
        const float stepSec = d->tempo > 1.0f ? 15.0f / d->tempo : 0.125f;
        const int sec = int(float(total) * stepSec + 0.5f);
        w(BEGIN(BITMAPS));
        w((4UL << 24) | 0x777777);
        snprintf(b, sizeof(b), songN == 1 ? "1 Part" : "%d Parts", songN);
        glyphs((kMapX0 + kMapX1) / 2, kMapY1 + 8, kFontCell, b, 1);
        snprintf(b, sizeof(b), "%d:%02d", sec / 60, sec % 60);
        w((4UL << 24) | kWhite);
        glyphs((kMapX0 + kMapX1) / 2, kMapY1 + 26, kFontText, b, 1);
        if (d->songLoop)
        {
            w((4UL << 24) | kOrange);
            glyphs((kMapX0 + kMapX1) / 2, kMapY1 + 41, kFontCell, "Loop Song", 1);
        }
        w(END());
    }
    else if (d->count == 0)
    {
        w(BEGIN(BITMAPS));
        w((4UL << 24) | 0x777777);
        glyphs(cellX(0), rowY(1), kFontText, "No Parts yet", 0);
        glyphs(cellX(0), rowY(2) + 4, kFontCell, "Rec, then Insert: Part 1 from the pattern on screen", 0);
        w(END());
    }
}

#endif
