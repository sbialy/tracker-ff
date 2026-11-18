// The SD card of fs_host_test.cpp: the SD_* calls of the NXP driver (shim/fsl_sd.h) on a disk image file, with counters. Host only,
// NOT firmware. The async read is done at once (the device runs it by DMA), its result is kept for the poll and the wait.
#include "fsl_sd.h"

#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

int g_fakeCardOut;         // the card detect pin: 1 = card pulled
extern "C" void pinMode(uint8_t, uint8_t) {}
extern "C" uint8_t digitalRead(uint8_t) { return uint8_t(g_fakeCardOut); }   // FileSystemImpl.cpp declares both itself since 38dd035e
const char* g_imagePath;   // the card
unsigned g_reads, g_writes, g_asyncReads; // block calls that reached the card
bool g_hostIo;             // the test marks the drive's own block calls (FileSystemImpl::hostReadBlocks / hostWriteBlocks)
unsigned g_hostReads, g_hostWrites;
static bool s_asyncOk, s_asyncBusy;

static bool io(bool write, uint8_t* buf, uint32_t block, uint32_t count)
{
    FILE* f = fopen(g_imagePath, write ? "r+b" : "rb");
    if (!f) return false;
    bool ok = fseeko(f, off_t(block) * 512, SEEK_SET) == 0;
    ok = ok && (write ? fwrite(buf, 512, count, f) : fread(buf, 512, count, f)) == count;
    fclose(f);
    return ok;
}

extern "C" {
status_t SD_HostInit(sd_card_t* card)
{
    card->isHostReady = true;
    return kStatus_Success;
}
void SD_HostReset(sdmmchost_t*) {}
void SD_PowerOnCard(void*, void*) {}
void SD_PowerOffCard(void*, void*) {}
status_t SD_CardInit(sd_card_t* card)
{
    struct stat st;
    if (g_fakeCardOut || stat(g_imagePath, &st) != 0) return kStatus_Fail;
    card->blockCount = uint32_t(st.st_size / 512);
    card->blockSize = 512;
    card->csd.eraseSectorSize = 1;
    return kStatus_Success;
}
void SD_CardDeinit(sd_card_t*) {}
status_t SD_ReadBlocks(sd_card_t*, uint8_t* buffer, uint32_t startBlock, uint32_t blockCount)
{
    (g_hostIo ? g_hostReads : g_reads)++;
    return io(false, buffer, startBlock, blockCount) ? kStatus_Success : kStatus_Fail;
}
status_t SD_WriteBlocks(sd_card_t*, const uint8_t* buffer, uint32_t startBlock, uint32_t blockCount)
{
    (g_hostIo ? g_hostWrites : g_writes)++;
    return io(true, const_cast<uint8_t*>(buffer), startBlock, blockCount) ? kStatus_Success : kStatus_Fail;
}
status_t SD_ReadBlocksAsync(sd_card_t*, uint8_t* buffer, uint32_t startBlock, uint32_t blockCount)
{
    g_asyncReads++;
    s_asyncOk = io(false, buffer, startBlock, blockCount);
    s_asyncBusy = true;
    return kStatus_Success;
}
bool SD_PollAsyncRead(sd_card_t*, bool* outSuccess)
{
    if (!s_asyncBusy) return false;
    s_asyncBusy = false;
    if (outSuccess) *outSuccess = s_asyncOk;
    return true;
}
status_t SD_WaitAsyncRead(sd_card_t*)
{
    s_asyncBusy = false;
    return s_asyncOk ? kStatus_Success : kStatus_Fail;
}
bool SD_IsAsyncBusy(sd_card_t*)
{
    return s_asyncBusy;
}
}

// SD.cpp's hardware setup (the SDHC clock and pins)
void initSDHC() {}
void enableGPIO(bool) {}
