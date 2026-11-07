# Host build of the real Tracker UI (core/ + synth-open/, the PLUG_HOST AUTO configuration) plus the FT812 model in this dir.
# Host only (macOS, clang). Nothing here is part of the firmware image. Driven by ui_shot.sh.
ROOT    := $(abspath ../../..)
HERE    := $(ROOT)/synth-open/tools/ui_shot
OUT     ?= $(ROOT)/build/ui-shot
OBJ     := $(OUT)/obj
GEN     := $(OUT)/gen
CORE    := $(ROOT)/core
SO      := $(ROOT)/synth-open
CXX     ?= clang++
CC      ?= clang

SYNTH_ENGINES ?= ACD FAT VAP WTFM PERC CHIP
include $(SO)/engines.mk
SYNTH_OPEN := 1
-include $(ROOT)/version.mk

# the overlay copies must exist before make resolves its pattern rules, so generate at parse time (idempotent, rewrites changed files only)
_gen := $(shell python3 -I $(HERE)/gen_overlay.py $(ROOT) $(GEN) 2>&1)
$(if $(_gen),$(error gen_overlay: $(_gen)))

# overlay sources (gen_shim.sh) replace their originals
OVERLAY_SRCS := plug_synth.cpp mtAudioEngine.cpp mtFileManagerOldVersionRecovery.cpp Tracker.cpp mtConfig.cpp fSampleRec.cpp effect_polyverb.cpp plug_host.cpp open_cold.cpp textBox.cpp
CORE_DIRS := source libs interface effector FileManager Exporter AudioEngine synth
CORE_CPP  := $(foreach d,$(CORE_DIRS),$(shell cd $(CORE) && find $(d) -name '*.cpp'))
CORE_CPP  := $(filter-out $(foreach o,$(OVERLAY_SRCS),%/$(o)),$(CORE_CPP))
SO_OVERLAY := $(filter plug_host.cpp plug_synth.cpp open_cold.cpp,$(OVERLAY_SRCS))
CORE_CPP  := $(filter-out $(foreach e,$(filter-out $(SYNTH_ENGINES_SEL),$(SYNTH_ENGINES_ALL)),$(SYNTH_ENGINE_CORE_SRCS_$(e))),$(CORE_CPP))
CORE_C    := $(shell cd $(CORE) && find libs -name '*.c')
SO_CPP    := $(filter-out $(SO_OVERLAY),$(filter-out engine_%.cpp,$(notdir $(wildcard $(SO)/*.cpp)))) $(addprefix ot/audio/,$(notdir $(wildcard $(SO)/ot/audio/*.cpp))) $(SYNTH_ENGINE_SRCS)
HOST_CPP  := $(wildcard $(HERE)/*.cpp)
PLUG_FX_C := ot_tape_plug ot_lofi_plug ot_bbd_plug ot_phaser_plug
# Console and Tube are private (poly-effects). ui_shot.sh puts their source at the pinned tag into $(EXT) (engines/external.py), without it
# it writes a stub with the same entry point and the two inserts show as failed to load. Standalone, the rule below writes the stub
EXT       ?= $(OUT)/external
PLUG_EXT_C := ff_console_plug ff_tube_plug
EXCLUDE   := $(shell cat $(HERE)/exclude.txt 2>/dev/null | grep -v '^\#')

OBJS := $(patsubst %.cpp,$(OBJ)/core/%.o,$(filter-out $(EXCLUDE),$(CORE_CPP))) \
        $(patsubst %.c,$(OBJ)/core/%.o,$(CORE_C)) \
        $(patsubst %.cpp,$(OBJ)/so/%.o,$(filter-out $(EXCLUDE),$(SO_CPP))) \
        $(patsubst %,$(OBJ)/gen/%.o,$(OVERLAY_SRCS)) \
        $(patsubst %,$(OBJ)/plugfx/%.o,$(PLUG_FX_C) $(PLUG_EXT_C)) \
        $(patsubst $(HERE)/%.cpp,$(OBJ)/host/%.o,$(HOST_CPP)) \
        $(OBJ)/plat/Keys.o

INC := -I$(HERE) -I$(GEN)/inc -I$(GEN)/src -I$(GEN)/libs/Audio -I$(GEN)/libs/Audio/utility -I$(GEN)/libs/Audio/Polyverb -I$(GEN)/libs -I$(CORE)/include -I$(CORE)/config -I$(CORE)/source -I$(CORE)/synth -I$(CORE)/libs \
  -I$(CORE)/libs/ElapsedMillis -I$(CORE)/libs/SerialFlash -I$(CORE)/libs/SPI  -I$(CORE)/libs/FT812 \
  -I$(CORE)/libs/fonts -I$(CORE)/libs/NewDisplay -I$(CORE)/interface -I$(CORE)/effector -I$(CORE)/FileManager -I$(CORE)/Exporter \
  -I$(CORE)/AudioEngine -I$(CORE)/AudioEngine/ConstantValues -I$(CORE)/AudioEngine/MasterEnhancer \
  -I$(ROOT)/util/include -I$(ROOT)/util/3rdparty -I$(ROOT)/synth/include -I$(SO) -I$(SO)/ot -I$(SO)/plug \
  -I$(ROOT)/platforms/mk66/3rdparty/Buttons
DEFS := -DFW_TRIM -DAUDIO_PERFORMANCE_INFO_ENABLED -DMEMORY_USAGE_INFO_ENABLED -D__NEWLIB__ -DKINETISK -D__MK66FX1M0__ -DF_CPU=180000000 \
  -DUSB_MIDI -DTEENSYDUINO=142 -DARDUINO=10805 -DLAYOUT_US_ENGLISH -DCPU_MK66FX1M0VMD18 $(FW_VERSION_DEFINES) \
  -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN $(SYNTH_ENGINE_DEFINES) -DFW_REVERB_SRAM=1 -DFEATURE_SAT_SOFTKNEE -DFEATURE_PLUG_HOST \
  -DENABLE_DEV_CONSOLE -DUI_SHOT_HOST -DTINY_OPTIONAL_x64 -DAUDIO_CORE_RAM= -DAUDIO_CORE_FLASH= \
  "-D_GLIBCXX_THROW_OR_ABORT(x)=__builtin_abort()" "-Dstatic_assert(...)="
WARN := -w
OPT ?= -O1
CXXFLAGS := -std=gnu++20 $(OPT) -g -fno-rtti -fno-exceptions -include cstdint -include assert.h -fno-strict-aliasing -ferror-limit=20 $(WARN) $(INC) $(DEFS)
CFLAGS   := $(OPT) -g -fno-strict-aliasing $(WARN) $(INC) $(DEFS)

all: $(OUT)/ui_shot

$(OUT)/ui_shot: $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS) -lz

$(GEN)/.stamp: $(HERE)/gen_overlay.py
	touch $@

# make 3.81 (macOS) takes the first matching pattern rule: the specific ones go first
$(OBJ)/core/libs/Audio/%.o: $(GEN)/libs/Audio/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/core/libs/Audio/%.o: $(GEN)/libs/Audio/%.c $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/core/%.o: $(CORE)/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/core/%.o: $(CORE)/%.c $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/so/%.o: $(SO)/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/gen/%.o: $(GEN)/src/% $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(CORE)/interface/sampleRecorder -MMD -MP -c $< -o $@
# the master plugins' own sources, natively, with the entry point renamed per file (host_twins.cpp)
$(OBJ)/plugfx/%.o: $(SO)/plug/fx/%.c $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Dplug_entry=plug_entry_$* -MMD -MP -c $< -o $@
$(OBJ)/plugfx/%.o: $(EXT)/%.c $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Dplug_entry=plug_entry_$* -MMD -MP -c $< -o $@
# kept after the build, so an unchanged stub does not rebuild its object
.SECONDARY: $(patsubst %,$(EXT)/%.c,$(PLUG_EXT_C))
$(EXT)/%.c:
	@mkdir -p $(dir $@)
	printf '/* stub: the private source of %s was not available */\nconst void *plug_entry(const void *host, unsigned abi) { (void)host; (void)abi; return 0; }\n' $* >$@
# the firmware's own key queue (USB test console), with host stand-ins for the Teensy core headers
$(OBJ)/plat/Keys.o: $(ROOT)/platforms/mk66/3rdparty/Buttons/Keys.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(HERE)/host_stubs -I$(ROOT)/platforms/mk66/3rdparty/I2C -MMD -MP -c $< -o $@
$(OBJ)/host/%.o: $(HERE)/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(shell find $(OBJ) -name '*.d' 2>/dev/null)
