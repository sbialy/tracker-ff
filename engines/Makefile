# The plugin modules of tracker-ff come from four module repositories (engines/README.md). Nothing is compiled here.
#
#   make -C engines fetch [MODULES=DIR] [FROM_LOCAL=1] [VIS=public]    the pinned files of external.lock.json into one directory
#   make -C engines check [MODULES=DIR] [VIS=public]                    external.py check + check_modules.py verify-external
#   make -C engines crosscheck TOOLCHAIN=... PY=... [MODULES=DIR]       the same plus the firmware's checkSynth and the logo reader
#   make -C engines list                                                the pinned sets
#
# MODULES defaults to build/modules of the repository. VIS public skips the private sets (no token, no local checkout needed).
# WITH=formflux switches the optional group on (FORM and FLUX of poly-engines and their presets), off by default: fetch, check and
# crosscheck then leave those rows and files out (engines/README.md, FORM and FLUX).
PY       ?= python3
HERE     := $(dir $(lastword $(MAKEFILE_LIST)))
MODULES  ?= $(abspath $(HERE)..)/build/modules
VIS      ?= all
HOST_ABI := $(HERE)../synth-open/plug/plug_abi.h
VFLAG    := $(if $(filter all,$(VIS)),,--visibility $(VIS))
WFLAG    := $(foreach g,$(WITH),--with $(g))

.PHONY: fetch check crosscheck list
fetch:
	python3 -I "$(HERE)external.py" fetch "$(MODULES)" $(if $(FROM_LOCAL),--from-local) $(VFLAG) $(WFLAG)

check:
	python3 -I "$(HERE)external.py" check "$(MODULES)" $(VFLAG) $(WFLAG)
	python3 -I "$(HERE)check_modules.py" verify-external "$(MODULES)" --host-abi "$(HOST_ABI)" --visibility $(VIS) $(WFLAG)

crosscheck:
	PY="$(PY)" bash "$(HERE)crosscheck.sh" "$(MODULES)" $(VIS) $(WFLAG)

list:
	python3 -I "$(HERE)external.py" list
