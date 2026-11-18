#!/bin/bash
# chance_fx_sim.sh [PNG_DIR]
#
# The Chance FX (a condition that gates only the other FX of the step, the note still plays) and the long pass conditions of Chance
# and Chance FX (pass k of 8, pass k of 16) on the real sequencer and pattern editor code of this tree (ui_shot) on the Mac.
# Host only: no device. Every scenario has its own SD card folder. The pattern is 4 steps long (patlen 4, 461 ms a pass at the default
# tempo), step 0 plays a MIDI note with a MIDI Chord (maj7, 4 notes) in the other FX slot, and the MIDI notes that go out
# (midilog 1, audio 1) are counted per pass: 4 = the note and its chord, 1 = the note alone, 0 = no note (the whole step dropped).
# FX ids: Chance 4, MIDI Chord 14, Chance FX 48. Values: 0-100 probability, 101 GL, 102 !L, 103-138 play N / skip M, 139-174 skip N /
# play M, 175-182 pass 1..8 of 8 (182 is every 8th pass), 183-198 pass 1..16 of 16 (198 is every 16th pass).
# SAN=address,undefined: the sanitizer build (build/ui-shot-san), a report fails the run.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="$BUILD/ui_shot"
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/chance_fx_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

CH=4; CHORD=14; COND=48
PASS_MS=461.3   # 4 steps at the default tempo (115.3 ms a step, see the strum checks of chord_capture_sim.sh)

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() {   # run NAME "script": the lines of the script that read back (step, grid, title, cursor, midi)
    local sd="$WORK/$1"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; $2" 2>"$WORK/$1.err" | grep -E '^(step|cursor|title|grid|midi) '
}
shot() { [ -n "$PNG_DIR" ] && echo "shot $PNG_DIR/$1.png" || echo "wait 1"; }
# passes NAME N "setstep lines": the note-ons of each of the first N passes, "4 1 1 1 ..."
passes() {
    local out
    out=$(run "$1" "instr 48; patlen 4; $3; audio 1; midilog 1; tap Play; wait $(( $2 * 461 + 150 )); tap Play; wait 100; midilog 0")
    grep '^midi on' <<<"$out" | awk -v n="$2" -v p="$PASS_MS" '{ t = $NF; k = int((t + 20) / p); if (k < n) c[k]++ } END { for (i = 0; i < n; i++) printf "%s%d", (i ? " " : ""), c[i]; print "" }'
}
rep() { local out="" i; for ((i = 0; i < $2; i++)); do out="$out${out:+ }$1"; done; printf '%s' "$out"; }
# the pass numbers (1 based) in "4" positions of a count line, e.g. "1 9 17"
fired() { awk '{ for (i = 1; i <= NF; i++) if ($i >= 4) printf "%s%d", (n++ ? " " : ""), i; print "" }' <<<"$1"; }
N=20
STEP="setstep 0 0 48 48"

# 1. Chance FX gates the chord: the note plays every pass, the chord only when the condition holds
for k in 1 3 8; do
  got=$(passes "g8_$k" $N "$STEP $COND $((174 + k)) $CHORD 24")
  case $k in 1) want="1 9 17" ;; 3) want="3 11 19" ;; 8) want="8 16" ;; esac
  expect "Chance FX pass $k of 8: the chord on passes $want, the note alone on every other pass" "$want" "$(fired "$got")"
  [ "$k" = 1 ] && expect "Chance FX pass 1 of 8: no pass loses its note (4 on passes 1 9 17, 1 elsewhere)" "4 1 1 1 1 1 1 1 4 1 1 1 1 1 1 1 4 1 1 1" "$got"
done
for k in 3 16; do
  got=$(passes "g16_$k" $N "$STEP $COND $((182 + k)) $CHORD 24")
  case $k in 3) want="3 19" ;; 16) want="16" ;; esac
  expect "Chance FX pass $k of 16: the chord on passes $want" "$want" "$(fired "$got")"
done
got=$(passes g8slot1 $N "$STEP $CHORD 24 $COND 182")
expect "Chance FX in FX2, chord in FX1 (the slot order does not matter): every 8th pass" "8 16" "$(fired "$got")"

# 2. Chance (the old one) with the new values drops the whole step: no note at all on the other passes
got=$(passes c8 $N "$STEP $CH 175 $CHORD 24")
expect "Chance pass 1 of 8: passes 1 9 17 play note and chord, every other pass is silent" "4 0 0 0 0 0 0 0 4 0 0 0 0 0 0 0 4 0 0 0" "$got"
got=$(passes c16 $N "$STEP $CH 198 $CHORD 24")
expect "Chance pass 16 of 16: only pass 16 plays" "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 4 0 0 0 0" "$got"

