#!/bin/bash
# projset_sim.sh [BIN]
#
# The Project Settings page (File screen F7, core/interface/configEditor/configMenu.cpp, fConfigEdit.cpp) on the real UI of this tree
# (ui_shot) over a card folder on the Mac, and the Macro depth project setting behind it (project.mtff "macro.mode", plug_mtff.h,
# synth-open/open_cold.cpp projectMacroMode):
#   1  the screens: the File entry, every row selected and its editor, On and Off, Per patch / Add / Scale, byte equal to goldens/projset/
#   2  the rows act: 4 FX Slots, Strum volume, Random seed, Reroll seed, Scale snap, Macro depth
#   3  the Config screen no longer lists Scale snap, Random seed and Reroll seed, and the page is not reachable from the Config button
#   4  project.mtff: the key is written only for Add and Scale, a reopen restores it, a bad value reads as no key, a stale stamp keeps it
#   5  the add paths: an engine change, Init and a library load take the project mode, a patch that is already in the project does not
#      change when the setting changes, with no key everything is as before (Init and a new engine are Scale, a file keeps its own mode)
#   6  a project open, a Save As and a new project carry the setting per project
# MTFF_UPDATE is not used here: PROJSET_UPDATE=1 writes the goldens, PROJSET_PNG_DIR=DIR also copies every screenshot.
# Host only: no device, no audio, virtual clock. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="${1:-$ROOT/build/ui-shot/ui_shot}"
[ -x "$BIN" ] || { echo "projset_sim.sh: no $BIN (ui_shot.sh pattern builds it)" >&2; exit 2; }
GOLD="$HERE/goldens/projset"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/projset_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
check() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected: $2
      got:      $3"; fi; }
run() {
    rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$BIN" "$1" "$2" >"$WORK/out.txt" 2>"$WORK/err.txt" || { echo "ui_shot failed:"; tail -5 "$WORK/err.txt"; fails=$((fails+1)); }
    [ -f "$WORK/poke.out" ] || : >"$WORK/poke.out"
}
fresh() { rm -rf "$1"; mkdir -p "$1/Plugins/Master" "$1/Plugins/Synth"; }
mode() { grep '^macromode ' "$WORK/poke.out" | tail -1 | awk '{print $2}'; }
modes() { grep '^macromode ' "$WORK/poke.out" | awk '{printf "%s ", $2}' | sed 's/ $//'; }
pmodes() { grep '^patchmode ' "$WORK/poke.out" | awk '{printf "%s ", $4}' | sed 's/ $//'; }
keyline() { grep '^macro.mode' "$1/Workspace/project.mtff" 2>/dev/null | tr '\n' '|' | sed 's/|$//'; }
golden() {
    local name="$1" shot="$2"
    [ -s "$shot" ] || { fail "golden $name" "no screenshot taken"; return; }
    [ -z "${PROJSET_PNG_DIR:-}" ] || { mkdir -p "$PROJSET_PNG_DIR"; cp "$shot" "$PROJSET_PNG_DIR/$name.png"; }
    if [ -n "${PROJSET_UPDATE:-}" ]; then mkdir -p "$GOLD"; cp "$shot" "$GOLD/$name.png"; pass "golden $name written"; return; fi
    if cmp -s "$shot" "$GOLD/$name.png"; then pass "golden $name: the screen is byte equal to goldens/projset/$name.png"
    else fail "golden $name" "differs from goldens/projset/$name.png (PROJSET_UPDATE=1 rewrites it)"; fi
}

# the page: File F7. Rows: 0 4 FX Slots, 1 Macro depth, 2 Strum volume, 3 Random seed, 4 Reroll seed, 5 Scale snap. F8 opens a row, enc picks, F8 applies.
PAGE='tap File; wait 800; tap F7; wait 800'
S="$WORK/shots"; mkdir -p "$S"
sh() { echo "shot $S/$1.png"; }

