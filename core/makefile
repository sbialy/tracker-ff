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
# Engine sources that live in core/ (Polyend's open Chiptune voice) are compiled only when their engine is picked
CPP_SRCS 	:= $(filter-out $(addprefix ./,$(foreach e,$(filter-out $(SYNTH_ENGINES_SEL),$(SYNTH_ENGINES_ALL)),$(SYNTH_ENGINE_CORE_SRCS_$(e)))),$(CPP_SRCS))
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
# Shared shim and OpenTracker core files always, engine sources (engine_<name>.cpp, ot/audio/engines, ot-synths) only
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
SYNTH_OPT_ENGINES ?=
SYNTH_OPT_GUI ?=
SYNTH_OPT_SHIM ?=
SYNTH_OPT_COLD ?=
SYNTH_OPT_FAT ?= $(SYNTH_OPT_ENGINES)
SYNTH_GUI_OBJS := $(addprefix $(OBJ_DIR)/,interface/instrumentEditor/SynthEditor.o interface/instrumentEditor/SynthPatchEditor.o interface/instrumentEditor/SynthMacros.o synth/Synth.o synth/FileSystemAdapter.o synth/MidiAdapter.o synth/registry.o)
$(filter $(OBJ_DIR)/synth-open/ot/% $(OBJ_DIR)/synth-open/ot-synths/% $(OBJ_DIR)/synth-open/phz/% $(OBJ_DIR)/synth-open/engine_chip.o,$(SYNTH_OPEN_OBJS)): EXTRA_OPT := $(SYNTH_OPT_ENGINES)
# CHIP (the voice and its glue) has its own knob, default the engines' flag. -O2 makes a voice about 12 % cheaper (12.7 % of a
# block instead of 14.4 %, default patch, synth-open/tools/phz_cycles/run_bench.sh -O2 CHIP) for +3.6 KB in the image
SYNTH_OPT_CHIP ?= $(SYNTH_OPT_ENGINES)
$(OBJ_DIR)/synth/engines/ChiptuneSynthVoice.o $(OBJ_DIR)/synth-open/engine_chip.o: EXTRA_OPT := $(SYNTH_OPT_CHIP)
# PHZ: -O2 and no fused multiply add. On the Cortex-M4F a VFMA / VMLA takes 3 cycles, a VMUL + VADD pair 2, and that is
# worth 20 % of the voice (synth-open/tools/phz_cycles). Measured: worst case 5.8 % of a block at these flags, 7.3 % at -Os
SYNTH_OPT_PHZ ?= -O2 -ffp-contract=off
$(OBJ_DIR)/synth-open/phz/phz.o: EXTRA_OPT := $(SYNTH_OPT_PHZ)
$(SYNTH_GUI_OBJS): EXTRA_OPT := $(SYNTH_OPT_GUI)
$(OBJ_DIR)/synth-open/open_synth.o: EXTRA_OPT := $(SYNTH_OPT_SHIM)
# Dirt: FMA fusing costs 3 cycles on the M4F against 2 for a separate multiply and add, so no contraction
SYNTH_OPT_DIRT ?= -O2 -ffp-contract=off
$(OBJ_DIR)/synth-open/ff_dirt.o: EXTRA_OPT := $(SYNTH_OPT_DIRT)
$(OBJ_DIR)/synth-open/ff_dirt_ctl.o: EXTRA_OPT := -Os
# plugin host: loader and bench are cold (-Os), the insert's per block path and the in-image Dirt plugin as the .plg build
$(OBJ_DIR)/synth-open/plug_host.o $(OBJ_DIR)/synth-open/plug_bench.o $(OBJ_DIR)/synth-open/plug_nvm.o $(OBJ_DIR)/synth-open/plug_ui.o $(OBJ_DIR)/synth-open/plug_synth.o: EXTRA_OPT := -Os
$(OBJ_DIR)/synth-open/plug_insert.o $(OBJ_DIR)/synth-open/plug_dirt_inimage.o: EXTRA_OPT := -O2 -ffp-contract=off
$(OBJ_DIR)/synth-open/open_cold.o $(OBJ_DIR)/synth-open/open_patchio.o $(OBJ_DIR)/synth-open/stub_fmt.o: EXTRA_OPT := $(SYNTH_OPT_COLD)
$(OBJ_DIR)/synth-open/ot/audio/engines/fat.o: EXTRA_OPT := $(SYNTH_OPT_FAT)

# TRIM=1: no-feature-loss flash trims for the SYNTH_OPEN image (synth-open/README.md, Flash trim).
#   -DFW_TRIM gates the source level trims: std::map registry, float math, PERC icons only with PERC.
#   OS_PATHS: -Os for the UI and cold dirs and core/source. Never AudioEngine/ or libs/Audio (DSP keeps -O2) and never
#   the engines (own per-engine flags above: explicit per-object EXTRA_OPT wins over these pattern rules).
#   The newlib-nano link and the platform at -Os are in platforms/mk66/makefile.
ifeq ($(TRIM),1)
DEFINES += -DFW_TRIM
TRIM_OS_PATHS ?= interface FileManager libs/NewDisplay effector Exporter synth source
$(foreach d,$(TRIM_OS_PATHS),$(eval $$(OBJ_DIR)/$(d)/%.o: EXTRA_OPT := -Os))
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

OBJS_SORTED := $(sort $(OBJS))
OBJ_DIRS	:= $(dir $(OBJS_SORTED))
DEPS		:= $(OBJS_SORTED:.o=.d)

INCLUDES=-I./include -I./config -I./source -I./synth -I./libs -I./libs/Audio -I./libs/ElapsedMillis -I./libs/SerialFlash -I./libs/SPI -I./libs/Audio/utility -I./libs/FT812 -I./libs/fonts -I./libs/NewDisplay -I./interface -I./effector -I./FileManager -I./Exporter -I./AudioEngine -I./libs/Audio/Polyverb -I./AudioEngine/ConstantValues -I./AudioEngine/MasterEnhancer -I${POLYUTIL_DIR}/include -I${POLYUTIL_DIR}/3rdparty -I${POLYSYNTH_DIR}/include  
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
ifeq ($(PLUG_HOST),1)
DEFINES+=-DFEATURE_PLUG_HOST
INCLUDES+=-I$(SYNTH_OPEN_DIR)/plug
ifeq ($(PLUG_BENCH),1)
DEFINES+=-DFEATURE_PLUG_BENCH
endif
endif
endif
else ifeq ($(PLATFORM), IMX)
COMPILER_OPTIONS+=-mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfp16-format=ieee
DEFINES+=-DPLATFORM_IMX -DFEATURE_SYNTH
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

$(TARGET): $(OBJS)
	$(TOOLCHAIN)ar -r "$(BUILD_DIR)/$(TARGET)" $(OBJS_SORTED)
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

pre-build:
	$(foreach dir,$(sort $(OBJ_DIRS)),mkdir -p $(dir);)
post-build: 

clean:
	-$(RM) $(BUILD_DIR)
	
-include $(DEPS)

.PHONY: build
