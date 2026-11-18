#include "FT812.h"

#include "display.h"
#include "displayControls.h"
#include "multiLabelControl.h"

#include <string.h>

cMultiLabel::cMultiLabel(strControlProperties* properties)
  : cDisplayControl(controlIdMultiLabel, 1)
{
    colorsCount = 3;
    colors = display.getTheme().defaultTextColors;

    if (properties == nullptr)
    {
        posX = 0;
        posY = 0;
        text = nullptr;
        data = nullptr;
        width = 0;
        height = 0;
        style = 0;
        value = 0;
        return;
    }

    colors = (properties->colors == nullptr) ? colors : properties->colors;

    posX = properties->x;
    posY = properties->y;

    text = properties->text;

    value = properties->value;

    data = (strMultiLabelData*)(properties->data);

    width = properties->w;
    height = properties->h;

    setStyle(properties->style);
}

cMultiLabel::~cMultiLabel()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cMultiLabel::setStyle(uint32_t style)
{
    this->style = style;

    textStyle = (style & controlStyleCenterX ? OPT_CENTERX : 0) |
                (style & controlStyleCenterY ? OPT_CENTERY : 0) |
                (style & controlStyleRightX ? OPT_RIGHTX : 0);

    uint32_t fontIndex = FONT_INDEX_FROM_STYLE;
    font = &fonts[fontIndex];
}

void cMultiLabel::setText(const char* text)
{
    this->text = text;
}

void cMultiLabel::setValue(int value)
{
    this->value = value;
}

void cMultiLabel::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cMultiLabel::setData(void* data)
{
    this->data = (strMultiLabelData*)(data);
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cMultiLabel::update()
{
    int16_t border_x = posX; //, border_y = posY-2;
    int16_t border_y = posY;
    int16_t lines_space = font->height;

    int16_t text_y = posY + data->yOffset / 2;
    int16_t text_h = height - data->yOffset - data->yForFxTextHole;

    if (data->textLinesCount > 0 && data->textLinesCount < 6 && height > font->height)
    {
        lines_space = (text_h - font->height * data->textLinesCount) / (data->textLinesCount + 1);
    }

    if (style & controlStyleCenterX)
    {
        border_x = posX - (width / 2);
    }
    if (style & controlStyleCenterY)
    {
        border_y = posY - (height / 2);
        text_y = text_y + lines_space + font->height / 2;
    }

    if (style & controlStyleBackground)
    {
        if (style & controlStyleNoTransparency)
        {
            API_BLEND_FUNC(SRC_ALPHA, ZERO);
        }

        API_COLOR(colors[1]);

        if (style & controlStyleRoundedBorder)
            API_LINE_WIDTH(32);
        else
            API_LINE_WIDTH(16);

        uint16_t ccRectHeight = ((height - data->yForFxTextHole) * 1) / 5;
        uint16_t ccRectY = border_y;

        uint16_t performanceRectHeight = ((height - data->yForFxTextHole) * 4) / 5;
        uint16_t performanceRectY = border_y + ccRectHeight + data->yForFxTextHole;

        API_BEGIN(RECTS);
        API_VERTEX2F(border_x, ccRectY);
        API_VERTEX2F(border_x + width, ccRectY + ccRectHeight);
        API_END();

        API_BEGIN(RECTS);
        API_VERTEX2F(border_x, performanceRectY);
        API_VERTEX2F(border_x + width, performanceRectY + performanceRectHeight);
        API_END();

        if (style & controlStyleBottomShadow)
        {
            API_BLEND_FUNC(SRC_ALPHA, ZERO);

            API_SAVE_CONTEXT();
            uint16_t grad_y = posY + height - 10;
            uint16_t grad_h = 12;
            API_SCISSOR_XY(border_x, grad_y);
            API_SCISSOR_SIZE(width + 1, grad_h);
            API_CMD_GRADIENT(0, grad_y, colors[1], 0, grad_y + grad_h, 0x0);
            API_RESTORE_CONTEXT();

            //			API_COLOR(colors[0]);
            //			API_LINE_WIDTH(1);
            //			API_BEGIN(LINES);
            //			API_VERTEX2F(border_x-1, posY);
            //			API_VERTEX2F(border_x-1, posY+height);
            //			API_VERTEX2F(border_x+width+1, posY);
            //			API_VERTEX2F(border_x+width+1, posY+height);
            //			API_END();

            API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        }

        if (style & controlStyleNoTransparency)
        {
            API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        }
    }
    if (style & controlStyleBorder)
    {
        API_COLOR(colors[2]);

        API_LINE_WIDTH(24);

        API_BEGIN(LINE_STRIP);
        API_VERTEX2F(border_x, border_y);
        API_VERTEX2F(border_x + width, border_y);
        API_VERTEX2F(border_x + width, border_y + height);
        API_VERTEX2F(border_x, border_y + height);
        API_VERTEX2F(border_x, border_y);
        API_END();
    }

    uint8_t ccIdx = data->textLinesCount - 1;

    if (data->text[ccIdx] != nullptr && *data->text[ccIdx] != 0)
    {

        if (data->selected > 0 && data->selected - 1 == ccIdx)
            API_COLOR(colors[2]);
        else
            API_COLOR(colors[0]);

        API_CMD_TEXT(posX + data->xOffset, text_y, font->handle, textStyle, data->text[ccIdx]);
    }

    uint16_t ccRectHeight = ((height - data->yForFxTextHole) * 1) / 5;
    text_y = border_y + ccRectHeight + data->yForFxTextHole + data->yOffset / 2;

    for (uint8_t i = 0; i < (data->textLinesCount - 1); i++)
    {
        if (data->text[i] != nullptr && *data->text[i] != 0)
        {

            if (data->selected > 0 && data->selected - 1 == i)
                API_COLOR(colors[2]);
            else
                API_COLOR(colors[0]);

            API_CMD_TEXT(posX + data->xOffset, text_y, font->handle, textStyle, data->text[i]);
        }

        text_y = text_y + (lines_space + font->height);
    }
}
