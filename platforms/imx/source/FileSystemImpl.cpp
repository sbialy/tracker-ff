#include "FileSystemImpl.h"
#include "TimeControl.h"
#include "config.h"
#include "log.h"
#include "util_string.h"

#include "interruptsConfig.h"

#include "ff.h"
#include "fsl_sd.h"
#include "fsl_sd_disk.h"
#include "sdmmc_config.h"

#include <algorithm>
#include <cr_section_macros.h>
#include <cstdio>
#include <polyassert.h>

static constexpr TCHAR driverNumberBuffer[3U] = { SDDISK + '0', ':', '/' };
static FATFS g_fileSystem; /* File system object */

__NOINIT(EXTERNAL_RAM) uint8_t externalBuffer[kFileOperationsExternalBufferSize];

static constexpr size_t kSdFilePoolSize = FF_FS_LOCK;
__NOINIT(EXTERNAL_RAM) static SdFile s_sdFilePool[kSdFilePoolSize];
static bool s_sdFilePoolUsed[kSdFilePoolSize]{ false };

static SdFile* allocateSdFile()
{
    for (size_t i = 0; i < kSdFilePoolSize; ++i)
    {
        if (!s_sdFilePoolUsed[i])
        {
            s_sdFilePoolUsed[i] = true;
            s_sdFilePool[i] = SdFile{};
            return &s_sdFilePool[i];
        }
    }
    return nullptr;
}

static void freeSdFile(SdFile* f)
{
    if (f == nullptr)
    {
        return;
    }
    for (size_t i = 0; i < kSdFilePoolSize; ++i)
    {
        if (&s_sdFilePool[i] == f)
        {
            s_sdFilePoolUsed[i] = false;
            return;
        }
    }
}

static int countOpenSdFiles()
{
    int count = 0;
    for (size_t i = 0; i < kSdFilePoolSize; ++i)
    {
        if (s_sdFilePoolUsed[i])
        {
            count++;
        }
    }
    return count;
}

void FileSystemImpl::update()
{
#ifdef TEST_FILESYSTEM
    TEST_listFilesByExtension();
#endif
    handleCardInitialization();
}

void FileSystemImpl::handleCardInitialization()
{
    bool cardInsertState = SD_IsCardPresent(&g_sd);

    if (cardInsertState == lastCardInsertState)
    {
        if (!cardInitializedSuccessfully && cardInsertState)
        {
            if (cardInsertTime + kCardInitDelayMs < timeControl.millis())
            {
                LOG("SD card init.");
                cardInitializedSuccessfully = initSdDriver();

                if (cardInitializedSuccessfully)
                {
                    if (cardStateChangeCallback != nullptr)
                    {
                        cardStateChangeCallback(true);
                    }
                }
            }
        }
    }
    else if (cardInsertState)
    {
        LOG("\nSD card inserted.");

        cardInsertTime = timeControl.millis();
        lastCardInsertState = true;
        cardInitializedSuccessfully = false;
    }
    else
    {
        LOG("\nSD card ejected.");

        deinitSdDriver();

        if (cardStateChangeCallback != nullptr)
        {
            cardStateChangeCallback(false);
        }

        lastCardInsertState = false;
    }
}

void FileSystemImpl::init()
{
    if (!cardInitializedSuccessfully)
    {
        cardInitializedSuccessfully = initSdDriver();
        lastCardInsertState = cardInitializedSuccessfully;
    }
}

bool FileSystemImpl::initSdDriver()
{
    BOARD_SD_Config(&g_sd, NULL, kSDHCInterruptLevel, NULL);

    if (SD_HostInit(&g_sd) == kStatus_Success)
    {
        if (SD_IsCardPresent(&g_sd))
        {
            if (f_mount(&g_fileSystem, driverNumberBuffer, 1U))
            {
                LOG("Mount volume failed.");
                return false;
            }
            else
            {
                if (f_chdrive((char const*)&driverNumberBuffer[0U]))
                {
                    LOG("Change drive failed.");
                    return false;
                }
            }
        }
        else
        {
            LOG("Card not present.");
            return false;
        }
    }
    else
    {
        LOG("SD host init failed.");
        return false;
    }

    return true;
}

