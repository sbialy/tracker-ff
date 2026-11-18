#!/bin/bash
# workspace_backup_sim.sh
#
# Idea 13, previous-save backup (ref/idea-ui.md): "Don't save" on the save-last-project window (File > New Project, File > Open) used to
# delete the Workspace with the work in it. Now the Workspace folder is renamed to Workspace.prev (one generation, the one before is
# replaced) after the pending autosave has finished. Nothing else changes: Cancel, Save and a project without changes behave as before.
#
# Scenarios (each has its own card folder):
#   W1  New Project > Don't save: Workspace.prev holds the old Workspace (marker, project.mt, edited pattern), Workspace is the new project
#   W2  Cancel: no Workspace.prev, the marker stays in the Workspace
#   W3  saved project without changes: no window, no Workspace.prev
#   W4  two discards in a row: one generation, Workspace.prev is the second Workspace
#   W5  Open from the Recent list > Don't save: Workspace.prev holds the discarded work, the Workspace is the opened project
#   W6  Save (not Don't save) then New Project: no Workspace.prev, the edit is in Projects
#   W7  restore by hand (rename Workspace.prev to Workspace on the card) and boot: the discarded edit is back
#   W8  edit still waiting for the autosave when Don't save is pressed: the backup holds it too
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)". SAN=address,undefined: the sanitizer build.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="$BUILD/ui_shot"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/workspace_backup_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
make -s -C "$HERE" -j4 ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() {   # run NAME "script": the lines the script prints (card folder $WORK/NAME, kept between runs of the same NAME)
    mkdir -p "$WORK/$1"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$1" "wait 500; $2" 2>"$WORK/$1.err" | grep -E '^(step|fm) '
}
has() { [ -e "$WORK/$1/$2" ] && echo yes || echo no; }

# the edit: a note on step 2 of track 1, then the autosave has it in the Workspace. MARK puts a marker file into the Workspace
EDIT="tap Pattern; wait 300; setstep 0 2 60; dirty; wait 3000"
EDIT_NOWAIT="tap Pattern; wait 300; setstep 0 2 61; dirty"    # no wait: the 10 s autosave has not run when the window comes up
mark() { printf 'put Workspace/marker%s.txt M%s' "$1" "$1"; }
NEWP="tap File; wait 600; tap F1; wait 600"                   # File > New Project (the save window when there is something to save)
CANCEL="tap F6; wait 600"; DONTSAVE="tap F7; wait 3000"; SAVE="tap F8; wait 4000"
STEP2="tap Pattern; wait 500; step 0 2"
note() { sed -n 's/^step 0 2 note \(-\{0,1\}[0-9]*\) .*/\1/p'; }

echo "-- W1: New Project, Don't save"
run w1 "$EDIT; $(mark A); $NEWP; $DONTSAVE" >/dev/null
expect "W1 Workspace.prev exists"                        "yes" "$(has w1 Workspace.prev)"
expect "W1 the marker moved with it"                     "yes" "$(has w1 Workspace.prev/markerA.txt)"
expect "W1 the old project file is in the backup"        "yes" "$(has w1 Workspace.prev/project.mt)"
expect "W1 the edited pattern is in the backup"          "yes" "$(has w1 Workspace.prev/patterns/pattern_01.mtp)"
expect "W1 the Workspace is the new project (no marker)" "no"  "$(has w1 Workspace/markerA.txt)"
expect "W1 the new Workspace has its project file"       "yes" "$(has w1 Workspace/project.mt)"
if [ -e "$WORK/w1/Workspace.prev/patterns/pattern_01.mtp" ] && [ -e "$WORK/w1/Workspace/patterns/pattern_01.mtp" ]; then
    cmp -s "$WORK/w1/Workspace.prev/patterns/pattern_01.mtp" "$WORK/w1/Workspace/patterns/pattern_01.mtp" && same=same || same=differ
else same=missing; fi
expect "W1 backup pattern differs from the new project's"  "differ" "$same"

echo "-- W2: Cancel"
run w2 "$EDIT; $(mark A); $NEWP; $CANCEL" >/dev/null
expect "W2 no Workspace.prev"                            "no"  "$(has w2 Workspace.prev)"
expect "W2 the Workspace is untouched"                   "yes" "$(has w2 Workspace/markerA.txt)"

echo "-- W3: nothing to save"
run w3 "poke saveproj Beta; wait 4000; $(mark A); $NEWP; wait 3000" >/dev/null
expect "W3 no Workspace.prev"                            "no"  "$(has w3 Workspace.prev)"
expect "W3 the new project replaced the Workspace"       "no"  "$(has w3 Workspace/markerA.txt)"

echo "-- W4: two discards, one generation"
run w4 "$EDIT; $(mark A); $NEWP; $DONTSAVE" >/dev/null
run w4 "$EDIT; $(mark B); $NEWP; $DONTSAVE" >/dev/null
expect "W4 the backup is the second Workspace"           "yes" "$(has w4 Workspace.prev/markerB.txt)"
expect "W4 the first one is gone"                        "no"  "$(has w4 Workspace.prev/markerA.txt)"
expect "W4 no Workspace.prev.prev"                       "no"  "$(has w4 Workspace.prev/Workspace.prev)"

echo "-- W5: Open from the Recent list, Don't save"
run w5 "poke saveproj Alpha; wait 4000; poke saveproj Beta; wait 4000; $EDIT; $(mark A); tap File; wait 600; tap F2; wait 1500; tap Right; wait 500; tap F8; wait 1000; $DONTSAVE; wait 6000" >/dev/null
expect "W5 Workspace.prev exists"                        "yes" "$(has w5 Workspace.prev)"
expect "W5 the discarded work is in it"                  "yes" "$(has w5 Workspace.prev/markerA.txt)"
expect "W5 the opened project's Workspace has no marker" "no"  "$(has w5 Workspace/markerA.txt)"
expect "W5 the opened project's Workspace has its file"  "yes" "$(has w5 Workspace/project.mt)"
expect "W5 the project on the card is untouched (Beta)"  "yes" "$(has w5 Projects/Beta/project.mt)"

echo "-- W6: Save, then New Project"
run w6 "poke saveproj Beta; wait 4000; $EDIT; $NEWP; $SAVE; wait 3000" >/dev/null
expect "W6 no Workspace.prev"                            "no"  "$(has w6 Workspace.prev)"
expect "W6 the edit was saved into the project"          "yes" "$(has w6 Projects/Beta/patterns/pattern_01.mtp)"

echo "-- W7: restore by hand"
rm -rf "$WORK/w1/Workspace" && mv "$WORK/w1/Workspace.prev" "$WORK/w1/Workspace"
expect "W7 the discarded edit is back after the rename"  "60"  "$(run w1 "$STEP2" | note)"

echo "-- W8: the edit has not reached the Workspace yet"
run w8 "$EDIT_NOWAIT; $(mark A); $NEWP; $DONTSAVE" >/dev/null
expect "W8 Workspace.prev exists"                        "yes" "$(has w8 Workspace.prev)"
rm -rf "$WORK/w8/Workspace" && mv "$WORK/w8/Workspace.prev" "$WORK/w8/Workspace"
expect "W8 the late edit is in the backup"               "61"  "$(run w8 "$STEP2" | note)"

echo
if [ "$fails" = 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
