#!/bin/bash
# midi_in_latency_sim.sh
#
# The two device findings of the 1.3.0 test images (J0 and J1) on the real UI code of this tree (ui_shot) on
# the Mac. Host only, no device. Every scenario arms the tracks with the keys the device run used (Shift + Rec, then F1 F2 F4..F8
# with the cursor on track 3), never with the "arm" shortcut of chord_capture_sim.sh, and feeds the notes at the arrival times
# measured on this Mac (the old midisend into a virtual CoreMIDI destination, synth-open/tools/midi/), or at the times the
# sequence asked for (midisend_ts).
#
#   A  the device arming leaves the 8 MIDI tracks (9-16) armed: getSingleArmedTrack() is -1, the record join never runs
#   B  J1 by pads 6 ms apart: device arming = C on track 3, E and G on MIDI tracks 9 and 10 (the device grid: C, no join, E G
#      "dropped") ON THE 1.3.0 TEST IMAGE. Since the cursor track record join (2026-11-12) the cursor track (3, armed) takes
#      the join with several armed: one step C with maj, the same as with the MIDI page disarmed. recjoin_cursor_sim.sh is the full set
#   C  J1 by USB MIDI at the old midisend's measured gaps (0 / 79 / 153 ms, offs at 565..612 ms): device arming = C and an OFF 5
#      steps later on track 3 (the device grid, before recjoin-cursor), now maj with the OFF 5 steps later; MIDI page disarmed = maj (the gaps are under the 100 ms window); timestamped
#      gaps (0 / 18 / 36 ms) = maj
#   D  J0 taps meant 270 ms apart at the old midisend's measured times (445-470 ms a tap): track 3 every 4 steps (the device
#      grid: 06 0A 0E 12); the same taps at 270 ms: every 2 to 3 steps
#
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)". SAN=address,undefined: the sanitizer build.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="$BUILD/ui_shot"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/midi_in_latency_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() {   # run NAME "script": the lines the script prints
    local sd="$WORK/$1"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; $2" 2>"$WORK/$1.err" | grep -E '^(step|armed|cursor) '
}
# the written steps as "track:step=note" (tracks 1 based, steps hex), empty steps left out
cells() { grep '^step' | grep -v 'note -1 instr 0 fx0 0:0 fx1 0:0' | awk '{printf "%s%d:%x=%s%s", (n++ ? " " : ""), $2 + 1, $3, $5, ($9 != "0:0" ? "/" $9 : "")} END {print ""}'; }
STEPS=""; for t in 2 8 9 10; do for i in $(seq 0 40); do STEPS="$STEPS step $t $i;"; done; done

# the device run's key sequence (fresh.ut + start.ut), with the cursor on track 3 (index 2)
# fresh.ut also turned the encoder 64 detents with Instr held: instrument 63, MIDI 16 (a MIDI instrument, the join takes it)
CURSOR="audio 1; tap Pattern; wait 1200; tap Right 2; wait 1200; hold Instr; wait 300; enc 64; wait 300; unhold Instr; wait 400"
ARM_SAMPLE="hold Shift; tap Rec; wait 400; tap F1; wait 200; tap F2; wait 200; tap F4; wait 200; tap F5; wait 200; tap F6; wait 200; tap F7; wait 200; tap F8; wait 400; unhold Shift; wait 500"
# the same on the MIDI page: cursor to track 9 (the first MIDI track), all eight F keys, back to track 3
ARM_MIDI="tap Right 6; wait 300; hold Shift; tap Rec; wait 400; tap F1; wait 200; tap F2; wait 200; tap F3; wait 200; tap F4; wait 200; tap F5; wait 200; tap F6; wait 200; tap F7; wait 200; tap F8; wait 400; unhold Shift; wait 500; tap Left 6; wait 300"
START="tap Play; wait 200; hold Rec; tap Play; unhold Rec; wait 100"
STOP="wait 300; tap Play; wait 300"

