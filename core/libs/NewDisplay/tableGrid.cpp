#if defined(FEATURE_PLUG_HOST)   // ff_config.h FEATURE_TABLES (Table lite), spelled out: this file includes no FF header first
// The Table page grid (tableGridControl.h). Every word of the display list is written here, glyphs as in partsTable.cpp.
#include "FT812.h"
#include "display.h"
#include "displayControls.h"
#include "tableGridControl.h"

namespace
{
// layout (800 x 480, content y 29-423): the header line, then 16 rows of 22 px
constexpr int kHeadY = 34, kRowY = 76, kRowPitch = 21, kRowH = 19;
constexpr int kNumX = 70;                                    // right edge of the row numbers
constexpr int kColX[4] = { 90, 200, 310, 520 };              // left edges of N, V, FX, value
constexpr int kColW[4] = { 100, 100, 200, 100 };
constexpr int kHeadX[4] = { 90, 250, 410, 570 };
constexpr int kHeadW = 150;
constexpr int kFontText = 2, kFontSmall = 4;   // Regular 14, Regular 12
constexpr uint32_t kWhite = 0xFFFFFF, kBlack = 0x000000, kText = 0xC0C0C0, kDim = 0x575757, kSel = 0x2C2C2C, kRed = 0xCD1818,
                   kGreen = 0x30CB3F;
const char* const kColName[4] = { "N", "V", "FX", "Value" };
}   // namespace

cTableGrid::cTableGrid(strControlProperties* properties)
  : cDisplayControl(controlIdTableGrid, 2)
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

void cTableGrid::w(uint32_t word)
{
    display.getDisplayDriver()->WriteData32(word);
    display.getDisplayDriver()->IncCMDOffset(4);
}

void cTableGrid::rect(int x0, int y0, int x1, int y1)
{
    w(VERTEX2F(x0, y0));
    w(VERTEX2F(x1, y1));
}

int cTableGrid::textWidth(int font, const char* s) const
{
    int x = 0;
    for (; *s; s++) x += fonts[font].data[(uint8_t)*s & 127];
    return x;
}

