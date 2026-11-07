#ifdef TRACKER_ARRANGEMENT

#include "FT812.h"

#include "arrangementControl.h"
#include "display.h"

#include <cstring>

static void Number2Bitmaps(int16_t x, int16_t y, uint8_t font_x, uint8_t font_y, int16_t number);
static void Number2HexBitmaps(int16_t x, int16_t y, uint8_t font_x, uint8_t font_y, int16_t number);

static uint8_t fourParamsOffset[4] = {
    5,
    55,
    5,
    55,
};
static uint8_t twoParamsOffsetFirst[4] = {
    7,
    10,
    5,
    0,
};
static uint8_t twoParamsOffsetSecond[4] = {
    0,
    61,
    50,
    50,
};
static uint8_t oneParamsOffset[4] = {
    32,
    37,
    27,
    27,
};

static const uint8_t fourParamsSelOffsetX[4] = {
    1,
    50,
    1,
    50,
};
static const uint8_t fourParamsSelOffsetY[4] = {
    0,
    0,
    25,
    25,
};
static const uint8_t fourParamsSelWidth[4] = {
    47,
    47,
    47,
    47,
};
static const uint8_t fourParamsSelHight[4] = {
    20,
    20,
    20,
    20,
};

static const uint8_t twoParamsSel1Offset[4] = {
    1,
    1,
    1,
    0,
};
static const uint8_t twoParamsSel1Width[4] = {
    40,
    33,
    43,
    0,
};

static const uint8_t twoParamsSel2Offset[4] = {
    0,
    51,
    43,
    43,
};
static const uint8_t twoParamsSel2Width[4] = {
    0,
    37,
    45,
    45,
};

static const uint8_t oneParamsSelOffset[4] = {
    24,
    29,
    19,
    19,
};
static const uint8_t oneParamsSelWidth[4] = {
    40,
    30,
    50,
    50,
};

const uint8_t paramToStateOffset[4] = {
    1,
    2,
    4,
    8,
};

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
cArrangement::cArrangement(strControlProperties* properties)
  : cDisplayControl(controlIdArrangement)
{
    // visibleCharOffset = 0;
    // firstVisibleTrack = 0;
    // visibleTracksOffset = 0;

    colorsCount = 16;
    colors = display.getTheme().patternTrackerColors;
    dimFx1Color = ((colors[8] & 0xFEFEFE) >> 1) + ((colors[1] & 0xFEFEFE) >> 1);
    dimFx2Color = ((colors[9] & 0xFEFEFE) >> 1) + ((colors[1] & 0xFEFEFE) >> 1);

    refreshStep = 0;

    if (properties == nullptr)
    {
        posX = 0;
        posY = 0;
        width = 0;
        height = 0;
        tracks = nullptr;
        this->style = 0;
        value = 0;
        return;
    }

    setColors((properties->colors == nullptr) ? colors : properties->colors);

    tracks = (strTrackerPattern*)properties->data;
    posX = properties->x;
    posY = properties->y;
    width = properties->w;
    height = properties->h;
    value = properties->value;

    setStyle(properties->style);
}

cArrangement::~cArrangement()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cArrangement::setStyle(uint32_t style)
{
    this->style = style;
}

void cArrangement::setText(const char* text)
{
}

void cArrangement::setValue(int value)
{
    this->value = value;
}

void cArrangement::setColors(const uint32_t* colors)
{
    this->colors = colors;
    dimFx1Color = ((colors[8] & 0xFEFEFE) >> 1) + ((colors[1] & 0xFEFEFE) >> 1);
    dimFx2Color = ((colors[9] & 0xFEFEFE) >> 1) + ((colors[1] & 0xFEFEFE) >> 1);
}

/*
void cArrangement::setDefaultColors(uint32_t colors[])
{
    memcpy(defaultColors, colors, colorsCount*4);
}
*/