# midiseq "+60@0 -60@123 ...": the events at those ms (5 ms grid of the simulator loop) as midion / midioff with the loop run between
midiseq() {
    local out="" prev=-1 w
    local -a ev=($1)
    for ((i = 0; i < ${#ev[@]}; i++)); do
        local e="${ev[$i]}" next
        local note="${e:1}"; note="${note%@*}"
        if [ $((i + 1)) -lt ${#ev[@]} ]; then next="${ev[$((i + 1))]#*@}"; w=$(( (next - ${e#*@} + 2) / 5 * 5 )); else w=0; fi
        if [ "${e:0:1}" = "+" ]; then out="$out midion $note 100 $w;"; else out="$out midioff $note 100 $w;"; fi
    done
    printf '%s' "$out"
}

# A. what the device arming leaves armed
out=$(run arm "$CURSOR; $ARM_SAMPLE; armed; $ARM_MIDI; armed")
expect "A device arming: track 3 and the 8 MIDI tracks armed" "armed 0 0 1 0 0 0 0 0 1 1 1 1 1 1 1 1" "$(sed -n 1p <<<"$out")"
expect "A MIDI page disarmed too: track 3 alone" "armed 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0" "$(sed -n 2p <<<"$out")"

# B. J1 by pads (padj1.ut: pad 12 16 19 pressed 6 ms apart, held 300 ms)
PADS="padon 12 5; padon 16 5; padon 19 300; padoff 12 5; padoff 16 5; padoff 19 150"
got=$(run padunit "$CURSOR; $ARM_SAMPLE; $START; $PADS; $STOP; $STEPS" | cells)
expect "B pads, device arming, cursor on 3 (armed): joins on 3 since recjoin-cursor (was C on 3, E and G on 9 and 10, no join)" "3:5=60/14:5 3:7=-4" "$got"
got=$(run padone "$CURSOR; $ARM_SAMPLE; $ARM_MIDI; $START; $PADS; $STOP; $STEPS" | cells)
expect "B pads, one armed track: one step, C with maj (MIDI Chord 14:5)" "3:5=60/14:5 3:7=-4" "$got"

# C. J1 by USB MIDI
OLDJ1="+72@0 +76@79 +79@153 -72@565 -76@591 -79@612"   # old midisend, measured on this Mac (cmp_old_j1_2)
TSJ1="+72@0 +76@18 +79@36 -72@346 -76@351 -79@356"     # midisend_ts, the gaps the sequence asks for
got=$(run midiunit "$CURSOR; $ARM_SAMPLE; $START; $(midiseq "$OLDJ1") $STOP; $STEPS" | cells)
expect "C MIDI old gaps, device arming, cursor on 3 (armed): joins on 3 since recjoin-cursor (was C, OFF 5 steps later, E G on 9 and 10)" "3:5=48/14:5 3:a=-4" "$got"
got=$(run midiold "$CURSOR; $ARM_SAMPLE; $ARM_MIDI; $START; $(midiseq "$OLDJ1") $STOP; $STEPS" | cells)
expect "C MIDI old gaps, one armed track: still joins (79 and 74 ms gaps, window 100 ms)" "3:5=48/14:5 3:a=-4" "$got"
got=$(run midits "$CURSOR; $ARM_SAMPLE; $ARM_MIDI; $START; $(midiseq "$TSJ1") $STOP; $STEPS" | cells)
expect "C MIDI timestamped gaps, one armed track: maj" "3:5=48/14:5 3:8=-4" "$got"

# D. J0 taps (jrun.sh: on, wait 20, off, wait 230, 8 notes)
OLDJ0="+60@0 -60@123 +62@462 -62@589 +64@933 -64@1075 +65@1405 -65@1515 +67@1863 -67@2006 +69@2347 -69@2490 +71@2837 -71@2977 +72@3290 -72@3435"
TSJ0=""; t=0; for n in 60 62 64 65 67 69 71 72; do TSJ0="$TSJ0 +$n@$t -$n@$((t + 30))"; t=$((t + 270)); done
ons() { cells | tr ' ' '\n' | grep '^3:' | grep -v '=-4' | sed 's/^3://; s/=.*//' | paste -sd' ' -; }
got=$(run j0old "$CURSOR; $ARM_SAMPLE; $START; $(midiseq "$OLDJ0") $STOP; $STEPS" | ons)
expect "D J0 at the old midisend's times: a note every 4 steps on track 3, the 32 step pattern wraps (device j0: 06 0A 0E 12 ...)" "1 5 9 d 11 15 19 1d" "$got"
got=$(run j0ts "$CURSOR; $ARM_SAMPLE; $START; $(midiseq "${TSJ0# }") $STOP; $STEPS" | ons)
expect "D J0 at 270 ms (midisend_ts): every 2 to 3 steps" "5 7 9 c e 10 13 15" "$got"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
