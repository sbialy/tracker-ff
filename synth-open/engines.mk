# Build-time engine picker for SYNTH_OPEN=1 builds. Included by the top level Makefile and by core/makefile.
#
#   make original SYNTH_OPEN=1 SYNTH_ENGINES="ACD VAP WTFM CHIP"
#
# One place lists the engines: the names, the sources of each engine and the default set. Everything else
# (registry entry, Synth::kSynthTypes entry, EngineDef, editor name and icon) comes from engine_list.h, which
# is compiled with one -DSYNTH_ENGINE_<NAME> per selected engine. Unlisted engines are neither compiled nor linked.
#
# Only the stock five can be built in. Every other engine is a runtime .plg plugin whose source lives in a module repository
# (tracker-engines-os for CHIP and the OpenTracker 3.x module engines JU6 FM4 DTYPE CSTYPE ATYPE TTYPE 8TYPE HANDTYPE BTYPE ARCTYPE,
# tracker-engines for PHZ PLK). This tree keeps their identity only (engine_list.h, engines/engines.json), engines/README.md.
# CHIP (Polyend's open Chiptune voice) was the sixth built-in engine until it became chip.plg: core/synth/engines/ChiptuneSynthVoice.cpp
# stays in this tree for the builds without SYNTH_OPEN, a SYNTH_OPEN image never compiles it.
# Adding an engine: one line in SYNTH_ENGINES_ALL, one SYNTH_ENGINE_SRCS_<NAME> line, one entry in engine_list.h
# (and its sources + EngineDef). Keep the canonical order: it is the order of the model list on screen.

SYNTH_ENGINES_ALL     := ACD FAT VAP WTFM PERC
SYNTH_ENGINES_DEFAULT := ACD FAT VAP WTFM
# STOCK is what the firmware image carries (SYNTH_ENGINES=stock).
SYNTH_ENGINES_STOCK   := ACD FAT VAP WTFM PERC
# The plugin engines of the module repositories: a name here stops the build with a pointer to engines/README.md
SYNTH_ENGINES_EXT     := CHIP JU6 FM4 DTYPE CSTYPE ATYPE TTYPE 8TYPE HANDTYPE BTYPE ARCTYPE PHZ PLK

# Sources are relative to synth-open/.
SYNTH_ENGINE_SRCS_ACD  := engine_acd.cpp ot/audio/engines/acd.cpp
SYNTH_ENGINE_SRCS_FAT  := engine_fat.cpp ot/audio/engines/fat.cpp
SYNTH_ENGINE_SRCS_VAP  := engine_vap.cpp ot/audio/engines/vap.cpp
SYNTH_ENGINE_SRCS_WTFM := engine_wtfm.cpp ot/audio/engines/wtfm.cpp
SYNTH_ENGINE_SRCS_PERC := engine_perc.cpp ot/audio/engines/perc.cpp
# Engine sources that live in core/ (relative to core/): core/makefile leaves them out of a SYNTH_OPEN build unless their engine is
# picked. Polyend's open Chiptune voice is the only one, and CHIP is a plugin now, so it is never picked.
SYNTH_ENGINE_CORE_SRCS_CHIP := synth/engines/ChiptuneSynthVoice.cpp
# Name parts of data sections that belong to an engine without being in its objects (the PERC list icons, only built in
# with PERC, see displayStructs.h). Only used to attribute flash in make synth-engines. PERC is the odd one: its
# icons are always built in without TRIM (the plain SYNTH_OPEN image keeps them), TRIM=1 builds them in only with PERC or T-Type (T-Type wears them)
SYNTH_ENGINE_SECTIONS_PERC := perc_mini_nomark perc_med_

SYNTH_ENGINES ?= $(SYNTH_ENGINES_DEFAULT)

# A single space, written so every GNU make reads it the same way (make 3.81 reads "+=" of nothing as a space, make 4.x as nothing)
_se_empty :=
_se_space := $(_se_empty) $(_se_empty)
_se_req := $(strip $(shell printf '%s' '$(SYNTH_ENGINES)' | tr 'a-z,' 'A-Z '))
# SYNTH_ENGINES=stock (alone or among names) stands for the six stock engines
_se_req := $(strip $(foreach w,$(_se_req),$(if $(filter STOCK,$(w)),$(SYNTH_ENGINES_STOCK),$(w))))
_se_ext := $(filter $(SYNTH_ENGINES_EXT),$(_se_req))
ifneq ($(_se_ext),)
$(error SYNTH_ENGINES has the plugin engine(s) $(_se_ext): they are runtime .plg plugins built in their module repository (tracker-engines-os, tracker-engines), see engines/README.md. Built-in names: $(SYNTH_ENGINES_ALL))
endif
_se_unknown := $(filter-out $(SYNTH_ENGINES_ALL),$(_se_req))
ifneq ($(_se_unknown),)
$(error Unknown engine name(s) in SYNTH_ENGINES: $(_se_unknown). Valid names: $(SYNTH_ENGINES_ALL))
endif

# Selected engines, in canonical order, without duplicates
SYNTH_ENGINES_SEL := $(filter $(_se_req),$(SYNTH_ENGINES_ALL))
ifeq ($(SYNTH_ENGINES_SEL),)
$(error SYNTH_ENGINES selects no engine. Valid names: $(SYNTH_ENGINES_ALL))
endif

# The default set keeps the plain build dir (and so the same objects as before the picker existed). Any other
# set gets its own dir, because the engine list changes -D flags that make's dependency files do not see.
ifeq ($(SYNTH_ENGINES_SEL),$(SYNTH_ENGINES_DEFAULT))
SYNTH_ENGINES_TAG :=
else
SYNTH_ENGINES_TAG := -E$(subst $(_se_space),_,$(SYNTH_ENGINES_SEL))
endif

SYNTH_ENGINE_DEFINES := $(addprefix -DSYNTH_ENGINE_,$(SYNTH_ENGINES_SEL))
SYNTH_ENGINE_SRCS    := $(foreach e,$(SYNTH_ENGINES_SEL),$(SYNTH_ENGINE_SRCS_$(e)))
