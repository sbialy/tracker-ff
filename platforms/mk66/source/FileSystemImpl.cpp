#include "FileSystemImpl.h"
#include "SD.h"
#include "cr_section_macros.h"
#include "fsl_sd.h"
#include "fsl_sd_disk.h"
#include "log.h"
#include "util_string.h"
#include <algorithm>
#ifdef FEATURE_USB_MSC
#include "msc_drive.h"
#endif

// The card detect pin needs two Teensy core calls only. wiring.h would pull in teensy_core/kinetis.h next to the NXP
// MK66F18.h that fsl_sd.h brings in, and the two redefine about 1,600 register macros (one "redefined" warning each).
// Declared here with the C linkage and the INPUT_PULLUP value of teensy_core/core_pins.h instead.
extern "C" {
void pinMode(uint8_t pin, uint8_t mode);
uint8_t digitalRead(uint8_t pin);
}
static constexpr uint8_t INPUT_PULLUP = 2;

static constexpr size_t kSdFilePoolSize = FF_FS_LOCK;
#ifdef FW_SDRAM_FILE_POOLS
// SDRAM, as before tracker-ce 5b6b0f2: keeps the internal RAM for the heap and the plugin slots
__NOINIT(EXTERNAL_RAM)
#endif
static SdFile s_sdFilePool[kSdFilePoolSize];
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

#ifdef FEATURE_USB_MSC
// USB_MSC=1, Drive mode: while the computer owns the card (lendToHost until remount) every call that could reach the card
// fails here, before FatFs or the SD driver: the gate of ref/usb-mass-storage-feasibility.md section 3. It catches the writers
// the core's storage flow does not know (Features saves, the module marker, the console, restart mode, plugin installs,
// screenshots, the recorder). FatFs is unmounted as well, so a direct f_* call (FileSystemAdapter::statPath) fails too.
static volatile bool s_hostOwned;
static uint32_t s_refused; // calls refused since the last lendToHost

bool FileSystemImpl::refuseHost()
{
    if (!s_hostOwned)
    {
        return false;
    }
    s_refused++;
    return true;
}
#define CARD_GATE(fail)                                                                                                    \
    do                                                                                                                     \
    {                                                                                                                      \
        if (refuseHost())                                                                                                  \
        {                                                                                                                  \
            return fail;                                                                                                   \
        }                                                                                                                  \
    } while (0)

bool FileSystemImpl::lendToHost()
{
    if (s_hostOwned)
    {
        return true;
    }
    if (!isSdCardInserted())
    {
        return false;
    }
    // The core suspended streaming and saving (Interface::handleFileTransferMode): let the last stream read land, then
    // unmount (the card stays initialised for the block access of the drive)
    SD_WaitAsyncRead(&g_sd);
    // A file still open for writing keeps its cached sector, its size and the FAT window in RAM: written now, the card the
    // computer mounts is consistent (the handle itself is invalid after the remount)
    for (size_t i = 0; i < kSdFilePoolSize; ++i)
    {
        if (s_sdFilePoolUsed[i] && s_sdFilePool[i].isOpen() && !s_sdFilePool[i].sync())
        {
            LOGE("card lent with an open file that did not sync");
        }
    }
    SD.stop();
    s_refused = 0;
    s_hostOwned = true;
    LOGEF("card lent to the computer, %u blocks", unsigned(g_sd.blockCount));
    return true;
}

bool FileSystemImpl::hostOwned() const
{
    return s_hostOwned;
}

uint32_t FileSystemImpl::refusedCalls() const
{
    return s_refused;
}

uint32_t FileSystemImpl::hostBlockCount() const
{
    return s_hostOwned ? g_sd.blockCount : 0;
}

bool FileSystemImpl::hostReadBlocks(uint32_t lba, uint8_t* dst, uint32_t count)
{
    if (!s_hostOwned || count == 0 || lba >= g_sd.blockCount || count > g_sd.blockCount - lba)
    {
        return false;
    }
    return SD_ReadBlocks(&g_sd, dst, lba, count) == kStatus_Success;
}

bool FileSystemImpl::hostWriteBlocks(uint32_t lba, const uint8_t* src, uint32_t count)
{
    if (!s_hostOwned || count == 0 || lba >= g_sd.blockCount || count > g_sd.blockCount - lba)
    {
        return false;
    }
    return SD_WriteBlocks(&g_sd, src, lba, count) == kStatus_Success;
}
#else
#define CARD_GATE(fail) ((void)0)
#endif

void FileSystemImpl::init(int sdDetectPin, Timer* timer)
{
    this->sdDetectPin = sdDetectPin;
    this->timer = timer;

    pinMode(sdDetectPin, INPUT_PULLUP);
    lastCardInsertState = digitalRead(sdDetectPin);
    cardInitializedSuccessfully = false;

    update(true);
    update(true);
}

