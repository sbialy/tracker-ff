# Polyend Tracker Community Edition (CE) - Top-Level Monorepo Makefile

ROOT_DIR      := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ifneq ($(wildcard $(ROOT_DIR)/agt/10.3/bin/arm-none-eabi-gcc),)
  TOOLCHAIN   ?= $(ROOT_DIR)/agt/10.3/bin/arm-none-eabi-
else
  TOOLCHAIN   ?= arm-none-eabi-
endif

NPROC         ?= $(shell nproc 2>/dev/null || echo 4)
CORE_DIR      := $(ROOT_DIR)/core
MK66_DIR      := $(ROOT_DIR)/platforms/mk66
IMX_DIR       := $(ROOT_DIR)/platforms/imx
SYNTH_DIR     := $(ROOT_DIR)/synth
UTIL_DIR      := $(ROOT_DIR)/util
OUT_DIR       := $(ROOT_DIR)/build

include $(ROOT_DIR)/version.mk

ORIGINAL_POSTFIX ?= $(EDITION_NAME)_$(ORIGINAL_VER_MAJOR).$(ORIGINAL_VER_MINOR).$(ORIGINAL_VER_PATCH)

# make original SYNTH_OPEN=1: synth path on MK66 backed by the open shim in synth-open/
# (never links the closed libPolySynth.a / libPolyDSP.a). Separate core build dir and a "-s" .ptf name.
SYNTH_OPEN_DIR := $(ROOT_DIR)/synth-open
ifeq ($(SYNTH_OPEN),1)
# Engine picker: make original SYNTH_OPEN=1 SYNTH_ENGINES="ACD VAP WTFM CHIP" (names in synth-open/engines.mk, default
# ACD FAT VAP WTFM). Unknown names stop the build with the list of valid ones. make synth-engines prints the flash cost.
include $(SYNTH_OPEN_DIR)/engines.mk
# Engine DSP at -O2 (CPU: at -Os GCC keeps the per-sample helpers svfStep, limit and fastSine as calls, -O2 saves up to 28.7
# points of a block at 4 voices for +14.9 KB, ref cpu-optimisation-plan item 1), FM4 and B-Type at -O3 (SYNTH_OPT_FM4,
# SYNTH_OPT_BTYPE in core/makefile). Cold synth GUI files at -Os for the flash fit (see synth-open/README.md). Override with
# SYNTH_OPT_ENGINES=-Os SYNTH_OPT_GUI= (empty = -O2) and give each variant its own SYNTH_TAG=-x.
SYNTH_OPT_ENGINES ?= -O2
SYNTH_OPT_GUI ?= -Os
SYNTH_OPT_COLD ?= -Os
# FAT at -O2 costs about +120 B over -Os in the linked image. It stays at -O2 as long as text+data keeps
# clear of FLASH_IMG_MAX (0xFF000)
SYNTH_OPT_FAT ?= -O2
ORIGINAL_POSTFIX := $(ORIGINAL_POSTFIX)-s
# TRIM=1: no-feature-loss flash trims (synth-open/README.md, Flash trim): newlib-nano link, std::map registry, float
# math, PERC icons only with PERC, -Os on UI/cold dirs, core/source and the platform. Opt-in, own build dirs (core and platform),
# own .ptf tag FF (tracker-ff) so a trim image can never be mistaken for a plain SO one. Stock make original is untouched.
ifeq ($(TRIM),1)
TRIM_TAG := -trim
SYNTH_TAG_NAME := FF
else
SYNTH_TAG_NAME := SO
endif
# PTF_TAG=PLUGH: another TAG in the .ptf name for a dev image (the name checks below still apply: 1 to 4 chars unless RELEASE=1)
ifneq ($(PTF_TAG),)
SYNTH_TAG_NAME := $(PTF_TAG)
endif
# Device-facing .ptf name, two shapes, both start with PolyendTracker_ like stock (rules in synth-open/tools/check_ptf_name.sh):
#   build image (the default: rc, dev, test, console)  PolyendTracker_<TAG>_<sha7>.ptf
#       sha7 = BUILD_TAG, the string baked into the image (BUILD_ID) and reported by the console ping, so file name, ping and
#       commit match (PolyendTracker_FF_1234567.ptf, 29 chars). TAG is 1 to 4 chars (a test or development image tag).
#   public release (RELEASE=1)                         PolyendTracker_<TAG>_<A.B.C>.ptf
#       A.B.C is SYNTH_OPEN_VER without any dash suffix (PolyendTracker_FF_1.3.0.ptf, 27 chars).
# RELEASE=1 is the only switch. Nothing else tells a release apart: package_release.sh packages the .ptf it is given and accepts
# both shapes, build_module.sh (the console repository) globs PolyendTracker_*.ptf in the image dir. Give RELEASE=1 to every make call of
# a public release (make original and make package-release). The name never reaches the image, the bytes are the same either way.
# Config > Firmware on MK66 lists only names that start with PolyendTrac and are >= 15 chars
# (platforms/mk66/3rdparty/SD/SD.cpp:590), and the bootloader reads the name into char[32], so <= 31 chars:
# 15 + 4 (TAG) + 1 + 7 (sha7) + 4 (.ptf) = 31, hence at most 4 chars of TAG in a build image name. BUILD.txt keeps the full record.
SYNTH_OPEN_VER_NUM := $(firstword $(subst -, ,$(SYNTH_OPEN_VER)))
ifeq ($(RELEASE),1)
SYNTH_PTF_VER := $(SYNTH_OPEN_VER_NUM)
PTF_NAME_MODE := --release
else
SYNTH_PTF_VER := $(BUILD_TAG)
PTF_NAME_MODE := --build
endif
SYNTH_PTF := PolyendTracker_$(SYNTH_TAG_NAME)_$(SYNTH_PTF_VER).ptf
ORIGINAL_CORE_BUILD := $(CORE_DIR)/build-MK66-ORIGINAL-SYNTHOPEN$(SYNTH_TAG)$(SYNTH_ENGINES_TAG)$(TRIM_TAG)
# True flash image cap, text+data of the axf (platforms/mk66/makefile has the same default), and the link map used for
# the per-engine report in build/BUILD.txt
SYNTH_FLASH_IMG_MAX ?= 1044480
# Tail pad in bytes. make original starts at 0 and sizes it from the linked image (original recipe), a value here only seeds pass 1
FF_TAIL_PAD ?= 0
# End of the last image that was run on the unit (device-proven, 0xFC2AC = 1.3.0 round 5). make original warns past it, the release gate
# (synth-open/tools/release_gate.sh) fails past it unless ALLOW_UNPROVEN_END=1, BUILD.txt reports the free bytes against both ceilings
FLASH_IMG_PROVEN ?= 0xFC2AC
BUDGET_TOOL := $(SYNTH_OPEN_DIR)/tools/engine_budget.sh
# Archive members (object file names) of every engine, plus the display icon sections that belong to it: the link map
# is summed over these for the per-engine cost in BUILD.txt and make synth-engines
BUDGET_ENGINE_ARGS := $(foreach e,$(SYNTH_ENGINES_ALL),--engine "$(e)=$(notdir $(patsubst %.cpp,%.o,$(SYNTH_ENGINE_SRCS_$(e)) $(SYNTH_ENGINE_CORE_SRCS_$(e))))"$(if $(SYNTH_ENGINE_SECTIONS_$(e)), --sect "$(e)=$(SYNTH_ENGINE_SECTIONS_$(e))"))
ALL_ENGINES_CORE_BUILD := $(CORE_DIR)/build-MK66-ORIGINAL-SYNTHOPEN$(SYNTH_TAG)-E$(subst $(_se_space),_,$(SYNTH_ENGINES_ALL))$(TRIM_TAG)
else
ifeq ($(filter synth-engines,$(MAKECMDGOALS)),)
# make synth-engines TRIM=1 is fine: it re-enters with SYNTH_OPEN=1
ifeq ($(TRIM),1)
$(error TRIM=1 only applies to SYNTH_OPEN=1 builds)
endif
ifeq ($(origin SYNTH_ENGINES),command line)
$(error SYNTH_ENGINES only applies to SYNTH_OPEN=1 builds)
endif
endif
ORIGINAL_CORE_BUILD := $(CORE_DIR)/build-MK66-ORIGINAL
endif
# Platform (MK66) build dir. TRIM=1 compiles the platform at -Os with other defines, so it never shares objects with plain builds
MK66_BUILD       := $(MK66_DIR)/build$(TRIM_TAG)
PLUS_POSTFIX     ?= $(EDITION_NAME)_$(PLUS_VER_MAJOR).$(PLUS_VER_MINOR).$(PLUS_VER_PATCH)
MINI_POSTFIX     ?= $(EDITION_NAME)_$(MINI_VER_MAJOR).$(MINI_VER_MINOR).$(MINI_VER_PATCH)

