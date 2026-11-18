#include "FT812.h"

#include "display.h"
#include "displayControls.h"
#include "keyboardControl.h"

cKeyboard::cKeyboard(strControlProperties* properties)
  : cDisplayControl(controlIdKeyboard, 1)
{
    colorsCount = 3;
    colors = display.getTheme().keyboardColors;

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
    fillKeyboard();
}

cKeyboard::~cKeyboard()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cKeyboard::setStyle(uint32_t style)
{
    this->style = style;

    textStyle = (style & controlStyleCenterX ? OPT_CENTERX : 0) |
                (style & controlStyleCenterY ? OPT_CENTERY : 0) |
                (style & controlStyleRightX ? OPT_RIGHTX : 0);

    textFont = FONT_INDEX_FROM_STYLE;
    textFont = (textFont >= 0) ? textFont : 0;
    textFont = fonts[textFont].handle;
}

void cKeyboard::setText(const char* text)
{
    this->text = text;
}

void cKeyboard::setValue(int value)
{
    this->value = value;
}

void cKeyboard::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

/*
void cKeyboard::setDefaultColors(uint32_t colors[])
{
    memcpy(defaultColors, colors, colorsCount*4);
}
*/

void cKeyboard::setData(void* /*data*/)
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cKeyboard::update()
{
    API_COLOR(colors[0]);
    API_LINE_WIDTH(kRedFrameWidth);
    API_BEGIN(RECTS);

    char localString[2];
    localString[1] = 0;
    for (uint8_t i = 0; i < KEYBOARD_SIZE; i++)
    {
        if (i == 26 || i == 29)
        {
            API_COLOR(colors[1]);
        }
        API_VERTEX2F(keyboard[i].x, keyboard[i].y);
        API_VERTEX2F(keyboard[i].x + keyboard[i].w, keyboard[i].y + keyboard[i].h);
        if (i == 26 || i == 29)
        {
            API_COLOR(colors[0]);
        }
    }

    API_END();

    if (value <= 41)
        keyboardShift = 0;
    else
    {
        keyboardShift = 1;
        value -= 42;
    }

    API_COLOR(colors[1]);

    for (uint8_t i = 0; i < KEYBOARD_SIZE; i++)
    {
        if (i == 26 || i == 29)
        {
            API_COLOR(colors[0]);
        }
        if (keyboardShift)
        {
            if (charOnKeyboardBig[i] > 2)
            {
                localString[0] = charOnKeyboardBig[i];
                API_CMD_TEXT(keyboard[i].x + keyboard[i].w / 2,
                             keyboard[i].y + keyboard[i].h / 2,
                             textFont,
                             OPT_CENTER,
                             localString);
            }
            else if (charOnKeyboardBig[i] == 0)
                API_CMD_TEXT(keyboard[i].x + keyboard[i].w / 2,
                             keyboard[i].y + keyboard[i].h / 2,
                             textFont,
                             OPT_CENTER,
                             "Backspace");
            else if (charOnKeyboardBig[i] == 1)
                API_CMD_TEXT(keyboard[i].x + keyboard[i].w / 2,
                             keyboard[i].y + keyboard[i].h / 2,
                             textFont,
                             OPT_CENTER,
                             "Caps Lock");
            else if (charOnKeyboardBig[i] == 2)
                API_CMD_TEXT(keyboard[i].x + keyboard[i].w / 2,
                             keyboard[i].y + keyboard[i].h / 2,
                             textFont,
                             OPT_CENTER,
                             "Space");
        }
        else
        {
            if (charOnKeyboardSmall[i] > 2)
            {
                localString[0] = charOnKeyboardSmall[i];
                API_CMD_TEXT(keyboard[i].x + keyboard[i].w / 2,
                             keyboard[i].y + keyboard[i].h / 2,
                             textFont,
                             OPT_CENTER,
                             localString);
            }
            else if (charOnKeyboardSmall[i] == 0)
                API_CMD_TEXT(keyboard[i].x + keyboard[i].w / 2,
                             keyboard[i].y + keyboard[i].h / 2,
                             textFont,
                             OPT_CENTER,
                             "Backspace");
            else if (charOnKeyboardSmall[i] == 1)
                API_CMD_TEXT(keyboard[i].x + keyboard[i].w / 2,
                             keyboard[i].y + keyboard[i].h / 2,
                             textFont,
                             OPT_CENTER,
                             "Caps Lock");
            else if (charOnKeyboardSmall[i] == 2)
                API_CMD_TEXT(keyboard[i].x + keyboard[i].w / 2,
                             keyboard[i].y + keyboard[i].h / 2,
                             textFont,
                             OPT_CENTER,
                             "Space");
        }

        if (i == 26 || i == 29)
        {
            API_COLOR(colors[1]);
        }
    }

    if (value >= 0)
    {
        API_COLOR(colors[2]);
        API_LINE_WIDTH(kRedFrameWidth);
        API_BEGIN(LINE_STRIP);

        uint16_t localX, localY;
        localX = keyboard[value].x - 1;
        localY = keyboard[value].y - 1;
        API_VERTEX2F(localX, localY);
        localX = keyboard[value].x + keyboard[value].w + 1;
        localY = keyboard[value].y - 1;
        API_VERTEX2F(localX, localY);
        localX = keyboard[value].x + keyboard[value].w + 1;
        localY = keyboard[value].y + keyboard[value].h + 1;
        API_VERTEX2F(localX, localY);
        localX = keyboard[value].x - 1;
        localY = keyboard[value].y + keyboard[value].h + 1;
        API_VERTEX2F(localX, localY);
        localX = keyboard[value].x - 1;
        localY = keyboard[value].y - 1;
        API_VERTEX2F(localX, localY);
        API_END();
    }
}

