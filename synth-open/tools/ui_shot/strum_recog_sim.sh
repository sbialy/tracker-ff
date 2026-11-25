#!/bin/bash
# strum_recog_sim.sh
#
# The hand roll of a recognised chord (core/source/mtRoll.h, written by record join in mtSequencer_chord.cpp and by Capture in
# mtSequencer_capture.cpp) on the real UI code of this tree (ui_shot) on the Mac. Host only: no device, every scenario has its own SD card
# folder. A chord that is recognised also gets a Strum FX in the other slot of the pair of its MIDI Chord FX: units = the mean gap of the
# notes (last note time - first note time) / (n - 1) / 2 ms rounded, + when the first note is not above the last, - (64 + units) when it is, nothing
# under a mean gap of 15 ms (the USB spread of simultaneous presses). Checked: a chord played together, slow rolls up and down, 4 notes,
# the window limit, recompute on every joined note and the clear when later notes pull the mean under the dead zone, a busy partner slot and a
# Strum that is not the group's (both kept), playback of the recorded Strum on a MIDI instrument, and the same rules in Capture. The units and the
# clamp at 63 of the helper itself are in strum_roll_test.cpp (a mean gap past 100 ms cannot reach the join, 50 units is the most it writes).
# ui_shot waits in steps of 5 ms ("midion N V MS" rounds MS up) and the notes land on audio blocks of 2.9 ms, so a gap of 10 ms is the
# nearest to the 13 ms of the unit under the dead zone, the units below are the values of this simulator (a 40 ms gap is 14 blocks, 20 units).
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
WORK="$(mktemp -d "${TMPDIR:-/tmp}/strum_recog_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() {   # run NAME "script": the script's read-back lines, one per line
    local sd="$WORK/$1"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; $2" 2>"$WORK/$1.err" | grep -E '^(step|capbuf|midi) '
}

# the first three note-ons of a midilog: "NOTE +ms to the next|..."
onsets() { awk '{n[NR]=$3; t[NR]=$NF} END{printf "%s +%.1f|%s +%.1f|%s", n[1], t[2]-t[1], n[2], t[3]-t[2], n[3]}'; }

# record join: Rec + Play, ONE armed track (2), the Config Recording option "Only notes" (recopt 0, as the unit), the group lands on step 6
JOIN="recopt 0; instr 64; arm 2; audio 1; tap Play; wait 200; hold Rec; tap Play; unhold Rec; wait 300"
JOINM="recopt 0; instr 48; arm 2; audio 1; tap Play; wait 200; hold Rec; tap Play; unhold Rec; wait 300"
REL="wait 400; midioff 60; midioff 64; midioff 67; midioff 71; wait 300; tap Play; wait 300; step 2 6; step 2 7"
# Capture (Config > Features > Capture on): the notes go to the ring and the under-screen button 3 (tap F4) writes the take into the pattern, the first note on step 0
CAP="capture 1; instr 64; audio 1; tap Play; wait 300"
OFFS="midioff 60 0 20; midioff 64 0 20; midioff 67 0 20; midioff 71 0 200"
BUF="wait 300; tap F4; wait 100; step 0 0"

# 1. record join: a chord pressed together is a chord and no roll (10 ms gaps, under the dead zone), the first step and the next one
out=$(run jsame "$JOIN; midion 60 100 10; midion 64 100 10; midion 67; $REL")
expect "join: C E G together (10 ms gaps) is maj and no Strum" "step 2 6 note 36 instr 64 fx0 14:5 fx1 0:0|step 2 7 note -1 instr 0 fx0 0:0 fx1 0:0" "$(paste -sd'|' - <<<"$out")"

# 2. slow rolls: 40 ms gaps are 20 units, up when the first note is below the last, down (64 + units) the other way round; 4 notes, mean of 30 ms
out=$(run jup "$JOIN; midion 60 100 40; midion 64 100 40; midion 67; $REL")
expect "join: C E G rolled up, 40 ms gaps: maj + Strum +20" "step 2 6 note 36 instr 64 fx0 14:5 fx1 47:20" "$(sed -n 1p <<<"$out")"
out=$(run jdown "$JOIN; midion 67 100 40; midion 64 100 40; midion 60; $REL")
expect "join: G E C rolled down, 40 ms gaps: maj (bass C) + Strum -20 (value 84)" "step 2 6 note 36 instr 64 fx0 14:5 fx1 47:84" "$(sed -n 1p <<<"$out")"
out=$(run jfour "$JOIN; midion 60 100 30; midion 64 100 30; midion 67 100 30; midion 71; $REL")
expect "join: C E G B rolled up, 30 ms gaps: maj7 + Strum +15" "step 2 6 note 36 instr 64 fx0 14:24 fx1 47:15" "$(sed -n 1p <<<"$out")"
out=$(run jfourdown "$JOIN; midion 71 100 30; midion 67 100 30; midion 64 100 30; midion 60; $REL")
expect "join: B G E C rolled down, 30 ms gaps: maj7 + Strum -15 (value 79)" "step 2 6 note 36 instr 64 fx0 14:24 fx1 47:79" "$(sed -n 1p <<<"$out")"
# direction is the first note against the last: C G E is up (C below E) although the middle note is above the last
out=$(run jmid "$JOIN; midion 60 100 40; midion 67 100 40; midion 64; $REL")
expect "join: C G E (first below last) is up" "step 2 6 note 36 instr 64 fx0 14:5 fx1 47:20" "$(sed -n 1p <<<"$out")"