.DEFAULT_GOAL := all

.PHONY: all original plus mini ptfx ptfx-check clean version bump-patch bump-minor synth-engines package-release lint-original release-gate
.PHONY: original-relink original-release stack-ratchet stack-ratchet-update stack-ratchet-release stack-ratchet-update-release stack-margin stack-margin-release lint-delta lint-delta-release

# Lint only, the original (MK66) target: -Wconversion scoped to the file manager and plugin host, format and bounds checks with builtins on.
# Prints warnings, builds nothing, never fails. Same variables as `make original` (SYNTH_OPEN=1 PLUG_HOST=1 adds the plugin host files).
lint-original:
	$(MAKE) -C "$(CORE_DIR)" lint \
		TOOLCHAIN="$(TOOLCHAIN)" PLATFORM=MK66 PRODUCT=ORIGINAL \
		POLYUTIL_DIR="$(UTIL_DIR)" POLYSYNTH_DIR="$(SYNTH_DIR)" \
		SYNTH_OPEN="$(SYNTH_OPEN)" SYNTH_OPEN_DIR="$(SYNTH_OPEN_DIR)" $(if $(filter 1,$(TRIM)),TRIM=1) \
		$(if $(SYNTH_ENGINES_SEL),SYNTH_ENGINES="$(SYNTH_ENGINES_SEL)") SYNTH_TAG="$(SYNTH_TAG)"

