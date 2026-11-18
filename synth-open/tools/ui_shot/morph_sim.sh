#!/bin/bash
# morph_sim.sh [PNG_DIR]
#
# The Morph step FX (o, id FX_TYPE_PATCH_MORPH) end to end on the ui_shot host binary: the pattern step goes through Sequencer::tick,
# audioEngine::seqPatchMorph and Synth::applyPatchMorph into the patch bank of synth part 0 (built in WTFM), and the values the part
# plays are read back ("poke rawvals 0"). Setup per scenario: slot 0 = every parameter at 50 % of its range, slot 1 at 20 %, slot 3 at
# 80 % ("poke slotfill", saved through Synth::savePatchToWorkspace, so the bank has them), slot 0 open.
#
#   S  the Patch FX alone (the reference rows R1, R3)
#   M  Morph alone (default target slot 1), next to a Patch FX (that slot is the target), 0 / 50 / 100
#   T  the Morph target on the Synth screen: Shift + encoder on the slot selector, the popup, the panel heading, the names file m= line,
#      the target after a fresh start from the same card, a Morph without Patch FX then goes to it
#   P  the pattern title bar names the blend and the target
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)". SAN=address,undefined: the sanitizer build.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="$BUILD/ui_shot"
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/morph_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
make -s -C "$HERE" -j4 ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

PATCH=46   # FX_TYPE_PATCH_CHANGE (FX_TYPE_R2)
MORPH=49   # FX_TYPE_PATCH_MORPH (FX_TYPE_R8)
fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
expect() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected: $2
      got:      $3"; fi; }

# run NAME "script" [keep]: the poke output and the stdout lines of the script, a fresh card unless keep
run() {
    local sd="$WORK/sd-$1"
    [ "${3:-}" = keep ] || { rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"; }
    rm -f "$WORK/poke" "$WORK/poke.out"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "wait 500; $2" >"$WORK/$1.out" 2>"$WORK/$1.err" ||
        { echo "ui_shot failed ($1):"; tail -5 "$WORK/$1.err"; }
    [ -f "$WORK/poke.out" ] && cat "$WORK/poke.out"
    cat "$WORK/$1.out"
}
vals() { grep '^rawvals' | tail -1 | awk '{print $NF}'; }   # the last values line
field() { grep '^rawvals' | tail -1 | awk -v k="$1" '{for (i = 1; i < NF; i++) if ($i == k) print $(i + 1)}'; }

SETUP="audio 1; instr 64; tap Params; wait 1500; poke slotfill 0 0 50; poke slotfill 0 1 20; poke slotfill 0 3 80; tap Pattern; wait 1200"
PLAY="tap Play; wait 400; tap Play; wait 200; poke rawvals 0"
step() { printf 'setstep 0 0 48 64 %s %s %s %s' "$1" "$2" "$3" "$4"; }   # track 1 step 0: C-4 on synth part 0 with the two FX

echo "-- S: Patch FX reference rows"
A="$(run a "$SETUP; poke rawvals 0" | vals)"
R1="$(run r1 "$SETUP; $(step $PATCH 1 0 0); $PLAY" | vals)"
R3="$(run r3 "$SETUP; $(step $PATCH 3 0 0); $PLAY" | vals)"
if [ -n "$A" ] && [ -n "$R1" ] && [ -n "$R3" ] && [ "$A" != "$R1" ] && [ "$A" != "$R3" ] && [ "$R1" != "$R3" ]; then
    pass "S1 slots 0, 1 and 3 hold different values and the Patch FX plays slot 1 and slot 3"
else
    fail "S1 slots 0, 1 and 3 differ" "A=$A R1=$R1 R3=$R3"
fi

echo "-- M: Morph FX through the sequencer"
expect "M1 Morph 100 alone goes to the default target, slot 1" "$R1" "$(run m1 "$SETUP; $(step $MORPH 100 0 0); $PLAY" | vals)"
expect "M2 Morph 100 next to Patch 3 goes to slot 3 (the Patch FX names the target, it does not switch)" "$R3" "$(run m2 "$SETUP; $(step $PATCH 3 $MORPH 100); $PLAY" | vals)"
expect "M3 the same with the FX in the other order" "$R3" "$(run m3 "$SETUP; $(step $MORPH 100 $PATCH 3); $PLAY" | vals)"
expect "M4 Morph 0 next to Patch 3 plays the open slot" "$A" "$(run m4 "$SETUP; $(step $PATCH 3 $MORPH 0); $PLAY" | vals)"
M50="$(run m5 "$SETUP; $(step $PATCH 3 $MORPH 50); $PLAY" | vals)"
verdict="$(python3 -I - "$A" "$R3" "$M50" <<'EOF'
import sys
a, b, m = ([float(x) for x in s.split(",")] for s in sys.argv[1:4])
if not (len(a) == len(b) == len(m)):
    print("length"); sys.exit()
mid = ends = bad = 0
for x, y, v in zip(a, b, m):
    half = x + (y - x) * 0.5
    if x != y and abs(v - half) <= 1e-4 * max(1.0, abs(half)): mid += 1
    elif x != y and v == y: ends += 1          # a discrete one switched at 50
    elif x != y and abs(v - round(half)) < 1e-6 and min(x, y) - 0.5 <= v <= max(x, y) + 0.5: mid += 1   # a stepped one, rounded
    elif x == y and v == x: pass
    else: bad += 1
print("ok" if bad == 0 and mid > 0 else "bad=%d mid=%d" % (bad, mid), "mid=%d switched=%d" % (mid, ends))
EOF
)"
case "$verdict" in ok*) pass "M5 Morph 50 next to Patch 3: every value at the half of slot 0 and slot 3, or switched to slot 3 (${verdict#ok })" ;;
    *) fail "M5 Morph 50 blend" "$verdict" ;; esac
