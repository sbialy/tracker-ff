// tracker-ff USB drive (USB_MSC=1, -DFEATURE_USB_MSC): the main loop side of the Bulk-Only unit, free of platform headers so the
// host harness (usbmsc/test/fs_host/s5_usb_e2e.*) runs this very file with the real usb_dev.c under it. Adapted from OpenTracker
// (src/hal/usb_storage.cpp 927b1516385e, Apache-2.0, see licenses/LICENSE-OpenTracker-Apache-2.0.txt): the host watch, the 8 ms
// block I/O pass and the bounded wait on exit are theirs, the card hand-over (FileSystemImpl) is ours.
//
// Threads: isrEvent() runs in the USB interrupt (the hook of usb_msc.h), everything else in the main loop with the interrupt
// masked around each touch of the Bot state (Env::usbMask / usbUnmask).
#pragma once

#include "msc_bot.h"

#include <stdint.h>

namespace ff
{
namespace mscdrive
{
// What the pump needs from the platform (device: msc_drive.cpp, host harness: the simulated clock and controller).
class Env
{
  public:
    virtual ~Env() = default;
    virtual uint32_t millis() = 0;
    virtual uint32_t micros() = 0;
    virtual void delayUs(uint32_t us) = 0;
    virtual void usbMask() = 0;   // the USB interrupt off (nests are not needed)
    virtual void usbUnmask() = 0;
    virtual void kick() = 0;      // usb_msc_kick: run the hook now, after block I/O finished
    virtual bool cardIn() = 0;    // the card detect pin, not FatFs
};

class Pump
{
  public:
    Pump(ot::msc::Bot& bot, ot::msc::IEndpoints& ep, ot::msc::IBlockDevice& dev, Env& env) : bot_(bot), ep_(ep), dev_(dev), env_(env) {}

    // USB interrupt: the hook of usb_msc.h (USB_MSC_EVENT_RESET = 1, else a token or a frame)
    void isrEvent(uint8_t event);

    // Main loop. begin(): present the card (blocks of 512 B) with UNIT ATTENTION. False with no blocks.
    bool begin(uint32_t blocks);
    // The block I/O pass, the host watch and the card detect. Cheap when no drive session runs.
    void service();
    // The medium goes away (NOT READY 3Ah from the next command on), the command in flight is finished or dropped. Bounded: a
    // computer that is gone ends it at once, a slow card after 3 s. Nothing of the drive touches the card after this returns.
    void end();

    bool active() const { return active_; }
    bool mediumPresented() const { return bot_.mediumPresent(); }
    uint32_t drops() const { return drops_; }   // commands given up on (cable pulled, exit timeout)
    ot::msc::Bot& bot() { return bot_; }

    // the computer is there: frames within the last 100 ms. Not an eject, not a mount, just the bus.
    bool hostSeen();

  private:
    void dropCommand();
    ot::msc::Bot& bot_;
    ot::msc::IEndpoints& ep_;
    ot::msc::IBlockDevice& dev_;
    Env& env_;
    ot::msc::HostWatch watch_;
    volatile uint32_t ticks_ = 0;   // ISR: tokens and frames
    uint32_t seenTicks_ = 0, seenMs_ = 0;
    bool active_ = false, medium_ = false;
    uint32_t drops_ = 0;
};
} // namespace mscdrive
} // namespace ff
