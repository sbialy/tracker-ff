#!/bin/bash
# insert_defer_sim.sh [--timeline] [PNG_DIR]
#
# When the master inserts of a project load, on the real UI and plugin host code of this tree (ui_shot, the PLUG_HOST configuration) on the
# Mac. Host only: no device, no audio, every scenario has its own SD card folder and the virtual clock.
# The synth engines wait before they install (a Model pick rests 1 s on the list, SynthEditor.cpp synthModelTick). The inserts of a project
# load take the same road (synth-open/plug_defer.h): they wait kOpenRestMs (1,000 ms) after the load finished, then load ONE per main loop
# pass, kStaggerMs (250 ms) apart, insert 1 first. A power on also waits for the 4,000 ms key window. A pick on the Inserts page is not
# part of this (400 ms debounce). The script reads the trace of host_poke.cpp (state, name and parameter values of both slots with the
# virtual clock) every change of a slot (UISHOT_TRACE_INSERTS) and checks when each slot loads, and that the order and the values of the
# project survive.
#   --timeline  print every change of the slots (the before and after of the load order)
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)". Variables: PLG_DIR (folder with every Master .plg, default
# build/release-1.3.0-int/card/Plugins/Master), GLUE_SRC and GLUE_PLG as for ui_shot.sh (the Glue twin and file),
# EXPECT=old runs the checks of the firmware before plug_defer.h (both load in the same pass at the end of the key window).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
TIMELINE=0
[ "${1:-}" = "--timeline" ] && { TIMELINE=1; shift; }
BUILD="$ROOT/build/ui-shot"
BIN="$BUILD/ui_shot"
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/insert_defer_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
MASTER="${PLG_DIR:-$ROOT/build/release-1.3.0-int/card/Plugins/Master}"
[ -f "$MASTER/dirt.plg" ] || { echo "insert_defer_sim.sh: no Master plugins (set PLG_DIR to a folder with dirt.plg and glue.plg)" >&2; exit 2; }
[ -x "$BIN" ] || { echo "insert_defer_sim.sh: build first (ui_shot.sh inserts glue builds $BIN)" >&2; exit 2; }
GLUE_UID="$(python3 -I -c 'h=2166136261
for c in b"ff.glue": h=((h^c)*16777619)&0xFFFFFFFF
print("%08x"%h)')"
# Dirt in insert 1 (Fold, Drive 62, 12 bit, Rate 2, Tone 40), Glue in insert 2 (Makeup 6, Mix 70): the order and the values must survive
SIDECAR="tffplug 1
format 2
mixer 10
slot 1 92b586d4 00010000 Dirt
p Type=2
p Drive=62
p Bits=12
p Rate=2
p Tone=40
p Mix=100
slot 2 $GLUE_UID 00010000 Glue
p Makeup=6
p Mix=70"

fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
check() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected: $2
      got:      $3"; fi; }

stage() {
    local sd="$1" f
    rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"
    for f in bbd console dirt lofi phaser tape tube; do cp "$MASTER/$f.plg" "$sd/Plugins/Master/"; done
    cp "${GLUE_PLG:-$MASTER/glue.plg}" "$sd/Plugins/Master/glue.plg"
    "$BIN" "$sd" "wait 100" >/dev/null 2>&1 || { echo "priming run failed"; exit 2; }   # a first power up writes the Workspace project
    printf '%s\n' "$SIDECAR" >"$sd/Workspace/plugins.txt"
}

