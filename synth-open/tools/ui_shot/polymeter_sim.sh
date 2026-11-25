#!/bin/bash
# polymeter_sim.sh
#
# Per-track pattern length in the pattern editor (polymeter, mtSequencer.h kPolymeterMark). Stock forces every track to the length of
# track 1 and the player reads only that one. With the pattern marked (reserved header byte rezerwa1), every track wraps on its own
# length while the pattern end (song advance, isLastPatternStep) stays on the pattern length.
#
# UI: hold the Length button (F2) and turn the encoder or press Up/Down/Left/Right as before = pattern length. Add Shift = the length of
# the cursor track only. The Length label then reads "track/pattern" (e.g. 3/8) while the cursor is on a track with its own length.
#
# Sections:  P playback (MIDI out log)   U editor UI   C compat (unmarked patterns play as stock, oracle = the base build)
#            S save and reload   D duplicate, expand, shrink   R render (PNG for the report)
# BASE_BIN=path/to/base/ui_shot: the same compat scenarios on the base build must give the same MIDI log (C3), skipped when unset.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)". SAN=address,undefined: the sanitizer build.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
BASE_BIN="${BASE_BIN:-}"
OUT_PNG="${OUT_PNG:-$ROOT/build/polymeter}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/polymeter_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD" "$OUT_PNG"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"${JOBS:-$(sh "$HERE/../jobs.sh" n)}" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() {   # run [BIN=..] SDNAME "script": the probe lines of the run
    local bin="$BIN"; case "$1" in BIN=*) bin="${1#BIN=}"; shift ;; esac
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$bin" "$WORK/$1" "wait 500; $2" 2>"$WORK/$1.err" | grep -E '^(lens|rawlen|midi on|cursor|step) '
}
lens() { grep '^lens' | sed -n "${1:-1}p" | sed 's/^lens //'; }
# the MIDI notes as "ch:step" in time order, steps counted from the first note (STEP_MS = one step at the project tempo)
steps() { grep '^midi on' | awk -v ms="$1" 'NR == 1 {t0 = $8} {printf "%s%s:%d", (n++ ? " " : ""), $6, int(($8 - t0) / ms + 0.5)}'; echo; }
chsteps() { tr ' ' '\n' | grep "^$1:" | cut -d: -f2 | tr '\n' ' ' | sed 's/ $//'; echo; }

GO="tap Pattern; wait 1200"
LEN8="hold F2; tap Left; tap Left; unhold F2; wait 200"                       # 32 -> 16 -> 8 (the defined lengths)
NOTES="setstep 0 0 60 48 0 0 0 0; setstep 1 0 62 49 0 0 0 0; setstep 2 0 64 50 0 0 0 0"   # MIDI 1..3 on tracks 1..3, step 1
trklen() { printf 'hold F2; hold Shift; %s; unhold Shift; unhold F2; wait 200' "$1"; }   # Shift + Length: the cursor track only
PLAY="audio 1; midilog 1; tap Play; wait 3000; tap Play; wait 200"           # 3 s = 26 steps at the default tempo

# step length: tracks 1 and 3 hit every 8 steps in an 8 step pattern, so the first gap of ch 3 is 8 steps
step_ms() { grep '^midi on' | awk '$6 == 3 {t[n++] = $8} END {if (n > 1) printf "%.3f", (t[1] - t[0]) / 8; else print 115.385}'; }
playsteps() { local out; out=$(cat); printf '%s\n' "$out" | steps "$(printf '%s\n' "$out" | step_ms)"; }

