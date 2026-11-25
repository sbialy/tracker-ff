# Host build of the real Tracker UI (core/ + synth-open/, the PLUG_HOST AUTO configuration) plus the FT812 model in this dir.
# Host only (macOS, clang). Nothing here is part of the firmware image. Driven by ui_shot.sh.
ROOT    := $(abspath ../../..)
HERE    := $(ROOT)/synth-open/tools/ui_shot
# CONSOLE=1: the development console (ui_shot --serve, the USB test console's Mac side). Its code is in a separate repository
# (../../../devtools.mk, DEVTOOLS_DIR), the objects go to their own OUT (make does not track -D flags)
CONSOLE ?=
DEVTOOLS_WANT := $(CONSOLE)
include $(ROOT)/devtools.mk
# SAN=address,undefined: the same build with sanitizers (own OUT, make does not track flags)
SAN     ?=
# STACKMODEL=1: core/ and synth-open/ code calls the hooks of host_stackmodel.cpp, a virtual ARM stack (UISHOT_STACK_FRAMES=frame table)
STACKMODEL ?=
# EXTHOOKS=1: the runtime extension hooks of the one image (FEATURE_EXT_HOOKS, synth-open/ext_module.cpp): the loader rules run in
# develop mode, the host never enters the console module (its image id is 0, a card module is refused), every hook stays null.
# EXTHOOKS=1 also builds MOD/IT the way the release image has it (FEATURE_MODIT_MODULE): the three sources renamed (synth-open/modit/modit_names.h)
# behind modit_dispatch.cpp, the image's forwarders (core/FileManager/fileManagerModit.cpp) call them through ext_hooks.modit, which
# ext_module.cpp sets once /Plugins/Ext/modit.plg passed the loader rules (tools/modit/make_host_module.py writes a file that does)
EXTHOOKS ?=
# MODIT_ID / MODIT_RUN (EXTHOOKS=1): the release image's id (CRC-32 of its flash, 0x...) and the run address of its extension slot (0x...) that the host's loader rules
# compare modit.plg with (synth-open/ext_module.cpp, its host branch). Both set: only the release's own modit.plg is accepted. Unset: the stand-in module of make_host_module.py
MODIT_ID ?=
MODIT_RUN ?=
# USB_MSC=1: the USB drive of the original (FEATURE_USB_MSC): Config > USB > Storage, Config > Features > USB drive, the card gate
# (host_drive.h) and the Exit that reads /Workspace back. UISHOT_DRIVE_FLAG=1 starts with the drive enumerated
USB_MSC ?=
# SKIPBACK=1 (with EXTHOOKS=1): Skip-back built in (the release leaves it out with FEATURE_NO_SKIPBACK, the code stays for 1.5): skipback_sim.sh
SKIPBACK ?=
# STRUM_RAMP=0: the plain Strum velocity (no ramp, synth-open/plug/strum_ramp.h), for A/B renders. Give it its own OUT, make does not track flags
STRUM_RAMP ?=
# PARTS=0: the build without the Parts view (core/source/parts.h), as release-1.4.0-int was: the compat runs of parts_sim.sh
PARTS   ?= 1
# IMX_WERROR=1 (PLATFORM=IMX only, imx_check.sh): the core objects (core/, the overlay copies of core files in gen/) compile with the warning set of the real
# IMX image (core/makefile: -Wall -Wextra) as errors, host stand-ins and synth-open/ objects stay -w. Own OUT, make does not track flags
IMX_WERROR ?=
# BRIDGE=1 (UNICORN_DIR=<dir with include/ and lib/libunicorn.a>, the unpacked pinned wheel unicorn-2.1.4-cp37-abi3-macosx_11_0_arm64.whl, laid out by
# devtools tools/mirror/emu-release.sh): adds the Unicorn include dir (-DUISHOT_BRIDGE) and links libunicorn.a into ui_shot (the emulator bridge of the emu parity plan).
# Unicorn is GPL-2.0: internal tool only, never distribute a ui_shot built with BRIDGE=1. Without BRIDGE=1 nothing changes. Own OUT, make does not track flags
BRIDGE  ?=
UNICORN_DIR ?=
# PLATFORM=IMX: the Tracker+ simulator (ref/plus-emulator-plan.md). The core is compiled with -DPLATFORM_IMX (its display, menus, audio paths
# and memory sizes), objects and binary in their own OUT (build/ui-shot-plus...). PRODUCT=MINI adds -DTRACKER_MINI (not modelled yet).
# Unset (or MK66): the original Tracker, every command line exactly as before
PLATFORM ?=
PLUS_SIM := $(if $(filter IMX,$(PLATFORM)),1)
# PRODUCT is read by version.mk as well: only the plus sim sets it
ifeq ($(PLUS_SIM),1)
PRODUCT ?= PLUS
endif
$(if $(filter-out MK66 IMX,$(PLATFORM)),$(error PLATFORM=$(PLATFORM): MK66 or IMX))
# STEREO_IO="FEATURE_STEREO_SENDS ...": the poly-stereo-io steps compiled in (the release Makefile's STEREO_IO list), none by default
STEREO_IO ?=
_sio_sp := $(empty) $(empty)
OUT     ?= $(ROOT)/build/ui-shot$(if $(PLUS_SIM),-$(if $(filter MINI,$(PRODUCT)),mini,plus))$(if $(filter 0,$(PARTS)),-noparts)$(if $(filter 1,$(CONSOLE)),-console)$(if $(SAN),-san)$(if $(STACKMODEL),-stack)$(if $(filter 1,$(EXTHOOKS)),-ext)$(if $(filter 1,$(USB_MSC)),-msc)$(if $(filter 1,$(SKIPBACK)),-skipback)$(if $(filter 1,$(BRIDGE)),-bridge)$(if $(IMX_WERROR),-werror)$(if $(filter 1,$(USB_IN)),-usbin)$(if $(strip $(STEREO_IO)),-sio-$(subst $(_sio_sp),+,$(strip $(subst FEATURE_,,$(STEREO_IO)))))
OBJ     := $(OUT)/obj
GEN     := $(OUT)/gen
CORE    := $(ROOT)/core
SO      := $(ROOT)/synth-open
# GNU make has built-in CXX and CC (c++ and cc), so ?= never applied: take clang unless the command line or the environment names a compiler
ifeq ($(origin CXX),default)
CXX     := clang++
endif
ifeq ($(origin CC),default)
CC      := clang
endif
# ccache for the host compiles only (never the ARM toolchain): CCACHE=ccache|path, CCACHE= (empty) turns it off, default is ccache when it is on PATH.
# BASEDIR + no hash_dir make the key the same in every worktree of this repo (absolute paths under ROOT are hashed relative, the cwd is left out)
CCACHE ?= $(shell command -v ccache 2>/dev/null)
ifneq ($(CCACHE),)
export CCACHE_BASEDIR ?= $(ROOT)
export CCACHE_NOHASHDIR ?= 1
CXXC := $(CCACHE) $(CXX)
CCC  := $(CCACHE) $(CC)
else
CXXC := $(CXX)
CCC  := $(CC)
endif

