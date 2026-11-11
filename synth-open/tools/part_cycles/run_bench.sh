#!/bin/bash
# Cost of one synth part inside the open shim (OpenSynth::renderAudioBuffer, real open_synth.cpp / open_cold.cpp / engine) on an emulated
# Cortex-M4F, per function and per source line with SDRAM accesses counted (device: about 16.9 cycles per SDRAM access). NOT the device.
#   synth-open/tools/part_cycles/run_bench.sh [ENGINE=VAP] [VOICES=1] [LIVE=1] [ROOT=<tree>]
# Needs TOOLCHAIN-like TC (default agt/12.3.rel1) and a python with unicorn + capstone (PYTHON=/tmp/phzvenv/bin/python).
# Env: OUT=<dir> keeps the build there, SCEN=0 (no voice allowed: the part is idle) or 1, EXTRA_FLAGS, TOPN lines shown.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${ROOT:-$(cd "$HERE/../../.." && pwd)}
ENGINE=${1:-VAP}; VOICES=${2:-1}; LIVE=${3:-1}
PY=${PYTHON:-/tmp/phzvenv/bin/python}
TC=${TC:-/Users/sbialy/prj/poly/agt/12.3.rel1/bin/arm-none-eabi}
OUT=${OUT:-${TMPDIR:-/tmp}/part_cycles_$$}
mkdir -p "$OUT"
eval "$(make -s -f /dev/stdin <<MK
SYNTH_ENGINES := $ENGINE
include $ROOT/synth-open/engines.mk
all:
	@echo "DEFS='\$(SYNTH_ENGINE_DEFINES)'"
	@echo "SRCS='\$(SYNTH_ENGINE_SRCS)'"
	@echo "CORE='\$(foreach e,\$(SYNTH_ENGINES_SEL),\$(SYNTH_ENGINE_CORE_SRCS_\$(e)))'"
MK
)"
SRCS_ABS=""
for s in $SRCS; do SRCS_ABS="$SRCS_ABS $ROOT/synth-open/$s"; done
for s in $CORE; do SRCS_ABS="$SRCS_ABS $ROOT/core/$s"; done
case " $SRCS " in *engine_acd.cpp*) ;; *) SRCS_ABS="$SRCS_ABS $ROOT/synth-open/engine_acd.cpp" ;; esac   # the enum name tables of FAT, VAP, WTFM live there
FL="-std=gnu++20 -O2 -g1 -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -mfp16-format=ieee -ffunction-sections -fdata-sections -fno-rtti -fno-exceptions -fno-threadsafe-statics -DNDEBUG -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN -DFW_TRIM -DOPEN_BENCH -DBENCH_VOICES=$VOICES -DBENCH_LIVE=$LIVE $DEFS $EXTRA_FLAGS"
INC="-I$ROOT/synth/include -I$ROOT/util/include -I$ROOT/util/3rdparty -I$ROOT/synth-open -I$ROOT/synth-open/ot -I$ROOT/core/synth -I$ROOT/core/config -I$ROOT/core/source"
N=0
for f in "$HERE/bench_part.cpp" $ROOT/synth-open/open_synth.cpp $ROOT/synth-open/open_cold.cpp $ROOT/synth-open/open_patchio.cpp $ROOT/synth-open/stub_fmt.cpp \
         $SRCS_ABS $ROOT/synth-open/ot/audio/governor.cpp $ROOT/synth-open/ot/audio/cpu_meter.cpp; do
  N=$((N+1))
  "$TC-g++" $FL $INC -c "$f" -o "$OUT/${N}_$(basename "$f" .cpp).o" &
done
wait
"$TC-gcc" -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -DFW_TRIM -c "$ROOT/platforms/mk66/source/fastmem.S" -o "$OUT/fastmem.o"   # the TRIM image links the word-wise memcpy and memset
"$TC-g++" -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -specs=nano.specs -nostartfiles -Wl,--gc-sections -Wl,-u,bench_setup -Wl,-u,bench_run \
  -Wl,-T,"$HERE/link.ld" "$OUT"/*.o -Wl,--start-group -lm -lc -lgcc -lstdc++ -Wl,--end-group -o "$OUT/bench.elf"
"$PY" -I "$HERE/run_bench.py" "$OUT/bench.elf" "$TC" ${SCEN:-1} ${TOPN:-40}
[ -n "$KEEP" ] || [ -n "$OUT_KEEP" ] || true
