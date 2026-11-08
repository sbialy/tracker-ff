#include "EncoderImpl.h"
#include "Encoder.h"
#include "core_pins.h"

void onPotChange(uint8_t n, int16_t value);

void EncoderImpl::setAcceleration(int acceleration)
{
    encoderDriver.setAcceleration(acceleration);
}

void EncoderImpl::setResolution(int resolution)
{
    encoderDriver.setResolution(resolution);
}

void EncoderImpl::init()
{
    encoderDriver.begin();
    encoderDriver.setResolution(24);
    encoderDriver.setAcceleration(2);
}

void EncoderImpl::update()
{
    if ((millis() - encTimer) > 100)
    {
        encTimer = millis();

        int32_t encValue = encoderDriver.read();
        DEVHOOK_ENCODER_READ(encValue);
        if (encValue != 0)
            onPotChange(0, encValue);
    }
}