SYNTH_ENGINES ?= ACD FAT VAP WTFM PERC
include $(SO)/engines.mk
SYNTH_OPEN := 1
-include $(ROOT)/version.mk

# the overlay copies must exist before make resolves its pattern rules, so generate at parse time (idempotent, rewrites changed files only)
_gen := $(shell $(if $(filter 1,$(BRIDGE)),UISHOT_BRIDGE=1 )$(if $(PLUS_SIM),UISHOT_PLUS=1 )python3 -I $(HERE)/gen_overlay.py $(ROOT) $(GEN) $(EXT) 2>&1)
$(if $(_gen),$(error gen_overlay: $(_gen)))

# overlay sources (gen_shim.sh) replace their originals
# plug_insert.cpp only with BRIDGE=1 (the insert lane): the plain build compiles the shipped file (its registers are the ffhw seam, ff_seams.h)
OVERLAY_SRCS := ptfx_loader.cpp fSampleEdit.cpp plug_synth.cpp mtAudioEngine.cpp mtFileManagerOldVersionRecovery.cpp Tracker.cpp mtConfig.cpp fSampleRec.cpp effect_polyverb.cpp plug_host.cpp $(if $(filter 1,$(BRIDGE)),plug_insert.cpp) open_cold.cpp textBox.cpp
CORE_DIRS := source libs interface effector FileManager Exporter AudioEngine synth
CORE_CPP  := $(foreach d,$(CORE_DIRS),$(shell cd $(CORE) && find $(d) -name '*.cpp'))
CORE_CPP  := $(filter-out $(foreach o,$(OVERLAY_SRCS),%/$(o)),$(CORE_CPP))
SO_OVERLAY := $(filter plug_host.cpp plug_insert.cpp plug_synth.cpp open_cold.cpp,$(OVERLAY_SRCS))
CORE_CPP  := $(filter-out $(foreach e,$(filter-out $(SYNTH_ENGINES_SEL),$(SYNTH_ENGINES_ALL) CHIP),$(SYNTH_ENGINE_CORE_SRCS_$(e))),$(CORE_CPP))
CORE_C    := $(shell cd $(CORE) && find libs -name '*.c')
SO_CPP    := $(filter-out $(SO_OVERLAY),$(filter-out engine_%.cpp,$(notdir $(wildcard $(SO)/*.cpp)))) $(addprefix ot/audio/,$(notdir $(wildcard $(SO)/ot/audio/*.cpp))) $(SYNTH_ENGINE_SRCS)
HOST_CPP  := $(wildcard $(HERE)/*.cpp)
# the emulator bridge (host_ucbridge.cpp, libunicorn) is BRIDGE=1 only: a plain build compiles exactly what it did before
HOST_CPP  := $(if $(filter 1,$(BRIDGE)),$(HOST_CPP),$(filter-out $(HERE)/host_ucbridge.cpp $(HERE)/host_nvm.cpp $(HERE)/host_synthcheck.cpp $(HERE)/host_plugrender.cpp,$(HOST_CPP)))
# PLATFORM=IMX BRIDGE=1 (the plus pin, round 5): the stock synth stack of a Tracker+ (Synth.cpp, the synth editor pages, the stock part output
# path, no FEATURE_SYNTH_OPEN) over the host polysynth:: proxy (host_plusproxy.cpp), one guest of the closed libPolySynth under Unicorn.
# Every other build never compiles the proxy. A plus build without BRIDGE has no Unicorn and keeps the open stack
PLUS_STOCK := $(and $(PLUS_SIM),$(filter 1,$(BRIDGE)))
HOST_CPP  := $(if $(PLUS_STOCK),$(HOST_CPP),$(filter-out $(HERE)/host_plusproxy.cpp,$(HOST_CPP)))
# and none of the open stack: its sources, its two overlays, the engine registry and the host built engines (the guest registers the lib's
# engines and the open Chiptune voice itself, plus_synth/build.sh)
ifneq ($(PLUS_STOCK),)
SO_CPP       := $(filter-out open_%.cpp ot/audio/% $(SYNTH_ENGINE_SRCS),$(SO_CPP))
OVERLAY_SRCS := $(filter-out open_cold.cpp,$(OVERLAY_SRCS))
CORE_CPP     := $(filter-out synth/registry.cpp synth/engines/%,$(CORE_CPP))
endif
# CONSOLE=1: the console core (as libTracker.a has it), the Mac transport and --serve, and the firmware's real key queue (Keys.cpp)
CONSOLE_OBJS := $(patsubst %.cpp,$(OBJ)/devtools/core/%.o,$(DEVTOOLS_CORE_SRCS)) $(patsubst %.cpp,$(OBJ)/devtools/host/%.o,$(DEVTOOLS_HOST_SRCS)) \
        $(if $(DEVTOOLS_HOST_SRCS),$(OBJ)/plat/Keys.o)
# Every master insert source comes from its module repository: Tape, Lo-Fi, BBD, Phaser and the Dirt wrapper from tracker-effects-os,
# Console, Tube and Glue from tracker-effects (private). ui_shot.sh puts their source at the pinned tag into $(EXT) (engines/external.py), without it
# it writes a stub with the same entry point and the two inserts show as failed to load. Standalone, the rule below writes the stub
EXT       ?= $(OUT)/external
PLUG_EXT_C := ot_tape_plug ot_lofi_plug ot_bbd_plug ot_phaser_plug ff_console_plug ff_tube_plug ff_glue_plug ff_sweepgate_plug ff_dustbed_plug ff_monobass_plug
EXCLUDE   := $(shell cat $(HERE)/exclude.txt 2>/dev/null | grep -v '^\#')

MODIT_SRCS := FileManager/fileManagerImportMod.cpp FileManager/fileManagerImportIt.cpp FileManager/fileManagerExportToMod.cpp
OBJS := $(patsubst %.cpp,$(OBJ)/core/%.o,$(filter-out $(EXCLUDE),$(CORE_CPP))) \
        $(patsubst %.c,$(OBJ)/core/%.o,$(CORE_C)) \
        $(patsubst %.cpp,$(OBJ)/so/%.o,$(filter-out $(EXCLUDE),$(SO_CPP))) \
        $(patsubst %,$(OBJ)/gen/%.o,$(OVERLAY_SRCS)) \
        $(patsubst %,$(OBJ)/plugfx/%.o,$(PLUG_EXT_C)) \
        $(patsubst $(HERE)/%.cpp,$(OBJ)/host/%.o,$(HOST_CPP)) \
        $(CONSOLE_OBJS) $(if $(filter 1,$(EXTHOOKS)),$(OBJ)/modit/modit_dispatch.o)

# PLATFORM=IMX: the REAL Tracker+ / Mini MIDI driver files (usb_midi.c, UsbMidiImpl.cpp, UartMidiImpl.cpp and the Arduino MIDI library they use) are
# compiled into the sim, with the NXP USB and LPUART layer below them stubbed by host_imxmidi.cpp (imx_stub/). The ARDUINO define of the MK66 host
# build is undone for them: the IMX image has none (midi_Defs.h would include Arduino.h). Raw MIDI goes in below the driver (usbmidi / dinmidi)
.DEFAULT_GOAL := all
IMXM_INC := -I$(HERE)/imx_stub -I$(ROOT)/platforms/imx/usb -I$(ROOT)/platforms/imx/source/Drivers -I$(ROOT)/platforms/imx/3rdparty/MIDI
IMXM_FLAGS := -w -UARDUINO $(IMXM_INC)
# usb_midi.c finds composite.h, usb_device_config.h and usb_device_descriptor.h of the real USB stack next to itself (a quote include looks in the
# including file's directory first): their include guards are defined, so the stub types of imx_midi_stub.h are the ones it sees
IMXM_GUARDS := -D_USB_DEVICE_COMPOSITE_H_ -D_USB_DEVICE_CONFIG_H_ -D_USB_DEVICE_DESCRIPTOR_H_
ifeq ($(PLUS_SIM),1)
OBJS += $(OBJ)/imxmidi/usb_midi.o $(OBJ)/imxmidi/UsbMidiImpl.o $(OBJ)/imxmidi/UartMidiImpl.o
$(OBJ)/host/host_platform.o $(OBJ)/host/host_imxmidi.o: CXXFLAGS += $(IMXM_INC)
$(OBJ)/imxmidi/%.o: $(ROOT)/platforms/imx/source/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXXC) $(CXXFLAGS) $(IMXM_FLAGS) -MMD -MP -c $< -o $@
$(OBJ)/imxmidi/usb_midi.o: $(ROOT)/platforms/imx/usb/usb_midi.c $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CCC) $(CFLAGS) $(IMXM_FLAGS) $(IMXM_GUARDS) -MMD -MP -c $< -o $@
endif

INC := -I$(HERE) -I$(GEN)/inc -I$(GEN)/src -I$(GEN)/libs/Audio -I$(GEN)/libs/Audio/utility -I$(GEN)/libs/Audio/Polyverb -I$(GEN)/libs -I$(CORE)/include -I$(CORE)/config -I$(CORE)/source -I$(CORE)/synth -I$(CORE)/libs \
  -I$(CORE)/libs/ElapsedMillis -I$(CORE)/libs/SerialFlash -I$(CORE)/libs/SPI  -I$(CORE)/libs/FT812 \
  -I$(CORE)/libs/fonts -I$(CORE)/libs/NewDisplay -I$(CORE)/interface -I$(CORE)/effector -I$(CORE)/FileManager -I$(CORE)/Exporter \
  -I$(CORE)/AudioEngine -I$(CORE)/AudioEngine/ConstantValues -I$(CORE)/AudioEngine/MasterEnhancer \
  -I$(ROOT)/util/include -I$(ROOT)/util/3rdparty -I$(ROOT)/synth/include -I$(SO) -I$(SO)/ot -I$(SO)/plug \
  -I$(ROOT)/platforms/mk66/3rdparty/Buttons $(DEVTOOLS_INC) $(DEVTOOLS_HOST_INC)
# The feature defines come from the release flavour (config/ff_release.mk, the list make original-release compiles with), never a copy here.
# The sim knobs only take flags out of it, each for a named reason (synth-open/tools/ff_flags_check.sh proves EXTHOOKS=1 USB_MSC=1 == the release
# core minus FF_SIM_BRIDGE_ONLY_DEFS):
#   EXTHOOKS unset   no FEATURE_EXT_HOOKS / FEATURE_MODIT_MODULE / FEATURE_CAPTURE_MODULE / FEATURE_THEME_MODULE / FEATURE_SELFTEST_MODULE
#                    (the plain sim, the goldens of most sims)
#   USB_MSC unset    no FEATURE_USB_MSC (the drive rows need host_drive.h)
#   PARTS=0          no FEATURE_PARTS (the compat runs of parts_sim.sh)
#   SKIPBACK=1       no FEATURE_NO_SKIPBACK: Skip-back is built (EXTHOOKS=1, skipback_sim.sh, the 1.5 code the release leaves out)
#   BRIDGE unset     no FF_SIM_BRIDGE_ONLY_DEFS (the Sample Editor effects as card bytes run only on the emulator bridge)
include $(ROOT)/config/ff_release.mk
SIM_FF_OFF := $(if $(filter 1,$(EXTHOOKS)),,-DFEATURE_EXT_HOOKS -DFEATURE_MODIT_MODULE -DFEATURE_CAPTURE_MODULE -DFEATURE_THEME_MODULE -DFEATURE_SELFTEST_MODULE) $(if $(filter 1,$(USB_MSC)),,-DFEATURE_USB_MSC) \
  $(if $(filter 0,$(PARTS)),-DFEATURE_PARTS) $(if $(filter 1,$(SKIPBACK)),-DFEATURE_NO_SKIPBACK) $(if $(filter 1,$(BRIDGE)),,$(FF_SIM_BRIDGE_ONLY_DEFS)) $(addprefix -D,$(FF_STEREO_IO_ALL))
SIM_FF_DEFS := $(filter-out $(SIM_FF_OFF),$(FF_RELEASE_CORE_DEFS)) $(filter -DFEATURE_USB_AUDIO,$(FF_RELEASE_PLATFORM_DEFS)) $(SYNTH_ENGINE_DEFINES)
DEFS := -DAUDIO_PERFORMANCE_INFO_ENABLED -DMEMORY_USAGE_INFO_ENABLED -D__NEWLIB__ -DKINETISK -D__MK66FX1M0__ -DF_CPU=180000000 \
  -DUSB_MIDI -DTEENSYDUINO=142 -DARDUINO=10805 -DLAYOUT_US_ENGLISH -DCPU_MK66FX1M0VMD18 $(FW_VERSION_DEFINES) \
  $(SIM_FF_DEFS) $(addprefix -D,$(STEREO_IO)) $(if $(filter 1,$(USB_IN)),-DFEATURE_USB_AUDIO_IN) \
  $(if $(DEVTOOLS_HOST_SRCS),-DENABLE_DEV_CONSOLE) $(if $(filter 1,$(EXTHOOKS)),$(if $(MODIT_ID),-DUISHOT_MODIT_ID=$(MODIT_ID)u) $(if $(MODIT_RUN),-DUISHOT_MODIT_RUN=$(MODIT_RUN)u)) -DUI_SHOT_HOST -DTINY_OPTIONAL_x64 -DAUDIO_CORE_RAM= -DAUDIO_CORE_FLASH= \
  $(if $(STRUM_RAMP),-DSTRUM_RAMP=$(STRUM_RAMP)) \
  "-D_GLIBCXX_THROW_OR_ABORT(x)=__builtin_abort()" "-Dstatic_assert(...)="
# the Tracker+ core (as core/makefile adds them for PLATFORM=IMX): FEATURE_SYNTH is already on. FEATURE_SEND_DUCK stays on (P11)
ifeq ($(PLUS_SIM),1)
DEFS := $(filter-out -DFEATURE_NO_SKIPBACK -DFEATURE_USB_AUDIO -DFEATURE_THEME_MODULE -DFEATURE_SELFTEST_MODULE,$(DEFS)) -DPLATFORM_IMX -DUISHOT_PLUS -DFEATURE_FF_FEATURES -DFEATURE_EXT_IMX -DFEATURE_SYNTH_PATCHFX $(if $(filter MINI,$(PRODUCT)),-DTRACKER_MINI)
endif
# poly-stereo-io: the compile time flags of the stereo steps (make OUT=... SIO_DEFS="-DFEATURE_STEREO_REC -DFEATURE_RADIO_STEREO")
DEFS += $(SIO_DEFS)
ifeq ($(BRIDGE),1)
$(if $(UNICORN_DIR),,$(error BRIDGE=1 needs UNICORN_DIR=<dir with include/unicorn/unicorn.h and lib/libunicorn.a>))
$(if $(wildcard $(UNICORN_DIR)/include/unicorn/unicorn.h),,$(error BRIDGE=1: $(UNICORN_DIR)/include/unicorn/unicorn.h not found))
$(if $(wildcard $(UNICORN_DIR)/lib/libunicorn.a),,$(error BRIDGE=1: $(UNICORN_DIR)/lib/libunicorn.a not found))
INC  += -I$(UNICORN_DIR)/include
# UISHOT_AXF at run time names the release axf the bridge maps (its image, its symbols), else this tree's make original-release output
BRIDGE_AXF ?= $(ROOT)/platforms/mk66/build-trim/PolyendTracker.axf
DEFS += -DUISHOT_BRIDGE -DUISHOT_DEFAULT_AXF='"$(BRIDGE_AXF)"'
# the Sample Editor effects are the card's /Effects/se_*.ptfx files run as bytes (the release recipe, T6): FW_SE_PTFX of FF_SIM_BRIDGE_ONLY_DEFS above.
# A plain build keeps them built in as the stock image does
LDFLAGS += $(UNICORN_DIR)/lib/libunicorn.a
endif
# PLATFORM=IMX: the Tracker+ / Mini extension module loader (synth-open/ext_imx.cpp, P10 H7) is on as in the image (FEATURE_EXT_IMX, the
# slot borrowed from the sample pool in develop mode). BRIDGE=1 adds the module under Unicorn (host_extimx.cpp) and the ext poke, without
# the bridge a module is refused at the entry
ifeq ($(PLUS_SIM)$(BRIDGE),11)
DEFS += -DUISHOT_EXT_IMX
endif
# phase 4: a stock Tracker+ CE core has none of the FF features (core/makefile: PLATFORM=IMX adds only -DPLATFORM_IMX -DFEATURE_SYNTH, the
# plus target passes no TRIM or SYNTH_OPEN). Kept: the host defines (UI_SHOT_HOST, ENABLE_DEV_CONSOLE for the Mac console). FEATURE_PLUG_HOST
# stays since P6 (core/makefile IMX: the master insert host, no engine plugins)
ifeq ($(PLUS_SIM),1)
# FEATURE_PROJECT_STORE, FEATURE_PLUG_HOST, FEATURE_FF_CORE, FEATURE_PARTS, FEATURE_SAT_SOFTKNEE and FEATURE_SEND_DUCK stay: core/makefile
# turns them on for PLATFORM=IMX (project.mtff and the master insert host without the open synth stack)
PLUS_FF_DEFS := -DFW_TRIM -DFW_REVERB_SRAM=1 -DFW_SE_PTFX
DEFS := $(filter-out $(PLUS_FF_DEFS),$(DEFS))
# the plus pin (PLUS_STOCK, BRIDGE=1, round 5): no FEATURE_SYNTH_OPEN and no engine defines either, as a Tracker+ image compiles the core. The
# synth parts are the stock stack over host_plusproxy.cpp. A plus build without BRIDGE (no Unicorn, no guest) keeps the open stack
ifneq ($(PLUS_STOCK),)
DEFS := $(filter-out -DFEATURE_SYNTH_OPEN $(SYNTH_ENGINE_DEFINES),$(DEFS)) -DUISHOT_PLUS_SYNTH_DEFAULT='"$(OUT)/plus-synth/harness.elf"'
endif
endif
# mixer.cpp keeps the real static_assert in the plus build (the host build blanks it everywhere else): AudioMixer<N> refuses N above
# AudioMixerBase::kMaxInputs (21 on PLATFORM_IMX, mixer.h), as the unit's compiler does
ifeq ($(PLUS_SIM),1)
$(OBJ)/core/libs/Audio/mixer.o: CXXFLAGS += -include $(HERE)/plus_mixer_sa.h
endif
# IMX_WNO lists the classes clang adds on top of what the real IMX compiler (GCC 10.3, core/makefile -Wall -Wextra) reports, so the gate counts
# what the unit's build would print
IMX_WNO ?= -Wno-macro-redefined -Wno-asm-operand-widths -Wno-non-c-typedef-for-linkage -Wno-deprecated-declarations -Wno-missing-braces \
  -Wno-literal-conversion -Wno-vla-cxx-extension -Wno-inconsistent-missing-override -Wno-unused-private-field -Wno-unused-const-variable \
  -Wno-tautological-constant-out-of-range-compare -Wno-tautological-overlap-compare -Wno-unused-lambda-capture -Wno-format -Wno-parentheses-equality \
  -Wno-unneeded-internal-declaration -Wno-unused-parameter --system-header-prefix=fmt/ --system-header-prefix=PolyUtil/
# known stock defects, kept as they are (OG image byte identical): textBox.cpp reads text_y before it is set (kb warnings-review)
$(OBJ)/gen/textBox.cpp.o: CXXFLAGS += -Wno-uninitialized
IMX_WARN := -Wall -Wextra -Werror -ferror-limit=0 $(IMX_WNO)
WARN := $(if $(IMX_WERROR),$(IMX_WARN),-w)
OPT ?= -O1
SANFLAGS := $(if $(SAN),-fsanitize=$(SAN) -fno-omit-frame-pointer -fno-sanitize=vptr -fsanitize-recover=address)
LDFLAGS += $(SANFLAGS)
INSTR := $(if $(STACKMODEL),-finstrument-functions)
ifneq ($(IMX_WERROR),)
$(if $(PLUS_SIM),,$(error IMX_WERROR=1 needs PLATFORM=IMX))
# everything that is not the firmware core stays silent: the host stand-ins, the open synth stack, the console, the module sources
$(OBJ)/host/%.o $(OBJ)/so/%.o $(OBJ)/plugfx/%.o $(OBJ)/devtools/core/%.o $(OBJ)/devtools/host/%.o $(OBJ)/plat/%.o $(OBJ)/modit/%.o: CXXFLAGS += -w
$(OBJ)/plugfx/%.o: CFLAGS += -w
$(OBJ)/gen/ext_module.cpp.o $(OBJ)/gen/plug_host.cpp.o $(OBJ)/gen/plug_insert.cpp.o $(OBJ)/gen/plug_synth.cpp.o $(OBJ)/gen/open_cold.cpp.o $(OBJ)/gen/open_synth.cpp.o: CXXFLAGS += -w
endif
CXXFLAGS := $(SANFLAGS) -std=gnu++20 $(OPT) -g -fno-rtti -fno-exceptions -include cstdint -include assert.h -fno-strict-aliasing -ferror-limit=20 $(WARN) $(INC) $(DEFS)
CFLAGS   := $(SANFLAGS) $(OPT) -g -fno-strict-aliasing $(WARN) $(INC) $(DEFS)

all: $(OUT)/ui_shot

# make does not track flags: a stamp named by a hash of the compile flags is a prerequisite of every object, so a flag change (a new release
# flag in config/ff_release.mk, another knob into the same OUT) rebuilds instead of linking stale objects
FLAGS_STAMP := $(OBJ)/.flags-$(shell printf '%s' '$(subst ','\'',$(CXXFLAGS) | $(CFLAGS) | $(LDFLAGS))' | shasum | cut -c1-12)
$(OBJS): $(FLAGS_STAMP)
$(FLAGS_STAMP):
	@mkdir -p $(@D) && rm -f $(OBJ)/.flags-* && touch $@

# the plus pin: the closed engine harness the proxy runs when UISHOT_PLUS_SYNTH is not set (the pin and the mirror set it to their own copy)
ifneq ($(PLUS_STOCK),)
all: $(OUT)/plus-synth/harness.elf
$(OUT)/plus-synth/harness.elf: $(SO)/tools/plus_synth/harness.cpp $(SO)/tools/plus_synth/link.ld $(SO)/tools/plus_synth/build.sh $(CORE)/synth/registry.cpp \
  $(CORE)/synth/engines/PlugSynthVoice.cpp $(CORE)/synth/engines/PlugSynthVoice.h $(SO)/plug_load.h
	bash $(SO)/tools/plus_synth/build.sh $(OUT)/plus-synth > $(OUT)/plus-synth.log 2>&1 || { tail -20 $(OUT)/plus-synth.log; exit 1; }
endif

# EXTHOOKS=1: the module's sources get the module's names, the dispatcher is the module's own file
$(patsubst %.cpp,$(OBJ)/core/%.o,$(MODIT_SRCS)) $(OBJ)/modit/modit_dispatch.o: CXXFLAGS += $(if $(filter 1,$(EXTHOOKS)),-include $(SO)/modit/modit_names.h)
$(OBJ)/modit/modit_dispatch.o: $(SO)/modit/modit_dispatch.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXXC) $(CXXFLAGS) -MMD -MP -c $< -o $@

# linked to a temp name and renamed (a new inode): a relink while sims run this binary (a sim's own make of the same OUT in run_tests'
# pool) never changes the file a running process maps, macOS kills a process whose executable is rewritten under it (tape_range_sim
# measured 0.0 ms for one run, poly-test-speed-r3)
$(OUT)/ui_shot: $(OBJS)
	$(CXX) -o $@.tmp.$$$$ $(OBJS) $(LDFLAGS) -lz && mv -f $@.tmp.$$$$ $@
	@# the symbols of the host's own objects, and the symbol table of the link with a host/firmware flag per function: host_heapmodel.cpp
	@# (UISHOT_HEAP=device) serves the host's own allocations from the system heap, uncounted
	@nm -U -j $(filter $(OBJ)/host/%.o $(OBJ)/devtools/host/%.o $(OBJ)/plat/%.o,$(OBJS)) 2>/dev/null | grep -v ':$$' | sed 's/^_//' | sort -u > $(OUT)/host_syms.txt
	@python3 -I $(HERE)/host_symtab.py $@ $(OUT)/host_syms.txt > $(OUT)/host_symtab.txt
	@# host_heapmodel.o needs the arena allocator of the firmware (new_arena.h) when this tree has it

$(GEN)/.stamp: $(HERE)/gen_overlay.py
	touch $@

# make 3.81 (macOS) takes the first matching pattern rule: the specific ones go first
$(OBJ)/core/libs/Audio/%.o: $(GEN)/libs/Audio/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXXC) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/core/libs/Audio/%.o: $(GEN)/libs/Audio/%.c $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CCC) $(CFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/core/%.o: $(CORE)/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXXC) $(CXXFLAGS) $(INSTR) -MMD -MP -c $< -o $@
$(OBJ)/core/%.o: $(CORE)/%.c $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CCC) $(CFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/so/%.o: $(SO)/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXXC) $(CXXFLAGS) $(INSTR) -MMD -MP -c $< -o $@
$(OBJ)/gen/%.o: $(GEN)/src/% $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXXC) $(CXXFLAGS) -I$(CORE)/interface/sampleRecorder -MMD -MP -c $< -o $@
# the master plugins' own sources, natively, with the entry point renamed per file (host_twins.cpp)
$(OBJ)/plugfx/%.o: $(EXT)/%.c $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CCC) $(CFLAGS) -Dplug_entry=plug_entry_$* -MMD -MP -c $< -o $@
# kept after the build, so an unchanged stub does not rebuild its object
.SECONDARY: $(patsubst %,$(EXT)/%.c,$(PLUG_EXT_C))
$(EXT)/%.c:
	@mkdir -p $(dir $@)
	printf '/* stub: the module source of %s was not available */\nconst void *plug_entry(const void *host, unsigned abi) { (void)host; (void)abi; return 0; }\n' $* >$@
# CONSOLE=1 (console checkout): the console sources, and the firmware's own key queue with host stand-ins for the Teensy core headers
$(OBJ)/devtools/core/%.o: $(DEVTOOLS_CORE_DIR)/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXXC) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/devtools/host/%.o: $(DEVTOOLS_HOST_DIR)/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXXC) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/plat/Keys.o: $(ROOT)/platforms/mk66/3rdparty/Buttons/Keys.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXXC) $(CXXFLAGS) -I$(DEVTOOLS_HOST_STUBS) -I$(ROOT)/platforms/mk66/3rdparty/I2C -MMD -MP -c $< -o $@
$(OBJ)/host/host_heapmodel.o: CXXFLAGS += -I$(ROOT)/platforms/mk66/3rdparty/teensy_core
$(OBJ)/host/%.o: $(HERE)/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXXC) $(CXXFLAGS) -MMD -MP -c $< -o $@

