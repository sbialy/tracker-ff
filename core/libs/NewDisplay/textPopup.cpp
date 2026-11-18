#include "FT812.h"

#include "display.h"
#include "displayControls.h"
#include "textPopupControl.h"

#include <string.h>

static constexpr uint8_t grad_h = 13;
static constexpr uint8_t alpha_tab[grad_h] = { 130, 120, 110, 100, 90, 80, 70,
                                               60,  50,  40,  30,  20, 10 };

cTextPopup::cTextPopup(strControlProperties* properties)
  : cDisplayControl(controlIdTextPopup, 1)
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

    data = (strTextPopupData*)(properties->data);

    width = properties->w;
    height = properties->h;

    setStyle(properties->style);
}

cTextPopup::~cTextPopup()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cTextPopup::setStyle(uint32_t style)
{
    this->style = style;

    textStyle = (style & controlStyleCenterX ? OPT_CENTERX : 0) |
                (style & controlStyleCenterY ? OPT_CENTERY : 0) |
                (style & controlStyleRightX ? OPT_RIGHTX : 0);

    int8_t textFont = FONT_INDEX_FROM_STYLE;
    textFont = (textFont >= 0) ? textFont : 0;
    font = &fonts[textFont];
}

void cTextPopup::setText(const char* text)
{
    this->text = text;
}

void cTextPopup::setValue(int value)
{
    this->value = value;
}

void cTextPopup::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cTextPopup::setData(void* data)
{
    this->data = (strTextPopupData*)(data);
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cTextPopup::update()
{
    int16_t border_x = posX;
    int16_t border_y = posY;
    int16_t border_w = width;

    if (style & controlStyleCenterX)
    {
        border_x = posX - (width / 2);
    }
    if (style & controlStyleCenterY)
    {
        border_y = posY - (height / 2);
    }

    if (style & controlStyleBackground)
    {
        int maxTextWidth = 0;

        if (data != nullptr && data->multiLineText != nullptr && data->textLinesCount > 0)
        {
            for (uint8_t i = 0; i < data->textLinesCount; i++)
            {
                if (data->multiLineText[i] != nullptr)
                {
                    strFont* lineFont = (strFont*)font;
                    uint32_t lineStyle = *data->multiLineStyle[i];

                    if (lineStyle > 0)
                    {
                        int8_t textFont = (((lineStyle >> 8) & 15) - 1);
                        textFont = (textFont >= 0) ? textFont : 0;
                        lineFont = &fonts[textFont];
                    }

                    int lineWidth = getTextWidth(
                      lineFont, data->multiLineText[i], strlen(data->multiLineText[i]));

                    maxTextWidth = std::max(maxTextWidth, lineWidth);
                }
            }
        }

        maxTextWidth += 20;
        border_w = maxTextWidth > width ? maxTextWidth : border_w;
        if (style & controlStyleCenterX && maxTextWidth > width)
        {
            border_x = posX - (border_w / 2);
        }

        if (style & controlStyleNoTransparency)
        {
            API_BLEND_FUNC(SRC_ALPHA, ZERO);
        }

        API_COLOR(colors[1]);

        if (style & controlStyleRoundedBorder)
            API_LINE_WIDTH(32);
        else
            API_LINE_WIDTH(16);

        API_BEGIN(RECTS);
        API_VERTEX2F(border_x, border_y);
        API_VERTEX2F(border_x + border_w, border_y + height);
        API_END();

        if (style & controlStyleNoTransparency)
        {
            API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        }

        if (style & controlStyleBottomShadow)
        {
            uint16_t grad_y = border_y + height + 1;

            API_LINE_WIDTH(16);
            API_BEGIN(LINES);
            API_COLOR(colors[2]);

            API_SAVE_CONTEXT();

            for (uint8_t i = 0; i < grad_h; i++)
            {
                API_COLOR_A(alpha_tab[i]);
                API_VERTEX2F(border_x, grad_y + i);
                API_VERTEX2F(border_x + border_w, grad_y + i);
            }

            API_RESTORE_CONTEXT();
        }
    }

    uint16_t line_textStyle;
    int16_t line_textFont;
    int16_t line_posX;
    int16_t line_posY = posY;

    if (style & controlStyleCenterY)
    {
        line_posY = posY - height / 2;
    }

    if (data != nullptr && data->multiLineText != nullptr && data->textLinesCount > 0)
    {
        for (uint8_t i = 0; i < data->textLinesCount; i++)
        {
            line_posY += height / (data->textLinesCount + 1);

            if (data->multiLineText[i] != nullptr)
            {
                // color
                if (*data->multiLineColors[i] <= 0xffffff)
                {
                    API_COLOR(*data->multiLineColors[i]);
                }
                else
                {
                    API_COLOR(colors[0]);
                }

                // styl :OOOOOOOOO
                line_textStyle = OPT_CENTERY;
                line_textFont = font->handle;

                uint32_t tempStyle = *data->multiLineStyle[i];
                if (tempStyle > 0)
                {
                    line_textStyle = (tempStyle & controlStyleCenterX ? OPT_CENTERX : 0) |
                                     (tempStyle & controlStyleCenterY ? OPT_CENTERY : 0) |
                                     (tempStyle & controlStyleRightX ? OPT_RIGHTX : 0);

                    int8_t textFont = (((tempStyle >> 8) & 15) - 1);
                    textFont = (textFont >= 0) ? textFont : 0;
                    line_textFont = fonts[textFont].handle;
                }

                // pozycja z uwzglednieniem stylu
                if (line_textStyle & OPT_CENTERX)
                    line_posX = posX;
                else if (line_textStyle & OPT_RIGHTX)
                    line_posX = posX + border_w - 10;
                else
                    line_posX = posX + 10;

                // wkoncu text
                API_CMD_TEXT(
                  line_posX, line_posY, line_textFont, line_textStyle, data->multiLineText[i]);
            }
        }
    }
}
