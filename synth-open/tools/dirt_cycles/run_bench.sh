#!/bin/bash
# Cost of the live Dirt master insert (synth-open/ff_dirt.h) per 128 frame audio block on an emulated Cortex-M4F (NOT the device, no flash
# wait states), with the firmware's compiler and flags. Real AudioEffectDirt and AudioStream between a stand-in source and a sink,
# see bench_dirt.cpp. Scenarios: 0 off, 1 Soft, 2 Hard, 3 Fold, 4 Asym, 5 Crush, 6 no Dirt object (baseline), 7 Soft with a silent bus.
#   synth-open/tools/dirt_cycles/run_bench.sh [scenario ...]      default: all
# Needs the pinned toolchain in agt/10.3 and a python with unicorn + capstone:
#   python3 -m venv $VENV && $VENV/bin/pip install unicorn capstone    (PYTHON=$VENV/bin/python)
# The cycle counter is tools/idle_cycles/run_bench.py (one block is 522,240 cycles at 180 MHz and 44117.6 Hz).
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
HERE=$ROOT/synth-open/tools/dirt_cycles
PY=${PYTHON:-python3}
TC=$ROOT/agt/10.3/bin/arm-none-eabi
OUT=${TMPDIR:-/tmp}/dirt_cycles_$$
mkdir -p "$OUT/o"
R=$ROOT
INC="-I$R/core/include -I$R/core/config -I$R/core/source -I$R/core/synth -I$R/core/libs -I$R/core/libs/Audio -I$R/core/libs/Audio/utility -I$R/synth-open"
FL="-std=gnu++20 -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -fno-rtti -fno-exceptions -fno-threadsafe-statics -DNDEBUG -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN -DFEATURE_FF_DIRT -DFW_TRIM -D__MK66FX1M0__ -DKINETISK -include assert.h"
sed -e '/#include "mtAudioEngine.h"/d' -e '/#include <mtAudioExport.h>/d' "$R/core/libs/Audio/AudioStream.cpp" > "$OUT/AudioStream_bench.cpp"
"$TC-g++" $FL -O2 $INC -c "$OUT/AudioStream_bench.cpp" -o "$OUT/o/AudioStream.o"
"$TC-g++" $FL -O2 $INC -c "$HERE/bench_dirt.cpp" -o "$OUT/o/bench_dirt.o"
"$TC-g++" $FL -O2 -ffp-contract=off $INC -c "$R/synth-open/ff_dirt.cpp" -o "$OUT/o/ff_dirt.o"      # the firmware flags, core/makefile SYNTH_OPT_DIRT
"$TC-g++" $FL -Os $INC -c "$R/synth-open/ff_dirt_ctl.cpp" -o "$OUT/o/ff_dirt_ctl.o"
"$TC-g++" -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -specs=nano.specs -nostartfiles -Wl,--gc-sections -Wl,-u,bench_setup -Wl,-u,bench_run \
  -Wl,-T,"$HERE/link.ld" "$OUT"/o/*.o -Wl,--start-group -lm -lc -lgcc -Wl,--end-group -o "$OUT/bench.elf"
if [ "$1" = check ]; then   # the ARM code against the host render: dirt_cycles/run_bench.sh check
  clang++ -std=c++20 -O2 -ffp-contract=off -I "$R/synth-open" "$R/synth-open/tools/dirt_render.cpp" "$R/synth-open/ff_dirt.cpp" "$R/synth-open/ff_dirt_ctl.cpp" -o "$OUT/render"
  for p in 1 2 3 4 5; do
    "$PY" -I "$HERE/check_arm.py" "$OUT/bench.elf" "$TC" $p 7 "$OUT/in.bin" "$OUT/out.bin"
    "$OUT/render" --check "$OUT/in.bin" "$OUT/out.bin" $p 7
  done
  rm -rf "$OUT"; exit 0
fi
for sc in ${@:-6 0 1 2 3 4 5 7}; do SC=$sc "$PY" -I "$R/synth-open/tools/idle_cycles/run_bench.py" "$OUT/bench.elf" "$TC" | head -5; done
[ -n "$KEEP_DIR" ] || rm -rf "$OUT"
