// Host unit test (NOT firmware) of the DIN MIDI out TX ring when the audio interrupt writes into a full ring: the firmware's own
// platforms/mk66/3rdparty/teensy_core/serial5.c (asm lines neutralised by the runner script) against host stand-ins for the registers.
//   S1  a ring with room takes the bytes from the audio interrupt, nothing is dropped
//   S2  full ring, audio interrupt: the status byte is dropped and counted, never waits
//   S3  the data bytes of that cut message are dropped too (counter up), a realtime byte inside it is not part of it
//   S4  the next status byte goes through (once the ring has room) with its data bytes, in order, and the cut message never reaches the wire
//   S5  a status byte that fits followed by data bytes that do not: the rest of that message is dropped, the next status goes through
//   S6  the main loop (priority 256) with a full ring waits (yield) and loses nothing, the counter does not move
//   S7  an interrupt at or above the UART priority drains the ring itself and loses nothing
#include <stdint.h>
#include <stdio.h>
#include <string.h>

volatile uint32_t g_SIM_SCGC1, g_PINCFG;
volatile uint8_t g_UART4_BDH, g_UART4_BDL, g_UART4_C1, g_UART4_C2, g_UART4_C3, g_UART4_C4, g_UART4_S1, g_UART4_S2, g_UART4_D, g_UART4_PFIFO,
  g_UART4_MODEM;

void serial5_begin(uint32_t divisor);
void serial5_putchar(uint32_t c);
int serial5_write_buffer_free(void);
void uart4_status_isr(void);
extern volatile uint32_t serial5_tx_dropped;

static int g_prio = 128;   // what nvic_execution_priority returns: 128 audio render, 256 main loop, 0 above the UART
static int g_yields;
static uint8_t g_wire[4096];
static int g_wireN;
int nvic_execution_priority(void) { return g_prio; }

// the wire log: a byte leaves when the interrupt routine of serial5.c moves it into UART4_D (TDRE set), seen as the ring getting shorter
static int ringUsed(void) { return 511 - serial5_write_buffer_free(); }
static void drain(int n)
{
    for (int i = 0; i < n && ringUsed() > 0; i++) {
        const int before = ringUsed();
        g_UART4_S1 |= 0x80;
        uart4_status_isr();
        if (ringUsed() < before) g_wire[g_wireN++] = g_UART4_D;
    }
}
void yield(void) { g_yields++; drain(1); }

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL line %d  %s\n", __LINE__, #c); } } while (0)

static void put(const uint8_t* b, int n) { for (int i = 0; i < n; i++) serial5_putchar(b[i]); }
static void fillRing(void) { while (serial5_write_buffer_free() > 0) serial5_putchar(0xF8); }   // 511 clock bytes, none of them a message

int main(void)
{
    serial5_begin(2000);
    g_UART4_C2 |= 0x80;
    g_prio = 128;

    // S1
    const uint8_t on[3] = {0x90, 60, 100};
    put(on, 3);
    CHECK(serial5_tx_dropped == 0);
    CHECK(ringUsed() == 3);
    drain(10);
    CHECK(ringUsed() == 0 && g_wireN == 3 && g_wire[0] == 0x90 && g_wire[1] == 60 && g_wire[2] == 100);

    // S2 and S3
    g_wireN = 0;
    fillRing();
    CHECK(serial5_write_buffer_free() == 0 && serial5_tx_dropped == 0);
    serial5_putchar(0x90);
    CHECK(serial5_tx_dropped == 1);
    serial5_putchar(61);
    serial5_putchar(101);
    CHECK(serial5_tx_dropped == 3);
    serial5_putchar(0xF8);   // a clock byte stands alone: it does not fit either (ring still full) and is counted, but it is not data of the cut message
    CHECK(serial5_tx_dropped == 4);
    serial5_putchar(62);     // the data byte after it still belongs to the cut message
    CHECK(serial5_tx_dropped == 5);

    // S4
    drain(4);   // four bytes leave, four slots free
    CHECK(serial5_write_buffer_free() == 4);
    const uint8_t cc[3] = {0xB0, 74, 5};
    put(cc, 3);
    CHECK(serial5_tx_dropped == 5);
    CHECK(serial5_write_buffer_free() == 1);
    drain(600);
    CHECK(ringUsed() == 0);
    {   // the wire: 511 clock bytes, then the CC; the cut note on (0x90 61 101 62) never appears
        int ok = g_wireN == 511 + 3, bad = 0;
        for (int i = 0; ok && i < 511; i++) ok = g_wire[i] == 0xF8;
        ok = ok && g_wire[511] == 0xB0 && g_wire[512] == 74 && g_wire[513] == 5;
        for (int i = 0; i < g_wireN; i++) bad += (g_wire[i] == 0x90 || g_wire[i] == 101 || g_wire[i] == 61 || g_wire[i] == 62);
        CHECK(ok);
        CHECK(bad == 0);
    }

    // S5: one slot free: the status fits, its two data bytes do not
    g_wireN = 0;
    fillRing();
    drain(1);
    CHECK(serial5_write_buffer_free() == 1);
    const uint32_t d0 = serial5_tx_dropped;
    serial5_putchar(0x80);   // fits (ring full now)
    serial5_putchar(60);     // dropped
    serial5_putchar(0);      // dropped (a zero data byte too)
    CHECK(serial5_tx_dropped == d0 + 2);
    drain(2);                // two slots: a new status and its data byte
    serial5_putchar(0xC0);   // a new status: goes through
    CHECK(serial5_tx_dropped == d0 + 2);
    serial5_putchar(7);      // its data byte goes through as well (flag cleared)
    CHECK(serial5_tx_dropped == d0 + 2);
    drain(600);
    CHECK(g_wireN == 511 + 3 && g_wire[g_wireN - 3] == 0x80 && g_wire[g_wireN - 2] == 0xC0 && g_wire[g_wireN - 1] == 7);

    // S6: the main loop waits
    g_wireN = 0;
    fillRing();
    g_prio = 256;
    g_yields = 0;
    const uint32_t d1 = serial5_tx_dropped;
    serial5_putchar(0xB1);
    CHECK(g_yields >= 1);
    CHECK(serial5_tx_dropped == d1);
    g_prio = 128;
    drain(600);
    CHECK(g_wire[g_wireN - 1] == 0xB1);

    // S7: at the UART priority the caller moves the ring itself (TDRE set), nothing is dropped
    g_wireN = 0;
    fillRing();
    g_prio = 0;
    g_UART4_S1 |= 0x80;
    const uint32_t d2 = serial5_tx_dropped;
    serial5_putchar(0xB2);
    CHECK(serial5_tx_dropped == d2);
    g_prio = 128;
    CHECK(ringUsed() == 511);

    printf("serial5_drop_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail != 0;
}
