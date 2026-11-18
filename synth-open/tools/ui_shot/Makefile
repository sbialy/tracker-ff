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
# compare modit.plg with (synth-open/ext_module.cpp, overlay). Both set: only the release's own modit.plg is accepted. Unset: the stand-in module of make_host_module.py
MODIT_ID ?=
MODIT_RUN ?=
# USB_MSC=1: the USB drive of the original (FEATURE_USB_MSC): Config > USB > Storage, Config > Features > USB drive, the card gate
# (host_drive.h) and the Exit that reads /Workspace back. UISHOT_DRIVE_FLAG=1 starts with the drive enumerated
USB_MSC ?=
# STRUM_RAMP=0: the plain Strum velocity (no ramp, synth-open/plug/strum_ramp.h), for A/B renders. Give it its own OUT, make does not track flags
STRUM_RAMP ?=
# PARTS=0: the build without the Parts view (core/source/parts.h), as release-1.4.0-int was: the compat runs of parts_sim.sh
PARTS   ?= 1
# BRIDGE=1 (UNICORN_DIR=<dir with include/ and lib/libunicorn.a>, the unpacked pinned wheel unicorn-2.1.4-cp37-abi3-macosx_11_0_arm64.whl, laid out by
# devtools tools/mirror/emu-release.sh): adds the Unicorn include dir (-DUISHOT_BRIDGE) and links libunicorn.a into ui_shot (the emulator bridge of the emu parity plan).
# Unicorn is GPL-2.0: internal tool only, never distribute a ui_shot built with BRIDGE=1. Without BRIDGE=1 nothing changes. Own OUT, make does not track flags
BRIDGE  ?=
UNICORN_DIR ?=
OUT     ?= $(ROOT)/build/ui-shot$(if $(filter 0,$(PARTS)),-noparts)$(if $(filter 1,$(CONSOLE)),-console)$(if $(SAN),-san)$(if $(STACKMODEL),-stack)$(if $(filter 1,$(EXTHOOKS)),-ext)$(if $(filter 1,$(USB_MSC)),-msc)$(if $(filter 1,$(BRIDGE)),-bridge)
OBJ     := $(OUT)/obj
GEN     := $(OUT)/gen
CORE    := $(ROOT)/core
SO      := $(ROOT)/synth-open
CXX     ?= clang++
CC      ?= clang

SYNTH_ENGINES ?= ACD FAT VAP WTFM PERC
include $(SO)/engines.mk
SYNTH_OPEN := 1
-include $(ROOT)/version.mk

# the overlay copies must exist before make resolves its pattern rules, so generate at parse time (idempotent, rewrites changed files only)
_gen := $(shell $(if $(filter 1,$(BRIDGE)),UISHOT_BRIDGE=1 )python3 -I $(HERE)/gen_overlay.py $(ROOT) $(GEN) $(EXT) 2>&1)
$(if $(_gen),$(error gen_overlay: $(_gen)))

