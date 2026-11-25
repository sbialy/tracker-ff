#!/bin/bash
# microtune_names_sim.sh [EVIDENCE_DIR]
#
# Microtuning phase 3, commit 1: ups and downs note names (core/source/mtTuning.h noteNameUd, project.mtff tune.names). Driven through the real UI:
# File F7 (Project Settings) > Tuning > Notes/period (row 0), Period (row 1), 1/1 key (row 2), Scale file (row 12) and Note names (row 13). The names are read back
# with "tune name K" (the screen name of key K, the same noteName() the pattern editor, the fill dialog and the pad popups call).
#   N1  17, 19, 22, 24 and 31 EDO: the 1/1 key and the whole period read off the xen wiki rules (nominals from the fifth, sharp and flat, one ^ or v)
#   N2  the default is degrees: the Note names row left alone shows 0.3 style names, the row back to Degrees restores them, 12 EDO keeps the stock names
#   N3  refused: 15, 13 and 53 EDO (no fifth names every degree), a stretched period, a Scala file. The row stays Degrees and the names stay degrees
#   N4  the choice survives a tuning that has no such names (31 over 1205 cents shows degrees, back to 2/1 shows ups and downs again), root key 61 moves the C
#   N5  project.mtff: tune.names ud is written only for ud, a reload shows the names, Degrees writes nothing, a garbage value and a phase 1 and phase 2 project load as before
#   N6  the pattern editor screen shows ^ and v (a picture of 31 EDO, EVIDENCE_DIR)
# Host only. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
NAME=microtune_names_sim
. "$(cd "$(dirname "$0")" && pwd)/microtune_p3_lib.sh"
EVID="${1:-}"; [ -n "$EVID" ] && mkdir -p "$EVID"
TUN="$ROOT/releases/tunings"

# names of keys K0..K1 after UI commands X on a fresh card, as one line
names() {   # X K0 K1
    local sd="$WORK/n-$RANDOM"; rm -rf "$sd"; mkdir -p "$sd/Tunings"; cp "$TUN/shruti-22.scl" "$sd/Tunings/"
    local c=""; for ((k=$2; k<=$3; k++)); do c="$c tune name $k;"; done
    "$BIN" "$sd" "$GO $1 $c" 2>>"$WORK/n.err" | grep '^tune name' | awk '{printf "%s ", $4}' | sed 's/ $//'; }
state() { "$BIN" "$1" "wait 300; tune names" 2>/dev/null | grep '^tune names' | tail -1; }

# N1: the five EDOs the user plays. Rows: Notes/period = 0 (shown 12, so N-12 steps), Note names = 13 (one step: Ups+downs)
declare -A WANT=(
 [17]="C4 Db4 C#4 D4 Eb4 D#4 E4 F4 E#4 F#4 G4 Ab4 G#4 A4 Bb4 A#4 B4"
 [19]="C4 C#4 Db4 D4 D#4 Eb4 E4 E#4 F4 F#4 Gb4 G4 G#4 Ab4 A4 A#4 Bb4 B4 B#4"
 [22]="C3 Db3 B#2 C#3 D3 Eb3 Fb3 D#3 E3 F3 Gb3 E#3 F#3 G3 Ab3 vG#3 G#3 A3 Bb3 Cb4 A#3 B3"
 [24]="C3 ^C3 C#3 vD3 D3 ^D3 D#3 vE3 E3 ^E3 F3 ^F3 F#3 vG3 G3 ^G3 G#3 vA3 A3 ^A3 A#3 vB3 B3 ^B3"
 [31]="C2 ^C2 C#2 Db2 vD2 D2 ^D2 D#2 Eb2 vE2 E2 Fb2 E#2 F2 ^F2 F#2 Gb2 vG2 G2 ^G2 G#2 Ab2 vA2 A2 ^A2 A#2 Bb2 vB2 B2 Cb3 B#2")
for n in 17 19 22 24 31; do
    got=$(names "$(ed 0 $((n-12))) $(ed 13 1)" 60 $((59+n)))
    if [ "$got" = "${WANT[$n]}" ]; then check "N1 $n EDO: the 1/1 key and the $n degrees read C ... B" ok; else check "N1 $n EDO" "got '$got'"; fi
