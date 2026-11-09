#!/bin/bash
# Cost of the synth objects in the audio graph while no synth voice sounds, on an emulated Cortex-M4F (NOT the device, no flash wait states).
#   synth-open/tools/idle_cycles/run_bench.sh
# Real code: AudioStream (core/libs/Audio/AudioStream.cpp, two includes cut), AudioAmplifier, AudioAnalyzeRMS, AudioMixer<11>, applyGain.
# PolySynth::update is a literal copy of core/libs/Audio/synth_PolySynth.cpp (mono path, Synth stubbed: the shim's renderAudioBuffer clears
# and OpenInstrument::render returns at once). Graph: 3 parts -> 6 panning amplifiers + 6 analysers (+ 6 export queue sinks) -> two tracks
# mixers that already carry one playing sample track. Scenarios (SC): 0 = Voices 0, 1 = Voices 1 and nothing sounding, 2 = no synth objects.
# EXTRA_FLAGS=-DBENCH_GAIN_DB=0.0f runs the parts at 0 dB (Synth::kGainDefault is -9 dB, 0 dB takes the cheaper reuseUnmodifiedMono path).
# base = the code before the idle skip, skip = with open::partIdle (-DIDLE_SKIP). Needs the pinned toolchain in agt/10.3 and a python with
# unicorn + capstone:  python3 -m venv $VENV && $VENV/bin/pip install unicorn capstone    (PYTHON=$VENV/bin/python)
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
HERE=$ROOT/synth-open/tools/idle_cycles
PY=${PYTHON:-python3}
TC=$ROOT/agt/10.3/bin/arm-none-eabi
OUT=${TMPDIR:-/tmp}/idle_cycles_$$
mkdir -p "$OUT"
R=$ROOT
INC="-I$R/core/include -I$R/core/config -I$R/core/source -I$R/core/synth -I$R/core/libs -I$R/core/libs/Audio -I$R/core/libs/ElapsedMillis -I$R/core/libs/Audio/utility -I$R/core/interface -I$R/core/effector -I$R/core/FileManager -I$R/core/Exporter -I$R/core/AudioEngine -I$R/core/libs/Audio/Polyverb -I$R/core/AudioEngine/ConstantValues -I$R/core/AudioEngine/MasterEnhancer -I$R/util/include -I$R/util/3rdparty -I$R/synth/include -I$R/core/libs/NewDisplay -I$R/core/libs/FT812 -I$R/core/libs/fonts"
FL="-std=gnu++20 -O2 -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -fno-rtti -fno-exceptions -fno-threadsafe-statics -DNDEBUG -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN -D__MK66FX1M0__ -DKINETISK -include assert.h $EXTRA_FLAGS"
sed -e '/#include "mtAudioEngine.h"/d' -e '/#include <mtAudioExport.h>/d' "$R/core/libs/Audio/AudioStream.cpp" > "$OUT/AudioStream_bench.cpp"
for v in base skip; do
  X=""; [ $v = skip ] && X="-DIDLE_SKIP"
  mkdir -p "$OUT/o_$v"
  for f in "$HERE/bench_idle.cpp" "$OUT/AudioStream_bench.cpp" "$R/core/libs/Audio/amplifier.cpp" "$R/core/libs/Audio/analyze_rms.cpp" "$R/core/libs/Audio/mixer.cpp" "$R/core/libs/Audio/dspApplyGain.cpp"; do
    "$TC-g++" $FL $X $INC -c "$f" -o "$OUT/o_$v/$(basename "$f" .cpp).o"
  done
  "$TC-g++" -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -specs=nano.specs -nostartfiles -Wl,--gc-sections -Wl,-u,bench_setup -Wl,-u,bench_run \
    -Wl,-T,"$HERE/link.ld" "$OUT/o_$v"/*.o -Wl,--start-group -lm -lc -lgcc -Wl,--end-group -o "$OUT/bench_$v.elf"
done
for v in base skip; do for sc in 2 0 1; do SC=$sc "$PY" -I "$HERE/run_bench.py" "$OUT/bench_$v.elf" "$TC" | head -1; done; done
[ -n "$KEEP_DIR" ] || rm -rf "$OUT"
