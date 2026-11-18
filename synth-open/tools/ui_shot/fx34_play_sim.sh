#!/bin/bash
# fx34_play_sim.sh [PNG_DIR]
#
# Playback of FX3 and FX4 (ranks 2 and 3, the "4 FX" project switch, core/source/fx34.h) on the real sequencer and player code of this tree
# (ui_shot) on the Mac. Host only, no device. Every scenario has its own SD card folder, the sequencer runs on the firmware's audio callback
# (audio 1) and the players are read back with "poke fx34play" (host_poke_fx34.cpp) or the MIDI log. Slots are ranks: rank 0 is the right FX
# column of bank 1/2 (FX2), 1 the left one (FX1), 2 the right of bank 3/4 (FX4), 3 the left (FX3). setstep writes ranks 0 and 1 (the pattern
# file), setfx writes ranks 2 and 3 (the sidecar set). FX ids: Panning 31, Delay send 30, Chance 4, Chance FX 48, Random value 16, MIDI Chord 14
# (value 24 = maj7), Strum 47, Arp 21, Patch 46, Morph 49, Off 1, CC A 8, Volume 18.
#   A  ranks 2 and 3 play on a sample track, and not with the switch off (a populated set is ignored)
#   B  D4 priority FX2 > FX1 > FX4 > FX3 on one parameter: all 6 pairs both ways, the hand over when the stronger FX ends, late and early arrivals
#   C  the pairs: Chance FX gates the other slot of its own pair only, Random value, Strum + Chord, Arp + Chord, Morph + Patch, Chance whole step
#   D  MIDI track CC, synth part sources, Performance mode custom FX (D8, only with the switch)
#   E  the switch off: the same steps with the set populated play exactly the 2 slot step
# SAN=address,undefined: the sanitizer build. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="$BUILD/ui_shot"
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/fx34_play_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

PAN=31; DLY=30; CHANCE=4; COND=48; RV=16; CHORD=14; STRUM=47; ARP=21; PATCH=46; MORPH=49; OFF=1; CCA=8
fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
expect() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected: $2
      got:      $3"; fi; }
# run NAME "script": the poke lines and the stdout lines of the script that read back (midi, step, fx34, player ...)
run() {
    local sd="$WORK/sd-$1"; rm -rf "$sd"; mkdir -p "$sd"
    rm -f "$WORK/poke" "$WORK/poke.out"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 UISHOT_POKE="$WORK/poke" "$BIN" "$sd" "wait 500; $2" >"$WORK/$1.out" 2>"$WORK/$1.err" ||
        { echo "ui_shot failed ($1):"; tail -5 "$WORK/$1.err"; }
    if grep -qE 'runtime error|AddressSanitizer' "$WORK/$1.err" 2>/dev/null; then fail "sanitizer report in $1" "$(grep -E 'runtime error|AddressSanitizer' "$WORK/$1.err" | head -2)"; fi
    [ -f "$WORK/poke.out" ] && cat "$WORK/poke.out"
    grep -E '^(midi|step|fx34|setfx|rawvals) ' "$WORK/$1.out"
}
SAMPLE="sampletone 0; instr 0; audio 1"
# P: the player line of track 0 a moment after the step starts
play1() { printf 'tap Play; wait 60; poke fx34play player 0; tap Play'; }
fxs()  { sed -n 's/^player 0 fx \([0-9,]*\) val \([0-9,]*\) mask pan \([0-9]*\) .* | pan \([-0-9]*\) .*/\1 \2 m\3 pan\4/p' <<<"$1" | tail -1; }