# 3. the limit of the window: 90 ms gaps (the slide window is 100 ms) are 45 units, 120 ms gaps do not join at all (no chord, no Strum)
out=$(run jslow "$JOIN; midion 60 100 90; midion 64 100 90; midion 67; wait 400; midioff 60; midioff 64; midioff 67; wait 300; tap Play; wait 300; step 2 6")
expect "join: 90 ms gaps, near the window: Strum +45" "step 2 6 note 36 instr 64 fx0 14:5 fx1 47:45" "$out"
out=$(run jlate "$JOIN; midion 60 100 120; midion 64 100 120; midion 67; $REL")
expect "join: 120 ms gaps do not join, no Strum anywhere" "0" "$(grep -c 'fx1 47:' <<<"$out")"

# 4. recompute on every joined note: C E 33 ms apart is ~maj with +17, the third note makes the mean 19 ms (+9), two quick notes more pull
#    the mean under 15 ms and the Strum the group wrote is cleared (the chord stays)
out=$(run jrecomp "$JOIN; midion 60 100 33; midion 64 100 3; step 2 6; midion 67 100 3; step 2 6; midion 71; step 2 6; $REL")
expect "join: two notes 33 ms apart, +17" "step 2 6 note 36 instr 64 fx0 14:5 fx1 47:17" "$(sed -n 1p <<<"$out")"
expect "join: the third note recomputes the mean (+9)" "step 2 6 note 36 instr 64 fx0 14:5 fx1 47:9" "$(sed -n 2p <<<"$out")"
expect "join: the fourth note pulls the mean under 15 ms, the Strum is cleared and the chord is now maj7" "step 2 6 note 36 instr 64 fx0 14:24 fx1 0:0" "$(sed -n 3p <<<"$out")"
expect "join: the step stays like that after the release" "step 2 6 note 36 instr 64 fx0 14:24 fx1 0:0" "$(sed -n 4p <<<"$out")"

# 5. the partner slot holds another FX (put there after the first note, as the user could): the chord is kept, no Strum, the FX is never overwritten
out=$(run jbusy "$JOIN; midion 60 100 5; setstep 2 6 36 64 0 0 4 50; midion 64 100 40; midion 67; $REL")
expect "join: partner slot busy (Chance 50): maj kept, no Strum, Chance kept" "step 2 6 note 36 instr 64 fx0 14:5 fx1 4:50" "$(sed -n 1p <<<"$out")"
# a Strum that was not written by this group is a different FX for the group: kept as it is
out=$(run jforeign "$JOIN; midion 60 100 5; setstep 2 6 36 64 0 0 47 10; midion 64 100 40; midion 67; $REL")
expect "join: a Strum already in the partner slot (+10) is kept" "step 2 6 note 36 instr 64 fx0 14:5 fx1 47:10" "$(sed -n 1p <<<"$out")"

# 6. a MIDI instrument records the same, and the Strum plays back: notes 40.6 ms apart, low to high (up), high to low (down)
out=$(run jplayup "$JOINM; midion 60 100 40; midion 64 100 40; midion 67; wait 400; midioff 60; midioff 64; midioff 67; wait 300; tap Play; wait 300; step 2 6; midilog 1; tap Play; wait 1500; tap Play; wait 200; midilog 0")
expect "MIDI instrument: maj + Strum +20 recorded" "step 2 6 note 36 instr 48 fx0 14:5 fx1 47:20" "$(grep '^step' <<<"$out")"
expect "playback of the rolled-up chord: C E G, 40.6 ms apart" "60 +40.6|64 +40.6|67" "$(grep '^midi on' <<<"$out" | onsets)"
out=$(run jplaydown "$JOINM; midion 67 100 40; midion 64 100 40; midion 60; wait 400; midioff 60; midioff 64; midioff 67; wait 300; tap Play; wait 300; step 2 6; midilog 1; tap Play; wait 1500; tap Play; wait 200; midilog 0")
expect "MIDI instrument: maj + Strum -20 recorded" "step 2 6 note 36 instr 48 fx0 14:5 fx1 47:84" "$(grep '^step' <<<"$out")"
expect "playback of the rolled-down chord: G E C, 40.6 ms apart" "67 +40.6|64 +40.6|60" "$(grep '^midi on' <<<"$out" | onsets)"

