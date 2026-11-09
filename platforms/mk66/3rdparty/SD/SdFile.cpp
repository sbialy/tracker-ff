

#include "SD.h"
#include "cr_section_macros.h"

#include <stdio.h>

#include "cserial.h"

static char print_string[300];

static constexpr size_t kFilPoolSize = FF_FS_LOCK;
#ifdef FW_SDRAM_FILE_POOLS
// SDRAM, as before tracker-ce 5b6b0f2: keeps the internal RAM for the heap and the plugin slots
__NOINIT(EXTERNAL_RAM)
#endif
static FIL s_filPool[kFilPoolSize];
static bool s_filPoolUsed[kFilPoolSize]{ false };

static FIL* allocateFil()
{
    for (size_t i = 0; i < kFilPoolSize; ++i)
    {
        if (!s_filPoolUsed[i])
        {
            s_filPoolUsed[i] = true;
            memset(&s_filPool[i], 0, sizeof(FIL));
            return &s_filPool[i];
        }
    }
    return nullptr;
}

static void freeFil(FIL* f)
{
    if (f == nullptr)
    {
        return;
    }
    for (size_t i = 0; i < kFilPoolSize; ++i)
    {
        if (&s_filPool[i] == f)
        {
            s_filPoolUsed[i] = false;
            return;
        }
    }
}

// size_t println(const char s[]);

bool SdFile::open(const char* path, uint8_t oflag)
{
    close();

    //---------------------------------------------
    int opened_files_count = get_lock_count();
    if (opened_files_count > 1)
    {
        snprintf(print_string, 300, "%d file open: %s", opened_files_count, path);
        cppserial_println(print_string);
    }
    //---------------------------------------------

    file = allocateFil();
    if (file == nullptr)
    {
        return false;
    }
    FRESULT error = f_open(file, path, oflag);
    if (error)
    {
        snprintf(print_string, 300, "open error: %s", path);
        reportSdError(print_string, error);
        close();
        return false;
    }

    strncpy(file_path, path, 255);

    return true;
}

bool SdFile::close()
{
    if (file == nullptr)
    {
        return true;
    }

    FRESULT error = f_close(file);

    freeFil(file);
    file = nullptr;
    memset(file_path, 0, 255);

    if (error == FR_INVALID_OBJECT)
    {
        return false;
    }
    else if (error)
    {
        reportSdError("close - failed", error);
        return false;
    }

    //---------------------------------------------
    int opened_files_count = get_lock_count();
    if (opened_files_count > 1)
    {
        snprintf(print_string, 300, "%d file close: %s", opened_files_count, file_path);
        cppserial_println(print_string);
    }
    //---------------------------------------------

    return true;
}

bool SdFile::enableFastSeek(uint32_t* clmtBuffer,
                            uint32_t clmtElementCount,
                            uint32_t* outDatabaseSector,
                            uint32_t* outSectorsPerCluster)
{
    if (file == nullptr || clmtBuffer == nullptr || clmtElementCount < 4)
    {
        return false;
    }
    clmtBuffer[0] = clmtElementCount;
    file->cltbl = (DWORD*)clmtBuffer;
    FRESULT res = f_lseek(file, CREATE_LINKMAP);
    if (res != FR_OK)
    {
        file->cltbl = nullptr;
        return false;
    }
    if (outDatabaseSector != nullptr && file->obj.fs != nullptr)
    {
        *outDatabaseSector = (uint32_t)file->obj.fs->database;
    }
    if (outSectorsPerCluster != nullptr && file->obj.fs != nullptr)
    {
        *outSectorsPerCluster = (uint32_t)file->obj.fs->csize;
    }
    return true;
}

int32_t SdFile::read(void* buf, uint32_t count)
{
    UINT read = 0;
    FRESULT error = f_read(file, buf, count, &read);
    if (error)
    {
        snprintf(print_string, 300, "read error: %s", file_path);
        reportSdError(print_string, error);
        return -1;
    }
    return read;
}

int32_t SdFile::write(const void* buf, uint32_t count)
{
    UINT written = 0;
    FRESULT error = f_write(file, buf, count, &written);
    if (error)
    {
        snprintf(print_string, 300, "write error: %s", file_path);
        reportSdError(print_string, error);
        return -1;
    }
    return written;
}
