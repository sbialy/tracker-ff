#ifndef __KEYS_H__
#define __KEYS_H__

#include "devhooks.h"
#include <stdint.h>

//-------- CONFIG --------------------------
constexpr uint8_t BUTTON_MAX = 48;

typedef uint8_t tca8418_reg_t;

static constexpr tca8418_reg_t CFG = 0x01;
static constexpr tca8418_reg_t INT_STAT = 0x02;
static constexpr tca8418_reg_t KEY_LCK_EC = 0x03;
static constexpr tca8418_reg_t KEY_EVENT_A = 0x04;
static constexpr tca8418_reg_t KP_GPIO1 = 0x1D;
static constexpr tca8418_reg_t KP_GPIO2 = 0x1E;
static constexpr tca8418_reg_t KP_GPIO3 = 0x1F;

class I2cBusController;

class Keys
{
  public:
    enum CfgRegisterFieldMasks
    {
        KE_IEN = 0x01,
        GPI_IEN = 0x02,
        K_LCK_IEN = 0x04,
        OVR_FLOW_IEN = 0x08,
        INT_CFG = 0x10,
        OVR_FLOW_M = 0x20,
        GPI_E_CFG = 0x40,
        AI = 0x80
    };

    void begin(uint8_t rows,
               uint16_t cols,
               uint8_t config,
               I2cBusController* i2cController,
               uint8_t* table,
               void (*resetFunc)(void));
    void setupKeys(uint8_t rows, uint16_t cols, uint8_t config, bool ignoreErrors);

    void enableInterrupt(uint8_t pin, void (*selfCheckFunction)(void));
    void disableInterrupt();

    static uint8_t getRowMask(int rowIdx);
    static uint16_t getColumnMask(int columnIdx);

    void setHoldTime(uint16_t time_ms) { holdTime = time_ms; };

    uint8_t update();

    void setOnPush(void (*funct)(uint8_t));
    void setOnRelease(void (*funct)(uint8_t));
    void setOnHold(void (*funct)(uint8_t));

    uint8_t isButtonPressed(uint8_t n);
    inline uint8_t isButtonHold(uint8_t n) { return buttonPush[n] && holdFactor[n] > 0; }

    uint8_t keyInt = 0;

    DEVHOOK_KEYS_PUBLIC // development hooks (devhooks.h): the injected key queue

  private:
    I2cBusController* i2cBusController;

    static constexpr int kI2cTimeout = 100;
    static constexpr int kMaxRowsCount = 8;
    static constexpr int kMaxColumnsCount = 10;
    static constexpr uint8_t holdTimeStepsCount = 6;
    static const uint16_t
      holdTimeStepDiff[holdTimeStepsCount]; // progressing key hold values differences

    void (*onPush)(uint8_t);
    void (*onRelease)(uint8_t);
    void (*onHold)(uint8_t);
    void (*resetTransmissionFunc)(void) = nullptr;

    uint8_t currentConfig = 0;
    uint8_t buttonPush[BUTTON_MAX] = { 0 };
    uint32_t holdTimeMs[BUTTON_MAX];
    uint8_t holdFactor[BUTTON_MAX] = { 0 };

    DEVHOOK_KEYS_PRIVATE

    uint8_t getKeyEvent();
    uint8_t getKeyEventCount();

    void writeByte(uint8_t reg, uint8_t data, bool ignoreError = false);
    bool readByte(uint8_t reg, uint8_t* data, bool ignoreError = false);

    void refreshConfig();

    uint8_t intPin;
    uint8_t* convertTable;
    uint8_t rows;
    uint16_t cols;
    uint8_t config;

    uint16_t holdTime = 333;
};

#endif
