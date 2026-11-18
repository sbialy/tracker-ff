// Simulated Kinetis USB-FS device controller around the REAL teensy_core/usb_dev.c (usbsim.c) for the host harness of the USB drive
// (usbmsc/test/usb_e2e). Host only, NOT firmware. The firmware side is untouched code: usb_dev.c with its BDT table, usb_isr(),
// the control request handler and usb_msc_impl.h. This side is the SIE (buffer descriptors, ping-pong banks, DATA0/1 checks, the
// token status) and a USB host (control transfers and bulk packets), plus a clock that produces the 1 ms start-of-frame interrupts.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { USBSIM_ACK = 0, USBSIM_NAK = 1, USBSIM_STALL = 2, USBSIM_NONE = 3 /* endpoint disabled or cable out: no handshake */ };

// usb_init() order without the hardware: audio select, drive select, then everything cleared and the cable plugged in.
void usbsim_init(int audio_on, int msc_on);
void usbsim_bus_reset(void);   // USBRST: ODDRST, the interrupt, ep0 set up by usb_isr()
void usbsim_connect(int on);   // cable: out = no SOF, no handshakes. In again = the caller resets the bus
int usbsim_connected(void);

// Control transfer. setup: 8 bytes. IN: data receives up to cap bytes, returns the length. OUT: wLength bytes from data. -1 = STALL,
// -2 = no answer (cable out).
int usbsim_control(const uint8_t setup[8], uint8_t* data, int cap);
// Bulk packets of endpoint ep (the host's DATA toggle is kept here). out: n <= 64 bytes. in: *n = bytes taken.
int usbsim_out(int ep, const uint8_t* p, uint32_t n);
int usbsim_in(int ep, uint8_t* p, uint32_t* n);
void usbsim_host_toggle_reset(int ep);   // after SET_CONFIGURATION or CLEAR_FEATURE(HALT) of that endpoint (both directions of ep)
int usbsim_endpoint_halted(int ep);

// Time. Every host action and every Env call of the harness moves it; start-of-frame interrupts follow while the cable is in.
void usbsim_advance(uint32_t us);
uint64_t usbsim_now_us(void);
// The main loop's USB interrupt mask (Env::usbMask): an interrupt that comes while masked runs when it is lifted
void usbsim_mask(int on);
void usbsim_deliver_pending(void);   // usb_msc_kick(): the interrupt the main loop set pending

// Counters
typedef struct {
    unsigned tokens, sofs, overruns, toggle_errors, masked_tokens, nested_isr, dts_drops, bad_in_without_own;
} usbsim_stats_t;
usbsim_stats_t* usbsim_stats(void);

// Peeks at firmware state for the checks
int usbsim_configuration(void);
int usbsim_msc_on(void);
int usbsim_msc_iface(void);

#ifdef __cplusplus
}
#endif