# Quality gates (synth-open/tools/stack_ratchet.py, lint_delta.py). They read the build or the lint output and change nothing in the image.
# RELEASE_ARGS is the one release recipe (synth-open/README.md, release image recipe): the five built in engines (CHIP is a plugin), the plugin host,
# USB_AUDIO=1, USB_MSC=1 (the USB drive, Config > USB > Storage, Config > Features > USB drive, off until the user turns it on) and -DFEATURE_EXT_HOOKS (Features page, USB audio, Restart mode, Developer mode: the flags every device round of 1.3.0 used), the public
# name PolyendTracker_FF_<A.B.C>.ptf. make original-release, the stack ratchet, the lint delta and the README build the same flags, the stack
# baseline is the frame list of this flavour. -DFEATURE_NO_SKIPBACK leaves Skip-back (skipback.h) out of the 1.4.0 release, the code stays for 1.5. SYNTH_TAG=-rel only names the object dirs (it is not a compiler flag): the flags are new, so the dirs are too.
# Pass TOOLCHAIN=... as for make original. make does not track flags: wipe core/build-* and platforms/mk66/build-trim between compilers.
RELEASE_ARGS := SYNTH_OPEN=1 TRIM=1 PLUG_HOST=1 RELEASE=1 SYNTH_TAG=-rel SYNTH_ENGINES="ACD FAT VAP WTFM PERC" \
	SYNTH_OPT_ENGINES="-O2 -ffp-contract=off" SYNTH_OPT_FAT="-O2 -ffp-contract=off" \
	SYNTH_OPT_FM4="-O3 -ffp-contract=off" SYNTH_OPT_BTYPE="-O3 -ffp-contract=off" \
	USB_AUDIO=1 USB_MSC=1 CUSTOM_DEFINES="-DFEATURE_EXT_HOOKS -DFEATURE_NO_SKIPBACK"
STACK_GCC_MAJOR = $(shell $(TOOLCHAIN)gcc -dumpversion 2>/dev/null | cut -d. -f1)
STACK_BASELINE ?= $(SYNTH_OPEN_DIR)/tools/stack_baseline_gcc$(STACK_GCC_MAJOR).txt
STACK_LOG ?=
STACK_RATCHET = python3 -I "$(SYNTH_OPEN_DIR)/tools/stack_ratchet.py" --root "$(ROOT_DIR)" --baseline "$(STACK_BASELINE)" \
	--su-dir "$(ORIGINAL_CORE_BUILD)" --su-dir "$(MK66_BUILD)" $(if $(STACK_LOG),--log "$(STACK_LOG)")
LINT_BASE ?= origin/main

# MOD import, IT import and IT export (core/FileManager/fileManagerImportMod.cpp, fileManagerImportIt.cpp, fileManagerExportToMod.cpp) are not in the
# release image: they are /Plugins/Ext/modit.plg, linked against the exact image (synth-open/modit/, core/makefile MODIT_MODULE). On by default for the
# release recipe (SYNTH_OPEN=1 PLUG_HOST=1 and -DFEATURE_EXT_HOOKS in CUSTOM_DEFINES), MODIT_MODULE=0 builds the three files into the image instead.
# make original runs modit-module after the image is final, build/modit.plg is the file the package carries (package_release.sh --modit).
MODIT_MODULE ?= $(if $(and $(filter 1,$(SYNTH_OPEN)),$(filter 1,$(PLUG_HOST)),$(filter -DFEATURE_EXT_HOOKS,$(CUSTOM_DEFINES))),1,0)
.PHONY: modit-module
modit-module:
	@test -f "$(MK66_BUILD)/PolyendTracker_$(ORIGINAL_POSTFIX).ptf" || { echo "no image: make original-release first"; exit 1; }
	TOOLCHAIN="$(TOOLCHAIN)" PTF="$(MK66_BUILD)/PolyendTracker_$(ORIGINAL_POSTFIX).ptf" "$(SYNTH_OPEN_DIR)/modit/build_modit.sh" \
		"$(MK66_BUILD)" "$(ORIGINAL_CORE_BUILD)/obj/modit" "$(OUT_DIR)/modit-build"
	@python3 -I "$(SYNTH_OPEN_DIR)/plug/check_ext.py" "$(OUT_DIR)/modit-build/modit.plg" "$(OUT_DIR)/$(SYNTH_PTF)"
	@cp "$(OUT_DIR)/modit-build/modit.plg" "$(OUT_DIR)/modit.plg"
	@echo "-> Output: $(OUT_DIR)/modit.plg"

