#!/bin/bash
# fx34_ui_sim.sh [PNG_DIR]
#
# The pattern editor side of the "4 FX" project switch (core/source/fx34.h, ref/four-fx-slots.md) on the real pattern editor and sequencer
# code (ui_shot, Mac only, no device). The FX bank, the editing of FX3 and FX4 through the buttons, pads and the encoder, the edit
# operations that carry them (delete, copy, paste, insert, backspace, duplicate, expand, shrink, invert), the 256 entry cap (refused and
# said, never dropped), the V and m marks, the title bar text, the V/m columns option and the manual "V/m FX to fields" action.
# Slot ranks (fx34.h): rank 0 = the right column of bank 1/2 (stored fx[0], "FX2"), rank 1 = the left column (fx[1], "FX1"), rank 2 = the
# right column of bank 3/4 ("FX4"), rank 3 = the left one ("FX3"). The script's buttons: Vol = the LEFT FX button, Fx = the RIGHT one.
# Every scenario has its own SD card folder. SAN=address,undefined: the sanitizer build. Ends with RESULT: GREEN or RESULT: RED (n failing).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="$BUILD/ui_shot"
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/fx34_ui_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
GOLD="$HERE/goldens/fx34"          # byte equal PNGs of the pages, UPDATE=1 writes them
SHOTS="$WORK/png"; mkdir -p "$SHOTS"
mkdir -p "$BUILD"
make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() {   # run NAME "script": the lines of the script that read back
    mkdir -p "$WORK/$1/Plugins/Master" "$WORK/$1/Plugins/Synth"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$1" "wait 500; $2" 2>"$WORK/$1.err" | grep -E '^(step|stepv|fx34|title|ltitle|cursor|lens|setfx) '
}
shot() { echo "shot $SHOTS/$1.png"; }
line() { grep -E "^$2 " <<<"$1" | sed -n "${3:-1}p"; }

# rank ids: V = 18, m = 2, Glide = 19, Panning = 31, Random value = 16, Chance FX = 48, MIDI Chord = 14
P="instr 48; setstep 0 0 48 48 0 0 0 0; setstep 0 1 50 48 0 0 0 0; setstep 0 2 52 48 0 0 0 0; tap Rec"

echo "-- B: the FX bank"
out=$(run b_off "$P; tap Vol; tap Vol; title; hold Vol; pad 5; unhold Vol; step 0 0; fx34 0 0")
expect "B1 switch off: no bank, a second tap on FX1 changes nothing" "title - - None" "$(line "$out" title 1)"
expect "B2 switch off: hold FX1 + pad writes the stored slot fx[1] (Glide 19) and no extra FX" "step 0 0 note 48 instr 48 fx0 0:0 fx1 19:0|fx34 0 0 r2 0:0 r3 0:0 n 0" "$(line "$out" step 1)|$(line "$out" fx34 1)"
out=$(run b_on "$P; fx34on 1; tap Vol; title; tap Vol; title; tap Vol; title")
expect "B3 switch on: the second tap names the bank (FX3 on the left column), the third goes back" "title - - None|title FX3: - - None|title - - None" "$(line "$out" title 1)|$(line "$out" title 2)|$(line "$out" title 3)"
out=$(run b_on_r "$P; fx34on 1; tap Fx; tap Fx; title; tap Fx; title")
expect "B4 the right column's bank is FX4" "title FX4: - - None|title - - None" "$(line "$out" title 1)|$(line "$out" title 2)"
out=$(run b_mod "$P; fx34on 1; tap Vol; hold Vol; pad 3; unhold Vol; title")
expect "B5 hold FX1 + pad is a modifier: it does not flip the bank (still bank 1/2 after the hold)" "title P - Panning" "$(line "$out" title 1)"
out=$(run b_mv "$P; fx34on 1; tap Note; tap Vol; tap Vol; title; fx34 0 0")
expect "B6 the Mic/Vol view has no bank (a tap there flips nothing)" "title Micro-move" "$(line "$out" title 1)"