echo "### 1: the screens"
sd="$WORK/s1"; fresh "$sd"
run "$sd" "wait 500; tap File; wait 800; $(sh file); tap F7; wait 800; $(sh row-fx-slots); tap F8; wait 300; $(sh fx-slots-editor); enc 1; $(sh fx-slots-editor-on); tap F8; wait 300; fx34state; \
tap F8; wait 300; enc -1; tap F8; wait 300; fx34state"
check "fx slots: on then off again" "fx34state on 1 n 0 parked 0 refused 0|fx34state on 0 n 0 parked 0 refused 0" "$(grep '^fx34state' "$WORK/out.txt" | paste -sd'|' -)"
for n in fx-slots-editor fx-slots-editor-on; do golden "$n" "$S/$n.png"; done
# the popups of the switch stay on the screen of the page (the notice of fx34io), so the rows are shot in a run of their own with the switch left alone
sd="$WORK/s1b"; fresh "$sd"
run "$sd" "wait 500; tap File; wait 800; tap F7; wait 800; tap F8; wait 300; enc 1; tap F8; wait 300; $(sh fx-slots-on); tap F8; wait 300; enc -1; tap F8; wait 300; $(sh fx-slots-off)"
golden fx-slots-on "$S/fx-slots-on.png"
golden fx-slots-off "$S/fx-slots-off.png"
sd="$WORK/s1c"; fresh "$sd"
run "$sd" "wait 500; tap File; wait 800; tap F7; wait 800; \
tap Down; $(sh row-macro-depth); tap F8; wait 300; $(sh macro-depth-editor); enc 1; $(sh macro-depth-editor-add); tap F8; wait 300; $(sh macro-depth-add); poke macromode; \
tap F8; wait 300; enc 1; tap F8; wait 300; $(sh macro-depth-scale); poke macromode; \
tap F8; wait 300; enc -2; tap F8; wait 300; $(sh macro-depth-per-patch); poke macromode; \
tap Down; $(sh row-strum-volume); tap F8; wait 300; $(sh strum-volume-editor); enc -3; tap F8; wait 300; $(sh strum-volume-70); poke strumdepth; \
tap Down; $(sh row-random-seed); tap F8; wait 300; $(sh random-seed-editor); enc 3; enc 2; tap F8; wait 300; $(sh random-seed-5); seed; \
tap Down; $(sh row-reroll-seed); \
tap Down; $(sh row-scale-snap); tap F8; wait 300; $(sh scale-snap-list); tap Down 2; tap F8; wait 300; $(sh scale-snap-editor); enc 1; tap F8; wait 300; $(sh scale-snap-track3); snap; \
tap F7; wait 300; $(sh scale-snap-back); tap F7; wait 800; $(sh file-after-back)"
check "macro depth list: Add, Scale, Per patch" "macromode 1|macromode 2|macromode 0" "$(grep '^macromode ' "$WORK/poke.out" | paste -sd'|' -)"
check "strum volume list: 100 down three steps is 70" "strumdepth 70" "$(grep '^strumdepth ' "$WORK/poke.out")"
check "random seed list: five steps from Off is seed 5" "seed 5" "$(grep '^seed ' "$WORK/out.txt" | head -1)"
check "scale snap: track 3 is Nearest" "snap 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0" "$(grep '^snap' "$WORK/out.txt" | head -1)"
for n in file row-fx-slots row-macro-depth macro-depth-editor macro-depth-editor-add macro-depth-add \
         macro-depth-scale macro-depth-per-patch row-strum-volume strum-volume-editor strum-volume-70 row-random-seed random-seed-editor random-seed-5 \
         row-reroll-seed row-scale-snap scale-snap-list scale-snap-editor scale-snap-track3 scale-snap-back file-after-back; do
    golden "$n" "$S/$n.png"
done
check "the Back of the page (F7) returns to the File screen" "same" "$(cmp -s "$S/file.png" "$S/file-after-back.png" && echo same || echo changed)"

echo "### 2: the rows act (and the Reroll row)"
sd="$WORK/s2"; fresh "$sd"
run "$sd" "wait 500; $PAGE; tap Down 4; tap F8; wait 300; seed; tap F8; wait 300; seed"
r1="$(grep '^seed ' "$WORK/out.txt" | sed -n 1p | cut -d' ' -f2)"; r2="$(grep '^seed ' "$WORK/out.txt" | sed -n 2p | cut -d' ' -f2)"
check "Reroll seed gives a seed 1..255 on each press" "yes" "$([ -n "$r1" ] && [ -n "$r2" ] && [ "$r1" -ge 1 ] && [ "$r2" -le 255 ] && echo yes || echo "no ($r1 $r2)")"
run "$sd" "wait 500; tap File; wait 800; tap F7; wait 800; tap Down 5; tap F8; wait 300; tap F7; wait 300; tap F7; wait 800; poke strumdepth"
check "the Scale snap list opens and Back returns to the page and then to the File screen" "strumdepth 100" "$(grep '^strumdepth ' "$WORK/poke.out")"

