#!/bin/bash
# groove_sim.sh
#
# Quantize with strength, the per track groove and the replace record mode (ref/idea-groove.md), on the ui_shot host build of this tree.
#
# Sections:
#   Q  Quantize: the Fill popup of the Micro-move column, type Quantize, strength 0..100 (keys of the device: Rec, Note, Vol, Shift + Down, F4, F5, F8)
#   G  groove playback: the MIDI note times of a track (midilog, virtual clock): swing on odd steps, negative groove pulls recorded micromove to the grid
#   F  groove keys: track.N.groove of project.mtff follow the project (Workspace autosave, Save As to Projects, Open), is never written for a project without a groove
#   R  replace record: Config > Recording mode Replace clears the armed track as the playhead passes, Overdub (the default) does not
#
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)". SAN=address,undefined: the sanitizer build.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
WORK="$(mktemp -d "${TMPDIR:-/tmp}/groove_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
# run NAME "script": stdout of one ui_shot run on its own card folder $WORK/NAME (reused when the name repeats, as a second power on)
run() { ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$1" "$2" 2>"$WORK/$1.err"; }

# ---------------------------------------------------------------------------------------------------------------- Q
echo "-- Q: Quantize (Fill type of the Micro-move column)"
# eight steps with micromove 0 12 24 .. 84 on track 1 (index 0), the same on track 2, steps 8 and 9 outside the selection
SETQ=""
for i in 0 1 2 3 4 5 6 7; do SETQ="$SETQ setstep 0 $i 60 0 0 0 0 0; setmm 0 $i $((i*12)); setstep 1 $i 60 0 0 0 0 0; setmm 1 $i $((i*12));"; done
SETQ="$SETQ setstep 0 8 60 0 0 0 0 0; setmm 0 8 50; setstep 0 9 60 0 0 0 0 0; setmm 0 9 50; setstep 0 10 61 0 0 0 0 0;"   # step 10: a note without micromove
# edit mode, the Micro-move column, steps 1-8 of track 1 selected, the Fill popup open on the type list at Quantize
OPEN="tap Pattern; wait 1200; $SETQ tap Rec; wait 300; tap Note; wait 200; tap Vol; wait 300; hold Shift; tap Down 7; unhold Shift; wait 300; tap F4; wait 400; tap F4 4; wait 300"
MMS="mm 0 0; mm 0 1; mm 0 2; mm 0 3; mm 0 4; mm 0 5; mm 0 6; mm 0 7"
mmline() { grep '^mm ' | awk -v t="$1" '$2 == t {printf "%s%s", (n++ ? " " : ""), $4} END {print ""}'; }
expect "Q0 the popup offers Quantize and names the value Strength (default 100): all eight on the grid (-1)" "-1 -1 -1 -1 -1 -1 -1 -1" \
    "$(run q0 "wait 500; $OPEN; tap F8; wait 300; $MMS" | mmline 0)"
expect "Q1 strength 50 halves every micromove (rounded, 0 becomes -1)" "-1 6 12 18 24 30 36 42" \
    "$(run q1 "wait 500; $OPEN; tap F5; enc -50; wait 200; tap F8; wait 300; $MMS" | mmline 0)"
expect "Q2 strength 25 keeps three quarters" "-1 9 18 27 36 45 54 63" \
    "$(run q2 "wait 500; $OPEN; tap F5; enc -75; wait 200; tap F8; wait 300; $MMS" | mmline 0)"
expect "Q3 strength 0 changes nothing" "0 12 24 36 48 60 72 84" \
    "$(run q3 "wait 500; $OPEN; tap F5; enc -100; wait 200; tap F8; wait 300; $MMS" | mmline 0)"
expect "Q4 only the selection moves: steps 9 and 10 of track 1, the note without micromove, and track 2 keep their values" "50 50 -1 | 0 12 24 36 48 60 72 84" \
    "$(run q4 "wait 500; $OPEN; tap F8; wait 300; mm 0 8; mm 0 9; mm 0 10; mm 1 0; mm 1 1; mm 1 2; mm 1 3; mm 1 4; mm 1 5; mm 1 6; mm 1 7" | { o=$(cat); a=$(printf '%s\n' "$o" | grep '^mm 0 ' | awk '$3 >= 8 {printf "%s%s", (n++ ? " " : ""), $4}'); b=$(printf '%s\n' "$o" | mmline 1); echo "$a | $b"; })"
expect "Q5 Cancel (F7) leaves the pattern as it was" "0 12 24 36 48 60 72 84" \
    "$(run q5 "wait 500; $OPEN; tap F7; wait 300; $MMS" | mmline 0)"
expect "Q6 Undo (F6) after the quantize brings the micromoves back" "0 12 24 36 48 60 72 84" \
    "$(run q6 "wait 500; $OPEN; tap F8; wait 300; tap F6; wait 300; $MMS" | mmline 0)"
expect "Q7 the strength is remembered: a second quantize at 50 gives a quarter of the original" "-1 3 6 9 12 15 18 21" \
    "$(run q7 "wait 500; $OPEN; tap F5; enc -50; wait 200; tap F8; wait 300; tap Up 7; wait 300; hold Shift; tap Down 7; unhold Shift; wait 300; tap F4; wait 400; tap F8; wait 300; $MMS" | mmline 0)"

# ---------------------------------------------------------------------------------------------------------------- G
echo "-- G: groove playback (MIDI note times)"
NOTES=""; for i in 0 1 2 3 4 5 6 7; do NOTES="$NOTES setstep 0 $i $((60+i)) 63 0 0 0 0;"; done      # track 1, MIDI instrument 63
MM80=""; for i in 0 1 2 3 4 5 6 7; do MM80="$MM80 setmm 0 $i 80;"; done                              # recorded micromove 80 on track 1
MMREF=""; for i in 0 1 2 3 4 5 6 7; do MMREF="$MMREF setmm 0 $i -1;"; done                           # no micromove, the same script time (setmm waits 40 ms: the play start must be the same)
PLAY="tap Play; wait 1500; tap Play; wait 300"
times() {   # NAME SETUP: the note on times (ms) of the run, one "N=T" per note
    run "$1" "audio 1; midilog 1; wait 500; tap Pattern; wait 1200; $2 $PLAY" | grep '^midi on' | awk '{printf "%s=%s ", $3, $8} END {print ""}'
}
# delays NAME SETUP REFSETUP: how late each note of track 1 (note 84 + step) is against the same run without groove and micromove, in percent of a step
delays() {
    local a b
    a="$(times "$1" "$2")"; b="$(times "${1}ref" "$3")"
    python3 -I - "$a" "$b" <<'EOF'
import sys
def parse(s):
    d = {}
    for kv in s.split():
        n, t = kv.split("=")
        d.setdefault(int(n), float(t))    # the first note on of that note number
    return d
a, r = parse(sys.argv[1]), parse(sys.argv[2])
notes = sorted(n for n in r if n in a)
steps = sorted(r)
period = (r[steps[6]] - r[steps[0]]) / 6.0
print(" ".join("%d" % round((a[n] - r[n]) / period * 100) for n in notes))
EOF
}
near() {   # "EXPECTED..." "GOT...": every number within +-4 percent (one tick is 2.1 percent of a step) and the same count
    python3 -I - "$1" "$2" <<'EOF'
import sys
e, g = [int(x) for x in sys.argv[1].split()], [int(x) for x in sys.argv[2].split()]
print("ok" if len(e) == len(g) and e and all(abs(x - y) <= 4 for x, y in zip(e, g)) else "off")
EOF
}
expectnear() { checks=$((checks+1)); local r; r="$(near "$2" "$3")"; if [ "$r" = ok ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s (+-4)\n      got:      %s\n' "$1" "$2" "$3"; fi; }

expect "G0 the reference run plays all eight notes" "8" "$(times gref "$NOTES" | wc -w | tr -d ' ')"
expectnear "G1 groove 0 is exactly the stock timing (set explicitly to 0)" "0 0 0 0 0 0 0 0" "$(delays g1 "$NOTES groove 0 0;" "$NOTES")"
expectnear "G2 groove +25: odd steps 25 percent of a step late, even steps on the grid" "0 25 0 25 0 25 0 25" "$(delays g2 "$NOTES groove 0 25;" "$NOTES")"
expectnear "G3 groove +50: odd steps half a step late" "0 50 0 50 0 50 0 50" "$(delays g3 "$NOTES groove 0 50;" "$NOTES")"
NOTES2=""; for i in 0 1 2 3 4 5 6 7; do NOTES2="$NOTES2 setstep 1 $i $((70+i)) 63 0 0 0 0;"; done   # track 2: notes 94..101
expectnear "G4 groove +17 on track 1 only: track 1 odd steps late, track 2 (notes 94..101) on the grid" "0 17 0 17 0 17 0 17 0 0 0 0 0 0 0 0" \
    "$(delays g4 "$NOTES $NOTES2 groove 0 17;" "$NOTES $NOTES2")"
expectnear "G5 recorded micromove 80 plays 81 percent of a step late without groove" "81 81 81 81 81 81 81 81" "$(delays g5 "$NOTES $MM80" "$NOTES $MMREF")"
expectnear "G6 groove -25 pulls it half way to the grid" "40 40 40 40 40 40 40 40" "$(delays g6 "$NOTES $MM80 groove 0 -25;" "$NOTES $MMREF")"
expectnear "G7 groove -50 puts it on the grid" "0 0 0 0 0 0 0 0" "$(delays g7 "$NOTES $MM80 groove 0 -50;" "$NOTES $MMREF")"
expectnear "G8 groove -50 on notes without micromove changes nothing" "0 0 0 0 0 0 0 0" "$(delays g8 "$NOTES groove 0 -50;" "$NOTES")"
expectnear "G9 groove +25 adds to a recorded micromove on odd steps, capped under one step (81 and 98 percent)" "81 98 81 98 81 98 81 98" "$(delays g9 "$NOTES $MM80 groove 0 25;" "$NOTES $MMREF")"
expect "G10 the pattern keeps its micromoves under groove (playback only)" "80 80 80 80 80 80 80 80" \
    "$(run g10 "audio 1; wait 500; tap Pattern; wait 1200; $NOTES $MM80 groove 0 -50; $PLAY; mm 0 0; mm 0 1; mm 0 2; mm 0 3; mm 0 4; mm 0 5; mm 0 6; mm 0 7" | mmline 0)"
expect "G11 groove survives a clamp: +80 reads +50, -80 reads -50" "groove 50 -50 0 0 0 0 0 0 0 0 0 0 0 0 0 0" \
    "$(run g11 "wait 300; groove 0 80; groove 1 -80; groove" | grep '^groove')"

# ---------------------------------------------------------------------------------------------------------------- F
echo "-- F: the track.N.groove keys of project.mtff follow the project"
# the track keys of a project.mtff, one line: the file has the stamp and the Strum ramp 100 of every new project too, those are not this section's
TK() { grep '^track\.' "$1" 2>/dev/null | paste -sd'|' -; }
GF() { TK "$WORK/$1/Workspace/project.mtff"; }
run f0 "wait 500; tap Pattern; wait 1200; dirty; wait 3500" > /dev/null
expect "F0 a project without a groove has no track key in its project.mtff" "" "$(GF f0)"
run f1 "wait 500; tap Pattern; wait 1200; groove 0 17; groove 3 -25; dirty; wait 4000" > /dev/null
expect "F1 the Workspace autosave writes the keys" "track.1.groove 17|track.4.groove -25" "$(GF f1)"
expect "F2 after a power cycle the sequencer has the groove again" "groove 17 0 0 -25 0 0 0 0 0 0 0 0 0 0 0 0" "$(run f1 "wait 500; groove" | grep '^groove')"
run f1 "wait 500; groove 0 0; dirty; wait 4000" > /dev/null
expect "F3 clearing one track keeps the other" "track.4.groove -25" "$(GF f1)"
run f1 "wait 500; groove 3 0; dirty; wait 4000" > /dev/null
expect "F4 clearing the last groove removes the key (the file stays, the new project's Strum ramp is in it)" "" "$(GF f1)"
# Save As and Open: the file follows the project folder
O="$(run f2 "wait 500; tap Pattern; wait 1200; groove 2 33; dirty; wait 3500; saveproj Projects Withgroove; groove 2 0; groove" | grep -E '^(saveproj|groove)' | tr '\n' ' ')"
expect "F5 Save As runs and the sequencer was cleared by hand" "saveproj ok status 0 groove 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 " "$O"
expect "F6 the project folder holds the key" "track.3.groove 33" "$(TK "$WORK/f2/Projects/Withgroove/project.mtff")"
O="$(run f2 "wait 500; tap Pattern; wait 1200; groove 2 0; groove 5 -9; dirty; wait 3500; saveproj Projects Plain; groove 5 0; dirty; wait 3500; saveproj Projects Plain; groove" | grep -E '^groove')"
expect "F7 a second project saved without a groove has no track key" "" "$(TK "$WORK/f2/Projects/Plain/project.mtff")"
O="$(run f2 "wait 500; openproj Projects Withgroove; groove" | grep -E '^(openproj|groove)' | tr '\n' ' ')"
expect "F8 Open brings the groove of the project (and the Workspace file)" "openproj ok status 0 groove 0 0 33 0 0 0 0 0 0 0 0 0 0 0 0 0 | track.3.groove 33" "$O| $(GF f2)"
O="$(run f2 "wait 500; openproj Projects Plain; groove" | grep -E '^(openproj|groove)' | tr '\n' ' ')"
expect "F9 Open of a project without the key clears the groove and the Workspace key" "openproj ok status 0 groove 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 | " "$O| $(GF f2)"
run f3 "wait 300" > /dev/null; mkdir -p "$WORK/f3/Workspace"; printf 'garbage\n' > "$WORK/f3/Workspace/project.mtff"
expect "F10 a foreign project.mtff reads as no groove and is kept" "groove 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 | garbage|" \
    "$(run f3 "wait 500; tap Pattern; wait 1200; dirty; wait 3500; groove" | grep '^groove') | $(cat "$WORK/f3/Workspace/project.mtff" | tr '\n' '|')"

# ---------------------------------------------------------------------------------------------------------------- R
echo "-- R: replace record"
# track 3 (index 2) holds a MIDI note on every step 0..15 (note 40 + step, MIDI instrument 63), track 4 the same (not armed)
OLD=""; for i in $(seq 0 15); do OLD="$OLD setstep 2 $i $((40+i)) 63 0 0 0 0; setstep 3 $i $((40+i)) 63 0 0 0 0;"; done
STEPS=""; for i in $(seq 0 19); do STEPS="$STEPS step 2 $i; step 3 $i;"; done
# the notes of a track as "step=note" (steps 0 based, note offs and empty steps left out)
cells() { grep '^step ' | awk -v t="$1" '$2 == t && $5 >= 0 {printf "%s%d=%s", (n++ ? " " : ""), $3, $5} END {print ""}'; }
# the take starts with the playhead (Rec held while Play starts it, no count in), lasts about 1 s: the head passes steps 0 to about 9
START="hold Rec; tap Play; unhold Rec; wait 20"
STOP="wait 600; tap Play; wait 300"
GO="audio 1; tap Pattern; wait 1200; tap Right 2; wait 1200; hold Instr; wait 300; enc 64; wait 300; unhold Instr; wait 400"
rec() {   # NAME MODE ARM OPT MIDI: a take, MODE 0 overdub 1 replace, ARM the armed track, OPT recopt, MIDI the events (midion 96 is stored as note 72)
    run "$1" "wait 500; $GO; recopt $4; recmode $2; $OLD arm $3; $START; $5; $STOP; $STEPS"
}
ALL16="0=40 1=41 2=42 3=43 4=44 5=45 6=46 7=47 8=48 9=49 10=50 11=51 12=52 13=53 14=54 15=55"
NOTE_A="wait 100; midion 96 100 40; midioff 96 100 0"
R1="$(rec r1 0 2 0 "$NOTE_A")"
expect "R1 overdub (the default): the old notes stay, the new note finds no free step (stock) and is not written" "$ALL16" "$(printf '%s\n' "$R1" | cells 2)"
R2="$(rec r2 1 2 0 "$NOTE_A")"
C2="$(printf '%s\n' "$R2" | cells 2)"
expect "R2 replace: the recorded note is on the pattern, exactly once" "1" "$(printf '%s\n' "$C2" | tr ' ' '\n' | grep -c '=72$')"
# old notes still there: step >= 12 for sure (the head does not get there), none below 5
expect "R3 replace: the playhead cleared the front of the armed track (no old note below step 6), the tail (steps 12 to 15) is untouched" "0 4" \
    "$(printf '%s\n' "$C2" | tr ' ' '\n' | awk -F= 'NF && $2 != 72 && $1 < 6 {lo++} NF && $1 >= 12 && $2 == 40 + $1 {hi++} END {print lo + 0, hi + 0}')"
expect "R4 replace: the track that is not armed keeps every note" "$ALL16" "$(printf '%s\n' "$R2" | cells 3)"
R5="$(rec r5 1 2 0 "")"
C5="$(printf '%s\n' "$R5" | cells 2)"
expect "R5 replace with no note played is a punch erase: front cleared, tail kept, nothing new" "0 4 0" \
    "$(printf '%s\n' "$C5" | tr ' ' '\n' | awk -F= 'NF && $1 < 6 {lo++} NF && $1 >= 12 && $2 == 40 + $1 {hi++} NF && $2 == 72 {nw++} END {print lo + 0, hi + 0, nw + 0}')"
R6="$(rec r6 1 2 0 "$NOTE_A")"
expect "R6 replace is repeatable: the same take twice gives the same pattern" "$C2" "$(printf '%s\n' "$R6" | cells 2)"
R12="$(rec r12 1 -1 0 "$NOTE_A")"
expect "R6b replace with every track armed (the default): only the track under the cursor is erased, the other armed tracks keep all notes" "$ALL16 | 0 4" \
    "$(printf '%s\n' "$R12" | cells 3) | $(printf '%s\n' "$R12" | cells 2 | tr ' ' '\n' | awk -F= 'NF && $2 != 72 && $1 < 6 {lo++} NF && $1 >= 12 && $2 == 40 + $1 {hi++} END {print lo + 0, hi + 0}')"
# the note struck at every part of a step (the quantized path moves the second half of a step to the next step)
allok=1
for off in 0 20 40 60 80 100 115; do
    X="$(rec "rs$off" 1 2 0 "wait $((100 + off)); midion 96 100 40; midioff 96 100 0")"
    n72="$(printf '%s\n' "$X" | cells 2 | tr ' ' '\n' | grep -c '=72$')"
    lo="$(printf '%s\n' "$X" | cells 2 | tr ' ' '\n' | awk -F= 'NF && $2 != 72 && $1 < 6 {print}' | wc -l | tr -d ' ')"
    hi="$(printf '%s\n' "$X" | cells 2 | tr ' ' '\n' | awk -F= 'NF && $1 >= 12 && $2 == 40 + $1 {n++} END {print n + 0}')"
    if [ "$n72" != 1 ] || [ "$lo" != 0 ] || [ "$hi" != 4 ]; then allok=0; echo "      note +$off ms: recorded $n72 times, $lo old notes below step 6, $hi of 4 tail notes"; fi
done
expect "R7 replace, the note struck over one whole step (offsets 0..115 ms): once on the pattern, front cleared, tail untouched" "1" "$allok"
allok=1
for off in 0 40 80 115; do
    X="$(rec "rq$off" 1 2 1 "wait $((100 + off)); midion 96 100 40; midioff 96 100 0")"
    n72="$(printf '%s\n' "$X" | cells 2 | tr ' ' '\n' | grep -c '=72$')"
    lo="$(printf '%s\n' "$X" | cells 2 | tr ' ' '\n' | awk -F= 'NF && $2 != 72 && $1 < 6 {print}' | wc -l | tr -d ' ')"
    if [ "$n72" != 1 ] || [ "$lo" != 0 ]; then allok=0; echo "      microtiming, note +$off ms: recorded $n72 times, $lo old notes below step 6"; fi
done
expect "R8 replace with Recording options Microtiming: the same, once" "1" "$allok"
R10="$(rec r10 1 2 0 "wait 100; midion 96 100 40; midioff 96 100 0; wait 240; midion 98 100 40; midioff 98 100 0")"
expect "R9 replace, two notes: both on the pattern" "2" "$(printf '%s\n' "$R10" | cells 2 | tr ' ' '\n' | grep -c '=7[24]$')"
# undo: the whole take is one revision (Undo key of the pattern screen is F6 in edit mode)
RU="$(run ru "wait 500; $GO; recopt 0; recmode 1; $OLD arm 2; $START; $NOTE_A; $STOP; tap Rec; wait 200; tap F6; wait 400; $STEPS")"
expect "R10 one Undo brings the whole replaced take back" "$ALL16" "$(printf '%s\n' "$RU" | cells 2)"
expect "R11 the Config item exists: 'Recording mode' on the General page (screen shot taken)" "yes" \
    "$(run r11 "wait 500; tap Config; wait 1500; shot $WORK/r11.png" >/dev/null; [ -s "$WORK/r11.png" ] && echo yes || echo no)"

echo
if [ "$fails" = 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
