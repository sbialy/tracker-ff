#include "FT812.h"

#include "display.h"
#include "displayControls.h"
#include "notePopoutControl.h"

cNotePopout::cNotePopout(strControlProperties* properties)
  : cDisplayControl(controlIdNotePopup, 1)
{
    colorsCount = 3;
    colors = display.getTheme().notePopupColors;

    if (properties == nullptr)
    {
        posX = 0;
        posY = 0;
        text = nullptr;
        value = -1;
        width = 0;
        height = 0;
        style = 0;
        return;
    }

    colors = (properties->colors == nullptr) ? colors : properties->colors;

    posX = properties->x;
    posY = properties->y;

    text = properties->text;
    value = properties->value;

    width = properties->w;
    height = properties->h;

    padNames = (strPadNames*)properties->data;

    setStyle(properties->style);
    fillpadMap();
}

cNotePopout::~cNotePopout()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cNotePopout::setStyle(uint32_t style)
{
    this->style = style;

    textStyle = (style & controlStyleCenterX ? OPT_CENTERX : 0) |
                (style & controlStyleCenterY ? OPT_CENTERY : 0) |
                (style & controlStyleRightX ? OPT_RIGHTX : 0);

    textFont = FONT_INDEX_FROM_STYLE;
    textFont = (textFont >= 0) ? textFont : 0;
    textFont = fonts[textFont].handle;
}

void cNotePopout::setText(const char* text)
{
    this->text = text;
}

void cNotePopout::setValue(int value)
{
    this->value = value;
}

void cNotePopout::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cNotePopout::setData(void* data)
{
    padNames = (strPadNames*)data;
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
extern int padToNoteIdx(int pad);
extern int noteToPadIdx(int noteIdx);
void cNotePopout::update()
{
    API_COLOR(colors[0]);
    API_BEGIN(RECTS);

    //	char localString[10];

    for (uint8_t i = 0; i < PADMAP_SIZE; i++)
    {
        API_VERTEX2F(padMap[i].x, padMap[i].y);
        API_VERTEX2F(padMap[i].x + padMap[i].w, padMap[i].y + padMap[i].h);
    }

    API_END();
    API_COLOR(colors[1]);

    for (uint8_t i = 0; i < PADMAP_SIZE; i++)
    {
        //		for(int j=0;j<padNames->length;j++)
        //		{
        //			localString[j] = padNames->name[i][j];
        //		}

        //		localString[4]=0; // 5 chararacter is max

        API_CMD_TEXT(padMap[i].x + padMap[i].w / 2,
                     padMap[i].y + padMap[i].h / 2,
                     textFont,
                     OPT_CENTER,
                     padNames->name[i]);
    }

    if (value >= 0)
    {
        auto drawRectOnPad = [&](int padIdx)
        {
            API_COLOR(colors[2]);
            API_LINE_WIDTH(kRedFrameWidth);
            API_BEGIN(LINE_STRIP);

            uint16_t localX, localY;
            localX = padMap[padIdx].x - 1;
            localY = padMap[padIdx].y - 1;
            API_VERTEX2F(localX, localY);
            localX = padMap[padIdx].x + padMap[padIdx].w + 1;
            localY = padMap[padIdx].y - 1;
            API_VERTEX2F(localX, localY);
            localX = padMap[padIdx].x + padMap[padIdx].w + 1;
            localY = padMap[padIdx].y + padMap[padIdx].h + 1;
            API_VERTEX2F(localX, localY);
            localX = padMap[padIdx].x - 1;
            localY = padMap[padIdx].y + padMap[padIdx].h + 1;
            API_VERTEX2F(localX, localY);
            localX = padMap[padIdx].x - 1;
            localY = padMap[padIdx].y - 1;
            API_VERTEX2F(localX, localY);
            API_END();
        };

#ifdef TRACKER_MINI
        const int rectangleCount = 8;
        int firstNoteIdx = padToNoteIdx(value);

        for (int idx = 0; idx < rectangleCount; idx++)
        {
            int padIdx = noteToPadIdx(idx + firstNoteIdx);
            drawRectOnPad(padIdx);
        }
#else
        drawRectOnPad(value);
#endif
    }
}

void cNotePopout::fillpadMap()
{
    uint16_t offset_x = width / 12;
    uint16_t offset_y = height / 4;

    for (uint8_t i = 0; i < PADMAP_SIZE; i++)
    {
        if (i <= 11)
        {
            if (i == 0)
                padMap[i].x = posX;
            else
                padMap[i].x = padMap[i - 1].x + offset_x;

            padMap[i].y = posY;
            padMap[i].w = offset_x - offset_x * 0.0667;
            padMap[i].h = offset_y * 0.9330;
        }
        else if (i >= 12 && (i <= 23))
        {
            if (i == 12)
                padMap[i].x = padMap[0].x;
            else
                padMap[i].x = padMap[i - 1].x + offset_x;

            padMap[i].y = posY + offset_y;
            padMap[i].w = offset_x - offset_x * 0.0667;
            padMap[i].h = offset_y * 0.9330;
        }
        else if ((i >= 24) && (i <= 35))
        {
            if (i == 24)
                padMap[i].x = padMap[0].x;
            else
                padMap[i].x = padMap[i - 1].x + offset_x;

            padMap[i].y = posY + 2 * offset_y;
            padMap[i].w = offset_x - offset_x * 0.0667;
            padMap[i].h = offset_y * 0.9330;
        }
        else if ((i >= 36) && (i <= 47))
        {
            if (i == 36)
                padMap[i].x = padMap[0].x;
            else
                padMap[i].x = padMap[i - 1].x + offset_x;

            padMap[i].y = posY + 3 * offset_y;
            padMap[i].w = offset_x - offset_x * 0.0667;
            padMap[i].h = offset_y * 0.9330;
        }
    }
}
