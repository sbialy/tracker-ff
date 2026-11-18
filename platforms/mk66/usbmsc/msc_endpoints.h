// tracker-ff USB drive: the Bot's endpoint pair (ot::msc::IEndpoints) on the driver of teensy_core/usb_msc_impl.h. A header of its own
// so the host harness (test/usb_e2e) builds the very same adapter over the real usb_dev.c. Free of platform headers.
#pragma once

#include "msc_bot.h"

extern "C" {
#include "usb_msc.h"
}

namespace ff
{
namespace mscdrive
{
// The bulk endpoints: the driver of usb_msc_impl.h (USB interrupt context only, but for these calls the Bot makes from there)
class UsbEndpoints final : public ot::msc::IEndpoints
{
  public:
    bool configured() const override { return usb_msc_configured() != 0; }
    bool rxQueue(uint8_t* buf, uint32_t len) override { return usb_msc_rx_queue(buf, len) != 0; }
    bool rxPoll(uint32_t* got) override { return usb_msc_rx_poll(got) != 0; }
    bool txQueue(const uint8_t* buf, uint32_t len) override { return usb_msc_tx_queue(buf, len) != 0; }
    bool txPoll() override { return usb_msc_tx_poll() != 0; }
    void rxFlush() override { usb_msc_rx_flush(); }
    void txFlush() override { usb_msc_tx_flush(); }
    void stall(bool in) override { usb_msc_stall(in ? 1 : 0); }
    bool stalled(bool in) const override { return usb_msc_stalled(in ? 1 : 0) != 0; }
};
} // namespace mscdrive
} // namespace ff