done
# the edges of the key range: key 91 is the next C, key 127 is D with a one digit octave, key 0 is C#0 (the octave counter never goes below 0)
got="$(names "$(ed 0 19) $(ed 13 1)" 91 91) $(names "$(ed 0 19) $(ed 13 1)" 127 127) $(names "$(ed 0 19) $(ed 13 1)" 0 0)"
check "N1 31 EDO key 91 C3, key 127 D4, key 0 C#0 ($got)" "$([ "$got" = "C3 D4 C#0" ] && echo ok || echo "got '$got'")"

# N2: the default stays degrees
g1=$(names "$(ed 0 19)" 91 91); check "N2 31 EDO with the Note names row left alone: degree names (key 91 is 0.3, got $g1)" "$([ "$g1" = "0.3" ] && echo ok || echo "got '$g1'")"
g2=$(names "$(ed 0 19) $(ed 13 1) $(ed 13 -1)" 91 91); check "N2 Note names to Ups+downs and back to Degrees: 0.3 again (got $g2)" "$([ "$g2" = "0.3" ] && echo ok || echo "got '$g2'")"
st12=$(names "" 60 62); g3=$(names "$(ed 13 1)" 60 62)
check "N2 12 EDO keeps the stock names with Ups+downs chosen too ($st12 / $g3)" "$([ -n "$st12" ] && [ "$st12" = "$g3" ] && echo ok || echo "got '$st12' '$g3'")"

# N3: refused. The row stays Degrees (cfg.names 0) and the names stay degrees
ui() {   # SD "ui commands": the lines "tune ..." of the run, one per line
    "$BIN" "$1" "$GO $2" 2>>"$WORK/n.err" | grep '^tune '; }
for n in 15 13 53; do
    sd="$WORK/n3-$n"; rm -rf "$sd"; mkdir -p "$sd"
    out=$(ui "$sd" "$(ed 0 $((n-12))) $(ed 13 1) tune names; tune name 60" | tr '\n' '|')
    check "N3 $n EDO: Ups+downs is refused (names 0, ok 0) and key 60 stays a degree ($out)" "$([[ "$out" == "tune names 0 ok 0 active 0|tune name 60 "[0-9]*"|" ]] && echo ok || echo "got '$out'")"
done
sd="$WORK/n3-str"; rm -rf "$sd"; mkdir -p "$sd"
out=$(ui "$sd" "$(ed 0 19) $(ed 1 5) $(ed 13 1) tune; tune names" | tr '\n' '|')
check "N3 31 over 1205 cents: refused, names 0 ($out)" "$([[ "$out" == "tune n 31 period 1205000 "*"|tune names 0 ok 1 active 0|" ]] && echo ok || echo "got '$out'")"
sd=$(newsd n3-scl "$TUN/shruti-22.scl")
out=$(ui "$sd" "$(ed 12 1) $(ed 13 1) tune names; tune name 60" | tr '\n' '|')
check "N3 the 22 note Scala file: Ups+downs refused, degrees ($out)" "$([[ "$out" == "tune names 0 ok 1 active 0|tune name 60 "[0-9]*"|" ]] && echo ok || echo "got '$out'")"

# N4: the choice is kept while the tuning has no such names (1205 cents), root key moves the C
sd="$WORK/n4"; rm -rf "$sd"; mkdir -p "$sd"
out=$(ui "$sd" "$(ed 0 19) $(ed 13 1) tune names; $(ed 1 5) tune names; tune name 60; $(ed 1 -5) tune names; tune name 60" | tr '\n' '|')
check "N4 31 EDO ud, period to 1205 (names stay 1, active 0, degrees), back to 2/1 (active 1, C2) ($out)" "$([[ "$out" == "tune names 1 ok 1 active 1|tune names 1 ok 1 active 0|tune name 60 "[0-9]*"|tune names 1 ok 1 active 1|tune name 60 C2|" ]] && echo ok || echo "got '$out'")"
sd="$WORK/n4r"; rm -rf "$sd"; mkdir -p "$sd"
out=$(ui "$sd" "$(ed 0 19) $(ed 13 1) $(ed 2 1) tune name 61; tune name 60; tune name 62" | tr '\n' '|')
check "N4 1/1 key 61: key 61 is C, key 60 is the degree below it, key 62 is ^C ($out)" "$([[ "$out" == *"tune name 61 C2|tune name 60 B#1|tune name 62 ^C2|" ]] && echo ok || echo "got '$out'")"

