// See usbsim.h. The firmware file is included below after a text transform (usb_dev_sim.c, made by usb_e2e.sh): the write-1-to-clear
// writes of ISTAT / ERRSTAT / OTGISTAT become usbsim_w1c() calls, nothing else changes.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "usbsim.h"

#include "usb_dev_sim.c"

volatile uint8_t usbsim_regs[0x100];
volatile uint8_t usbsim_endpt[16 * 4];
volatile unsigned usbsim_pended;

void usbsim_w1c(volatile uint8_t* reg, uint8_t mask) { *reg = (uint8_t)(*reg & ~mask); }

// ---- stand-ins for the rest of the firmware usb_dev.c calls --------------------------------------------------------------
#define POOL 64
static usb_packet_t g_pool[POOL];
static uint8_t g_poolUsed[POOL];
usb_packet_t* usb_malloc(void)
{
    for (int i = 0; i < POOL; i++)
        if (!g_poolUsed[i]) { g_poolUsed[i] = 1; memset(&g_pool[i], 0, sizeof g_pool[i]); return &g_pool[i]; }
    return NULL;
}
// usb_dev.c frees (addr - 8): on a 32-bit target that is the packet, here the struct has 64-bit pointers, so any address inside the
// packet frees it
void usb_free(usb_packet_t* p)
{
    uintptr_t a = (uintptr_t)p;
    for (int i = 0; i < POOL; i++)
        if (a >= (uintptr_t)&g_pool[i] - 16 && a < (uintptr_t)&g_pool[i] + sizeof g_pool[i]) { g_poolUsed[i] = 0; return; }
}
unsigned int usb_get_free_buffer_count() { return 0; }
uint32_t usb_cdc_line_coding[2] = { 115200, 0x00080000 };
volatile uint8_t usb_cdc_transmit_flush_timer;
volatile uint8_t usb_seremu_transmit_flush_timer;
volatile uint32_t systick_millis_count;
volatile uint8_t usb_cdc_line_rtsdtr;
volatile uint32_t usb_cdc_line_rtsdtr_millis;
void usb_serial_flush_callback(void) {}
void usb_seremu_flush_callback(void) {}
void usb_midi_flush_output(void) {}
#ifdef FF_USB_AUDIO_TX
uint16_t usb_audio_transmit_buffer[256];
uint8_t usb_audio_transmit_setting;
unsigned int usb_audio_transmit_callback(uint16_t* buf) { (void)buf; return 176; }
int ffUsbAudioBootFlag(void) { return 0; }
#endif
#ifdef FF_USB_MSC
int ffUsbMscBootFlag(void) { return 0; }
#endif

// ---- the simulation -------------------------------------------------------------------------------------------------------
static struct {
    uint8_t odd[16][2];          // SIE bank of the next transaction, [endpoint][RX=0 / TX=1] (ODDRST: even)
    uint8_t hostToggle[16][2];   // DATA0/1 the host sends (OUT) / expects (IN) next
    int connected;
    uint64_t now, nextSof;
    int masked, sofPending, inIsr;
    usbsim_stats_t st;
} S;

usbsim_stats_t* usbsim_stats(void) { return &S.st; }
int usbsim_configuration(void) { return usb_configuration; }
int usbsim_msc_on(void) { return ff_usb_msc_on; }
int usbsim_msc_iface(void) { return ff_usb_msc_iface; }
int usbsim_connected(void) { return S.connected; }
uint64_t usbsim_now_us(void) { return S.now; }

static void run_isr(void)
{
    if (S.inIsr) { S.st.nested_isr++; return; }
    S.inIsr = 1;
    usb_isr();
    S.inIsr = 0;
}

void usbsim_mask(int on)
{
    S.masked = on;
    if (!on) {
        if (usbsim_pended) { usbsim_pended = 0; run_isr(); }
        if (S.sofPending) { S.sofPending = 0; USB0_ISTAT |= USB_ISTAT_SOFTOK; run_isr(); }
    }
}

void usbsim_deliver_pending(void)
{
    if (S.masked) return;
    if (usbsim_pended) { usbsim_pended = 0; run_isr(); }
}

static void sof(void)
{
    S.st.sofs++;
    if (S.masked) { S.sofPending = 1; return; }
    USB0_ISTAT |= USB_ISTAT_SOFTOK;
    run_isr();
}

void usbsim_advance(uint32_t us)
{
    S.now += us;
    while (S.nextSof <= S.now) {
        S.nextSof += 1000;
        if (S.connected) sof();
    }
}

