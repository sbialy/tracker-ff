# The one definition of the tracker-ff release flavour (the original Tracker, MK66). Read by the top Makefile (RELEASE_ARGS: make
# original-release, the stack ratchet, the lint delta) and by the host simulator (synth-open/tools/ui_shot/Makefile), so the simulator
# compiles the core with the defines the shipped image has. synth-open/tools/ff_flags_check.sh proves it: the defines of a real release
# compile (core and platform) equal FF_RELEASE_CORE_DEFS + FF_RELEASE_PLATFORM_DEFS, and the simulator's release flavour (EXTHOOKS=1 USB_MSC=1)
# equals FF_RELEASE_CORE_DEFS minus FF_SIM_BRIDGE_ONLY_DEFS. Change a flag here, never in a copy.

# The make variables of the release recipe. core/makefile and platforms/mk66/makefile turn them into the defines below.
# SYNTH_TAG=-rel only names the object dirs (not a compiler flag). -DFEATURE_NO_SKIPBACK leaves Skip-back (core/source/skipback.h) out of the
# release, the code stays for 1.5.
# FW_LTO=ui is part of the recipe: link time optimisation of the UI objects only (core/makefile, LTO_UI_PATHS), never the audio / DSP objects,
# the engines or the ISR code. make original-release FW_LTO= switches it off (wipe core/build-* and platforms/mk66/build-trim when you change it).
FF_RELEASE_LTO := $(if $(filter undefined,$(origin FW_LTO)),ui,$(FW_LTO))
# TABLE_DWT=1: the Table lite cycle stamps (core/source/tables.h DwtBlock), measurement images only, empty (no change) otherwise. The top
# Makefile gives such an image its own build id (sha7 plus "d")
FF_RELEASE_TABLE_DWT := $(if $(filter 1,$(TABLE_DWT)), -DTABLE_DWT)
# Stereo I/O steps (poly-stereo-io): each one is its own compile time flag, any subset can ship. STEREO_IO lists the flags that are on,
# make original-release STEREO_IO="FEATURE_STEREO_REC FEATURE_RADIO_STEREO" builds a subset, STEREO_IO= builds none. The subset gets its own
# object dirs through the SYNTH_TAG letters (R rec, D radio, S sends, E edit, U usb in). FEATURE_RADIO_STEREO needs FEATURE_STEREO_REC.
# MK66 release only: core/makefile and the Tracker+ and Mini builds never set these flags.
FF_STEREO_IO_ALL := FEATURE_STEREO_REC FEATURE_RADIO_STEREO FEATURE_STEREO_SENDS FEATURE_STEREO_EDIT FEATURE_USB_AUDIO_IN
STEREO_IO ?= $(FF_STEREO_IO_ALL)
FF_STEREO_IO_DEFINES := $(addprefix -D,$(STEREO_IO))
FF_STEREO_IO_TAG := $(if $(strip $(STEREO_IO)),-sio$(if $(findstring FEATURE_STEREO_REC,$(STEREO_IO)),R)$(if $(findstring FEATURE_RADIO_STEREO,$(STEREO_IO)),D)$(if $(findstring FEATURE_STEREO_SENDS,$(STEREO_IO)),S)$(if $(findstring FEATURE_STEREO_EDIT,$(STEREO_IO)),E)$(if $(findstring FEATURE_USB_AUDIO_IN,$(STEREO_IO)),U),)
FF_RELEASE_ARGS := SYNTH_OPEN=1 TRIM=1 PLUG_HOST=1 RELEASE=1 SYNTH_TAG=-rel$(FF_STEREO_IO_TAG) SYNTH_ENGINES="ACD FAT VAP WTFM PERC" FW_LTO="$(FF_RELEASE_LTO)" \
	SYNTH_OPT_ENGINES="-O2 -ffp-contract=off" SYNTH_OPT_FAT="-O2 -ffp-contract=off" \
	SYNTH_OPT_FM4="-O3 -ffp-contract=off" SYNTH_OPT_BTYPE="-O3 -ffp-contract=off" \
	USB_AUDIO=1 USB_MSC=1 CUSTOM_DEFINES="-DFEATURE_EXT_HOOKS -DFEATURE_NO_SKIPBACK$(FF_RELEASE_TABLE_DWT) $(FF_STEREO_IO_DEFINES)"

# The feature defines a release core object compiles with (FEATURE_*, FW_*, SYNTH_ENGINE_*), as core/makefile derives them from FF_RELEASE_ARGS.
# The engine defines come from SYNTH_ENGINES (synth-open/engines.mk), the version defines from version.mk: neither is listed here.
FF_RELEASE_CORE_DEFS := -DFW_TRIM -DFW_SE_PTFX -DFW_REVERB_SRAM=1 -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN -DFEATURE_PARTS -DFEATURE_PROJECT_STORE -DFEATURE_SAT_SOFTKNEE \
	-DFEATURE_PLUG_HOST -DFEATURE_FF_CORE -DFEATURE_SEND_DUCK -DFEATURE_MODIT_MODULE -DFEATURE_CAPTURE_MODULE -DFEATURE_THEME_MODULE -DFEATURE_SELFTEST_MODULE -DFEATURE_USB_MSC -DFEATURE_EXT_HOOKS -DFEATURE_NO_SKIPBACK \
	$(addprefix -D,$(FF_STEREO_IO_ALL))
# The platform side only (platforms/mk66/makefile): USB audio and the SDRAM file pools. The core never reads FEATURE_USB_AUDIO today, the
# simulator defines it anyway so a future core use is compiled the way the image compiles it.
FF_RELEASE_PLATFORM_DEFS := -DFEATURE_USB_AUDIO -DFW_SDRAM_FILE_POOLS
# Release core defines the simulator only has in its emulator bridge build (BRIDGE=1): the Sample Editor effects as card bytes need the bridge
FF_SIM_BRIDGE_ONLY_DEFS := -DFW_SE_PTFX