# make original-release: the public release image, build/PolyendTracker_FF_<A.B.C>.ptf (RELEASE=1 is in RELEASE_ARGS)
original-release:
	$(MAKE) original $(RELEASE_ARGS)

# Stack ratchet: the -fstack-usage .su files of the last build against the committed baseline. Fails on a new frame of 512 B or more, a
# new VLA function, a grown frame, and on the hard cap for the audio chain. STACK_LOG=build.log also checks the -Wvla variables.
# Run it after the same make original (same variables) or use stack-ratchet-release.
stack-ratchet:
	$(STACK_RATCHET)

# Lock a gain in, or take a reviewed new frame into the baseline (the commit must say why)
stack-ratchet-update:
	$(STACK_RATCHET) --update --tag "release image on GCC $(STACK_GCC_MAJOR)"

# Build the release flavour with the log kept, then the ratchet with the VLA check. The log is $(OUT_DIR)/stack-ratchet-build.log
stack-ratchet-release:
	@mkdir -p "$(OUT_DIR)"
	$(MAKE) original $(RELEASE_ARGS) > "$(OUT_DIR)/stack-ratchet-build.log" 2>&1 || { tail -n 60 "$(OUT_DIR)/stack-ratchet-build.log"; exit 1; }
	$(MAKE) --no-print-directory stack-ratchet $(RELEASE_ARGS) STACK_LOG="$(OUT_DIR)/stack-ratchet-build.log"

# The same for the release flavour, from the build and log of the last make stack-ratchet-release (it fails on the new frame, this takes it in)
stack-ratchet-update-release:
	$(MAKE) --no-print-directory stack-ratchet-update $(RELEASE_ARGS) STACK_LOG="$(OUT_DIR)/stack-ratchet-build.log"

# Static worst case stack gate (synth-open/tools/stack/stack_margin.py): the linked image of the last build, main loop + audio chain + every
# nested interrupt against the real stack room (STACK_MARGIN of mk20dx128.c + the stack in use at the last _sbrk of Profiler::init). FAILS when the
# weakest row of the release set is below STACK_FLOOR (512 B), or when the interrupt census, the dynamic frame bounds (stack_vla_bounds.txt) or the
# STACK_MARGIN check no longer match the image. Reads the axf and the .su files, changes nothing in the image. Writes $(OUT_DIR)/stack-margin.txt (the
# report), .json and the stamp package_release.sh checks (PASS + the sha256 of the .ptf it was run for).
# Run it after the same make original (same variables, so the same build dirs) or use stack-margin-release. STACK_PTF names the image for the stamp.
STACK_FLOOR ?= 512
STACK_PTF ?= $(OUT_DIR)/$(SYNTH_PTF)
STACK_MARGIN_TOOL = python3 -I "$(SYNTH_OPEN_DIR)/tools/stack/stack_margin.py" "$(MK66_BUILD)" --root "$(ROOT_DIR)" \
	--su "$(ORIGINAL_CORE_BUILD)" --su "$(MK66_BUILD)" --min-margin $(STACK_FLOOR) $(STACK_MARGIN_ARGS)
stack-margin:
	@test "$(SYNTH_OPEN)" = 1 || { echo "stack-margin gates the synth-open image: pass the variables of make original (SYNTH_OPEN=1 ...), or use make stack-margin-release"; exit 1; }
	@mkdir -p "$(OUT_DIR)"
	@test -f "$(MK66_BUILD)/PolyendTracker.axf" || { echo "$(MK66_BUILD)/PolyendTracker.axf is missing, run make original (same variables) first"; exit 1; }
	@TOOLCHAIN="$(TOOLCHAIN)" $(STACK_MARGIN_TOOL) --json "$(OUT_DIR)/stack-margin.json" --ptf "$(STACK_PTF)" --stamp "$(STACK_PTF).stack-margin" \
		> "$(OUT_DIR)/stack-margin.txt" 2>&1; rc=$$?; cat "$(OUT_DIR)/stack-margin.txt"; exit $$rc

# Build the release flavour (a no-op relink when stack-ratchet-release or original-release ran before), then the gate on it
stack-margin-release:
	@mkdir -p "$(OUT_DIR)"
	$(MAKE) original $(RELEASE_ARGS) > "$(OUT_DIR)/stack-margin-build.log" 2>&1 || { tail -n 60 "$(OUT_DIR)/stack-margin-build.log"; exit 1; }
	$(MAKE) --no-print-directory stack-margin $(RELEASE_ARGS)

