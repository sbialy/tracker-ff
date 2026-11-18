// Host shim of the NXP SD driver (platforms/mk66/3rdparty/SD/sdmmc/inc/fsl_sd.h) for fs_host_test.cpp: only the names SD.cpp,
// fsl_sd_disk.c and FileSystemImpl.cpp use. The card is a disk image file (fake_sd.cpp). Host only, NOT firmware.
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef int32_t status_t;
enum { kStatus_Success = 0, kStatus_Fail = 1 };
typedef struct { int cdType; uint32_t cdTimeOut_ms; void (*cardInserted)(bool, void*); void (*cardRemoved)(bool, void*); void* userData; } sdmmchost_detect_card_t;   // the fields SD.cpp names since 38dd035e (partial initializer clean up)
enum { kSDMMCHOST_DetectCardByHostCD = 1 };
typedef struct { void* base; uint32_t sourceClock_Hz; } sdmmchost_t;
typedef struct { const sdmmchost_detect_card_t* cd; void* pwr; } sdmmchost_usr_param_t;
typedef struct { uint32_t eraseSectorSize; } sd_csd_t;
typedef struct
{
    sdmmchost_t host;
    sdmmchost_usr_param_t usrParam;
    bool isHostReady;
    uint32_t blockCount;
    uint32_t blockSize;
    sd_csd_t csd;
} sd_card_t;
#define SDHC ((void*)0x400B1000u)

// pin setup of SdCard::pinsInit (never called on the device either)
typedef struct { int a, b, c, d, e, f, g; } port_pin_config_t;
enum { kPORT_PullUp, kPORT_FastSlewRate, kPORT_PassiveFilterDisable, kPORT_OpenDrainDisable, kPORT_LowDriveStrength,
       kPORT_MuxAlt4, kPORT_UnlockRegister };
enum { kCLOCK_PortE };
#define PORTE ((void*)0)
static inline void PORT_SetPinConfig(void*, uint32_t, const port_pin_config_t*) {}
static inline void CLOCK_EnableClock(int) {}
static inline void SDMMCHOST_Reset(void*) {}
static inline void SDMMCHOST_Deinit(void*) {}

#ifdef __cplusplus
extern "C" {
#endif
status_t SD_HostInit(sd_card_t* card);
void SD_HostReset(sdmmchost_t* host);
void SD_PowerOnCard(void* base, void* pwr);
void SD_PowerOffCard(void* base, void* pwr);
status_t SD_CardInit(sd_card_t* card);
void SD_CardDeinit(sd_card_t* card);
status_t SD_ReadBlocks(sd_card_t* card, uint8_t* buffer, uint32_t startBlock, uint32_t blockCount);
status_t SD_WriteBlocks(sd_card_t* card, const uint8_t* buffer, uint32_t startBlock, uint32_t blockCount);
status_t SD_ReadBlocksAsync(sd_card_t* card, uint8_t* buffer, uint32_t startBlock, uint32_t blockCount);
bool SD_PollAsyncRead(sd_card_t* card, bool* outSuccess);
status_t SD_WaitAsyncRead(sd_card_t* card);
bool SD_IsAsyncBusy(sd_card_t* card);
#ifdef __cplusplus
}
#endif
