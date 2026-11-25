#!/bin/bash
# build.sh [OUT]: the Tracker+ simulator's closed-engine harness (harness.cpp + the firmware's core/synth/registry.cpp and the open
# ChiptuneSynthVoice.cpp, as the stock IMX core registers them) linked with the unmodified synth/lib/libPolySynth.a and libPolyDSP.a and
# the CMSIS math lib of platforms/imx. Cortex-M7 code (-mcpu=cortex-m7 -mfpu=fpv5-sp-d16, the Tracker+ CPU), run by run.py under Unicorn.
# PLUGVOICE_SRC: another PlugSynthVoice.cpp (the mutant of ui_shot/plus_ttype_vel_sim.sh), its header is found through core/synth/engines.
# TOOLCHAIN: the arm-none-eabi- prefix (default agt/12.3.rel1 of this tree or of ~/prj/poly). Host tooling, never part of a firmware image.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="${1:-$ROOT/build/plus-synth}"
TC="${TOOLCHAIN:-$ROOT/agt/12.3.rel1/bin/arm-none-eabi-}"
[ -x "${TC}g++" ] || TC="$HOME/prj/poly/agt/12.3.rel1/bin/arm-none-eabi-"
[ -x "${TC}g++" ] || { echo "plus_synth/build.sh: no arm-none-eabi-g++ (set TOOLCHAIN)" >&2; exit 2; }
mkdir -p "$OUT"
cd "$ROOT"
"${TC}g++" -std=gnu++20 -O2 -g -mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mfp16-format=ieee -mthumb -fno-rtti -fno-exceptions \
  -ffunction-sections -fdata-sections -DPLATFORM_IMX -DFEATURE_SYNTH -DNDEBUG \
  -Isynth/include -Iutil/include -Iutil/3rdparty -Icore/config -Icore/synth -Icore/include -Isynth-open -Isynth-open/ot -Isynth-open/plug -DPLUS_SYNTH_HARNESS -Icore/synth/engines \
  -specs=nano.specs -specs=nosys.specs -nostartfiles -T "$HERE/link.ld" -Wl,--gc-sections -Wl,-Map="$OUT/harness.map" -o "$OUT/harness.elf" \
  "$HERE/harness.cpp" core/synth/registry.cpp core/synth/engines/ChiptuneSynthVoice.cpp "${PLUGVOICE_SRC:-core/synth/engines/PlugSynthVoice.cpp}" \
  -Lsynth/lib -Lplatforms/imx/3rdparty/math \
  -Wl,--start-group -lPolySynth -lPolyDSP -larm_cortexM4lf_math -lstdc++_nano -lm -lc_nano -lnosys -lgcc -Wl,--end-group
"${TC}nm" "$OUT/harness.elf" > "$OUT/harness.nm"
echo "plus_synth: $OUT/harness.elf"
