#include "mtPadsBacklight.h"
#include "Platform.h"

cMtPadsBacklight padsBacklight;

void cMtPadsBacklight::init(uint16_t blink, Leds* leds)
{
    blinkConstrain = blink;
    blinkTimer = 0;
    cMtPadsBacklight::leds = leds;
}

void cMtPadsBacklight::update()
{
    if (blinkCounter)
    {
        if (blinkTimer > blinkConstrain)
        {
            blinkTimer = 0;

            if (toggler == 1)
                toggler = 0;
            else if (toggler == 0)
                toggler = 1;

            for (uint8_t i = 0; i < 48; i++)
            {
                if (blinkLayer[i])
                    setFrontLayer(toggler, blinkLayer[i], i);
            }
        }
    }
}

void cMtPadsBacklight::setBackLayer(uint8_t state, uint8_t gamma_pwm, uint8_t n)
{
    if (state)
    {
        leds->setLed(n, state, (gamma_pwm > frontLayer[n] ? gamma_pwm : frontLayer[n]));
        backLayer[n] = gamma_pwm;
    }
    else
    {
        if (frontLayer[n] != 0)
            leds->setLed(n, 1, frontLayer[n]);
        else
            leds->setLed(n, state, gamma_pwm);

        backLayer[n] = 0;
    }
}

void cMtPadsBacklight::setFrontLayer(uint8_t state, uint8_t gamma_pwm, uint8_t n)
{
    if (state)
    {
        leds->setLed(n, state, (gamma_pwm > backLayer[n] ? gamma_pwm : backLayer[n]));
        frontLayer[n] = gamma_pwm;
    }
    else
    {
        if (backLayer[n] != 0)
            leds->setLed(n, 1, backLayer[n]);
        else
            leds->setLed(n, state, gamma_pwm);

        frontLayer[n] = 0;
    }
}

void cMtPadsBacklight::startBlink(uint8_t gamma_pwm, uint8_t n)
{
    blinkLayer[n] = gamma_pwm;
    blinkCounter++;
}

void cMtPadsBacklight::stopBlink(uint8_t n)
{
    if (blinkLayer[n] != 0)
    {
        blinkLayer[n] = 0;
        setFrontLayer(0, 0, n);
        if (blinkCounter > 0)
            blinkCounter--;
    }
}

void cMtPadsBacklight::clearGlobalPads(uint8_t front, uint8_t back, uint8_t blink)
{
    if (front && back)
    {
        for (uint8_t i = 0; i < 48; i++)
        {
            if (frontLayer[i] != 0 || backLayer[i] != 0)
            {
                frontLayer[i] = 0;
                backLayer[i] = 0;
                leds->setLed(i, 0, 0);
            }
        }
    }
    else if (front)
    {
        for (uint8_t i = 0; i < 48; i++)
        {
            if (frontLayer[i] != 0)
            {
                setFrontLayer(0, 0, i);
            }
        }
    }
    else if (back)
    {
        for (uint8_t i = 0; i < 48; i++)
        {
            if (backLayer[i] != 0)
            {
                setBackLayer(0, 0, i);
            }
        }
    }
    if (blink)
    {
        for (uint8_t i = 0; i < 48; i++)
        {
            stopBlink(i);
        }
    }
}