# Lint delta: make lint-original, kept to the warnings on lines added since the merge-base with $(LINT_BASE) in the classes that matched the
# device crashes (array-bounds, null-dereference, *-overflow, and -Wconversion in the file manager and plugin host), plus cppcheck on the
# changed files when it is installed. Reviewed false positives: synth-open/tools/lint_delta_allow.txt. Same variables as lint-original.
lint-delta:
	@mkdir -p "$(OUT_DIR)"
	$(MAKE) --no-print-directory lint-original > "$(OUT_DIR)/lint-original.log" 2>&1
	python3 -I "$(SYNTH_OPEN_DIR)/tools/lint_delta.py" --root "$(ROOT_DIR)" --base "$(LINT_BASE)" \
		--allow "$(SYNTH_OPEN_DIR)/tools/lint_delta_allow.txt" --cppcheck "$(OUT_DIR)/lint-original.log"

lint-delta-release:
	$(MAKE) --no-print-directory lint-delta $(RELEASE_ARGS)

all: original plus mini

# The core build and the platform link of the original. Twice in one make original when the tail pad has to change (FF_TAIL_PAD)
# FW_LTO=1: the link time optimisation build (see core/makefile). Symbols a card module needs by name are kept with --undefined.
LTO_KEEP_ARGS = $(foreach s,$(shell grep -v "^\#" $(SYNTH_OPEN_DIR)/lto_keep.txt),-Xlinker --undefined=$(s))

define ORIGINAL_BUILD_STEPS
	$(MAKE) -C "$(CORE_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		PLATFORM=MK66 PRODUCT=ORIGINAL \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)" \
		SYNTH_OPEN="$(SYNTH_OPEN)" SYNTH_OPEN_DIR="$(SYNTH_OPEN_DIR)" $(if $(filter 1,$(TRIM)),TRIM=1) \
		$(if $(SYNTH_ENGINES_SEL),SYNTH_ENGINES="$(SYNTH_ENGINES_SEL)") \
		SYNTH_TAG="$(SYNTH_TAG)" FF_TAIL_PAD="$(FF_TAIL_PAD)" SYNTH_OPT_ENGINES="$(SYNTH_OPT_ENGINES)" \
		SYNTH_OPT_GUI="$(SYNTH_OPT_GUI)" SYNTH_OPT_SHIM="$(SYNTH_OPT_SHIM)" \
		SYNTH_OPT_COLD="$(SYNTH_OPT_COLD)" SYNTH_OPT_FAT="$(SYNTH_OPT_FAT)"
	$(MAKE) -C "$(MK66_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		TRACKER_DIR="$(CORE_DIR)" \
		CORE_BUILD_DIR="$(ORIGINAL_CORE_BUILD)" \
		PRODUCT=ORIGINAL \
		TARGET_POSTFIX="$(ORIGINAL_POSTFIX)" \
		BUILD_ID_STR="$(BUILD_ID_STR)" \
		$(if $(filter 1,$(TRIM)),TRIM=1 BUILD_DIR="build$(TRIM_TAG)") \
		$(if $(filter 1,$(SYNTH_OPEN)),SDRAM_FILE_POOLS=1) \
		$(if $(filter 1,$(SYNTH_OPEN)),LINK_EXTRA="$(if $(filter 1 ui,$(FW_LTO)),-flto=10 $(LTO_KEEP_ARGS) )-Xlinker -Map=build$(TRIM_TAG)/PolyendTracker.map")
endef

original:
	@echo "=================================================================="
	@echo " Building Original Tracker (MK66) - $(EDITION_NAME) $(ORIGINAL_VER_MAJOR).$(ORIGINAL_VER_MINOR).$(ORIGINAL_VER_PATCH) ($(BUILD_TAG))"
	@echo "=================================================================="
	@rm -f "$(MK66_BUILD)/obj/source/PlatformImpl.o"
	@# SDRAM_FILE_POOLS follows SYNTH_OPEN and the plain build shares these objects: rebuild them every time
	@rm -f "$(MK66_BUILD)/obj/source/FileSystemImpl.o" "$(MK66_BUILD)/obj/3rdparty/SD/SdFile.o"
ifeq ($(SYNTH_OPEN),1)
	@# name checked before the long build
	@"$(SYNTH_OPEN_DIR)/tools/check_ptf_name.sh" $(PTF_NAME_MODE) "$(SYNTH_PTF)"
	@rm -f "$(OUT_DIR)/$(SYNTH_PTF)" "$(OUT_DIR)/BUILD.txt" "$(OUT_DIR)/modit.plg"
endif
	$(ORIGINAL_BUILD_STEPS)
