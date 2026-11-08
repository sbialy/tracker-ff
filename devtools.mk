# Development console of the original Tracker: build wiring for the private poly-devtools repository.
#
# The USB test console (ENABLE_DEV_CONSOLE on the MK66, and `ui_shot --serve` on the Mac) is development tooling kept in
# poly-devtools (private, not for redistribution). This tree only holds the one-line hooks (core/include/devhooks.h).
# Included by core/makefile (PLATFORM=MK66), platforms/mk66/makefile and synth-open/tools/ui_shot/Makefile. The caller sets
#   DEVTOOLS_WANT := 1    this build has the console (core and platform: -DENABLE_DEV_CONSOLE in CUSTOM_DEFINES, ui_shot: CONSOLE=1)
# and gets, from $(DEVTOOLS_DIR)/firmware/devtools.mk,
#   DEVTOOLS_INC          -I flags of the console headers (devhooks_impl.h, devhooks_<name>.inc, UsbTest.h)
#   DEVTOOLS_CORE_DIR, DEVTOOLS_CORE_SRCS   console sources of libTracker.a (names in that dir), objects $(OBJ_DIR)/source/<name>.o (their old place, same archive order)
#   DEVTOOLS_MK66_DIR, DEVTOOLS_MK66_SRCS   platform sources (names), linked right after DEVTOOLS_MK66_AFTER (their old place in the find order)
#   DEVTOOLS_HOST_DIR, DEVTOOLS_HOST_SRCS, DEVTOOLS_HOST_INC, DEVTOOLS_HOST_STUBS   the Mac side of ui_shot (transport, --serve, Teensy stand-ins)
#
# DEVTOOLS_DIR defaults to poly-devtools next to this checkout (../poly-devtools). Without it a console build stops here with a
# message. Default, release and stock builds never read this file's second half and need nothing from poly-devtools.
# The checkout must implement the hook set of core/include/devhooks.h (DEVHOOKS_API): tracker-ff 1.2.0 goes with poly-devtools v1.2.0.

DEVTOOLS_TREE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
DEVHOOKS_API := $(shell sed -n 's/^\#define DEVHOOKS_API //p' $(DEVTOOLS_TREE)/core/include/devhooks.h)

ifeq ($(DEVTOOLS_WANT),1)
DEVTOOLS_DIR ?= $(abspath $(DEVTOOLS_TREE)/../poly-devtools)
ifeq ($(wildcard $(DEVTOOLS_DIR)/firmware/devtools.mk),)
$(error ENABLE_DEV_CONSOLE (the development console image) needs the private poly-devtools checkout, not found at DEVTOOLS_DIR=$(DEVTOOLS_DIR). Clone it there or pass DEVTOOLS_DIR=/path/to/poly-devtools. Default and release builds do not need it)
endif
include $(DEVTOOLS_DIR)/firmware/devtools.mk
ifneq ($(DEVTOOLS_API),$(DEVHOOKS_API))
$(error poly-devtools at $(DEVTOOLS_DIR) implements hook set $(DEVTOOLS_API), this tree has $(DEVHOOKS_API) (core/include/devhooks.h). Check out the poly-devtools tag that goes with this release)
endif
endif