echo "-- A: ranks 2 and 3 play on a sample track"
out=$(run a1 "$SAMPLE; fx34on 1; setstep 0 0 60 0 0 0 0 0; setfx 0 0 2 $PAN 77 >/dev/null; $(play1)")
expect "A1 FX4 (rank 2) alone: Panning 77 held by rank 2, the player plays 77" "0,0,31,0 0,0,77,0 m4 pan77" "$(fxs "$out")"
out=$(run a2 "$SAMPLE; fx34on 1; setstep 0 0 60 0 0 0 0 0; setfx 0 0 3 $PAN 33; $(play1)")
expect "A2 FX3 (rank 3) alone: Panning 33 held by rank 3, the player plays 33" "0,0,0,31 0,0,0,33 m8 pan33" "$(fxs "$out")"
out=$(run a3 "$SAMPLE; fx34on 0; setstep 0 0 60 0 0 0 0 0; setfx 0 0 3 $PAN 33; setfx 0 0 2 $PAN 44; $(play1)")
expect "A3 switch off: the set is ignored, nothing is held and the pan is untouched" "0,0,0,0 0,0,0,0 m0 pan0" "$(fxs "$out")"
out=$(run a4 "$SAMPLE; fx34on 0; setstep 0 0 60 0 $PAN 20 $PAN 90; setfx 0 0 3 $PAN 33; $(play1)")
expect "A4 switch off, FX in ranks 0 and 1: as before, rank 0 wins (20), the set stays out" "31,31,0,0 20,90,0,0 m3 pan20" "$(fxs "$out")"
out=$(run a5 "$SAMPLE; setstep 0 0 60 0 0 0 0 0; setfx 0 0 3 $PAN 33; fx34on 1; $(play1)")
expect "A5 switch turned on after the set was written is also ignored (a switch on clears the sets)" "0,0,0,0 0,0,0,0 m0 pan0" "$(fxs "$out")"

