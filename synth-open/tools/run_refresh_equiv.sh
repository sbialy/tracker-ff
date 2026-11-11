#!/bin/sh
# Fuzz of the open shim's parameter refresh (host, arm64 native), see refresh_equiv_test.cpp.
#   synth-open/tools/run_refresh_equiv.sh [ENGINES="ACD FAT VAP WTFM PERC"] [STEPS=20000] [TREE=<other tree for the reference hash>]
# 1. the tree with -DOPEN_REFRESH_CHECK: every refresh is compared bit for bit with the reference algorithm (abort on a difference)
# AUTOINIT=zero (default) or pattern: the stack is initialised so the hashes are deterministic. Without it the output after a part's engine is swapped
#    (operation 28 of the test) depends on stack garbage, in the base tree d8abebc as well (found by this test, not fixed here)
# 2. the same test without the check on TREE (default: none) and on this tree: the output hashes must be equal
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ENGINES=${1:-"ACD FAT VAP WTFM PERC"}; STEPS=${2:-20000}; TREE=${TREE:-}
eval "$(make -s -f /dev/stdin <<MK
SYNTH_ENGINES := $ENGINES
include $ROOT/synth-open/engines.mk
all:
	@echo "DEFS='\$(SYNTH_ENGINE_DEFINES)'"
	@echo "SRCS='\$(SYNTH_ENGINE_SRCS)'"
	@echo "CORE='\$(foreach e,\$(SYNTH_ENGINES_SEL),\$(SYNTH_ENGINE_CORE_SRCS_\$(e)))'"
MK
)"
OUT=${TMPDIR:-/tmp}/refresh_equiv_$$; mkdir -p "$OUT"
build() {   # tree flags out
  T=$1; SRCS_ABS=""
  for s in $SRCS; do SRCS_ABS="$SRCS_ABS $T/synth-open/$s"; done
  for s in $CORE; do SRCS_ABS="$SRCS_ABS $T/core/$s"; done
  clang++ -arch arm64 -std=c++20 -O1 "-D_GLIBCXX_THROW_OR_ABORT(x)=__builtin_abort()" \
    -I "$T/synth/include" -I "$T/util/include" -I "$T/util/3rdparty" -I "$T/synth-open" -I "$T/synth-open/ot" -I "$T/core/synth" -I "$T/core/config" \
    -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN -DOPEN_HOST_TEST -DTINY_OPTIONAL_x64 "-ftrivial-auto-var-init=${AUTOINIT:-zero}" $DEFS $2 \
    "$ROOT/synth-open/tools/refresh_equiv_test.cpp" "$T/synth-open/open_synth.cpp" "$T/synth-open/open_cold.cpp" \
    "$T/synth-open/open_patchio.cpp" "$T/synth-open/stub_fmt.cpp" $SRCS_ABS "$T/synth-open/ot/audio/governor.cpp" "$T/synth-open/ot/audio/cpu_meter.cpp" -o "$3"
}
build "$ROOT" -DOPEN_REFRESH_CHECK "$OUT/check"
build "$ROOT" "" "$OUT/new"
"$OUT/check" "$STEPS"
H1=$("$OUT/new" "$STEPS")
echo "this tree, no check: $H1"
if [ -n "$TREE" ]; then
  build "$TREE" "" "$OUT/base"
  H0=$("$OUT/base" "$STEPS"); echo "$TREE: $H0"
  [ "$H0" = "$H1" ] && echo "BIT IDENTICAL to $TREE" || { echo "DIFFERENT from $TREE"; exit 1; }
fi
if [ -n "${KEEP:-}" ]; then echo "binaries in $OUT"; else rm -rf "$OUT"; fi