void usbsim_connect(int on) { S.connected = on; }

void usbsim_init(int audio_on, int msc_on)
{
    memset(&S, 0, sizeof S);
    memset((void*)usbsim_regs, 0, sizeof usbsim_regs);
    memset((void*)usbsim_endpt, 0, sizeof usbsim_endpt);
    memset(g_poolUsed, 0, sizeof g_poolUsed);
    usbsim_pended = 0;
    usb_configuration = 0;
#ifdef FF_USB_AUDIO_TX
    ff_usb_audio_select(audio_on);
#else
    (void)audio_on;
#endif
#ifdef FF_USB_MSC
    ff_usb_msc_select(msc_on);
#else
    (void)msc_on;
#endif
    for (unsigned i = 0; i < (NUM_ENDPOINTS + 1) * 4; i++) { table[i].desc = 0; table[i].addr = 0; }
    S.connected = 1;
    S.nextSof = 1000;
}

void usbsim_bus_reset(void)
{
    memset(S.odd, 0, sizeof S.odd);
    memset(S.hostToggle, 0, sizeof S.hostToggle);
    // usb_configuration is NOT cleared by the firmware on a bus reset (only SET_CONFIGURATION writes it): the same here
    USB0_ISTAT |= USB_ISTAT_USBRST;
    run_isr();
    memset(S.odd, 0, sizeof S.odd);   // ODDRST was written by the ISR: banks back to even
}

// A token completed: the SIE has written the BDT, now it tells the CPU
static void token(int ep, int tx, int bank)
{
    S.st.tokens++;
    if (S.masked && ep >= 9) S.st.masked_tokens++;
    USB0_STAT = (uint8_t)((ep << 4) | (tx << 3) | (bank << 2));
    USB0_ISTAT |= USB_ISTAT_TOKDNE;
    run_isr();
}

static void complete(bdt_t* b, uint32_t count, uint32_t pid, uint32_t data1)
{
    b->desc = (count << 16) | (pid << 2) | (data1 ? BDT_DATA1 : 0);   // OWN cleared, token PID and DATA bit as received
}

#define EPRXEN 0x08
#define EPTXEN 0x04
#define EPSTALL 0x02

void usbsim_host_toggle_reset(int ep) { S.hostToggle[ep][0] = S.hostToggle[ep][1] = 0; }
int usbsim_endpoint_halted(int ep) { return (usbsim_endpt[ep * 4] & EPSTALL) != 0; }

int usbsim_out(int ep, const uint8_t* p, uint32_t n)
{
    if (!S.connected) return USBSIM_NONE;
    const uint8_t reg = usbsim_endpt[ep * 4];
    if (!(reg & EPRXEN)) return USBSIM_NONE;
    if (reg & EPSTALL) return USBSIM_STALL;
    usbsim_advance(n * 8 / 12 + 5);   // 12 Mbit/s plus token and handshake
    const int bank = S.odd[ep][0];
    bdt_t* b = &table[index(ep, RX, bank)];
    if (!(b->desc & BDT_OWN)) return USBSIM_NAK;
    const uint32_t bdtToggle = (b->desc & BDT_DATA1) ? 1 : 0;
    if ((b->desc & BDT_DTS) && bdtToggle != S.hostToggle[ep][0]) {
        S.st.dts_drops++;   // a retransmission: ACKed and dropped
        S.hostToggle[ep][0] ^= 1;
        return USBSIM_ACK;
    }
    const uint32_t bc = (b->desc >> 16) & 0x3FF;
    if (n > bc) { S.st.overruns++; return USBSIM_NAK; }
    memcpy(b->addr, p, n);
    complete(b, n, 0x1, S.hostToggle[ep][0]);
    S.hostToggle[ep][0] ^= 1;
    S.odd[ep][0] ^= 1;
    token(ep, 0, bank);
    return USBSIM_ACK;
}

