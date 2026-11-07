#ifndef SOURCE_USBTESTFLASH_H_
#define SOURCE_USBTESTFLASH_H_

// USB test console, firmware update checks: a streaming validator for the Intel HEX .ptf images of the original Tracker (MK66)
// and the rules for a file name in /Firmware. Portable, knows nothing about the Tracker. The rules mirror
// synth-open/tools/check_ptf.py and platforms/mk66/3rdparty/SD/SD.cpp (enListFileFilter_firmware), the host side copy is
// synth-open/tools/usbtest/ptfcheck.py (usbtest_flash_test.cpp checks that both give the same verdicts). Protocol:
// docs/usb-test-console.md. Compiled only with ENABLE_DEV_CONSOLE.

#include <stddef.h>
#include <stdint.h>

#if defined(ENABLE_DEV_CONSOLE) && !defined(PLATFORM_IMX)

namespace usbtest
{
constexpr uint32_t kPtfFlashLimit = 0xFF000;   // every data byte below it, the top sector stays untouched
constexpr uint32_t kPtfMinFileBytes = 4096;    // a real image is a few MB of hex text
constexpr uint32_t kPtfMaxFileBytes = 6u << 20;
constexpr size_t kFirmwareNameMax = 31;        // the bootloader reads the name into char[32]
constexpr size_t kFirmwareNameMin = 15;        // Config > Firmware lists nothing shorter
constexpr uint32_t kFirmwareSpareBytes = 1u << 20; // free space wanted on top of the file itself

enum PtfError : uint8_t
{
    kPtfOk,
    kPtfEmpty,         // no data records
    kPtfNotHex,        // the first thing in the file is not a record (another platform's format, or not an image)
    kPtfBadChar,       // not a hex digit inside a record, or text between records
    kPtfBadLength,     // a record longer than its length byte says, or an odd count of digits
    kPtfBadChecksum,
    kPtfRecordType,    // anything but 00 data, 01 end of file, 02 segment address (stock images use nothing else)
    kPtfTruncated,     // the file ends inside a record
    kPtfNoEof,         // no end of file record
    kPtfAfterEof,      // text after the end of file record
    kPtfBeyondLimit,   // data at or above kPtfFlashLimit
    kPtfFlashConfig,   // the flash configuration field (0x400) is not the stock one: risk of locking the MCU
    kPtfBadSp,         // initial stack pointer outside the MK66 SRAM: an image of another chip
    kPtfBadReset,      // reset vector is not a Thumb address inside the image
    kPtfStartAddress,  // the image does not start at address 0
};

const char* ptfErrorName(PtfError error);

// Feed the file in any chunking, call finish() at the end. Once an error is latched feed() ignores the rest.
class PtfValidator
{
  public:
    void begin(uint32_t limit = kPtfFlashLimit);
    void feed(const uint8_t* data, size_t length);
    PtfError finish();

    PtfError error() const { return error_; }
    uint32_t errorRecord() const { return record_; } // 1 based record number the error belongs to (0 before the first)
    uint32_t top() const { return top_; }            // highest data address + 1
    uint32_t stackPointer() const;
    uint32_t resetVector() const;
    uint32_t records() const { return record_; }

  private:
    enum State : uint8_t
    {
        kWaitColon,
        kHighNibble,
        kLowNibble,
        kDone, // after the end of file record
    };

    void fail(PtfError error)
    {
        if (error_ == kPtfOk)
        {
            error_ = error;
        }
    }
    void byte(uint8_t b);
    void dataByte(uint8_t b);
    void endRecord();

    State state_;
    PtfError error_;
    uint8_t digit_;       // the high nibble while waiting for the low one
    uint8_t position_;    // byte index inside the record
    uint8_t count_;
    uint8_t type_;
    uint8_t sum_;
    uint16_t addr_;
    uint32_t upper_;      // segment address << 4
    uint32_t record_;
    uint32_t limit_;
    uint32_t top_;
    uint32_t low_;
    bool eof_;
    bool any_;
    uint8_t segment_[2];
    uint8_t vectors_[8];  // 0x00..0x07: initial SP and reset vector
    uint8_t config_[16];  // 0x400..0x40F
};

enum NameError : uint8_t
{
    kNameOk,
    kNameTooShort,
    kNameTooLong,
    kNameNotPtf,
    kNameNoPrefix,  // Config > Firmware only lists names that start with PolyendTrac
    kNameBadChar,
};
const char* nameErrorName(NameError error);

// A name as it is written to /Firmware and into _fwinfo. Without `listed` only the characters are checked (the temp name).
NameError firmwareNameError(const char* name, bool listed);

} // namespace usbtest

#endif /* ENABLE_DEV_CONSOLE */

#endif /* SOURCE_USBTESTFLASH_H_ */
