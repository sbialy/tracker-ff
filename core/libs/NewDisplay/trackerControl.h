
#ifndef _TACKER_CONTROL_H_
#define _TACKER_CONTROL_H_

#include "displayControls.h"

struct strTrackerPattern
{
    static constexpr uint8_t kTrackCount = 16;

    struct strTracks
    {
        struct strRow
        {
            char note[5];
            char instr[4]; // 3 znak >0 jesli midi
            char fx[2][5];
            int8_t vol;     // volume byte of the step, -1 = none (marks, V column)
            int8_t mm;      // micro-move byte of the step, -1 = none
            uint8_t hidden; // 1 = the FX bank that is not on screen holds an FX in this step
        } row[15];

    } track[kTrackCount];

    //-------------------------------
    // aktualna pozycja kursora :
    int8_t actualTrack; // <- ramka bedzie widziana zaleznie od firstVisibleTrack (0-7)
    int16_t actualStep; // <- aktualnie zaznaczony/edytowany step (0-xxx)
    int16_t middleVisibleStep; // od tej wartosci zalezy zawsze jakie stepy sa na ekranie (-xxx-xxx)

    int16_t patternLength;        // dlugosc patternu (1-xxx)
    int8_t firstVisibleTrack;     // pierwszy track od lewej widziany na wyswitlaczu (0-4)
                                  // - steruje przesunieciem na boki
    uint8_t stepDevider;          // podzialka

    bool showPlayhead;
    int16_t playheadPosition; // linia odtwarzania - wartosc stepa (0-xxx)
    int8_t playheadStartTrack;
    int8_t playheadEndTrack;

    int8_t selectState;   // 1 = kursor ; 2 = kursor + zaznaczenie
    int8_t selectedParam; // wybrany do modyfikacji parametr (0-4)
    uint8_t selectColor;  // kolor zaznaczenia: 0 - red, 1 - green, 2 - white

    // sekcja zaznaczenia
    int8_t selectStartTrack; // start zaznaczenia
    int16_t selectStartStep; // <= kolejnosc obojetna
    int8_t selectEndTrack;   // koniec zaznaczenia
    int16_t selectEndStep;

    uint8_t selectColumn; // jezli 1 to cala kolumna
                          // startStep zawsze 0
                          // endStep pobrac z patternu
    // koniec sekcji zaznaczenia

    uint8_t popupMode; // 1 = obcina fill; 2 waski step popup; 4 szeroki step popup

    // the 4 track view with the V and m columns (Config > Pattern V/m columns): 3 tracks of 6 columns, set by the pattern editor
    bool vmColumns;
    // the 4 FX bank 3/4 is on screen: the FX columns are tinted and the hidden mark points at slots 1/2
    bool fxBank34;

    uint8_t inactive[kTrackCount];
    // bit t set = track t is not drawn (the follower of a linked stereo pair): the columns reflow, no gap is left
    uint16_t hidden;
    // the real track shown in screen column col, counted from firstVisibleTrack
    int colTrack(int col) const
    {
        int t = firstVisibleTrack;
        for (;; t++)
        {
            while (t < kTrackCount && ((hidden >> t) & 1))
                t++;
            if (col-- <= 0 || t >= kTrackCount - 1)
                return t < kTrackCount ? t : kTrackCount - 1;
        }
    }
    // the screen column of a real track (a hidden track counts as the column of the next visible one)
    int colOf(int track) const
    {
        int c = 0;
        for (int t = firstVisibleTrack; t < track; t++)
            c += !((hidden >> t) & 1);
        return track < firstVisibleTrack ? track - firstVisibleTrack : c;
    }
    // per-track pattern length (polymeter): bit r set = row r of the track is past the track's own length, drawn dimmed
    uint16_t beyondLength[kTrackCount];
    bool isBeyondLength(int t, int r) const { return (beyondLength[t] >> r) & 1; }

    bool showTrackNames;
    const char* tracksNames[kTrackCount];

    void init()
    {
        for (uint8_t t = 0; t < kTrackCount; t++)
        {
            for (uint8_t r = 0; r < 15; r++)
            {
                for (int i = 0; i < 5; i++)
                {
                    track[t].row[r].note[i] = 0;
                    track[t].row[r].fx[0][i] = 0;
                    track[t].row[r].fx[1][i] = 0;
                }
                for (int i = 0; i < 4; i++)
                {
                    track[t].row[r].instr[i] = 0;
                }
                track[t].row[r].vol = -1;
                track[t].row[r].mm = -1;
                track[t].row[r].hidden = 0;
            }
            inactive[t] = 0;
            hidden = 0;
            beyondLength[t] = 0;
            tracksNames[t] = nullptr;
        }
        actualTrack = 0;
        actualStep = 0;
        middleVisibleStep = 0;
        patternLength = 1;
        firstVisibleTrack = 0;
        stepDevider = 4;
        showPlayhead = true;
        playheadPosition = 0;
        playheadStartTrack = 0;
        playheadEndTrack = 0;
        selectState = 0;
        selectedParam = 0;
        selectColor = 0;
        selectStartTrack = 0;
        selectStartStep = 0;
        selectEndTrack = 0;
        selectEndStep = 0;
        selectColumn = 0;
        popupMode = 0;
        vmColumns = false;
        fxBank34 = false;
        showTrackNames = false;
    }
};

// ilosc znakow parametru
const uint8_t param_length[4] = { 3, 2, 4, 4 };

//--------------------------------------------------------------------
class cTracker : public cDisplayControl
{
  public:
    cTracker(strControlProperties* properties);
    virtual ~cTracker();

    virtual void update();
    virtual void update2();
    virtual void update3();
    virtual void update4();
    virtual void update5();

    virtual void setStyle(uint32_t style);
    virtual void setText(const char* text);
    virtual void setValue(int value);
    virtual void setColors(const uint32_t* colors);
    // virtual void setDefaultColors(const uint32_t colors[]);
    virtual void setData(void* data);

  private:
    strTrackerPattern* tracks;

    int16_t select1_x, select2_x, select1_y, select2_y;
    uint8_t selectActive = 0;

    void calculateSelection();
    uint8_t whichParamFromTwoSelected();

    void backgroundDivider(uint16_t* rowNumbers);
    void lines();
    void playHead();
    void selection();
    void drawRowNumbers(uint16_t* rowNumbers, bool showTrackEndLines);
    void tracksNumbers();

    void notes();
    void instruments();
    void fxes1();
    void fxes2();
    void marks();      // the V / m / hidden FX marks of the cells (4 track layout)
    void vmColumns();  // the V and m columns (3 track layout)

    uint8_t displayMode;
    bool vm;           // the 3 track layout with the V and m columns is on screen
    uint16_t tracksWidth;
    uint8_t columnsCount;
    uint8_t rowCount;
    uint8_t paramCount;

    uint8_t rightOffset;
    uint8_t leftOffset;

    void String2Bitmaps(int16_t x,
                        int16_t y,
                        const strFont* font,
                        char* string,
                        int8_t length,
                        int16_t w_limit = 800);

    int16_t lastFontHandle;

    uint32_t actualColor;
    uint32_t lastFontColor;
    uint32_t dimFx1Color;
    uint32_t dimFx2Color;

    uint8_t stepHight;
};

#endif
