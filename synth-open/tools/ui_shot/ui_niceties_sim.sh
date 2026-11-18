#!/bin/bash
# ui_niceties_sim.sh
#
# Idea 16, three small UI niceties (ref/idea-ui.md), on the EXTHOOKS=1 ui_shot (the Features page is a FEATURE_EXT_HOOKS thing):
#   K  Insert on a synth patch parameter value: middle, maximum, minimum, middle (the value knob press of a groovebox). Value rows use the
#      knob scale (edit mapped 0..1), list rows step over their entries (the section grid and macro rows are guarded in code, not driven here)
#   P  Config > Features > Play stop lock: a running song stops with Shift + Play only
#   D  Config > Features > Demo start: the next start opens an empty project and parks the Workspace as Workspace.keep, switched off
#      the demo Workspace is dropped and the parked one comes back
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD="$ROOT/build/ui-shot-ext"
BIN="$BUILD/ui_shot"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/ui_niceties_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
make -s -C "$HERE" -j4 EXTHOOKS=1 >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() {   # run NAME "script": the card folder $WORK/NAME is kept between runs, the poke lines go to stdout
    mkdir -p "$WORK/$1"; rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$BIN" "$WORK/$1" "wait 500; $2" >"$WORK/$1.out" 2>"$WORK/$1.err"
    cat "$WORK/poke.out" 2>/dev/null
}
has() { [ -e "$WORK/$1/$2" ] && echo yes || echo no; }
cfg_on() { mkdir -p "$WORK/$1/Features"; printf 'feature 1\nid %s\nversion 1\non 1\n' "$2" > "$WORK/$1/Features/$2.cfg"; }

# ---- K: the synth patch editor, part 64 (WTFM), Osc section: position 1 is Ratio 1 (a value), position 5 Character 1 (a list)
SYN='instr 64; tap Params; wait 1200; tap F8; wait 1500'
V='poke synthval; wait 50'
INS='tap Insert; wait 300'
out=$(run k1 "$SYN; $V; $INS; $V; tap Right; wait 400; tap Down; wait 800; $V; $INS; $V; $INS; $V; $INS; $V; $INS; $V")
norms=$(echo "$out" | sed -n 's/.*norm \(.*\)/\1/p' | tr '\n' ' ')
expect "K1 value row (Ratio 1): Insert cycles the knob scale mid, max, min, mid" "yes" "$(echo "$norms" | awk '{print ($(NF-3)==0.500 && $(NF-2)==1.000 && $(NF-1)==0.000 && $NF==0.500) ? "yes" : "no:" $0}')"
out=$(run k2 "$SYN; tap Right; wait 400; tap Down 5; wait 800; $V; $INS; $V; $INS; $V; $INS; $V; $INS; $V")
mode=$(echo "$out" | sed -n 1p | sed 's/.*mode \([a-z]*\).*/\1/')
pos=$(echo "$out" | sed 's/.*list \([0-9]*\) norm.*/\1/' | tr '\n' ' ')
expect "K2 list row is a list" "list" "$mode"
expect "K2 list row: 0, middle, last, back to 0, middle" "yes" "$(echo "$pos" | awk '{print ($1==0 && $2>0 && $3>$2 && $4==0 && $5==$2) ? "yes" : "no:" $0}')"

# ---- P: Play stop lock
PLAY='tap Pattern; wait 300; tap Play; wait 400; poke playing; tap Play; wait 400; poke playing; hold Shift; tap Play; unhold Shift; wait 400; poke playing'
cp_=$(run p0 "$PLAY" | awk '{printf "%s ", $2}')
expect "P0 lock off: Play starts, Play stops, (Shift+Play starts again)" "1 0 1 " "$cp_"
cfg_on p1 playlock
out=$(run p1 "$PLAY" | awk '{printf "%s ", $2}')
expect "P1 lock on: Play starts, Play does not stop, Shift + Play stops" "1 1 0 " "$out"
out=$(run p1 "tap Pattern; wait 300; tap Play; wait 300; poke playing; hold Shift; tap Play; unhold Shift; wait 300; poke playing; tap Play; wait 300; poke playing" | awk '{printf "%s ", $2}')
expect "P2 lock on: Shift + Play stops, Play starts again" "1 0 1 " "$out"
# switched on through the page: Config > Features > Play stop lock > State On
mkdir -p "$WORK/p3"
run p3 "tap Config; wait 800; tap Down 4; wait 300; tap F8; wait 500; tap Down 2; wait 300; tap F8; wait 500; tap F8; wait 300; tap Down; wait 200; tap F8; wait 3000" >/dev/null   # the page saves after a debounce
expect "P3 the page writes playlock.cfg with on 1" "yes" "$(grep -q '^on 1' "$WORK/p3/Features/playlock.cfg" 2>/dev/null && echo yes || echo no)"
out=$(run p3 "$PLAY" | awk '{printf "%s ", $2}')
expect "P3 and the next start keeps the lock" "1 1 0 " "$out"

# ---- D: Demo start
run d1 "put Workspace/markerA.txt MA; wait 200" >/dev/null
expect "D0 normal start: no Workspace.keep" "no" "$(has d1 Workspace.keep)"
cfg_on d1 demo
run d1 "put Workspace/markerD.txt MD; wait 3000; tap Pattern; wait 500; step 0 2" >/dev/null
expect "D1 demo start parks the Workspace (marker A inside)" "yes" "$(has d1 Workspace.keep/markerA.txt)"
expect "D1 the demo Workspace is a new project (no marker A)" "no" "$(has d1 Workspace/markerA.txt)"
expect "D1 the demo Workspace has its project file" "yes" "$(has d1 Workspace/project.mt)"
run d1 "wait 2000" >/dev/null
expect "D2 second demo start: Workspace.keep is not overwritten" "yes" "$(has d1 Workspace.keep/markerA.txt)"
expect "D2 second demo start: the earlier demo is dropped" "no" "$(has d1 Workspace/markerD.txt)"
rm -f "$WORK/d1/Features/demo.cfg"
run d1 "wait 2000" >/dev/null
expect "D3 demo off: the parked Workspace is back" "yes" "$(has d1 Workspace/markerA.txt)"
expect "D3 demo off: Workspace.keep is gone" "no" "$(has d1 Workspace.keep)"
expect "D3 demo off: the demo Workspace is gone" "no" "$(has d1 Workspace/markerD.txt)"

echo
if [ "$fails" = 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
