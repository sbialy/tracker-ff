#ifndef LIBS_NEWDISPLAY_PARTSTABLE_H_
#define LIBS_NEWDISPLAY_PARTSTABLE_H_

// The Parts table of the Song screen's Parts view (core/source/parts.h, design build/parts-ui/DESIGN.md section 2.2): Parts as rows
// (16 on screen), the 16 tracks as columns (8 sample | 8 MIDI), a Bars column and the song map in the right column.
//
// Display list budget: the whole frame is one 8 KB display list (display.cpp step3 appends every control into it), the rest of the
// Song screen takes about 600 words of the 2,048. The table draws its glyphs as single VERTEX2II words (no CMD_NUMBER expansion)
// and counts what it emits: past kWordBudget the numbers of repeat cells are left out first, then the numbers of changed cells.
#include "displayControls.h"

// plain data (no initialisers): it lives in SDRAM, which a static constructor must not touch
struct parts_table_data_t
{
    const uint8_t* rec;   // the Part records (parts::Part), first byte src[0]
    uint8_t stride;             // record size
    const uint8_t* start;       // the start step of each track per Part, 16 bytes a Part (parts::List::start), 0 = step 1
    uint16_t count;             // Parts in the list
    uint16_t top;               // the first row on screen
    uint16_t sel;               // the selected Part
    uint8_t selRows;            // rows of the selected block (Copy range)
    uint8_t cursor;             // Build: the cursor track (0..15)
    uint8_t field;              // Build: 0 the cursor cell, 1 the Length field, 2 the Repeats field has the cursor
    uint8_t build;              // 1: Build (Rec on), red cell cursor, header half
    uint16_t held;              // Build: track buttons held right now (bit per track)
    int16_t playing;           // the Part that plays, -1 none
    int16_t queued;            // the queued Part, -1 none
    uint8_t loop;               // the playing Part loops
    uint8_t pos;                // step of the playing pass
    uint8_t len;               // steps of the playing pass
    int16_t flash;             // a Part just saved by the Performance snapshot (green tint), -1 none
    const uint8_t* patLen;      // known length (steps) per pattern number, 0 = not known (16 assumed)
    float tempo;                // for the song time
    uint8_t songLoop;           // Loop Song is on (Play Song goes back to Part 1 after the last Part)
    uint16_t words;             // display list words of the last draw (out, the budget test reads it)
    uint16_t hidden;            // bit t = track t is the hidden follower of a linked stereo pair: no column, the tracks after it move left
    uint8_t dropped;            // 1: repeat cell numbers left out, 2: all cell numbers left out (out)
};

class cPartsTable : public cDisplayControl
{
  public:
    cPartsTable(strControlProperties* properties = nullptr);
    virtual ~cPartsTable() {}

    virtual void update();
    virtual void update2();
    virtual void update3();
    virtual void update4();

    virtual void setStyle(uint32_t style) { this->style = style; }
    virtual void setText(const char*) {}
    virtual void setValue(int value) { this->value = value; }
    virtual void setColors(const uint32_t* colors) { this->colors = colors; }
    virtual void setData(void* data) { d = (parts_table_data_t*)data; }

    static constexpr int kRows = 16;
    // the whole table, all four stages: the frame holds 2,043 words, the rest of the Parts screen takes about 610 (Build labels
    // with patterns) and an info popup about 180 more
    static constexpr int kWordBudget = 1300;
    static constexpr int kMarksWords = 40;   // stage 4 at most

  private:
    parts_table_data_t* d = nullptr;
    uint16_t words = 0;
    int lastCell = -1;
    int lastHandle = -1;

    const uint8_t* src(int part) const { return d->rec + part * d->stride; }
    const uint8_t* start(int part) const { return d->start + part * 16; }
    int rows() const;
    int stepsOf(int part) const;
    void cells();
    void numbers();
    void marks();
    void songMap();   // steps the Part plays (length or the Auto length, times the passes)
    void w(uint32_t word);
    void glyphs(int x, int y, int font, const char* s, int align);   // align 0 left, 1 centre, 2 right; y is the top
    int textWidth(int font, const char* s) const;
};

#endif
