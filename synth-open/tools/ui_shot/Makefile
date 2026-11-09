# Host build of the real Tracker UI (core/ + synth-open/, the PLUG_HOST AUTO configuration) plus the FT812 model in this dir.
# Host only (macOS, clang). Nothing here is part of the firmware image. Driven by ui_shot.sh.
ROOT    := $(abspath ../../..)
HERE    := $(ROOT)/synth-open/tools/ui_shot
# CONSOLE=1: the development console (ui_shot --serve, the USB test console's Mac side). Its code is in the private poly-devtools
# (../../../devtools.mk, DEVTOOLS_DIR), the objects go to their own OUT (make does not track -D flags)
CONSOLE ?=
DEVTOOLS_WANT := $(CONSOLE)
include $(ROOT)/devtools.mk
# SAN=address,undefined: the same build with sanitizers (own OUT, make does not track flags), see ref/newlist-fault-verdict.md
SAN     ?=
# STACKMODEL=1: core/ and synth-open/ code calls the hooks of host_stackmodel.cpp, a virtual ARM stack (UISHOT_STACK_FRAMES=frame table)
STACKMODEL ?=
# MCU_NULL=1: the display controls (core/libs/NewDisplay) read a null text as the MCU does (address 0 is readable flash there), host_mcunull.h
MCU_NULL ?=
OUT     ?= $(ROOT)/build/ui-shot$(if $(filter 1,$(CONSOLE)),-console)$(if $(SAN),-san)$(if $(STACKMODEL),-stack)
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
_gen := $(shell python3 -I $(HERE)/gen_overlay.py $(ROOT) $(GEN) $(EXT) 2>&1)
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
# CONSOLE=1: the console core (as libTracker.a has it), the Mac transport and --serve, and the firmware's real key queue (Keys.cpp)
CONSOLE_OBJS := $(patsubst %.cpp,$(OBJ)/devtools/core/%.o,$(DEVTOOLS_CORE_SRCS)) $(patsubst %.cpp,$(OBJ)/devtools/host/%.o,$(DEVTOOLS_HOST_SRCS)) \
        $(if $(DEVTOOLS_HOST_SRCS),$(OBJ)/plat/Keys.o)
# Every master insert source comes from its module repository: Tape, Lo-Fi, BBD, Phaser and the Dirt wrapper from poly-effects-os,
# Console and Tube from poly-effects (private). ui_shot.sh puts their source at the pinned tag into $(EXT) (engines/external.py), without it
# it writes a stub with the same entry point and the two inserts show as failed to load. Standalone, the rule below writes the stub
EXT       ?= $(OUT)/external
PLUG_EXT_C := ot_tape_plug ot_lofi_plug ot_bbd_plug ot_phaser_plug ff_console_plug ff_tube_plug
EXCLUDE   := $(shell cat $(HERE)/exclude.txt 2>/dev/null | grep -v '^\#')

OBJS := $(patsubst %.cpp,$(OBJ)/core/%.o,$(filter-out $(EXCLUDE),$(CORE_CPP))) \
        $(patsubst %.c,$(OBJ)/core/%.o,$(CORE_C)) \
        $(patsubst %.cpp,$(OBJ)/so/%.o,$(filter-out $(EXCLUDE),$(SO_CPP))) \
        $(patsubst %,$(OBJ)/gen/%.o,$(OVERLAY_SRCS)) \
        $(patsubst %,$(OBJ)/plugfx/%.o,$(PLUG_EXT_C)) \
        $(patsubst $(HERE)/%.cpp,$(OBJ)/host/%.o,$(HOST_CPP)) \
        $(CONSOLE_OBJS)

INC := -I$(HERE) -I$(GEN)/inc -I$(GEN)/src -I$(GEN)/libs/Audio -I$(GEN)/libs/Audio/utility -I$(GEN)/libs/Audio/Polyverb -I$(GEN)/libs -I$(CORE)/include -I$(CORE)/config -I$(CORE)/source -I$(CORE)/synth -I$(CORE)/libs \
  -I$(CORE)/libs/ElapsedMillis -I$(CORE)/libs/SerialFlash -I$(CORE)/libs/SPI  -I$(CORE)/libs/FT812 \
  -I$(CORE)/libs/fonts -I$(CORE)/libs/NewDisplay -I$(CORE)/interface -I$(CORE)/effector -I$(CORE)/FileManager -I$(CORE)/Exporter \
  -I$(CORE)/AudioEngine -I$(CORE)/AudioEngine/ConstantValues -I$(CORE)/AudioEngine/MasterEnhancer \
  -I$(ROOT)/util/include -I$(ROOT)/util/3rdparty -I$(ROOT)/synth/include -I$(SO) -I$(SO)/ot -I$(SO)/plug \
  -I$(ROOT)/platforms/mk66/3rdparty/Buttons $(DEVTOOLS_INC) $(DEVTOOLS_HOST_INC)