echo "-- U: editor"
U=$(run u "$GO; $LEN8; lens; tap Right; $(trklen 'tap Down 5'); lens; tap Left; wait 200; lens; tap Right; wait 200; lens; \
$(trklen 'tap Right'); lens; $(trklen 'tap Up 9'); lens; $(trklen 'tap Down 9'); lens; $(trklen 'tap Up'); lens; $(trklen 'tap Left; tap Left'); lens")
expect "U1 pattern length 8 with the Length button, unmarked" "mark 0 pattern 8 tracks 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 label 8" "$(printf '%s\n' "$U" | lens 1)"
expect "U2 Shift + Length + Down 5 on track 2: 3 steps, marked, label 3/8" "mark 1 pattern 8 tracks 8 3 8 8 8 8 8 8 8 8 8 8 8 8 8 8 label 3/8" "$(printf '%s\n' "$U" | lens 2)"
expect "U3 cursor back on track 1: label shows the pattern length" "label 8" "$(printf '%s\n' "$U" | lens 3 | grep -o 'label .*')"
expect "U4 cursor on track 2 again: label 3/8" "label 3/8" "$(printf '%s\n' "$U" | lens 4 | grep -o 'label .*')"
expect "U5 Shift + Length + Right: next defined length 4" "tracks 8 4 8" "$(printf '%s\n' "$U" | lens 5 | grep -o 'tracks [0-9]* [0-9]* [0-9]*')"
expect "U6 Shift + Length + Up 9: clamped to the pattern length, mark cleared again" "mark 0 pattern 8 tracks 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 label 8" "$(printf '%s\n' "$U" | lens 6)"
expect "U7 Shift + Length + Down 9: at least 1 step" "tracks 8 1 8" "$(printf '%s\n' "$U" | lens 7 | grep -o 'tracks [0-9]* [0-9]* [0-9]*')"
expect "U8 Shift + Length + Up: 2 steps" "tracks 8 2 8" "$(printf '%s\n' "$U" | lens 8 | grep -o 'tracks [0-9]* [0-9]* [0-9]*')"
expect "U9 Shift + Length + Left twice: 1 (no defined length below)" "tracks 8 1 8" "$(printf '%s\n' "$U" | lens 9 | grep -o 'tracks [0-9]* [0-9]* [0-9]*')"

V=$(run v "$GO; $LEN8; tap Right; $(trklen 'tap Down 5'); hold F2; tap Right; unhold F2; wait 200; lens; hold F2; tap Down 14; unhold F2; wait 200; lens; \
hold F2; tap Up 2; unhold F2; wait 200; lens; tap Right; $(trklen 'tap Down'); tap Left 2; $(trklen 'tap Down 2'); lens; rawlen")
expect "U10 pattern 8 -> 16 (Length + Right): full tracks follow, track 2 keeps 3" "mark 1 pattern 16 tracks 16 3 16 16 16 16 16 16 16 16 16 16 16 16 16 16 label 3/16" "$(printf '%s\n' "$V" | lens 1)"
expect "U11 pattern 16 -> 2 (Length + Down 14): track 2 clamped to 2, mark cleared" "mark 0 pattern 2 tracks 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 label 2" "$(printf '%s\n' "$V" | lens 2)"
expect "U12 pattern 2 -> 4" "mark 0 pattern 4 tracks 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 label 4" "$(printf '%s\n' "$V" | lens 3)"
expect "U13 track 1 own length (header byte) 2, track 3 length 3" "mark 1 pattern 4 tracks 2 4 3 4 4 4 4 4 4 4 4 4 4 4 4 4 label 2/4" "$(printf '%s\n' "$V" | lens 4)"
expect "U14 stored bytes: mark 'P' (80), track 1 in rezerwa2 (last + 1 = 2), pattern stamp in rezerwa3 (last + 2 = 5), track 3 length field 2 (last step)" "rawlen r1 80 r2 2 r3 5 3 3 2 3 3 3 3 3 3 3 3 3 3 3 3 3" "$(printf '%s\n' "$V" | grep '^rawlen')"

