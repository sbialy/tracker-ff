// Host stand-in for the Teensy kinetis.h, just the registers platforms/mk66/3rdparty/teensy_core/serial5.c touches, as plain variables
// the test reads and writes. NOT firmware.
#pragma once
#include <stdint.h>
#include <stddef.h>
#define HAS_KINETISK_UART4 1
extern volatile uint32_t g_SIM_SCGC1, g_PINCFG;
extern volatile uint8_t g_UART4_BDH, g_UART4_BDL, g_UART4_C1, g_UART4_C2, g_UART4_C3, g_UART4_C4, g_UART4_S1, g_UART4_S2, g_UART4_D,
  g_UART4_PFIFO, g_UART4_MODEM;
#define SIM_SCGC1 g_SIM_SCGC1
#define SIM_SCGC1_UART4 0x400u
#define CORE_PIN33_CONFIG g_PINCFG
#define CORE_PIN34_CONFIG g_PINCFG
#define CORE_PIN24_CONFIG g_PINCFG
#define PORT_PCR_PE 2
#define PORT_PCR_PS 1
#define PORT_PCR_PFE 0x10
#define PORT_PCR_DSE 0x40
#define PORT_PCR_SRE 4
#define PORT_PCR_ODE 0x20
#define PORT_PCR_MUX(n) ((n) << 8)
#define UART4_BDH g_UART4_BDH
#define UART4_BDL g_UART4_BDL
#define UART4_C1 g_UART4_C1
#define UART4_C2 g_UART4_C2
#define UART4_C3 g_UART4_C3
#define UART4_C4 g_UART4_C4
#define UART4_S1 g_UART4_S1
#define UART4_S2 g_UART4_S2
#define UART4_D g_UART4_D
#define UART4_PFIFO g_UART4_PFIFO
#define UART4_MODEM g_UART4_MODEM
#define UART_C2_TE 8
#define UART_C2_RE 4
#define UART_C2_RIE 0x20
#define UART_C2_TIE 0x80
#define UART_C2_TCIE 0x40
#define UART_S1_TDRE 0x80
#define UART_S1_TC 0x40
#define UART_S1_RDRF 0x20
#define UART_BDH_SBNS 0x20
#define UART_MODEM_TXCTSE 1
#define IRQ_UART4_STATUS 70
#define NVIC_SET_PRIORITY(irq, p) ((void)(irq), (void)(p))
#define NVIC_ENABLE_IRQ(irq) ((void)(irq))
#define NVIC_DISABLE_IRQ(irq) ((void)(irq))
// the test sets what the running code's interrupt priority is (nvic_execution_priority), yield() lets the test's "UART" move on
int nvic_execution_priority(void);
void yield(void);