echo "-- E: editing FX3 and FX4"
out=$(run e_type "$P; fx34on 1; tap Vol; tap Vol; hold Vol; pad 3; unhold Vol; fx34 0 0; step 0 0")
expect "E1 bank 3/4, left column: hold + pad sets rank 3 (Panning 31, default 50), the stored slots stay empty" "fx34 0 0 r2 0:0 r3 31:50 n 1|step 0 0 note 48 instr 48 fx0 0:0 fx1 0:0" "$(line "$out" fx34 1)|$(line "$out" step 1)"
out=$(run e_right "$P; fx34on 1; tap Fx; tap Fx; hold Fx; pad 5; unhold Fx; fx34 0 0")
expect "E2 bank 3/4, right column: rank 2 (Glide 19)" "fx34 0 0 r2 19:0 r3 0:0 n 1" "$(line "$out" fx34 1)"
out=$(run e_enc "$P; fx34on 1; setfx 0 0 3 31 50; tap Vol; tap Vol; enc 5; fx34 0 0; step 0 0")
expect "E3 bank 3/4: the encoder changes the value of rank 3" "fx34 0 0 r2 0:0 r3 31:55 n 1|step 0 0 note 48 instr 48 fx0 0:0 fx1 0:0" "$(line "$out" fx34 1)|$(line "$out" step 1)"
out=$(run e_del "$P; fx34on 1; setstep 0 0 48 48 18 70 19 20; setfx 0 0 3 31 50; setfx 0 0 2 31 60; tap Vol; tap Vol; tap Delete; fx34 0 0; step 0 0")
expect "E4 Delete in bank 3/4 clears the cursor column's rank only (rank 3), not the stored slots nor rank 2" "fx34 0 0 r2 31:60 r3 0:0 n 1|step 0 0 note 48 instr 48 fx0 18:70 fx1 19:20" "$(line "$out" fx34 1)|$(line "$out" step 1)"
out=$(run e_del2 "$P; fx34on 1; setstep 0 0 48 48 18 70 19 20; setfx 0 0 3 31 50; setfx 0 0 2 31 60; tap Fx; tap Fx; tap Delete; fx34 0 0; step 0 0")
expect "E5 the right column of bank 3/4 clears rank 2" "fx34 0 0 r2 0:0 r3 31:50 n 1" "$(line "$out" fx34 1)"
out=$(run e_off "$P; setstep 0 0 48 48 18 70 19 20; fx34on 1; setfx 0 0 3 31 50; fx34on 0; tap Vol; tap Vol; tap Delete; fx34 0 0; step 0 0; fx34on 1; fx34 0 0")
expect "E6 switch off: Delete clears the stored slot fx[1] and touches no extra FX (the set is cleared by turning the switch on again)" "step 0 0 note 48 instr 48 fx0 18:70 fx1 0:20|fx34 0 0 r2 0:0 r3 0:0 n 0" "$(line "$out" step 1)|$(line "$out" fx34 2)"