bool FileSystemImpl::remount()
{
    // An ejected card was unmounted and its host shut down already. The card
    // is asked directly because this can be called from the ejection callback,
    // where lastCardInsertState still says it is in.
    if (!cardInitializedSuccessfully || !SD_IsCardPresent(&g_sd))
    {
        return false;
    }

    if (int openFiles = countOpenSdFiles(); openFiles > 0)
    {
        LOGF("SD card remount with %d open file(s), now invalid.", openFiles);
    }

    // Mounting initializes the card again, which must not cut off a read that
    // sample streaming still has in flight.
    waitAsyncRead();

    return f_mount(&g_fileSystem, driverNumberBuffer, 1U) == FR_OK;
}

void FileSystemImpl::deinitSdDriver()
{
    f_mount(0, driverNumberBuffer, 1);
    memset(&g_fileSystem, 0, sizeof(g_fileSystem));
    SD_HostDeinit(&g_sd);
    LOG("SD card deinit.");
}

bool FileSystemImpl::exists(const char* path)
{
    return SD.exists(path);
}

bool FileSystemImpl::rename(const char* oldPath, const char* newPath)
{
    return SD.rename(oldPath, newPath);
}

bool FileSystemImpl::mkdir(uint8_t hidden, const char* path, bool pFlag)
{
    return SD.mkdir(hidden, path, pFlag);
}

bool FileSystemImpl::remove(const char* path)
{
    return SD.remove(path);
}

bool FileSystemImpl::removeDirWithFiles(const char* path)
{
    return SD.removeDirWithFiles(path);
}

bool FileSystemImpl::removeOnlyDirFiles(const char* path)
{
    return SD.removeOnlyDirFiles(path);
}

size_t FileSystemImpl::getFileSize(const char* path)
{
    SdFile file;

    if (!file.open(path, FA_READ))
    {
        return 0;
    }

    size_t size = file.size();
    file.close();

    return size;
}

size_t FileSystemImpl::getFileSize(FileHandle fileHandle)
{
    SdFile* file = (SdFile*)fileHandle;

    if (!file->isOpen())
    {
        return 0;
    }
    size_t size = file->size();

    return size;
}

size_t FileSystemImpl::getFileAvailable(FileHandle fileHandle)
{
    assert(fileHandle);
    SdFile* file = (SdFile*)fileHandle;
    return file->available();
}

std::tuple<bool, FileSystemImpl::FileHandle> FileSystemImpl::openFile(const char* filepath,
                                                                      FileOpenMode openMode,
                                                                      FileHandle* fileHandle)
{

    SdFile* file;
    if (fileHandle == nullptr) // no input fileHandle
    {
        file = allocateSdFile();
    }
    else // fileHandle on input
    {
        if (*fileHandle == nullptr) // empty
        {
            file = allocateSdFile();
            *fileHandle = (FileHandle)file;
        }
        else // already created
        {
            file = (SdFile*)*fileHandle;
        }
    }

    if (file == nullptr)
    {
        return std::make_tuple(false, nullptr);
    }

    char mode = 0;

    switch (openMode)
    {
        case FileOpenMode::kWriteOpen:
            mode = FA_READ | FA_WRITE | FA_OPEN_ALWAYS;
            break;
        case FileOpenMode::kWriteAppend:
            mode = FA_READ | FA_WRITE | FA_OPEN_APPEND;
            break;
        case FileOpenMode::kWriteCreateNew:
            mode = FA_READ | FA_WRITE | FA_CREATE_ALWAYS;
            break;
        default:
            mode = FA_READ;
            break;
    }

    bool isOpen = file->open(filepath, mode);

    if (!isOpen && fileHandle == nullptr)
    {
        freeSdFile(file);
        file = nullptr;
    }

    return std::make_tuple(isOpen, (FileHandle)file);
}

bool FileSystemImpl::seekFileCur(int offset, FileHandle fileHandle)
{
    assert(fileHandle != nullptr);
    SdFile* file = (SdFile*)fileHandle;

    assert(file->isOpen());

    if (offset == 0)
    {
        return true;
    }

    return file->seekCur(offset);
}