# BRIDGE=1: bridge-check proves the pinned library is linked (nm finds uc_open, uc_close in ui_shot) and runs uc_smoke.c, a standalone uc_open / map / run / uc_close
.PHONY: bridge-check
bridge-check: $(OUT)/ui_shot
	@test "$(BRIDGE)" = 1 || { echo "bridge-check needs BRIDGE=1 UNICORN_DIR=..." >&2; exit 2; }
	nm $(OUT)/ui_shot | grep -E ' _uc_(open|close|emu_start)$$'
	$(CC) -O1 -I$(UNICORN_DIR)/include -o $(OUT)/uc_smoke $(HERE)/uc_smoke.c $(UNICORN_DIR)/lib/libunicorn.a -lm -lpthread
	$(OUT)/uc_smoke

# BRIDGE=1: the bridge unit test (host_ucbridge_test.cc): every synth .plg of BRIDGE_CARD rendered through host_ucbridge from native code,
# byte compared with the prototype's renders in BRIDGE_REF (synth-open/tools/emu_parity/run.sh output, uc_<name>.f32)
BRIDGE_CARD ?= $(HOME)/prj/poly/build/emu/current/card/Plugins/Synth
BRIDGE_REF ?= $(HOME)/prj/poly/build/emu-parity/run1
.PHONY: bridge-test
bridge-test: $(OUT)/ui_shot $(OUT)/host_ucbridge_test
	@test "$(BRIDGE)" = 1 || { echo "bridge-test needs BRIDGE=1 UNICORN_DIR=..." >&2; exit 2; }
	$(OUT)/host_ucbridge_test $(BRIDGE_AXF) $(BRIDGE_CARD) $(BRIDGE_REF) 100 4