void FileSystemImpl::update(bool forcedUpdate)
{
    bool cardInsertState = !digitalRead(sdDetectPin);

    if (cardInsertState == lastCardInsertState)
    {
        if (!cardInitializedSuccessfully && cardInsertState)
        {
            if (forcedUpdate || cardInsertTime + kCardInitDelayMs < timer->millis())
            {
                LOGE("SD card init.");
                cardInitializedSuccessfully = SD.init();

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
        LOGE("\nSD card inserted.");

        cardInsertTime = timer->millis();
        lastCardInsertState = true;
        cardInitializedSuccessfully = false;
    }
    else
    {
        LOGE("\nSD card ejected.");

        SD.stop();

        if (cardStateChangeCallback != nullptr)
        {
            cardStateChangeCallback(false);
        }

        lastCardInsertState = false;
    }
}

bool FileSystemImpl::isSdCardInserted()
{
    update();
    return lastCardInsertState && cardInitializedSuccessfully;
}

void FileSystemImpl::setMountEventCallback(CardStateChangeCallback funct)
{
    cardStateChangeCallback = funct;
}

bool FileSystemImpl::remount()
{
#ifdef FEATURE_USB_MSC
    // Drive mode ends (Interface::handleFileTransferMode): the drive withdraws the card from the computer and finishes the
    // command in flight, then FatFs forgets everything it knew (every open handle of the old mount is invalid) and the card
    // is initialised and mounted again. A card pulled in the mode has nothing to mount.
    ffUsbMscTakeBack();
    s_hostOwned = false;
    LOGEF("card back from the computer, %u calls refused", unsigned(s_refused));
    if (!lastCardInsertState)
    {
        return false;
    }
    SD.stop();
    cardInitializedSuccessfully = SD.init();
    return cardInitializedSuccessfully;
#else
    // Nothing else writes to the card here: this platform has no USB storage
    // mode.
    return false;
#endif
}

bool FileSystemImpl::exists(const char* path)
{
    CARD_GATE(false);
    return SD.exists(path);
}

bool FileSystemImpl::rename(const char* oldPath, const char* newPath)
{
    CARD_GATE(false);
    return SD.rename(oldPath, newPath);
}

bool FileSystemImpl::mkdir(uint8_t hidden, const char* path, bool pFlag)
{
    CARD_GATE(false);
    return SD.mkdir(hidden, path, pFlag);
}

bool FileSystemImpl::remove(const char* path)
{
    CARD_GATE(false);
    return SD.remove(path);
}

bool FileSystemImpl::removeDirWithFiles(const char* path)
{
    CARD_GATE(false);
    return SD.removeDirWithFiles(path);
}

bool FileSystemImpl::removeOnlyDirFiles(const char* path)
{
    CARD_GATE(false);
    return SD.removeOnlyDirFiles(path);
}

size_t FileSystemImpl::getFileSize(const char* path)
{
    CARD_GATE(0);
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
    CARD_GATE(0);
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
    CARD_GATE(0);
    assert(fileHandle);
    SdFile* file = (SdFile*)fileHandle;
    return file->available();
}

std::tuple<bool, FileSystemImpl::FileHandle> FileSystemImpl::openFile(const char* filepath,
                                                                      FileOpenMode openMode,
                                                                      FileHandle* fileHandle)
{
    CARD_GATE(std::make_tuple(false, fileHandle != nullptr ? *fileHandle : nullptr));

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
    CARD_GATE(false);
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
    CARD_GATE(false);
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

#ifdef FEATURE_USB_MSC
    // The computer owns the card: the pool slot is given back, the card is not touched (FatFs is unmounted, a FIL of the
    // old mount is never valid again)
    if (!refuseHost())
#endif
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
    CARD_GATE(false);
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
    CARD_GATE(false);
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
    CARD_GATE();
    assert(fileHandle != nullptr);
    SdFile* file = (SdFile*)fileHandle;

    file->end();
}

int32_t FileSystemImpl::readFile(void* destBuf, size_t destLen, FileHandle fileHandle)
{
    CARD_GATE(-1);
    assert(fileHandle != nullptr);
    SdFile* file = (SdFile*)fileHandle;

    assert(file->isOpen());

    return file->read(destBuf, destLen);
}

int32_t FileSystemImpl::writeFile(const void* data, size_t dataLen, FileHandle fileHandle)
{
    CARD_GATE(-1);
    assert(fileHandle != nullptr);
    SdFile* file = (SdFile*)fileHandle;

    assert(file->isOpen());

    return file->write(data, dataLen);
}

int FileSystemImpl::createInstrumentsList(const char* path,
                                          char** list,
                                          uint8_t list_length,
                                          uint32_t* sizeOut)
{
    CARD_GATE(kListError);
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
    CARD_GATE(0);
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
    CARD_GATE(kListError);
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
#ifdef FEATURE_USB_MSC
    if (refuseHost())
    {
        count = 0;
        return;
    }
#endif
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
    CARD_GATE(0);
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
    CARD_GATE(false);
    nesDir.close();
    return nesDir.open(path);
}

bool FileSystemImpl::nesDir_readItem(char* path, uint8_t* isDir, uint8_t* isHid, uint32_t* size)
{
    CARD_GATE(false);
    return nesDir.readItem(path, isDir, isHid, size);
}

void FileSystemImpl::nesDir_close()
{
    CARD_GATE();
    nesDir.close();
}
