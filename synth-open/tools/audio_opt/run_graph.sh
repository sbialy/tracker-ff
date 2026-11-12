#!/bin/bash
# Audio graph equivalence and cost bench, see graph_bench.cpp. Needs the pinned toolchain in agt/10.3 and a python with unicorn + capstone:
#   python3 -m venv /tmp/phzvenv && /tmp/phzvenv/bin/pip install unicorn capstone      (PYTHON=/tmp/phzvenv/bin/python)
#
#   run_graph.sh build <src-root> <label> [extra compile flags]    -> $OUT/<label>.elf  (default OUT=/tmp/audio_opt)
#   run_graph.sh run <label> <scenario> <blocks>                   -> $OUT/<label>.sc<scenario>.bin  (logs) and their sha256
#   run_graph.sh cmp <label-a> <label-b> <scenario>                -> byte for byte comparison of two logs, per-log verdicts
#   run_graph.sh cycles <label> <scenario> [warm-up blocks]        -> emulated cycles per block (no flash or SDRAM wait states) per function,
#                                                                     SDRAM reads and writes per block
#   run_graph.sh git <commit> <label> [extra flags]                -> build <commit> (git archive snapshot, no checkout) as <label>
# <src-root> is any tree with core/ util/ synth/ (the working tree, or a git archive snapshot). Scenarios: 0 song, 1 tails, 2 idle,
# 3 three voices playing, 4 three voices playing with zero fx sends, 5 one burst then idle with the filters on, 6 loud bursts without sends and sidechain switching. -DIDLE_NULL makes the stand-in voice transmit nothing when idle.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT0=$(cd "$HERE/../../.." && pwd)
PY=${PYTHON:-/tmp/phzvenv/bin/python}
TC=${TOOLCHAIN:+${TOOLCHAIN%-}}      # TOOLCHAIN=.../bin/arm-none-eabi- (as for make) picks the compiler, else agt/10.3 of the tree
TC=${TC:-$ROOT0/agt/10.3/bin/arm-none-eabi}
OUT=${OUT:-/tmp/audio_opt}
mkdir -p "$OUT"
cmd=$1; shift
case "$cmd" in
build)
  R=$(cd "$1" && pwd); L=$2; shift 2
  EXTRA="$*"
  INC="-I$R/core/include -I$R/core/config -I$R/core/source -I$R/core/synth -I$R/core/libs -I$R/core/libs/Audio -I$R/core/libs/ElapsedMillis -I$R/core/libs/Audio/utility -I$R/core/interface -I$R/core/effector -I$R/core/FileManager -I$R/core/Exporter -I$R/core/AudioEngine -I$R/core/libs/Audio/Polyverb -I$R/core/AudioEngine/ConstantValues -I$R/core/AudioEngine/MasterEnhancer -I$R/util/include -I$R/util/3rdparty -I$R/synth/include -I$R/core/libs/NewDisplay -I$R/core/libs/FT812 -I$R/core/libs/fonts"
  FL="-std=gnu++20 -O2 -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -fno-rtti -fno-exceptions -fno-threadsafe-statics -DNDEBUG -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN -DFW_TRIM -D__MK66FX1M0__ -DKINETISK -include assert.h"
  D=$OUT/o_$L; rm -rf "$D"; mkdir -p "$D"
  sed -e '/#include "mtAudioEngine.h"/d' -e '/#include <mtAudioExport.h>/d' "$R/core/libs/Audio/AudioStream.cpp" > "$D/AudioStream_bench.cpp"
  A=$R/core/libs/Audio
  # fastmem: the firmware links the word-wise memcpy/memset of platforms/mk66/source/fastmem.S with TRIM=1, so does the bench
  for f in "$HERE/graph_bench.cpp" "$D/AudioStream_bench.cpp" $A/amplifier.cpp $A/analyze_rms.cpp $A/mixer.cpp $A/dspApplyGain.cpp $A/monoToStereo.cpp \
           $A/effect_bitDepth.cpp $A/filter_variable.cpp $A/effect_limiter.cpp $A/effect_shortDelay.cpp $A/effect_polyverb.cpp $A/record_queue.cpp \
           $A/Output.cpp $A/mux.cpp; do
    "$TC-g++" $FL $EXTRA $INC -c "$f" -o "$D/$(basename "$f" .cpp).o"
  done
  "$TC-gcc" -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -DFW_TRIM -c "$ROOT0/platforms/mk66/source/fastmem.S" -o "$D/fastmem.o"
  "$TC-g++" -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -specs=nano.specs -nostartfiles -Wl,--gc-sections -Wl,-u,bench_setup -Wl,-u,bench_run \
    -Wl,-T,"$HERE/link.ld" "$D"/*.o -Wl,--start-group -lm -lc -lgcc -Wl,--end-group -o "$OUT/$L.elf"
  echo "built $OUT/$L.elf"
  ;;
git)
  C=$1; L=$2; shift 2
  S=$OUT/src_$L; rm -rf "$S"; mkdir -p "$S"
  (cd "$ROOT0" && git archive "$C" core util synth) | tar -x -C "$S"
  "$0" build "$S" "$L" "$@"
  ;;
run)
  "$PY" -I "$HERE/run_graph.py" run "$OUT/$1.elf" "$TC" "$2" "$3" "$OUT/$1.sc$2.bin"
  ;;
cmp)
  "$PY" -I "$HERE/run_graph.py" cmp "$OUT/$1.sc$3.bin" "$OUT/$2.sc$3.bin"
  ;;
cycles)
  "$PY" -I "$HERE/run_graph.py" cycles "$OUT/$1.elf" "$TC" "$2" "${3:-40}"
  ;;
*) echo "usage: see the header of this file"; exit 2 ;;
esac
