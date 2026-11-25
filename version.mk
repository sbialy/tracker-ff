# Polyend Tracker Community Edition (CE) Centralized Version Configuration

EDITION_NAME := CE
VER_ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD_TAG    ?= $(shell git -C "$(VER_ROOT_DIR)" rev-parse --short=7 HEAD 2>/dev/null || date +"%Y%m%d")

# Original Tracker (MK66)
ORIGINAL_VER_MAJOR := 1
ORIGINAL_VER_MINOR := 0
ORIGINAL_VER_PATCH := 0
ORIGINAL_VER_BETA  := 255

# Tracker+ (IMX): tracker-ff 2.0.0, same number as the original Tracker image (SYNTH_OPEN_VER below)
PLUS_VER_MAJOR     := 2
PLUS_VER_MINOR     := 0
PLUS_VER_PATCH     := 0
PLUS_VER_BETA      := 255

# Tracker Mini (IMX): tracker-ff 2.0.0
MINI_VER_MAJOR     := 2
MINI_VER_MINOR     := 0
MINI_VER_PATCH     := 0
MINI_VER_BETA      := 255

ifeq ($(PRODUCT), MINI)
  FW_VER_1    := $(MINI_VER_MAJOR)
  FW_VER_2    := $(MINI_VER_MINOR)
  FW_VER_3    := $(MINI_VER_PATCH)
  FW_BETA     := $(MINI_VER_BETA)
else ifeq ($(PRODUCT), PLUS)
  FW_VER_1    := $(PLUS_VER_MAJOR)
  FW_VER_2    := $(PLUS_VER_MINOR)
  FW_VER_3    := $(PLUS_VER_PATCH)
  FW_BETA     := $(PLUS_VER_BETA)
else
  FW_VER_1    := $(ORIGINAL_VER_MAJOR)
  FW_VER_2    := $(ORIGINAL_VER_MINOR)
  FW_VER_3    := $(ORIGINAL_VER_PATCH)
  FW_BETA     := $(ORIGINAL_VER_BETA)
endif

# make original SYNTH_OPEN=1 is tracker-ff: the edition text on the Config screen becomes "tracker-ff"
# (label "2.0.0 tracker-ff", next to the build hash) and the numeric FW_VER is the tracker-ff version, which is also
# written into project, instrument, pattern and config headers (informational, nothing checks it on load).
# SYNTH_OPEN_VER names the .ptf. A stock make original keeps the numbers above, so its image does not change.
SYNTH_OPEN_VER := 2.0.0
ifeq ($(SYNTH_OPEN),1)
  FW_EDITION_TEXT := tracker-ff
  _so_ver_num := $(firstword $(subst -, ,$(SYNTH_OPEN_VER)))
  ifneq ($(PRODUCT),MINI)
  ifneq ($(PRODUCT),PLUS)
    FW_VER_1 := $(word 1,$(subst ., ,$(_so_ver_num)))
    FW_VER_2 := $(word 2,$(subst ., ,$(_so_ver_num)))
    FW_VER_3 := $(word 3,$(subst ., ,$(_so_ver_num)))
  endif
  endif
else ifneq ($(filter MINI PLUS,$(PRODUCT)),)
  # Tracker+ and Mini images are tracker-ff 2.0.0 too: Config > Firmware shows "2.0.0 tracker-ff" and the files are
  # TrackerPlus_FF_2.0.0.ptf and TrackerMini_FF_2.0.0.ptf (IMX_EDITION_TAG, the FF tag of PolyendTracker_FF_2.0.0.ptf)
  FW_EDITION_TEXT := tracker-ff
else
  FW_EDITION_TEXT := $(EDITION_NAME)
endif

FW_VERSION_DEFINES := -DFW_EDITION_NAME=\"$(FW_EDITION_TEXT)\" \
                      -DFW_VER_MAJOR=$(FW_VER_1) \
                      -DFW_VER_MINOR=$(FW_VER_2) \
                      -DFW_VER_PATCH=$(FW_VER_3) \
                      -DFW_VER_BETA=$(FW_BETA)

# The edition tag in the Tracker+ and Mini file names (the Original names come from the SYNTH_OPEN rules in the Makefile)
IMX_EDITION_TAG        := FF

BUILD_ID_STR           := $(BUILD_TAG)
DEFAULT_TARGET_POSTFIX := $(if $(filter MINI PLUS,$(PRODUCT)),$(IMX_EDITION_TAG),$(EDITION_NAME))_$(FW_VER_1).$(FW_VER_2).$(FW_VER_3)