void cArrangement::setData(void* data)
{
    tracks = (strTrackerPattern*)data;
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
uint8_t cArrangement::update()
{
    // API_LIB_BeginDirectDL();
    displayDriver->beginCoProListNoCheck();
    API_CMD_DLSTART();

    API_SAVE_CONTEXT();

    switch (refreshStep)
    {
        case 0: refresh1(); break;
        case 1: refresh2(); break;
        case 2: refresh3(); break;
        case 3: refresh4(); break;
        case 4: refresh5(); break;
        default: refreshStep = 0; break;
    }

    API_RESTORE_CONTEXT();

    displayDriver->endCoProList();

    return 0;
}

uint8_t cArrangement::memCpy(uint32_t address)
{
    uint32_t dlOffset = display.getDisplayDriver()->getDisplayListOffset();

    displayDriver->beginCoProListNoCheck();

    switch (refreshStep)
    {
        case 0: API_CMD_MEMCPY(address, RAM_DL, dlOffset); break;
        case 1: API_CMD_MEMCPY(address + ramPartSize[0], RAM_DL, dlOffset); break;
        case 2: API_CMD_MEMCPY(address + ramPartSize[0] + ramPartSize[1], RAM_DL, dlOffset); break;
        case 3:
            API_CMD_MEMCPY(
              address + ramPartSize[0] + ramPartSize[1] + ramPartSize[2], RAM_DL, dlOffset);
            break;
        case 4:
            API_CMD_MEMCPY(address + ramPartSize[0] + ramPartSize[1] + ramPartSize[2] +
                             ramPartSize[3],
                           RAM_DL,
                           dlOffset);
            break;
        default: break;
    }

    ramPartSize[refreshStep] = dlOffset;

    displayDriver->endCoProList();
    // displayDriver->awaitCoProEmpty();

    /*
        if(dlOffset > 4000)
        {
            __asm__ __volatile__ ("bkpt #0");
        }
    */

    refreshStep++;
    if (refreshStep > 4)
    {
        refreshStep = 0;
        return 0;
    }

    return 1;
}

uint8_t cArrangement::append(uint32_t address)
{
    // length = ramPartSize[0]+ramPartSize[1]+ramPartSize[2]+ramPartSize[3]+ramPartSize[4];
    // API_CMD_APPEND(address		, 3900);

    //	API_CMD_APPEND(address,
    // ramPartSize[0]+ramPartSize[1]+ramPartSize[2]+ramPartSize[3]+ramPartSize[4]);

    API_CMD_APPEND(address, ramPartSize[0]);
    API_CMD_APPEND(address + ramPartSize[0], ramPartSize[1]);
    API_CMD_APPEND(address + ramPartSize[0] + ramPartSize[1], ramPartSize[2]);
    API_CMD_APPEND(address + ramPartSize[0] + ramPartSize[1] + ramPartSize[2], ramPartSize[3]);
    API_CMD_APPEND(address + ramPartSize[0] + ramPartSize[1] + ramPartSize[2] + ramPartSize[3],
                   ramPartSize[4]);

    ramSize = ramPartSize[0] + ramPartSize[1] + ramPartSize[2] + ramPartSize[3] + ramPartSize[4];

    /*
        if(ramPartSize[0] +ramPartSize[1] +ramPartSize[2] +ramPartSize[3] +ramPartSize[4] > 8000)
        {
        //	Serial.println("ft800 cmd overload");
            __asm__ __volatile__ ("bkpt #0");
        }
    */
    return 0;
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cArrangement::refresh1()
{
    displayMode = value;
    lastFontHandle = -1; // zerowanie ostatnio uzywanej czcionki

    visibleStepsCount = 7;
    stepsDataStructOffset = 7 - (visibleStepsCount / 2);

    trackHeadWidth = 90;
    stepWidth = (800 - trackHeadWidth) / visibleStepsCount;

    visibleTracksCount = 8;
    firstVisibleTrack = 0;

    stepsNumbersHight = 18;
    trackHight = (423 - stepsNumbersHight) / 8;

    paramCount = 4;

    backgroundDivider();

    calculateSelection();

    playHead();

    selection();

    stepNumbers();
}

//------------------------------------------------------------------------------------------
void cArrangement::refresh2()
{
    notes();
}

void cArrangement::refresh3()
{

    instruments();
}

void cArrangement::refresh4()
{
    fxes1();
}

void cArrangement::refresh5()
{
    fxes2();

    tracksNames();

    API_BLEND_FUNC(SRC_ALPHA, ZERO);
    lines();

    API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
}

//-------------------------------------------------------------------------------------
// podzialka
void cArrangement::backgroundDivider()
{
    int16_t div_row = tracks->actualStep - visibleStepsCount / 2;

    // tlo calosci
    API_COLOR(colors[1]);
    API_LINE_WIDTH(16);
    API_BEGIN(RECTS);

    API_VERTEX2F(posX + 1, posY);
    API_VERTEX2F(width - 2, posY + height - 2);

    // podzialka
    API_LINE_WIDTH(16);
    API_COLOR(colors[11]);

    for (uint8_t i = 0; i < visibleStepsCount; i++)
    {
        if (div_row < 0 || div_row > tracks->patternLength - 1)
        {
            div_row++;
            continue;
        }

        if (div_row % tracks->stepDevider == 0)
        {
            API_VERTEX2F(trackHeadWidth + (i * stepWidth) + 1, stepsNumbersHight);
            API_VERTEX2F(trackHeadWidth + (i * stepWidth) + stepWidth - 1, 423);
        }

        div_row++;
    }

    API_END();

    // API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
    // API_BLEND_FUNC(SRC_ALPHA, ZERO);

    if (style & controlStyleBottomShadow)
    {
        // gradient-cien dolny
        API_SAVE_CONTEXT();
        API_SCISSOR_XY(1, 413);
        API_SCISSOR_SIZE(798, 10);
        API_CMD_GRADIENT(0, 413, colors[1], 0, 423, 0x0);
        API_RESTORE_CONTEXT();
    }

    // API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
}

//-------------------------------------------------------------------------------------
// // linie oddzielajace tracki
void cArrangement::lines()
{
    API_COLOR(colors[0]);
    API_LINE_WIDTH(1);
    API_BEGIN(RECTS);

    uint16_t y;
    for (uint8_t i = 0; i < visibleTracksCount; i++)
    {
        y = stepsNumbersHight + trackHight * i;

        API_VERTEX2F(0, y);
        API_VERTEX2F(799, y);
    }

    API_END();
}

#define DISP_RGB(red, green, blue)                                                                 \
    ((((red)&255UL) << 16) | (((green)&255UL) << 8) | (((blue)&255UL) << 0))

//-------------------------------------------------------------------------------------
// playhead
void cArrangement::playHead()
{
    uint8_t half_of_steps = visibleStepsCount / 2;

    if (tracks->playheadPosition > tracks->actualStep - half_of_steps &&
        tracks->playheadPosition < tracks->actualStep + half_of_steps)
    {
        uint8_t step = tracks->playheadPosition - (tracks->actualStep - half_of_steps);
        uint16_t phy1 = 0;
        uint16_t phy2 = 413;
        uint16_t phx1 = trackHeadWidth + (step)*stepWidth;
        uint16_t phx2 = trackHeadWidth + (step + 1) * stepWidth;
        // uint32_t playheadColor = colors[13];

        if (selectActive && tracks->playheadSelectMode)
        {
            phy1 = select1_y;
            phy2 = select2_y;
        }
        else if (tracks->playheadRecMode) // pozostalosc narazie nie uzywana
        {
            // playheadColor = colors[15];

            API_COLOR(colors[15]);
            API_LINE_WIDTH(8);
            API_BEGIN(RECTS);

            if (paramCount == 1 && ((1 << tracks->selectedParam) &
                                    displayMode)) // jesli jest na ekranie zaznaczony parametr
            {
                // for(uint8_t i = 0; i<8; i++)
                //{
                //	uint16_t recPosX = 1+oneParamsSelOffset[tracks->selectedParam]+(tracksSpace*i);
                uint16_t recPosX = 1 + oneParamsSelOffset[tracks->selectedParam] +
                                   (tracksSpace * (tracks->actualTrack - firstVisibleTrack));
                if (recPosX < 800 && recPosX > 0)
                {
                    API_VERTEX2F(recPosX, 7 * 28);
                    API_VERTEX2F(recPosX + oneParamsSelWidth[tracks->selectedParam], 8 * 28 - 1);
                }
                //}
            }
            else if (paramCount == 2 && ((1 << tracks->selectedParam) & displayMode))
            {
                uint8_t param = whichParamFromTwoSelected();
                uint8_t offset = param == 0 ? twoParamsSel1Offset[tracks->selectedParam]
                                            : twoParamsSel2Offset[tracks->selectedParam];
                uint8_t width = param == 0 ? twoParamsSel1Width[tracks->selectedParam]
                                           : twoParamsSel2Width[tracks->selectedParam];

                // for(uint8_t i = 0; i<8; i++)
                //{
                uint16_t recPosX =
                  1 + offset + (tracksSpace * (tracks->actualTrack - firstVisibleTrack));
                if (recPosX < 800 && recPosX > 0)
                {
                    API_VERTEX2F(recPosX, 7 * 28);
                    API_VERTEX2F(recPosX + width, 8 * 28 - 1);
                }
                //}
            }
            else if (paramCount == 4)
            {
                // for(uint8_t i = 0; i<8; i++)
                //{
                uint16_t recPosX = 1 + fourParamsSelOffsetX[tracks->selectedParam] +
                                   (tracksSpace * (tracks->actualTrack - firstVisibleTrack));
                if (recPosX < 800 && recPosX > 0)
                {
                    API_VERTEX2F(recPosX, 7 * 28);
                    API_VERTEX2F(recPosX + fourParamsSelWidth[tracks->selectedParam], 8 * 28 - 1);
                }
                //}
            }
            API_END();
        }

        API_SAVE_CONTEXT();

        // API_LINE_WIDTH(16);
        API_SCISSOR_XY(phx1 - 11, phy1);
        API_SCISSOR_SIZE(11, phy2 - phy1);
        API_CMD_GRADIENT(phx1 - 11, 0, colors[1], phx1, 0, 0x0);

        API_RESTORE_CONTEXT();

        API_SAVE_CONTEXT();

        // API_LINE_WIDTH(16);
        API_SCISSOR_XY(phx2, phy1);
        API_SCISSOR_SIZE(11, phy2 - phy1);
        API_CMD_GRADIENT(phx2, 0, 0x0, phx2 + 11, 0, colors[1]);

        API_RESTORE_CONTEXT();
    }

    API_END();
}

void cArrangement::calculateSelection()
{
    if (tracks->selectState == 2)
    {
        selectActive = 1;

        if (tracks->selectStartTrack > tracks->selectEndTrack)
        {
            select1_y = tracks->selectEndTrack;
            select2_y = tracks->selectStartTrack;
        }
        else
        {
            select1_y = tracks->selectStartTrack;
            select2_y = tracks->selectEndTrack;
        }

        if (tracks->selectStartStep > tracks->selectEndStep)
        {
            select1_x = tracks->selectEndStep;
            select2_x = tracks->selectStartStep;
        }
        else
        {
            select1_x = tracks->selectStartStep;
            select2_x = tracks->selectEndStep;
        }

        select1_y = select1_y - firstVisibleTrack;
        select1_y = stepsNumbersHight + select1_y * trackHight + 1;

        select2_y = select2_y - firstVisibleTrack;
        select2_y = stepsNumbersHight + select2_y * trackHight + trackHight - 2;

        select1_x = select1_x - tracks->actualStep + visibleStepsCount / 2;
        select1_x = trackHeadWidth + select1_x * stepWidth + 1;

        select2_x = select2_x - tracks->actualStep + visibleStepsCount / 2;
        select2_x = trackHeadWidth + select2_x * stepWidth + stepWidth - 2;
    }
    else
    {
        selectActive = 0;
    }
}
//-------------------------------------------------------------------------------------
// ZAZNACZENIE ACTUAL
void cArrangement::selection()
{
    // API_BLEND_FUNC(SRC_ALPHA, ZERO);

    uint32_t selection_color = colors[10];
    if (tracks->selectColor == 1)
        selection_color = colors[17];
    else if (tracks->selectColor == 2)
        selection_color = colors[18];

    //--------------------
    // ZAZNACZENIE

    if (tracks->selectState == 2)
    {
        // pole pod edytowanym parametrem
        API_COLOR(selection_color);
        API_LINE_WIDTH(8);
        API_BEGIN(RECTS);

        if (paramCount == 1 && ((1 << tracks->selectedParam) &
                                displayMode)) // jesli jest na ekranie zaznaczony parametr
        {
            for (uint8_t i = 0; i < 8; i++)
            {
                uint16_t SelPosX =
                  select1_x + oneParamsSelOffset[tracks->selectedParam] + (tracksSpace * i);
                if (SelPosX < select2_x && SelPosX > select1_x)
                {
                    API_VERTEX2F(SelPosX, select1_y + 1);
                    API_VERTEX2F(SelPosX + oneParamsSelWidth[tracks->selectedParam], select2_y - 1);
                }
            }
        }
        else if (paramCount == 2 && ((1 << tracks->selectedParam) & displayMode))
        {
            uint8_t param = whichParamFromTwoSelected();
            uint8_t offset = param == 0 ? twoParamsSel1Offset[tracks->selectedParam]
                                        : twoParamsSel2Offset[tracks->selectedParam];
            uint8_t width = param == 0 ? twoParamsSel1Width[tracks->selectedParam]
                                       : twoParamsSel2Width[tracks->selectedParam];

            for (uint8_t i = 0; i < 8; i++)
            {
                uint16_t SelPosX = select1_x + offset + (tracksSpace * i);
                if (SelPosX < select2_x && SelPosX > select1_x)
                {
                    API_VERTEX2F(SelPosX, select1_y + 1);
                    API_VERTEX2F(SelPosX + width, select2_y - 1);
                }
            }
        }
        else if (paramCount == 4)
        {
            for (uint8_t i = 0; i < 8; i++)
            {
                uint16_t SelPosX =
                  select1_x + fourParamsSelOffsetX[tracks->selectedParam] + (stepWidth * i);
                if (SelPosX < select2_x && SelPosX > select1_x)
                {
                    API_VERTEX2F(SelPosX, select1_y + 1);
                    API_VERTEX2F(SelPosX + fourParamsSelWidth[tracks->selectedParam],
                                 select2_y - 1);
                }
            }
        }
        API_END();

        // ramka zaznaczenia
        API_BEGIN(LINES);

        if (select1_y >= stepsNumbersHight) // gorna
        {
            API_VERTEX2F(select1_x, select1_y);
            API_VERTEX2F(select2_x, select1_y);
        }
        else
        {
            select1_y = stepsNumbersHight;
        }

        if (select2_y <= stepsNumbersHight + trackHight * 8) // dolna
        {
            API_VERTEX2F(select2_x, select2_y);
            API_VERTEX2F(select1_x, select2_y);
        }
        else
        {
            select2_y = stepsNumbersHight + trackHight * 8;
        }

        if (select2_x <= trackHeadWidth + stepWidth * visibleStepsCount) // prawa
        {
            API_VERTEX2F(select2_x, select1_y);
            API_VERTEX2F(select2_x, select2_y);
        }

        if (select1_x >= trackHeadWidth) // lewa
        {
            API_VERTEX2F(select1_x, select2_y);
            API_VERTEX2F(select1_x, select1_y);
        }

        API_END();
    }

    // aktualnie modyfikowany step
    if (tracks->selectState && tracks->actualTrack >= firstVisibleTrack &&
        tracks->actualTrack <= firstVisibleTrack + visibleTracksCount)
    {
        uint16_t select_x = trackHeadWidth + (visibleStepsCount / 2) * stepWidth + 2;
        uint16_t select1_y =
          stepsNumbersHight + (tracks->actualTrack - firstVisibleTrack) * trackHight + 2;
        uint16_t select2_y = select1_y + trackHight - 4;
        uint16_t select_w = stepWidth - 4;
        uint16_t rect_select_x = 0;
        uint16_t rect_select_width = 0;
        uint16_t rect_select_y = 0;
        uint16_t rect_select_hight = 0;

        if (paramCount == 1)
        {
            // rect_select_x = select_x+oneParamsSelOffset[tracks->selectedParam];
            // rect_select_width = oneParamsSelWidth[tracks->selectedParam];
        }
        else if (paramCount == 2)
        {
            // uint8_t param = whichParamFromTwoSelected();
            // rect_select_x = select_x + (param==0 ? twoParamsSel1Offset[tracks->selectedParam] :
            // twoParamsSel2Offset[tracks->selectedParam]); rect_select_width =        (param==0 ?
            // twoParamsSel1Width[tracks->selectedParam]  :
            // twoParamsSel2Width[tracks->selectedParam]);
        }
        else if (paramCount == 4)
        {
            rect_select_x = select_x + fourParamsSelOffsetX[tracks->selectedParam];
            rect_select_width = fourParamsSelWidth[tracks->selectedParam];
            rect_select_y = select1_y + fourParamsSelOffsetY[tracks->selectedParam];
            rect_select_hight = fourParamsSelHight[tracks->selectedParam];
        }

        API_COLOR(selection_color);
        API_LINE_WIDTH(8);
        API_BEGIN(RECTS);
        API_VERTEX2F(rect_select_x, rect_select_y);
        API_VERTEX2F(rect_select_x + rect_select_width, rect_select_y + rect_select_hight);
        API_END();

        // ramka tylko w okolo aktualnego stepa
        if (tracks->selectState == 1)
        {

            API_BEGIN(LINE_STRIP);

            /*
                    API_LINE_WIDTH(16);
                    API_VERTEX2F(rect_select_x-2, select1_y);
                    API_VERTEX2F(rect_select_x+rect_select_width, select1_y);
                    API_VERTEX2F(rect_select_x+rect_select_width, select2_y);
                    API_VERTEX2F(rect_select_x-2, select2_y);
                    API_VERTEX2F(rect_select_x-2, select1_y);
            */

            API_LINE_WIDTH(8);
            API_VERTEX2F(select_x, select1_y);
            API_VERTEX2F(select_x + select_w, select1_y);
            API_VERTEX2F(select_x + select_w, select2_y);
            API_VERTEX2F(select_x, select2_y);
            API_VERTEX2F(select_x, select1_y);

            API_END();
        }
    }
}

uint8_t cArrangement::whichParamFromTwoSelected()
{
    if (displayMode & 1)
    {
        if (tracks->selectedParam == 0)
            return 0;
        else
            return 1;
    }
    else if (displayMode & 2)
    {
        if (tracks->selectedParam == 1)
            return 0;
        else
            return 1;
    }
    else if (displayMode & 4)
    {
        if (tracks->selectedParam == 2)
            return 0;
        else
            return 1;
    }

    return 1;
}

//-------------------------------------------------------------------------------------
// step number
void cArrangement::stepNumbers()
{
    int16_t step = tracks->actualStep - visibleStepsCount / 2 + 1;
    API_SAVE_CONTEXT();

    API_COLOR(colors[16]);
    API_BITMAP_HANDLE(fonts[4].handle);
    API_BEGIN(BITMAPS);

    bool hexMode = (this->style & controlStyleHexNumbers) ? true : false;

    for (uint16_t i = 0; i < visibleStepsCount; i++)
    {
        if (step < 1 || step > tracks->patternLength)
        {
            step++;
            continue;
        }

        if (hexMode)
        {
            Number2HexBitmaps(trackHeadWidth + 12 + i * stepWidth, 6, 7, 12, step - 1);
        }
        else
        {
            Number2Bitmaps(trackHeadWidth + 12 + i * stepWidth, 6, 7, 12, step);
        }
        step++;
    }
    API_END();

    API_RESTORE_CONTEXT();
}

//-------------------------------------------------------------------------------------
// tracks names
void cArrangement::tracksNames()
{
    if (tracks->tracksNames == nullptr)
        return;

    int16_t x, y, w, h;
    w = trackHeadWidth - 2;
    h = trackHight / 2 - 7;

    x = 0;

    API_LINE_WIDTH(32);
    API_BEGIN(RECTS);

    for (uint8_t i = 0; i < visibleTracksCount; i++)
    {
        if (tracks->inactive[firstVisibleTrack + i])
            API_COLOR(colors[12]);
        else
            API_COLOR(colors[14]);

        // xxx w = strlen(tracks->tracksNames+((tracks->firstVisibleTrack+i)*21)) *
        // fonts[3].width+6; if(w > tracksSpace-2) w = tracksSpace-2;

        // x = rightOffset+(i*tracksSpace)+1;
        y = stepsNumbersHight + trackHight * i + 2;

        API_VERTEX2F(x, y);
        API_VERTEX2F(x + w, y + h);
    }

    API_END();

    // numer/nazwa
    y = 0;
    x = 6;
    actualColor = lastFontColor = colors[0];

    API_SAVE_CONTEXT();
    API_COLOR(actualColor);
    lastFontHandle = -1;
    API_BEGIN(BITMAPS);

    for (uint8_t i = 0; i < visibleTracksCount; i++)
    {
        y = stepsNumbersHight + (i * trackHight) + 10;
        char* trackName = tracks->tracksNames + ((firstVisibleTrack + i) * 21);
        String2Bitmaps(x, y, &fonts[3], trackName, 7, trackHeadWidth - fonts[3].width);
    }

    API_END();
    API_RESTORE_CONTEXT();
}

//===============================================================================================================================
void cArrangement::notes()
{
    API_SAVE_CONTEXT();
    API_COLOR(colors[2]);
    actualColor = lastFontColor = colors[2];
    // API_BITMAP_HANDLE(fonts[1].handle);
    lastFontHandle = -1;
    API_BEGIN(BITMAPS);

    uint8_t offset_x = trackHeadWidth + fourParamsOffset[0];

    // if(paramCount == 1) 		offset_x = 27 + ((tracksSpace/2)-(param_length[1]*12)/2);
    if (paramCount == 1)
        offset_x = trackHeadWidth + oneParamsOffset[0];
    else if (paramCount == 2)
        offset_x = trackHeadWidth + twoParamsOffsetFirst[0];

    int8_t mark = -1;
    if ((tracks->selectedParam == 0 /*|| tracks->selectedParam == 4*/) && tracks->selectState)
    {
        mark = tracks->actualTrack - firstVisibleTrack;
    }

    for (uint16_t i = 0; i < visibleStepsCount; i++)
    {
        for (uint16_t j = 0; j < visibleTracksCount; j++)
        {
            int16_t param_x = offset_x + i * stepWidth;
            int16_t param_y = stepsNumbersHight + 12 + j * trackHight;

            char* text = tracks->track[firstVisibleTrack + j].row[i + stepsDataStructOffset].note;

            if (selectActive && mark >= 0 && param_x > select1_x && param_x < select2_x &&
                param_y > select1_y && param_y < select2_y)
                actualColor = colors[14];
            else if (i == visibleStepsCount / 2 && j == mark)
                actualColor = colors[14];
            else
            {
                if (tracks->inactive[firstVisibleTrack + j])
                    actualColor = colors[12];
                else if (*text == '-')
                    actualColor = colors[6];
                else
                    actualColor = colors[2];
            }

            String2Bitmaps(param_x, param_y, &fonts[1], text, param_length[0]);
        }
    }
    API_END();
    API_RESTORE_CONTEXT();
}

//===============================================================================================================================
void cArrangement::instruments()
{
    API_SAVE_CONTEXT();
    API_COLOR(colors[3]);
    actualColor = lastFontColor = colors[3];
    // API_BITMAP_HANDLE(fonts[1].handle);
    lastFontHandle = -1;
    API_BEGIN(BITMAPS);

    uint8_t offset_x = trackHeadWidth + fourParamsOffset[1]; // 59
    if (paramCount == 1)
        offset_x = trackHeadWidth + oneParamsOffset[1];
    else if (paramCount == 2)
        offset_x = (displayMode & 1) ? trackHeadWidth + twoParamsOffsetSecond[1]
                                     : trackHeadWidth + twoParamsOffsetFirst[1];

    int8_t mark = -1;
    if ((tracks->selectedParam == 1 /*|| tracks->selectedParam == 4*/) && tracks->selectState)
    {
        mark = tracks->actualTrack - firstVisibleTrack;
    }

    for (uint16_t i = 0; i < visibleStepsCount; i++)
    {
        for (uint16_t j = 0; j < visibleTracksCount; j++)
        {
            uint8_t track = firstVisibleTrack + j;
            int16_t param_x = offset_x + i * stepWidth;
            int16_t param_y = stepsNumbersHight + 12 + j * trackHight;
            // uint8_t midi_channel  =  tracks->track[track].row[j].instr[3];
            char* text = tracks->track[track].row[i + stepsDataStructOffset].instr;
            if (selectActive && mark >= 0 && param_x > select1_x && param_x < select2_x &&
                param_y > select1_y && param_y < select2_y)
                actualColor = colors[14];
            else if (i == visibleStepsCount / 2 && j == mark)
                actualColor = colors[14];
            else
            {
                if (tracks->inactive[track])
                    actualColor = colors[12];
                else if (*text == '-')
                    actualColor = colors[7];
                else
                    actualColor = colors[3];
            }

            if (tracks->track[track].row[j].instr[2] > 0)
                String2Bitmaps(param_x - 4, param_y, &fonts[0], text, param_length[1] + 1);
            else
                String2Bitmaps(param_x, param_y, &fonts[1], text, param_length[1]);
        }
    }
    API_END();
    API_RESTORE_CONTEXT();
}

//===============================================================================================================================
void cArrangement::fxes1()
{
    API_SAVE_CONTEXT();
    API_COLOR(colors[4]);
    actualColor = lastFontColor = colors[4];
    lastFontHandle = -1;
    // API_BITMAP_HANDLE(fonts[1].handle);
    API_BEGIN(BITMAPS);

    uint8_t offset_x = trackHeadWidth + fourParamsOffset[2]; // 99
    if (paramCount == 1)
        offset_x = trackHeadWidth + oneParamsOffset[2];
    else if (paramCount == 2)
        offset_x = (displayMode & 8) ? trackHeadWidth + twoParamsOffsetFirst[2]
                                     : trackHeadWidth + twoParamsOffsetSecond[2];

    int8_t mark = -1;
    if ((tracks->selectedParam == 2 /*|| tracks->selectedParam == 4*/) && tracks->selectState)
    {
        mark = tracks->actualTrack - firstVisibleTrack;
    }

    for (uint16_t i = 0; i < visibleStepsCount; i++)
    {
        for (uint16_t j = 0; j < visibleTracksCount; j++)
        {
            int16_t param_x = offset_x + i * stepWidth;
            int16_t param_y = stepsNumbersHight + 37 + j * trackHight;
            char* text =
              &tracks->track[firstVisibleTrack + j].row[i + stepsDataStructOffset].fx[0][0];

            if (selectActive && mark >= 0 && param_x > select1_x && param_x < select2_x &&
                param_y > select1_y && param_y < select2_y)
                actualColor = colors[14];
            else if (i == visibleStepsCount / 2 && j == mark)
                actualColor = colors[14];
            else
            {
                if (tracks->inactive[firstVisibleTrack + j])
                    actualColor = colors[12];
                else if (*text == '-')
                    actualColor = colors[8];
                else if (text[1] == '-' && text[2] == '-')
                    actualColor = dimFx1Color;
                else
                    actualColor = colors[4];
            }
            String2Bitmaps(param_x, param_y, &fonts[1], text, param_length[3]);
        }
    }
    API_END();
    API_RESTORE_CONTEXT();
}

void cArrangement::fxes2()
{
    API_SAVE_CONTEXT();
    API_COLOR(colors[5]);
    actualColor = lastFontColor = colors[5];
    lastFontHandle = -1;
    // API_BITMAP_HANDLE(fonts[1].handle);
    API_BEGIN(BITMAPS);

    uint8_t offset_x = trackHeadWidth + fourParamsOffset[3];
    if (paramCount == 1)
        offset_x = trackHeadWidth + oneParamsOffset[3];
    else if (paramCount == 2)
        offset_x = trackHeadWidth + twoParamsOffsetSecond[3];

    int8_t mark = -1;
    if ((tracks->selectedParam == 3 /*|| tracks->selectedParam == 4*/) && tracks->selectState)
    {
        mark = tracks->actualTrack - firstVisibleTrack;
    }

    for (uint16_t i = 0; i < visibleStepsCount; i++)
    {
        for (uint16_t j = 0; j < visibleTracksCount; j++)
        {
            int16_t param_x = offset_x + i * stepWidth;
            int16_t param_y = stepsNumbersHight + 37 + j * trackHight;
            char* text =
              &tracks->track[firstVisibleTrack + j].row[i + stepsDataStructOffset].fx[1][0];

            if (selectActive && mark >= 0 && param_x > select1_x && param_x < select2_x &&
                param_y > select1_y && param_y < select2_y)
                actualColor = colors[14];
            else if (i == visibleStepsCount / 2 && j == mark)
                actualColor = colors[14];
            else
            {
                if (tracks->inactive[firstVisibleTrack + j])
                    actualColor = colors[12];
                else if (*text == '-')
                    actualColor = colors[9];
                else if (text[1] == '-' && text[2] == '-')
                    actualColor = dimFx2Color;
                else
                    actualColor = colors[5];
            }

            String2Bitmaps(param_x, param_y, &fonts[1], text, param_length[3]);
        }
    }
    API_END();
    API_RESTORE_CONTEXT();
}
//===============================================================================================================================
//===============================================================================================================================
//===============================================================================================================================
static inline void draw_char(uint16_t x, uint16_t y, uint8_t charr)
{
    if (x > 511 || y > 511)
    {
        API_CELL(charr);
        API_VERTEX2F(x, y);
    }
    else
    {
        API_VERTEX2II(x, y, fonts[4].handle, charr);
    }
}

/*
Note: this is not cDisplayControl::string2Bitmaps

This one has support for multiple fonts and optimizations for switching bitmap handles only when
necessary.
*/
void cArrangement::String2Bitmaps(int16_t x,
                                  int16_t y,
                                  const strFont* font,
                                  char* string,
                                  int8_t length,
                                  int16_t w_limit)
{
    if (actualColor != lastFontColor)
    {
        API_COLOR(actualColor);
        lastFontColor = actualColor;
    }
    if (lastFontHandle != font->handle)
    {
        API_BITMAP_HANDLE(font->handle);
        lastFontHandle = font->handle;
    }

    w_limit += x;
    y = y - font->height / 2;
    uint8_t strPtr = 0;

    x += fonts[1].width - font->width;

    unsigned char ch;
    for (uint8_t i = 0; i < length; i++)
    {
        ch = string[strPtr++];
        if (ch == 0 || x > w_limit)
            return;

        if (x >= 0 && x <= 799 && y >= 0 && y <= 479)
        {
            if ((x > 511 || y > 511) && font->data[ch])
            {
                API_CELL(ch);
                API_VERTEX2F(x, y);
            }
            else if (font->data[ch])
            {
                API_VERTEX2II(x, y, font->handle, ch);
            }
        }
        x += font->width;
    }
}

static void Number2HexBitmaps(int16_t x, int16_t y, uint8_t font_x, uint8_t font_y, int16_t number)
{
    y = y - font_y / 2;
    uint8_t digit, ascii_offset;

    if (number < 0)
    {
        number *= -1;
        draw_char(x, y, 45);
        x += font_x;
    }

    // note: only prints the last two hex digits of the number (00-FF)
    number = number % 256;
    digit = number / 16;
    number = number % 16;

    // Xx
    ascii_offset = digit > 9 ? 55 : 48;
    draw_char(x, y, digit + ascii_offset);
    x += font_x;

    // xX
    ascii_offset = number > 9 ? 55 : 48;
    draw_char(x, y, number + ascii_offset);
}

static void Number2Bitmaps(int16_t x, int16_t y, uint8_t font_x, uint8_t font_y, int16_t number)
{
    y = y - font_y / 2;
    uint8_t digit, flag = 0;

    if (number < 0)
    {
        number = number * (-1);
        draw_char(x, y, 45);
        x += font_x;
    }

    //	digit =  number/10000;
    //	number = number%10000;
    //	if(digit > 0) {draw_char(x,y,digit+48); x+=font_x; flag = 1;}

    //	digit =  number/1000;
    //	number = number%1000;
    //	if(digit > 0 || flag) {draw_char(x,y,digit+48); x+=font_x; flag = 1;}

    digit = number / 100;
    number = number % 100;
    if (digit > 0 || flag)
    {
        draw_char(x, y, digit + 48);
        x += font_x - 1;
        flag = 1;
    } // skrocony odstep po 1

    digit = number / 10;
    number = number % 10;
    if (digit > 0 || flag)
    {
        draw_char(x, y, digit + 48);
        x += font_x;
        flag = 1;
    }

    draw_char(x, y, number + 48);
}

#endif
