#!/bin/bash
# microtune_snap_sim.sh [EVIDENCE_DIR]
#
# Microtuning phase 3, commit 2: Scale snap in the degrees of a tuning (core/source/scales.cpp inDegrees, mtTuning.h semiDegree). The tuning (Notes/period, 1/1 key,
# Scale file rows) and the track's snap mode (Project Settings > Scale snap > Track 1) are set through the real UI. Notes are played by the sequencer on MIDI instrument 48
# (channel 1): "midilog 1" prints every note sent to USB MIDI out, a step note N is sent as N + 24 (scale_snap_sim.sh does the same in 12 EDO). The project scale is the
# Notes scale of the pad config, set with the "scale" line (a Config list, not a tuning row). The scale root of a tuned project is the 1/1 key.
#   P1  31 EDO, Major: Nearest, Down and Up read off the 7 nominals (0 5 10 13 18 23 28) and the period above
#   P2  31 EDO, Minor (flat third, sixth and seventh: 0 5 8 13 18 21 26) and Chromatic (every degree of the tuning is in)
#   P3  17 EDO Major (0 3 6 7 10 13 16), 19 EDO Major (0 3 6 8 11 14 17), 22 EDO and 24 EDO Major (Pythagorean nominals)
#   P4  13 EDO (no fifth): the round(s * 13 / 12) mapping, the 1/1 key moves the scale root (31 EDO, key 62)
#   P5  a Scala file keeps Snap off (the notes play as written), 12 EDO keeps the stock Snap, Off plays as written in a tuning
# Host only. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
NAME=microtune_snap_sim
. "$(cd "$(dirname "$0")" && pwd)/microtune_p3_lib.sh"
EVID="${1:-}"; [ -n "$EVID" ] && mkdir -p "$EVID"
TUN="$ROOT/releases/tunings"
# the Scale snap group is row 5 of the Project Settings page (before Stereo pairs and Tuning): its Track 1 row takes M steps (Off Nearest Down Up)
snapmode() {   # M
    local t="tap File; wait 800; tap F7; wait 800; tap Up 20; tap Down 5; tap F8; wait 300; tap F8; wait 200;" m=$1
    while [ "$m" -gt 0 ]; do if [ "${LISTKEYS:-enc}" = tap ]; then t="$t tap Down; wait 50;"; else t="$t enc 1;"; fi; m=$((m-1)); done
    echo "$t tap F8; wait 200;"; }
# play RELS (key offsets from the 1/1 key ROOT) on track 1 after UI commands X, SCALE index, snap mode M: the MIDI notes sent minus 24 minus ROOT, in order
play() {   # X SCALE M ROOT "rels"
    local sd="$WORK/p-$RANDOM" x=$1 sc=$2 m=$3 root=$4 steps="" i=0 r
    rm -rf "$sd"; mkdir -p "$sd/Tunings"; cp "$TUN/shruti-22.scl" "$sd/Tunings/"
    for r in $5; do steps="$steps setstep 0 $((i*4)) $((root+r)) 48 0 0 0 0;"; i=$((i+1)); done
    "$BIN" "$sd" "$GO $x $(snapmode "$m") scale 24 $sc; instr 48; $steps audio 1; midilog 1; tap Play; wait $((i*470+700)); tap Play; wait 200; midilog 0; snap; tune" 2>>"$WORK/p.err" \
        | grep -E '^(midi on|snap |tune n)' | awk -v b=$((root+24)) -v c=$i '/^midi on/ && n < c {printf "%s%d", (n++ ? " " : ""), $3 - b} END {print ""}'; }
# one case: LABEL X SCALE MODE ROOT RELS WANT
case1() { local got; got=$(play "$2" "$3" "$4" "$5" "$6"); if [ "$got" = "$7" ]; then check "$1: $6 -> $7" ok; else check "$1" "written $6 want '$7' got '$got'"; fi; }
T31="$(ed 0 19)"; T17="$(ed 0 5)"; T19="$(ed 0 7)"; T22="$(ed 0 10)"; T24="$(ed 0 12)"; T13="$(ed 0 1)"
MAJ=2; MIN=1; CHR=0

# P1: 31 EDO Major
R="1 3 8 15 16 26 30 11"
case1 "P1 31 EDO Major Nearest" "$T31" $MAJ 1 60 "$R" "0 5 10 13 18 28 31 10"
case1 "P1 31 EDO Major Down" "$T31" $MAJ 2 60 "$R" "0 0 5 13 13 23 28 10"
case1 "P1 31 EDO Major Up" "$T31" $MAJ 3 60 "$R" "5 5 10 18 18 28 31 13"
case1 "P1 31 EDO Major, notes on the scale and the period above stay" "$T31" $MAJ 1 60 "0 5 10 13 18 23 28 31" "0 5 10 13 18 23 28 31"
# P2: Minor and Chromatic
case1 "P2 31 EDO Minor Nearest (flat third 8, sixth 21, seventh 26)" "$T31" $MIN 1 60 "1 3 6 7 10 15 16 22" "0 5 5 8 8 13 18 21"
case1 "P2 31 EDO Chromatic Nearest: every degree is in" "$T31" $CHR 1 60 "1 3 6 7 10 15 16 22" "1 3 6 7 10 15 16 22"
# P3: other EDOs
case1 "P3 17 EDO Major Nearest" "$T17" $MAJ 1 60 "1 2 4 5 8 9 11 14" "0 3 3 6 7 10 10 13"
case1 "P3 19 EDO Major Nearest" "$T19" $MAJ 1 60 "1 2 4 5 7 9 10 13" "0 3 3 6 6 8 11 14"
case1 "P3 22 EDO Major Nearest (E is 8: the Pythagorean third, ties go down)" "$T22" $MAJ 1 60 "2 3 6 7 10 11 15 19" "0 4 4 8 9 9 13 17"
case1 "P3 24 EDO Major Down" "$T24" $MAJ 2 60 "1 3 5 7 9 11 13 15" "0 0 4 4 8 10 10 14"
# P4: no fifth, root
case1 "P4 13 EDO (round(s * 13 / 12): major 0 2 4 5 8 10 12) Nearest" "$T13" $MAJ 1 60 "1 3 6 7 9 11" "0 2 5 8 8 10"
case1 "P4 31 EDO Major with the 1/1 key on 62 (the scale root is the 1/1 key)" "$T31 $(ed 2 2)" $MAJ 1 62 "1 3 8 15 16" "0 5 10 13 18"
# P5: Scala keeps Snap off, Off is off, 12 EDO is the stock Snap
case1 "P5 a Scala file (shruti 22): Nearest is locked off, the notes play as written" "$(ed 12 1)" $MAJ 1 60 "1 3 8 15" "1 3 8 15"
case1 "P5 31 EDO with the track on Off: as written" "$T31" $MAJ 0 60 "1 3 8 15" "1 3 8 15"
case1 "P5 12 EDO: the stock Snap (C# D# F# G# A# play C D F G A)" "" $MAJ 1 60 "1 3 6 8 10" "0 2 5 7 9"
finish
