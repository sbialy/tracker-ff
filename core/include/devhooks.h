#ifndef DEVHOOKS_H_
#define DEVHOOKS_H_

// Development hooks of the original Tracker (MK66) and its Mac host build (ui_shot).
//
// The USB test console (key and pad injection, screenshots, files, firmware update over USB serial) is development tooling
// that lives in the private poly-devtools repository, not here. The firmware keeps only the places where it plugs in: each
// is one line, a DEVHOOK_* macro or an `#include DEVHOOK_DEFS(name)`. Without ENABLE_DEV_CONSOLE every macro expands to
// nothing (or to the default code it is given) and every DEVHOOK_DEFS include is the empty devhooks_off.inc, so default and
// release images compile exactly as if the hooks were not there.
//
// With -DENABLE_DEV_CONSOLE (make original ... CUSTOM_DEFINES="-DENABLE_DEV_CONSOLE"), devtools.mk at the top of the tree
// requires the poly-devtools checkout (DEVTOOLS_DIR, default ../poly-devtools next to this repository) and adds its
// firmware/include directory, where devhooks_impl.h defines the same macros with the console code and devhooks_<name>.inc
// hold the definitions. DEVHOOKS_API below names this set of hooks, devtools.mk refuses a checkout made for another set.
//
// Tracker+ and Mini (PLATFORM_IMX) have Polyend's own console (platforms/imx/source/DevConsole.*) behind the same
// ENABLE_DEV_CONSOLE flag. These hooks stay empty there and IMX builds never need poly-devtools.
//
// Plain C and C++ (platforms/mk66/3rdparty/teensy_core/mk20dx128.c includes it).

#define DEVHOOKS_API 3

#define DEVHOOK_STR_(x) #x
#define DEVHOOK_STR(x) DEVHOOK_STR_(x)

#if defined(ENABLE_DEV_CONSOLE) && !defined(PLATFORM_IMX)

#define DEVHOOKS 1
#include "devhooks_impl.h" // poly-devtools firmware/include

#else

// Definitions at file scope, one line each: #include DEVHOOK_DEFS(name)
#define DEVHOOK_DEFS(name) "devhooks_off.inc"

// core/source/Tracker.cpp: TrackerImpl::init and update
#define DEVHOOK_TRACKER_INIT(platform, develop)
#define DEVHOOK_TRACKER_UPDATE()

// platforms/mk66/source/util.cpp: where LOG text goes, `fallback` is the serial port code of a normal image
#define DEVHOOK_LOG(msg, fallback) fallback
#define DEVHOOK_LOGV(format, args, fallback) fallback

// platforms/mk66/3rdparty/Buttons/Keys.h and Keys.cpp: the injected key queue
#define DEVHOOK_KEYS_PUBLIC
#define DEVHOOK_KEYS_PRIVATE
#define DEVHOOK_KEYS_POLL()

// platforms/mk66/source/ButtonsBaseImpl.h
#define DEVHOOK_BUTTONS_PUBLIC

// platforms/mk66/source/EncoderImpl.h and EncoderImpl.cpp: the injected encoder turns
#define DEVHOOK_ENCODER_PUBLIC
#define DEVHOOK_ENCODER_PRIVATE
#define DEVHOOK_ENCODER_READ(value)

// core/libs/NewDisplay/display.h and display.cpp: the screenshot holds the display update
#define DEVHOOK_DISPLAY_MEMBERS
#define DEVHOOK_DISPLAY_UPDATE_HOLD()

// core/interface/core/interface.h
#define DEVHOOK_INTERFACE_MEMBERS

// Stall diagnosis of a console image (hook set 2). SITE notes where the main loop is (a stall record names it), a SPIN pair
// bounds one busy-wait: BEGIN declares the poll counter, STEP counts a poll and, when the console image gives up, runs
// `onExpire` (a statement, so `break` and `return` act on the caller's loop and function). Sites: FT812 command FIFO waits
// (platforms/mk66/3rdparty/FT812/EVE_Layer.cpp and API_Layer.cpp) and the display's immediate update (core/libs/NewDisplay/display.cpp).
#define DEVHOOK_SITE(id, arg)
#define DEVHOOK_SPIN_BEGIN(n)
#define DEVHOOK_SPIN_STEP(n, site, onExpire)

// platforms/mk66/3rdparty/teensy_core/usb_dev.c (hook set 3): a console image resets the unit from inside the USB interrupt, for a main
// loop that no longer answers. LINE_CODING sees every CDC SET_LINE_CODING (baud rate), SOF runs on every start of frame once configured.
#define DEVHOOK_USB_LINE_CODING(baud)
#define DEVHOOK_USB_SOF()

#endif

#endif /* DEVHOOKS_H_ */
