#ifndef FILEMANAGER_FILEMANAGEROPERATION_H_
#define FILEMANAGER_FILEMANAGEROPERATION_H_

#include <stdint.h>

class cFileManager;

class FileSystem;

class FileManagerOperation
{
  public:
    virtual ~FileManagerOperation() {}

    static void init(FileSystem* fileSystem);
    virtual void update() = 0;
    virtual float getProgress() { return 1.0f; };

    enum SampleChannelMode
    {
        kLeft,
        kRight,
        kLeftAndRight,
    };

    enum class SampleLoadMode : uint8_t
    {
        kRam = 0,
        kAuto = 1,
        kStream = 2,
    };

    struct SampleLoadData
    {
        char* filePath;
        char* fileName;
        bool ptiFile;
        uint32_t fileOffset;

        uint16_t sourceFormat;
        uint16_t sourceChannelCount;
        uint16_t sourceBitsPerSample;
        uint32_t sourceSampleRate;

        uint32_t
          sourceAllChannelBytesPerSample; // bytes per one sample (of sample) [multiply channels]
        uint32_t sourceBytesPerSec;
        uint32_t sourceTotalSamplesCountPerChannel;

        bool highQualitySource;
        bool conversion;

        uint16_t destChannelCount;
        uint32_t destSampleCountPerChannel;
        bool shouldStream;
    };

  protected:
    friend cFileManager;

    static FileSystem* fileSystem;

    void stopOperationWithError(uint8_t errorId);
};

#endif /* FILEMANAGER_FILEMANAGEROPERATION_H_ */
