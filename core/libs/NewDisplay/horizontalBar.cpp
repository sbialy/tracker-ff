#include "FT812.h"

#include "display.h"
#include "displayControls.h"
#include "horizontalBarControl.h"

cHorizontalBar::cHorizontalBar(strControlProperties* properties)
  : cDisplayControl(controlIdHorizontalBar, 1)
{
    colorsCount = 4;
    colors = display.getTheme().popupHorizontalBarColors;

    if (properties == nullptr)
    {
        posX = 0;
        posY = 0;
        text = nullptr;
        value = 0;
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

    setStyle(properties->style);
}

cHorizontalBar::~cHorizontalBar()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cHorizontalBar::setStyle(uint32_t style)
{
    this->style = style;

    textStyle = (style & controlStyleCenterX ? OPT_CENTERX : 0) |
                (style & controlStyleCenterY ? OPT_CENTERY : 0) |
                (style & controlStyleRightX ? OPT_RIGHTX : 0);

    textFont = FONT_INDEX_FROM_STYLE;
    textFont = (textFont >= 0) ? textFont : 0;
    textFont = fonts[textFont].handle;
}

void cHorizontalBar::setText(const char* text)
{
    this->text = text;
}

void cHorizontalBar::setValue(int value)
{
    this->value = value;
}

void cHorizontalBar::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cHorizontalBar::setData(void* /*data*/)
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cHorizontalBar::update()
{
    uint16_t frameWidth = width;
    uint16_t frameX = posX; //(width > 800/8) ? posX+5 : posX + width/2;

    uint16_t frameHeight = height;
    uint16_t frameY = posY;

    uint16_t barWidth = (5 * width) / 7;
    uint16_t barX = posX + (width / 7);

    uint16_t barHeight = 2;
    uint16_t barY = posY + ((3 * height) / 5);

    uint16_t textX = posX + (width / 2);
    uint16_t textY = posY + ((2 * height) / 6);

    if ((style & controlStyleBackground))
    {
        API_COLOR(colors[0]);

        API_LINE_WIDTH(16);
        API_BEGIN(RECTS);
        API_VERTEX2F(frameX + 1, frameY + 1);
        API_VERTEX2F(frameX + frameWidth - 1, frameY + frameHeight - 1);
        API_END();

        API_COLOR(colors[1]);

        API_LINE_WIDTH(16);
        API_BEGIN(LINE_STRIP);
        API_VERTEX2F(frameX, frameY);
        API_VERTEX2F(frameX + frameWidth, frameY);
        API_VERTEX2F(frameX + frameWidth, frameY + frameHeight);
        API_VERTEX2F(frameX, frameY + frameHeight);
        API_VERTEX2F(frameX, frameY);
        API_END();
    }

    if ((style & controlStyleValue_0_100))
    {
        API_LINE_WIDTH(16);
        API_BLEND_FUNC(SRC_ALPHA, ZERO);
        API_BEGIN(RECTS);

        if (style & controlStyleNoTransparency)
        {
            API_COLOR(colors[3]);
            API_VERTEX2F(barX, barY);
            API_VERTEX2F(barX + barWidth, barY + barHeight);
        }

        if (value > 0)
        {
            int tempVal = value;
            if (tempVal > 100)
            {
                tempVal = 100;
            }

            uint16_t barFillY = (barWidth * tempVal) / 100;

            API_COLOR(colors[2]);
            API_VERTEX2F(barX, barY);
            API_VERTEX2F(barX + barFillY, barY + barHeight);
        }

        API_END();

        API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);

        if (text != nullptr)
        {
            API_CMD_TEXT(textX, textY, textFont, OPT_CENTER, text);
        }
    }
}
