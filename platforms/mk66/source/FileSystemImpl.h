#ifndef SOURCE_FILESYSTEMIMPL_H_
#define SOURCE_FILESYSTEMIMPL_H_

#include "Platform.h"
#include "SD.h"
#include <functional>
#include <tuple>

class FileSystemImpl final : public FileSystem
{
  public:
    static constexpr int kFileBufferSize = 1024;

    virtual bool isSdCardInserted() override;
    virtual void setMountEventCallback(CardStateChangeCallback funct) override;
    virtual bool remount() override;

    virtual bool exists(const char* path) override;
    virtual bool rename(const char* oldPath, const char* newPath) override;
    // todo: hidden to bool
    virtual bool mkdir(uint8_t hidden, const char* path, bool pFlag = true) override;
    virtual bool remove(const char* path) override;
    virtual bool removeDirWithFiles(const char* path) override;
    virtual bool removeOnlyDirFiles(const char* path) override;

    virtual size_t getFileSize(const char* path) override;
    virtual size_t getFileSize(FileHandle fileHandle) override; // file needs to be open
    virtual size_t getFileAvailable(FileHandle fileHandle) override;
    virtual std::tuple<bool, FileHandle> openFile(
      const char* filepath,
      FileOpenMode openMode = kRead,
      // non-null file handle means that fileHandle is already created
      FileHandle* fileHandle = nullptr) override;

    virtual int32_t readFile(void* destBuf, size_t bufLen, FileHandle file) override;
    virtual int32_t writeFile(const void* data, size_t dataSize, FileHandle fileHandle) override;
    // Seeking in cached files is not supported (and would be inefficient)
    virtual bool seekFileCur(int offset, FileHandle fileHandle) override;
    virtual bool seekFile(uint32_t pos, FileHandle fileHandle) override;
    virtual void closeFile(FileHandle fileHandle, bool deleteOnClose = true) override;
    virtual void endFile(FileHandle file) override;
    virtual bool isFileOpen(FileHandle file) override;
    virtual bool enableFastSeek(FileHandle fileHandle,
                                uint32_t* clmtBuffer,
                                uint32_t clmtElementCount,
                                uint32_t* outDatabaseSector = nullptr,
                                uint32_t* outSectorsPerCluster = nullptr) override;
    virtual bool readSectorsAsync(uint8_t* buffer, uint32_t startSector, uint32_t sectorCount) override;
    virtual bool pollAsyncRead(bool* outSuccess = nullptr) override;
    virtual bool waitAsyncRead() override;
    virtual bool isAsyncReadBusy() override;

    virtual int createInstrumentsList(const char* path,
                                      char** list,
                                      uint8_t list_length,
                                      uint32_t* sizeOut) override;
    virtual int countContainedFiles(const char* path) override;

    virtual int createFilesList(const char* path,
                                uint8_t start_line,
                                char** list,
                                uint8_t list_length,
                                uint16_t max_used_memory,
                                enListFileFilter filter = enListFileFilter_all,
                                uint16_t skip_count = 0,
                                uint16_t max_files_in_subdir = 0,
                                enListFileFilter folderSizeFilter = enListFileFilter_all) override;

    virtual void listDirContents(const char* dirPath,
                                 DirContentType contentType,
                                 std::function<bool(const char* name)> nameFilter,
                                 char* namesBuffer,
                                 int namesBufferSize,
                                 int maxNameLen,
                                 int& outNamesCount,
                                 char** fileList) override;

    virtual int readNbFiles(const char* romPath) override;

    virtual bool nesDir_open(const char* path) override;
    virtual bool nesDir_readItem(char* path,
                                 uint8_t* isDir,
                                 uint8_t* isHid,
                                 uint32_t* size) override;
    virtual void nesDir_close() override;

    void init(int sdDetectPin, Timer* timer);
    void update(bool forcedUpdate = false);

  private:
    static constexpr int kCardInitDelayMs = 500;

    SdDir nesDir;
    CardStateChangeCallback cardStateChangeCallback = nullptr;
    int sdDetectPin;
    Timer* timer;
    bool lastCardInsertState;
    bool cardInitializedSuccessfully;
    uint32_t cardInsertTime;
};

#endif /* SOURCE_FILESYSTEMIMPL_H_ */