ifeq ($(SYNTH_OPEN),1)
	@# Tail pad: the last 4 KB sector of the image must not be short. Pass 1 links with the pad given (0), tail_pad.py reads the pad the image
	@# needs (0 to 2,064 B, synth-open/tools/tail_pad.py), a different pad relinks (only ext_module.o is rebuilt), the result is checked again
	@pad=$(FF_TAIL_PAD); ok=; for i in 1 2 3; do \
		need=$$(python3 -I "$(SYNTH_OPEN_DIR)/tools/tail_pad.py" --toolchain "$(TOOLCHAIN)" --have $$pad "$(MK66_BUILD)/PolyendTracker.axf") || exit 1; \
		if [ "$$need" = "$$pad" ]; then ok=1; break; fi; \
		echo "tail pad: $$pad B -> $$need B (last 4 KB sector rule), relinking"; \
		$(MAKE) --no-print-directory original-relink FF_TAIL_PAD=$$need || exit 1; pad=$$need; \
	done; [ -n "$$ok" ] || { echo "ERROR: the tail pad does not settle"; exit 1; }; echo "tail pad: $$pad B"
endif
	@mkdir -p "$(OUT_DIR)"
ifeq ($(SYNTH_OPEN),1)
	python3 "$(SYNTH_OPEN_DIR)/tools/check_ptf.py" --warn-proven $(FLASH_IMG_PROVEN) "$(MK66_BUILD)/PolyendTracker_$(ORIGINAL_POSTFIX).ptf"
	@# No static constructor may touch the SDRAM: it is set up in main(), after them (a hit never boots, f563f27)
	python3 "$(SYNTH_OPEN_DIR)/tools/check_static_init.py" --toolchain "$(TOOLCHAIN)" "$(MK66_BUILD)/PolyendTracker.axf"
	@cp "$(MK66_BUILD)/PolyendTracker_$(ORIGINAL_POSTFIX).ptf" "$(OUT_DIR)/$(SYNTH_PTF)"
	@echo "-> Output: $(OUT_DIR)/$(SYNTH_PTF)"
ifeq ($(MODIT_MODULE),1)
	@# MOD/IT is a card module of this exact image: built after the tail pad settled (the relink changes the image id), packed against the .ptf
	$(MAKE) --no-print-directory modit-module
endif
	@# Size guard of its own: the one in platforms/mk66/makefile only runs while the image is being linked.
	@# BUILD.txt next to the .ptf carries the engine list (the file name cannot: the bootloader limit is 31 chars)
	@TOOLCHAIN="$(TOOLCHAIN)" $(BUDGET_TOOL) report --axf "$(MK66_BUILD)/PolyendTracker.axf" \
		--map "$(MK66_BUILD)/PolyendTracker.map" --engines "$(SYNTH_ENGINES_SEL)" --max $(SYNTH_FLASH_IMG_MAX) --proven $(FLASH_IMG_PROVEN) \
		$(BUDGET_ENGINE_ARGS) --txt "$(OUT_DIR)/BUILD.txt" --ptf "$(SYNTH_PTF)" --version "$(SYNTH_OPEN_VER)" --commit "$(BUILD_TAG)" \
		|| { rm -f "$(OUT_DIR)/$(SYNTH_PTF)"; exit 1; }
	@echo "-> Build info: $(OUT_DIR)/BUILD.txt"
else
	@cp "$(MK66_BUILD)/PolyendTracker_$(ORIGINAL_POSTFIX).ptf" "$(OUT_DIR)/"
	@echo "-> Output: $(OUT_DIR)/PolyendTracker_$(ORIGINAL_POSTFIX).ptf"
endif

# Core build and link only (the tail pad pass of make original)
original-relink:
	$(ORIGINAL_BUILD_STEPS)

# make package-release SYNTH_OPEN=1 [TRIM=1]: folder + zip of the already built build/$(SYNTH_PTF) with LICENSE, NOTICE.md
# and every bundled third-party license text, in build/release/ (synth-open/tools/package_release.sh). Does not build.
package-release:
ifneq ($(SYNTH_OPEN),1)
	$(error package-release needs SYNTH_OPEN=1 (it packages build/PolyendTracker_<TAG>_<sha7>.ptf, or <A.B.C> with RELEASE=1))
endif
	@"$(SYNTH_OPEN_DIR)/tools/check_ptf_name.sh" $(PTF_NAME_MODE) "$(SYNTH_PTF)"
	@test -f "$(OUT_DIR)/$(SYNTH_PTF)" || { echo "build/$(SYNTH_PTF) is missing, run make original SYNTH_OPEN=1 first"; exit 1; }
	@"$(SYNTH_OPEN_DIR)/tools/package_release.sh" "$(OUT_DIR)/$(SYNTH_PTF)" "$(OUT_DIR)/release" --zip $(if $(filter 1,$(MODIT_MODULE)),--modit "$(OUT_DIR)/modit.plg")

# make release-gate PLG_DIR=<flat folder of the new .plg and .plgi> [OLD_REF=<ref>]: the firmware and engine mismatch simulator against the latest
# published release tag, fails on a RED check that is not in synth-open/tools/ui_shot/mismatch_known_findings.txt. package_release.sh runs it too.
release-gate:
	@PLG_DIR="$(PLG_DIR)" OLD_REF="$(OLD_REF)" $(if $(IMAGE),IMAGE="$(IMAGE)") "$(SYNTH_OPEN_DIR)/tools/release_gate.sh"

