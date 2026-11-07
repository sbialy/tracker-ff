#include "UsbTestCore.h"

#if defined(ENABLE_DEV_CONSOLE) && !defined(PLATFORM_IMX)

#include <string.h>

// Firmware update through the console: fwcheck, fwpromote and flash. The three commands share one task (kTaskFw) that reads the
// file from the card, computes the CRC32 and runs the .ptf validator over it. Only `flash` ever touches the bootloader, and only
// after the file passed, matches the confirm token, nothing is playing and a short delay let the reply out. Docs: docs/usb-test-console.md.

namespace usbtest
{
namespace
{
constexpr uint32_t kHandoffDelayMs = 250; // the ok line leaves the ring before the bootloader takes over
constexpr char kFirmwareDir[] = "/Firmware/";
constexpr uint32_t kSramLow = 0x1FFF0000; // MK66 SRAM
constexpr uint32_t kSramHigh = 0x20030000;

enum FwMode : uint8_t
{
    kFwCheck,
    kFwPromote,
    kFwFlash,
};

const uint8_t kStockConfig[16] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                   0xFF, 0xFF, 0xFF, 0xFF, 0xDE, 0xF9, 0xFF, 0xFF }; // FSEC 0xDE, FOPT 0xF9

int hexValue(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

// "0x" and exactly eight hex digits. Strict on purpose: the token is the proof that a person (or the runner) meant this file.
bool parseToken(const char* s, uint32_t* out)
{
    if (s[0] != '0' || (s[1] != 'x' && s[1] != 'X'))
    {
        return false;
    }
    uint32_t v = 0;
    int digits = 0;
    for (const char* p = s + 2; *p != 0; p++, digits++)
    {
        const int d = hexValue(*p);
        if (d < 0 || digits >= 8)
        {
            return false;
        }
        v = (v << 4) | uint32_t(d);
    }
    if (digits != 8)
    {
        return false;
    }
    *out = v;
    return true;
}

uint32_t load32le(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

void copyName(char* dest, size_t size, const char* src)
{
    size_t i = 0;
    for (; i + 1 < size && src[i] != 0; i++)
    {
        dest[i] = src[i];
    }
    dest[i] = 0;
}

bool joinFirmwarePath(char* out, size_t size, const char* name)
{
    const size_t dir = sizeof(kFirmwareDir) - 1;
    const size_t n = strlen(name);
    if (dir + n + 1 > size)
    {
        return false;
    }
    memcpy(out, kFirmwareDir, dir);
    memcpy(out + dir, name, n + 1);
    return true;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
const char* ptfErrorName(PtfError error)
{
    switch (error)
    {
        case kPtfOk: return "ok";
        case kPtfEmpty: return "empty";
        case kPtfNotHex: return "not-intel-hex";
        case kPtfBadChar: return "bad-char";
        case kPtfBadLength: return "bad-length";
        case kPtfBadChecksum: return "bad-checksum";
        case kPtfRecordType: return "record-type";
        case kPtfTruncated: return "truncated";
        case kPtfNoEof: return "no-eof";
        case kPtfAfterEof: return "after-eof";
        case kPtfBeyondLimit: return "beyond-limit";
        case kPtfFlashConfig: return "flash-config";
        case kPtfBadSp: return "bad-sp";
        case kPtfBadReset: return "bad-reset";
        case kPtfStartAddress: return "start-address";
    }
    return "?";
}

const char* nameErrorName(NameError error)
{
    switch (error)
    {
        case kNameOk: return "ok";
        case kNameTooShort: return "name-too-short";
        case kNameTooLong: return "name-too-long";
        case kNameNotPtf: return "name-not-ptf";
        case kNameNoPrefix: return "name-no-prefix";
        case kNameBadChar: return "name-bad-char";
    }
    return "?";
}

NameError firmwareNameError(const char* name, bool listed)
{
    const size_t length = strlen(name);
    if (length == 0)
    {
        return kNameTooShort;
    }
    if (length > kFirmwareNameMax)
    {
        return kNameTooLong;
    }
    for (size_t i = 0; i < length; i++)
    {
        const char c = name[i];
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' ||
                        c == '_' || c == '-';
        if (!ok)
        {
            return kNameBadChar;
        }
    }
    if (!listed)
    {
        return kNameOk;
    }
    if (length < kFirmwareNameMin)
    {
        return kNameTooShort;
    }
    if (strcmp(name + length - 4, ".ptf") != 0)
    {
        return kNameNotPtf;
    }
    if (strncmp(name, "PolyendTrac", 11) != 0)
    {
        return kNameNoPrefix;
    }
    return kNameOk;
}

// ---------------------------------------------------------------------------------------------------------------------
void PtfValidator::begin(uint32_t limit)
{
    memset(this, 0, sizeof(*this));
    limit_ = limit;
    low_ = 0xFFFFFFFFu;
    memset(vectors_, 0xFF, sizeof(vectors_));
    memset(config_, 0xFF, sizeof(config_));
    state_ = kWaitColon;
}

uint32_t PtfValidator::stackPointer() const
{
    return load32le(vectors_);
}

uint32_t PtfValidator::resetVector() const
{
    return load32le(vectors_ + 4);
}

void PtfValidator::feed(const uint8_t* data, size_t length)
{
    for (size_t i = 0; i < length && error_ == kPtfOk; i++)
    {
        const char c = char(data[i]);
        const bool space = c == ' ' || c == '\t' || c == '\r' || c == '\n';
        switch (state_)
        {
            case kWaitColon:
                if (c == ':')
                {
                    state_ = kHighNibble;
                    position_ = 0;
                    sum_ = 0;
                    record_++;
                }
                else if (!space)
                {
                    // a digit straight after a record means the record is longer than its length byte says
                    fail(record_ == 0 ? kPtfNotHex : (hexValue(c) >= 0 ? kPtfBadLength : kPtfBadChar));
                }
                break;
            case kHighNibble:
            {
                const int v = hexValue(c);
                if (v < 0)
                {
                    fail(space || c == ':' ? kPtfBadLength : kPtfBadChar);
                }
                else
                {
                    digit_ = uint8_t(v);
                    state_ = kLowNibble;
                }
                break;
            }
            case kLowNibble:
            {
                const int v = hexValue(c);
                if (v < 0)
                {
                    fail(space || c == ':' ? kPtfBadLength : kPtfBadChar);
                }
                else
                {
                    state_ = kHighNibble;
                    byte(uint8_t((digit_ << 4) | uint8_t(v)));
                }
                break;
            }
            case kDone:
                if (!space)
                {
                    fail(kPtfAfterEof);
                }
                break;
        }
    }
}

void PtfValidator::byte(uint8_t b)
{
    sum_ = uint8_t(sum_ + b);
    if (position_ == 0)
    {
        count_ = b;
    }
    else if (position_ == 1)
    {
        addr_ = uint16_t(b << 8);
    }
    else if (position_ == 2)
    {
        addr_ = uint16_t(addr_ | b);
    }
    else if (position_ == 3)
    {
        type_ = b;
        if (b > 2)
        {
            fail(kPtfRecordType);
        }
        else if ((b == 1 && count_ != 0) || (b == 2 && count_ != 2))
        {
            fail(kPtfBadLength);
        }
    }
    else if (position_ < 4u + count_)
    {
        dataByte(b);
    }
    else
    {
        if (sum_ != 0)
        {
            fail(kPtfBadChecksum);
        }
        else
        {
            endRecord();
        }
        return;
    }
    position_++;
}

void PtfValidator::dataByte(uint8_t b)
{
    const uint32_t index = uint32_t(position_ - 4);
    if (type_ == 2)
    {
        segment_[index] = b;
        if (index == 1)
        {
            upper_ = ((uint32_t(segment_[0]) << 8) | segment_[1]) << 4;
        }
        return;
    }
    if (type_ != 0)
    {
        return;
    }
    const uint32_t address = upper_ + addr_ + index;
    if (address >= limit_)
    {
        fail(kPtfBeyondLimit);
        return;
    }
    any_ = true;
    if (address < low_)
    {
        low_ = address;
    }
    if (address + 1 > top_)
    {
        top_ = address + 1;
    }
    if (address < sizeof(vectors_))
    {
        vectors_[address] = b;
    }
    else if (address >= 0x400 && address < 0x400 + sizeof(config_))
    {
        config_[address - 0x400] = b;
    }
}

void PtfValidator::endRecord()
{
    if (type_ == 1)
    {
        eof_ = true;
        state_ = kDone;
    }
    else
    {
        state_ = kWaitColon;
    }
}

PtfError PtfValidator::finish()
{
    if (error_ != kPtfOk)
    {
        return error_;
    }
    if (state_ == kHighNibble || state_ == kLowNibble)
    {
        fail(kPtfTruncated);
    }
    else if (record_ == 0)
    {
        fail(kPtfEmpty);
    }
    else if (!eof_)
    {
        fail(kPtfNoEof);
    }
    else if (!any_)
    {
        fail(kPtfEmpty);
    }
    else if (low_ != 0)
    {
        fail(kPtfStartAddress); // before the vectors and the flash configuration: a file that starts elsewhere has neither
    }
    else if (memcmp(config_, kStockConfig, sizeof(config_)) != 0)
    {
        fail(kPtfFlashConfig);
    }
    else if (!(stackPointer() > kSramLow && stackPointer() <= kSramHigh))
    {
        fail(kPtfBadSp);
    }
    else if ((resetVector() & 1u) == 0 || (resetVector() & ~1u) < 0x40 || (resetVector() & ~1u) >= top_)
    {
        fail(kPtfBadReset);
    }
    return error_;
}

// ---------------------------------------------------------------------------------------------------------------------
// fwcheck                       -> ok fwinfo free=<bytes|unknown> limit=0xff000 namemax=31 filemax=<bytes>
// fwcheck <name>                -> ok fwcheck size= crc= top= sp= reset= listed=<0|1>      (read only)
// fwpromote <tmp> <name> <crc>  -> ok fwpromote <name> size= crc=       (re-reads <tmp>, rename into /Firmware/<name>)
// flash <name> <crc>            -> ok flash handoff <name> size= crc=, then the bootloader takes over
void Console::cmdFw(int argc, char** argv)
{
    const char* cmd = argv[0];
    const uint8_t mode = strcmp(cmd, "flash") == 0 ? kFwFlash : (strcmp(cmd, "fwpromote") == 0 ? kFwPromote : kFwCheck);
    if (mode == kFwCheck && argc == 1)
    {
        const long freeBytes = host_->freeBytes();
        if (freeBytes > 0)
        {
            reply("ok fwinfo free=%ld limit=0x%lx namemax=%d filemax=%lu", freeBytes,
                  static_cast<unsigned long>(kPtfFlashLimit), int(kFirmwareNameMax),
                  static_cast<unsigned long>(kPtfMaxFileBytes));
        }
        else
        {
            reply("ok fwinfo free=unknown limit=0x%lx namemax=%d filemax=%lu",
                  static_cast<unsigned long>(kPtfFlashLimit), int(kFirmwareNameMax),
                  static_cast<unsigned long>(kPtfMaxFileBytes));
        }
        return;
    }
    const int want = mode == kFwCheck ? 2 : (mode == kFwPromote ? 4 : 3);
    uint32_t token = 0;
    if (argc != want || (mode != kFwCheck && !parseToken(argv[argc - 1], &token)))
    {
        if (mode == kFwCheck)
        {
            reply("err usage: fwcheck [<name in /Firmware>]");
        }
        else if (mode == kFwPromote)
        {
            reply("err usage: fwpromote <tmp name> <name> 0x<crc32 of the file>");
        }
        else
        {
            reply("err flash: needs the confirm token: flash <name> 0x<crc32 of the file>");
        }
        return;
    }
    if (host_->playing() || host_->rendering() || host_->fileManagerStatus() != 0)
    {
        reply("err %s refused while playing, rendering or loading", cmd);
        return;
    }
    const char* source = argv[1];
    const char* target = mode == kFwPromote ? argv[2] : argv[1];
    NameError nameError = firmwareNameError(source, mode == kFwFlash);
    if (nameError == kNameOk && mode == kFwPromote)
    {
        nameError = firmwareNameError(target, true);
        if (nameError == kNameOk && strcmp(source, target) == 0)
        {
            nameError = kNameBadChar;
        }
    }
    if (nameError != kNameOk)
    {
        reply("err %s %s", cmd, nameErrorName(nameError));
        return;
    }
    if (!joinFirmwarePath(path_, sizeof(path_), source))
    {
        reply("err %s %s", cmd, nameErrorName(kNameTooLong));
        return;
    }
    copyName(name_, sizeof(name_), target);
    const long size = host_->fileSize(path_);
    if (size < 0)
    {
        reply("err %s no such file", cmd);
        return;
    }
    if (size < long(kPtfMinFileBytes) || size > long(kPtfMaxFileBytes))
    {
        reply("err %s size %ld outside %lu..%lu", cmd, size, static_cast<unsigned long>(kPtfMinFileBytes),
              static_cast<unsigned long>(kPtfMaxFileBytes));
        return;
    }
    file_ = host_->fileOpenRead(path_);
    if (file_ == nullptr)
    {
        reply("err %s cannot open", cmd);
        return;
    }
    task_ = kTaskFw;
    taskStartMs_ = taskLastMs_ = now_;
    fwMode_ = mode;
    fwPhase_ = 0;
    fileSize_ = uint32_t(size);
    fileDone_ = 0;
    fileCrc_ = 0;
    fileWantCrc_ = token;
    ptf_.begin();
}

void Console::stepFw(uint32_t now)
{
    const char* cmd = fwMode_ == kFwFlash ? "flash" : (fwMode_ == kFwPromote ? "fwpromote" : "fwcheck");
    if (fwPhase_ == 1)
    {
        // hand-off armed, the ok line is out: one more look at the state, then the bootloader
        if (now - taskStartMs_ < kHandoffDelayMs)
        {
            return;
        }
        if (host_->playing() || host_->rendering() || host_->fileManagerStatus() != 0)
        {
            finishTask();
            reply("err flash cancelled, playing or loading started during the hand-off");
            return;
        }
        txPump();
        finishTask();
        host_->runBootloader(name_); // does not return when it works
        reply("err flash: the bootloader hand-off failed (/Firmware/_fwinfo not written)");
        return;
    }

    uint8_t* chunk = reinterpret_cast<uint8_t*>(host_->scratch());
    for (int i = 0; i < 16; i++)
    {
        const int n = host_->fileRead(file_, chunk, kChunk);
        if (n < 0)
        {
            host_->fileClose(file_);
            finishTask();
            reply("err %s read failed at %lu", cmd, static_cast<unsigned long>(fileDone_));
            return;
        }
        if (n > 0)
        {
            fileCrc_ = crc32(fileCrc_, chunk, size_t(n));
            ptf_.feed(chunk, size_t(n));
            fileDone_ += uint32_t(n);
            continue;
        }
        host_->fileClose(file_);
        file_ = nullptr;
        if (fileDone_ != fileSize_)
        {
            finishTask();
            reply("err %s short read %lu of %lu", cmd, static_cast<unsigned long>(fileDone_),
                  static_cast<unsigned long>(fileSize_));
            return;
        }
        const PtfError error = ptf_.finish();
        if (error != kPtfOk)
        {
            finishTask();
            reply("err %s ptf %s record=%lu", cmd, ptfErrorName(error), static_cast<unsigned long>(ptf_.errorRecord()));
            return;
        }
        if (fwMode_ != kFwCheck && fileCrc_ != fileWantCrc_)
        {
            finishTask();
            reply("err %s crc %08lx, the token says %08lx", cmd, static_cast<unsigned long>(fileCrc_),
                  static_cast<unsigned long>(fileWantCrc_));
            return;
        }
        fwDone(cmd);
        return;
    }
}

void Console::fwDone(const char* cmd)
{
    if (fwMode_ == kFwCheck)
    {
        const bool listed = firmwareNameError(path_ + sizeof(kFirmwareDir) - 1, true) == kNameOk;
        finishTask();
        reply("ok fwcheck size=%lu crc=%08lx top=0x%lx sp=0x%08lx reset=0x%08lx listed=%d",
              static_cast<unsigned long>(fileSize_), static_cast<unsigned long>(fileCrc_),
              static_cast<unsigned long>(ptf_.top()), static_cast<unsigned long>(ptf_.stackPointer()),
              static_cast<unsigned long>(ptf_.resetVector()), listed ? 1 : 0);
    }
    else if (fwMode_ == kFwPromote)
    {
        char finalPath[sizeof(path_)];
        const bool built = joinFirmwarePath(finalPath, sizeof(finalPath), name_);
        finishTask();
        // The old file of that name goes first (FatFs does not rename over it). The checked copy stays in the temp file until the
        // rename worked, so a failure here leaves a good file on the card.
        if (!built || (host_->fileSize(finalPath) >= 0 && !host_->fileRemove(finalPath)) ||
            !host_->fileRename(path_, finalPath))
        {
            reply("err %s rename failed, the checked copy is still %s", cmd, path_);
            return;
        }
        if (host_->fileSize(finalPath) != long(fileSize_))
        {
            reply("err %s size after the rename is %ld", cmd, host_->fileSize(finalPath));
            return;
        }
        reply("ok fwpromote %s size=%lu crc=%08lx", name_, static_cast<unsigned long>(fileSize_),
              static_cast<unsigned long>(fileCrc_));
    }
    else
    {
        fwPhase_ = 1;
        taskStartMs_ = now_;
        reply("ok flash handoff %s size=%lu crc=%08lx", name_, static_cast<unsigned long>(fileSize_),
              static_cast<unsigned long>(fileCrc_));
    }
}

} // namespace usbtest

#endif /* ENABLE_DEV_CONSOLE */
