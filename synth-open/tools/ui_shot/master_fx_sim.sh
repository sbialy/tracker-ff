#!/bin/bash
# master_fx_sim.sh
#
# M07: audioEngine::init used to start the delay with the feedback in percent (the clamp took it to 0.99) and the reverb
# with the damp that a project load turns around (setReverbDamp passes 1 - damp). The project load always re-applied both, so only the state
# between the start and the first load was wrong. This runs the real engine code of this tree on the Mac (ui_shot, the host build of
# core/ and synth-open/): for several project values it lets the project load path set the delay and the reverb, scrambles them, runs the
# code audioEngine::init calls (beginDelay, beginReverb) and compares the delay feedback byte and the reverb low pass coefficient.
# Host only, no device. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)" (exit 1).
#
#   synth-open/tools/ui_shot/master_fx_sim.sh
#   OUT   the ui_shot build directory (default build/ui-shot-masterfx of the tree). The Makefile does not track headers, use a fresh one after a merge.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="${OUT:-$ROOT/build/ui-shot-masterfx}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/master_fx_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"
make -s -C "$HERE" OUT="$OUT" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >"$OUT.make.log" 2>&1 || { tail -30 "$OUT.make.log"; exit 2; }

sd="$WORK/sd"; mkdir -p "$sd"
# percent, damp in permille: the default project (50, DEFAULT_REVERB_DAMP), the ends of both ranges and two in between
cases="50:500 0:0 100:1000 25:250 99:800"
script="wait 300"; for c in $cases; do script="$script; poke masterfx ${c%%:*} ${c##*:}"; done
UISHOT_POKE="$WORK/poke" "$OUT/ui_shot" "$sd" "$script" >"$WORK/out.txt" 2>"$WORK/err.txt" || { echo "ui_shot failed:"; tail -5 "$WORK/err.txt"; exit 2; }
[ -f "$WORK/poke.out" ] || { echo "no poke output"; exit 2; }

fails=0; checks=0
while IFS= read -r line; do
    case "$line" in
        masterfx*) checks=$((checks+1))
                   case "$line" in
                       *same) printf 'ok    %s\n' "$line" ;;
                       *) fails=$((fails+1)); printf 'FAIL  %s\n' "$line" ;;
                   esac ;;
    esac
done < "$WORK/poke.out"
[ "$checks" -eq 5 ] || { fails=$((fails+1)); echo "FAIL  expected 5 masterfx lines, got $checks"; }
echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
