#!/bin/bash
# parts_polymeter_sim.sh
#
# features-140: Parts (core/source/parts.cpp) with polymeter sources (idea-polymeter, mtSequencer.h kPolymeterMark). A Part image takes
# its header from the first source pattern. Before the merge fix that copied the polymeter mark and the per-track lengths of the first
# source onto every track, also the tracks that come from another pattern, and the tiling used the source's pattern length.
# Now the image is unmarked and every track is tiled at the length it has in its own source.
#   pattern 1: 8 steps, polymeter, track 2 of 3 steps, track 3 of 5 steps (notes on step 1 of tracks 1..3, MIDI channels 1..3)
#   pattern 2: 8 steps, plain, track 3 has notes on steps 1 and 7
#   Part 0: tracks 1 and 2 from pattern 1, track 3 from pattern 2, length Auto (8)
# Expected while the Part plays: channel 2 every 3 steps from each Part start (0 3 6 8 11 14 ...), channel 3 on 0 6 8 14 ...
# (pattern 2's track at its own 8 steps, not at the 5 of pattern 1's track 3), channel 1 every 8 steps.
# Host only. Ends with "RESULT: GREEN (n checks)" (exit 0) or "RESULT: RED (n failing of m)" (exit 1).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD="$ROOT/build/ui-shot"
BIN="$BUILD/ui_shot"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/parts_polymeter_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
make -s -C "$HERE" -j"${JOBS:-4}" >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() { ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$1" "wait 500; $2" 2>"$WORK/$1.err" | grep -E '^(lens|midi on|parts|part) '; }
# the steps of channel CH counted from the first note of channel 1 (one step = a quarter of the gap between the first two channel 1
# notes over 8 steps), the first N of them
chsteps() {
    awk -v ch="$1" -v n="$2" '$1 == "midi" && $2 == "on" {t[NR] = $8; c[NR] = $6; if ($6 == 1) one[k++] = $8}
        END {ms = (one[1] - one[0]) / 8; m = 0; s = "";
             for (i = 1; i <= NR; i++) if ((i in c) && c[i] == ch && m < n) {s = s (m++ ? " " : "") int((t[i] - one[0]) / ms + 0.5)}
             print s}'
}

trklen() { printf 'hold F2; hold Shift; %s; unhold Shift; unhold F2; wait 200' "$1"; }   # Shift + Length: the cursor track only
SETUP="pattern 2; plen 8; setstep 2 0 64 50 0 0 0 0; setstep 2 6 65 50 0 0 0 0; dirty; pattern 1; plen 8; \
setstep 0 0 60 48 0 0 0 0; setstep 1 0 62 49 0 0 0 0; setstep 2 0 64 50 0 0 0 0; dirty; tap Pattern; wait 1200; \
tap Right; $(trklen 'tap Down 5'); tap Right; $(trklen 'tap Down 3'); lens; dirty; pattern 2; wait 300; pattern 1; wait 300"
PARTS_UI='tap Song; wait 300; hold Shift; tap F8; unhold Shift; wait 200; tap Rec; wait 100; tap Insert; partset 0 src2 2; tap Rec; wait 200; tap F6; wait 100; parts'   # F6 = Loop Song: the song wraps over its one Part (it stops after the last Part otherwise)
PLAY='audio 1; midilog 1; tap Play; wait 3000; tap Play; wait 200'

out=$(run a "$SETUP; $PARTS_UI; $PLAY")
expect "pattern 1 is a polymeter pattern: track 2 has 3 steps, track 3 has 5" "lens mark 1 pattern 8 tracks 8 3 5 8 8 8 8 8 8 8 8 8 8 8 8 8" \
  "$(grep '^lens' <<<"$out" | head -1 | sed 's/ label.*//')"
expect "channel 1 (track 1, pattern 1) every 8 steps" "0 8 16 24" "$(chsteps 1 4 <<<"$out")"
expect "channel 2 (track 2, pattern 1, own length 3) restarts with every Part pass" "0 3 6 8 11 14 16 19" "$(chsteps 2 8 <<<"$out")"
expect "channel 3 (track 3 from pattern 2) plays pattern 2's 8 steps, not the 5 of pattern 1's track 3" "0 6 8 14 16 22" "$(chsteps 3 6 <<<"$out")"

# a start step on a polymeter track: the window is cut from the track's own length. Track 2 of pattern 1 (3 steps) gets a second note on step 3,
# its start is step 2 (partset start1 = track 2): the window is steps 2-3 (2 steps), tiled to the 8 step Part = every 2 steps from Part step 1
SETUP_B="${SETUP/dirty; pattern 2; wait 300; pattern 1; wait 300/setstep 1 2 63 49 0 0 0 0; dirty; pattern 2; wait 300; pattern 1; wait 300}"
out=$(run b "$SETUP_B; $PARTS_UI; partset 0 start1 1; $PLAY")
expect "a start step on the 3 step track 2 (start 2): the 2 step window (step 2 is its second step) tiles every 2 steps over the 8 step Part" "1 3 5 7 9 11" "$(chsteps 2 6 <<<"$out")"
expect "the other tracks do not move (channel 1 every 8, channel 3 as before)" "0 8 16 24|0 6 8 14 16 22" "$(chsteps 1 4 <<<"$out")|$(chsteps 3 6 <<<"$out")"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