# 3. The values that were there before are the same on both: probability, play/skip cycles, skip/play
expect "Chance FX 0 %: the chord never plays" "$(rep 1 8)" "$(passes p0 8 "$STEP $COND 0 $CHORD 24")"
expect "Chance FX 100 %: the chord always plays" "$(rep 4 8)" "$(passes p100 8 "$STEP $COND 100 $CHORD 24")"
expect "Chance 0 %: the step never plays" "$(rep 0 8)" "$(passes cp0 8 "$STEP $CH 0 $CHORD 24")"
expect "Chance FX play 1 skip 1 (103): chord, note, chord, note" "4 1 4 1 4 1 4 1" "$(passes ps11 8 "$STEP $COND 103 $CHORD 24")"
expect "Chance play 1 skip 1 (103): note and chord, nothing, ..." "4 0 4 0 4 0 4 0" "$(passes cps11 8 "$STEP $CH 103 $CHORD 24")"
expect "Chance FX skip 1 play 1 (139): note, chord, note, chord" "1 4 1 4 1 4 1 4" "$(passes sp11 8 "$STEP $COND 139 $CHORD 24")"
expect "Chance play 2 skip 3 (103 + 6 + 2 = 111): 2 on, 3 off" "4 4 0 0 0 4 4 0 0 0" "$(passes ps23 10 "$STEP $CH 111 $CHORD 24")"

# 4. Both slots: Chance next to Chance FX, Chance FX next to Chance FX, a step with no FX to gate
expect "Chance 1 of 8 next to Chance FX: the step plays on pass 1, 9, 17 and has nothing to gate" "1 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 1 0 0 0" "$(passes cc $N "$STEP $CH 175 $COND 182")"
expect "Chance FX next to Chance FX: nothing to gate, the note plays every pass" "$(rep 1 10)" "$(passes cond2 10 "$STEP $COND 175 $COND 182")"
expect "Chance FX alone (nothing to gate): the note plays every pass" "$(rep 1 10)" "$(passes condone 10 "$STEP $COND 175 0 0")"
expect "no Chance FX, a chord: the old step, note and chord every pass" "$(rep 4 4)" "$(passes plain 4 "$STEP $CHORD 24 0 0")"

# 5. Group with last and Not last follow the last condition of the track, Chance FX included
#    step 0 has the condition (0 or 100 %), step 2 (note 50, instrument 48) has its own chord gated by GL (101) or !L (102)
got=$(passes gl100 4 "$STEP $COND 100 $CHORD 24; setstep 0 2 50 48 $COND 101 $CHORD 24")
expect "GL after a Chance FX that fired: both steps chord (4 + 4 notes a pass)" "8 8 8 8" "$got"
got=$(passes gl0 4 "$STEP $COND 0 $CHORD 24; setstep 0 2 50 48 $COND 101 $CHORD 24")
expect "GL after a Chance FX that failed: both steps note alone (1 + 1)" "2 2 2 2" "$got"
got=$(passes nl0 4 "$STEP $COND 0 $CHORD 24; setstep 0 2 50 48 $COND 102 $CHORD 24")
expect "!L after a Chance FX that failed: the second step plays its chord (1 + 4)" "5 5 5 5" "$got"

# 6. The pass count restarts with the playback: Stop and Play again begin at pass 1
out=$(run restart "instr 48; patlen 4; $STEP $COND 175 $CHORD 24; audio 1; midilog 1; tap Play; wait 1000; tap Play; wait 200; step 0 0; tap Play; wait 1000; tap Play; wait 100; midilog 0")
runs=$(grep -n '^step' <<<"$out" | cut -d: -f1)
first=$(sed -n "1,$((runs - 1))p" <<<"$out" | grep -c '^midi on')
second=$(sed -n "$((runs + 1)),\$p" <<<"$out" | grep -c '^midi on')
# 1000 ms = pass 1 (note + chord) and pass 2 (note alone) + the start of pass 3: 4 + 1 + 1 = 6 notes, the same in both runs
expect "Stop and Play again: pass 1 (the chord) comes first again" "6 6" "$first $second"