echo "### 3: the Config screen"
sd="$WORK/s3"; fresh "$sd"
run "$sd" "wait 500; tap Config; wait 800; $(sh config-base); tap Right; wait 300; $(sh config-project-settings); tap Left; wait 200; tap Down 5; wait 300; $(sh config-after-down5)"
golden config-base "$S/config-base.png"
golden config-project-settings "$S/config-project-settings.png"
golden config-after-down5 "$S/config-after-down5.png"
run "$sd" "wait 500; $PAGE; tap Config; wait 500; $(sh page-config-button)"
golden page-config-button "$S/page-config-button.png"
check "the Config button on the page leaves the page as it was" "same" "$(cmp -s "$S/page-config-button.png" "$S/row-fx-slots.png" && echo same || echo changed)"

echo "### 4: project.mtff macro.mode"
sd="$WORK/s4"; fresh "$sd"
run "$sd" "wait 500; poke macromode; dirty; wait 15000"
check "a new project: Per patch, no key written" "0|" "$(mode)|$(keyline "$sd")"
run "$sd" "wait 500; $PAGE; tap Down; tap F8; enc 1; tap F8; wait 300; poke macromode; wait 15000"
check "Add: the page sets it and the Workspace save writes macro.mode 0" "1|macro.mode 0" "$(mode)|$(keyline "$sd")"
run "$sd" "wait 500; poke macromode"
check "a power up (Workspace load) restores Add" "1" "$(mode)"
run "$sd" "wait 500; $PAGE; tap Down; tap F8; enc 1; tap F8; wait 300; poke macromode; wait 15000"
check "Scale: macro.mode 1" "2|macro.mode 1" "$(mode)|$(keyline "$sd")"
run "$sd" "wait 500; poke macromode"
check "a power up restores Scale" "2" "$(mode)"
run "$sd" "wait 500; $PAGE; tap Down; tap F8; enc -2; tap F8; wait 300; poke macromode; wait 15000"
check "Per patch again: the key is gone" "0|" "$(mode)|$(keyline "$sd")"
HDRL="$(head -1 "$sd/Workspace/project.mtff")"
for bad in 'macro.mode 7' 'macro.mode x' 'macro.mode' 'macro.mode 1 2' 'macro.mode -1'; do
    printf 'tffproj 1\n%s\nx.keep me\n' "$bad" >"$sd/Workspace/project.mtff"
    run "$sd" "wait 500; poke macromode"
    check "\"$bad\" reads as no key" "0" "$(mode)"
done
printf 'tffproj 1\nmacro.mode 1\nx.keep me\n' >"$sd/Workspace/project.mtff"
run "$sd" "wait 500; poke macromode; $PAGE; tap Down 2; tap F8; enc -1; tap F8; wait 15000"
check "a bare header with the key applies, and an unknown line survives a save" "2|macro.mode 1|x.keep me" "$(mode)|$(keyline "$sd")|$(grep '^x.keep' "$sd/Workspace/project.mtff")"
printf 'tffproj 1\nmt 12345678 2324\nmacro.mode 0\nx.future 5\n' >"$sd/Workspace/project.mtff"
run "$sd" "wait 500; poke macromode"
check "a stale stamp: the project-wide key still applies" "1" "$(mode)"
printf 'tffproj 2 ff=1.5.0 build=cafe123 cap=40000\nmacro.mode 1\n' >"$sd/Workspace/project.mtff"
cp "$sd/Workspace/project.mtff" "$WORK/newer.keep"
run "$sd" "wait 500; poke macromode; $PAGE; tap Down; tap F8; enc 1; tap F8; wait 15000"
check "a newer format: nothing applied (Per patch at load), the file stays byte for byte" "same" "$(cmp -s "$sd/Workspace/project.mtff" "$WORK/newer.keep" && echo same || echo changed)"
check "a newer format: the mode read at the load was Per patch" "0" "$(grep '^macromode ' "$WORK/poke.out" | head -1 | awk '{print $2}')"

