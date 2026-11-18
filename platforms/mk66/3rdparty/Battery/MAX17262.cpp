#include "MAX17262.h"
#include "core_pins.h"
#include "i2cBusControllerNXP.h"
#include "usb_serial.h"

#define DEBUG_COMMUNICATION 0

bool MAX17262::begin(uint16_t /*designCap*/, uint16_t /*vEmpty*/)
{
#ifdef TRACKER_MINI

    i2cBusControllerNXP2.init();
    i2cBusControllerNXP2.setTimeout(1000);

    int devVersion = getVersion();

    this->designCap = designCap;
    this->vEmpty = vEmpty;

    if (devVersion != 0x4039)
    {
        return false;
    }

    verify(true);

    return true;
#else
    return false;
#endif
}

bool MAX17262::verify(bool extend)
{
    uint16_t status = getStatus();

    bool firstRun = status & 2;

    int tempVEmpty = getVEmpty();
    if (tempVEmpty < 0)
    {
        return false;
    }

    if (firstRun || (extend && (tempVEmpty != vEmpty)))
    {
        int timeout = 0;

        while (readRegister(0x3D) & 1)
        {
            delay(10);
            timeout++;
            if (timeout > 30)
                return false;
        }

        //		int hibCFG = readRegister(0xBA) ; //Store original HibCFG value
        //		writeRegister (0x60 , 0x90) ; // Exit Hibernate Mode step 1
        //		writeRegister (0xBA , 0x0) ; // Exit Hibernate Mode step 2
        //		writeRegister (0x60 , 0x0) ; // Exit Hibernate Mode step 3

        // setDesignCap(designCap); // Write DesignCap
        setVEmpty(vEmpty);          // Write VEmpty
        writeRegister(0x1E, 333);   // Write IchgTerm
        writeRegister(0x0, 0x8000); // Write ModelCFG 4.2V

        designCap *= 2;

        //		delay(100);
        writeAndVerifyRegister(0x05, designCap); // Clear RepCap
        writeRegister(0x18, designCap);          // Write DesignCap
        writeRegister(0x10, designCap);          // Write FullCapRep

        int attempt = 0;
        do
        {
            writeRegister(0x45, designCap / 2); // Write dQAcc
            writeRegister(0x46, 0x0C80);        // Write dPAcc
            delay(10);
            writeRegister(0x23, designCap); // Write FullCapNom
        } while (((readRegister(0x23) != designCap) | (readRegister(0x45) != designCap / 2) |
                  (readRegister(0x46) != 0xC80)) &&
                 attempt++ < 3);

        int Update_Capacity = int(readRegister(0xFF) / 25600.00 * designCap);
        writeRegister(0x0F, Update_Capacity); // Write MixCap
        writeRegister(0x1F, Update_Capacity); // Write AvCap

        //		while(readRegister(0xDB) & 0x8000)
        //		{
        //			delay(10);
        //		}
        //		//do not continue until ModelCFG.Refresh==0
        //		writeRegister (0xBA , hibCFG) ; // Restore Original HibCFG value

        status &= ~2;
        writeRegister(0x00, status); // update status
    }

    return true;
}

int MAX17262::getVoltage()
{
    return (readRegister(0x9) * 1.25) / 16; // = mV
}

int MAX17262::getSOC()
{
    return readRegister(0x6) >> 8;
}

int MAX17262::getCurrent()
{
    return (int16_t)readRegister(0x0A) * 0.15625;
}

bool MAX17262::isCharging()
{
    return ((int16_t)readRegister(0x0A)) > 200;
}

bool MAX17262::isBatteryPresent()
{
    return (getStatus() >> 3) & 1;
}

int MAX17262::getVersion()
{
    return readRegister(0x21);
}

int MAX17262::getVEmpty()
{
    int read = readRegister(0x3A);

    if (read < 0)
        return -1;

    return (((read >> 7) & 0x1FF) * 10);
}

uint16_t MAX17262::getFullCapacity()
{
    return readRegister(0x10) * 0.5;
}

uint16_t MAX17262::getCurrentCapacity()
{
    return readRegister(0x05) * 0.5;
}

uint16_t MAX17262::getTimeToEmpty()
{
    return readRegister(0x11) / 10.667f;
}

uint16_t MAX17262::getTimeToFull()
{
    return readRegister(0x20) / 10.667f;
}

uint16_t MAX17262::getVRecovery()
{
    return ((readRegister(0x3A) & 0x7F) * 40);
}

void MAX17262::setVEmpty(uint16_t mV)
{
    uint16_t reg = ((mV / 10) << 7) | (readRegister(0x3A) & 0x7F);

    writeRegister(0x3A, reg);
}

void MAX17262::setDesignCap(uint16_t mAh)
{
    writeRegister(0x18, mAh / 0.5);
}

int MAX17262::getStatus()
{
    return readRegister(0x0);
}

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
int MAX17262::writeRegister(uint8_t reg, uint16_t value)
{
    i2cBusControllerNXP2.startTransmission(MAX1704X_ADDR);
    i2cBusControllerNXP2.sendByte(reg);
    i2cBusControllerNXP2.sendByte(value & 0xff);
    i2cBusControllerNXP2.sendByte((value >> 8) & 0xff);
    return i2cBusControllerNXP2.finishTransmission();
}

int MAX17262::readRegister(uint8_t reg, uint8_t len)
{
    bool repeatRead = true;
    int errorCnt = 0;

    while (repeatRead)
    {
        i2cBusControllerNXP2.startTransmission(MAX1704X_ADDR);
        i2cBusControllerNXP2.sendByte(reg);
        i2cBusControllerNXP2.setTimeout(5000);
        I2cBusControllerImpl2::OperationResult result = i2cBusControllerNXP2.finishTransmission();
        if (result == I2cBusControllerImpl2::OperationResult::kError)
        {
#if DEBUG_COMMUNICATION
            Serial.printf("\nbattery readRegister i2c write error");
#endif
        }
        else
        {
            i2cBusControllerNXP2.setTimeout(5000);
            result = i2cBusControllerNXP2.request(MAX1704X_ADDR, (uint8_t)len);
            if (result == I2cBusControllerImpl2::OperationResult::kError)
            {
#if DEBUG_COMMUNICATION
                Serial.printf("\nbattery readRegister i2c read error");
#endif
            }
        }

        if (result > 0)
        {
            errorCnt++;

            if (errorCnt % 10 == 0)
            {
#if DEBUG_COMMUNICATION
                Serial.println("reset i2c bus");
#endif
                i2cBusControllerNXP2.unblockBus();
                i2cBusControllerNXP2.init();
            }

            if (errorCnt > 100)
            {
#if DEBUG_COMMUNICATION
                Serial.println("reset i2c bus x10 failed");
#endif
                return -1;
            }
        }
        else
        {
            repeatRead = false;
            break;
        }
    }

    uint16_t data = (uint8_t)i2cBusControllerNXP2.read();
    if (len == 2)
    {
        data |= ((uint8_t)i2cBusControllerNXP2.read() << 8);
    }

    return data;
}

void MAX17262::writeAndVerifyRegister(char RegisterAddress, int RegisterValueToWrite)
{
    int attempt = 0;
    int RegisterValueRead = 0;

    do
    {
        writeRegister(RegisterAddress, RegisterValueToWrite);
        delay(1); // 1ms
        RegisterValueRead = readRegister(RegisterAddress);
    } while (RegisterValueToWrite != RegisterValueRead && attempt++ < 3);
}
