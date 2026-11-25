#!/bin/bash
# perform_pages_sim.sh
#
# The pages of the Perform key (ref/synth-ab-8p8.md): Perform opens Performance page 1 (tracks 1 to 8), a second press page 2 (tracks 9 to 16, stock), a third
# press the Synth play page (the A/B page), a fourth comes back to page 1. Shift + Perform opens the Synth play page from every page except Performance (Shift + Params is the plain Params key). The Performance
# state (FX rows, track states, the mode itself) must survive the trip to the A/B page. Real UI, real firmware code (ui_shot, the host build, no device).
# "poke perfpages" prints what the firmware holds (host_poke.cpp). The screenshots of page 1, page 2, the A/B page and page 1 again go to $OUT (default a
# temp folder, kept when OUT is set), the test checks that the three screens differ and that page 1 after the trip looks like page 1 before it.
#
# Rows:
#   PP1 Perform from the Pattern page: Performance mode, page 1
#   PP2 Perform again: page 2, Perform again: the Synth play page (open), Performance mode still on
#   PP3 Perform on the Synth play page: Performance page 1 (not page 2)
#   PP4 the state: a track toggled on page 1 and on page 2 and a pad row picked are the same after the trip
#   PP5 Shift + Perform in Performance mode still flips the two track pages, it never opens the Synth play page (the stock flip is why Performance is left out of the Shift + Perform entry)
#   PP6 Shift + Perform from the Pattern page opens the Synth play page, Perform leaves it for Performance page 1, Shift + Params opens Params again (Pattern page and Performance mode)
#   PP7 a page key other than Perform, Params and Master on the A/B page turns Performance mode off, Params keeps it
#   PP8 a song playing: the stop confirm on the way in, Cancel comes back to Performance page 2 with the song playing
#   PP9 screens: page 1, page 2 and the A/B page differ, page 1 after the trip equals page 1 before it
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD="$ROOT/build/ui-shot"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
WORK="$(mktemp -d "${TMPDIR:-/tmp}/perform_pages_sim.XXXXXX")"
SHOTS="${OUT:-$WORK/shots}"
[ -n "${OUT:-}" ] || trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD" "$SHOTS"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() {   # run NAME "script": the project starts empty, part 1 is instrument 64
    local n="$1"
    rm -rf "$WORK/$n"; mkdir -p "$WORK/$n"
    UISHOT_POKE="$WORK/$n.poke" "$BIN" "$WORK/$n" "wait 500; instr 64; audio 1; $2" >"$WORK/$n.out" 2>"$WORK/$n.err"
    [ -f "$WORK/$n.poke.out" ] || : >"$WORK/$n.poke.out"
}
# pp NAME K: the Kth "poke perfpages" line of the run, words kept
pp() { grep '^perfpages' "$WORK/$1.poke.out" | sed -n "$2p" | sed 's/^perfpages //'; }
# the fields of one line: screen perfmode page keep, and the tracks and rows strings
f() { echo "$1" | tr ' ' '\n' | awk -v k="$2" '$1==k {getline; print; exit}'; }
PP='poke perfpages'
PERF="tap Pattern; wait 300; tap Performance; wait 500"
NEXT="tap Performance; wait 600"

# ===================================================================== PP1 to PP4
run a "$PERF; $PP; shot $SHOTS/page1.png; $NEXT; $PP; shot $SHOTS/page2.png; $NEXT; $PP; shot $SHOTS/ab.png; $NEXT; $PP; shot $SHOTS/page1_back.png; $NEXT; $PP; $NEXT; $PP; $NEXT; $PP"
expect "PP1 Perform from the Pattern page: Performance mode, page 1" "1 1 0" "$(f "$(pp a 1)" screen) $(f "$(pp a 1)" perfmode) $(f "$(pp a 1)" page)"
expect "PP2 second press: page 2, still Performance mode" "1 1 1" "$(f "$(pp a 2)" screen) $(f "$(pp a 2)" perfmode) $(f "$(pp a 2)" page)"
expect "PP2 third press: the Synth play page, Performance mode kept (keep flag set for the return)" "2 1 1" "$(f "$(pp a 3)" screen) $(f "$(pp a 3)" perfmode) $(f "$(pp a 3)" keep)"
expect "PP3 Perform on the Synth play page: Performance page 1" "1 1 0" "$(f "$(pp a 4)" screen) $(f "$(pp a 4)" perfmode) $(f "$(pp a 4)" page)"
expect "PP3 the cycle repeats: page 2, the Synth play page, page 1 again" "1:1 2:1 1:0" "$(f "$(pp a 5)" screen):$(f "$(pp a 5)" page) $(f "$(pp a 6)" screen):$(f "$(pp a 6)" perfmode) $(f "$(pp a 7)" screen):$(f "$(pp a 7)" page)"