echo "-- P: playback (MIDI out, notes on step 1 of tracks 1-3 on MIDI channels 1-3, 8 step pattern)"
P1=$(run p1 "$GO; $LEN8; $NOTES; tap Right; $(trklen 'tap Down 5'); $PLAY" | playsteps)
expect "P1 track 2 at 3 steps: channel 2 every 3 steps" "0 3 6 9 12 15 18 21 24" "$(printf '%s\n' "$P1" | chsteps 2)"
expect "P1 track 1 at the pattern length: every 8" "0 8 16 24" "$(printf '%s\n' "$P1" | chsteps 1)"
expect "P1 track 3 at the pattern length: every 8" "0 8 16 24" "$(printf '%s\n' "$P1" | chsteps 3)"
P2=$(run p2 "$GO; $LEN8; $NOTES; $(trklen 'tap Down 3'); $PLAY" | playsteps)
expect "P2 track 1 at 5 steps (its length in the header byte): channel 1 every 5" "0 5 10 15 20 25" "$(printf '%s\n' "$P2" | chsteps 1)"
expect "P2 tracks 2 and 3 stay at 8" "0 8 16 24|0 8 16 24" "$(printf '%s\n' "$P2" | chsteps 2)|$(printf '%s\n' "$P2" | chsteps 3)"
P3=$(run p3 "$GO; $LEN8; $NOTES; tap Right; $(trklen 'tap Down 5'); $(trklen 'tap Up 5'); lens; $PLAY")
expect "P3 track 2 back at 8: unmarked again" "mark 0 pattern 8 tracks 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 label 8" "$(printf '%s\n' "$P3" | lens)"
expect "P3 plays as stock: channel 2 every 8" "0 8 16 24" "$(printf '%s\n' "$P3" | playsteps | chsteps 2)"
# a note on step 3 of a 3 step track 2 and on step 5 (past its end): step 5 never plays
P4=$(run p4 "$GO; $LEN8; setstep 1 2 62 49 0 0 0 0; setstep 1 4 65 49 0 0 0 0; setstep 2 0 64 50 0 0 0 0; tap Right; $(trklen 'tap Down 5'); $PLAY")
expect "P4 a note past the track's end stays silent (only note 62+24 plays on channel 2)" "86" "$(printf '%s\n' "$P4" | grep '^midi on' | awk '$6 == 2 {print $3}' | sort -u | tr '\n' ' ' | sed 's/ $//')"

echo "-- C: compat (unmarked patterns)"
# stock files can hold stale lengths in tracks 9-16 (the stock length edit syncs tracks 2-8 only): seed them, unmarked, they must not count
CSEED="$GO; $LEN8; $NOTES; setstep 9 0 67 51 0 0 0 0; rawlen 9 2; rawlen 2 4; rawlen 15 0"
C1=$(run c1 "$CSEED; lens; $PLAY")
expect "C1 stale lengths, no mark: every track plays the pattern length" "mark 0 pattern 8 tracks 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 label 8" "$(printf '%s\n' "$C1" | lens)"
expect "C1 plays as stock: track 10 (stored length 3) every 8" "0 8 16 24" "$(printf '%s\n' "$C1" | playsteps | chsteps 4)"
expect "C1 plays as stock: track 3 (stored length 5) every 8" "0 8 16 24" "$(printf '%s\n' "$C1" | playsteps | chsteps 3)"
C2=$(run c2 "$CSEED; tap Right; $(trklen 'tap Down 5'); lens")
expect "C2 the first Shift edit resets the stale lengths: only track 2 differs" "mark 1 pattern 8 tracks 8 3 8 8 8 8 8 8 8 8 8 8 8 8 8 8 label 3/8" "$(printf '%s\n' "$C2" | lens)"
if [ -n "$BASE_BIN" ]; then
    for sc in "c3a|$CSEED; $PLAY" "c3b|$GO; $LEN8; $NOTES; $PLAY" "c3c|$GO; $NOTES; hold F2; tap Down 3; unhold F2; wait 200; $PLAY"; do
        name="${sc%%|*}"; scr="${sc#*|}"
        new=$(run "$name-new" "$scr" | grep '^midi on')
        old=$(run BIN="$BASE_BIN" "$name-base" "$scr" | grep '^midi on')
        expect "C3 $name: MIDI log identical to the base build ($(printf '%s\n' "$old" | grep -c .) notes)" "$old" "$new"
    done
    # 1.3.0 style round trip: polymeter saved here, the base build (no polymeter) changes the pattern length (it syncs tracks 2-8 only),
    # back here the stale stamp drops the mark and every track follows the pattern again
    run c4a "$GO; $LEN8; $NOTES; setstep 1 4 65 49 0 0 0 0; tap Right; $(trklen 'tap Down 5'); wait 3000; dirty; wait 8000" >/dev/null
    C4B=$(run BIN="$BASE_BIN" c4a "wait 1000; $GO; $PLAY; hold F2; tap Up; unhold F2; wait 3000; dirty; wait 8000")
    expect "C4 the base build ignores the mark: channel 2 every 8 steps and step 5 (past the end here) plays" "86 89 86 89 86 89 86" \
        "$(printf '%s\n' "$C4B" | grep '^midi on' | awk '$6 == 2 {print $3}' | tr '\n' ' ' | sed 's/ $//')"
    expect "C4 back here after the base build's length edit: stale stamp, unmarked, every track 9" "mark 0 pattern 9 tracks 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 label 9" \
        "$(run c4a "wait 1000; $GO; lens" | lens)"