$(OUT)/host_ucbridge_test: $(HERE)/host_ucbridge_test.cc $(OBJ)/host/host_ucbridge.o
	$(CXX) $(CXXFLAGS) -ffp-contract=off -o $@ $< $(OBJ)/host/host_ucbridge.o $(LDFLAGS) -lz
# BRIDGE=1: the differential test of emu parity T5 (host_synthcheck_test.cc): for every synth .plg of BRIDGE_CARD placed in the FlexNVM window
# as the bank places it, and for corrupt and ABI mismatch variants of each, the image's own plug_synth start() (run under the bridge) and the
# overlay's ucbs::start (the image's calls plus the ports of checkSynth and checkChord) must give the same code, chord answer and fault
.PHONY: synth-start-test
synth-start-test: $(OUT)/host_synthcheck_test
	@test "$(BRIDGE)" = 1 || { echo "synth-start-test needs BRIDGE=1 UNICORN_DIR=..." >&2; exit 2; }
	$(OUT)/host_synthcheck_test $(BRIDGE_AXF) $(BRIDGE_CARD)
$(OUT)/host_synthcheck_test: $(HERE)/host_synthcheck_test.cc $(OBJ)/host/host_ucbridge.o $(OBJ)/host/host_synthcheck.o
	$(CXX) $(CXXFLAGS) -o $@ $< $(OBJ)/host/host_ucbridge.o $(OBJ)/host/host_synthcheck.o $(LDFLAGS) -lz

