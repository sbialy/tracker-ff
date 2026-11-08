#include "Keys.h"
#include "core_pins.h"
#include "cserial.h"
#include "i2cBusController.h"

const uint16_t Keys::holdTimeStepDiff[Keys::holdTimeStepsCount] = { 0, 100, 190, 235, 255, 270 };

void Keys::begin(uint8_t rows,
                 uint16_t cols,
                 uint8_t config,
                 I2cBusController* i2cBusController,
                 uint8_t* table,
                 void (*resetFunc)(void))
{
    resetTransmissionFunc = resetFunc;
    convertTable = table;
    this->rows = rows;
    this->cols = cols;
    this->config = config;

    this->i2cBusController = i2cBusController;
    setupKeys(rows, cols, config, false);
}

void Keys::setupKeys(uint8_t rows, uint16_t cols, uint8_t config, bool ignoreErrors)
{
    writeByte(KP_GPIO1, rows, ignoreErrors);
    writeByte(KP_GPIO2, (0xff & cols), ignoreErrors);
    writeByte(KP_GPIO3, (0x03 & (cols >> 8)), ignoreErrors);

    currentConfig = config | AI;
    writeByte(CFG, currentConfig, ignoreErrors);
    writeByte(INT_STAT, 0x0F, ignoreErrors); // clear interrupt
}

uint8_t Keys::getRowMask(int rowIdx)
{
    return 1 << rowIdx;
}

uint16_t Keys::getColumnMask(int columnIdx)
{
    return 1 << columnIdx;
}

void Keys::writeByte(uint8_t reg, uint8_t data, bool ignoreError)
{
    if (!i2cBusController->startTransmission(0x34))
        return;
    i2cBusController->sendByte(reg);
    i2cBusController->sendByte(data);
    I2cBusController::OperationResult status = i2cBusController->finishTransmission();

    if (!ignoreError && status == I2cBusController::OperationResult::kError)
    {
        refreshConfig();
    }
}

bool Keys::readByte(uint8_t reg, uint8_t* data, bool ignoreError)
{
    if (!i2cBusController->startTransmission(0x34))
        return false;
    i2cBusController->sendByte(reg);
    I2cBusController::OperationResult status = i2cBusController->finishTransmission();

    if (!ignoreError && status == I2cBusController::OperationResult::kError)
    {
        refreshConfig();
    }

    status = i2cBusController->request(0x34, 0x01);
    if (!ignoreError && status == I2cBusController::OperationResult::kError)
    {
        refreshConfig();
    }

    *data = i2cBusController->read();

    return true;
}

void Keys::enableInterrupt(uint8_t pin, void (*selfCheckFunction)(void))
{
    intPin = pin;
    pinMode(pin, INPUT_PULLUP);
    attachInterrupt(pin, selfCheckFunction, FALLING);
}

void Keys::disableInterrupt()
{
    detachInterrupt(intPin);
}

uint8_t Keys::getKeyEvent()
{
    uint8_t event = 0;
    if (!readByte(KEY_EVENT_A, &event))
        return 0;
    return (event);
}

uint8_t Keys::getKeyEventCount()
{
    uint8_t count = 0;
    if (!readByte(KEY_LCK_EC, &count))
        return 0;
    return (count & 0x0F);
}

#include DEVHOOK_DEFS(keys) // development hooks (devhooks.h): Keys::inject

uint8_t Keys::update()
{
    uint8_t result = 0;

    DEVHOOK_KEYS_POLL();
    // keyInt = 1; // debug i2c errors
    if (keyInt)
    {
        result = 1;
        uint8_t eventCount = getKeyEventCount();
        if (eventCount > 0)
        {
            for (uint8_t i = 0; i < eventCount; i++)
            {
                uint8_t key = getKeyEvent();
                if (key > 0)
                {
                    uint8_t keyNumber = key & 0x7F;
                    uint8_t keyState = key & 0x80;

                    uint8_t button = convertTable[keyNumber - 1];

                    if (keyState)
                    {
                        holdTimeMs[button] = millis();
                        holdFactor[button] = 0;
                        onPush(button);
                        buttonPush[button] = 1;
                    }
                    else
                    {
                        onRelease(button);
                        buttonPush[button] = 0;
                    }

                    keyInt = 0;
                    writeByte(INT_STAT, 0x0F); // clear interrupt
                }
            }
        }
    }

    for (uint8_t i = 0; i < BUTTON_MAX; i++)
    {
        if (buttonPush[i] &&
            ((millis() - holdTimeMs[i]) > (holdTime - holdTimeStepDiff[holdFactor[i]])))
        {
            if (holdFactor[i] < holdTimeStepsCount - 1)
                holdFactor[i]++;

            holdTimeMs[i] = millis();
            onHold(i);
        }
    }

    return result;
}

void Keys::setOnPush(void (*funct)(uint8_t))
{
    onPush = funct;
}
void Keys::setOnRelease(void (*funct)(uint8_t))
{
    onRelease = funct;
}
void Keys::setOnHold(void (*funct)(uint8_t))
{
    onHold = funct;
}

uint8_t Keys::isButtonPressed(uint8_t n)
{
    return buttonPush[n];
}

void Keys::refreshConfig()
{
    if (resetTransmissionFunc != nullptr)
        resetTransmissionFunc();

    uint8_t cfg = 0;
    readByte(CFG, &cfg, true);
    //	cppserial_print_val(cfg);
    //	cppserial_print(" - ");
    //	cppserial_print_val(currentConfig);

    if (cfg != currentConfig)
    {
        for (int button = 0; button < BUTTON_MAX; button++)
        {
            if (buttonPush[button])
            {
                onRelease(button);
                buttonPush[button] = 0;
            }
        }

        setupKeys(rows, cols, config, true);

        //		 cppserial_print(" setup keys ");
    }
    //
    //	cppserial_println("");
}
