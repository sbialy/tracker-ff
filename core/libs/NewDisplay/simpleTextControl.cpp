#include "simpleTextControl.h"
#include "FT812.h"
#include "display.h"

cSimpleText::cSimpleText(strControlProperties* properties)
  : cDisplayControl(controlIdSimpleText, 1)
{
    colorsCount = 2;
    colors = display.getTheme().defaultTextColors;

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

cSimpleText::~cSimpleText()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cSimpleText::setStyle(uint32_t style)
{
    this->style = style;

    textStyle = (style & controlStyleCenterX ? OPT_CENTERX : 0) |
                (style & controlStyleCenterY ? OPT_CENTERY : 0) |
                (style & controlStyleRightX ? OPT_RIGHTX : 0);

    textFont = FONT_INDEX_FROM_STYLE;
    textFont = (textFont >= 0) ? textFont : 0;
    textFont = fonts[textFont].handle;
}

void cSimpleText::setText(const char* text)
{
    this->text = text;
}

void cSimpleText::setValue(int value)
{
    this->value = value;
}

void cSimpleText::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cSimpleText::setData(void* /*data*/)
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cSimpleText::update()
{
    API_COLOR(colors[1]);

    if (text != nullptr)
    {
        API_CMD_TEXT(posX, posY, 29, textStyle, text);
    }
}