echo "-- B: priority FX2 > FX1 > FX4 > FX3 (rank 0 > 1 > 2 > 3)"
# set RANK TYPE VALUE as a script piece: ranks 0 and 1 through setstep (built per scenario), ranks 2 and 3 through setfx
mk() {   # mk v0 v1 v2 v3: the step 0 (Panning in each rank that has a value, - for none)
    local s="setstep 0 0 60 0 $( [ "$1" != - ] && echo "$PAN $1" || echo "0 0") $( [ "$2" != - ] && echo "$PAN $2" || echo "0 0")"
    [ "$3" != - ] && s="$s; setfx 0 0 2 $PAN $3"
    [ "$4" != - ] && s="$s; setfx 0 0 3 $PAN $4"
    printf '%s' "$s"
}
vals=(0 1 2 3)
for hi in 0 1 2; do
  for lo in $(seq $((hi+1)) 3); do
    for flip in 0 1; do
      v=(- - - -); wantv=80; v[$hi]=80; v[$lo]=20
      if [ $flip = 1 ]; then v[$hi]=20; v[$lo]=80; wantv=20; fi
      out=$(run "b_${hi}${lo}${flip}" "$SAMPLE; fx34on 1; $(mk ${v[0]} ${v[1]} ${v[2]} ${v[3]}); $(play1)")
      got=$(fxs "$out"); got=${got##* pan}
      expect "B rank $hi against rank $lo ($([ $flip = 0 ] && echo "$hi=80 $lo=20" || echo "$hi=20 $lo=80")): rank $hi plays $wantv" "$wantv" "$got"
    done
  done
done
# hand over: ranks 0..3 hold 10 30 50 70, Off ends rank 0, then 1, 2, 3. The steps start at 0 / 115.3 / 230.6 / 345.9 / 461.2 ms
# (tap Play takes 80 ms, so the first poll lands 25 ms into step 1, the next ones 25 ms into steps 2, 3 ...)
CH="setstep 0 1 60 0 $PAN 10 $PAN 30; setfx 0 1 2 $PAN 50; setfx 0 1 3 $PAN 70; setstep 0 2 -1 0 $OFF 0 0 0; setstep 0 3 -1 0 0 0 $OFF 0; setfx 0 4 2 $OFF 0; setfx 0 5 3 $OFF 0"
poll() { printf 'wait 115; poke fx34play player 0'; }
out=$(run b_chain "$SAMPLE; fx34on 1; $CH; tap Play; wait 60; poke fx34play player 0; $(poll); $(poll); $(poll); $(poll); tap Play")
got=$(grep '^player' <<<"$out" | while read -r l; do v=$(fxs "$l"); echo "${v##* pan}"; done | paste -sd' ' -)
expect "B hand over: 10 (rank 0), Off rank 0 -> 30, Off rank 1 -> 50, Off rank 2 -> 70, Off rank 3 -> no holder" "10 30 50 70 70" "$got"
got=$(grep '^player' <<<"$out" | tail -1 | sed 's/ mask.*//')
expect "B hand over: after the last Off no rank holds a Panning FX" "player 0 fx 0,0,0,0 val 0,0,0,0" "$got"
# a weaker FX that arrives later does not take over, a stronger one does and gives back
LATE="setstep 0 0 60 0 0 0 0 0; setfx 0 0 3 $PAN 90; setstep 0 1 -1 0 $PAN 10 0 0; setstep 0 2 -1 0 $OFF 0 0 0"
out=$(run b_late "$SAMPLE; fx34on 1; $LATE; tap Play; wait 60; poke fx34play player 0; $(poll); $(poll); tap Play")
got=$(grep '^player' <<<"$out" | while read -r l; do v=$(fxs "$l"); echo "${v##* pan}"; done | paste -sd' ' -)
expect "B late: rank 3 holds 90, rank 0 arrives with 10 and wins (10), its Off gives 90 back, nothing changes after" "10 90 90" "$got"
LATE2="setstep 0 0 60 0 $PAN 10 0 0; setstep 0 1 -1 0 0 0 0 0; setfx 0 1 3 $PAN 90"
out=$(run b_late2 "$SAMPLE; fx34on 1; $LATE2; tap Play; wait 60; poke fx34play player 0; $(poll); tap Play")
got=$(grep '^player' <<<"$out" | while read -r l; do v=$(fxs "$l"); echo "${v##* pan}"; done | paste -sd' ' -)
expect "B late: rank 0 holds 10, rank 3 arrives with 90 and does not take over" "10 10" "$got"
# two different parameters in different ranks do not touch each other
out=$(run b_two "$SAMPLE; fx34on 1; setstep 0 0 60 0 0 0 0 0; setfx 0 0 2 $PAN 61; setfx 0 0 3 $DLY 44; $(play1)")
expect "B two parameters: Panning in rank 2 and Delay send in rank 3 both play" "pan 61 dly 44" "$(sed -n 's/.*dly \([0-9]*\) vol.*| pan \([-0-9]*\) dly \([0-9]*\) .*/pan \2 dly \3/p' <<<"$out" | tail -1)"

echo "-- C: the pairs (ranks 0 and 1, ranks 2 and 3) and the whole step Chance"
# Chance FX (48) with value 0 never passes: it removes the Panning FX of the other slot of ITS pair, no other
gate() {   # gate NAME COND_RANK PAN_RANK : the hold mask of the Panning after one step
    local c=$2 pn=$3 sc="" 
    [ "$c" -ge 2 ] && sc="setfx 0 0 $c $COND 0;" || sc="CS$c"
    local v0="0 0" v1="0 0"
    [ "$c" = 0 ] && v0="$COND 0"; [ "$c" = 1 ] && v1="$COND 0"
    [ "$pn" = 0 ] && v0="$PAN 55"; [ "$pn" = 1 ] && v1="$PAN 55"
    local pre="setstep 0 0 60 0 $v0 $v1;"
    [ "$c" -ge 2 ] && pre="$pre setfx 0 0 $c $COND 0;"
    [ "$pn" -ge 2 ] && pre="$pre setfx 0 0 $pn $PAN 55;"
    local out; out=$(run "$1" "$SAMPLE; fx34on 1; $pre $(play1)")
    sed -n 's/.* mask pan \([0-9]*\) .*/\1/p' <<<"$out" | tail -1
}
expect "C1 Chance FX (0 %) in rank 1 gates the Panning in rank 0 (its pair, as before)" "0" "$(gate c1 1 0)"
expect "C2 Chance FX (0 %) in rank 3 gates the Panning in rank 2 (its pair)" "0" "$(gate c2 3 2)"
expect "C3 Chance FX (0 %) in rank 2 gates the Panning in rank 3 (its pair)" "0" "$(gate c3 2 3)"
expect "C4 Chance FX (0 %) in rank 3 does NOT gate the Panning in rank 1 (the other pair)" "2" "$(gate c4 3 1)"
expect "C5 Chance FX (0 %) in rank 3 does NOT gate the Panning in rank 0 (the other pair)" "1" "$(gate c5 3 0)"
expect "C6 Chance FX (0 %) in rank 1 does NOT gate the Panning in rank 2 (the other pair)" "4" "$(gate c6 1 2)"
expect "C7 Chance FX (0 %) in rank 0 does NOT gate the Panning in rank 3 (the other pair)" "8" "$(gate c7 0 3)"
# Random value (16): randomises the other slot of its own pair
rvl() {   # rvl NAME SEED RV_RANK PAN_RANK : the last value of the Panning rank
    local rk=$3 pn=$4 v0="0 0" v1="0 0" pre
    [ "$rk" = 0 ] && v0="$RV 50"; [ "$rk" = 1 ] && v1="$RV 50"
    [ "$pn" = 0 ] && v0="$PAN 50"; [ "$pn" = 1 ] && v1="$PAN 50"
    pre="setstep 0 0 60 0 $v0 $v1;"
    [ "$rk" -ge 2 ] && pre="$pre setfx 0 0 $rk $RV 50;"
    [ "$pn" -ge 2 ] && pre="$pre setfx 0 0 $pn $PAN 50;"
    local out; out=$(run "$1" "$SAMPLE; fx34on 1; seed $2; $pre $(play1)")
    sed -n 's/^player 0 fx [0-9,]* val \([0-9]*\),\([0-9]*\),\([0-9]*\),\([0-9]*\) .*/\1 \2 \3 \4/p' <<<"$out" | tail -1 | awk -v r="$pn" '{print $(r+1)}'
}
r_in=$(rvl d1 3 3 2); r_cross=$(rvl d2 3 3 1); r_in0=$(rvl d3 3 1 0); r_cross0=$(rvl d4 3 1 2)
[ "$r_in" != 50 ] && pass "C8 Random value in rank 3 randomises the Panning of rank 2 ($r_in, not 50)" || fail "C8 Random value in rank 3 randomises rank 2" "got $r_in"
expect "C9 Random value in rank 3 leaves the Panning of rank 1 alone (the other pair)" "50" "$r_cross"
[ "$r_in0" != 50 ] && pass "C10 Random value in rank 1 randomises the Panning of rank 0 (as before, $r_in0)" || fail "C10 rank 1 randomises rank 0" "got $r_in0"
expect "C11 Random value in rank 1 leaves the Panning of rank 2 alone (the other pair)" "50" "$r_cross0"
# Chance (4) is the whole step, any of the 4 slots
notes() { grep -c '^midi on' <<<"$1"; }
M="instr 48; audio 1; midilog 1"
out=$(run ch0 "$M; fx34on 1; setstep 0 0 48 48 0 0 0 0; setfx 0 0 3 $CHANCE 0; tap Play; wait 300; tap Play; wait 100; midilog 0")
expect "C12 Chance 0 % in rank 3 drops the whole step (no note)" "0" "$(notes "$out")"
out=$(run ch1 "$M; fx34on 1; setstep 0 0 48 48 0 0 0 0; setfx 0 0 3 $CHANCE 100; tap Play; wait 300; tap Play; wait 100; midilog 0")
expect "C13 Chance 100 % in rank 3 plays the note" "1" "$(notes "$out")"
# Strum + MIDI Chord
# the note ons with the time relative to the first one (the start of the first step depends on the run)
mon() { grep '^midi on' <<<"$1" | awk '{ if (!n++) t0 = $NF; $NF = sprintf("%.1f", $NF - t0); print }' | paste -sd'|' -; }
runm() { run "$1" "$M; $2; tap Play; wait 300; tap Play; wait 100; midilog 0"; }
ref=$(mon "$(runm s_ref "setstep 0 0 48 48 14 24 47 10")")
got=$(mon "$(runm s_pair "fx34on 1; setstep 0 0 48 48 0 0 0 0; setfx 0 0 3 $CHORD 24; setfx 0 0 2 $STRUM 10")")
expect "C14 Strum in rank 2 strums the MIDI Chord in rank 3 like ranks 0 and 1 do (20.3 ms a note)" "$ref" "$got"
case "$ref" in *"t 20.3"*) pass "C15 the reference really strums (notes 20.3 ms apart)" ;; *) fail "C15 reference strum" "$ref" ;; esac
plain=$(mon "$(runm s_plain "setstep 0 0 48 48 14 24 0 0")")
got=$(mon "$(runm s_cross "fx34on 1; setstep 0 0 48 48 0 0 $STRUM 10; setfx 0 0 3 $CHORD 24")")
expect "C16 Strum in rank 1 does not strum the MIDI Chord in rank 3 (the other pair): the chord plays plain" "$plain" "$got"
got=$(mon "$(runm s_cross2 "fx34on 1; setstep 0 0 48 48 $CHORD 24 0 0; setfx 0 0 3 $STRUM 10")")
expect "C17 Strum in rank 3 does not strum the MIDI Chord in rank 0 (the other pair)" "$plain" "$got"
# Arp + MIDI Chord (an Arp in the pair keeps the chord from playing as a chord)
runa() { run "$1" "$M; $2; tap Play; wait 600; tap Play; wait 100; midilog 0"; }
ref=$(mon "$(runa a_ref "setstep 0 0 49 48 14 5 21 3")")
got=$(mon "$(runa a_pair "fx34on 1; setstep 0 0 49 48 0 0 0 0; setfx 0 0 3 $CHORD 5; setfx 0 0 2 $ARP 3")")
expect "C18 Arp in rank 2 arpeggiates the MIDI Chord in rank 3 like ranks 0 and 1 do" "$ref" "$got"
plain=$(mon "$(runa a_plain "setstep 0 0 49 48 14 5 0 0")")
got=$(mon "$(runa a_cross "fx34on 1; setstep 0 0 49 48 $ARP 3 0 0; setfx 0 0 3 $CHORD 5")")
expect "C19 Arp in rank 0 does not touch the MIDI Chord in rank 3 (the other pair): the chord plays plain" "$plain" "$got"
# Morph + Patch (synth part 0, built in WTFM): slots 0 / 1 / 3 hold different values, see morph_sim.sh
SETUP="audio 1; instr 64; tap Params; wait 1500; poke slotfill 0 0 50; poke slotfill 0 1 20; poke slotfill 0 3 80; tap Pattern; wait 1200"
PLAYM="tap Play; wait 400; tap Play; wait 200; poke rawvals 0"
rawv() { grep '^rawvals' <<<"$1" | tail -1 | awk '{print $NF}'; }
mk4() {   # mk4 NAME "setstep f0t f0v f1t f1v" "setfx lines": a note on synth part 0 at step 0
    local r; r=$(run "$1" "$SETUP; fx34on 1; setstep 0 0 48 64 $2; $3; $PLAYM"); rawv "$r"
}
A=$(run m_a "$SETUP; poke rawvals 0" | grep '^rawvals' | tail -1 | awk '{print $NF}')
R1=$(mk4 m_r1 "$PATCH 1 0 0" "")
R3=$(mk4 m_r3 "$PATCH 3 0 0" "")
if [ -n "$A" ] && [ "$A" != "$R1" ] && [ "$A" != "$R3" ] && [ "$R1" != "$R3" ]; then pass "C20 the slots 0, 1 and 3 hold different values (reference rows)"; else fail "C20 reference rows" "A=$A R1=$R1 R3=$R3"; fi
expect "C21 Morph 100 in rank 2 next to the Patch FX 3 in rank 3 blends to slot 3 (the pair)" "$R3" "$(mk4 m_pair "0 0 0 0" "setfx 0 0 3 $PATCH 3; setfx 0 0 2 $MORPH 100")"
expect "C22 Morph 100 in rank 1 and the Patch FX 3 in rank 3 (other pair): the Patch plays and the Morph goes to its own target, slot 1" "$R1" "$(mk4 m_cross "0 0 $MORPH 100" "setfx 0 0 3 $PATCH 3")"
expect "C23 the same in the 2 slot step (Morph in rank 1, Patch FX 3 in rank 0 name the target): slot 3" "$R3" "$(mk4 m_ref "$PATCH 3 $MORPH 100" "")"