else
    echo "skip  C3 C4 (BASE_BIN not set)"
fi

echo "-- S: save and reload"
S1=$(run s "$GO; $LEN8; tap Right; $(trklen 'tap Down 5'); tap Right; $(trklen 'tap Down 2'); wait 3000; dirty; wait 8000; lens")
S2=$(run s "wait 1500; $GO; lens; rawlen")
expect "S1 lengths after the edit" "mark 1 pattern 8 tracks 8 3 6" "$(printf '%s\n' "$S1" | lens | grep -o 'mark.*tracks [0-9]* [0-9]* [0-9]*')"
expect "S2 the same after a restart from the Workspace" "mark 1 pattern 8 tracks 8 3 6" "$(printf '%s\n' "$S2" | lens | grep -o 'mark.*tracks [0-9]* [0-9]* [0-9]*')"

echo "-- D: duplicate, expand, shrink"
D=$(run d "$GO; $LEN8; tap Right; $(trklen 'tap Down 5'); setstep 1 0 62 49 0 0 0 0; setstep 1 2 63 49 0 0 0 0; patop dup; lens; rawlen; \
patop shr; lens; patop exp; lens; $(trklen 'tap Up 20'); patop dup; lens")
expect "D1 duplicate: pattern 16, track 2 doubles to 6" "mark 1 pattern 16 tracks 16 6 16" "$(printf '%s\n' "$D" | lens 1 | grep -o 'mark.*tracks [0-9]* [0-9]* [0-9]*')"
expect "D2 shrink: pattern 8, track 2 back to 3" "mark 1 pattern 8 tracks 8 3 8" "$(printf '%s\n' "$D" | lens 2 | grep -o 'mark.*tracks [0-9]* [0-9]* [0-9]*')"
expect "D3 expand: pattern 16, track 2 at 6" "mark 1 pattern 16 tracks 16 6 16" "$(printf '%s\n' "$D" | lens 3 | grep -o 'mark.*tracks [0-9]* [0-9]* [0-9]*')"
expect "D4 duplicate with every track full: stock (unmarked, pattern 32)" "mark 0 pattern 32 tracks 32 32 32" "$(printf '%s\n' "$D" | lens 4 | grep -o 'mark.*tracks [0-9]* [0-9]* [0-9]*')"
DS=$(run d2 "$GO; $LEN8; tap Right; $(trklen 'tap Down 5'); setstep 1 0 62 49 0 0 0 0; setstep 1 2 63 49 0 0 0 0; patop dup; step 1 3; step 1 5; step 1 8")
expect "D5 duplicate copies the 3 own steps of track 2 to steps 4-6, step 9 stays empty" "62 63 -1" "$(printf '%s\n' "$DS" | grep '^step' | awk '{printf "%s%s", (n++ ? " " : ""), $5} END {print ""}')"

echo "-- R: render"
run r "$GO; $LEN8; $NOTES; setstep 1 4 65 49 0 0 0 0; tap Right; $(trklen 'tap Down 5'); tap Right; $(trklen 'tap Down 2'); tap Left; wait 300; shot $OUT_PNG/polymeter_editor.png" >/dev/null
expect "R1 editor PNG written" "yes" "$([ -s "$OUT_PNG/polymeter_editor.png" ] && echo yes || echo no)"

if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
