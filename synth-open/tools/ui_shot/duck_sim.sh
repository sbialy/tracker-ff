#!/bin/bash
# duck_sim.sh [PNG_DIR]
#
# Send ducking on the real UI, plugin host and file code of this tree (ui_shot, the PLUG_HOST configuration) on the Mac: the page, the editing, the
# project sidecar and the rules that keep older projects and older firmware safe. Host only, no device, no audio (the DSP is in tools/duck_test.sh).
# Every scenario has its own SD card folder. Master > F4 is the Limiter screen, F6 on it opens the ducking page (a second page of the same screen,
# F6 on the page goes back). Places on the page: F1 depth, F2 release, F3 threshold, F5 source (a second press steps the list).
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)". Variables: PLG_DIR (folder with the Master .plg files, optional here).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD="$ROOT/build/ui-shot"
BIN="$BUILD/ui_shot"
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/duck_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
[ -x "$BIN" ] || { echo "duck_sim.sh: build first (make -C synth-open/tools/ui_shot)" >&2; exit 2; }

fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
check() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected: $2
      got:      $3"; fi; }

stage() {   # a card with the Workspace of a first power up
    local sd="$1"
    rm -rf "$sd"; mkdir -p "$sd/Plugins/Master" "$sd/Plugins/Synth"
    "$BIN" "$sd" "wait 100" >/dev/null 2>&1 || { echo "priming run failed"; exit 2; }
}

run() {   # run SD SCRIPT -> $WORK/poke.out
    rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$BIN" "$1" "$2" >"$WORK/out.txt" 2>"$WORK/err.txt" || { echo "ui_shot failed:"; tail -5 "$WORK/err.txt"; fails=$((fails+1)); }
    [ -f "$WORK/poke.out" ] || : >"$WORK/poke.out"
}
duck_lines() { grep '^duck ' "$WORK/poke.out"; }
nth_duck() { duck_lines | sed -n "${1}p"; }
png() { [ -n "$PNG_DIR" ] && echo "shot $PNG_DIR/duck_$1.png; "; }

OPEN='tap Master; wait 800; tap F4; wait 600'
DUCKPAGE="$OPEN; tap F6; wait 600"

echo "### scenario 1: a project without a sidecar: ducking is off, the page opens on its defaults, and a save writes no file"
sd="$WORK/s1"; stage "$sd"
run "$sd" "$OPEN; poke duck; $(png limiter)tap F6; wait 600; poke duck; $(png page_off)poke projsave"
check "Limiter screen first (the screen of 1.3.0), engine ducking off" "duck engine=0,60,50,50 sidecar=0,0,0,0 gain=1.0000 page=limiter place=0" "$(nth_duck 1)"
check "F6 opens the ducking page, nothing is on yet" "duck engine=0,60,50,50 sidecar=0,0,0,0 gain=1.0000 page=duck place=0" "$(nth_duck 2)"
check "the save wrote no plugins.txt and no duck key (a project that never used ducking has neither)" "no plugins.txt, no duck key" "$([ -f "$sd/Workspace/plugins.txt" ] && echo "plugins.txt: $(cat "$sd/Workspace/plugins.txt")" || { grep -q "^duck" "$sd/Workspace/project.mtff" 2>/dev/null && echo "duck key" || echo "no plugins.txt, no duck key"; })"

echo "### scenario 2: pick a source, set the three values, save: project.mtff holds one duck.send line, plugins.txt does not exist"
sd="$WORK/s2"; stage "$sd"
# F5 selects the source place, a second F5 steps the list to the first source (track 1). F1 depth +10, F2 release -20, F3 threshold +5
run "$sd" "$DUCKPAGE; tap F5; tap F5; poke duck; tap F1; enc 10; tap F2; enc -20; tap F3; enc 5; wait 300; poke duck; $(png page_on)poke projsave; poke duck"
check "F5 F5: source 1" "duck engine=1,60,50,50 sidecar=1,60,50,50 gain=1.0000 page=duck place=4" "$(nth_duck 1)"
check "depth 70, release 30, threshold 55 (the encoder steps are the values)" "duck engine=1,70,30,55 sidecar=1,70,30,55 gain=1.0000 page=duck place=2" "$(nth_duck 2)"
check "project.mtff after the save: one duck.send line" "duck.send 1 70 30 55" "$(grep '^duck' "$sd/Workspace/project.mtff" 2>/dev/null)"
check "and no plugins.txt (no plugin, no mixer stage: the plugin sidecar has nothing to say)" "absent" "$([ -e "$sd/Workspace/plugins.txt" ] && echo present || echo absent)"