# BRIDGE=1: the insert and .ptfx proof of emu parity T6 (host_ucbridge_fx_test.cc): every master insert .plg and Sample Editor .ptfx of BRIDGE_FX_CARD
# (Plugins/Master, Effects) run through host_ucbridge, byte compared with the Python Unicorn reference of emu_parity/fx_ref.py (the image's own libm).
# bridge-fx-ref makes the reference (PY needs unicorn 2.1.4 and numpy, TOOLCHAIN=.../arm-none-eabi- for nm), bridge-fx-test compares
BRIDGE_FX_CARD ?= $(HOME)/prj/poly/build/emu/current/card
BRIDGE_FX_REF ?= $(ROOT)/build/bridge-fx-ref
PYENV_DIR ?= $(HOME)/prj/poly/build/pyenv
# the pinned python (synth-open/tools/pyenv.sh makes it), an explicit PY= wins
PY ?= $(if $(wildcard $(PYENV_DIR)/.pyenv-stamp),$(PYENV_DIR)/bin/python,$(error PY: no pinned python, run: sh synth-open/tools/pyenv.sh))
.PHONY: bridge-fx-ref bridge-fx-test
bridge-fx-ref:
	TOOLCHAIN=$(TOOLCHAIN) $(PY) -I $(ROOT)/synth-open/tools/emu_parity/fx_ref.py $(BRIDGE_AXF) $(BRIDGE_FX_CARD) $(BRIDGE_FX_REF)
