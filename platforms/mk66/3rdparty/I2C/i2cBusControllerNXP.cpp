#include "i2cBusControllerNXP.h"

#include "cserial.h"
#include "fsl_gpio.h"
#include "fsl_i2c.h"
#include "fsl_port.h"

i2c_master_handle_t handleI2c0;
i2c_master_handle_t handleI2c2;

I2cBusControllerImpl0 i2cBusControllerNXP0;
I2cBusControllerImpl2 i2cBusControllerNXP2;

static void I2c_callback(I2C_Type* /*base*/,
                         i2c_master_handle_t* /*handle*/,
                         status_t status,
                         void* userData)
{
    if (status == kStatus_Success)
    {
        ((I2cBusControllerImplBase*)userData)->completionFlag = true;
    }
}

void I2cBusControllerImplBase::setTimeout(uint32_t timeoutUs)
{
    this->timeoutUs = timeoutUs;
    constexpr int kOneUsCycles = 180;
    constexpr int kIncrementCycles = 3; // experimental

    I2C_setRetryTimes(timeoutUs * (kOneUsCycles / kIncrementCycles));
}

bool I2cBusControllerImplBase::isBusIdle()
{
    return !(((I2C_Type*)baseAddr)->S & _i2c_flags::kI2C_BusBusyFlag);
}

bool I2cBusControllerImplBase::startTransmission(uint8_t address)
{
    bool result = true;

    //	int retryCount = 0;
    //
    //	while(((I2C_Type*)baseAddr)->S & _i2c_flags::kI2C_BusBusyFlag)
    //	{
    //		for(uint32_t t = 0; t < 1000; t++)
    //		{
    //			__asm__ volatile ("nop");
    //		}
    //		retryCount++;
    //
    //		if(retryCount > 10)
    //		{
    //			//cppserial_print_val((uint8_t)((I2C_Type*)baseAddr)->S);
    //			//cppserial_println(" i2c busy error");
    //			result = false;
    //			//reset();
    //			break;
    //		}
    //	}

    transmitSize = 0;
    currentSlaveAddress = address;
    status = kStart;

    return result;
}

I2cBusController::OperationResult I2cBusControllerImplBase::finishTransmission(bool sendStop,
                                                                               int* error)
{
    i2c_master_transfer_t masterXfer;
    masterXfer.slaveAddress = currentSlaveAddress;
    masterXfer.direction = kI2C_Write;
    masterXfer.subaddress = 0;
    masterXfer.subaddressSize = 0;
    masterXfer.data = transmitQueue;
    masterXfer.dataSize = transmitSize;
    masterXfer.flags = kI2C_TransferDefaultFlag;
    masterXfer.sendStop = sendStop;

    assert(baseAddr != nullptr);
    status = kSending;

    status_t transferStatus = I2C_MasterTransferBlocking((I2C_Type*)baseAddr, &masterXfer);

    if (error != nullptr)
    {
        *error = transferStatus;
    }

    OperationResult result = OperationResult::kOK;

    if (transferStatus == kStatus_Success)
    {
        status = kIdle;
    }
    else
    {
        status = kError;
        result = OperationResult::kError;
    }

    return result;
}

I2cBusController::OperationResult I2cBusControllerImplBase::sendTransmission(bool sendStop)
{
    i2c_master_transfer_t masterXfer;
    memset(&masterXfer, 0, sizeof(masterXfer));
    /* subAddress = 0x01, data = g_master_txBuff - write to slave.
      start + slaveaddress(w) + subAddress + length of data buffer + data buffer + stop*/
    masterXfer.slaveAddress = currentSlaveAddress;
    masterXfer.direction = kI2C_Write;
    masterXfer.subaddress = 0;
    masterXfer.subaddressSize = 0;
    masterXfer.data = transmitQueue;
    masterXfer.dataSize = transmitSize;
    masterXfer.flags = kI2C_TransferDefaultFlag;
    masterXfer.sendStop = sendStop;

    status = kSending;

    status_t transferStatus =
      I2C_MasterTransferNonBlocking((I2C_Type*)baseAddr, (i2c_master_handle_t*)handle, &masterXfer);

    OperationResult result = OperationResult::kOK;
    if (transferStatus != kStatus_Success)
    {
        status = kError;
        result = OperationResult::kError;
    }

    return result;
}

I2cBusController::OperationResult I2cBusControllerImplBase::request(uint8_t address, size_t size)
{
    i2c_master_transfer_t masterXfer;
    memset(&masterXfer, 0, sizeof(masterXfer));
    masterXfer.slaveAddress = address;
    masterXfer.direction = kI2C_Read;
    masterXfer.subaddress = 0;
    masterXfer.subaddressSize = 0;
    masterXfer.data = receiveQueue;
    masterXfer.dataSize = size;
    masterXfer.flags = kI2C_TransferDefaultFlag;
    masterXfer.sendStop = true;
    status = kReceiving;

    receiveSize = size;
    dataToReceiveIdx = 0;

    assert(baseAddr != nullptr);

    status_t transferStatus = I2C_MasterTransferBlocking((I2C_Type*)baseAddr, &masterXfer);

    OperationResult result = OperationResult::kOK;
    if (transferStatus == kStatus_Success)
    {
        status = kIdle;
    }
    else
    {
        status = kError;
        result = OperationResult::kError;
    }

    return result;
}

uint8_t I2cBusControllerImplBase::read()
{
    if (dataToReceiveIdx < receiveSize)
    {
        return receiveQueue[dataToReceiveIdx++];
    }
    else
    {
        return 0;
    }
}