void cKeyboard::fillKeyboard()
{

    uint16_t offset_x = width / 12;
    uint16_t offset_y = height / 4;

    for (uint8_t i = 0; i < KEYBOARD_SIZE; i++)
    {
        if (i == 0)
        {
            keyboard[i].x = posX;
            keyboard[i].y = posY;
            keyboard[i].w = offset_x - offset_x * 0.0667;
            keyboard[i].h = offset_y * 0.9330;
        }
        else if ((i >= 1) && (i <= 9))
        {
            keyboard[i].x = keyboard[i - 1].x + offset_x;
            keyboard[i].y = posY;
            keyboard[i].w = offset_x - offset_x * 0.0667;
            keyboard[i].h = offset_y * 0.9330;
        }
        else if (i == 10)
        {
            keyboard[i].x = keyboard[i - 1].x + offset_x;
            keyboard[i].y = posY;
            keyboard[i].w = 2 * offset_x - offset_x * 0.0667;
            keyboard[i].h = offset_y * 0.9330;
        }
        else if ((i >= 11) && (i <= 22))
        {
            if (i == 11)
                keyboard[i].x = keyboard[0].x;
            else
                keyboard[i].x = keyboard[i - 1].x + offset_x;
            keyboard[i].y = posY + offset_y;
            keyboard[i].w = offset_x - offset_x * 0.0667;
            keyboard[i].h = offset_y * 0.9330;
        }
        else if ((i >= 23) && (i <= 32))
        {
            if (i == 23)
                keyboard[i].x = keyboard[0].x;
            else
                keyboard[i].x = keyboard[i - 1].x + offset_x;
            keyboard[i].y = posY + 2 * offset_y;
            keyboard[i].w = offset_x - offset_x * 0.0667;
            keyboard[i].h = offset_y * 0.9330;
        }
        else if (i == 33)
        {
            keyboard[i].x = keyboard[i - 1].x + offset_x;
            keyboard[i].y = posY + 2 * offset_y;
            keyboard[i].w = 2 * offset_x - offset_x * 0.0667;
            keyboard[i].h = offset_y * 0.9330;
        }
        else if ((i >= 34) && (i <= 40))
        {
            if (i == 34)
                keyboard[i].x = keyboard[0].x;
            else
                keyboard[i].x = keyboard[i - 1].x + offset_x;
            keyboard[i].y = posY + 3 * offset_y;
            keyboard[i].w = offset_x - offset_x * 0.0667;
            keyboard[i].h = offset_y * 0.9330;
        }
        else if (i == 41)
        {
            keyboard[i].x = keyboard[i - 1].x + offset_x;
            keyboard[i].y = posY + 3 * offset_y;
            keyboard[i].w = 5 * offset_x - offset_x * 0.0667;
            keyboard[i].h = offset_y * 0.9330;
        }
    }
}