# the state: toggle track 1 on page 1 (button 0) and track 9 on page 2 (button 0 there), pick a pad row, go to the Synth play page and back
run b "poke perffx 2; $PERF; pad 14; wait 200; tap F1; wait 200; $NEXT; tap F2; wait 200; $PP; $NEXT; $PP; $NEXT; $PP; tap Performance; wait 400; $PP"
before=$(pp b 1); away=$(pp b 2); back=$(pp b 3); again=$(pp b 4)
expect "PP4 state: two tracks are toggled before the trip" "yes" "$([ "$(f "$before" tracks | tr -d 0)" != "" ] && echo yes || echo no)"
expect "PP4 state: the track states are the same on the Synth play page and after it" "$(f "$before" tracks)" "$(f "$back" tracks)"
expect "PP4 state: the Synth play page leaves them alone" "$(f "$before" tracks)" "$(f "$away" tracks)"
expect "PP4 state: a pad row is picked (slot 3, row 1) and it is the same after the trip" "777777777777|$(f "$before" rows)" "777777777777|$(f "$back" rows)"
expect "PP4 state: the pick is there" "no" "$([ "$(f "$before" rows)" = "777777777777" ] && echo yes || echo no)"
expect "PP4 state: Performance mode on the whole way" "1 1 1" "$(f "$before" perfmode) $(f "$away" perfmode) $(f "$back" perfmode)"

# ===================================================================== PP5 Shift + Perform
run c "$PERF; $NEXT; $PP; hold Shift; tap Performance; unhold Shift; wait 500; $PP; hold Shift; tap Performance; unhold Shift; wait 500; $PP"
expect "PP5 Shift + Perform on page 2 flips to page 1 (stock), on page 1 to page 2, never the Synth play page" "1:1 1:0 1:1" "$(f "$(pp c 1)" screen):$(f "$(pp c 1)" page) $(f "$(pp c 2)" screen):$(f "$(pp c 2)" page) $(f "$(pp c 3)" screen):$(f "$(pp c 3)" page)"

# ===================================================================== PP6 Shift + Perform opens the page, Shift + Params is stock again
run d "tap Pattern; wait 300; hold Shift; tap Performance; unhold Shift; wait 400; $PP; tap Performance; wait 600; $PP; tap Pattern; wait 400; hold Shift; tap Params; unhold Shift; wait 500; $PP; $PERF; hold Shift; tap Params; unhold Shift; wait 600; $PP"
expect "PP6 Shift + Perform from the Pattern page opens the Synth play page (Performance mode not on)" "2 0 0" "$(f "$(pp d 1)" screen) $(f "$(pp d 1)" perfmode) $(f "$(pp d 1)" keep)"
expect "PP6 Perform on the page leaves it for Performance (page 1)" "1 0" "$(f "$(pp d 2)" screen) $(f "$(pp d 2)" page)"
expect "PP6 Shift + Params from the Pattern page is the plain Params key: another page, not the Synth play page" "0 0" "$(f "$(pp d 3)" screen) $(f "$(pp d 3)" open)"
expect "PP6 Shift + Params from Performance mode is the plain Params key: Params, Performance mode kept, no Synth play page" "0 1 0" "$(f "$(pp d 4)" screen) $(f "$(pp d 4)" perfmode) $(f "$(pp d 4)" open)"

# ===================================================================== PP7 leaving the A/B page for another page
run e "$PERF; $NEXT; $NEXT; $PP; tap Params; wait 600; $PP; $PERF; $NEXT; $NEXT; tap Pattern; wait 600; $PP; tap Performance; wait 400; $PP"
expect "PP7 Params from the A/B page keeps Performance mode (page open: Params, not the Synth play page)" "0 1" "$(f "$(pp e 2)" screen) $(f "$(pp e 2)" perfmode)"
expect "PP7 Pattern from the A/B page turns Performance mode off" "0 0" "$(f "$(pp e 3)" screen) $(f "$(pp e 3)" perfmode)"

# ===================================================================== PP8 a song playing
run g "tap Pattern; wait 300; tap Play; wait 400; $PP; tap Performance; wait 500; $NEXT; $PP; $NEXT; wait 300; poke synthplay; tap F1; wait 200; poke synthplay"
expect "PP8 song playing: the third press asks to stop the song (the page is not open yet)" "1" "$(grep '^synthplay mode' "$WORK/g.poke.out" | head -1 | sed 's/.*confirm //')"

# ===================================================================== PP9 screens
h() { shasum -a 256 "$SHOTS/$1.png" | cut -c1-16; }
expect "PP9 screens: the three screens differ" "3" "$(printf '%s\n' "$(h page1)" "$(h page2)" "$(h ab)" | sort -u | wc -l | tr -d ' ')"
expect "PP9 screens: page 1 after the trip is page 1 before it" "$(h page1)" "$(h page1_back)"
python3 -I "$HERE/png_cmp.py" --check "$SHOTS/page1.png" "$SHOTS/page2.png" "$SHOTS/ab.png" "$SHOTS/page1_back.png" >/dev/null 2>&1; expect "PP9 screens: four readable 800x480 PNGs" "0" "$?"

if [ "$fails" = 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