# 7. Capture: the same rules at the press (the take is written into the pattern)
out=$(run csame "$CAP; midion 60 100 10; midion 64 100 10; midion 67 100 100; $OFFS; $BUF")
expect "capture: C E G together (10 ms gaps) is maj and no Strum" "step 0 0 note 36 instr 64 fx0 14:5 fx1 0:0" "$out"
out=$(run cup "$CAP; midion 60 100 40; midion 64 100 40; midion 67 100 100; $OFFS; $BUF")
expect "capture: C E G rolled up, 40 ms gaps: maj + Strum +20" "step 0 0 note 36 instr 64 fx0 14:5 fx1 47:20" "$out"
out=$(run cdown "$CAP; midion 67 100 40; midion 64 100 40; midion 60 100 100; $OFFS; $BUF")
expect "capture: G E C rolled down, 40 ms gaps: maj + Strum -20 (value 84)" "step 0 0 note 36 instr 64 fx0 14:5 fx1 47:84" "$out"
out=$(run cfour "$CAP; midion 60 100 30; midion 64 100 30; midion 67 100 30; midion 71 100 100; $OFFS; $BUF")
expect "capture: C E G B rolled up, 30 ms gaps: maj7 + Strum +15" "step 0 0 note 36 instr 64 fx0 14:24 fx1 47:15" "$out"
out=$(run cfourdown "$CAP; midion 71 100 30; midion 67 100 30; midion 64 100 30; midion 60 100 100; $OFFS; $BUF")
expect "capture: B G E C rolled down, 30 ms gaps: maj7 + Strum -15 (value 79)" "step 0 0 note 36 instr 64 fx0 14:24 fx1 47:79" "$out"
out=$(run cslow "$CAP; midion 60 100 90; midion 64 100 90; midion 67 100 100; $OFFS; $BUF")
expect "capture: 90 ms gaps, near the window: Strum +46 (the mean of the block times)" "step 0 0 note 36 instr 64 fx0 14:5 fx1 47:46" "$out"
out=$(run cnochord "$CAP; midion 60 100 40; midion 61 100 40; midion 62 100 100; midioff 60 0 20; midioff 61 0 20; midioff 62 0 200; $BUF")
expect "capture: no chord, the first note alone and no Strum" "step 0 0 note 36 instr 64 fx0 0:0 fx1 0:0" "$out"
out=$(run csingle "$CAP; midion 60 100 300; midioff 60 0 200; $BUF")
expect "capture: one note, no chord and no Strum" "step 0 0 note 36 instr 64 fx0 0:0 fx1 0:0" "$out"
# the pattern step holds two other FX: no free slot for the chord and its Strum, the first note alone is written and the effects stay
out=$(run cbusy "capture 1; instr 64; setstep 0 0 -1 0 4 50 3 2; audio 1; tap Play; wait 300; midion 60 100 40; midion 64 100 40; midion 67 100 100; $OFFS; $BUF")
expect "capture: no free FX slot, the first note alone and the step keeps its two FX" "step 0 0 note 36 instr 64 fx0 4:50 fx1 3:2" "$(paste -sd'|' - <<<"$out")"
# sample instruments take no chord FX (one note per track), so no Strum either
out=$(run csample "capture 1; instr 0; audio 1; tap Play; wait 300; midion 60 100 40; midion 64 100 40; midion 67 100 100; $OFFS; wait 300; tap F4; wait 100; step 0 0; step 1 0; step 2 0")
expect "capture: a chord on sample instruments has no Chord FX and no Strum" "0" "$(grep -c 'fx[01] \(14\|47\):' <<<"$out")"

new=$(cat "$WORK"/*.err 2>/dev/null | grep "SUMMARY" | grep -v "mtWavetableEnvelopes.cpp:206" | sort | uniq -c)
if [ -n "$new" ]; then fails=$((fails+1)); echo "FAIL  sanitizer report in a run:"; echo "$new"; fi
echo "$checks checks"
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fails failing)"; exit 1