echo "-- O: edit operations carry the extra FX"
out=$(run o_ins "$P; fx34on 1; setfx 0 0 3 31 50; setfx 0 1 2 18 77; tap Insert; fx34 0 0; fx34 0 1; fx34 0 2")
expect "O1 Insert: the extra FX move down with the steps" "fx34 0 0 r2 0:0 r3 0:0 n 2|fx34 0 1 r2 0:0 r3 31:50 n 2|fx34 0 2 r2 18:77 r3 0:0 n 2" "$(line "$out" fx34 1)|$(line "$out" fx34 2)|$(line "$out" fx34 3)"
out=$(run o_bsp "$P; fx34on 1; setfx 0 0 3 31 50; setfx 0 2 2 18 77; tap Down; tap Down; hold Shift; tap Delete; unhold Shift; fx34 0 0; fx34 0 1; fx34 0 2")
expect "O2 Backspace (Shift + Delete): the extra FX move up, the step before the cursor goes" "fx34 0 0 r2 0:0 r3 31:50 n 2|fx34 0 1 r2 18:77 r3 0:0 n 2|fx34 0 2 r2 0:0 r3 0:0 n 2" "$(line "$out" fx34 1)|$(line "$out" fx34 2)|$(line "$out" fx34 3)"
# copy step 0, paste on step 1: Copy, Down, Shift + Copy. Copy with FX buttons held pastes that column only
out=$(run o_paste "$P; fx34on 1; setfx 0 0 3 31 50; setfx 0 0 2 19 9; tap Copy; tap Down; hold Shift; tap Copy; unhold Shift; fx34 0 1")
expect "O3 Copy and paste of a step carry both extra FX" "fx34 0 1 r2 19:9 r3 31:50 n 4" "$(line "$out" fx34 1)"
out=$(run o_paste1 "$P; fx34on 1; setfx 0 0 3 31 50; setfx 0 0 2 19 9; tap Vol; tap Vol; tap Copy; tap Down; hold Vol; hold Shift; tap Copy; unhold Shift; unhold Vol; fx34 0 1")
expect "O4 paste with the left button held in bank 3/4 pastes rank 3 only" "fx34 0 1 r2 0:0 r3 31:50 n 3" "$(line "$out" fx34 1)"
out=$(run o_dup "$P; patlen 16; fx34on 1; setfx 0 0 3 31 50; setfx 0 1 2 19 40; tap F8; tap F3; lens; fx34 0 16; fx34 0 17")
expect "O5 Duplicate pattern: the extra FX double with the steps" "fx34 0 16 r2 0:0 r3 31:50 n 4|fx34 0 17 r2 19:40 r3 0:0 n 4" "$(line "$out" fx34 1)|$(line "$out" fx34 2)"
out=$(run o_exp "$P; patlen 16; fx34on 1; setfx 0 1 2 19 40; tap F8; tap F4; fx34 0 2; fx34 0 1")
expect "O6 Expand pattern: step 1 goes to step 2" "fx34 0 2 r2 19:40 r3 0:0 n 1|fx34 0 1 r2 0:0 r3 0:0 n 1" "$(line "$out" fx34 1)|$(line "$out" fx34 2)"
out=$(run o_shr "$P; patlen 16; fx34on 1; setfx 0 2 3 31 50; setfx 0 3 2 19 40; tap F8; tap F5; fx34 0 1; fx34 0 2")
expect "O7 Shrink pattern: step 2 goes to step 1, the odd step 3 goes with its step" "fx34 0 1 r2 0:0 r3 31:50 n 1|fx34 0 2 r2 0:0 r3 0:0 n 1" "$(line "$out" fx34 1)|$(line "$out" fx34 2)"
out=$(run o_inv "$P; patlen 16; fx34on 1; setfx 0 0 3 31 50; setfx 0 2 2 19 40; hold Shift; tap Down; tap Down; unhold Shift; tap F8; tap F2; fx34 0 0; fx34 0 2")
expect "O8 Invert (steps 0..2 reversed): the extra FX swap with their steps" "fx34 0 0 r2 19:40 r3 0:0 n 2|fx34 0 2 r2 0:0 r3 31:50 n 2" "$(line "$out" fx34 1)|$(line "$out" fx34 2)"

echo "-- C: the cap of 256 extra FX a pattern"
big="fx34on 1"; for s in $(seq 1 127); do big="$big; setfx 0 $s 2 19 1; setfx 0 $s 3 19 2"; done
out=$(run c_full "$P; $big; setfx 1 0 2 19 1; setfx 1 0 3 19 2; setfx 2 0 2 19 9; setfx 2 0 3 19 9; fx34 0 5")
expect "C1 the 256th entry is stored, the next one is refused" "256" "$(grep -c '^setfx ok' <<<"$out")"
expect "C2 the refused put said so (setfx full)" "2" "$(grep -c '^setfx full' <<<"$out")"
out=$(run c_ui "$P; $big; setfx 1 0 2 19 1; setfx 1 0 3 19 2; tap Vol; tap Vol; hold Vol; pad 5; unhold Vol; wait 300; fx34 0 0; $(shot fx34-cap-popup)")
expect "C3 a pad edit that does not fit stores nothing (rank 3 of step 0 stays empty, n stays 256)" "fx34 0 0 r2 0:0 r3 0:0 n 256" "$(line "$out" fx34 1)"
out=$(run c_dup "$P; patlen 16; fx34on 1; $(for t in 0 1 2 3 4 5 6; do for s in $(seq 0 15); do printf 'setfx %s %s 2 19 1; setfx %s %s 3 19 1; ' $t $s $t $s; done; done) tap F8; tap F3; lens")
expect "C4 Duplicate pattern that would overflow the set is refused whole (pattern stays 16 steps)" "pattern 16" "$(grep '^lens' <<<"$out" | grep -o 'pattern [0-9]*')"

