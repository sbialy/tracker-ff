
#ifndef _ARRANGEMENT_CONTROL_H_
#define _ARRANGEMENT_CONTROL_H_

#include "displayControls.h"

#include "trackerControl.h"
//{
//	struct strTracks
//	{
//		struct strRow
//		{
//			char note[5] = "";
//			char instr[4] = ""; // 3 znak >0 jesli midi
//			char fx[2][5] = {"",""};
//		} row[15];
//
//	} track[8];
//
//	//-------------------------------
//	// aktualna pozycja kursora :
//	int8_t actualTrack = 0;		// <- ramka bedzie widziana zaleznie od firstVisibleTrack (0-7)
//	int16_t actualStep = 0; 	// <- od tej wartosci zalezy zawsze pozycja w pionie (0-xxx)
//
//	int16_t patternLength = 1;		// dlugosc patternu (1-xxx)
//	int8_t firstVisibleTrack = 0;	// pierwszy track od lewej widziany na wyswitlaczu (0-4)
//									// - steruje przesunieciem na boki
//	int16_t playheadPosition = 0;	// linia odtwarzania - wartosc stepa (0-xxx)
//	uint8_t stepDevider = 4;		// podzialka
//	uint8_t playheadSelectMode = 0;	// linia odtwarzania tylko w zaznaczeniu
//	uint8_t playheadRecMode = 0;	// linia odtwarzania kiedy nagrywanie
//
//	int8_t selectState = 0;			// 1 = kursor ; 2 = kursor + zaznaczenie
//	int8_t selectedParam = 0;		// wybrany do modyfikacji parametr (0-4)
//	uint8_t selectColor = 0;		// kolor zaznaczenia: 0 - red, 1 - green, 2 - white
//
//	int8_t selectStartTrack = 0;	// start zaznaczenia 	//
//	int16_t selectStartStep = 0;							// <= kolejnosc obojetna
//	int8_t selectEndTrack = 0;		// koniec zaznaczenia	//
//	int16_t selectEndStep = 0;
//
//	uint8_t selectColumn = 0;
//
//	uint8_t popupMode = 0; 			// 1 = obcina fill; 2 waski step popup; 4 szeroki step popup
//
//	uint8_t inactive[8] = {0,0,0,0,0,0,0,0};
//
//	char* tracksNames;
//};

//// ilosc znakow parametru
// const uint8_t param_length[4] =
//{
//	3, 2, 4, 4
//};

//--------------------------------------------------------------------
class cArrangement : public cDisplayControl
{
  public:
    cArrangement(strControlProperties* properties);
    virtual ~cArrangement();

    virtual uint8_t update();
    virtual uint8_t memCpy(uint32_t address);

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

    uint16_t ramPartSize[5] = { 0 };
    uint8_t refreshStep;

    void refresh1();
    void refresh2();
    void refresh3();
    void refresh4();
    void refresh5();

    void calculateSelection();
    uint8_t whichParamFromTwoSelected();

    void backgroundDivider();
    void lines();
    void playHead();
    void selection();
    void stepNumbers();
    void tracksNames();

    void notes();
    void instruments();
    void fxes1();
    void fxes2();

    uint8_t displayMode;

    uint8_t tracksSpace;
    uint8_t columnsCount;
    uint8_t rowCount;
    uint8_t rightOffset;
    uint8_t leftOffset;

    uint8_t stepsNumbersHight;
    uint8_t stepsDataStructOffset;

    uint8_t paramCount;

    uint8_t firstVisibleTrack;
    uint8_t visibleTracksCount;

    uint16_t trackHight;
    uint16_t stepWidth;

    uint8_t trackHeadWidth;

    uint8_t visibleStepsCount;

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
};

#endif
