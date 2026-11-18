#include "ExternalSdram.h"
#include "core_pins.h"
#include "kinetis.h"
#include "log.h"
#include "sdram.h"
#include "usb_serial.h"
#include <random>

//#define RAM_TEST

enum ResultType
{
    kSuccessful = 0,
    kSequentialError,
    kRandomError,
};

void ExternalSDRAM::init()
{
    constexpr int kWaitForClockMs = 10;

    // divide MCGOUTCLK (180MHz) by 2
    // https://www.nxp.com/docs/en/reference-manual/K66P144M180SF5RMV2.pdf
    // str. 120, str.269
    SIM_CLKDIV1 |= SIM_CLKDIV1_OUTDIV3(1);
    delay(kWaitForClockMs);

    Extern_SDRAM_Init();
}

void ExternalSDRAM::update()
{
#ifdef RAM_TEST
    static elapsedMillis updateTimer = 0;
    uint32_t kUpdateTimeMs = 2000;
    if (updateTimer > kUpdateTimeMs)
    {
        test();
        updateTimer = 0;
    }
#endif
}

void ExternalSDRAM::setLowPowerMode(bool lowPowerMode)
{
    Extern_SDRAM_SetLowPower((uint8_t)lowPowerMode);
}

bool ExternalSDRAM::test()
{
    int* const kExternalRamAddrBlock0 = (int*)0x70000000;

    return testRamBlock(0, kExternalRamAddrBlock0);
}

bool ExternalSDRAM::testRamBlock(int /*blockIdx*/, int* const /*startAddress*/)
{
    int testResult = kSuccessful;
#ifdef RAM_TEST
    std::minstd_rand rand;

    rand.seed(micros());
    std::uniform_int_distribution<> distrib(0, kRamSize - 1);

    __disable_irq();

    for (int idx = 0; idx < kRamSize; idx++)
    {
        startAddress[idx] = idx;
    }

    for (int idx = 0; idx < kRamSize; idx++)
    {
        if (startAddress[idx] != idx)
        {
            testResult |= kSequentialError;
            break;
        }
    }

    for (int testNum = 0; testNum < kRamSize; testNum++)
    {

        int idx = distrib(rand);
        startAddress[idx] = idx;

        idx = distrib(rand);
        if (startAddress[idx] != idx)
        {
            testResult |= kRandomError;
            break;
        }
    }

    __enable_irq();
#ifdef DEBUG
    int blockIdxToPrint = blockIdx + 1;
    LOG("**************************************");
    LOGF("Start Test RAM %d\n", blockIdxToPrint);
    LOGF("Start addr: %x\n", (uint32_t)startAddress);

    if (testResult == kSuccessful)
    {
        LOGF("Test RAM %d successful\n", blockIdxToPrint);
    }
    else
    {
        LOGF("Test RAM %d failed on: %s%s\n",
             blockIdxToPrint,
             testResult & kSequentialError ? "-sequential read" : "",
             testResult & kRandomError ? " -random read/write" : "");
    }
#else
    (void)blockIdx;
#endif

#endif
    return testResult == kSuccessful;
}

ExternalSDRAM externalRAM;