echo "-- V: V and m marks, title bar text, columns option"
V="instr 48; setstep 0 0 48 48 0 0 0 0; setstep 0 1 50 48 0 0 0 0; setmm 0 0 30; tap Rec; tap Note; tap Fx; pad 24; tap Up; tap Note; tap Note"
out=$(run v_title "$V; ltitle; tap Down; ltitle; tap Up; ltitle; $(shot fx34-marks)")
expect "V1 the title bar names the cursor step's volume and micro-move" "ltitle Pattern 1  V51 m30" "$(line "$out" ltitle 1)"
expect "V2 a step with neither shows the plain title" "ltitle Pattern 1" "$(line "$out" ltitle 2)"
out=$(run v_cols "$V; vmcols 1; wait 200; $(shot fx34-vm-columns)")
expect "V3 (screenshot only) the V / m columns layout renders, 3 tracks" "" "$(grep -c FAIL <<<"$out" | sed 's/^0$//')"
out=$(run v_bank "$V; fx34on 1; setfx 0 0 3 31 50; setfx 0 0 2 19 40; setfx 0 1 3 14 3; tap Fx; tap Fx; $(shot fx34-bank34); title")
expect "V4 bank 3/4 title names the slot" "title FX4: G - Glide" "$(line "$out" title 1)"

echo "-- D: the manual V/m FX to fields action"
out=$(run d_move "instr 48; setstep 0 0 48 48 18 80 2 12; setstep 0 1 -1 0 18 60 0 0; setstep 0 2 52 48 18 40 16 5; setstep 0 3 53 48 2 9 0 0; tap Rec; tap F8; tap F8; tap F6; wait 200; step 0 0; stepv 0 0; step 0 1; stepv 0 1; step 0 2; stepv 0 2; step 0 3; stepv 0 3; $(shot fx34-move-vm)")
expect "D1 a note step: V and m FX go to the fields, the FX are cleared" "step 0 0 note 48 instr 48 fx0 0:0 fx1 0:0|stepv 0 0 vol 80 move 12" "$(line "$out" step 1)|$(line "$out" stepv 1)"
expect "D2 a step without a note keeps its V FX (a held modulation, not a velocity)" "step 0 1 note -1 instr 0 fx0 18:60 fx1 0:0|stepv 0 1 vol -1 move -1" "$(line "$out" step 2)|$(line "$out" stepv 2)"
expect "D3 a V FX next to a Random value FX is left alone (it would lose its target)" "step 0 2 note 52 instr 48 fx0 18:40 fx1 16:5|stepv 0 2 vol -1 move -1" "$(line "$out" step 3)|$(line "$out" stepv 3)"
expect "D4 an m FX alone: moved" "step 0 3 note 53 instr 48 fx0 0:0 fx1 0:0|stepv 0 3 vol -1 move 9" "$(line "$out" step 4)|$(line "$out" stepv 4)"

echo "-- G: goldens (goldens/fx34, byte equal)"
mkdir -p "$GOLD"
for g in fx34-marks fx34-vm-columns fx34-bank34 fx34-cap-popup fx34-move-vm; do
    [ -n "$PNG_DIR" ] && cp "$SHOTS/$g.png" "$PNG_DIR/" 2>/dev/null
    if [ "${UPDATE:-}" = 1 ]; then cp "$SHOTS/$g.png" "$GOLD/$g.png"; echo "wrote $g.png"; continue; fi
    checks=$((checks+1))
    if [ -f "$SHOTS/$g.png" ] && cmp -s "$SHOTS/$g.png" "$GOLD/$g.png"; then echo "ok    G $g.png is byte equal to the golden"
    else fails=$((fails+1)); echo "FAIL  G $g.png differs from goldens/fx34/$g.png"; fi
done

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
