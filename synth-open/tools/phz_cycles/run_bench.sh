#!/bin/sh
# PHZ cost on a Cortex-M4F without hardware: builds bench_arm.cpp + PHZ with the firmware's compiler and flags, runs it
# in a Unicorn (QEMU) emulator and turns the executed instructions into an estimated cycle count per 128 frame block.
#   synth-open/tools/phz_cycles/run_bench.sh [OPT] [ENGINE]   OPT: fw (default: the firmware's flags per engine, -O2, FM4 and
#   B-Type -O3, PHZ and PLK -O2 -ffp-contract=off), or one flag set for the engine, e.g. -Os / -O2
#   ENGINE: PHZ (default, phz_cycles scenarios, one voice) or any engine of synth-open/engines.mk (bench_engine.cpp: 4 voices as the
#   shim renders them, BENCH_VOICES=n for another count; scenarios default patch, all parameters max / mid / min, CHIP per model),
#   PHZG is PHZ through that generic bench,
#   engine and voice sources with OPT, libm and libc from the toolchain's newlib-nano as in the firmware link
# Needs the pinned toolchain in agt/10.3 (or TC=/path/to/arm-none-eabi, the 12.3 release compiler) and a python with unicorn + capstone:
#   python3 -m venv /tmp/phzvenv && /tmp/phzvenv/bin/pip install unicorn capstone     (PYTHON=/tmp/phzvenv/bin/python)
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
OPT=${1:-fw}
PY=${PYTHON:-/tmp/phzvenv/bin/python}
TC=${TC:-$ROOT/agt/10.3/bin/arm-none-eabi}
OUT=${KEEP_DIR:-${TMPDIR:-/tmp}/phz_cycles_$$}
mkdir -p "$OUT"
ENGINE=${2:-PHZ}
if [ "$OPT" = fw ]; then
  case $ENGINE in FM4|BTYPE) OPT=-O3 ;; PHZ|PHZG|PLK) OPT="-O2 -ffp-contract=off" ;; *) OPT=-O2 ;; esac
fi
FL="-std=gnu++20 $OPT -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -mfp16-format=ieee -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -fno-rtti -fno-exceptions -fno-threadsafe-statics -DNDEBUG -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN -DSYNTH_ENGINE_$ENGINE $EXTRA_FLAGS"
INC="-I$ROOT/synth/include -I$ROOT/util/include -I$ROOT/util/3rdparty -I$ROOT/synth-open -I$ROOT/synth-open/ot -I$ROOT/core/synth -I$ROOT/core/config"
# ENGINE: any name of synth-open/engines.mk (sources and the EngineDef symbol come from engines.mk and engine_list.h),
# PHZ keeps its dedicated bench (phz_cycles scenarios, one voice), PHZG is PHZ through the generic bench (4 voices)
GENERIC=$ENGINE
[ "$ENGINE" = PHZG ] && GENERIC=PHZ
if [ "$ENGINE" = PHZ ]; then
  BENCH=tools/phz_cycles/bench_arm; SRCS="phz/phz engine_phz"
else
  eval "$(make -s -f /dev/stdin <<MK
SYNTH_ENGINES := $GENERIC
include $ROOT/synth-open/engines.mk
all:
	@echo "GSRCS='\$(SYNTH_ENGINE_SRCS_$GENERIC)'"
	@echo "GCORE='\$(SYNTH_ENGINE_CORE_SRCS_$GENERIC)'"
MK
)"
  [ -n "$GSRCS" ] || { echo "unknown ENGINE $ENGINE"; exit 1; }
  BDEF=$(grep "^SYNTH_ENGINE($GENERIC," "$ROOT/synth-open/engine_list.h" | cut -d, -f5 | tr -d ' ')
  BENCH=tools/phz_cycles/bench_engine; SRCS=""
  for s in $GSRCS; do SRCS="$SRCS ${s%.cpp}"; done
  for s in $GCORE; do SRCS="$SRCS ../core/${s%.cpp}"; done
  # the enum name tables (kWaveNames, ...) of FAT, VAP and WTFM live in engine_acd.cpp; its engine is not linked (gc-sections)
  [ "$GENERIC" = ACD ] || SRCS="$SRCS engine_acd"
  FL="$FL -DSYNTH_ENGINE_$GENERIC"
fi
for f in $BENCH $SRCS; do
  "$TC-g++" $FL ${BDEF:+-DBENCH_DEF=$BDEF} ${BENCH_VOICES:+-DBENCH_VOICES=$BENCH_VOICES} $INC -c "$ROOT/synth-open/$f.cpp" -o "$OUT/$(basename $f).o"
done
"$TC-g++" -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -specs=nano.specs -nostartfiles -Wl,--gc-sections -Wl,-u,bench_setup -Wl,-u,bench_run -Wl,-u,bench_peak \
  -Wl,-T,"$ROOT/synth-open/tools/phz_cycles/link.ld" "$OUT"/*.o -Wl,--start-group -lm -lc -lgcc -Wl,--end-group -o "$OUT/bench.elf"
ENGINE=$ENGINE "$PY" -I "$ROOT/synth-open/tools/phz_cycles/run_bench.py" "$OUT/bench.elf" "$TC"
[ -n "$KEEP_DIR" ] || rm -rf "$OUT"