bool FileSystemImpl::seekFile(uint32_t pos, FileHandle fileHandle)
{
    assert(fileHandle != nullptr);
    SdFile* file = (SdFile*)fileHandle;

    assert(file->isOpen());

    return file->seek(pos);
}

void FileSystemImpl::closeFile(FileHandle fileHandle, bool deleteOnClose)
{
    if (fileHandle == nullptr)
    {
        return;
    }
    SdFile* file = (SdFile*)fileHandle;

    file->close();

    if (deleteOnClose)
    {
        freeSdFile(file);
    }
}

bool FileSystemImpl::isFileOpen(FileHandle fileHandle)
{
    if (fileHandle == nullptr)
    {
        return false;
    }

    SdFile* file = (SdFile*)fileHandle;

    return file->isOpen();
}

bool FileSystemImpl::enableFastSeek(FileHandle fileHandle,
                                    uint32_t* clmtBuffer,
                                    uint32_t clmtElementCount,
                                    uint32_t* outDatabaseSector,
                                    uint32_t* outSectorsPerCluster)
{
    if (fileHandle == nullptr)
    {
        return false;
    }
    SdFile* file = (SdFile*)fileHandle;
    if (!file->isOpen())
    {
        return false;
    }
    return file->enableFastSeek(
      clmtBuffer, clmtElementCount, outDatabaseSector, outSectorsPerCluster);
}

bool FileSystemImpl::readSectorsAsync(uint8_t* buffer, uint32_t startSector, uint32_t sectorCount)
{
    return SD_ReadBlocksAsync(&g_sd, buffer, startSector, sectorCount) == kStatus_Success;
}

bool FileSystemImpl::pollAsyncRead(bool* outSuccess)
{
    return SD_PollAsyncRead(&g_sd, outSuccess);
}

bool FileSystemImpl::waitAsyncRead()
{
    return SD_WaitAsyncRead(&g_sd) == kStatus_Success;
}

bool FileSystemImpl::isAsyncReadBusy()
{
    return SD_IsAsyncBusy(&g_sd);
}

void FileSystemImpl::endFile(FileHandle fileHandle)
{
    assert(fileHandle != nullptr);
    SdFile* file = (SdFile*)fileHandle;

    file->end();
}

int32_t FileSystemImpl::readFile(void* destBuf, size_t destLen, FileHandle fileHandle)
{
    assert(fileHandle != nullptr);
    SdFile* file = (SdFile*)fileHandle;

    assert(file->isOpen());

    return file->read(destBuf, destLen);
}

int32_t FileSystemImpl::writeFile(const void* data, size_t dataLen, FileHandle fileHandle)
{
    assert(fileHandle != nullptr);
    SdFile* file = (SdFile*)fileHandle;

    assert(file->isOpen());

    return file->write(data, dataLen);
}

void FileSystemImpl::setMountEventCallback(CardStateChangeCallback funct)
{
    cardStateChangeCallback = funct;
}

bool FileSystemImpl::isSdCardInserted()
{
    handleCardInitialization();
    return lastCardInsertState && cardInitializedSuccessfully;
}

int FileSystemImpl::createInstrumentsList(const char* path,
                                          char** list,
                                          uint8_t list_length,
                                          uint32_t* sizeOut)
{
    SdDir dir;
    int instrumentsFound = 0;
    if (dir.open(path))
    {
        instrumentsFound = dir.createInstrumentsList(list, list_length, sizeOut);
        dir.close();
    }
    else
    {
        return kListError;
    }

    return instrumentsFound;
}

int FileSystemImpl::countContainedFiles(const char* path)
{
    SdDir dir;
    dir.open(path);

    int files_count = dir.countContainedFiles();

    dir.close();

    return files_count;
}

