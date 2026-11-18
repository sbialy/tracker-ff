// Host stand-in for the Kinetis USB-FS registers of usb_dev.c (forced include, -include simregs.h): the real kinetis.h first, so every
// constant and the Cortex-M instructions compile, then the USB0 registers (absolute addresses on the device) become variables of
// usbsim.c. The ENDPTn registers are one array, usb_dev.c steps through them with pointer arithmetic (&USB0_ENDPT0 + 4 * n).
// Host only, NOT firmware.
#pragma once
#include <stdint.h>
#include "kinetis.h"

#undef __enable_irq
#undef __disable_irq
#define __enable_irq()
#define __disable_irq()

extern volatile uint8_t usbsim_regs[0x100];
extern volatile uint8_t usbsim_endpt[16 * 4];
extern volatile unsigned usbsim_pended;
void usbsim_w1c(volatile uint8_t* reg, uint8_t mask);

#define USBSIM_REG(off) (usbsim_regs[off])
#undef USB0_ISTAT
#define USB0_ISTAT USBSIM_REG(0x08)
#undef USB0_STAT
#define USB0_STAT USBSIM_REG(0x0C)
#undef USB0_CTL
#define USB0_CTL USBSIM_REG(0x0D)
#undef USB0_ADDR
#define USB0_ADDR USBSIM_REG(0x0E)
#undef USB0_INTEN
#define USB0_INTEN USBSIM_REG(0x09)
#undef USB0_ERRSTAT
#define USB0_ERRSTAT USBSIM_REG(0x0A)
#undef USB0_ERREN
#define USB0_ERREN USBSIM_REG(0x0B)
#undef USB0_BDTPAGE1
#define USB0_BDTPAGE1 USBSIM_REG(0x10)
#undef USB0_BDTPAGE2
#define USB0_BDTPAGE2 USBSIM_REG(0x11)
#undef USB0_BDTPAGE3
#define USB0_BDTPAGE3 USBSIM_REG(0x12)
#undef USB0_OTGISTAT
#define USB0_OTGISTAT USBSIM_REG(0x13)
#undef USB0_USBCTRL
#define USB0_USBCTRL USBSIM_REG(0x14)
#undef USB0_CONTROL
#define USB0_CONTROL USBSIM_REG(0x15)
#undef USB0_USBTRC0
#define USB0_USBTRC0 USBSIM_REG(0x16)
#undef USB0_CLK_RECOVER_CTRL
#define USB0_CLK_RECOVER_CTRL USBSIM_REG(0x17)
#undef USB0_CLK_RECOVER_IRC_EN
#define USB0_CLK_RECOVER_IRC_EN USBSIM_REG(0x18)
#undef USB0_ENDPT0
#define USB0_ENDPT0 (usbsim_endpt[0])
#undef USB0_ENDPT1
#define USB0_ENDPT1 (usbsim_endpt[4])

#undef NVIC_SET_PENDING
#define NVIC_SET_PENDING(n) (++usbsim_pended)
#undef NVIC_ENABLE_IRQ
#define NVIC_ENABLE_IRQ(n) ((void)(n))
#undef NVIC_DISABLE_IRQ
#define NVIC_DISABLE_IRQ(n) ((void)(n))
#undef NVIC_SET_PRIORITY
#define NVIC_SET_PRIORITY(n, p) ((void)(n), (void)(p))