# overlay sources (gen_shim.sh) replace their originals
OVERLAY_SRCS := ext_module.cpp ptfx_loader.cpp fSampleEdit.cpp plug_synth.cpp mtAudioEngine.cpp mtFileManagerOldVersionRecovery.cpp Tracker.cpp mtConfig.cpp fSampleRec.cpp effect_polyverb.cpp plug_host.cpp plug_insert.cpp open_cold.cpp open_synth.cpp textBox.cpp
CORE_DIRS := source libs interface effector FileManager Exporter AudioEngine synth
CORE_CPP  := $(foreach d,$(CORE_DIRS),$(shell cd $(CORE) && find $(d) -name '*.cpp'))
CORE_CPP  := $(filter-out $(foreach o,$(OVERLAY_SRCS),%/$(o)),$(CORE_CPP))
SO_OVERLAY := $(filter ext_module.cpp plug_host.cpp plug_insert.cpp plug_synth.cpp open_cold.cpp open_synth.cpp,$(OVERLAY_SRCS))
CORE_CPP  := $(filter-out $(foreach e,$(filter-out $(SYNTH_ENGINES_SEL),$(SYNTH_ENGINES_ALL) CHIP),$(SYNTH_ENGINE_CORE_SRCS_$(e))),$(CORE_CPP))
CORE_C    := $(shell cd $(CORE) && find libs -name '*.c')
SO_CPP    := $(filter-out $(SO_OVERLAY),$(filter-out engine_%.cpp,$(notdir $(wildcard $(SO)/*.cpp)))) $(addprefix ot/audio/,$(notdir $(wildcard $(SO)/ot/audio/*.cpp))) $(SYNTH_ENGINE_SRCS)
HOST_CPP  := $(wildcard $(HERE)/*.cpp)
# the emulator bridge (host_ucbridge.cpp, libunicorn) is BRIDGE=1 only: a plain build compiles exactly what it did before
HOST_CPP  := $(if $(filter 1,$(BRIDGE)),$(HOST_CPP),$(filter-out $(HERE)/host_ucbridge.cpp $(HERE)/host_nvm.cpp $(HERE)/host_synthcheck.cpp $(HERE)/host_plugrender.cpp,$(HOST_CPP)))
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

INC := -I$(HERE) -I$(GEN)/inc -I$(GEN)/src -I$(GEN)/libs/Audio -I$(GEN)/libs/Audio/utility -I$(GEN)/libs/Audio/Polyverb -I$(GEN)/libs -I$(CORE)/include -I$(CORE)/config -I$(CORE)/source -I$(CORE)/synth -I$(CORE)/libs \
  -I$(CORE)/libs/ElapsedMillis -I$(CORE)/libs/SerialFlash -I$(CORE)/libs/SPI  -I$(CORE)/libs/FT812 \
  -I$(CORE)/libs/fonts -I$(CORE)/libs/NewDisplay -I$(CORE)/interface -I$(CORE)/effector -I$(CORE)/FileManager -I$(CORE)/Exporter \
  -I$(CORE)/AudioEngine -I$(CORE)/AudioEngine/ConstantValues -I$(CORE)/AudioEngine/MasterEnhancer \
  -I$(ROOT)/util/include -I$(ROOT)/util/3rdparty -I$(ROOT)/synth/include -I$(SO) -I$(SO)/ot -I$(SO)/plug \
  -I$(ROOT)/platforms/mk66/3rdparty/Buttons $(DEVTOOLS_INC) $(DEVTOOLS_HOST_INC)
DEFS := -DFW_TRIM -DAUDIO_PERFORMANCE_INFO_ENABLED -DMEMORY_USAGE_INFO_ENABLED -D__NEWLIB__ -DKINETISK -D__MK66FX1M0__ -DF_CPU=180000000 \
  -DUSB_MIDI -DTEENSYDUINO=142 -DARDUINO=10805 -DLAYOUT_US_ENGLISH -DCPU_MK66FX1M0VMD18 $(FW_VERSION_DEFINES) \
  -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN $(SYNTH_ENGINE_DEFINES) -DFW_REVERB_SRAM=1 -DFEATURE_SAT_SOFTKNEE -DFEATURE_PLUG_HOST -DFEATURE_SEND_DUCK $(if $(filter 0,$(PARTS)),,-DFEATURE_PARTS) \
  $(if $(DEVTOOLS_HOST_SRCS),-DENABLE_DEV_CONSOLE) $(if $(filter 1,$(EXTHOOKS)),-DFEATURE_EXT_HOOKS -DFEATURE_MODIT_MODULE $(if $(MODIT_ID),-DUISHOT_MODIT_ID=$(MODIT_ID)u) $(if $(MODIT_RUN),-DUISHOT_MODIT_RUN=$(MODIT_RUN)u)) $(if $(filter 1,$(USB_MSC)),-DFEATURE_USB_MSC) -DUI_SHOT_HOST -DTINY_OPTIONAL_x64 -DAUDIO_CORE_RAM= -DAUDIO_CORE_FLASH= \
  $(if $(STRUM_RAMP),-DSTRUM_RAMP=$(STRUM_RAMP)) \
  "-D_GLIBCXX_THROW_OR_ABORT(x)=__builtin_abort()" "-Dstatic_assert(...)="
ifeq ($(BRIDGE),1)
$(if $(UNICORN_DIR),,$(error BRIDGE=1 needs UNICORN_DIR=<dir with include/unicorn/unicorn.h and lib/libunicorn.a>))
$(if $(wildcard $(UNICORN_DIR)/include/unicorn/unicorn.h),,$(error BRIDGE=1: $(UNICORN_DIR)/include/unicorn/unicorn.h not found))
$(if $(wildcard $(UNICORN_DIR)/lib/libunicorn.a),,$(error BRIDGE=1: $(UNICORN_DIR)/lib/libunicorn.a not found))
INC  += -I$(UNICORN_DIR)/include
# UISHOT_AXF at run time names the release axf the bridge maps (its image, its symbols), else this tree's make original-release output
BRIDGE_AXF ?= $(ROOT)/platforms/mk66/build-trim/PolyendTracker.axf
DEFS += -DUISHOT_BRIDGE -DUISHOT_DEFAULT_AXF='"$(BRIDGE_AXF)"'
# the Sample Editor effects are the card's /Effects/se_*.ptfx files run as bytes (the release recipe, T6). A plain build keeps them built in as the stock image does
DEFS += -DFW_SE_PTFX
LDFLAGS += $(UNICORN_DIR)/lib/libunicorn.a
endif
WARN := -w
OPT ?= -O1
SANFLAGS := $(if $(SAN),-fsanitize=$(SAN) -fno-omit-frame-pointer -fno-sanitize=vptr -fsanitize-recover=address)
LDFLAGS += $(SANFLAGS)
INSTR := $(if $(STACKMODEL),-finstrument-functions)
CXXFLAGS := $(SANFLAGS) -std=gnu++20 $(OPT) -g -fno-rtti -fno-exceptions -include cstdint -include assert.h -fno-strict-aliasing -ferror-limit=20 $(WARN) $(INC) $(DEFS)
CFLAGS   := $(SANFLAGS) $(OPT) -g -fno-strict-aliasing $(WARN) $(INC) $(DEFS)

all: $(OUT)/ui_shot

# EXTHOOKS=1: the module's sources get the module's names, the dispatcher is the module's own file
$(patsubst %.cpp,$(OBJ)/core/%.o,$(MODIT_SRCS)) $(OBJ)/modit/modit_dispatch.o: CXXFLAGS += $(if $(filter 1,$(EXTHOOKS)),-include $(SO)/modit/modit_names.h)
$(OBJ)/modit/modit_dispatch.o: $(SO)/modit/modit_dispatch.cpp $(GEN)/.stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

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
# CONSOLE=1 (console checkout): the console sources, and the firmware's own key queue with host stand-ins for the Teensy core headers
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

# the dependency files are only read, never made: without this empty rule make tries to remake each included .d through the built-in
# %: %.o chain (plugfx/x.d from x.d.o from a $(EXT)/x.d.c stub) and prints the stub's compile errors on every incremental build
%.d: ;
-include $(shell find $(OBJ) -name '*.d' 2>/dev/null)