bridge-fx-test: $(OUT)/host_ucbridge_fx_test
	@test "$(BRIDGE)" = 1 || { echo "bridge-fx-test needs BRIDGE=1 UNICORN_DIR=..." >&2; exit 2; }
	$(OUT)/host_ucbridge_fx_test $(BRIDGE_AXF) $(BRIDGE_FX_CARD) $(BRIDGE_FX_REF)
$(OUT)/host_ucbridge_fx_test: $(HERE)/host_ucbridge_fx_test.cc $(OBJ)/host/host_ucbridge.o
	$(CXX) $(CXXFLAGS) -ffp-contract=off -o $@ $< $(OBJ)/host/host_ucbridge.o $(LDFLAGS) -lz

# PLATFORM=IMX BRIDGE=1: IMX port P5 H8 (host_imxplug_test.cc): every synth .plg of BRIDGE_CARD loaded by plug::imx::load (the plus sim's
# plug_imx.o) into an IMX synth slot that Unicorn maps at 0x80000000, rendered and byte compared with the MK66 placement of the same
# engine (and with BRIDGE_REF's uc_<name>.f32 where present)
imx-plug-test: $(OUT)/host_imxplug_test
	@test "$(BRIDGE)" = 1 && test -n "$(PLUS_SIM)" || { echo "imx-plug-test needs PLATFORM=IMX BRIDGE=1 UNICORN_DIR=..." >&2; exit 2; }
	$(OUT)/host_imxplug_test $(BRIDGE_AXF) $(BRIDGE_CARD) $(BRIDGE_REF) 100 4
$(OUT)/host_imxplug_test: $(HERE)/host_imxplug_test.cc $(OBJ)/host/host_ucbridge.o $(OBJ)/so/plug_imx.o
	$(CXX) $(CXXFLAGS) -ffp-contract=off -o $@ $< $(OBJ)/host/host_ucbridge.o $(OBJ)/so/plug_imx.o $(LDFLAGS) -lz

# the dependency files are only read, never made: without this empty rule make tries to remake each included .d through the built-in
# %: %.o chain (plugfx/x.d from x.d.o from a $(EXT)/x.d.c stub) and prints the stub's compile errors on every incremental build
%.d: ;
-include $(shell find $(OBJ) -name '*.d' 2>/dev/null)

# the defines of this flavour, one per line (synth-open/tools/ff_flags_check.sh)
.PHONY: print-defs
print-defs:
	@printf '%s\n' $(DEFS)