# 7. The editor: grid cell, title bar and the encoder through the whole value range
out=$(run ui "instr 48; setstep 0 0 48 48 $COND 175 $CHORD 24; setstep 0 1 48 48 $COND 190 0 0; setstep 0 2 48 48 $CH 182 0 0; setstep 0 3 48 48 $COND 60 0 0; grid 0 0; grid 0 1; grid 0 2; grid 0 3")
expect "grid, Chance FX pass 1 of 8" "grid 0 0 fx0 [K1/8] fx1 [0^7 ]" "$(sed -n 1p <<<"$out")"
expect "grid, Chance FX pass 8 of 16" "grid 0 1 fx0 [KP08] fx1 [----]" "$(sed -n 2p <<<"$out")"
expect "grid, Chance pass 8 of 8" "grid 0 2 fx0 [C8/8] fx1 [----]" "$(sed -n 3p <<<"$out")"
expect "grid, Chance FX 60 %" "grid 0 3 fx0 [K 60] fx1 [----]" "$(sed -n 4p <<<"$out")"

# 8. Picking and editing it in the pattern editor: the FX pad (Fx button held), the encoder through the FX list, the value range
out=$(run pick "instr 48; setstep 0 0 48 48 0 0 0 0; tap Rec; tap Fx; hold Fx; pad 48; unhold Fx; wait 100; step 0 0; title")
expect "pad 48 with Fx held picks Chance FX (id 48, list place 48 since Morph of idea-morph sits after Patch), default 50 %" "step 0 0 note 48 instr 48 fx0 48:50 fx1 0:0" "$(grep '^step' <<<"$out")"
expect "the title bar names the FX when it is picked" "title K - Chance FX" "$(grep '^title' <<<"$out")"
out=$(run walk "instr 48; setstep 0 0 48 48 47 5 0 0; tap Rec; tap Fx; hold Fx; enc 1; unhold Fx; wait 100; step 0 0; hold Fx; enc 1; unhold Fx; hold Fx; enc 1; unhold Fx; step 0 0; hold Fx; enc -1; unhold Fx; hold Fx; enc -1; unhold Fx; step 0 0")
expect "encoder through the FX list: Strum, Patch, (Morph,) Chance FX, back (features-140: the FX id range of the editor is 0 to 51)" "step 0 0 note 48 instr 48 fx0 46:0 fx1 0:0|step 0 0 note 48 instr 48 fx0 48:50 fx1 0:0|step 0 0 note 48 instr 48 fx0 46:0 fx1 0:0" "$(grep '^step' <<<"$out" | paste -sd'|' -)"
out=$(run range "instr 48; setstep 0 0 48 48 48 174 14 24; tap Rec; tap Fx; title; enc 1; step 0 0; title; enc 1; title; enc -1; enc -1; step 0 0; title; setstep 0 0 48 48 48 197 14 24; enc 1; step 0 0; title; enc 1; step 0 0; title; $(shot chance-fx-title)")
expect "range: 174 is Skip 6 / Play 6 (the old end of the range)" "title Chance FX: Skip 6 / Play 6" "$(grep '^title' <<<"$out" | sed -n 1p)"
expect "range: +1 is pass 1 of 8 (175)" "step 0 0 note 48 instr 48 fx0 48:175 fx1 14:24|title Chance FX: Pass 1 of 8" "$(grep -E '^(step|title)' <<<"$out" | sed -n 2,3p | paste -sd'|' -)"
expect "range: +1 is pass 2 of 8, back twice is the old 174" "title Chance FX: Pass 2 of 8|step 0 0 note 48 instr 48 fx0 48:174 fx1 14:24|title Chance FX: Skip 6 / Play 6" "$(grep -E '^(step|title)' <<<"$out" | sed -n 4,6p | paste -sd'|' -)"
expect "range: 197 + 1 is pass 16 of 16 (198), + 1 stops there" "step 0 0 note 48 instr 48 fx0 48:198 fx1 14:24|title Chance FX: Pass 16 of 16|step 0 0 note 48 instr 48 fx0 48:198 fx1 14:24|title Chance FX: Pass 16 of 16" "$(grep -E '^(step|title)' <<<"$out" | sed -n 7,10p | paste -sd'|' -)"
# the old Chance reaches the new values the same way (the same value range)
out=$(run chrange "instr 48; setstep 0 0 48 48 4 174 0 0; tap Rec; tap Fx; enc 1; step 0 0; title")
expect "Chance: 174 + 1 is pass 1 of 8 as well" "step 0 0 note 48 instr 48 fx0 4:175 fx1 0:0|title Chance: Pass 1 of 8" "$(grep -E '^(step|title)' <<<"$out" | paste -sd'|' -)"

echo
new=$(cat "$WORK"/*.err 2>/dev/null | grep "SUMMARY" | grep -v "mtWavetableEnvelopes.cpp:206" | sort | uniq -c)
if [ -n "$new" ]; then fails=$((fails+1)); echo "FAIL  sanitizer report in a run:"; echo "$new"; fi
echo "$checks checks"
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fails failing)"; exit 1
