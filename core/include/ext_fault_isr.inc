/* Fault handler with runtime hooks (ext_hooks.h): a loaded module records the fault and resets (never returns), otherwise the stock
   code runs (the power-off guard, then the USB and UART polling loop). */
#define DEVHOOK_FAULT_ISR 1
void usb_isr(void);
void uart0_status_isr(void);
void uart1_status_isr(void);
void uart2_status_isr(void);
__attribute__((used)) void ext_fault(uint32_t *frame)
{
	if (ext_hooks.fault) ext_hooks.fault(frame);
	powerOffGuardFault();
	while (1) {
		if (SIM_SCGC4 & SIM_SCGC4_USBOTG) usb_isr();
		if (SIM_SCGC4 & SIM_SCGC4_UART0) uart0_status_isr();
		if (SIM_SCGC4 & SIM_SCGC4_UART1) uart1_status_isr();
		if (SIM_SCGC4 & SIM_SCGC4_UART2) uart2_status_isr();
	}
}
__attribute__((naked)) void fault_isr(void)
{
	__asm volatile (
		"tst lr, #4\n\t"
		"ite eq\n\t"
		"mrseq r0, msp\n\t"
		"mrsne r0, psp\n\t"
		"b ext_fault\n\t"
	);
}
__attribute__((naked)) void unused_isr(void)
{
	__asm volatile ("b fault_isr\n\t");
}