echo "### 5: the add paths"
# the patch library is made once (Patches folder), every run below starts from a fresh card with a copy of it: no patch of the project to carry over
LIB="$WORK/lib"; fresh "$LIB"
run "$LIB" "wait 500; poke macromode 1; poke synthlib engine 0 1; poke patchmode 0; poke synthlib save 0 PAdd; poke macromode 2; poke synthlib engine 0 2; poke patchmode 0; poke synthlib save 0 PScale"
check "the library files: Add saved under an Add project, Scale under a Scale project" "add scale" "$(pmodes)"
card5() { sd="$WORK/c5$1"; fresh "$sd"; cp -R "$LIB/Patches" "$sd/Patches"; }
# no key: a new engine and Init give Scale (the way it always was), a library file keeps its own mode
card5 a
# (a library file is found under the folder of its engine: Patches/<engine>/<name>, so the part holds that engine when it loads it)
run "$sd" "wait 500; poke macromode; poke synthlib engine 0 1; poke patchmode 0; poke synthlib init 0; poke patchmode 0; poke synthlib engine 0 2; poke patchmode 0; poke synthlib engine 0 1; poke synthlib load 0 PAdd; poke patchmode 0; poke synthlib engine 0 2; poke synthlib load 0 PScale; poke patchmode 0"
check "no key: engine change, Init and the next engine are Scale, a library file keeps the mode it was saved with" "0|scale scale scale add scale" "$(mode)|$(pmodes)"
card5 b
run "$sd" "wait 500; poke macromode 0; poke synthlib engine 0 2; poke patchmode 0; poke macromode 1; poke synthlib load 0 PScale; poke patchmode 0; poke macromode 0; poke synthlib engine 0 1; poke patchmode 0; poke macromode 1; poke synthlib engine 0 2; poke patchmode 0"
check "Add project: a Scale file and a new engine become Add (each patch was Scale before)" "scale add scale add" "$(pmodes)"
card5 c
run "$sd" "wait 500; poke macromode 0; poke synthlib engine 0 1; poke synthlib load 0 PAdd; poke patchmode 0; poke macromode 2; poke synthlib load 0 PAdd; poke patchmode 0; poke macromode 1; poke synthlib init 0; poke patchmode 0; poke macromode 2; poke synthlib init 0; poke patchmode 0; poke macromode 1; poke synthlib engine 0 2; poke patchmode 0; poke macromode 2; poke synthlib engine 0 1; poke patchmode 0"
check "Scale and Add project: an Add file, Init and a new engine follow the project, each patch was the other mode before" "add scale add scale add scale" "$(pmodes)"
# patches already in the project do not change when the setting changes
card5 d
run "$sd" "wait 500; poke macromode 1; poke synthlib engine 0 1; poke synthlib engine 1 1; poke macromode 2; poke patchmode 0; poke patchmode 1; poke macromode 0; poke patchmode 0; poke patchmode 1; poke macromode 1; poke patchmode 0; poke patchmode 1"
check "changing the project setting leaves the patches already in the project as they are" "add add add add add add" "$(pmodes)"
# the page row does the same as the poke
card5 e
run "$sd" "wait 500; poke macromode 0; poke synthlib engine 0 1; $PAGE; tap Down; tap F8; enc 1; tap F8; wait 300; poke patchmode 0; poke synthlib engine 1 1; poke patchmode 1; poke macromode"
check "the Macro depth row on the page: the patch there before keeps its mode, a patch added after it takes Add" "scale add 1" "$(pmodes) $(mode)"
# a library load while a part holds a patch: the part's own project patch is replaced by the loaded one, which takes the project mode
card5 f
run "$sd" "wait 500; poke macromode 0; poke synthlib engine 0 2; poke patchmode 0; poke macromode 1; poke synthlib load 0 PScale; poke patchmode 0; poke macromode 0; poke synthlib load 0 PScale; poke patchmode 0"
check "a load into a part that holds a patch: Add project makes it Add, Per patch leaves the file as it is" "scale add scale" "$(pmodes)"

echo "### 6: per project"
sd="$WORK/s6"; fresh "$sd"
run "$sd" "wait 500; poke macromode 1; projsave Projects Alpha; wait 3000; poke macromode 2; projsave Projects Beta; wait 3000; poke macromode 0; projsave Projects Gamma; wait 3000; \
projopen Projects Alpha; poke macromode; projopen Projects Beta; poke macromode; projopen Projects Gamma; poke macromode; projopen Projects Alpha; poke macromode; newproj; poke macromode"
check "save, open and new project: Alpha Add, Beta Scale, Gamma Per patch, Alpha again, a new project Per patch" "1 2 0 1 0" "$(modes | awk '{print $4, $5, $6, $7, $8}')"
echo "$(for p in Alpha Beta Gamma; do grep '^macro.mode' "$sd/Projects/$p/project.mtff" 2>/dev/null | tr '\n' ' '; echo -n '|'; done)" >"$WORK/keys.txt"
check "Projects/Alpha has macro.mode 0, Beta macro.mode 1, Gamma no key" "macro.mode 0 |macro.mode 1 ||" "$(cat "$WORK/keys.txt")"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
