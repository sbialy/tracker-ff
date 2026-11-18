#include "FT812.h"

#include "display.h"
#include "progressCursorControl.h"

cProgressCursor::cProgressCursor(strControlProperties* properties)
  : cDisplayControl(controlIdProgressCursor, 1)
{
    colorsCount = 2;
    colors = display.getTheme().waveformProgressColors;

    if (properties == nullptr)
    {
        posX = 0;
        posY = 0;
        width = 0;
        height = 0;

        this->style = 0;
        return;
    }

    colors = (properties->colors == nullptr) ? colors : properties->colors;

    posX = properties->x;
    posY = properties->y;
    width = properties->w;
    height = properties->h;

    setStyle(properties->style);
}

cProgressCursor::~cProgressCursor()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cProgressCursor::setStyle(uint32_t style)
{
    this->style = style;
}

void cProgressCursor::setText(const char* /*text*/)
{
}

void cProgressCursor::setValue(int value)
{
    this->value = value;
}

void cProgressCursor::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cProgressCursor::setData(void* /*data*/)
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cProgressCursor::update()
{
    if (value > 0 && value < width)
    {
        API_COLOR(colors[1]);
        API_BEGIN(LINES);
        API_VERTEX2F(posX + value, posY);
        API_VERTEX2F(posX + value, posY + height);
        API_END();
    }
}
