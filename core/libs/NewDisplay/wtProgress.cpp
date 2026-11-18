#include "wtProgress.h"
#include "FT812.h"
#include "display.h"

cWtProgress::cWtProgress(strControlProperties* properties)
  : cDisplayControl(controlIdWtProgress, 1)
{
    colorsCount = 2;
    colors = display.getTheme().waveTablePositionColors;

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

cWtProgress::~cWtProgress()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cWtProgress::setStyle(uint32_t style)
{
    this->style = style;
}

void cWtProgress::setText(const char* /*text*/)
{
}

void cWtProgress::setValue(int value)
{
    this->value = value;
}

void cWtProgress::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cWtProgress::setData(void* data)
{
    wt_data = (wt_position_data_t*)data;
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cWtProgress::update()
{
    if (wt_data != NULL)
    {
        uint32_t bar_heigth = 15;
        uint32_t bar_length;

        API_COLOR(colors[1]);
        API_BEGIN(LINES);
        API_VERTEX2F(posX, posY);
        API_VERTEX2F(posX + width, posY + height);

        float lengthOfWindow =
          wt_data->positionMax == 0 ? 0 : (width / (float)wt_data->positionMax);
        uint32_t bar_start = (wt_data->position * lengthOfWindow + 0.5f); /* 0.5f for rounding*/

        if (lengthOfWindow < 1)
        {
            bar_length = 1;
        }
        else
        {
            bar_length = lengthOfWindow;
        }

        API_BEGIN(RECTS);
        API_VERTEX2F(posX + bar_start, posY - (bar_heigth / 2) + (height / 2));
        API_VERTEX2F(posX + bar_start + bar_length, posY + (bar_heigth) / 2);
        API_END();

        API_END();
    }
}
