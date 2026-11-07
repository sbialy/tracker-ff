# Standalone build of the synth engine .plg set. No firmware build is run or needed. Works with Apple make 3.81.
#
#   make TOOLCHAIN=/path/to/arm-none-eabi-                          the release set (FLAVOUR=autof) into build/engines-autof of the repository
#   make TOOLCHAIN=... FLAVOUR=fused OUT=/tmp/set PY=/tmp/phzvenv/bin/python
#   make check-sdk TOOLCHAIN=...                                      file lists and engine list still match the sources
#   make verify OUT=...                                               SHA256SUMS, SET.json, ABI of a finished set
#   make package OUT=...                                              ../tracker-ff-engines-<set>-<flavour>.zip, files under Plugins/Synth/
#
# TOOLCHAIN is the compiler prefix with the trailing dash, there is no default. PY is a Python with unicorn: it runs the render
# checks (without it they are skipped and SET.json says so). make does not track flags and does not need to: every build
# starts from a scratch copy of the listed files.
FLAVOUR ?= autof
PY      ?= python3
HERE    := $(dir $(lastword $(MAKEFILE_LIST)))
OUT     ?= $(abspath $(HERE)..)/build/engines-$(FLAVOUR)
HOST_ABI ?= $(HERE)../synth-open/plug/plug_abi.h

.PHONY: all check-sdk verify package clean
all:
	python3 "$(HERE)build_set.py" build --out "$(OUT)" --flavour "$(FLAVOUR)" --py "$(PY)"

check-sdk:
	python3 -I "$(HERE)closure.py" --check

verify:
	python3 "$(HERE)build_set.py" verify "$(OUT)" --host-abi "$(HOST_ABI)"

package: verify
	python3 "$(HERE)build_set.py" package "$(OUT)" --host-abi "$(HOST_ABI)"

clean:
	rm -rf "$(OUT)"