bool I2cBusControllerImplBase::isDataToReceive()
{
    return receiveSize != dataToReceiveIdx;
}

void I2cBusControllerImplBase::reset()
{
    /* Disable I2C prior to configuring it. */
    ((I2C_Type*)baseAddr)->C1 &= ~(uint8_t)(I2C_C1_IICEN_MASK);

    /* Clear all flags. */
    I2C_MasterClearStatusFlags((I2C_Type*)baseAddr, (uint32_t)kClearFlags);

    /* Enable the I2C peripheral based on the configuration. */
    ((I2C_Type*)baseAddr)->C1 |= I2C_C1_IICEN_MASK;
}

void I2cBusControllerImpl0::init()
{
    const port_pin_config_t sda_config = { kPORT_PullDisable,          kPORT_SlowSlewRate,
                                           kPORT_PassiveFilterDisable, kPORT_OpenDrainEnable,
                                           kPORT_HighDriveStrength,    kPORT_MuxAlt2,
                                           kPORT_UnlockRegister };

    PORT_SetPinConfig(PORTD, 9U, &sda_config);

    const port_pin_config_t scl_config = { kPORT_PullDisable,          kPORT_SlowSlewRate,
                                           kPORT_PassiveFilterDisable, kPORT_OpenDrainEnable,
                                           kPORT_HighDriveStrength,    kPORT_MuxAlt2,
                                           kPORT_UnlockRegister };

    PORT_SetPinConfig(PORTD, 8U, &scl_config);

    i2c_master_config_t masterConfig;
    I2C_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate_Bps = 400000;
    masterConfig.glitchFilterWidth = 4;

    baseAddr = (void*)I2C0;

    I2C_MasterInit((I2C_Type*)baseAddr, &masterConfig, 90000000);

    memset(&handleI2c0, 0, sizeof(handleI2c0));
    handle = &handleI2c0;
    I2C_MasterTransferCreateHandle(
      (I2C_Type*)baseAddr, (i2c_master_handle_t*)handle, I2c_callback, this);
}

void I2cBusControllerImpl0::unblockBus()
{
    int kForcedEdgesCount = 16;
    const port_pin_config_t scl_config = { kPORT_PullDisable,          kPORT_FastSlewRate,
                                           kPORT_PassiveFilterDisable, kPORT_OpenDrainDisable,
                                           kPORT_LowDriveStrength,     kPORT_MuxAsGpio,
                                           kPORT_UnlockRegister };

    PORT_SetPinConfig(PORTD, 8U, &scl_config);

    gpio_pin_config_t gpio_config;
    gpio_config.pinDirection = kGPIO_DigitalOutput;
    gpio_config.outputLogic = 0;

    GPIO_PinInit(GPIOD, 8, &gpio_config);

    for (int bit = 0; bit < kForcedEdgesCount; bit++)
    {
        GPIO_PinWrite(GPIOD, 8, bit & 1);
    }
}

void I2cBusControllerImpl2::init()
{
    const port_pin_config_t sda_config = { kPORT_PullDisable,          kPORT_SlowSlewRate,
                                           kPORT_PassiveFilterDisable, kPORT_OpenDrainEnable,
                                           kPORT_HighDriveStrength,    kPORT_MuxAlt5,
                                           kPORT_UnlockRegister };

    PORT_SetPinConfig(PORTA, 13U, &sda_config);

    const port_pin_config_t scl_config = { kPORT_PullDisable,          kPORT_SlowSlewRate,
                                           kPORT_PassiveFilterDisable, kPORT_OpenDrainEnable,
                                           kPORT_HighDriveStrength,    kPORT_MuxAlt5,
                                           kPORT_UnlockRegister };

    i2c_master_config_t masterConfig;
    I2C_MasterGetDefaultConfig(&masterConfig);
#ifndef TRACKER_MINI
    PORT_SetPinConfig(PORTA, 12U, &scl_config);
    masterConfig.baudRate_Bps = 400000;
#else
    PORT_SetPinConfig(PORTA, 14U, &scl_config);
    masterConfig.baudRate_Bps = 100000;
#endif

    baseAddr = (void*)I2C2;

    masterConfig.glitchFilterWidth = 4;
    I2C_MasterInit((I2C_Type*)baseAddr, &masterConfig, 90000000);

    memset(&handleI2c2, 0, sizeof(handleI2c2));
    handle = &handleI2c2;
    I2C_MasterTransferCreateHandle(
      (I2C_Type*)baseAddr, (i2c_master_handle_t*)handle, I2c_callback, this);
}

void I2cBusControllerImpl2::unblockBus()
{
    int kForcedEdgesCount = 16;
    const port_pin_config_t scl_config = { kPORT_PullDisable,          kPORT_FastSlewRate,
                                           kPORT_PassiveFilterDisable, kPORT_OpenDrainDisable,
                                           kPORT_LowDriveStrength,     kPORT_MuxAsGpio,
                                           kPORT_UnlockRegister };

    PORT_SetPinConfig(PORTA, 12U, &scl_config);

    gpio_pin_config_t gpio_config;
    gpio_config.pinDirection = kGPIO_DigitalOutput;
    gpio_config.outputLogic = 0;

    GPIO_PinInit(GPIOA, 12, &gpio_config);

    for (int bit = 0; bit < kForcedEdgesCount; bit++)
    {
        GPIO_PinWrite(GPIOA, 12, bit & 1);
    }
}
