# Variables to provide (example values):
# TOOLCHAIN=/usr/local/bin/arm-none-eabi-
# PLATFORM=MK66 (MK66 and IMX are valid values)
# DEBUG=1 (any value turns on)

# Target configuration
TARGET		:= libTracker.a

# Build environment setup
.DEFAULT_GOAL := build

# SYNTH_OPEN=1 (MK66 only): opt-in synth code path backed by the open shim in SYNTH_OPEN_DIR.
# It uses its own build dir so plain and synth objects never mix.
# SYNTH_TAG names a build variant: its objects live in their own dir, so changing the
# SYNTH_OPT_* flags below never reuses objects built with other flags.
ifeq ($(SYNTH_OPEN),1)
# Engine picker (SYNTH_ENGINES, synth-open/engines.mk): every engine set other than the default has its own dir
SYNTH_OPEN_DIR ?= ../synth-open
include $(SYNTH_OPEN_DIR)/engines.mk
# TRIM=1 (flash trims, see below) has its own dir as well: it changes -D and -O flags that make's dependency files do not see
ifeq ($(TRIM),1)
TRIM_TAG := -trim
endif
BUILD_DIR   := build-$(PLATFORM)-$(PRODUCT)-SYNTHOPEN$(SYNTH_TAG)$(SYNTH_ENGINES_TAG)$(TRIM_TAG)
else
BUILD_DIR   := build-$(PLATFORM)-$(PRODUCT)
endif
OBJ_DIR 	:= $(BUILD_DIR)/obj/
RM 			:= rm -rf

CPP_SRCS 	+= $(shell find ./source/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./libs/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./interface/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./effector/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./FileManager/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./Exporter/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./AudioEngine/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./synth/ -name *.cpp)
ifeq ($(SYNTH_OPEN),1)
# Engine sources that live in core/ (Polyend's open Chiptune voice) are compiled only when their engine is picked, and CHIP is a
# plugin (chip.plg of tracker-engines-os) now, so a SYNTH_OPEN image never compiles it
CPP_SRCS 	:= $(filter-out $(addprefix ./,$(foreach e,$(filter-out $(SYNTH_ENGINES_SEL),$(SYNTH_ENGINES_ALL) CHIP),$(SYNTH_ENGINE_CORE_SRCS_$(e)))),$(CPP_SRCS))
endif
# MODIT_MODULE=1 (default with SYNTH_OPEN=1 PLUG_HOST=1 and -DFEATURE_EXT_HOOKS in CUSTOM_DEFINES, the release build): MOD import, IT import
# and IT export are the card module /Plugins/Ext/modit.plg (synth-open/modit/, build_modit.sh), not part of the image. The three sources
# go to the module, FileManager/fileManagerModit.cpp is the image's side (the entry points forward through ext_hooks.modit).
# PLUG_HOST and CUSTOM_DEFINES come from the command line (the Makefile's RELEASE_ARGS), so they are known here.
MODIT_MODULE ?= $(if $(and $(filter 1,$(SYNTH_OPEN)),$(filter 1,$(PLUG_HOST)),$(filter -DFEATURE_EXT_HOOKS,$(CUSTOM_DEFINES))),1,0)
ifeq ($(MODIT_MODULE),1)
CPP_SRCS 	:= $(filter-out ./FileManager/fileManagerImportMod.cpp ./FileManager/fileManagerImportIt.cpp ./FileManager/fileManagerExportToMod.cpp ./source/mtSequencer_captureMod.cpp ./source/mtSequencer_captureFile.cpp ./source/ThemeManagerJson.cpp ./source/mtTest.cpp,$(CPP_SRCS))
endif
C_SRCS 	 	+= $(shell find ./libs/ -name *.c)

_CPP_OBJS	= $(patsubst ./%.cpp, %.o, $(CPP_SRCS))
_C_OBJS		= $(patsubst ./%.c, %.o, $(C_SRCS))

OBJS		+= $(addprefix $(OBJ_DIR)/, $(_CPP_OBJS))
OBJS		+= $(addprefix $(OBJ_DIR)/, $(_C_OBJS))

