#!/bin/bash
# fx_columns_sim.sh
#
# Which FX slot a held FX button, Delete, Copy + Paste and the Shift + FX1/FX2 inversion act on, on the real pattern editor code
# (ui_shot). Written for the suspicion in ref/four-fx-slots.md section 5: ELEMENTS_FX1 acts on fx[0] while the FX1 column shows fx[1],
# so "hold FX1 + Delete" might clear the FX2 column. Result: it does not, the button names are crossed in two places that cancel out.
#   - the pattern editor shows fx[1] in the LEFT column and fx[0] in the RIGHT column (fPatternEdit.cpp, row.fx[0] <- fx[1])
#   - the left button (host name "Vol", interfaceButtonVol, functFx1) sets editParam EditFx1 and Delete then clears ELEMENTS_FX2 = fx[1]
#   - the right button (host name "Fx", functFx2) sets EditFx2 and Delete clears ELEMENTS_FX1 = fx[0]
#   - Shift + Copy with a button held asks getSelectedElement(), which tests isButtonPressed(interfaceButtonFx1) = the RIGHT button
#     (interfaceDefs.h: Fx1 = button21 = "Fx") and answers ELEMENTS_FX1 = fx[0] = the right column: the same pairing
# The column a button names is the column the action hits. This script pins that, so a change of the slot indices (the 4 FX work) is caught.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
WORK="$(mktemp -d "${TMPDIR:-/tmp}/fx_columns_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() {   # run NAME "script": the step / cursor lines of the script
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$1" "wait 500; $2" 2>"$WORK/$1.err" | grep -E '^(step|stepv|cursor) '
}
# step 0 holds fx[0] = 7:11 (right column) and fx[1] = 8:22 (left column), the step of a fresh scenario
S="instr 48; setstep 0 0 48 48 7 11 8 22; setstep 0 1 -1 0 0 0 0 0; tap Rec"

out=$(run del_left "$S; tap Vol; hold Vol; tap Delete; unhold Vol; step 0 0")
expect "D1 left button (FX1 column) held + Delete clears fx[1], the left column" "step 0 0 note 48 instr 48 fx0 7:11 fx1 0:22" "$(grep '^step' <<<"$out")"
out=$(run del_right "$S; tap Fx; hold Fx; tap Delete; unhold Fx; step 0 0")
expect "D2 right button (FX2 column) held + Delete clears fx[0], the right column" "step 0 0 note 48 instr 48 fx0 0:11 fx1 8:22" "$(grep '^step' <<<"$out")"
out=$(run del_cursor "$S; tap Vol; tap Delete; step 0 0")
expect "D3 cursor in the left column + Delete clears fx[1]" "step 0 0 note 48 instr 48 fx0 7:11 fx1 0:22" "$(grep '^step' <<<"$out")"

# copy step 0, paste on step 1 with the left button held (Shift + Copy pastes), then on step 2 with the right one
out=$(run paste_left "$S; tap Vol; tap Copy; tap Down; hold Vol; hold Shift; tap Copy; unhold Shift; unhold Vol; step 0 1")
expect "P1 paste with the left button held pastes fx[1] only" "step 0 1 note -1 instr 0 fx0 0:0 fx1 8:22" "$(grep '^step' <<<"$out")"
out=$(run paste_right "$S; tap Vol; tap Copy; tap Down; tap Fx; hold Fx; hold Shift; tap Copy; unhold Shift; unhold Fx; step 0 1")
expect "P2 paste with the right button held pastes fx[0] only" "step 0 1 note -1 instr 0 fx0 7:11 fx1 0:0" "$(grep '^step' <<<"$out")"

# Mic/Vol view (Note pressed once more on the Note column): the left column shows m, the right one V. Step 0 holds V 51 and m 30.
# Shift + Copy with a column button held pastes that column only. Stock (and 1.4.0 before this fix) crossed the two: the right
# button pasted the micro-move and the left one the volume (getSelectedElement answered ELEMENTS_MICROMOVE for the right button).
MV="instr 48; setstep 0 0 48 48 0 0 0 0; setstep 0 1 -1 0 0 0 0 0; setmm 0 0 30; tap Rec; tap Note; tap Fx; pad 24; tap Up"
out=$(run mv_right "$MV; tap Copy; tap Down; hold Fx; hold Shift; tap Copy; unhold Shift; unhold Fx; stepv 0 1")
expect "M1 Mic/Vol view: paste with the right button (the V column) held pastes the volume only" "stepv 0 1 vol 51 move -1" "$(grep '^stepv' <<<"$out")"
out=$(run mv_left "$MV; tap Copy; tap Down; tap Vol; hold Vol; hold Shift; tap Copy; unhold Shift; unhold Vol; stepv 0 1")
expect "M2 Mic/Vol view: paste with the left button (the m column) held pastes the micro-move only" "stepv 0 1 vol -1 move 30" "$(grep '^stepv' <<<"$out")"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
