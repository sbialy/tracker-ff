#include "FT812.h"

#include "batteryControl.h"
#include "display.h"
#include "displayControls.h"

cBattery::cBattery(strControlProperties* properties)
  : cDisplayControl(ControlIdBattery, 1)
{
    colorsCount = 3;
    colors = display.getTheme().batteryIconColors;

    if (properties == nullptr)
    {
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

cBattery::~cBattery()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cBattery::setStyle(uint32_t style)
{
    this->style = style;

    textStyle = (style & controlStyleCenterX ? OPT_CENTERX : 0) |
                (style & controlStyleCenterY ? OPT_CENTERY : 0) |
                (style & controlStyleRightX ? OPT_RIGHTX : 0);

    int8_t textFont = FONT_INDEX_FROM_STYLE;
    textFont = (textFont >= 0) ? textFont : 0;
    font = &fonts[textFont];
}

void cBattery::setText(const char* text)
{
    this->text = text;
}

void cBattery::setValue(int value)
{
    this->value = value;
}

void cBattery::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cBattery::setData(void* /*data*/)
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cBattery::update()
{
    int16_t imgX = posX;
    int16_t imgY = posY;

    int16_t text_y = posY + 10;

    if (style & controlStyleCenterX)
    {
    }

    if (text != nullptr)
    {
        API_COLOR(colors[0]);
        API_CMD_TEXT(posX, text_y, font->handle, textStyle, text);
    }

    if (style & controlStyleShowBitmap)
    {
        int bitmapId = style & controlStyleTransformImg ? displayBatteryCh : displayBattery;

        API_SAVE_CONTEXT();

        // level bar
        if (value >= 0)
        {
            int16_t barX = posX + 1;
            int16_t barY =
              posY - (style & controlStyleCenterY ? (bitmaps[displayBattery].height / 2) : 0);

            API_COLOR(value >= 20 ? colors[1] : colors[2]);

            API_BEGIN(RECTS);
            API_VERTEX2F(barX, barY + 1);
            API_VERTEX2F(barX + (((bitmaps[displayBattery].width - 4) * value) / 100),
                         barY + (bitmaps[displayBattery].height - 2));
            API_END();
        }

        if (style & controlStyleCenterY)
        {
            imgY = posY - (bitmaps[bitmapId].height / 2);
        }

        API_COLOR(colors[0]);
        API_CMD_SETBITMAP(bitmaps[bitmapId].address,
                          bitmaps[bitmapId].format,
                          bitmaps[bitmapId].width,
                          bitmaps[bitmapId].height);
        API_BEGIN(BITMAPS);
        API_VERTEX2F(imgX, imgY);
        API_END();

        if (value < 0)
        {
            API_COLOR(colors[2]);
            API_BEGIN(LINES);
            API_VERTEX2F(imgX, imgY + 3 + bitmaps[bitmapId].height);
            API_VERTEX2F(imgX + bitmaps[bitmapId].width, imgY - 3);
            API_END();
        }

        API_RESTORE_CONTEXT();
    }
}
