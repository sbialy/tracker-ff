#include "core/interface.h"
#include "mtSleep.h"
#include <stdint.h>

//-----------------------------------------------------------------
//-------------------------|   POTS   |----------------------------
//-----------------------------------------------------------------
void onPotChange(uint8_t n, int16_t value)
{
    mtInterface.potChange(n, value);
}

//-----------------------------------------------------------------
//------------------------|   BUTTONS   |--------------------------
//-----------------------------------------------------------------
// 0 - release
// 1 - press
// 2 - hold
// 3 - double click
void onButtonPush(uint8_t n)
{
    mtInterface.buttonChange(n, buttonPress);
}

void onButtonRelease(uint8_t n)
{
    mtInterface.buttonChange(n, buttonRelease);
}

void onButtonHold(uint8_t n)
{
    mtInterface.buttonChange(n, buttonHold);
}

//-----------------------------------------------------------------
//----------------------|   POWER BUTTON   |-----------------------
//-----------------------------------------------------------------
void onPowerButtonChange(int value)
{
    if (mtInterface.ip->isPowerButtonBlocked())
    {
        return;
    }

    if (!mtInterface.powerButtonChange(value)) // jesli nie ma ustawionej akcji
    {
        mtInterface.handlePowerButtonAction(value);
    }
}

//-----------------------------------------------------------------
//------------------------|   PADS   |-----------------------------
//-----------------------------------------------------------------
void onPadPush(uint8_t n)
{
    mtInterface.padPressed(n, 0, 0, 0);
}

void onPadRelease(uint8_t n)
{
    mtInterface.padReleased(n);
}

void onPadHold(uint8_t n)
{
    mtInterface.padHold(n);
}

void onSDCardSlotChange(bool state)
{
    mtInterface.handleCardSlotAction(state);
}

#ifdef ENABLE_DEV_CONSOLE
// For the platform's development console (USB serial, develop mode only).
bool onDevStorageMode(bool enable)
{
    return mtInterface.setFileTransferMode(enable);
}
#endif

void onBatteryCharging()
{
    // mtInterface.showChargingBatteryPopup();
}

void onBatteryEmpty()
{
    mtInterface.showEmptyBatteryPopup();
    lowPower.goLowPower();
}

void onBatteryLow()
{
    mtInterface.showLowBatteryPopup();
}
