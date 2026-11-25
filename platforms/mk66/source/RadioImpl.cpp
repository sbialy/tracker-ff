#include "RadioImpl.h"

void RadioImpl::init(Pins* pins)
{
    pinMode(pins->lineInSourceSelect, OUTPUT);
    digitalWrite(pins->lineInSourceSelect, LOW);

    RadioImpl::lineInSourceSelectPin = pins->lineInSourceSelect;

    radioDriver.init(pins->reset, pins->i2sData, pins->i2sClock, pins->interrupt);
}

void RadioImpl::enableLineInput(bool enabled)
{
    if (enabled)
    {
        digitalWrite(lineInSourceSelectPin, LOW);
    }
    else
    {
        digitalWrite(lineInSourceSelectPin, HIGH);
    }
}

void RadioImpl::enable(bool enabled)
{
    if (enabled)
    {
        radioDriver.powerOn();
    }
    else
    {
        radioDriver.powerOff();
    }
}

bool RadioImpl::setFrequency(float freq)
{
    return radioDriver.setFrequency(freq);
}

void RadioImpl::setSeekCallback(void (*seekCallback)(void))
{
    if (seekCallback == nullptr)
    {
        radioDriver.resetSeekCallback();
        return;
    }

    radioDriver.setSeekCallback(seekCallback);
}

void RadioImpl::seek(SeekDirection direction)
{
    if (direction == SeekDirection::kDown)
    {
        radioDriver.seekDown();
    }
    else if (direction == SeekDirection::kUp)
    {
        radioDriver.seekUp();
    }
}

void RadioImpl::updateSeek()
{
    radioDriver.stateMachineSeek();
}

bool RadioImpl::updateRDS()
{
    return radioDriver.update_RDS();
}

float RadioImpl::getFrequency()
{
    return radioDriver.getFrequency();
}

float RadioImpl::getBaseFrequency()
{
    return radioDriver.getRadioBottomBase();
}

float RadioImpl::getSpacing()
{
    return radioDriver.getRadioSpacing();
}

bool RadioImpl::isinitialized()
{
    return (bool)radioDriver.getInitializationStatus();
}

void RadioImpl::clearRDS()
{
    radioDriver.clearRDS();
}

void RadioImpl::setRegion(Region region)
{
    radioDriver.setRegion((radio_region_t)region, 1);
}

bool RadioImpl::isRegionChanged()
{
    return radioDriver.hasRegionChanged();
}

char* RadioImpl::getStationName()
{
    return radioDriver.rds_data.nazwaStacji;
}

#ifdef FEATURE_RADIO_STEREO
void RadioImpl::setStereo(bool stereo)
{
    radioDriver.setStereo(stereo);
}
#endif
