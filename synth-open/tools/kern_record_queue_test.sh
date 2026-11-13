#!/bin/sh
# AudioRecordQueue leaving update_all while it is disabled (record_queue.cpp, AudioStream.cpp transmit and connect): the real AudioStream.cpp and
# record_queue.cpp of the old tree (git OLD, default release-1.3.0-int 1d9c033) and of this tree run the same random script on the host
# (kern_record_queue_test.cpp). The traces (every block a queue hands to its reader, the blocks in use, what a plain consumer saw) must be equal.
#   synth-open/tools/kern_record_queue_test.sh [WORKTREE]
set -e
OLD=${OLD:-1d9c033}
ROOT=$(cd "${1:-$(dirname "$0")/../..}" && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/kern_rq.XXXXXX"); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/old" "$T/new"
cd "$ROOT"
git cat-file -e "$OLD:core/libs/Audio/record_queue.cpp" 2> /dev/null || { echo "kern_record_queue_test: commit $OLD not in this repository, SKIPPED"; exit 0; }
for f in AudioStream.cpp AudioStream.h record_queue.cpp record_queue.h; do
  git show "$OLD:core/libs/Audio/$f" > "$T/old/$f"
  cp "core/libs/Audio/$f" "$T/new/$f"
done
for d in old new; do
  # AudioStream.cpp and record_queue.cpp include the engine and the exporter (unused there) and the Cortex-M4 dsp header (unused there)
  for f in AudioStream.cpp record_queue.cpp; do
    grep -v -e '#include "mtAudioEngine.h"' -e '#include <mtAudioExport.h>' -e '#include "utility/dspinst.h"' "$T/$d/$f" > "$T/$d/x_$f"
  done
  c++ -std=c++20 -O1 -Wall -Wno-unused -I synth-open/tools/kern_host -I "$T/$d" "$T/$d/x_AudioStream.cpp" "$T/$d/x_record_queue.cpp" \
    synth-open/tools/kern_record_queue_test.cpp -o "$T/$d/run"
done
bad=0
for seed in 1 2 3 4 5 6 7 8 9 10 11 12; do
  "$T/old/run" $seed > "$T/old.trace" 2> /dev/null; "$T/new/run" $seed > "$T/new.trace" 2> "$T/new.err"
  if ! cmp -s "$T/old.trace" "$T/new.trace"; then echo "seed $seed: traces differ"; diff "$T/old.trace" "$T/new.trace" | head -5; bad=$((bad+1)); fi
done
echo "new tree, last seed: $(cat "$T/new.err")"
echo "kern_record_queue_test: 12 seeds x 4000 rounds, $(wc -l < "$T/new.trace") trace lines per seed, $bad differ"
if [ "$bad" -eq 0 ]; then echo PASS; else echo FAIL; exit 1; fi
