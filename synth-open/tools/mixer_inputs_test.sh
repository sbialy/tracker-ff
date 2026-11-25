#!/bin/sh
# The input limit of AudioMixer on the host: the real AudioStream.cpp, mixer.cpp and dspApplyGain.cpp under AddressSanitizer, as the original
# (kMaxInputs 13) and as Tracker+/Mini (-DPLATFORM_IMX, 21). Each size runs and sums exactly, and a mixer one input over the limit does not compile
# (static_assert in AudioMixer), so mix() can never be handed a table it overruns. Description of the scenarios: mixer_inputs_test.cpp.
#   synth-open/tools/mixer_inputs_test.sh [WORKTREE]
set -e
ROOT=$(cd "${1:-$(dirname "$0")/../..}" && pwd -P)
T=$(mktemp -d "${TMPDIR:-/tmp}/mixer_inputs_test.XXXXXX"); trap 'rm -rf "$T"' EXIT
cd "$ROOT"
# the host copy of libs/Audio that ui_shot generates (Cortex-M DSP instructions with C bodies), as duck_test.sh does
make -C synth-open/tools/ui_shot -s "$ROOT/build/ui-shot/gen/.stamp" > /dev/null
G=build/ui-shot/gen/libs/Audio
mkdir -p "$T/utility" "$T/ss"
# the host layout of strInstrument is not the firmware's (mtStructs.h size assert): a copy without that one assert, so the static_asserts of
# the mixer stay real (duck_test.sh blanks every static_assert instead)
grep -v 'static_assert(sizeof(strInstrument) == 372' core/source/mtStructs.h > "$T/ss/mtStructs.h"
python3 -I - "$G/utility/dspinst.h" "$T/utility/dspinst.h" <<'PY'
import sys
t = open(sys.argv[1]).read()
a = t.index('static inline int16_t saturate16(int32_t val)\n{')
b = t.find('return 0; // TODO....', a, t.find('}', a))
if b >= 0:
    t = t[:b] + 'return val > 32767 ? 32767 : (val < -32768 ? -32768 : (int16_t)val);' + t[b + len('return 0; // TODO....'):]
open(sys.argv[2], 'w').write(t)
PY
grep -v -e '#include "mtAudioEngine.h"' -e '#include <mtAudioExport.h>' $G/AudioStream.cpp > "$T/x_AudioStream.cpp"
INC="-I $T/ss -I $T/utility -I synth-open/tools/kern_host -I $G -I core/source -I core/include -I core/synth -I core/libs -I core/libs/ElapsedMillis -I core/libs/SerialFlash \
  -I core/libs/SPI -I core/libs/FT812 -I core/libs/fonts -I core/libs/NewDisplay -I core/interface -I core/effector -I core/FileManager -I core/Exporter \
  -I core/AudioEngine -I core/AudioEngine/ConstantValues -I core/AudioEngine/MasterEnhancer -I util/include -I util/3rdparty -I synth/include -I synth-open"
DEF="-std=c++20 -O1 -w -g -fsanitize=address -fno-omit-frame-pointer -include cstdint -include assert.h -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN -DFEATURE_SAT_SOFTKNEE"
bad=0
for P in og imx; do
  [ $P = imx ] && PD=-DPLATFORM_IMX || PD=
  c++ $DEF $PD $INC "$T/x_AudioStream.cpp" $G/mixer.cpp $G/dspApplyGain.cpp synth-open/tools/mixer_inputs_test.cpp -o "$T/t_$P"
  if [ $P = imx ]; then SC="11 18 21 22"; MAX=21; else SC="11 13 22"; MAX=13; fi
  echo "== $P, kMaxInputs $MAX"
  for s in $SC; do "$T/t_$P" $s || bad=$((bad+1)); done
  # one over the limit: the explicit instantiation must be refused by the static_assert, and one at the limit accepted
  for k in $MAX $((MAX+1)); do
    printf '#include "mixer.cpp"\nunsigned s = sizeof(AudioMixer<%d>);\n' $k > "$T/over.cpp"
    if c++ $DEF $PD $INC -I $G -fsyntax-only "$T/over.cpp" > "$T/over.log" 2>&1; then r=accepted; else r=refused; fi
    if [ $k = $MAX ]; then want=accepted; else want=refused; fi
    if [ $r = refused ] && ! grep -q "kMaxInputs" "$T/over.log"; then r="refused for another reason"; fi
    echo "AudioMixer<$k>: $r (expected $want)"
    [ "$r" = "$want" ] || { bad=$((bad+1)); head -5 "$T/over.log"; }
  done
done
[ $bad = 0 ] && echo "mixer_inputs_test: ALL PASS" || { echo "mixer_inputs_test: $bad FAILED"; exit 1; }