ifeq ($(SYNTH_OPEN),1)
ifneq ($(PLATFORM),MK66)
$(error SYNTH_OPEN=1 is only valid for PLATFORM=MK66)
endif
# Shared shim and OpenTracker core files always, engine sources (engine_<name>.cpp, ot/audio/engines) only
# for the picked engines (synth-open/engines.mk)
SYNTH_OPEN_SRCS := $(filter-out $(SYNTH_OPEN_DIR)/engine_%.cpp,$(wildcard $(SYNTH_OPEN_DIR)/*.cpp)) $(wildcard $(SYNTH_OPEN_DIR)/ot/audio/*.cpp) $(addprefix $(SYNTH_OPEN_DIR)/,$(SYNTH_ENGINE_SRCS))
SYNTH_OPEN_OBJS := $(patsubst $(SYNTH_OPEN_DIR)/%.cpp,$(OBJ_DIR)/synth-open/%.o,$(SYNTH_OPEN_SRCS))
OBJS		+= $(SYNTH_OPEN_OBJS)

# Per-file optimisation overrides for flash fitting (appended after -O2, so the last -O wins).
#   SYNTH_OPT_ENGINES  OpenTracker engines (synth-open/ot/audio/engines/*.cpp)
#   SYNTH_OPT_GUI      cold synth GUI and glue (SynthEditor, SynthPatchEditor, SynthMacros, Synth.cpp, adapters, registry)
#   SYNTH_OPT_FAT      FAT engine only (defaults to SYNTH_OPT_ENGINES)
#   SYNTH_OPT_SHIM     the shim render path (synth-open/open_synth.cpp: instrument, voice pool, OpenPatch::effective), leave empty (-O2)
#   SYNTH_OPT_COLD     cold shim code (synth-open/open_cold.cpp, open_patchio.cpp, stub_fmt.cpp: metadata, value text, patch store, registry glue)
# Example: make original SYNTH_OPEN=1 SYNTH_TAG=-c SYNTH_OPT_ENGINES=-Os SYNTH_OPT_GUI=-Os
# FW_LTO=1 (opt-in): link time optimisation of the -Os objects only (UI, file manager, cold shim, platform). The -O2 audio objects
# (AudioEngine, libs/Audio, engines, open_synth) are compiled as before and stay out of the LTO set. The archive needs gcc-ar.
FW_LTO ?=
LTO_ALL := $(if $(filter 1,$(FW_LTO)),-flto)
# FW_LTO=ui: only the UI side (interface, FileManager, NewDisplay, effector, Exporter, synth GUI), no sequencer, platform or shim objects
LTO_UI := $(if $(filter 1 ui,$(FW_LTO)),-flto)
LTO_ANY := $(LTO_UI)
LTO_UI_PATHS := interface FileManager libs/NewDisplay effector Exporter
SYNTH_OPT_ENGINES ?=
SYNTH_OPT_GUI ?=
SYNTH_OPT_SHIM ?=
SYNTH_OPT_COLD ?=
SYNTH_OPT_FAT ?= $(SYNTH_OPT_ENGINES)
SYNTH_GUI_OBJS := $(addprefix $(OBJ_DIR)/,interface/instrumentEditor/SynthEditor.o interface/instrumentEditor/SynthPatchEditor.o interface/instrumentEditor/SynthMacros.o interface/instrumentEditor/SynthAutomation.o synth/Synth.o synth/FileSystemAdapter.o synth/MidiAdapter.o synth/registry.o)
$(filter $(OBJ_DIR)/synth-open/ot/%,$(SYNTH_OPEN_OBJS)): EXTRA_OPT := $(SYNTH_OPT_ENGINES)
# SYNTH_OPT_CHIP is accepted and unused: CHIP is chip.plg, built in tracker-engines-os with the engines' flags
SYNTH_OPT_CHIP ?= $(SYNTH_OPT_ENGINES)
# PHZ: -O2 and no fused multiply add. On the Cortex-M4F a VFMA / VMLA takes 3 cycles, a VMUL + VADD pair 2, and that is
# worth 20 % of the voice (synth-open/tools/phz_cycles). Measured: worst case 5.8 % of a block at these flags, 7.3 % at -Os
$(SYNTH_GUI_OBJS): EXTRA_OPT := $(SYNTH_OPT_GUI) $(LTO_UI)
$(OBJ_DIR)/synth-open/open_synth.o: EXTRA_OPT := $(SYNTH_OPT_SHIM)
# Dirt: FMA fusing costs 3 cycles on the M4F against 2 for a separate multiply and add, so no contraction
SYNTH_OPT_DIRT ?= -O2 -ffp-contract=off
$(OBJ_DIR)/synth-open/ff_dirt.o: EXTRA_OPT := $(SYNTH_OPT_DIRT)
$(OBJ_DIR)/synth-open/ff_dirt_ctl.o: EXTRA_OPT := -Os
# plugin host: loader and bench are cold (-Os), the insert's per block path and the in-image Dirt plugin as the .plg build
$(OBJ_DIR)/synth-open/plug_host.o $(OBJ_DIR)/synth-open/plug_bench.o $(OBJ_DIR)/synth-open/plug_nvm.o $(OBJ_DIR)/synth-open/plug_ui.o $(OBJ_DIR)/synth-open/plug_synth.o: EXTRA_OPT := -Os $(LTO_ALL)
$(OBJ_DIR)/synth-open/ext_module.o: EXTRA_OPT := -Os
$(OBJ_DIR)/synth-open/plug_insert.o $(OBJ_DIR)/synth-open/plug_dirt_inimage.o: EXTRA_OPT := -O2 -ffp-contract=off
# Tail pad (synth-open/ext_module.cpp): the top Makefile sizes it from the linked image and passes FF_TAIL_PAD. The stamp is named after the
# value, so ext_module.o is rebuilt exactly when the value changes (make does not track flags)
FF_TAIL_PAD ?= 0
TAILPAD_STAMP := $(OBJ_DIR)/synth-open/.tailpad-$(FF_TAIL_PAD)
$(OBJ_DIR)/synth-open/ext_module.o: COMPILER_OPTIONS += -DFF_TAIL_PAD=$(FF_TAIL_PAD)
$(OBJ_DIR)/synth-open/ext_module.o: $(TAILPAD_STAMP)
$(TAILPAD_STAMP):
	@mkdir -p $(dir $@)
	@rm -f $(dir $@).tailpad-*
	@touch $@
$(OBJ_DIR)/synth-open/open_cold.o $(OBJ_DIR)/synth-open/open_patchio.o $(OBJ_DIR)/synth-open/stub_fmt.o: EXTRA_OPT := $(SYNTH_OPT_COLD) $(LTO_ALL)
$(OBJ_DIR)/synth-open/ot/audio/engines/fat.o: EXTRA_OPT := $(SYNTH_OPT_FAT)
# FM4 and B-Type (-O3) are plugins built in tracker-engines-os, SYNTH_OPT_FM4 and SYNTH_OPT_BTYPE are accepted and unused.
# Our code under synth-open/ builds warning free with -Wall -Wextra on 10.3 and 12.3, so every warning there is an error, and only there
# (not core/, not the platform). The first six classes are named so they stay errors even if someone adds -Wno-error later: they found the
# real bugs in the stock code (wrapped unsigned clamps, a missing return, an uninitialised read). -Wstack-usage= stays a warning, the stack
# ratchet (synth-open/tools/stack_ratchet.py, make stack-ratchet) judges frame sizes against a committed baseline.
# A new compiler that adds a warning breaks the build here: fix the code, or demote the one class with -Wno-error=<class> below.
# make does not track flags: after changing this delete the core build dir of the SYNTH_TAG and platforms/mk66/build-trim.
SYNTH_OPEN_WERROR := -Werror=return-type -Werror=uninitialized -Werror=maybe-uninitialized -Werror=implicit-fallthrough -Werror=sign-compare -Werror=type-limits -Werror=unused-variable
SYNTH_OPEN_WERROR += -Werror -Wno-error=stack-usage=
$(SYNTH_OPEN_OBJS): COMPILER_OPTIONS += $(SYNTH_OPEN_WERROR)

# TRIM=1: no-feature-loss flash trims for the SYNTH_OPEN image (synth-open/README.md, Flash trim).
#   -DFW_TRIM gates the source level trims: std::map registry, float math, PERC icons only with PERC.
#   OS_PATHS: -Os for the UI and cold dirs and core/source. Never AudioEngine/ or libs/Audio (DSP keeps -O2) and never
#   the engines (own per-engine flags above: explicit per-object EXTRA_OPT wins over these pattern rules).
#   The newlib-nano link and the platform at -Os are in platforms/mk66/makefile.
ifeq ($(TRIM),1)
DEFINES += -DFW_TRIM
# -DFW_SE_PTFX: nine sample editor effects (Amplifier Overdrive Delay Bitcrusher Chorus Flanger Equalizer Limiter Compressor) are card
# files /Effects/se_*.ptfx (tracker-effects-os src/ptfx), their classes are not linked. The list rows stay, a row without its file reads
# "Missing: <name>". Stock builds (no TRIM) keep every effect built in.
DEFINES += -DFW_SE_PTFX
TRIM_OS_PATHS ?= interface FileManager libs/NewDisplay effector Exporter synth source
$(foreach d,$(TRIM_OS_PATHS),$(eval $$(OBJ_DIR)/$(d)/%.o: EXTRA_OPT := -Os $$(if $$(filter $(d),$(LTO_UI_PATHS)),$$(LTO_UI),$$(LTO_ALL))))
# Audio graph construction and the performance pad / note on glue run once at start or once per event, not per block: -Os frees flash
# for the idle-skip audio CPU work without touching a per-block path
# mtAudioSequencer holds the per step effect handlers (seqFx, endFx, fx*): they run once per sequencer step and effect, not per block (-3,056 B)
TRIM_OS_FILES ?= AudioEngine/AudioInput AudioEngine/MixerAndFxs AudioEngine/AudioVoice AudioEngine/AudioOutput AudioEngine/ExportQueues AudioEngine/mtAudioPerformanceMode AudioEngine/mtAudioNoteOnHandle AudioEngine/mtAudioSequencer
$(foreach f,$(TRIM_OS_FILES),$(eval $$(OBJ_DIR)/$(f).o: EXTRA_OPT := -Os))
endif
endif

ifeq ($(TRIM),1)
ifneq ($(SYNTH_OPEN),1)
$(error TRIM=1 only applies to SYNTH_OPEN=1 builds)
endif
endif

# Development console image (MK66, -DENABLE_DEV_CONSOLE): its sources come from a separate checkout (../devtools.mk). The objects
# keep the names they had under source/, so the sorted archive and the link order are the ones of a tree that still held them
ifeq ($(PLATFORM),MK66)
DEVTOOLS_WANT := $(if $(filter -DENABLE_DEV_CONSOLE -DENABLE_DEV_CONSOLE=%,$(CUSTOM_DEFINES)),1)
include ../devtools.mk
endif
DEVTOOLS_CORE_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/source/%.o,$(DEVTOOLS_CORE_SRCS))
OBJS		+= $(DEVTOOLS_CORE_OBJS)

# P5 plugin host core on Tracker+ and Mini (ref/ff-imx-port-plan.md R2.1b-d): plug_imx.cpp with plug_index.h and plug_load.h, the SDRAM
# code slots, the cache steps and the services table. No plug_nvm.cpp, plug_fccob.h or bank journal (FlexNVM is MK66 only). It reaches
# synth-open/ by relative includes, so the IMX core gets no synth-open include path. Until P6/P7 call it, no symbol of libcore.a's
# plug_imx.o is referenced and the link pulls nothing of it.
# P6 (H4) master insert host on IMX: FEATURE_PLUG_HOST with plug_host.cpp (both inserts in plug::imx::insertSlot, SDRAM), plug_insert.cpp
# and plug_ui.cpp (Inserts page, picker), -I synth-open and synth-open/plug for the core sites. The synth engine plugin side (plug_synth.cpp,
# plug_nvm.cpp, the FlexNVM bank) stays MK66 only: its core sites need FEATURE_PLUG_HOST && FEATURE_SYNTH_OPEN. plug_ui.cpp builds with
# -Wno-error=array-bounds: GCC 12.3 reports a false positive at plug_ui.cpp:144 (s_bound[col] after listAt(col), col is 0..6 there).
# Insert host SDRAM: 148,512 B, paid by the 144 KiB sample pool cut (core/source/mtStructs.h, user decision 2026-11-20).
ifeq ($(PLATFORM),IMX)
SYNTH_OPEN_DIR ?= ../synth-open
# ext_imx.cpp: the extension module loader (P10 H7), on with FEATURE_EXT_IMX (below). Its 48 KiB slot is borrowed from the sample pool in
# develop mode only (core/source/Tracker.cpp borrowExtSlot, user decision 2026-11-20): 0 B of static SDRAM
PLUG_IMX_SRCS := $(SYNTH_OPEN_DIR)/plug_imx.cpp $(SYNTH_OPEN_DIR)/plug_host.cpp $(SYNTH_OPEN_DIR)/plug_insert.cpp $(SYNTH_OPEN_DIR)/plug_ui.cpp $(SYNTH_OPEN_DIR)/ext_imx.cpp
PLUG_IMX_OBJS := $(patsubst $(SYNTH_OPEN_DIR)/%.cpp,$(OBJ_DIR)/synth-open/%.o,$(PLUG_IMX_SRCS))
OBJS		+= $(PLUG_IMX_OBJS)
$(PLUG_IMX_OBJS): COMPILER_OPTIONS += -Werror -Wno-error=stack-usage=
$(OBJ_DIR)/synth-open/plug_ui.o: COMPILER_OPTIONS += -Wno-error=array-bounds
endif
OBJS_SORTED := $(sort $(OBJS))
OBJ_DIRS	:= $(dir $(OBJS_SORTED))
DEPS		:= $(OBJS_SORTED:.o=.d)

INCLUDES=-I./include -I./config -I./source -I./synth -I./libs -I./libs/Audio -I./libs/ElapsedMillis -I./libs/SerialFlash -I./libs/SPI -I./libs/Audio/utility -I./libs/FT812 -I./libs/fonts -I./libs/NewDisplay -I./interface -I./effector -I./FileManager -I./Exporter -I./AudioEngine -I./libs/Audio/Polyverb -I./AudioEngine/ConstantValues -I./AudioEngine/MasterEnhancer -I${POLYUTIL_DIR}/include -I${POLYUTIL_DIR}/3rdparty -I${POLYSYNTH_DIR}/include  
INCLUDES+=$(DEVTOOLS_INC)
COMPILER_OPTIONS=-MMD -MP -fno-common -g -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -fstack-usage -specs=nano.specs -Wall -Wextra -c -ffunction-sections -fdata-sections -ffreestanding -fno-builtin 
ifdef DEBUG
COMPILER_OPTIONS+=-Og -DDEBUG
else
COMPILER_OPTIONS+=-O2 -DNDEBUG
endif

CPP_OPTIONS=-std=gnu++20 -felide-constructors -includecstdint -fno-rtti -fno-exceptions 
CC_OPTIONS=

DEFINES+=-DAUDIO_PERFORMANCE_INFO_ENABLED -DMEMORY_USAGE_INFO_ENABLED -D__NEWLIB__ -DKINETISK -D__MK66FX1M0__ -DF_CPU=180000000 -DUSB_MIDI -DTEENSYDUINO=142 -DARDUINO=10805 -DLAYOUT_US_ENGLISH -DCPU_MK66FX1M0VMD18
POLYUTIL_DIR ?= ../util
POLYSYNTH_DIR ?= ../synth
-include ../version.mk
DEFINES += $(FW_VERSION_DEFINES) $(CUSTOM_DEFINES)

ifeq ($(PLATFORM), MK66)
COMPILER_OPTIONS+=-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 
# Crash-class warnings (crash class audit), warn only, 0 B of flash: the audio ISR and the main loop share one stack
# with no guard page, so a frame of 512 B or more (-Wstack-usage) or a VLA (-Wvla) deserves a look. The image already has about 30 hits,
# watch for NEW names. Not -Wconversion (4,951 lines), see the lint target below. CRASH_WARN= switches them off.
CRASH_WARN ?= -Wstack-usage=512 -Wvla
COMPILER_OPTIONS+=$(CRASH_WARN)
# Parts: the Parts view of the Song screen, a song built from Parts kept in the project's project.mtff sidecar (core/source/parts.h,
# ref/parts.md). It needs the plugin host (PLUG_HOST=1) that owns project.mtff: the default is on with it and off without it. PARTS=0
# builds without it (stock behaviour, a define is not a make dependency: wipe the object dir between them)
PARTS ?= $(if $(filter 1,$(PLUG_HOST)),1,0)
ifeq ($(PARTS),1)
ifneq ($(PLUG_HOST),1)
$(error PARTS=1 needs PLUG_HOST=1: the Parts list is stored in project.mtff)
endif
endif
ifeq ($(PARTS),1)
DEFINES+=-DFEATURE_PARTS
endif
# PROJECT_STORE=1 (default with PLUG_HOST=1): project.mtff, the FF settings of a project (core/FileManager/projectSidecar.h), a core
# store of its own that the file manager calls through ff_hooks.h. Default on with PLUG_HOST=1 here, and always on Tracker+ and Mini (the
# PLATFORM=IMX branch below): a build without the open synth stack takes the card file layer headers of synth-open (plug_sidecar_io.h,
# open_fileops.h) and the store's own stat.
PROJECT_STORE ?= $(if $(filter 1,$(PLUG_HOST)),1,0)
ifeq ($(PROJECT_STORE),1)
DEFINES+=-DFEATURE_PROJECT_STORE
ifneq ($(SYNTH_OPEN),1)
SYNTH_OPEN_DIR ?= ../synth-open
INCLUDES+=-I$(SYNTH_OPEN_DIR)
endif
endif
ifeq ($(SYNTH_OPEN),1)
# Patch.h uses __fp16. FEATURE_SYNTH_OPEN also makes interfaceDefs.h set GUI_HANDLE_SYNTH=1.
COMPILER_OPTIONS+=-mfp16-format=ieee
DEFINES+=-DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN $(SYNTH_ENGINE_DEFINES)
# Reverb line buffers: 0 = both in SDRAM (stock), 1 = the hot buffer E (32 KB, 70 percent of the reverb memory traffic) in SRAM,
# 2 = E and E2 (64 KB) in SRAM. Costs that much heap (RAM left for malloc), see synth-open/README.md, Reverb in SRAM. A define is not a
# make dependency: give a variant its own SYNTH_TAG (make original SYNTH_OPEN=1 REVERB_SRAM=2 SYNTH_TAG=-rv2) or wipe the object dir.
REVERB_SRAM ?= 1
DEFINES+=-DFW_REVERB_SRAM=$(REVERB_SRAM)
INCLUDES+=-I$(SYNTH_OPEN_DIR) -I$(SYNTH_OPEN_DIR)/ot
# FF_DIRT=1 (default): the live Dirt master bus insert (synth-open/ff_dirt.cpp), five new entries in the master screen's Saturation list. FF_DIRT=0 leaves
# it out (the class stays unused, the list keeps its five stock entries). A define is not a make dependency: build FF_DIRT=0 with its own SYNTH_TAG.
# PLUG_HOST=1 drops the five built-in Dirt entries by default (dirt.plg replaces them, synth-open/plug_host.cpp loads a project
# saved with one as Inserts with Insert 1 = Dirt). ff_dirt.cpp and ff_dirt_ctl.cpp are still compiled (build_plugs.sh links
# them into dirt.plg as the reference), the linker drops them from the image.
ifeq ($(PLUG_HOST),1)
FF_DIRT ?= 0
endif
FF_DIRT ?= 1
ifeq ($(FF_DIRT),1)
DEFINES+=-DFEATURE_FF_DIRT
endif
# SAT_SOFTKNEE=1 (default with PLUG_HOST=1): the "Soft Knee" mixer saturation type (synth-open/sat_softknee.h), project value
# 10 in every build (stock firmware loads it as Hard Clip). Needs PLUG_HOST=1: its Saturation list maps list positions to values.
SAT_SOFTKNEE ?= $(if $(filter 1,$(PLUG_HOST)),1,0)
ifeq ($(SAT_SOFTKNEE),1)
ifneq ($(PLUG_HOST),1)
$(error SAT_SOFTKNEE=1 needs PLUG_HOST=1)
endif
DEFINES+=-DFEATURE_SAT_SOFTKNEE
endif
# PLUG_HOST=1: runtime plugin host, master insert host v1 (synth-open/plug_*.cpp, plug/README.md). PLUG_BENCH=1 adds the boot bench.
# A define is not a make dependency: give each variant its own SYNTH_TAG.
PLUG_HOST ?= 0
PLUG_BENCH ?= 0
ifeq ($(MODIT_MODULE),1)
DEFINES+=-DFEATURE_MODIT_MODULE -DFEATURE_CAPTURE_MODULE -DFEATURE_THEME_MODULE -DFEATURE_SELFTEST_MODULE
endif
ifeq ($(PLUG_HOST),1)
DEFINES+=-DFEATURE_PLUG_HOST -DFEATURE_FF_CORE
INCLUDES+=-I$(SYNTH_OPEN_DIR)/plug
# SEND_DUCK=1 (default with PLUG_HOST=1): send ducking, a chosen track or synth part pulls the delay and reverb returns down
# (core/libs/Audio/effect_duck.h). The settings live in the plugin sidecar, so it needs the plugin host. SEND_DUCK=0 is the build without it.
SEND_DUCK ?= 1
ifeq ($(SEND_DUCK),1)
DEFINES+=-DFEATURE_SEND_DUCK
endif
ifeq ($(PLUG_BENCH),1)
DEFINES+=-DFEATURE_PLUG_BENCH
endif
endif
endif
# USB_MSC=1 (platforms/mk66/makefile has the same flag): the SD card as a USB drive in Drive mode (Config > USB > Storage,
# Config > Features > USB drive, off by default). The core side: the Storage row on MK66 and the Exit that reloads /Workspace.
# A define is not a make dependency: build it in a clean core build dir.
USB_MSC ?= 0
ifeq ($(USB_MSC),1)
DEFINES+=-DFEATURE_USB_MSC
endif
else ifeq ($(PLATFORM), IMX)
COMPILER_OPTIONS+=-mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfp16-format=ieee
DEFINES+=-DPLATFORM_IMX -DFEATURE_SYNTH
# project.mtff on Tracker+ and Mini (PROJECT_STORE=0 leaves it out): the store without the plugin host, see the MK66 branch above
PROJECT_STORE ?= 1
ifeq ($(PROJECT_STORE),1)
SYNTH_OPEN_DIR ?= ../synth-open
DEFINES+=-DFEATURE_PROJECT_STORE
INCLUDES+=-I$(SYNTH_OPEN_DIR)
endif
# FF core features on Tracker+ and Mini (FEATURE_FF_CORE): the project.mtff features that need no plugin host (core/FileManager/projectSidecar.cpp,
# FEATURE_PROJECT_STORE, owns the file): Project Settings page, FX3/FX4, Scale snap, Follow chord, Seed, Groove, the newer FF popup, Soft Knee, Parts. On the MK66
# release the same define comes with PLUG_HOST=1. PARTS=0 builds without the Parts view (a define is not a make dependency: wipe the object dir)
PARTS ?= 1
# FEATURE_FF_FEATURES (core/source/feature_flags.h): Config > Features with the MIDI remote, Play stop lock and Chord names, the settings in /Features/<id>.cfg
DEFINES+=-DFEATURE_FF_CORE -DFEATURE_SAT_SOFTKNEE -DFEATURE_FF_FEATURES -DFEATURE_PLUG_HOST
# the extension module loader (synth-open/ext_imx.cpp, P10): /Plugins/Ext/module.plg in develop mode, slot borrowed from the sample pool
DEFINES+=-DFEATURE_EXT_IMX
# the master insert host's core sites (plug_host.h, plug_ui.h) and synth-open/plug for plug_api.h, P6
INCLUDES+=-I$(SYNTH_OPEN_DIR) -I$(SYNTH_OPEN_DIR)/plug
# P7 H5: the plugin synth engine adapter reads the OpenTracker engine interface (synth-open/ot/audio/synth_engine.h), that object only
$(filter %/synth/engines/PlugSynthVoice.o,$(OBJS)): INCLUDES += -I$(SYNTH_OPEN_DIR)/ot
# Send ducking on Tracker+ and Mini (P11): the settings are in project.mtff (core), the detector is wired in connectSendDuck
SEND_DUCK ?= 1
ifeq ($(SEND_DUCK),1)
DEFINES+=-DFEATURE_SEND_DUCK
endif
ifneq ($(PARTS),0)
DEFINES+=-DFEATURE_PARTS
endif
# P12: the Patch step FX and its bank of 16 patch slots per synth part over the stock polysynth:: patch API (core/synth/Synth.cpp, the bank's
# 17,160 B in SDRAM, paid by the IMX sample pool cut). The MK66 has the same FX inside FEATURE_SYNTH_OPEN
DEFINES+=-DFEATURE_SYNTH_PATCHFX
else 
$(error Invalid PLATFORM value $(PLATFORM))
endif

ifeq ($(PRODUCT), MINI)
DEFINES+=-DTRACKER_MINI
else ifeq ($(PRODUCT), ORIGINAL)
else ifeq ($(PRODUCT), PLUS)
else 
$(error Invalid PRODUCT value $(PRODUCT))
endif




# Build rules

$(OBJ_DIR)/%.o: %.cpp
	$(TOOLCHAIN)c++ $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) $(EXTRA_OPT) -o "$@" "$<"

$(OBJ_DIR)/synth-open/%.o: $(SYNTH_OPEN_DIR)/%.cpp
	$(TOOLCHAIN)c++ $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) $(EXTRA_OPT) -o "$@" "$<"

$(OBJ_DIR)/%.o: %.c
	$(TOOLCHAIN)gcc $(DEFINES) $(INCLUDES) $(CC_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

ifneq ($(DEVTOOLS_CORE_OBJS),)
$(DEVTOOLS_CORE_OBJS): $(OBJ_DIR)/source/%.o: $(DEVTOOLS_CORE_DIR)/%.cpp
	$(TOOLCHAIN)c++ $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) $(EXTRA_OPT) -o "$@" "$<"
endif

# MODIT_MODULE=1: the objects of modit.plg, built here with the image's own compiler line (same defines, same flags, -Os with TRIM=1 as the
# FileManager folder has it) and the forced include that gives the three sources the module's member names (synth-open/modit/modit_names.h).
# They are not archived into libTracker.a: synth-open/modit/build_modit.sh links them against the finished image. Their .su files sit in
# the build dir like every other one, so the stack ratchet reads the module's frames too.
ifeq ($(MODIT_MODULE),1)
MODIT_OBJS := $(addprefix $(OBJ_DIR)/modit/,fileManagerImportMod.o fileManagerImportIt.o fileManagerExportToMod.o modit_dispatch.o modit_entry.o mtSequencer_captureMod.o mtSequencer_captureFile.o ThemeManagerJson.o mtTest.o)
$(MODIT_OBJS): COMPILER_OPTIONS += -include $(abspath $(SYNTH_OPEN_DIR))/modit/modit_names.h
$(MODIT_OBJS): EXTRA_OPT := $(if $(filter 1,$(TRIM)),-Os)
$(OBJ_DIR)/modit/fileManager%.o: FileManager/fileManager%.cpp
	@mkdir -p $(dir $@)
	$(TOOLCHAIN)c++ $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) $(EXTRA_OPT) -o "$@" "$<"
# the theme and self test tenants of extras.plg (EXT_SUB_THEME, EXT_SUB_SELFTEST): the custom theme JSON parser and the factory self test
$(OBJ_DIR)/modit/ThemeManagerJson.o: source/ThemeManagerJson.cpp
	@mkdir -p $(dir $@)
	$(TOOLCHAIN)c++ $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) $(EXTRA_OPT) -o "$@" "$<"
$(OBJ_DIR)/modit/mtTest.o: source/mtTest.cpp
	@mkdir -p $(dir $@)
	$(TOOLCHAIN)c++ $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) $(EXTRA_OPT) -o "$@" "$<"
# the Capture tenant of extras.plg (EXT_SUB_CAPTURE): the take logic, no renames (the image has no Sequencer::captureOp)
$(OBJ_DIR)/modit/mtSequencer_captureMod.o: source/mtSequencer_captureMod.cpp
	@mkdir -p $(dir $@)
	$(TOOLCHAIN)c++ $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) $(EXTRA_OPT) -o "$@" "$<"
# the take file writer and reader, the second Capture tenant (EXT_SUB_TAKEFILE)
$(OBJ_DIR)/modit/mtSequencer_captureFile.o: source/mtSequencer_captureFile.cpp
	@mkdir -p $(dir $@)
	$(TOOLCHAIN)c++ $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) $(EXTRA_OPT) -o "$@" "$<"
$(OBJ_DIR)/modit/modit_%.o: $(SYNTH_OPEN_DIR)/modit/modit_%.cpp
	@mkdir -p $(dir $@)
	$(TOOLCHAIN)c++ $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) $(EXTRA_OPT) -o "$@" "$<"
$(TARGET): $(MODIT_OBJS)
endif

$(TARGET): $(OBJS)
	$(TOOLCHAIN)$(if $(LTO_ANY),gcc-ar,ar) -r "$(BUILD_DIR)/$(TARGET)" $(OBJS_SORTED)
	$(MAKE) --no-print-directory post-build
	
# Main rules

build:
	$(MAKE) --no-print-directory pre-build
	$(MAKE) --no-print-directory main-build

main-build: $(TARGET)

ifeq ($(SYNTH_OPEN),1)
# Compile only the objects of the picked engines (no library, no link): make synth-engines uses it to size every engine
# without having to fit the sum into flash
ENGINE_OBJS := $(addprefix $(OBJ_DIR)/synth-open/,$(SYNTH_ENGINE_SRCS:.cpp=.o)) $(addprefix $(OBJ_DIR)/,$(foreach e,$(SYNTH_ENGINES_SEL),$(SYNTH_ENGINE_CORE_SRCS_$(e):.cpp=.o)))
engine-objs:
	$(MAKE) --no-print-directory pre-build
	$(MAKE) --no-print-directory $(ENGINE_OBJS)
.PHONY: engine-objs
endif

# make lint-original (top Makefile): the warnings the release flags hide, on the files that count, output to /dev/null, nothing is built.
# The release flags carry -ffreestanding -fno-builtin, which turn the format and string bound checks off, so lint runs with builtins on,
# plus -Wconversion scoped to the file manager, the sequencer file code and (SYNTH_OPEN=1) the plugin host and shim: narrowing into
# counts and indexes is where the device crashes came from (about 130 sites there, against 4,951 for the whole tree).
# Add -Wconversion to a single file for good with `#pragma GCC diagnostic error "-Wconversion"` after cleaning it.
LINT_SRCS := $(wildcard ./FileManager/*.cpp ./source/File*.cpp) $(if $(filter 1,$(SYNTH_OPEN)),$(wildcard $(SYNTH_OPEN_DIR)/plug*.cpp $(SYNTH_OPEN_DIR)/open_*.cpp))
LINT_OPTIONS := $(filter-out -c -g -MMD -MP -ffreestanding -fno-builtin -fstack-usage,$(COMPILER_OPTIONS)) -S -Wconversion -Wno-sign-conversion -Wformat-overflow=2 -Wformat-truncation=2 -Warray-bounds=2 -Wstringop-overflow=4 -Wnull-dereference
lint:
	@for f in $(LINT_SRCS); do $(TOOLCHAIN)c++ $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(LINT_OPTIONS) $(EXTRA_OPT) -o /dev/null "$$f" || true; done
.PHONY: lint

pre-build:
	$(foreach dir,$(sort $(OBJ_DIRS)),mkdir -p $(dir);)
post-build: 

clean:
	-$(RM) $(BUILD_DIR)
	
-include $(DEPS)

.PHONY: build