int FileSystemImpl::createFilesList(const char* path,
                                    uint8_t start_line,
                                    char** list,
                                    uint8_t list_length,
                                    uint16_t max_used_memory,
                                    enListFileFilter filter,
                                    uint16_t skip_count,
                                    uint16_t max_files_in_subdir,
                                    enListFileFilter folderSizeFilter)
{
    SdDir dir;

    if (!dir.open(path, O_READ))
    {
        return kListError;
    }

    int locationFileCount = dir.createFilesList(start_line,
                                                list,
                                                list_length,
                                                max_used_memory,
                                                (SdDir::enListFileFilter)filter,
                                                skip_count,
                                                max_files_in_subdir,
                                                (SdDir::enListFileFilter)folderSizeFilter);

    dir.close();
    return locationFileCount;
}

void FileSystemImpl::listDirContents(const char* dirPath,
                                     DirContentType contentType,
                                     std::function<bool(const char* name)> nameFilter,
                                     char* namesBuffer,
                                     int namesBufferSize,
                                     int maxNameLen,
                                     int& count,
                                     char** fileList)
{
    assert(count != 0);
    assert(fileList != nullptr);

    SdDir sdDir;

    if (!sdDir.open(dirPath, O_READ))
    {
        count = 0;
        return;
    }

    FILINFO fileInfo;

    int listPos = 0;
    while (listPos < count)
    {
        if (sdDir.readNextInOpenedDir(&fileInfo))
        {
            break;
        }

        if (sdDir.isEndOfFolder(&fileInfo))
        {
            break;
        }

        if (sdDir.isHidden(&fileInfo))
        {
            continue;
        }
        bool isFolder = sdDir.isFolder(&fileInfo);

        int nameLen;

        if (contentType == kFolders)
        {
            if (!isFolder || sdDir.isMacMetaFolder(&fileInfo))
            {
                continue;
            }

            if (nameFilter != nullptr)
            {
                if (!nameFilter(fileInfo.fname))
                {
                    continue;
                }
            }

            nameLen = strlen(fileInfo.fname) + 2;
        }
        else if (contentType == kFiles)
        {
            if (isFolder || sdDir.isMacMetaFile(&fileInfo))
            {
                continue;
            }

            if (nameFilter != nullptr)
            {
                if (!nameFilter(fileInfo.fname))
                {
                    continue;
                }
            }

            nameLen = strlen(fileInfo.fname) + 1;
        }
        else
        {
            continue;
        }

        if (nameLen > namesBufferSize)
        {
            break;
        }
        if (nameLen > maxNameLen)
        {
            continue;
        }

        fileList[listPos] = namesBuffer;

        if (isFolder)
        {
            *namesBuffer = '/';
            namesBuffer++;
        }

        polystrncpy(namesBuffer, fileInfo.fname, maxNameLen + 1);

        namesBuffer += nameLen;
        namesBufferSize -= nameLen;
        listPos++;
    }
    count = listPos;

    sdDir.close();

    std::sort(fileList,
              fileList + count,
              [](const char* a, const char* b) { return compareStrings(a, b); });
}

int FileSystemImpl::readNbFiles(const char* romPath)
{
    static constexpr int MAX_FILENAME_SIZE = 28;

    int totalFiles = 0;
    char filename[MAX_FILENAME_SIZE + 1];

    SdDir dir;
    dir.open(romPath);

    uint32_t size;
    uint8_t isDir;
    uint8_t isHidden;
    char name[64];

    while (true)
    {
        if (!dir.readItem(name, &isDir, &isHidden, &size))
        {
            // no more files
            break;
        }

        if (!isDir)
        {
            totalFiles++;
        }
        else
        {
            strncpy(&filename[0], name, MAX_FILENAME_SIZE);

            if ((strcmp(filename, ".")) && (strcmp(filename, "..")))
            {
                totalFiles++;
            }
        }
    }

    dir.close();

    return totalFiles;
}

bool FileSystemImpl::nesDir_open(const char* path)
{
    nesDir.close();
    return nesDir.open(path);
}

bool FileSystemImpl::nesDir_readItem(char* path, uint8_t* isDir, uint8_t* isHid, uint32_t* size)
{
    return nesDir.readItem(path, isDir, isHid, size);
}

void FileSystemImpl::nesDir_close()
{
    nesDir.close();
}
