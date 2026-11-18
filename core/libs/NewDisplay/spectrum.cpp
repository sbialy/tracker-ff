#include "FT812.h"

#include "display.h"
#include "polyassert.h"
#include "spectrumControl.h"

cSpectrum::cSpectrum(strControlProperties* properties)
  : cDisplayControl(controlIdSpectrum, 1)
{
    colorsCount = 2;
    colors = display.getTheme().waveformColors;

    refreshStep = 0;

    if (properties == nullptr)
    {
        posX = 0;
        posY = 0;
        width = 0;
        height = 0;
        spectrum = nullptr;
        this->style = 0;
        return;
    }

    colors = (properties->colors == nullptr) ? colors : properties->colors;

    spectrum = (strTrackerSpectrum*)properties->data;
    posX = properties->x;
    posY = properties->y;
    width = properties->w;
    height = properties->h;

    setStyle(properties->style);
}

cSpectrum::~cSpectrum()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cSpectrum::setStyle(uint32_t style)
{
    this->style = style;
}

void cSpectrum::setText(const char* /*text*/)
{
}

void cSpectrum::setValue(int value)
{
    this->value = value;
}

void cSpectrum::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cSpectrum::setData(void* data)
{
    spectrum = (strTrackerSpectrum*)data;

    width = spectrum->width;
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cSpectrum::update()
{
    API_COLOR(colors[0]);
    API_LINE_WIDTH(16);

    // uint16_t half = height/2;
    uint16_t center = posY + height / 2;

    float yNormalizationCoeff = (float)height / (float)(MAX_16BIT / 300);

    if (spectrum == nullptr)
    {
        API_BEGIN(LINES);
        API_VERTEX2F(posX, center);
        API_VERTEX2F(posX + width - 1, center);
    }
    else if (spectrum->spectrumType == 1)
    {
        // srodek
        API_COLOR(colors[1]);
        API_LINE_WIDTH(8);
        API_BEGIN(LINES);
        API_VERTEX2F(posX, center);
        API_VERTEX2F(posX + width - 1, center);
        API_END();

        // jedna lamana linia
        API_COLOR(colors[0]);
        API_LINE_WIDTH(8);
        API_BEGIN(LINE_STRIP);

        int16_t sample_y = 0;
        int16_t next_sample_y = 0;

        int pointsLimit = 0;

        if (spectrum->pointsLimit == strTrackerSpectrum::PointsLimit::k600)
        {
            pointsLimit = 600;
        }
        else if (spectrum->pointsLimit == strTrackerSpectrum::PointsLimit::k300)
        {
            pointsLimit = 300;
        }
        else if (spectrum->pointsLimit == strTrackerSpectrum::PointsLimit::k200)
        {
            pointsLimit = 200;
        }

        float limitToWidthCoeff = (float)width / (float)pointsLimit;
        float idx = 0.0f;

        for (uint16_t i = 0; i < pointsLimit; i++)
        {
            int sampleIdx = (int)idx;

            if (spectrum->upperData[sampleIdx] > 0)
            {
                sample_y = center - spectrum->upperData[sampleIdx] * yNormalizationCoeff;
            }
            else if (spectrum->lowerData[sampleIdx] < 0)
            {
                sample_y = center - spectrum->lowerData[sampleIdx] * yNormalizationCoeff;
            }
            else
            {
                sample_y = center;
            }

            idx += limitToWidthCoeff;

            int nextSampleIdx = (int)idx;

            if (nextSampleIdx < width - 1 && sampleIdx > 0)
            {
                if (spectrum->upperData[nextSampleIdx] > 0)
                {
                    next_sample_y =
                      center - spectrum->upperData[nextSampleIdx] * yNormalizationCoeff;
                }
                else if (spectrum->lowerData[nextSampleIdx] < 0)
                {
                    next_sample_y =
                      center - spectrum->lowerData[nextSampleIdx] * yNormalizationCoeff;
                }
                else
                {
                    next_sample_y = center;
                }
            }
            else
            {
                next_sample_y = sample_y + 1;
            }

            if (next_sample_y == sample_y)
            {
                continue; // omija powtorzenia takiej samej wartosci probki
            }

            assert(sampleIdx < width);
            API_VERTEX2F(posX + sampleIdx, sample_y);
        }
    }
    else if (spectrum->spectrumType == 0)
    {
        API_BEGIN(LINES);

        int pointsLimit = 0;
        if (spectrum->pointsLimit == strTrackerSpectrum::PointsLimit::k600)
        {
            pointsLimit = 600;
        }
        else if (spectrum->pointsLimit == strTrackerSpectrum::PointsLimit::k300)
        {
            pointsLimit = 300;
        }
        else if (spectrum->pointsLimit == strTrackerSpectrum::PointsLimit::k200)
        {
            pointsLimit = 200;
        }

        float mult = (float)width / (float)pointsLimit;
        uint16_t length = width / mult;

        for (uint16_t i = 0; i < length; i++)
        {
            int idx = i * mult;
            assert(idx < width);
            API_VERTEX2F(posX + idx, center - spectrum->upperData[idx] * yNormalizationCoeff);
            API_VERTEX2F(posX + idx, center - spectrum->lowerData[idx] * yNormalizationCoeff);
        }
    }
    else if (spectrum->spectrumType == 3)
    {
        // elementsState.waitSpinner = 1;
    }
    API_END();
}

//------------------------------------------------------------------------------------------
void cSpectrum::update2()
{
    if (spectrum == nullptr)
    {
    }
    else if (spectrum->spectrumType == 1)
    {
    }
    else if (spectrum->spectrumType == 0 && width > 200)
    {
        API_BEGIN(LINE_STRIP);

        uint16_t center = posY + height / 2;

        uint8_t odd = 0;

        uint16_t width_left = (width - 200 > 200) ? 200 : width - 200;

        if (width_left > 0)
        {
            for (uint16_t i = 0; i < width_left; i++)
            {
                if (odd)
                    API_VERTEX2II(posX + i + 199, center - spectrum->upperData[i + 199], 0, 0);
                else
                    API_VERTEX2II(posX + i + 199, center - spectrum->lowerData[i + 199], 0, 0);
                odd = !odd;
            }
        }
    }
    API_END();
}

//------------------------------------------------------------------------------------------
void cSpectrum::update3()
{
    if (spectrum == nullptr)
    {
    }
    else if (spectrum->spectrumType == 1)
    {
    }
    else if (spectrum->spectrumType == 0 && width > 400)
    {
        API_BEGIN(LINE_STRIP);

        uint16_t center = posY + height / 2;

        uint8_t odd = 0;

        uint16_t width_left = (width - 400 > 200) ? 200 : width - 400;

        if (width_left > 0)
        {
            for (uint16_t i = 0; i < width_left; i++)
            {
                if (odd)
                    API_VERTEX2F(posX + i + 399, center - spectrum->upperData[i + 399]);
                else
                    API_VERTEX2F(posX + i + 399, center - spectrum->lowerData[i + 399]);
                odd = !odd;
            }
        }
    }
    API_END();
}