echo "-- D: MIDI track CC, synth part sources, Performance mode custom FX"
for rk in 2 3; do
  out=$(run "cc$rk" "$M; fx34on 1; setstep 0 0 48 48 0 0 0 0; setfx 0 0 $rk $CCA 77; tap Play; wait 300; tap Play; wait 100; midilog 0")
  expect "D$((rk-1)) CC A in rank $rk sends the CC (value 77) of the MIDI instrument" "1" "$(grep -c '^midi cc [0-9]* 77 ch 1' <<<"$out")"
done
out=$(run cc_off "$M; fx34on 0; setstep 0 0 48 48 0 0 0 0; setfx 0 0 3 $CCA 77; tap Play; wait 300; tap Play; wait 100; midilog 0")
expect "D3 the same with the switch off sends nothing" "0" "$(grep -c '^midi cc' <<<"$out")"
# synth part sources: Panning in each rank, the value of the part is the most significant one
syn() {   # syn NAME "setstep f0 f1" "setfx lines" -> "<value of the part> <sources>"
    local o; o=$(run "$1" "audio 1; instr 64; fx34on 1; setstep 0 0 60 64 $2; $3; tap Play; wait 80; poke fx34play synth 0; tap Play")
    sed -n 's/^synth 0 p0 \[\(.*\)\] = \([-0-9.]*\) p.*/\2 [\1]/p' <<<"$o" | tail -1
}
r=$(syn y1 "0 0 0 0" "setfx 0 0 2 $PAN 30; setfx 0 0 3 $PAN 80")
case "$r" in "-60.0 "*|"-40.0 "*|"-"*) : ;; esac
v23=${r%% *}
r1=$(syn y2 "0 0 0 0" "setfx 0 0 3 $PAN 80"); v3=${r1%% *}
r2=$(syn y3 "0 0 0 0" "setfx 0 0 2 $PAN 30"); v2=${r2%% *}
r3=$(syn y4 "0 0 $PAN 55" "setfx 0 0 2 $PAN 30; setfx 0 0 3 $PAN 80"); v1=${r3%% *}
r4=$(syn y5 "$PAN 10 $PAN 55" "setfx 0 0 2 $PAN 30; setfx 0 0 3 $PAN 80"); v0=${r4%% *}
if [ "$v2" != "$v3" ] && [ "$v23" = "$v2" ] && [ "$v1" != "$v2" ] && [ "$v0" != "$v1" ] && [ "$v0" != "$v2" ]; then
    pass "D4 synth part Panning: rank 3 alone $v3, rank 2 alone $v2, both = rank 2, with rank 1 = $v1, with rank 0 = $v0 (FX2 > FX1 > FX4 > FX3)"
