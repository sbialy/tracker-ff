// SysTick with runtime hooks (ext_hooks.h): a naked front hands the stacked frame to ext_tick, which calls the module's tick when one
// is loaded, then the stock handler runs (renamed systick_isr_stock).
#define DEVHOOK_SYSTICK_ISR systick_isr_stock
extern "C" void systick_isr_stock(void);
extern "C" __attribute__((used)) void ext_tick(uint32_t* frame)
{
    if (ext_hooks.tick) ext_hooks.tick(frame);
}
extern "C" __attribute__((naked)) void systick_isr(void)
{
    __asm volatile("tst lr, #4\n\t"
                   "ite eq\n\t"
                   "mrseq r0, msp\n\t"
                   "mrsne r0, psp\n\t"
                   "push {r0, lr}\n\t"
                   "bl ext_tick\n\t"
                   "pop {r0, lr}\n\t"
                   "b systick_isr_stock\n\t");
}
