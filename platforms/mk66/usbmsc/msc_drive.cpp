// tracker-ff USB drive (USB_MSC=1, -DFEATURE_USB_MSC), the platform side of Drive mode. See msc_drive.h.
//
// Round 1 (phases 0 and 1 of ref/usb-mass-storage-feasibility.md): the boot flag, the card hand-over and the take-back.
// Round 1b: the USB side. The endpoint driver (teensy_core/usb_msc_impl.h, included by usb_dev.c) moves the packets by DMA
// between the controller and the buffers of the Bot (msc_bot.cpp), the Pump (msc_pump.cpp) runs it from the main loop against
// FileSystemImpl::hostRead/WriteBlocks. Fail closed: the interface exists only when the boot flag was on at start, the card is
// presented only between enter() and the take-back (a computer sees "medium not present" the rest of the time), and the Bot
// has no way to the card but the lent block calls.
#include "msc_drive.h"

#include "FileSystemImpl.h"
#include "PlatformImpl.h"
#include "UsbImpl.h"
#include "avr_functions.h" // eeprom_read_byte / eeprom_write_byte, usable before main (eeprom.c initialises itself)
#include "cr_section_macros.h"
#include "kinetis.h"
#include "log.h"
#include "msc_bot.h"
#include "msc_endpoints.h"
#include "msc_pump.h"
#include "usb_desc.h"
#include "usb_dev.h"
#include "usb_msc.h"
#include "wiring.h"

#include <new>

#ifndef FF_USB_MSC
#error "FEATURE_USB_MSC needs the FF_USB_MSC descriptor (usb_desc.h, USB_MIDI)"
#endif

extern PlatformImpl platform;

namespace
{
// EEPROM: byte 7 of the 9 spare bytes between the start state (byte 0) and the config (byte 10, platforms/mk66/config/config.h,
// byte 8 is the power return mirror, byte 9 the USB audio flag). Read only when the config id is valid, so a blank or foreign
// EEPROM is off.
constexpr uint32_t kFlagAddr = 7;
constexpr uint8_t kFlagOn = 0x5A;
constexpr uint32_t kConfigIdAddr = 10;

FileSystemImpl* fs()
{
    return static_cast<FileSystemImpl*>(platform.getFileSystem());
}

constexpr uint32_t kRingBytes = ot::msc::Bot::kChunk * ot::msc::Bot::kMaxChunks; // 16 KB: four chunks of one card request each

// The card for the computer: only through the lent block calls of FileSystemImpl (refused unless the computer owns the card)
class Card final : public ot::msc::IBlockDevice
{
  public:
    bool read(uint32_t lba, uint8_t* dst, uint32_t count) override { return fs()->hostReadBlocks(lba, dst, count); }
    bool write(uint32_t lba, const uint8_t* src, uint32_t count) override { return fs()->hostWriteBlocks(lba, src, count); }
};

class DeviceEnv final : public ff::mscdrive::Env
{
  public:
    uint32_t millis() override { return ::millis(); }
    uint32_t micros() override { return ::micros(); }
    void delayUs(uint32_t us) override { delayMicroseconds(us); }
    void usbMask() override { NVIC_DISABLE_IRQ(IRQ_USBOTG); }
    void usbUnmask() override { NVIC_ENABLE_IRQ(IRQ_USBOTG); }
    void kick() override { usb_msc_kick(); }
    bool cardIn() override { return fs()->cardIn(); }
};

// No static constructors and no atexit registrations (the virtual destructors of the interfaces would add both): the objects are
// built in place by init(), only when the drive was enumerated, so an image with the drive off runs none of it. The ring is an
// uninitialised array in the SDRAM section (a constructor on an SDRAM global hung the boot once, f563f27).
struct Drive
{
    Drive() : pump(bot, ep, card, env) {}
    ff::mscdrive::UsbEndpoints ep;
    Card card;
    DeviceEnv env;
    ot::msc::Bot bot;
    ff::mscdrive::Pump pump;
};
alignas(Drive) uint8_t g_driveMem[sizeof(Drive)];
Drive* volatile g_drive; // null until init(): no drive, every entry below does nothing
// CBW 64 B, CSW 16 B, short replies 64 B: internal RAM, the USB controller reaches all of it
uint8_t g_small[ot::msc::Bot::kSmallBytes] __attribute__((aligned(4)));
// The data stage of READ(10) / WRITE(10): the USB controller and the SD host both DMA into and out of it. SDRAM (the FlexBus is a
// crossbar slave, uncached), read by the Bot only while the medium is presented. Device row W3 proves the USB DMA reaches it.
__NOINIT(EXTERNAL_RAM) uint8_t g_ring[kRingBytes] __attribute__((aligned(32)));

void isrHook(uint8_t event)
{
    Drive* d = g_drive;
    if (d != nullptr)
    {
        d->pump.isrEvent(event);
    }
}
} // namespace

