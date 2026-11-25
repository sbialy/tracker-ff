#ifndef LIBS_NEWDISPLAY_TABLEGRID_H_
#define LIBS_NEWDISPLAY_TABLEGRID_H_

// The Table page of the instrument editor (core/interface/instrumentEditor/TablePage.cpp, Table lite core/source/tables.h): a header
// with the four table fields and 16 rows of four cells (N, V, FX, value). The page formats every text, the control only draws: glyphs as
// VERTEX2II / CELL + VERTEX2F words, about 400 display list words with every cell filled.
#include "displayControls.h"

// plain data (no initialisers): it lives in SDRAM, which a static constructor must not touch
struct table_grid_data_t
{
    char head[4][16];       // the four header fields: Table, Tic, Len, Mode
    char cell[16][4][8];    // per row: N, V, (FX: fx below), value ("" = empty, drawn as dots)
    char fx[16][18];        // per row: the FX as its letter and short name ("V Volume"), "" = none
    uint8_t rows;           // rows played (Len): the rows after it are dimmed
    uint8_t place;          // 0..3 a header field has the cursor, 4 the grid
    uint8_t row;            // the cursor row
    uint8_t col;            // the cursor column 0..3
    uint8_t selA, selB;     // the selected rows (Copy and Clear), selA <= selB
    uint8_t bound;          // 0: the instrument has no table (the grid shows a hint)
    int8_t playing;         // the row a track plays now, -1 none
    // the FX picker (held Fx key): every FX a row takes at once, 4 columns of kPickRows, filled down. 0 entries = no picker
    uint8_t pickN;
    uint8_t pickSel;        // the entry the cursor is on
    const char* pickName[40];   // the short names (flash strings)
    char pickLetter[40];    // the pattern editor's letter of each FX
    char pickLong[32];      // the long name of the selected entry, the title line
    char pickHint[16];      // the row the pick is for
};
constexpr int kTablePickRows = 9;

class cTableGrid : public cDisplayControl
{
  public:
    cTableGrid(strControlProperties* properties = nullptr);
    virtual ~cTableGrid() {}

    virtual void update();
    virtual void update2();   // stage 2: the FX picker panel (its own coprocessor list, see tableGrid.cpp)
    virtual void update3() {}
    virtual void update4() {}

    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char*) {}
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { d = (table_grid_data_t*)data; }

  private:
    table_grid_data_t* d = nullptr;
    int lastCell = -1;
    int lastHandle = -1;

    void w(uint32_t word);
    void pick();
    void rect(int x0, int y0, int x1, int y1);
    void glyphs(int x, int y, int font, const char* s, int align);   // align 0 left, 1 centre; y is the top
    int textWidth(int font, const char* s) const;
};

#endif
