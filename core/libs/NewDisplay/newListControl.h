#ifndef LIBS_NEWDISPLAY_NEWLISTCONTROL_H_
#define LIBS_NEWDISPLAY_NEWLISTCONTROL_H_

#include "displayControls.h"

struct strNewListData
{
    uint16_t rowHeight = 0; // 0 = default = 27
    uint16_t length;
    uint16_t linesCount;
    int16_t start;

    enum class Style
    {
        kNoStyle = 0,
        kHeadline
    };

    bool selectionActive = false;

    char** params = nullptr;
    char** values = nullptr;
    Style* styles = nullptr;
    const int* imageIds = nullptr;
    uint8_t* selectTab = nullptr;

    bool scrollText = false;
    uint16_t lineTextLenMax = 0;
};

class NewList : public cDisplayControl
{
  public:
    NewList(strControlProperties* properties = nullptr);
    virtual ~NewList();

    virtual void update();

    virtual void setStyle(uint32_t style);
    virtual void setText(const char* text);
    virtual void setValue(int value);
    virtual void setColors(const uint32_t* colors);
    virtual void setData(void* data);

  protected:
    // Default member initializers: the constructor returns early without properties and left these as heap garbage. A hidden control that
    // a screenshot refresh (cDisplay::devShotEnd, doScreenShot) updates then wrote through a garbage `data` (ref/sim-parity.md)
    int textStyle = 0;

    strNewListData* data = nullptr;

    int listPosition = 0;
    int barPos = 0;
    int listPos = 0;
    bool disableBar = false;

    uint32_t textScrollTimeStart = 0;
    uint8_t textScrollItem = 0;
    uint8_t textScrollStage = 0;

    void drawBar(int x1, int y1, int x2, int y2);
    void drawScrollBar();
    void drawTextList(int x1, int x2, int y, int rowH, const strFont* font1, const strFont* font2);
    void drawImagesList(int x1, int x2, int y, int rowH);

    bool listHasHeadlineStyle();
    inline void setRowTextColor(uint32_t* lastColor, int row);
    void getRowParamTextStyle(int dataIdx, int* paramX, int* paramY, const strFont* paramFont);
    void getRowParamTextScroll(int textSpace, int* paramX, const strFont* font, const char* text);
};

#endif /* LIBS_NEWDISPLAY_NEWLISTCONTROL_H_ */
