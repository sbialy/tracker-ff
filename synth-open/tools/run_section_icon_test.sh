#!/bin/sh
# Builds and runs synth-open/tools/section_icon_test.cpp (host, NOT firmware) with every engine of engines.mk: every section
# page of every engine (built in or card plugin) must get an editor icon (synth-open/open_section_icon.h).
#   synth-open/tools/run_section_icon_test.sh
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
eval "$(make -s -f /dev/stdin <<MK
include $ROOT/synth-open/engines.mk
SYNTH_ENGINES := \$(SYNTH_ENGINES_ALL)
all:
	@echo "DEFS='\$(addprefix -DSYNTH_ENGINE_,\$(SYNTH_ENGINES_ALL))'"
	@echo "SRCS='\$(foreach e,\$(SYNTH_ENGINES_ALL),\$(SYNTH_ENGINE_SRCS_\$(e)))'"
	@echo "CORE='\$(foreach e,\$(SYNTH_ENGINES_ALL),\$(SYNTH_ENGINE_CORE_SRCS_\$(e)))'"
MK
)"
SRCS_ABS=""
for s in $SRCS; do SRCS_ABS="$SRCS_ABS $ROOT/synth-open/$s"; done
for s in $CORE; do SRCS_ABS="$SRCS_ABS $ROOT/core/$s"; done
OUT=${TMPDIR:-/tmp}/section_icon_test_$$
clang++ -arch x86_64 -std=c++20 -O1 "-D_GLIBCXX_THROW_OR_ABORT(x)=__builtin_abort()" \
  -I "$ROOT/synth/include" -I "$ROOT/util/include" -I "$ROOT/util/3rdparty" -I "$ROOT/synth-open" -I "$ROOT/synth-open/ot" -I "$ROOT/core/synth" -I "$ROOT/core/config" \
  -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN -DOPEN_HOST_TEST $DEFS \
  "$ROOT/synth-open/tools/section_icon_test.cpp" "$ROOT/synth-open/stub_fmt.cpp" \
  $SRCS_ABS \
  -o "$OUT"
"$OUT"
rc=$?
rm -f "$OUT"
exit $rc