extern "C" {
int ffUsbMscBootFlag(void)
{
    static const char kId[] = "EEPROM"; // mtConfig.header.id with its terminator, as PlatformImpl::configValid
    for (uint32_t i = 0; i < sizeof kId; i++)
    {
        if (eeprom_read_byte(reinterpret_cast<const uint8_t*>(kConfigIdAddr + i)) != uint8_t(kId[i])) return 0;
    }
    return eeprom_read_byte(reinterpret_cast<const uint8_t*>(kFlagAddr)) == kFlagOn;
}

void ffUsbMscSetBootFlag(int on)
{
    const uint8_t v = on ? kFlagOn : 0;
    if (eeprom_read_byte(reinterpret_cast<const uint8_t*>(kFlagAddr)) != v)
    {
        eeprom_write_byte(reinterpret_cast<uint8_t*>(kFlagAddr), v); // one write per change
    }
}

int ffUsbMscActive(void)
{
    return ff_usb_msc_on;
}

void ffUsbMscTakeBack(void)
{
    // FileSystemImpl::remount: the medium goes (NOT READY 3Ah from the next command on), the command in flight finishes or is
    // dropped (the computer gone: 100 ms without frames, a slow card: 3 s)
    Drive* d = g_drive;
    if (d != nullptr)
    {
        d->pump.end();
    }
}
}

namespace ff
{
namespace mscdrive
{
void init()
{
    if (!ff_usb_msc_on)
    {
        return; // the interface is not enumerated, nothing to answer
    }
    if (g_drive != nullptr)
    {
        return;
    }
    Drive* d = new (g_driveMem) Drive();
    d->bot.init(&d->ep, g_ring, sizeof g_ring, g_small);
    g_drive = d;
    // From now on the computer's commands are answered: NOT READY "medium not present" until enter() presents the card
    usb_msc_set_hook(&isrHook);
}

bool enter()
{
    if (!ff_usb_msc_on)
    {
        return false;
    }
    if (!fs()->lendToHost())
    {
        return false;
    }
    // Lent: the firmware is out of the card. If the drive cannot present it (no blocks, or init() did not run), the mode stays on
    // with the medium absent and the user leaves it with Exit.
    if (g_drive == nullptr || !g_drive->pump.begin(fs()->hostBlockCount()))
    {
        LOGE("USB drive: card lent but not presented");
        return false;
    }
    LOGEF("USB drive: card presented, %u blocks", unsigned(fs()->hostBlockCount()));
    return true;
}

bool active()
{
    return fs()->hostOwned();
}

void update()
{
    Drive* d = g_drive;
    if (d != nullptr)
    {
        d->pump.service();
    }
}
} // namespace mscdrive
} // namespace ff

// Usb (core/include/Platform.h) on the original: only the storage class does anything, and only with USB_MSC=1
void UsbImpl::setClassState(Usb::ClassType type, bool enable)
{
    if (type == Usb::ClassType::kStorage && enable)
    {
        ff::mscdrive::enter();
    }
    // Off: the core's remount() already took the card back (FileSystemImpl::remount, ffUsbMscTakeBack)
}

bool UsbImpl::getClassState(Usb::ClassType type)
{
    return type == Usb::ClassType::kStorage && ff::mscdrive::active();
}

bool PlatformImpl::usbDriveRunning()
{
    return ffUsbMscActive() != 0;
}

void PlatformImpl::setUsbDriveBoot(bool on)
{
    ffUsbMscSetBootFlag(on ? 1 : 0);
}
