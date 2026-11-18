// See msc_pump.h. Adapted from OpenTracker src/hal/usb_storage.cpp (Apache-2.0).
#include "msc_pump.h"

namespace ff
{
namespace mscdrive
{
using ot::msc::Bot;

void Pump::isrEvent(uint8_t event)
{
    if (event == 1) // USB_MSC_EVENT_RESET
    {
        bot_.onReset();
    }
    else
    {
        ++ticks_; // tokens and frames, the frames stop when the cable is out
    }
    bot_.service();
}

bool Pump::hostSeen()
{
    if (!ep_.configured())
    {
        return false;
    }
    // no frames for 100 ms: cable out or the computer asleep
    const uint32_t t = ticks_;
    const uint32_t now = env_.millis();
    if (t != seenTicks_)
    {
        seenTicks_ = t;
        seenMs_ = now;
    }
    return now - seenMs_ <= 100;
}

bool Pump::begin(uint32_t blocks)
{
    if (!blocks || active_)
    {
        return false;
    }
    seenTicks_ = ticks_;
    seenMs_ = env_.millis();
    env_.usbMask();
    bot_.setMedium(true, blocks); // refused (stays absent) without a data buffer
    env_.usbUnmask();
    medium_ = bot_.mediumPresent();
    active_ = medium_;
    return medium_;
}

// The computer stopped mid-command: both pipes halt (it recovers with a reset recovery or a new enumeration), every queued segment
// is dropped, a block request already running finishes and its result is dropped.
void Pump::dropCommand()
{
    env_.usbMask();
    bot_.abort();
    env_.usbUnmask();
    while (bot_.ioPending())
    {
        bot_.runIo(dev_);
    }
    env_.delayUs(100); // a packet the controller had started lands (~50 us)
    drops_++;
}

void Pump::end()
{
    env_.usbMask();
    bot_.setMedium(false, 0); // NOT READY "medium not present" from the next command on
    env_.usbUnmask();
    medium_ = false;
    active_ = false;
    // A command in flight finishes: its block requests run here. 3 s at most, at once when the computer is gone.
    const uint32_t t = env_.millis();
    uint32_t seenMs = t;
    uint32_t last = ticks_;
    for (;;)
    {
        env_.usbMask();
        const bool busy = bot_.busy();
        env_.usbUnmask();
        if (!busy)
        {
            return;
        }
        if (bot_.runIo(dev_))
        {
            env_.kick();
        }
        if (ticks_ != last)
        {
            last = ticks_;
            seenMs = env_.millis();
        }
        if (env_.millis() - seenMs > 100 || env_.millis() - t > 3000)
        {
            dropCommand();
            return;
        }
    }
}

void Pump::service()
{
    if (!active_)
    {
        return;
    }
    const uint32_t now = env_.millis();
    // Cable pulled or the computer gone mid-command: abort after 500 ms without frames, so the unit is not busy forever
    const bool seen = hostSeen();
    env_.usbMask();
    const bool busy = bot_.busy();
    env_.usbUnmask();
    if (watch_.update(seen, busy, now))
    {
        dropCommand();
    }
    // The card pulled while the computer owns it: the medium goes, nothing is presented again (the core leaves the mode on a
    // card removal and takes the card back: fail closed, no re-present race with a new mount)
    if (medium_ && !env_.cardIn())
    {
        env_.usbMask();
        bot_.setMedium(false, 0);
        env_.usbUnmask();
        medium_ = false;
    }
    if (!medium_)
    {
        return;
    }
    // Block I/O for the computer: keep the ring full for up to 8 ms per loop pass (the next request follows within microseconds
    // of the kick while a chunk is free, the UI keeps running between passes)
    const uint32_t t0 = env_.micros();
    uint32_t idle = t0;
    for (;;)
    {
        const uint32_t t = env_.micros();
        if (bot_.runIo(dev_))
        {
            env_.kick();
            idle = env_.micros();
        }
        else if (t - idle > (seen && bot_.busy() ? 2000u : 300u))
        {
            break; // a stream waits for USB, or no command runs
        }
        if (t - t0 > 8000)
        {
            break;
        }
    }
}
} // namespace mscdrive
} // namespace ff