# make synth-engines [SYNTH_ENGINES="ACD VAP WTFM CHIP"]: flash cost of every engine and the budget left for the selection
synth-engines:
	@$(MAKE) --no-print-directory synth-engines-run SYNTH_OPEN=1

# Links ALL engines once, with the flash cap lifted for that link only (never flashed), and reads the cost of each engine
# from the link map: only what survives --gc-sections counts. The image is not copied to build/.
synth-engines-run:
	@echo "Linking all engines ($(SYNTH_ENGINES_ALL)) for the map, cap lifted for this measuring link only"
	@rm -f "$(MK66_BUILD)/obj/source/PlatformImpl.o"
	@$(MAKE) -C "$(CORE_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" PLATFORM=MK66 PRODUCT=ORIGINAL POLYUTIL_DIR="$(UTIL_DIR)" POLYSYNTH_DIR="$(SYNTH_DIR)" \
		SYNTH_OPEN=1 SYNTH_OPEN_DIR="$(SYNTH_OPEN_DIR)" SYNTH_ENGINES="$(SYNTH_ENGINES_ALL)" SYNTH_TAG="$(SYNTH_TAG)" $(if $(filter 1,$(TRIM)),TRIM=1) \
		SYNTH_OPT_ENGINES="$(SYNTH_OPT_ENGINES)" SYNTH_OPT_GUI="$(SYNTH_OPT_GUI)" SYNTH_OPT_SHIM="$(SYNTH_OPT_SHIM)" \
		SYNTH_OPT_COLD="$(SYNTH_OPT_COLD)" SYNTH_OPT_FAT="$(SYNTH_OPT_FAT)" > /dev/null 2>&1 || { echo "core build failed (run make original SYNTH_OPEN=1 SYNTH_ENGINES=\"$(SYNTH_ENGINES_ALL)\" to see why)"; exit 1; }
	@$(MAKE) -C "$(MK66_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" TRACKER_DIR="$(CORE_DIR)" CORE_BUILD_DIR="$(ALL_ENGINES_CORE_BUILD)" PRODUCT=ORIGINAL \
		TARGET_POSTFIX="budget" BUILD_ID_STR="budget" FLASH_IMG_MAX=16777216 $(if $(filter 1,$(TRIM)),TRIM=1 BUILD_DIR="build$(TRIM_TAG)") \
		LINK_EXTRA="-Xlinker --defsym=__flash_img_max=0x1000000 -Xlinker -Map=build$(TRIM_TAG)/PolyendTracker_budget.map" > /dev/null 2>&1 \
		|| { echo "measuring link failed"; exit 1; }
	@TOOLCHAIN="$(TOOLCHAIN)" $(BUDGET_TOOL) table --axf "$(MK66_BUILD)/PolyendTracker.axf" \
		--map "$(MK66_BUILD)/PolyendTracker_budget.map" --pick "$(SYNTH_ENGINES_SEL)" \
		--max $(SYNTH_FLASH_IMG_MAX) $(BUDGET_ENGINE_ARGS)
	@rm -f "$(MK66_BUILD)"/PolyendTracker_budget.* "$(MK66_BUILD)"/PolyendTracker_*_budget.*

.PHONY: synth-engines-run

plus:
	@echo "=================================================================="
	@echo " Building Tracker+ (IMX) - $(EDITION_NAME) $(PLUS_VER_MAJOR).$(PLUS_VER_MINOR).$(PLUS_VER_PATCH) ($(BUILD_TAG))"
	@echo "=================================================================="
	@rm -f "$(IMX_DIR)/build-PLUS/obj/source/PlatformImpl.o"
	$(MAKE) -C "$(CORE_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		PLATFORM=IMX PRODUCT=PLUS \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)"
	$(MAKE) -C "$(IMX_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		TRACKER_DIR="$(CORE_DIR)" \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)" \
		PRODUCT=PLUS \
		TARGET_POSTFIX="$(PLUS_POSTFIX)" \
		BUILD_ID_STR="$(BUILD_ID_STR)"
	@mkdir -p "$(OUT_DIR)"
	@cp "$(IMX_DIR)/build-PLUS/TrackerPlus_$(PLUS_POSTFIX).ptf" "$(OUT_DIR)/"
	@echo "-> Output: $(OUT_DIR)/TrackerPlus_$(PLUS_POSTFIX).ptf"

