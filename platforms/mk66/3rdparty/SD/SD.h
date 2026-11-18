
#ifndef LIBS_NEWSD_SD_H_
#define LIBS_NEWSD_SD_H_

#include <stdint.h>
#include <string.h>

#include "ff.h"

#ifdef SDK_ALIGN
#undef SDK_ALIGN
#endif

const uint8_t SD_DEBUGLOG = 1;

//#define SD_FILE_WRITE  ( FA_READ | FA_WRITE | FA_CREATE_NEW | FA_OPEN_APPEND )
#define SD_FILE_WRITE (FA_READ | FA_WRITE | FA_CREATE_ALWAYS)
#define O_RDONLY      FA_READ
#define O_READ        FA_READ

#define FILE_WRITE      (FA_READ | FA_WRITE | FA_CREATE_ALWAYS)
#define FILE_OPEN_WRITE (FA_READ | FA_WRITE | FA_OPEN_ALWAYS)

void reportSdError(const char* text, uint16_t value);

class SdFile;

class SdCard
{
  public:
    bool init();
    void stop();
    void pinsInit();

    bool exists(const char* path)
    {
        if (f_stat(path, nullptr) == FR_OK)
            return true;
        return false;
    }

    bool rename(const char* oldPath, const char* newPath);
    bool mkdir(uint8_t hidden, const char* path, bool pFlag = true);
    SdFile open(const char* path, uint8_t oflag = FA_READ); // todo:
    bool remove(const char* path);
    bool removeDirWithFiles(const char* path);
    bool removeOnlyDirFiles(const char* path);

    uint32_t clusterCount();
    uint16_t sectorsPerCluster();
    uint32_t freeClusterCount();

  private:
    FRESULT delete_node(TCHAR* path, UINT sz_buff, FILINFO* fno, bool removeDir = true);
};

class SdFile
{
  public:
    bool open(const char* path, uint8_t oflag = FA_READ);
    bool close();
    bool enableFastSeek(uint32_t* clmtBuffer,
                        uint32_t clmtElementCount,
                        uint32_t* outDatabaseSector = nullptr,
                        uint32_t* outSectorsPerCluster = nullptr);

    int32_t read(void* buf, uint32_t count);
    int32_t write(const void* buf, uint32_t count);
    uint32_t write(uint8_t b) { return write(&b, 1); }
    bool end()
    {
        FRESULT error = f_truncate(file);
        if (error)
        {
            reportSdError("truncate - failed", error);
            return false;
        }
        return true;
    }

    bool seek(uint32_t pos)
    {
        FRESULT error = f_lseek(file, pos);
        if (error)
        {
            reportSdError("seek - failed", error);
            return false;
        }
        return true;
    }

    bool seekCur(int32_t offset)
    {
        FRESULT error = f_lseek(file, f_tell(file) + offset);
        if (error)
        {
            reportSdError("seekCur - failed", error);
            return false;
        }
        return true;
    }

    void println() { print("\n"); }

    void println(const char* text)
    {
        print(text);
        println();
    }

    void print(const char* text) { f_puts(text, file); }

    void print(int value) { f_printf(file, "%u", value); }

    void printf(const char* format, int value) { f_printf(file, format, value); }

    void printf(const char* format, int value1, int value2)
    {
        f_printf(file, format, value1, value2);
    }

    void printf(const char* format, int value1, int value2, int value3)
    {
        f_printf(file, format, value1, value2, value3);
    }

    void printf(const char* format, char* str, int value) { f_printf(file, format, str, value); }

    operator bool() { return isOpen(); }

    bool isOpen() { return file != nullptr; }
#ifdef FEATURE_USB_MSC
    // USB drive (FileSystemImpl::lendToHost): what an open file still holds goes to the card before the computer gets it
    bool sync() { return file == nullptr || f_sync(file) == FR_OK; }
#endif

    uint32_t available() { return isOpen() ? f_size(file) - f_tell(file) : 0; }

    uint64_t size() { return f_size(file); }

  private:
    FIL* file = nullptr;
    char file_path[255]{ 0 };
};

class SdDir
{
  public:
    bool open(const char* path, uint8_t oflag = FA_READ);
    bool close();

    operator bool() { return isOpen(); }

    bool isOpen() { return directory != nullptr; }

    FRESULT readNextInOpenedDir(FILINFO* fno);

    static bool isEndOfFolder(FILINFO* fno);
    static bool isHidden(FILINFO* fno);
    static bool isFolder(FILINFO* fno);
    static bool isMacMetaFile(FILINFO* fno);
    static bool isMacMetaFolder(FILINFO* fno);

    bool readItem(char* path, uint8_t* isDir, uint8_t* isHid, uint32_t* size);

    enum enListFileFilter
    {
        enListFileFilter_all,
        enListFileFilter_folder,
        enListFileFilter_wave,
        enListFileFilter_wave_inst,
        enListFileFilter_firmware,
        enListFileFilter_mod,
        enListFileFilter_game,
    };

    uint16_t createFilesList(uint8_t start_line,
                             char** list,
                             uint8_t list_length,
                             uint16_t max_used_memory,
                             enListFileFilter filter = enListFileFilter_all,
                             uint16_t skip_count = 0,
                             uint16_t max_files_in_subdir = 0,
                             enListFileFilter folderSizeFilter = enListFileFilter_all);

    uint16_t createInstrumentsList(char** list, uint8_t list_length, uint32_t* sizeOut);
    uint16_t countContainedFiles();

  private:
    DIR* directory = nullptr;
    char dir_path[255]{ 0 };

    bool filterFileEntry(const FILINFO& fno, enListFileFilter filter);
    uint16_t countFilesInSubDirectory(const char* dir_name, enListFileFilter filter);
};

extern SdCard SD;

#endif /* LIBS_NEWSD_SD_H_ */
