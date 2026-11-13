#!/bin/bash
# The mono part path of PolySynth::update, old passes against core/libs/Audio/synth_part_clip.h, both compiled for the Cortex-M4 with the firmware
# flags (synth_part_arm.cpp) and run in the Unicorn emulator on random and edge blocks (the soft clip is a fused multiply-add, whether both
# loops contract it the same way is up to the compiler). Needs the toolchain and a python with unicorn (PYTHON=python3).
#   TOOLCHAIN=.../arm-none-eabi- synth-open/tools/audio_opt/run_synth_part_arm.sh [blocks]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT0=$(cd "$HERE/../../.." && pwd)
PY=${PYTHON:-python3}
TC=${TOOLCHAIN:+${TOOLCHAIN%-}}
TC=${TC:-$ROOT0/agt/12.3.rel1/bin/arm-none-eabi}
OUT=${OUT:-/tmp/audio_opt}; mkdir -p "$OUT"
FL="-std=gnu++20 -O2 -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -fno-rtti -fno-exceptions -fno-threadsafe-statics -DNDEBUG"
"$TC-g++" $FL -I"$ROOT0/core/libs/Audio" -c "$HERE/synth_part_arm.cpp" -o "$OUT/synth_part_arm.o"
"$TC-g++" -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -specs=nano.specs -nostartfiles -Wl,--gc-sections -Wl,-u,bench_setup -Wl,-u,bench_run \
  -Wl,-T,"$HERE/link.ld" "$OUT/synth_part_arm.o" -Wl,--start-group -lm -lc -lgcc -Wl,--end-group -o "$OUT/synth_part_arm.elf" 2> /dev/null
"$PY" -I "$HERE/synth_part_arm_run.py" "$OUT/synth_part_arm.elf" "$TC" "${1:-3000}"