mini:
	@echo "=================================================================="
	@echo " Building Tracker Mini (IMX) - $(EDITION_NAME) $(MINI_VER_MAJOR).$(MINI_VER_MINOR).$(MINI_VER_PATCH) ($(BUILD_TAG))"
	@echo "=================================================================="
	@rm -f "$(IMX_DIR)/build-MINI/obj/source/PlatformImpl.o"
	$(MAKE) -C "$(CORE_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		PLATFORM=IMX PRODUCT=MINI \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)"
	$(MAKE) -C "$(IMX_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		TRACKER_DIR="$(CORE_DIR)" \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)" \
		PRODUCT=MINI \
		TARGET_POSTFIX="$(MINI_POSTFIX)" \
		BUILD_ID_STR="$(BUILD_ID_STR)"
	@mkdir -p "$(OUT_DIR)"
	@cp "$(IMX_DIR)/build-MINI/TrackerMini_$(MINI_POSTFIX).ptf" "$(OUT_DIR)/"
	@echo "-> Output: $(OUT_DIR)/TrackerMini_$(MINI_POSTFIX).ptf"

ptfx:
	@mkdir -p "$(ROOT_DIR)/editor-fx-sdk/build/Effects"
	@for src in "$(ROOT_DIR)"/editor-fx-sdk/effects/*.c; do \
		base=$$(basename "$$src" .c); \
		python3 "$(ROOT_DIR)/editor-fx-sdk/ptfx_builder.py" "$$src" -o "$(ROOT_DIR)/editor-fx-sdk/build/Effects/$$base.ptfx" --toolchain "$(TOOLCHAIN)"; \
	done

# Builds the ot_*.c effects, then renders each through a host harness (clang, real libm)
# into build/ptfx-wav and checks the result. Host side only, nothing runs on the device.
ptfx-check:
	python3 -I "$(ROOT_DIR)/editor-fx-sdk/tools/ptfx_host_check.py"

clean:
	$(MAKE) -C "$(CORE_DIR)" clean PLATFORM=MK66 PRODUCT=ORIGINAL
	$(MAKE) -C "$(CORE_DIR)" clean PLATFORM=MK66 PRODUCT=ORIGINAL SYNTH_OPEN=1
	rm -rf $(CORE_DIR)/build-MK66-ORIGINAL-SYNTHOPEN-*
	$(MAKE) -C "$(CORE_DIR)" clean PLATFORM=IMX PRODUCT=PLUS
	$(MAKE) -C "$(CORE_DIR)" clean PLATFORM=IMX PRODUCT=MINI
	$(MAKE) -C "$(MK66_DIR)" clean PRODUCT=ORIGINAL
	rm -rf "$(MK66_DIR)/build-trim"
	$(MAKE) -C "$(IMX_DIR)" clean PRODUCT=PLUS
	$(MAKE) -C "$(IMX_DIR)" clean PRODUCT=MINI
	rm -rf "$(OUT_DIR)" "$(ROOT_DIR)/editor-fx-sdk/build"

version:
	@echo "Edition      : $(EDITION_NAME) (Build $(BUILD_TAG))"
	@echo "Original (MK66): $(EDITION_NAME) $(ORIGINAL_VER_MAJOR).$(ORIGINAL_VER_MINOR).$(ORIGINAL_VER_PATCH) -> PolyendTracker_$(ORIGINAL_POSTFIX).ptf"
	@echo "Tracker+ (IMX) : $(EDITION_NAME) $(PLUS_VER_MAJOR).$(PLUS_VER_MINOR).$(PLUS_VER_PATCH) -> TrackerPlus_$(PLUS_POSTFIX).ptf"
	@echo "Mini     (IMX) : $(EDITION_NAME) $(MINI_VER_MAJOR).$(MINI_VER_MINOR).$(MINI_VER_PATCH) -> TrackerMini_$(MINI_POSTFIX).ptf"

TARGET ?= all

bump-patch:
	@python3 -c 'import re, sys; t="$(TARGET)".upper(); p="version.mk"; s=open(p).read(); \
	targets = ["ORIGINAL", "PLUS", "MINI"] if t == "ALL" else [t]; \
	for k in targets: \
	    m = re.search(rf"{k}_VER_PATCH\s*:=\s*(\d+)", s); \
	    assert m, f"Unknown TARGET {t}"; \
	    v = int(m.group(1)) + 1; \
	    s = re.sub(rf"{k}_VER_PATCH\s*:=\s*\d+", f"{k}_VER_PATCH := {v}", s); \
	open(p, "w").write(s); print(f"Bumped patch version for {targets}")'

bump-minor:
	@python3 -c 'import re, sys; t="$(TARGET)".upper(); p="version.mk"; s=open(p).read(); \
	targets = ["ORIGINAL", "PLUS", "MINI"] if t == "ALL" else [t]; \
	for k in targets: \
	    m = re.search(rf"{k}_VER_MINOR\s*:=\s*(\d+)", s); \
	    assert m, f"Unknown TARGET {t}"; \
	    v = int(m.group(1)) + 1; \
	    s = re.sub(rf"{k}_VER_MINOR\s*:=\s*\d+", f"{k}_VER_MINOR := {v}", s); \
	    s = re.sub(rf"{k}_VER_PATCH\s*:=\s*\d+", f"{k}_VER_PATCH := 0", s); \
	open(p, "w").write(s); print(f"Bumped minor version for {targets} (patch reset to 0)")'
