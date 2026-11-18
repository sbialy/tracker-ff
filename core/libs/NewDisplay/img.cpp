#include "FT812.h"

#include "display.h"
#include "displayControls.h"
#include "imgControl.h"

cImg::cImg(strControlProperties* properties)
  : cDisplayControl(controlIdImg, 1)
{
    colorsCount = 0;
    colors = nullptr;

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

cImg::~cImg()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cImg::setStyle(uint32_t style)
{
    this->style = style;

    textStyle = (style & controlStyleCenterX ? OPT_CENTERX : 0) |
                (style & controlStyleCenterY ? OPT_CENTERY : 0) |
                (style & controlStyleRightX ? OPT_RIGHTX : 0);

    textFont = FONT_INDEX_FROM_STYLE;
    textFont = (textFont >= 0) ? textFont : 0;
    textFont = fonts[textFont].handle;
}

void cImg::setText(const char* text)
{
    this->text = text;
}

void cImg::setValue(int value)
{
    this->value = value;
}

void cImg::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cImg::setData(void* /*data*/)
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cImg::update()
{
    if (value < 300000 || value > 1000000)
    {
        return;
    }

    int16_t x = posX, y = posY;

    if (style & controlStyleCenterX)
    {
        x = posX - getProps.Width / 2;
    }
    if (style & controlStyleCenterY)
    {

        y = posY - getProps.Height / 2;
    }

    // API_LIB_WriteDataToCMD
    // API_CMD_LOADIMAGE(value, 0);
    // API_CMD_SETBITMAP(value, RGB565, 150, 150);

    // API_BITMAP_HANDLE(12);
    // API_CMD_SETBITMAP(value, PALETTED4444, 150, 150);

    // API_BITMAP_HANDLE(12);
    // API_BITMAP_SOURCE(value);
    API_SAVE_CONTEXT();

    API_CMD_SETBITMAP(value, RGB565, getProps.Width, getProps.Height);

    int transformStandard = 256;
    int tranformImg = 256;

    if (style & controlStyleTransformImg)
    {
        tranformImg = 276;
        API_BITMAP_TRANSFORM_A(tranformImg);
    }

    API_COLOR(0xffffff);
    API_BEGIN(RECTS);
    API_VERTEX2F(x, y);
    API_VERTEX2F(x + (getProps.Width * ((float)transformStandard / (float)tranformImg)),
                 y + getProps.Height);
    API_END();

    //	API_BITMAP_LAYOUT(RGB565, getProps.Width*2, getProps.Height); // Format, Stride, Height
    //	API_BITMAP_SIZE(BILINEAR, BORDER, BORDER, getProps.Width, getProps.Height);
    //	API_BITMAP_LAYOUT_H((getProps.Width * 2) >> 10, getProps.Height >> 9);
    //	API_BITMAP_SIZE_H(getProps.Width >> 9, getProps.Width >> 9);

    API_BEGIN(BITMAPS);
    // API_BLEND_FUNC(SRC_ALPHA, ZERO);
    // API_BLEND_FUNC(SRC_ALPHA, ONE);
    API_VERTEX2F(x, y);
    API_END();

    if (style & controlStyleTransformImg)
    {
        API_BITMAP_TRANSFORM_A(transformStandard);
    }

    // API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);

    API_RESTORE_CONTEXT();
}