echo "### scenario 3: the next power up reads project.mtff: the settings are back, the page shows them"
run "$sd" "wait 300; poke duck; $OPEN; tap F6; wait 600; poke duck; $(png page_reloaded)wait 10"
check "engine holds the saved values after the project load" "duck engine=1,70,30,55 sidecar=1,70,30,55 gain=1.0000 page=none place=0" "$(nth_duck 1)"
check "and the page opens on them" "duck engine=1,70,30,55 sidecar=1,70,30,55 gain=1.0000 page=duck place=0" "$(nth_duck 2)"

echo "### scenario 4: a project saved by 1.3.0 (a sidecar with a plugin and a mixer line, no duck line): ducking off, the file is not changed by the save"
sd="$WORK/s4"; stage "$sd"
OLD="tffplug 1
format 2
mixer 10
slot 1 92b586d4 00010000 Dirt
p Type=2
p Drive=62
p Mix=100"
printf '%s\n' "$OLD" > "$sd/Workspace/plugins.txt"
run "$sd" "wait 300; poke duck; poke projsave; poke duck"
check "no duck line: ducking off with the defaults ready" "duck engine=0,60,50,50 sidecar=0,0,0,0 gain=1.0000 page=none place=0" "$(nth_duck 1)"
check "the file after a save is byte for byte the file before (no duck line appears)" "$OLD" "$(cat "$sd/Workspace/plugins.txt")"

echo "### scenario 5: ducking and a plugin: the duck key is in project.mtff, plugins.txt is not touched, then ducking off again: the key goes"
sd="$WORK/s5"; stage "$sd"
printf '%s\n' "$OLD" > "$sd/Workspace/plugins.txt"
# source 1, save, then F5 to the source place and the encoder down to 0 (off), save
run "$sd" "$DUCKPAGE; tap F5; tap F5; poke projsave; cat Workspace/project.mtff; enc -1; poke duck; poke projsave; cat Workspace/project.mtff"
check "with a plugin in plugins.txt the duck key is added to project.mtff (once: the second save has none)" "duck.send 1 60 50 50" "$(grep '^duck' "$WORK/out.txt")"
check "source set back to off: engine off, depth keeps" "duck engine=0,60,50,50 sidecar=0,60,50,50 gain=1.0000 page=duck place=4" "$(nth_duck 1)"
check "plugins.txt is the original after both saves" "$OLD" "$(cat "$sd/Workspace/plugins.txt")"

echo "### scenario 6: duck page and limiter page do not mix their values, the limiter page and Back work as in 1.3.0"
sd="$WORK/s6"; stage "$sd"
run "$sd" "$DUCKPAGE; tap F1; enc 25; tap F6; wait 400; poke duck; tap F1; enc 5; wait 300; poke duck; $(png limiter_after)tap F8; wait 600; poke duck"
check "depth 85 on the duck page, F6 back to the limiter page" "duck engine=0,85,50,50 sidecar=0,85,50,50 gain=1.0000 page=limiter place=0" "$(nth_duck 1)"
check "the encoder on the limiter page changes the limiter, not the duck depth" "duck engine=0,85,50,50 sidecar=0,85,50,50 gain=1.0000 page=limiter place=0" "$(nth_duck 2)"
check "F8 (Back) leaves the screen" "duck engine=0,85,50,50 sidecar=0,85,50,50 gain=1.0000 page=none place=0" "$(nth_duck 3)"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