DEFS := -DFW_TRIM -DAUDIO_PERFORMANCE_INFO_ENABLED -DMEMORY_USAGE_INFO_ENABLED -D__NEWLIB__ -DKINETISK -D__MK66FX1M0__ -DF_CPU=180000000 \
  -DUSB_MIDI -DTEENSYDUINO=142 -DARDUINO=10805 -DLAYOUT_US_ENGLISH -DCPU_MK66FX1M0VMD18 $(FW_VERSION_DEFINES) \
  -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN $(SYNTH_ENGINE_DEFINES) -DFW_REVERB_SRAM=1 -DFEATURE_SAT_SOFTKNEE -DFEATURE_PLUG_HOST \
  $(if $(DEVTOOLS_HOST_SRCS),-DENABLE_DEV_CONSOLE) -DUI_SHOT_HOST -DTINY_OPTIONAL_x64 -DAUDIO_CORE_RAM= -DAUDIO_CORE_FLASH= \
  "-D_GLIBCXX_THROW_OR_ABORT(x)=__builtin_abort()" "-Dstatic_assert(...)="
WARN := -w
OPT ?= -O1
SANFLAGS := $(if $(SAN),-fsanitize=$(SAN) -fno-omit-frame-pointer -fno-sanitize=vptr -fsanitize-recover=address)
LDFLAGS += $(SANFLAGS)
INSTR := $(if $(STACKMODEL),-finstrument-functions)
CXXFLAGS := $(SANFLAGS) -std=gnu++20 $(OPT) -g -fno-rtti -fno-exceptions -include cstdint -include assert.h -fno-strict-aliasing -ferror-limit=20 $(WARN) $(INC) $(DEFS)
CFLAGS   := $(SANFLAGS) $(OPT) -g -fno-strict-aliasing $(WARN) $(INC) $(DEFS)

all: $(OUT)/ui_shot

$(OBJ)/core/libs/NewDisplay/%.o: CXXFLAGS += $(if $(MCU_NULL),-include $(HERE)/host_mcunull.h)
$(OBJ)/gen/textBox.cpp.o: CXXFLAGS += $(if $(MCU_NULL),-include $(HERE)/host_mcunull.h)   # overlay copy of a NewDisplay control

$(OUT)/ui_shot: $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS) -lz
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
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/core/libs/Audio/%.o: $(GEN)/libs/Audio/%.c $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/core/%.o: $(CORE)/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INSTR) -MMD -MP -c $< -o $@
$(OBJ)/core/%.o: $(CORE)/%.c $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/so/%.o: $(SO)/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INSTR) -MMD -MP -c $< -o $@
$(OBJ)/gen/%.o: $(GEN)/src/% $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(CORE)/interface/sampleRecorder -MMD -MP -c $< -o $@
# the master plugins' own sources, natively, with the entry point renamed per file (host_twins.cpp)
$(OBJ)/plugfx/%.o: $(EXT)/%.c $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Dplug_entry=plug_entry_$* -MMD -MP -c $< -o $@
# kept after the build, so an unchanged stub does not rebuild its object
.SECONDARY: $(patsubst %,$(EXT)/%.c,$(PLUG_EXT_C))
$(EXT)/%.c:
	@mkdir -p $(dir $@)
	printf '/* stub: the module source of %s was not available */\nconst void *plug_entry(const void *host, unsigned abi) { (void)host; (void)abi; return 0; }\n' $* >$@
# CONSOLE=1 (poly-devtools): the console sources, and the firmware's own key queue with host stand-ins for the Teensy core headers
$(OBJ)/devtools/core/%.o: $(DEVTOOLS_CORE_DIR)/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/devtools/host/%.o: $(DEVTOOLS_HOST_DIR)/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/plat/Keys.o: $(ROOT)/platforms/mk66/3rdparty/Buttons/Keys.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(DEVTOOLS_HOST_STUBS) -I$(ROOT)/platforms/mk66/3rdparty/I2C -MMD -MP -c $< -o $@
$(OBJ)/host/host_heapmodel.o: CXXFLAGS += -I$(ROOT)/platforms/mk66/3rdparty/teensy_core
$(OBJ)/host/%.o: $(HERE)/%.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(shell find $(OBJ) -name '*.d' 2>/dev/null)