run() {   # run SD SCRIPT -> $WORK/poke.out: every change of a slot's state or values with the virtual clock (UISHOT_TRACE_INSERTS, host_poke.cpp)
    rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_TRACE_INSERTS=1 UISHOT_POKE="$WORK/poke" "$BIN" "$1" "$2" >"$WORK/out.txt" 2>"$WORK/err.txt" || { echo "ui_shot failed:"; tail -5 "$WORK/err.txt"; fails=$((fails+1)); }
    [ -f "$WORK/poke.out" ] || : >"$WORK/poke.out"
}
# a slot is loaded when it runs, or when it is loaded and bypassed by a built-in Saturation entry (the project's mixer stage is not Inserts here)
# t_load N [AFTER]: the first virtual ms after AFTER at which slot N (1 or 2) is loaded, empty when never
t_load() { awk -v n="$1" -v after="${2:-0}" '/^inserts/ { split($2, t, "="); f = $(2 + n); if (t[2] > after && (index(f, n "=running:") == 1 || index(f, n "=bypassed:") == 1)) { print t[2]; exit } }' "$WORK/poke.out"; }
# t_pending AFTER: the first ms after AFTER at which a slot is pending (the load of the project queued both)
t_pending() { awk -v after="${1:-0}" '/^inserts/ { split($2, t, "="); if (t[2] >= after && ($3 ~ /^1=pending/ || $4 ~ /^2=pending/)) { print t[2]; exit } }' "$WORK/poke.out"; }
last_line() { grep '^inserts' "$WORK/poke.out" | tail -1; }
show_timeline() { [ "$TIMELINE" = 1 ] && sed 's/^/      /' "$WORK/poke.out"; return 0; }
LOADED="inserts 1=bypassed:Dirt[2,62,12,2,40,100] 2=bypassed:Glue[-12,1,3,4,6,70,0,0]"

echo "### scenario 1: power on with a project that has two inserts: nothing loads before the key window closed and the load rested, then one at a time"
sd="$WORK/s1"; stage "$sd"
run "$sd" "wait 200"
show_timeline
tq="$(t_pending 0)"; t1="$(t_load 1)"; t2="$(t_load 2)"
echo "      the project load queued both at T=${tq:-never} ms, insert 1 is loaded at T=${t1:-never}, insert 2 at T=${t2:-never}"
if [ -z "$tq" ] || [ -z "$t1" ] || [ -z "$t2" ]; then fail "both inserts load" "queued=${tq:-never} t1=${t1:-never} t2=${t2:-never}"; else
    [ "$t1" -ge 4000 ] && pass "insert 1 waits for the 4,000 ms key window" || fail "insert 1 waits for the key window" "T=$t1"
    if [ "${EXPECT:-new}" = old ]; then
        [ "$t2" -le "$((t1 + 5))" ] && pass "(before plug_defer.h) both inserts load in the same pass" || fail "same pass" "t1=$t1 t2=$t2"
    else
        [ "$t1" -ge "$((tq + 1000))" ] && pass "insert 1 loads at least 1,000 ms after the load queued it" || fail "rest" "queued=$tq t1=$t1"
        [ "$t2" -ge "$((t1 + 250))" ] && pass "insert 2 loads at least 250 ms after insert 1 (one insert per pass)" || fail "stagger" "t1=$t1 t2=$t2"
    fi
fi
check "order and values survive the load" "$LOADED" "$(last_line | cut -d' ' -f1,3-)"

echo "### scenario 2: the end of a project load after the key window: the inserts wait 1 s, then load one per pass"
sd="$WORK/s2"; stage "$sd"
run "$sd" "wait 1500; poke projload; wait 3000"
show_timeline
tp="$(awk '/^projload/ { split($2, t, "="); print t[2]; exit }' "$WORK/poke.out")"
t1="$(t_load 1 "$tp")"; t2="$(t_load 2 "$tp")"
echo "      project load ends at T=$tp ms, insert 1 is loaded at T=${t1:-never}, insert 2 at T=${t2:-never}"
if [ -z "$tp" ] || [ -z "$t1" ] || [ -z "$t2" ]; then fail "both inserts load again" "tp=${tp:-none} t1=${t1:-never} t2=${t2:-never}"; else
    if [ "${EXPECT:-new}" = old ]; then
        [ "$t1" -le "$((tp + 50))" ] && pass "(before plug_defer.h) insert 1 loads in the next pass" || fail "next pass" "tp=$tp t1=$t1"
        [ "$t2" -le "$((t1 + 5))" ] && pass "(before plug_defer.h) insert 2 in the same pass" || fail "same pass" "t1=$t1 t2=$t2"
    else
        [ "$t1" -ge "$((tp + 1000))" ] && [ "$t1" -le "$((tp + 1050))" ] && pass "insert 1 loads 1,000 ms after the load, as the Model list rests" || fail "rest" "tp=$tp t1=$t1"
        [ "$t2" -ge "$((t1 + 250))" ] && [ "$t2" -le "$((t1 + 300))" ] && pass "insert 2 loads 250 ms after insert 1, a pass of its own" || fail "stagger" "t1=$t1 t2=$t2"
    fi
fi
check "order and values survive the second load" "$LOADED" "$(last_line | cut -d' ' -f1,3-)"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