int usbsim_in(int ep, uint8_t* p, uint32_t* n)
{
    *n = 0;
    if (!S.connected) return USBSIM_NONE;
    const uint8_t reg = usbsim_endpt[ep * 4];
    if (!(reg & EPTXEN)) return USBSIM_NONE;
    if (reg & EPSTALL) return USBSIM_STALL;
    usbsim_advance(10);
    const int bank = S.odd[ep][1];
    bdt_t* b = &table[index(ep, TX, bank)];
    if (!(b->desc & BDT_OWN)) return USBSIM_NAK;
    const uint32_t bdtToggle = (b->desc & BDT_DATA1) ? 1 : 0;
    if (bdtToggle != S.hostToggle[ep][1]) S.st.toggle_errors++;
    const uint32_t bc = (b->desc >> 16) & 0x3FF;
    memcpy(p, b->addr, bc);
    usbsim_advance(bc * 8 / 12);
    complete(b, bc, 0x9, S.hostToggle[ep][1]);
    S.hostToggle[ep][1] ^= 1;
    S.odd[ep][1] ^= 1;
    *n = bc;
    token(ep, 1, bank);
    return USBSIM_ACK;
}

// ---- control transfers on endpoint 0 --------------------------------------------------------------------------------------
static int ep0_setup(const uint8_t s[8])
{
    usbsim_endpt[0] &= (uint8_t)~EPSTALL;   // a SETUP token clears a stall of the control endpoint
    const int bank = S.odd[0][0];
    bdt_t* b = &table[index(0, RX, bank)];
    if (!(b->desc & BDT_OWN)) return USBSIM_NAK;
    memcpy(b->addr, s, 8);
    complete(b, 8, 0xD, 0);
    S.odd[0][0] ^= 1;
    S.hostToggle[0][0] = 1;   // first stage after SETUP is DATA1 (both directions)
    S.hostToggle[0][1] = 1;
    token(0, 0, bank);
    return USBSIM_ACK;
}

static int ep0_in(uint8_t* p, uint32_t* n)
{
    *n = 0;
    const int bank = S.odd[0][1];
    bdt_t* b = &table[index(0, TX, bank)];
    if (!(b->desc & BDT_OWN)) return USBSIM_NAK;
    const uint32_t bc = (b->desc >> 16) & 0x3FF;
    memcpy(p, b->addr, bc);
    complete(b, bc, 0x9, S.hostToggle[0][1]);
    S.hostToggle[0][1] ^= 1;
    S.odd[0][1] ^= 1;
    *n = bc;
    token(0, 1, bank);
    return USBSIM_ACK;
}

static int ep0_out(const uint8_t* p, uint32_t n)
{
    const int bank = S.odd[0][0];
    bdt_t* b = &table[index(0, RX, bank)];
    if (!(b->desc & BDT_OWN)) return USBSIM_NAK;
    memcpy(b->addr, p, n);
    complete(b, n, 0x1, S.hostToggle[0][0]);
    S.hostToggle[0][0] ^= 1;
    S.odd[0][0] ^= 1;
    token(0, 0, bank);
    return USBSIM_ACK;
}

int usbsim_control(const uint8_t s[8], uint8_t* data, int cap)
{
    if (!S.connected) return -2;
    usbsim_advance(100);
    if (ep0_setup(s) != USBSIM_ACK) return -2;
    if (usbsim_endpt[0] & EPSTALL) { usbsim_endpt[0] &= (uint8_t)~EPSTALL; return -1; }
    const int in = s[0] & 0x80;
    const int wlen = s[6] | (s[7] << 8);
    int got = 0;
    if (in && wlen) {
        for (int guard = 0; guard < 64 && got < wlen; guard++) {
            uint8_t tmp[64];
            uint32_t n;
            const int r = ep0_in(tmp, &n);
            if (usbsim_endpt[0] & EPSTALL) { usbsim_endpt[0] &= (uint8_t)~EPSTALL; return -1; }
            if (r != USBSIM_ACK) return -2;
            if (got + (int)n <= cap) memcpy(data + got, tmp, n);
            got += (int)n;
            if (n < 64) break;
        }
        const uint8_t z = 0;
        if (ep0_out(&z, 0) != USBSIM_ACK) return -2;   // status stage OUT
        return got < cap ? got : cap;
    }
    if (!in && wlen) {
        for (int off = 0; off < wlen; off += 64) {
            const int k = wlen - off < 64 ? wlen - off : 64;
            if (ep0_out(data + off, (uint32_t)k) != USBSIM_ACK) return -2;
        }
    }
    uint8_t tmp[64];
    uint32_t n;
    const int r = ep0_in(tmp, &n);   // status stage IN, zero length
    if (usbsim_endpt[0] & EPSTALL) { usbsim_endpt[0] &= (uint8_t)~EPSTALL; return -1; }
    if (r != USBSIM_ACK || n != 0) return -2;
    return 0;
}