// inside BEGIN(BITMAPS)
void cTableGrid::glyphs(int x, int y, int font, const char* s, int align)
{
    if (align == 1) x -= textWidth(font, s) / 2;
    const int h = fonts[font].handle;
    for (; *s; s++)
    {
        const uint8_t c = (uint8_t)*s & 127;
        if (x >= 0 && y >= 0 && x < 512 && y < 512)
        {
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

void cTableGrid::update()
{
    lastCell = lastHandle = -1;
    if (d == nullptr) return;
    // background, the selected rows, the playing row, the cursor box
    w(LINE_WIDTH(16));
    w(BEGIN(RECTS));
    w(COLOR_RGB(0x0A, 0x0A, 0x0A));
    rect(posX, posY, posX + width, posY + height);
    const int curRow = d->row & 15, curCol = d->col & 3;
    if (d->bound)
    {
        if (d->place >= 4 && d->selB > d->selA)
        {
            w((4UL << 24) | kSel);
            rect(kColX[0] - 6, kRowY + d->selA * kRowPitch - 1, kColX[3] + kColW[3], kRowY + d->selB * kRowPitch + kRowH);
        }
        if (d->playing >= 0 && d->playing < 16)
        {
            w((4UL << 24) | kGreen);
            const int y = kRowY + d->playing * kRowPitch;
            rect(kNumX + 6, y + 2, kNumX + 12, y + kRowH - 2);
        }
        w((4UL << 24) | kWhite);
        if (d->place < 4)
            rect(kHeadX[d->place] - 6, kHeadY - 3, kHeadX[d->place] + kHeadW - 16, kHeadY + 19);
        else
        {
            const int y = kRowY + curRow * kRowPitch;
            rect(kColX[curCol] - 6, y - 1, kColX[curCol] + kColW[curCol] - 12, y + kRowH);
        }
    }
    else if (d->place < 4)
    {
        w((4UL << 24) | kWhite);
        rect(kHeadX[d->place] - 6, kHeadY - 3, kHeadX[d->place] + kHeadW - 16, kHeadY + 19);
    }
    // the rule under the header
    w((4UL << 24) | kDim);
    rect(posX + 10, kHeadY + 22, posX + width - 10, kHeadY + 23);
    if (d->bound && d->rows < 16)
    {   // the end of the rows played (Len)
        w((4UL << 24) | kRed);
        rect(kNumX - 20, kRowY + d->rows * kRowPitch - 2, kColX[3] + kColW[3], kRowY + d->rows * kRowPitch - 1);
    }
    w(END());

    w(BEGIN(BITMAPS));
    for (int f = 0; f < 4; f++)
    {
        w((4UL << 24) | ((d->place == f) ? kBlack : (d->bound || f == 0 ? kText : kDim)));
        glyphs(kHeadX[f], kHeadY, kFontText, d->head[f], 0);
    }
    if (!d->bound)
    {
        w((4UL << 24) | kDim);
        glyphs(posX + width / 2, kRowY + 7 * kRowPitch, kFontText, "No table on this instrument: F1 and the encoder bind one", 1);
        w(END());
        return;
    }
    char b[4];
    for (int r = 0; r < 16; r++)
    {
        const int y = kRowY + r * kRowPitch + 2;
        const bool on = r < d->rows;
        b[0] = char('0' + (r + 1) / 10);
        b[1] = char('0' + (r + 1) % 10);
        b[2] = 0;
        w((4UL << 24) | (r == curRow && d->place >= 4 ? kWhite : kDim));
        glyphs(kNumX - textWidth(kFontText, b), y, kFontText, b, 0);
        for (int c = 0; c < 4; c++)
        {
            const char* s = d->cell[r][c];
            if (c == 2) s = d->fx[r];
            const bool cur = d->place >= 4 && r == curRow && c == curCol;
            w((4UL << 24) | (cur ? kBlack : (!on ? kDim : (s[0] ? kText : kDim))));
            glyphs(kColX[c], y, kFontText, s[0] ? s : (c == 2 ? "- - -" : "- -"), 0);
        }
    }
    w((4UL << 24) | kDim);
    for (int c = 0; c < 4; c++) glyphs(kColX[c], kRowY - 16, kFontSmall, kColName[c], 0);
    w(END());
}

// Stage 2, the FX picker. Each stage is one coprocessor list, written in one SPI burst into the 4 KB command FIFO (display.cpp step1).
// The grid and the panel in one list came to 1,059 words (4,236 B): over the 4,092 B the ring holds, the list wrapped over its own
// start and the unit never showed the panel (the sim did until eve_model.cpp got the FIFO limit). Apart, about 460 and 600 words
void cTableGrid::update2()
{
    lastCell = lastHandle = -1;
    if (d != nullptr && d->bound && d->pickN) pick();
}

// The FX picker over the grid: a panel of columns filled down, the selected entry as a white bar, its long name and a hint under it
void cTableGrid::pick()
{
    constexpr int kX0 = 40, kX1 = 760, kY0 = 58, kPitchX = 178, kPitchY = 22, kListY = kY0 + 30;
    const int yEnd = kListY + kTablePickRows * kPitchY;
    w(BEGIN(RECTS));
    w((4UL << 24) | kDim);
    rect(kX0 - 2, kY0 - 2, kX1 + 2, yEnd + 56);
    w((4UL << 24) | 0x111111);
    rect(kX0, kY0, kX1, yEnd + 54);
    const int sx = kX0 + 12 + (d->pickSel / kTablePickRows) * kPitchX, sy = kListY + (d->pickSel % kTablePickRows) * kPitchY;
    w((4UL << 24) | kWhite);
    rect(sx - 6, sy - 1, sx + kPitchX - 12, sy + kRowH);
    w(END());
    w(BEGIN(BITMAPS));
    char b[2] = { 0, 0 };
    w((4UL << 24) | kText);
    glyphs(kX0 + 12, kY0 + 5, kFontText, d->pickHint, 0);
    for (int i = 0; i < d->pickN; i++)
    {
        const int x = kX0 + 12 + (i / kTablePickRows) * kPitchX, y = kListY + (i % kTablePickRows) * kPitchY + 2;
        const bool sel = i == d->pickSel;
        w((4UL << 24) | (sel ? kBlack : kDim));
        b[0] = d->pickLetter[i];
        glyphs(x, y, kFontText, b, 0);
        w((4UL << 24) | (sel ? kBlack : kText));
        glyphs(x + 22, y, kFontText, d->pickName[i], 0);
    }
    w((4UL << 24) | kText);
    glyphs(kX0 + 12, yEnd + 8, kFontText, d->pickLong, 0);
    w((4UL << 24) | kDim);
    glyphs(kX0 + 12, yEnd + 32, kFontSmall, "Arrows, encoder or pad to choose. Release the key to set. Delete: none", 0);
    w(END());
}
#endif
