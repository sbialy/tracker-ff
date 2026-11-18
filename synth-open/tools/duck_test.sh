#!/bin/sh
# Send ducking on the host: the real AudioStream.cpp, mixer.cpp and dspApplyGain.cpp with the duck stream (core/libs/Audio/effect_duck.h) in a
# small graph (duck_test.cpp, the scenarios and what each checks are described there). One process per scenario, the graph registers its streams for
# good. Scenario 6 and 7 write the mixer output of a dry-only run with the duck on and off, they must be byte identical.
#   synth-open/tools/duck_test.sh [WORKTREE]
set -e
ROOT=$(cd "${1:-$(dirname "$0")/../..}" && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/duck_test.XXXXXX"); trap 'rm -rf "$T"' EXIT
cd "$ROOT"
# The host copy of libs/Audio that ui_shot generates (Cortex-M DSP instructions with C bodies, gen_overlay.py): the instruction set helpers
# of dspApplyGain.cpp are not in the small kern_host stand-ins
make -C synth-open/tools/ui_shot -s "$ROOT/build/ui-shot/gen/.stamp" > /dev/null
G=build/ui-shot/gen/libs/Audio
cmp -s core/libs/Audio/effect_duck.h $G/effect_duck.h || { echo "duck_test: $G is not a copy of core/libs/Audio"; exit 1; }
# ui_shot never plays audio: its dspinst.h has "return 0; // TODO" for saturate16 and the 32x32 multiplies. The mixer needs the first one
mkdir -p "$T/utility"
python3 -I - "$G/utility/dspinst.h" "$T/utility/dspinst.h" <<'PY'
import sys
t = open(sys.argv[1]).read()
a = t.index('static inline int16_t saturate16(int32_t val)\n{')
b = t.find('return 0; // TODO....', a, t.find('}', a))
if b >= 0:   # gen_overlay.py of idea-tape (features-140) already writes a real body: then the copy stays as it is
    t = t[:b] + 'return val > 32767 ? 32767 : (val < -32768 ? -32768 : (int16_t)val);' + t[b + len('return 0; // TODO....'):]
open(sys.argv[2], 'w').write(t)
PY
# AudioStream.cpp includes the engine and the exporter (unused there)
grep -v -e '#include "mtAudioEngine.h"' -e '#include <mtAudioExport.h>' $G/AudioStream.cpp > "$T/x_AudioStream.cpp"
c++ -std=c++20 -O1 -w "-Dstatic_assert(...)=" -include cstdint -include assert.h -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN -DFEATURE_SAT_SOFTKNEE \
  -I "$T/utility" -I synth-open/tools/kern_host -I $G -I core/source -I core/include -I core/synth -I core/libs -I core/libs/ElapsedMillis -I core/libs/SerialFlash \
  -I core/libs/SPI -I core/libs/FT812 -I core/libs/fonts -I core/libs/NewDisplay -I core/interface -I core/effector -I core/FileManager -I core/Exporter \
  -I core/AudioEngine -I core/AudioEngine/ConstantValues -I core/AudioEngine/MasterEnhancer -I util/include -I util/3rdparty -I synth/include -I synth-open \
  "$T/x_AudioStream.cpp" $G/mixer.cpp $G/dspApplyGain.cpp synth-open/tools/duck_test.cpp -o "$T/duck_test"
bad=0
for n in 1 2 3 4 5 8; do
  "$T/duck_test" $n || bad=$((bad+1))
done
"$T/duck_test" 6 "$T/dry_on.bin" || bad=$((bad+1))
"$T/duck_test" 7 "$T/dry_off.bin" || bad=$((bad+1))
if cmp -s "$T/dry_on.bin" "$T/dry_off.bin"; then echo "dry path: duck on and off byte identical ($(wc -c < "$T/dry_on.bin") bytes)"; else echo "dry path: DIFFERS"; bad=$((bad+1)); fi
# Older firmware reading a project with a duck line: the sidecar reader of the commit before ducking (OLD, default the tip this branch started from)
# skips the line and reads the rest exactly as it reads the same file without it
OLD=${OLD:-8d8a489}
if git cat-file -e "$OLD:synth-open/plug_sidecar.h" 2> /dev/null; then
  mkdir -p "$T/old"
  git show "$OLD:synth-open/plug_sidecar.h" > "$T/old/plug_sidecar.h"
  c++ -std=c++20 -O1 -w -I "$T/old" synth-open/tools/duck_oldreader.cpp -o "$T/oldreader"
  printf 'tffplug 1\nformat 2\nmixer 10\nslot 1 92b586d4 00010000 Dirt\np Type=2\np Drive=62\nslot 2 b75e3097 00010000 BBD\np Mix=45\n' > "$T/plain.txt"
  printf 'tffplug 1\nformat 2\nmixer 10\nduck 3 60 50 50\nslot 1 92b586d4 00010000 Dirt\np Type=2\np Drive=62\nslot 2 b75e3097 00010000 BBD\np Mix=45\n' > "$T/duck.txt"
  printf 'tffplug 1\nformat 2\nduck 3 60 50 50\n' > "$T/duckonly.txt"
  a=$("$T/oldreader" "$T/plain.txt"); b=$("$T/oldreader" "$T/duck.txt"); c=$("$T/oldreader" "$T/duckonly.txt")
  if [ "$a" = "$b" ]; then echo "old reader ($OLD): the file with a duck line reads as the file without it: $a"; else echo "old reader: DIFFERS: $a / $b"; bad=$((bad+1)); fi
  case "$c" in "read=1 format=2 mixer=0 slot1=00000000//0 slot2=00000000//0") echo "old reader: a duck-only file reads as an empty sidecar (no plugin, Hard Clip)";; *) echo "old reader: duck-only file: $c"; bad=$((bad+1));; esac
else echo "old reader: commit $OLD not in this repository, SKIPPED"; fi
echo "duck_test: $bad failed"
if [ "$bad" -eq 0 ]; then echo PASS; else echo FAIL; exit 1; fi
