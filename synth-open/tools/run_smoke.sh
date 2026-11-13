#!/bin/sh
# Builds and runs synth-open/tools/host_smoke_test.cpp (host, NOT firmware) for an engine set:
#   synth-open/tools/run_smoke.sh "ACD VAP WTFM CHIP"      (default: ACD FAT VAP WTFM)
#   TRIM=1 synth-open/tools/run_smoke.sh "..."             also builds with -DFW_TRIM (std::map registry, as make ... TRIM=1)
#   PLUGENG=1 synth-open/tools/run_smoke.sh "..."          adds a plugin engine stand-in (JU6's numbers) to the run time engine list
# Uses the same engine list as the firmware build (synth-open/engines.mk), so every built engine gets a note.
# Needs clang++ (macOS: x86_64 build via Rosetta, as the test always was) and make.
# SMOKE_EXTRA="-DOPEN_REFRESH_CHECK" adds compile flags (that one checks the refreshParams fast paths against the reference algorithm)
# SMOKE_ARCH=arm64 builds a native arm64 binary instead (no Rosetta), the default stays x86_64:
#   SMOKE_ARCH=arm64 synth-open/tools/run_smoke.sh "ACD VAP WTFM CHIP"
#   CHIP_PLUGIN_SRC=.../tracker-engines-os/src/synth-open/engine_chip.cpp synth-open/tools/run_smoke.sh "ACD FAT VAP WTFM PERC"
#                                                          also runs CHIP as a plugin engine: that engine file and the Polyend voice are linked
#                                                          natively and added to the engine list at stored 6 (CHIP is chip.plg, not a built-in engine)
# tiny/optional.h only knows __x86_64__ and __arm__, so the native build adds -DTINY_OPTIONAL_x64 (its sentinel 0xffff8000...-1 is also
# a non-canonical address on arm64 user space), no vendor file is touched.
# The smoke test never loads a .plg. The synth plugins have their own acceptance test, the firmware's real checkSynth in Unicorn
# (every engine plugin of the four module repositories must PASS, also run by engines/crosscheck.sh):
#   TOOLCHAIN=.../agt/10.3/bin/arm-none-eabi- $PY -I synth-open/tools/validate_synth.py DIR_WITH_PLGS
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ENGINES=${1:-"ACD FAT VAP WTFM"}
eval "$(make -s -f /dev/stdin <<MK
SYNTH_ENGINES := $ENGINES
include $ROOT/synth-open/engines.mk
all:
	@echo "SEL='\$(SYNTH_ENGINES_SEL)'"
	@echo "DEFS='\$(SYNTH_ENGINE_DEFINES)'"
	@echo "SRCS='\$(SYNTH_ENGINE_SRCS)'"
	@echo "CORE='\$(foreach e,\$(SYNTH_ENGINES_SEL),\$(SYNTH_ENGINE_CORE_SRCS_\$(e)))'"
MK
)"
[ -n "$SEL" ] || { echo "run_smoke.sh: no engine selected, see the engines.mk error above (only the built-in engines can be linked in)" >&2; exit 2; }
echo "engines: $SEL"
SRCS_ABS=""
for s in $SRCS; do SRCS_ABS="$SRCS_ABS $ROOT/synth-open/$s"; done
for s in $CORE; do SRCS_ABS="$SRCS_ABS $ROOT/core/$s"; done
if [ -n "${CHIP_PLUGIN_SRC:-}" ]; then SRCS_ABS="$SRCS_ABS $ROOT/synth-open/tools/chip_smoke_engine.cpp $ROOT/core/synth/engines/ChiptuneSynthVoice.cpp"; fi
OUT=${TMPDIR:-/tmp}/synth_smoke_$$
ARCH=${SMOKE_ARCH:-x86_64}
ARCH_DEFS=""
[ "$ARCH" = x86_64 ] || ARCH_DEFS="-DTINY_OPTIONAL_x64"
clang++ -arch "$ARCH" -std=c++20 -O1 "-D_GLIBCXX_THROW_OR_ABORT(x)=__builtin_abort()" \
  -I "$ROOT/synth/include" -I "$ROOT/util/include" -I "$ROOT/util/3rdparty" -I "$ROOT/synth-open" -I "$ROOT/synth-open/ot" -I "$ROOT/core/synth" -I "$ROOT/core/config" \
  -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN -DOPEN_HOST_TEST "-DSMOKE_FIXTURES=\"$ROOT/synth-open/tools/fixtures\"" ${TRIM:+-DFW_TRIM} ${PLUGENG:+-DSMOKE_PLUGIN_ENGINE} ${SMOKE_EXTRA:-} ${CHIP_PLUGIN_SRC:+-DSMOKE_CHIP_PLUGIN "-DCHIP_PLUGIN_SRC=\"$CHIP_PLUGIN_SRC\""} $ARCH_DEFS $DEFS \
  "$ROOT/synth-open/tools/host_smoke_test.cpp" "$ROOT/synth-open/open_synth.cpp" "$ROOT/synth-open/open_cold.cpp" \
  "$ROOT/synth-open/open_patchio.cpp" "$ROOT/synth-open/stub_fmt.cpp" $SRCS_ABS "$ROOT/synth-open/ot/audio/governor.cpp" "$ROOT/synth-open/ot/audio/cpu_meter.cpp" \
  -o "$OUT"
"$OUT"
rm -f "$OUT"