expect "M6 a later Morph 0 step brings the open slot back" "$A" "$(run m6 "$SETUP; $(step $PATCH 3 $MORPH 50); setstep 0 1 48 64 $MORPH 0 0 0; tap Play; wait 800; tap Play; wait 200; poke rawvals 0" | vals)"
expect "M7 a Morph with no file at the target (slot 2) changes nothing" "$A" "$(run m7 "$SETUP; $(step $PATCH 2 $MORPH 100); $PLAY" | vals)"

echo "-- T: Morph target on the Synth screen"
# the slot selector is the 7th place of the Synth screen (Right 6 times from Model), Shift + encoder sets the target one slot a detent,
# 2 detents from the default slot 1
TGT="audio 1; instr 64; tap Params; wait 1500; poke slotfill 0 0 50; poke slotfill 0 1 20; poke slotfill 0 3 80; tap Right 6; wait 300; hold Shift; enc 1; wait 100; enc 1; unhold Shift; wait 300"
out="$(run t1 "$TGT; poke popup; poke heading; poke rawvals 0${PNG_DIR:+; shot $PNG_DIR/morph-target.png}; cat Workspace/0.names")"
expect "T1 Shift + encoder 2 on the slot selector: target 3" "3" "$(printf '%s\n' "$out" | field target)"
case "$out" in *"Morph target: 3"*) pass "T2 the popup says Morph target: 3" ;; *) fail "T2 popup" "$(printf '%s\n' "$out" | grep -i popup)" ;; esac
case "$out" in *"heading label Slot 0  Morph 3:|"*) pass "T3 the patch panel heading says Slot 0  Morph 3:" ;; *) fail "T3 panel heading" "$(printf '%s\n' "$out" | grep '^heading')" ;; esac
expect "T4 the names file has the m=3 line" "m=3" "$(printf '%s\n' "$out" | grep -x 'm=3')"
expect "T5 the slot itself did not move" "0" "$(printf '%s\n' "$out" | field slot)"
out="$(run t1 "poke rawvals 0" keep)"
expect "T6 a fresh start from the same card reads the target back" "3" "$(printf '%s\n' "$out" | field target)"
expect "T7 after the restart a Morph 100 alone goes to slot 3" "$R3" "$(run t1 "tap Pattern; wait 1200; audio 1; $(step $MORPH 100 0 0); $PLAY" keep | vals)"
out="$(run t8 "$TGT; hold Shift; enc -1; wait 100; enc -1; unhold Shift; wait 300; poke rawvals 0; poke heading; cat Workspace/0.names")"
expect "T8 back to slot 1: default target, the m= line is gone" "1 0" "$(printf '%s\n' "$out" | field target) $(printf '%s\n' "$out" | grep -c '^m=')"
case "$(printf '%s\n' "$out" | grep '^heading')" in "heading label Slot 0:|"*) pass "T9 the default target is not shown in the heading" ;; *) fail "T9 the default target is not shown" "$(printf '%s\n' "$out" | grep '^heading')" ;; esac
out="$(run t10 "audio 1; instr 64; tap Params; wait 1500; poke rawvals 0")"
[ -f "$WORK/sd-t10/Workspace/0.names" ] && fail "T10 a part that never set a target has no names file" "$(cat "$WORK/sd-t10/Workspace/0.names")" ||
    pass "T10 a part that never set a target writes no names file"

echo "-- P: pattern title bar"
# the Fx key puts the cursor on the FX column that shows the first FX of the step (the Morph here)
P="$SETUP; $(step $MORPH 40 $PATCH 3); tap Fx; wait 300; cursor; title${PNG_DIR:+; shot $PNG_DIR/morph-title.png}"
P2="$SETUP; $(step $MORPH 75 0 0); tap Fx; wait 300; title"
out="$(run p1 "$P")"
case "$out" in *"title Morph: 40% to slot 3"*) pass "P1 the title bar of the Morph cell: Morph: 40% to slot 3 (the Patch FX next to it)" ;; *) fail "P1 title bar" "$(printf '%s\n' "$out" | grep -E '^(title|cursor)')" ;; esac
expect "P2 without a Patch FX the title names the part's target" "title Morph: 75% to slot 1" "$(run p2 "$P2" | grep '^title')"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
