#include "FT812.h"

#include "display.h"
#include "displayControls.h"
#include "pointsControl.h"

cPoints::cPoints(strControlProperties* properties)
  : cDisplayControl(controlIdPoints, 1)
{
    colorsCount = 4;
    colors = display.getTheme().waveformPointsColors;

    if (properties == nullptr)
    {
        posX = 0;
        posY = 0;
        text = nullptr;
        points = nullptr;
        value = 0;
        width = 0;
        height = 0;
        style = 0;
        return;
    }

    colors = (properties->colors == nullptr) ? colors : properties->colors;

    points = (strTrackerPoints*)properties->data;

    posX = properties->x;
    posY = properties->y;

    text = properties->text;
    value = properties->value;

    width = properties->w;
    height = properties->h;

    setStyle(properties->style);
}

cPoints::~cPoints()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cPoints::setStyle(uint32_t style)
{
    this->style = style;

    textStyle = (style & controlStyleCenterX ? OPT_CENTERX : 0) |
                (style & controlStyleCenterY ? OPT_CENTERY : 0) |
                (style & controlStyleRightX ? OPT_RIGHTX : 0);

    textFont = FONT_INDEX_FROM_STYLE;
    textFont = (textFont >= 0) ? textFont : 0;
    textFont = fonts[textFont].handle;
}

void cPoints::setText(const char* text)
{
    this->text = text;
}

void cPoints::setValue(int /*value*/)
{
}

void cPoints::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cPoints::setData(void* data)
{
    points = (strTrackerPoints*)data;
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cPoints::update()
{
    if (points == nullptr)
    {
        return;
    }

    API_SAVE_CONTEXT();
    API_COLOR(colors[0]);

    if (points->pointsType == 0)
    {
        uint16_t start = points->startPoint + posX;
        uint16_t end = points->endPoint + posX;
        // tlo start - end
        API_COLOR(colors[1]);
        API_BLEND_FUNC(SRC_ALPHA, ONE);
        API_LINE_WIDTH(16);

        API_BEGIN(RECTS);
        API_VERTEX2F((points->startPoint >= 0) ? start : posX, posY);
        API_VERTEX2F((points->endPoint >= 0) ? end : posX + width, posY + height);
        API_END();

        API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);

        // API_LINE_WIDTH(8);
    }
    else if (points->pointsType > 0 && (points->loopPoint1 >= 0 || points->loopPoint2 >= 0))
    {
        uint16_t loop1 = points->loopPoint1 + posX;
        uint16_t loop2 = points->loopPoint2 + posX;
        // tlo loop
        API_COLOR(colors[1]);
        API_BLEND_FUNC(SRC_ALPHA, ONE);
        API_LINE_WIDTH(16);

        API_BEGIN(RECTS);
        API_VERTEX2F((points->loopPoint1 >= 0) ? loop1 : posX, posY);
        API_VERTEX2F((points->loopPoint2 >= 0) ? loop2 : posX + width, posY + height);
        API_END();

        API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);

        // API_LINE_WIDTH(8);

        if (points->loopPoint1 >= 0)
        {
            if (points->selected & selectLoop1)
            {
                API_COLOR(points->zerocrossed & loop1Zerocrossed ? colors[3] : colors[2]);
            }
            else
            {
                API_COLOR(colors[0]);
            }

            // loop point 1
            drawPoint(loop1, posY, height, false, true);
        }
        if (points->loopPoint2 >= 0)
        {
            if (points->selected & selectLoop2)
            {
                API_COLOR(points->zerocrossed & loop2Zerocrossed ? colors[3] : colors[2]);
            }
            else
            {
                API_COLOR(colors[0]);
            }

            // loop point 2
            drawPoint(loop2, posY, height, false, false);
        }
    }

    if (points->startPoint >= 0)
    {
        uint16_t startPos = points->startPoint + posX;

        if (points->selected & selectStart)
        {
            API_COLOR(points->zerocrossed & startZerocrossed ? colors[3] : colors[2]);
        }
        else
        {
            API_COLOR(colors[0]);
        }

        // start point
        drawPoint(startPos, posY, height, true, true);
    }

    if (points->endPoint >= 0)
    {
        uint16_t endPos = points->endPoint + posX;

        if (points->selected & selectEnd)
        {
            API_COLOR(points->zerocrossed & endZerocrossed ? colors[3] : colors[2]);
        }
        else
        {
            API_COLOR(colors[0]);
        }
        // end point
        drawPoint(endPos, posY, height, true, false);
    }

    API_RESTORE_CONTEXT();
}

void cPoints::drawPoint(int posX, int posY, int height, bool topMark, bool markSide)
{
    API_BEGIN(LINES);
    API_VERTEX2F(posX, posY + (topMark ? -8 : 0));
    API_VERTEX2F(posX, posY + height + (topMark ? 0 : 8));
    API_END();

    // znaczek loop
    API_BEGIN(LINE_STRIP);
    API_VERTEX2F(posX + (markSide ? 1 : -1), posY + (topMark ? -8 : height + 2));
    API_VERTEX2F(posX + (markSide ? 5 : -5), posY + (topMark ? -5 : height + 5));
    API_VERTEX2F(posX + (markSide ? 1 : -1), posY + (topMark ? -2 : height + 8));
    API_END();
}
