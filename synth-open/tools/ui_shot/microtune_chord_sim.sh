#!/bin/bash
# microtune_chord_sim.sh [EVIDENCE_DIR]
#
# Microtuning phase 3, commit 4: Chord and Arp intervals follow the tuning (core/source/mtSequencer.cpp chordOffsets and getNextArpNote, mtTuning.h chordStep). A chord
# interval of i semitones lands on the key round(i * N / 12) above the step note. The tuning is set through the real UI (Project Settings > Tuning), the chord and the Arp are
# FX of a step on MIDI instrument 48 (channel 1), "midilog 1" prints every note sent to USB MIDI out (a step note N is sent as N + 24). Offsets below are relative to the step note.
#   H1  31 EDO: major triad (0 4 7) plays 0 10 18, the 0 7 14 chord plays 0 18 36. 17 EDO: 0 6 10. 7 EDO: 0 2 4
#   H2  12 EDO (default tuning) is untouched: 0 4 7. A Scala file of 7 notes remaps as 7 EDO does (0 2 4)
#   H3  fewer than 12 notes: two semitones on one key play once (5 EDO chord 0 6 7 is two notes, 0 3)
#   H4  the Arp: 31 EDO major triad arp up walks 0 10 18 and nothing else, 12 EDO walks 0 4 7
#   H6  the Arp dedupes like a chord: 5 EDO chord 0 6 7 (keys 0 3 3) walks up 0 3 0 3 0 and down 3 0 3 0 3, 7 EDO (keys 0 4 4) up 0 4 0 4 0 and down 4 0 4 0 4. A repeat
#       of one key in a row is a failure, on the unfixed code the walk is 0 3 3 0 3. The 5 and 7 EDO triad 0 4 7 has no collision and is unchanged (0 2 3, 0 2 4)
#   H7  the Arp with 12 notes or more is byte for byte as before: no tuning walks 0 6 7 0 6 up and 7 6 0 7 6 down, 31 EDO 0 16 18 0 16 and 18 16 0 18 16
#   H5  note off: the chord ends (no note left sounding after Stop in 31 EDO, every note on has its note off)
# Host only. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
NAME=microtune_chord_sim
. "$(cd "$(dirname "$0")" && pwd)/microtune_p3_lib.sh"
EVID="${1:-}"; [ -n "$EVID" ] && mkdir -p "$EVID"
TUN="$ROOT/releases/tunings"
# run X STEPS WAITMS: the MIDI events "on N" / "off N" (rebased: N - 24 - 60) in order, after UI commands X on the Project Settings page
run() {
    local sd="$WORK/c-$RANDOM"; rm -rf "$sd"; mkdir -p "$sd/Tunings"; cp "$TUN/just-major-5limit.scl" "$sd/Tunings/"
    "$BIN" "$sd" "$GO $1 tap Pattern; wait 300; instr 48; $2 audio 1; midilog 1; tap Play; wait $3; tap Play; wait 300; midilog 0; tune" 2>>"$WORK/c.err" \
        | grep -E '^(midi on|midi off)' | awk '{printf "%s%s %d", (n++ ? "," : ""), ($2 == "on" ? "on" : "off"), $3 - 84} END {print ""}'; }
# the sorted unique note-on offsets of a run
ons() { echo "$1" | tr ',' '\n' | awk '$1 == "on" {print $2}' | sort -n | uniq | tr '\n' ' ' | sed 's/ $//'; }
chord() {   # LABEL X CHORD WANT (offsets)
    local out; out=$(run "$2" "setstep 0 0 60 48 14 $3 0 0;" 900); local got; got=$(ons "$out")
    if [ "$got" = "$4" ]; then check "$1: chord $3 plays $4" ok; else check "$1" "chord $3 want '$4' got '$got' ($out)"; fi; }
T31="$(ed 0 19)"; T17="$(ed 0 5)"; T7="$(ed 0 -5)"; T5="$(ed 0 -7)"
chord "H1 31 EDO major triad" "$T31" 5 "0 10 18"
chord "H1 31 EDO 0 7 14 (chord 12)" "$T31" 12 "0 18 36"
chord "H1 17 EDO major triad" "$T17" 5 "0 6 10"
chord "H1 7 EDO major triad" "$T7" 5 "0 2 4"
chord "H2 12 EDO default tuning is untouched" "" 5 "0 4 7"
chord "H2 a 7 note Scala file remaps like 7 EDO" "$(ed 12 1)" 5 "0 2 4"
chord "H3 5 EDO chord 0 6 7: two semitones on one key play once" "$T5" 10 "0 3"
# H4 the arp: chord in fx0, arp up in fx1 (value 3 is what scale_snap_sim.sh uses)
arp() {   # LABEL X WANT
    local out got; out=$(run "$2" "setstep 0 0 60 48 14 5 21 3;" 2200); got=$(ons "$out")
    if [ "$got" = "$3" ]; then check "$1: arp notes $3" ok; else check "$1" "want '$3' got '$got' ($out)"; fi; }
arp "H4 31 EDO arp up" "$T31" "0 10 18"
arp "H4 12 EDO arp up" "" "0 4 7"
# H6 and H7 the ordered walk: the first 5 note ons of chord CHORD in fx0 and the Arp (VALUE 3 up, 14 down) in fx1
arpseq() {   # LABEL X CHORD VALUE WANT
    local out got; out=$(run "$2" "setstep 0 0 60 48 14 $3 21 $4;" 2200); got=$(echo "$out" | tr ',' '\n' | awk '$1 == "on" && n < 5 {printf "%s%s", (n++ ? " " : ""), $2}')
    if [ "$got" = "$5" ]; then check "$1: chord $3 arp walks $5" ok; else check "$1" "want '$5' got '$got' ($out)"; fi; }
arpseq "H6 5 EDO arp up, 0 6 7 plays the repeated key once" "$T5" 10 3 "0 3 0 3 0"
arpseq "H6 5 EDO arp down" "$T5" 10 14 "3 0 3 0 3"
arpseq "H6 7 EDO arp up" "$T7" 10 3 "0 4 0 4 0"
arpseq "H6 7 EDO arp down" "$T7" 10 14 "4 0 4 0 4"
arpseq "H6 5 EDO triad 0 4 7 has no collision" "$T5" 5 3 "0 2 3 0 2"
arpseq "H6 7 EDO triad 0 4 7 has no collision" "$T7" 5 3 "0 2 4 0 2"
arpseq "H7 no tuning arp up is untouched" "" 10 3 "0 6 7 0 6"
arpseq "H7 no tuning arp down is untouched" "" 10 14 "7 6 0 7 6"
arpseq "H7 31 EDO arp up is untouched" "$T31" 10 3 "0 16 18 0 16"
arpseq "H7 31 EDO arp down is untouched" "$T31" 10 14 "18 16 0 18 16"
# H5 every note on has its note off
out=$(run "$T31" "setstep 0 0 60 48 14 5 0 0;" 900)
on=$(echo "$out" | tr ',' '\n' | grep -c '^on'); off=$(echo "$out" | tr ',' '\n' | grep -c '^off')
offs=$(echo "$out" | tr ',' '\n' | awk '$1 == "off" {print $2}' | sort -n | uniq | tr '\n' ' ' | sed 's/ $//')
if [ "$on" -ge 3 ] && [ "$offs" = "0 10 18" ]; then check "H5 31 EDO chord: $on notes on, note off for 0 10 18 ($off offs)" ok; else check "H5 31 EDO chord off" "ons $on offs '$offs' ($out)"; fi
finish