# N5: project.mtff
sd="$WORK/n5"; rm -rf "$sd"; mkdir -p "$sd"; runui "$sd" "$(ed 0 19) $(ed 13 1)"
check "N5 ud on 31 EDO writes tune.n 31 and tune.names ud ($(mtff "$sd"))" "$([ "$(mtff "$sd")" = "tune.n 31|tune.names ud|" ] && echo ok || echo "got '$(mtff "$sd")'")"
out=$(ui "$sd" "tune names; tune name 61; tune name 63" | tr '\n' '|')
check "N5 reload (a new process = power up): the names are back (^C2, Db2: $out)" "$([[ "$out" == *"tune names 1 ok 1 active 1|tune name 61 ^C2|tune name 63 Db2|" ]] && echo ok || echo "got '$out'")"
runui "$sd" "$(ed 13 -1)"
check "N5 Degrees again: the tune.names line is gone ($(mtff "$sd"))" "$([ "$(mtff "$sd")" = "tune.n 31|" ] && echo ok || echo "got '$(mtff "$sd")'")"
sd="$WORK/n5d"; rm -rf "$sd"; mkdir -p "$sd"; runui "$sd" "$(ed 13 1)"
check "N5 12 EDO with Ups+downs chosen: the line is written ($(mtff "$sd")), Degrees: none" "$([ "$(mtff "$sd")" = "tune.names ud|" ] && echo ok || echo "got '$(mtff "$sd")'")"
runui "$sd" "$(ed 13 -1)"; check "N5 ... and gone again" "$([ -z "$(mtff "$sd")" ] && echo ok || echo "got '$(mtff "$sd")'")"
sd="$WORK/n5g"; mkws "$sd" 'tffproj 1\ntune.n 31\ntune.names loud\n'
out=$(ui "$sd" "tune names; tune name 91" | tr '\n' '|'); check "N5 a garbage value (tune.names loud) is the default: degrees ($out)" "$([[ "$out" == "tune names 0 ok 1 active 0|tune name 91 0.3|" ]] && echo ok || echo "got '$out'")"
sd="$WORK/n5p1"; mkws "$sd" 'tffproj 1\ntune.n 31\ntune.hz 2640000\n'
out=$(ui "$sd" "tune names; tune name 91" | tr '\n' '|'); check "N5 a phase 1 project (tune.n, tune.hz) loads with degree names ($out)" "$([[ "$out" == "tune names 0 ok 1 active 0|tune name 91 0.3|" ]] && echo ok || echo "got '$out'")"
sd="$WORK/n5p2"; mkws "$sd" 'tffproj 1\ntune.n 7\ntune.file x.scl\ntune.cents 203910 386314 498045 701955 884359 1088269\n'
out=$(ui "$sd" "tune names; tune name 60" | tr '\n' '|'); check "N5 a phase 2 project (a Scala tuning) loads with degree names ($out)" "$([[ "$out" == "tune names 0 ok 0 active 0|tune name 60 "[0-9]*"|" ]] && echo ok || echo "got '$out'")"
sd="$WORK/n5s"; mkws "$sd" 'tffproj 1\ntune.n 31\ntune.names ud\nfuture.key 7\n'
runui "$sd" ""; check "N5 a rewrite keeps tune.names ud next to tune.n ($(mtff "$sd"))" "$([ "$(mtff "$sd")" = "tune.n 31|tune.names ud|" ] && echo ok || echo "got '$(mtff "$sd")'")"

# N6: the pattern editor draws ^ and v
sd="$WORK/n6"; rm -rf "$sd"; mkdir -p "$sd"
"$BIN" "$sd" "$GO $(ed 0 19) $(ed 13 1) setstep 0 0 61 0 0 0 0 0; setstep 0 1 64 0 0 0 0 0; setstep 0 2 72 0 0 0 0 0; setstep 0 3 89 0 0 0 0 0; setstep 0 4 90 0 0 0 0 0; setstep 0 5 63 0 0 0 0 0; tap Pattern; wait 600; shot $WORK/n6.png" >/dev/null 2>>"$WORK/n.err"
[ -n "$EVID" ] && cp "$WORK/n6.png" "$EVID/names-31edo-pattern.png"
check "N6 the pattern editor drew a picture with the 31 EDO names ($(wc -c < "$WORK/n6.png" 2>/dev/null) bytes)" "$([ "$(wc -c < "$WORK/n6.png" 2>/dev/null || echo 0)" -gt 5000 ] && echo ok || echo "no picture")"
finish
