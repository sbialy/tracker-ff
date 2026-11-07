// USB test console, MK66 side: the CDC serial port as transport, the key, pad and encoder drivers as input, the reset and
// fault records. Compiled to nothing without ENABLE_DEV_CONSOLE.
#include "UsbTest.h"

#if defined(ENABLE_DEV_CONSOLE) && !defined(PLATFORM_IMX)

#include "ButtonsImpl.h"
#include "EncoderImpl.h"
#include "PadsImpl.h"
#include "PlatformImpl.h"
#include "SD.h"
#include "kinetis.h"
#include "usb_dev.h"
#include "usb_serial.h"

extern PlatformImpl platform; // TrackerMain.cpp

#ifdef REBOOT_ON_FAULT
// Written by fault_isr (teensy_core/mk20dx128.c) just before it resets the MCU
extern "C" volatile uint32_t usbtest_fault_record[6];
#endif

namespace
{
// Packets of the shared pool (30, MIDI uses them too) the console may have queued for the host. TX_PACKET_LIMIT in
// usb_serial.c is 8, the console stops at 4 so a host that reads slowly cannot starve MIDI.
constexpr uint32_t kMaxPacketsInFlight = 4;
constexpr uint32_t kFaultMagic = 0xFA17C0DE;

uint8_t g_faultTaken;
uint32_t g_fault[4];
bool g_faultValid;
uint32_t g_resetStatus;
bool g_resetStatusTaken;
} // namespace

int UsbTestPlatform_RxByte()
{
    return usb_serial_getchar();
}

bool UsbTestPlatform_LinkUp()
{
    return usb_configuration != 0 && Serial.dtr() != 0;
}

size_t UsbTestPlatform_TxFree()
{
    if (usb_tx_packet_count(CDC_TX_ENDPOINT) >= kMaxPacketsInFlight)
    {
        return 0;
    }
    const int room = usb_serial_write_buffer_free();
    return room > 0 ? size_t(room) : 0;
}

size_t UsbTestPlatform_TxWrite(const uint8_t* data, size_t length)
{
    const size_t room = UsbTestPlatform_TxFree();
    if (length > room)
    {
        length = room;
    }
    if (length == 0)
    {
        return 0;
    }
    // never waits: at most the free part of the current packet
    return usb_serial_write(data, uint32_t(length)) < 0 ? 0 : length;
}

uint32_t UsbTestPlatform_IrqLock()
{
    uint32_t primask;
    __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(primask)::"memory");
    return primask;
}

void UsbTestPlatform_IrqUnlock(uint32_t state)
{
    __asm volatile("msr primask, %0" ::"r"(state) : "memory");
}

bool UsbTestPlatform_ValidPointer(const void* p)
{
    const uint32_t a = reinterpret_cast<uint32_t>(p);
    return a < 0x00100000u                                  // flash
           || (a >= 0x1FFF0000u && a < 0x20030000u)         // SRAM
           || (a >= 0x70000000u && a < 0x71000000u);        // SDRAM
}

bool UsbTestPlatform_InjectButton(uint8_t id, bool down)
{
    return static_cast<ButtonsImpl*>(platform.getButtons())->inject(id, down);
}

bool UsbTestPlatform_InjectPad(uint8_t id, bool down)
{
    return static_cast<PadsImpl*>(platform.getPads())->inject(id, down);
}

void UsbTestPlatform_InjectEncoder(int delta)
{
    ((EncoderImpl*)platform.getEncoder())->inject(delta);
}

bool UsbTestPlatform_InputPending()
{
    return static_cast<ButtonsImpl*>(platform.getButtons())->injectPending() ||
           static_cast<PadsImpl*>(platform.getPads())->injectPending() ||
           ((EncoderImpl*)platform.getEncoder())->injectPending();
}

// RCM_SRS1 << 8 | RCM_SRS0 as the last reset left them (read once, the first time anyone asks)
uint32_t UsbTestPlatform_ResetStatus()
{
    if (!g_resetStatusTaken)
    {
        g_resetStatusTaken = true;
        g_resetStatus = (uint32_t(RCM_SRS1) << 8) | RCM_SRS0;
    }
    return g_resetStatus;
}

bool UsbTestPlatform_FaultRecord(uint32_t* pc, uint32_t* lr, uint32_t* psr, uint32_t* cfsr)
{
#ifdef REBOOT_ON_FAULT
    if (g_faultTaken == 0)
    {
        g_faultTaken = 1;
        // the record is only believed with the magic and the check word, the RAM is not cleared at power up
        if (usbtest_fault_record[0] == kFaultMagic &&
            usbtest_fault_record[5] == ~(usbtest_fault_record[1] ^ usbtest_fault_record[0]))
        {
            g_faultValid = true;
            for (int i = 0; i < 4; i++)
            {
                g_fault[i] = usbtest_fault_record[1 + i];
            }
        }
        usbtest_fault_record[0] = 0;
    }
#endif
    if (g_faultValid)
    {
        *pc = g_fault[0];
        *lr = g_fault[1];
        *psr = g_fault[2];
        *cfsr = g_fault[3];
    }
    return g_faultValid;
}

// FatFs f_getfree: the free cluster count of the FSINFO sector, a card with a stale one makes it scan the FAT (seconds, main loop)
long UsbTestPlatform_FreeBytes()
{
    const uint64_t bytes = uint64_t(SD.freeClusterCount()) * SD.sectorsPerCluster() * 512u;
    if (bytes == 0)
    {
        return -1; // freeClusterCount() also returns 0 when f_getfree failed, so zero counts as unknown
    }
    return bytes > 0x7FFFFFFFull ? 0x7FFFFFFF : long(bytes);
}

#endif /* ENABLE_DEV_CONSOLE */