else
    fail "D4 synth part Panning priority" "r3=$v3 r2=$v2 r2+r3=$v23 r1=$v1 r0=$v0"
fi
r=$(syn y6 "0 0 0 0" "setfx 0 0 3 $PAN 80")
expect "D5 the rank 3 value sits in its own source of the part (instrument, Fx1, Fx2, Performance, Fx3 = rank 2, Fx4 = rank 3)" "60.0 [0.0 - - - - 60.0]" "$r"
# Performance mode custom FX (D8): custom play mode 15 puts a Roll (id 3) on every step, in the first empty slot. Ranks 2 and 3 only with the switch
cf() {   # cf NAME SWITCH f0 f1 -> the 4 slots of the step that sounded
    local o; o=$(run "$1" "$SAMPLE; fx34on $2; setstep 0 1 60 0 $3 $4; poke fx34play custom 0; tap Play; wait 60; poke fx34play slots 0; tap Play")
    sed -n 's/^slots 0 //p' <<<"$o" | tail -1
}
expect "D6 custom FX, ranks 0 and 1 free: the Roll takes rank 0" "3:" "$(cf e1 1 "0 0" "0 0" | cut -c1-2)"
expect "D7 custom FX, rank 0 full: the Roll takes rank 1" "30:40 3:" "$(cf e2 1 "$DLY 40" "0 0" | cut -c1-8)"
expect "D8 custom FX, ranks 0 and 1 full, switch on: the Roll takes rank 2" "30:40 30:41 3:" "$(cf e3 1 "$DLY 40" "$DLY 41" | cut -c1-14)"
expect "D9 custom FX, ranks 0 and 1 full, switch off: no room, the Roll is not played" "30:40 30:41 0:0 0:0" "$(cf e4 0 "$DLY 40" "$DLY 41")"

echo "-- E: switch off, the 2 slot step"
out=$(run e_on "$M; fx34on 1; setstep 0 0 48 48 $CHORD 24 $STRUM 10; setfx 0 0 3 $CHANCE 0; tap Play; wait 300; tap Play; wait 100; midilog 0")
expect "E1 switch on: a Chance 0 % in rank 3 drops the step" "0" "$(notes "$out")"
out=$(run e_off "$M; fx34on 0; setstep 0 0 48 48 $CHORD 24 $STRUM 10; setfx 0 0 3 $CHANCE 0; tap Play; wait 300; tap Play; wait 100; midilog 0")
ref0=$(mon "$(runm e_ref "setstep 0 0 48 48 14 24 47 10")")
expect "E2 switch off: the same set is ignored, the chord strums as the 2 slot step" "$ref0" "$(mon "$out")"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
