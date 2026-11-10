#ifndef EXT_HOOKS_H_
#define EXT_HOOKS_H_

// Runtime hooks (FEATURE_EXT_HOOKS, MK66). One image for every use: in develop mode (/.develop at the card root) the boot loads an
// extension module (/Plugins/Ext/module.plg, synth-open/ext_module.cpp) that fills ext_hooks. Every pointer is null otherwise and
// each hook is one load and one compare. A module is linked against this exact image (the image id in its header must match).
// Plain C and C++ (platforms/mk66/3rdparty/teensy_core/mk20dx128.c includes it).

#include "ext_abi.h"

#ifdef __cplusplus
extern "C" {
#endif
extern ext_hooks_t ext_hooks;
extern volatile uint32_t ext_site[2];
void ext_boot(void* platform, int develop);   // synth-open/ext_module.cpp
void ext_update(void);                         // main loop: the crash marker, then the module
#ifdef __cplusplus
}
#endif

// SysTick and the fault handler stay stock (ext_systick_isr.inc, ext_fault_isr.inc are empty): the module puts its own handlers
// into the RAM vector table.
#define DEVHOOK_DEFS(name) DEVHOOK_STR(ext_##name.inc)

#define DEVHOOK_TRACKER_INIT(platform, develop) ext_boot(platform, develop)
#define DEVHOOK_TRACKER_UPDATE()                                                                                               \
    do                                                                                                                         \
    {                                                                                                                          \
        ext_update();                                                                                                          \
    } while (0)

// LOGE is compiled in (core/source/log.h): the text goes to the module, nowhere without one (never the serial port)
#define DEVHOOK_LOG(msg, fallback)                                                                                             \
    do                                                                                                                         \
    {                                                                                                                          \
        if (ext_hooks.log) ext_hooks.log(msg);                                                                                 \
    } while (0)
#define DEVHOOK_LOGV(format, args, fallback)                                                                                   \
    do                                                                                                                         \
    {                                                                                                                          \
        if (ext_hooks.logv) ext_hooks.logv(format, args);                                                                      \
    } while (0)

// the injected key queue and encoder turns stay in the image (inert while empty), the module calls inject()
#define DEVHOOK_KEYS_PUBLIC                                                                                                    \
    bool inject(uint8_t button, bool down);                                                                                    \
    bool injectPending() const { return injHead != injTail; }
#define DEVHOOK_KEYS_PRIVATE                                                                                                   \
    static constexpr uint8_t kInjectQueue = 16;                                                                                \
    uint8_t injQueue[kInjectQueue] = { 0 };                                                                                    \
    uint8_t injHead = 0;                                                                                                       \
    uint8_t injTail = 0;
#define DEVHOOK_KEYS_POLL()                                                                                                    \
    do                                                                                                                         \
    {                                                                                                                          \
        while (injTail != injHead)                                                                                             \
        {                                                                                                                      \
            const uint8_t event = injQueue[injTail & (kInjectQueue - 1)];                                                      \
            injTail++;                                                                                                         \
            const uint8_t button = event & 0x7F;                                                                               \
            if (event & 0x80)                                                                                                  \
            {                                                                                                                  \
                holdTimeMs[button] = millis();                                                                                 \
                holdFactor[button] = 0;                                                                                        \
                onPush(button);                                                                                                \
                buttonPush[button] = 1;                                                                                        \
            }                                                                                                                  \
            else                                                                                                               \
            {                                                                                                                  \
                onRelease(button);                                                                                             \
                buttonPush[button] = 0;                                                                                        \
            }                                                                                                                  \
        }                                                                                                                      \
    } while (0)
#define DEVHOOK_BUTTONS_PUBLIC                                                                                                 \
    bool inject(uint8_t button, bool down) { return buttonsDriver.inject(button, down); }                                      \
    bool injectPending() const { return buttonsDriver.injectPending(); }
#define DEVHOOK_ENCODER_PUBLIC                                                                                                 \
    void inject(int delta) { injected += delta; }                                                                              \
    bool injectPending() const { return injected != 0; }
#define DEVHOOK_ENCODER_PRIVATE volatile int32_t injected = 0;
#define DEVHOOK_ENCODER_READ(value)                                                                                            \
    do                                                                                                                         \
    {                                                                                                                          \
        value += injected;                                                                                                     \
        injected = 0;                                                                                                          \
    } while (0)

// the screenshot functions are defined by the module (linked against this image), the hold flag stays here
#define DEVHOOK_DISPLAY_MEMBERS                                                                                                \
    bool devShotBegin();                                                                                                       \
    void devShotSnapshot(int half);                                                                                            \
    void devShotRow(int row, uint8_t* dest);                                                                                   \
    void devShotEnd();                                                                                                         \
    bool devNextText(int* cursor, int* id, int* x, int* y, int* w, int* h, const char** text);                                 \
    bool devShotFrozen = false;
#define DEVHOOK_DISPLAY_UPDATE_HOLD()                                                                                          \
    do                                                                                                                         \
    {                                                                                                                          \
        if (devShotFrozen) return;                                                                                             \
    } while (0)
#define DEVHOOK_INTERFACE_MEMBERS int devPageIndex();

#define DEVHOOK_SITE(id, arg)                                                                                                  \
    do                                                                                                                         \
    {                                                                                                                          \
        ext_site[0] = uint32_t(id);                                                                                            \
        ext_site[1] = uint32_t(arg);                                                                                           \
    } while (0)
#define DEVHOOK_TRACE(tag, a, b, c)
#define DEVHOOK_SPIN_LIMIT 120000u
#define DEVHOOK_SPIN_BEGIN(n) uint32_t n = 0
#define DEVHOOK_SPIN_STEP(n, site, onExpire)                                                                                   \
    if (++(n) >= DEVHOOK_SPIN_LIMIT && ext_hooks.spinExpired && ext_hooks.spinExpired(site))                                   \
    {                                                                                                                          \
        onExpire;                                                                                                              \
    }
// the public call sites still name the console function, hook set 5 gives them DEVHOOK_COPROC_RESET()
#define usbtest_coproc_reset()                                                                                                 \
    do                                                                                                                         \
    {                                                                                                                          \
        if (ext_hooks.coprocReset) ext_hooks.coprocReset();                                                                    \
    } while (0)
enum
{
    DEVSITE_NONE = 0,
    DEVSITE_DISPLAY_STEP = 1,
    DEVSITE_CONTROL_UPDATE = 2,
    DEVSITE_DISPLAY_IMMEDIATE = 3,
    DEVSITE_FT812_WAIT_EMPTY = 4,
    DEVSITE_FT812_WAIT_SPACE = 5,
};
#define DEVHOOK_USB_LINE_CODING(baud)                                                                                          \
    do                                                                                                                         \
    {                                                                                                                          \
        if (ext_hooks.usbLineCoding) ext_hooks.usbLineCoding(baud);                                                            \
    } while (0)
#define DEVHOOK_USB_SOF()                                                                                                      \
    do                                                                                                                         \
    {                                                                                                                          \
        if (ext_hooks.usbSof) ext_hooks.usbSof();                                                                              \
    } while (0)

#endif /* EXT_HOOKS_H_ */
