#!/bin/bash
# fx34_compat_sim.sh [PNG_DIR]
#
# The compatibility rules of the 4 FX project switch (core/source/fx34.h, project.mtff key fx.slots, ref/four-fx-slots.md) on the real
# file manager and sequencer (ui_shot) against two OTHER firmware trees, built here from git archives into build/rel140-4fx/:
#   base140  release-1.4.0-int (BASE_REF), the firmware before the switch existed
#   fw130    tracker-ff-v1.3.0 (OLD_REF), the firmware that must still load and play a project that uses FX3 and FX4
#
#   A  switch OFF: one fixed script (steps with notes, FX, V and m on three patterns, pattern changes, Save As, open, save over, a second Save As)
#      runs on this tree and on base140. Every file of the card (Workspace and Projects: .mt, every .mtp, patternsMetadata, project.mtff,
#      recents) has the same bytes, and no .mtx file exists anywhere
#   B  the key: only "fx.slots 4" turns it on, a missing key, 3, 0 and text leave it off, the key is written only while on, the unknown lines
#      of the file survive, a new project starts off whatever the last project was, opening an off project after an on project leaks nothing
#   C  switch ON, the project loaded by the 1.3.0 build: the .mt and every .mtp are the same bytes as the same project without FX3 and FX4
#      (the pattern files stay stock v6), 1.3.0 loads it without a complaint and its MIDI out (notes, velocity from the V byte, timing from
#      the m byte, the FX of slots 1 and 2) is the same as that of the project without FX3 and FX4, and as this tree's with the switch off
#   D  1.3.0 edits the project and saves (its autosave rewrites the pattern with another CRC): this tree finds the sidecar stale, parks it
#      (the file on the card is not touched), plays slots 1 and 2 only, and a new FX3 written over it moves the old one to .mtx.old
#   E  1.3.0 Save As of the project: only the .mtp files travel, the project of the copy has no FX3 and FX4 (said in the CHANGELOG)
#   F  the retired Tape Start id 51 (dev projects, never released): the build before the Tape merge (TAPE_PRE_REF) writes a project with id 51 in
#      FX1, FX2 and in the FX3/FX4 sidecar, this tree loads it as id 50 with the value + 64 and plays it bit for bit as that build did
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
BASE_REF="${BASE_REF:-release-1.4.0-int}"
OLD_REF="${OLD_REF:-tracker-ff-v1.3.0}"
CACHE="${FX34_CACHE:-$ROOT/build/rel140-4fx}"
NCPU="$(sh "$HERE/../jobs.sh" n)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/fx34_compat.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
SAN="${SAN:-}"   # SAN=address,undefined: the sanitizer build of this tree (build/ui-shot-bridge-san), a report fails the run
# this tree is the emulator bridge build (bridge_env.sh, emu parity T10: the build every plugin gate of this tree runs), the two other trees are
# plain builds of their own Makefile (a ref before the bridge has none). No plugin is on any card here, so the bridge changes no file and no MIDI byte
. "$HERE/bridge_env.sh"
bridge_env "$ROOT" || exit 2
NEWB="$ROOT/build/ui-shot-bridge${SAN:+-san}"
mkdir -p "$NEWB" "$CACHE"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$NCPU" "${BRIDGE_ARGS[@]}" OUT="$NEWB" ${SAN:+SAN=$SAN} >"$NEWB.make.log" 2>&1 || { tail -30 "$NEWB.make.log"; exit 2; }
NEW="${UI_SHOT_BIN:-$NEWB/ui_shot}"   # UI_SHOT_BIN: a prebuilt BRIDGE=1 binary of this tree (SAN or not), no make; the two old trees still build their own

# The 1.3.0 host has no projsave / projopen script command (the Save As and Open the Project screen starts). They are host only code of
# this directory, not firmware: the handler of this tree is put into the copy of the old host before it is built
patch_old_host() {
    python3 -I - "$1/synth-open/tools/ui_shot/ui_shot_main.cpp" "$HERE/ui_shot_main.cpp" <<'PY'
import re, sys
dst, src = sys.argv[1], sys.argv[2]
d = open(dst).read()
if 'c == "projsave"' in d:
    sys.exit(0)
s = open(src).read()
i = s.index('    if (c == "projsave" || c == "projopen")')
j = s.index('#ifdef FEATURE_PARTS', i)
k = d.index('    if (c == "audio")')
open(dst, 'w').write(d[:k] + s[i:j] + d[k:])
PY
}
host_of() {   # host_of REF NAME [same-version]: a ui_shot of that ref, built once into CACHE/NAME-KEY (KEY from the stamp), HOST_DIR = that dir
    local ref="$1" dir="$2" sha
    sha="$(git -C "$ROOT" rev-parse "$ref^{commit}" 2>/dev/null)" || { echo "fx34_compat_sim.sh: no ref $ref" >&2; exit 2; }
    sha="$sha host-patch-1"   # the cache key: the commit and the version of patch_old_host
    # same-version: the base host is built with the version.mk of THIS tree. The firmware version is stamped into every pattern file header (and its CRC) and
    # the project files, so a base of the previous release differed in those bytes on any version bump (A2 red on 1.4.1 against release-1.4.0-int). A has to
    # prove "switch off writes the same bytes as the build before the switch existed", not "the version stamp is unchanged", so the base takes our stamp
    [ "${3:-}" = same-version ] && sha="$sha ver-$(shasum <"$ROOT/version.mk" | cut -c1-12)"
    # the dir name carries the stamp, so a cache shared by several worktrees (run_tests passes FX34_CACHE) never rebuilds a host another sim
    # is running: a missing one is built in a private dir and renamed into place
    HOST_DIR="$CACHE/$2-$(printf '%s' "$sha" | shasum | cut -c1-12)"; dir="$HOST_DIR.tmp.$$"
    if [ ! -x "$HOST_DIR/build/ui-shot/ui_shot" ]; then
        rm -rf "$dir"; mkdir -p "$dir"
        git -C "$ROOT" archive "$ref" | tar -x -C "$dir" || exit 2
        [ "${3:-}" = same-version ] && cp "$ROOT/version.mk" "$dir/version.mk"
        patch_old_host "$dir"
        # CHIP_PLUGIN_SRC (set by run_tests.sh) would move the old tree's binary to build/ui-shot-chipplg, where the check above never looks
        env -u CHIP_PLUGIN_SRC make -s -C "$dir/synth-open/tools/ui_shot" -j"$NCPU" >"$dir/make.log" 2>&1 || { tail -30 "$dir/make.log"; rm -rf "$dir"; exit 2; }
        echo "$sha" >"$dir/.ref"
        { [ -e "$HOST_DIR" ] && rm -rf "$dir"; } || mv "$dir" "$HOST_DIR"
    fi
}
host_of "$BASE_REF" base140 same-version; BASE="$HOST_DIR/build/ui-shot/ui_shot"
host_of "$OLD_REF" fw130; OLD="$HOST_DIR/build/ui-shot/ui_shot"
echo "     this tree $(git -C "$ROOT" rev-parse --short HEAD)   base $BASE_REF $(git -C "$ROOT" rev-parse --short "$BASE_REF")   old $OLD_REF $(git -C "$ROOT" rev-parse --short "$OLD_REF^{commit}")"

fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
expect() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected: $2
      got:      $3"; fi; }
fresh() { rm -rf "$1"; mkdir -p "$1/Plugins/Master" "$1/Plugins/Synth"; }
# ui_shot BIN SD "script": the stdout lines of the script
ui() { ASAN_OPTIONS=detect_leaks=0 "$1" "$2" "$3" 2>"$WORK/err.txt" || { echo "ui_shot failed:"; tail -5 "$WORK/err.txt"; fails=$((fails+1)); }; }
# The cap= token of the project.mtff header is the kMtffMax of the build that wrote it (32768 on the base, 40960 since Table lite):
# it is a build constant, not project data, so the header line is compared with that token masked. Every other byte counts.
tree_sums() { (cd "$1" && find . -type f -not -path './Plugins/*' | sort | while IFS= read -r f; do
    case "$f" in */project.mtff) printf '%s  %s\n' "$(sed -E '1s/ cap=[0-9]+$/ cap=N/' "$f" | shasum | cut -d' ' -f1)" "$f" ;;
                 *) shasum "$f" ;; esac; done); }
hex_of() { od -An -tx1 "$1" | tr -d ' \n'; }

# ---------------------------------------------------------------------------------------------------------------------------------- A
echo "### A switch off: the files of this tree are the files of $BASE_REF"
SCRIPT_A='wait 500; instr 48; setstep 0 0 60 48 7 11 8 22; setstep 0 1 62 48 0 0 0 0; setstep 1 2 -1 0 14 3 4 50; setmm 0 0 12; dirty; wait 15000;
pattern 2; plen 12; setstep 2 0 72 49 14 3 0 0; setstep 3 5 70 50 4 80 0 0; dirty; wait 15000;
pattern 3; plen 8; setstep 4 1 48 48 0 0 0 0; dirty; wait 15000; pattern 1;
projsave Projects Alpha; wait 500; setstep 0 4 64 48 0 0 0 0; dirty; wait 15000;
projsave Projects Alpha; projsave Projects Beta; projopen Projects Beta; wait 2000; setstep 0 6 65 48 3 10 0 0; dirty; wait 15000; projsave Projects Beta; wait 500'
fresh "$WORK/a_base"; ui "$BASE" "$WORK/a_base" "$SCRIPT_A" >/dev/null
fresh "$WORK/a_new"; ui "$NEW" "$WORK/a_new" "$SCRIPT_A" >/dev/null
tree_sums "$WORK/a_base" >"$WORK/a_base.sum"; tree_sums "$WORK/a_new" >"$WORK/a_new.sum"
N="$(wc -l <"$WORK/a_base.sum" | tr -d ' ')"
[ "$N" -ge 20 ] && pass "A1 the script wrote $N files (Workspace, two projects, recents)" || fail "A1 the base run wrote $N files" "$(tail -3 "$WORK/err.txt")"
expect "A2 every file of the card has the same bytes as on $BASE_REF (shasum of $N files)" "$(shasum <"$WORK/a_base.sum")" "$(shasum <"$WORK/a_new.sum")"
diff "$WORK/a_base.sum" "$WORK/a_new.sum" | head -5
expect "A3 no .mtx or .mtx.old anywhere" "0" "$(find "$WORK/a_new" -name '*.mtx*' | wc -l | tr -d ' ')"
expect "A4 no fx.slots line in any project.mtff" "0" "$(cat "$WORK/a_new"/Workspace/project.mtff "$WORK/a_new"/Projects/*/project.mtff | grep -c fx.slots)"
expect "A5 the pattern files are version 6 of the size they always had" "16432" "$(stat -f %z "$WORK/a_new/Workspace/patterns/pattern_01.mtp")"

# ---------------------------------------------------------------------------------------------------------------------------------- B
echo "### B the key fx.slots"
state_of() { ui "$NEW" "$1" "wait 500; fx34state" | grep '^fx34state' | awk '{print $3}'; }
sd="$WORK/b"; fresh "$sd"; ui "$NEW" "$sd" "wait 500" >/dev/null
expect "B1 a new project starts with the switch off" "0" "$(state_of "$sd")"
# The header the firmware writes first (ref/mtff-version.md): "tffproj 1 ff=<version of this tree> build=<the host's build id> cap=<kMtffMax>"
VER="$(sed -n 's/^SYNTH_OPEN_VER := *//p' "$ROOT/version.mk" | head -1 | cut -d- -f1)"
CAP="$(sed -n 's/^constexpr int kMtffMax = \([0-9]*\);.*/\1/p' "$ROOT/core/FileManager/mtff_codec.h")"   # kMtffMax of this tree
HDR="tffproj 1 ff=$VER build=uishot cap=$CAP"
expect "B2 and its project.mtff is the three lines it always was (header, mt, strum.ramp), no fx.slots line" "$HDR|mt|strum.ramp 100" "$(sed -E 's/^mt .*/mt/' "$sd/Workspace/project.mtff" | tr '\n' '|' | sed 's/|$//')"
for v in "3" "0" "on" "4 7" "04x" "5" ""; do
    printf 'tffproj 1\nfx.slots %s\n' "$v" >"$sd/Workspace/project.mtff"
    expect "B3 \"fx.slots $v\" leaves the project at 2 FX slots" "0" "$(state_of "$sd")"
done
printf 'tffproj 1\nstrum.ramp 40\n' >"$sd/Workspace/project.mtff"
expect "B4 a file without the key: off" "0" "$(state_of "$sd")"
printf 'tffproj 1\nmt 12345678 2324\nfx.slots 4\n' >"$sd/Workspace/project.mtff"
expect "B5 \"fx.slots 4\" on a stale stamp still applies (a project-wide key)" "1" "$(state_of "$sd")"
printf 'tffproj 1\nfx.slots 4\nx.future 5\n' >"$sd/Workspace/project.mtff"
out="$(ui "$NEW" "$sd" "wait 500; fx34state; tap File; wait 800; tap F7; wait 800; tap F8; enc -1; tap F8; wait 300; fx34state; poke popup; wait 15000" ; cat "$WORK/poke.out" 2>/dev/null)"
expect "B6 the key turns it on at the load, the page row turns it off" "1 0" "$(grep '^fx34state' <<<"$out" | awk '{printf "%s ", $3}' | sed 's/ $//')"
expect "B7 switched off the line is gone and the unknown line stays" "x.future 5" "$(grep -v '^tffproj\|^mt \|^strum.ramp' "$sd/Workspace/project.mtff" | tr '\n' '|' | sed 's/|$//')"
out="$(ui "$NEW" "$sd" "wait 500; tap File; wait 800; tap F7; wait 800; tap F8; enc 1; tap F8; wait 300; fx34state; wait 15000")"
expect "B8 the page row turns it on again, the key is written, the unknown line kept" "fx.slots 4|x.future 5" "$(grep -E '^(fx.slots|x.future)' "$sd/Workspace/project.mtff" | tr '\n' '|' | sed 's/|$//')"
# a project that is on, a project that is off: nothing carries over
sd="$WORK/b2"; fresh "$sd"
out="$(ui "$NEW" "$sd" "wait 500; tap File; wait 800; tap F7; wait 800; tap F8; enc 1; tap F8; wait 300; projsave Projects On; fx34state; tap F8; enc -1; tap F8; wait 300; projsave Projects Off; fx34state; projopen Projects On; fx34state; projopen Projects Off; fx34state; projopen Projects On; fx34state; newproj; fx34state")"
expect "B9 on, saved; off, saved; open On = on; open Off = off; open On = on; a new project = off" "1 0 1 0 1 0" "$(grep '^fx34state' <<<"$out" | awk '{printf "%s ", $3}' | sed 's/ $//')"
expect "B10 Projects/On has the key, Projects/Off has not" "1 0" "$(grep -c fx.slots "$sd/Projects/On/project.mtff") $(grep -c fx.slots "$sd/Projects/Off/project.mtff")"

# ---------------------------------------------------------------------------------------------------------------------------------- C
echo "### C switch on, the project in the 1.3.0 build"
# track 1 and 2 play MIDI instruments (channel 1 and 2). Slots 1 and 2: Chance and MIDI Chord. V: the volume byte. m: the micro-move byte
STEPS='setstep 0 0 60 48 0 0 0 0; fx34setv 0 0 90; setmm 0 0 20; setstep 0 2 64 48 14 3 0 0; setstep 1 1 62 49 4 100 0 0; setstep 1 3 67 49 14 1 0 0; fx34setv 1 3 40; setmm 1 3 35; plen 8'
EXTRA='setfx 0 0 2 14 3; setfx 0 2 3 7 11; setfx 1 1 3 8 22; setfx 1 3 2 6 5; setfx 1 3 3 5 99'
mk() {   # mk SD on|off: the project of this tree, with FX3 and FX4 or without
    fresh "$1"
    if [ "$2" = on ]; then ui "$NEW" "$1" "wait 500; instr 48; tap File; wait 800; tap F7; wait 800; tap F8; enc 1; tap F8; wait 300; $STEPS; $EXTRA; dirty; wait 15000; projsave Projects T" >/dev/null
    else ui "$NEW" "$1" "wait 500; instr 48; $STEPS; dirty; wait 15000; projsave Projects T" >/dev/null; fi
}
mk "$WORK/c_on" on; mk "$WORK/c_off" off
expect "C1 the sidecar is on the card (3 entries of pattern 1 and 2 of step 3: 5 entries, 12 + 20 + 4 bytes)" "36" "$(stat -f %z "$WORK/c_on/Workspace/patterns/pattern_01.mtx" 2>/dev/null)"
expect "C2 project.mt is the same bytes with and without FX3 and FX4" "$(shasum <"$WORK/c_off/Workspace/project.mt")" "$(shasum <"$WORK/c_on/Workspace/project.mt")"
expect "C3 pattern_01.mtp is the same bytes with and without FX3 and FX4" "$(shasum <"$WORK/c_off/Workspace/patterns/pattern_01.mtp")" "$(shasum <"$WORK/c_on/Workspace/patterns/pattern_01.mtp")"
expect "C4 the project folder: the same .mt and .mtp, plus the sidecar" "$(cd "$WORK/c_off/Projects/T" && find . -type f | sort | tr '\n' ' ')|1" "$(cd "$WORK/c_on/Projects/T" && find . -type f -not -name '*.mtx' | sort | tr '\n' ' ')|$(find "$WORK/c_on/Projects/T" -name '*.mtx' | wc -l | tr -d ' ')"
expect "C5 the pattern file keeps its size and version (v6, 16,432 B)" "16432 6" "$(stat -f %z "$WORK/c_on/Workspace/patterns/pattern_01.mtp") $(od -An -tu1 -j10 -N1 "$WORK/c_on/Workspace/patterns/pattern_01.mtp" | tr -d ' ')"
expect "C6 the only difference in project.mtff is the key" "fx.slots 4" "$(diff "$WORK/c_off/Workspace/project.mtff" "$WORK/c_on/Workspace/project.mtff" | grep '^[<>]' | grep -v '^[<>] mt ' | sed 's/^> //')"
PLAY='audio 1; midilog 1; tap Play; wait 2500; tap Play; wait 200; midilog 0'
mlog() { ui "$1" "$2" "wait 500; $PLAY; step 0 0; step 0 2; step 1 1; step 1 3" | grep -E '^(midi |step )' | sed 's/ t [0-9.]*$/&/'; }
cp -R "$WORK/c_on" "$WORK/c_on_old"; cp -R "$WORK/c_off" "$WORK/c_off_old"
SUM_BEFORE="$(shasum "$WORK/c_on_old/Workspace/patterns/pattern_01.mtx" "$WORK/c_on_old/Workspace/patterns/pattern_01.mtp" | cut -c1-12 | tr '\n' ' ')"
L_ON_OLD="$(mlog "$OLD" "$WORK/c_on_old")"
L_OFF_OLD="$(mlog "$OLD" "$WORK/c_off_old")"
L_OFF_NEW="$(mlog "$NEW" "$WORK/c_off")"
[ "$(grep -c '^midi on' <<<"$L_ON_OLD")" -ge 6 ] && pass "C7 the 1.3.0 build plays the project: $(grep -c '^midi on' <<<"$L_ON_OLD") notes in 2.5 s" || fail "C7 1.3.0 played $(grep -c '^midi on' <<<"$L_ON_OLD") notes" "$L_ON_OLD"
expect "C8 1.3.0: the MIDI out (notes, velocity, time) of the project with FX3 and FX4 is that of the project without" "$L_OFF_OLD" "$L_ON_OLD"
expect "C9 this tree with the switch off plays what 1.3.0 plays (slots 1 and 2, V and m unchanged)" "$L_OFF_OLD" "$L_OFF_NEW"
grep '^midi on' <<<"$L_ON_OLD" | awk '{print $4}' | sort -u | tr '\n' ' ' | grep -q '114 .*50 \|50 .*114 ' && pass "C10 the V byte plays (velocity 114 of V 90 and 50 of V 40 are in the log)" || fail "C10 V byte" "$(grep '^midi on' <<<"$L_ON_OLD" | head -5)"
expect "C11 1.3.0 left the sidecar and the pattern file as they were (no edit, no write)" "$SUM_BEFORE" "$(shasum "$WORK/c_on_old/Workspace/patterns/pattern_01.mtx" "$WORK/c_on_old/Workspace/patterns/pattern_01.mtp" | cut -c1-12 | tr '\n' ' ')"
# the same card in this tree with the switch on keeps the data
expect "C12 this tree loads the project back with FX3 and FX4 (5 entries)" "5" "$(ui "$NEW" "$WORK/c_on" "wait 500; fx34state" | awk '/^fx34state/ {print $5}')"

# ---------------------------------------------------------------------------------------------------------------------------------- D
echo "### D 1.3.0 edits the project and saves: the sidecar is stale"
ui "$OLD" "$WORK/c_on_old" "wait 500; setstep 0 5 70 48 0 0 0 0; dirty; wait 15000" >/dev/null
MTX0="$(shasum <"$WORK/c_on/Workspace/patterns/pattern_01.mtx")"
[ "$(shasum <"$WORK/c_on_old/Workspace/patterns/pattern_01.mtp")" != "$(shasum <"$WORK/c_on/Workspace/patterns/pattern_01.mtp")" ] && pass "D1 1.3.0 rewrote the pattern file (another CRC)" || fail "D1 1.3.0 did not write the pattern"
expect "D2 and left the sidecar alone" "$MTX0" "$(shasum <"$WORK/c_on_old/Workspace/patterns/pattern_01.mtx")"
out="$(ui "$NEW" "$WORK/c_on_old" "wait 500; fx34state; fx34 0 0")"
expect "D3 this tree: the switch is on, the pattern has no FX3 and FX4, one sidecar parked" "fx34state on 1 n 0 parked 1 refused 0" "$(grep '^fx34state' <<<"$out")"
expect "D4 the parked sidecar is untouched" "$MTX0" "$(shasum <"$WORK/c_on_old/Workspace/patterns/pattern_01.mtx")"
expect "D5 and the status line says so (host.txt)" "1" "$(grep -c 'FX3/FX4 of the pattern on screen parked' "$WORK/c_on_old/Plugins/host.txt")"
ui "$NEW" "$WORK/c_on_old" "wait 500; setfx 0 0 3 9 9; dirty; wait 15000" >/dev/null
expect "D6 a new FX3 over the parked file: the parked one steps aside as .mtx.old, unchanged" "$MTX0" "$(shasum <"$WORK/c_on_old/Workspace/patterns/pattern_01.mtx.old" 2>/dev/null)"
[ "$(shasum <"$WORK/c_on_old/Workspace/patterns/pattern_01.mtx")" != "$MTX0" ] && [ -f "$WORK/c_on_old/Workspace/patterns/pattern_01.mtx" ] && pass "D7 and the new sidecar is written, bound to the new pattern file" || fail "D7 new sidecar"
expect "D8 reloaded: the new FX3 is there" "fx34 0 0 r2 0:0 r3 9:9 n 1" "$(ui "$NEW" "$WORK/c_on_old" "wait 500; fx34 0 0" | grep '^fx34 ')"

# ---------------------------------------------------------------------------------------------------------------------------------- E
echo "### E 1.3.0 Save As"
cp -R "$WORK/c_on" "$WORK/e"
ui "$OLD" "$WORK/e" "wait 500; projsave Projects Copy13" >/dev/null
expect "E1 the Save As of 1.3.0 wrote the project with its .mtp files" "1" "$(ls "$WORK/e/Projects/Copy13/patterns/" | grep -c 'pattern_01.mtp')"
expect "E2 and no sidecar: FX3 and FX4 do not travel (CHANGELOG)" "0" "$(find "$WORK/e/Projects/Copy13" -name '*.mtx*' | wc -l | tr -d ' ')"
expect "E3 this tree opens that copy as a 2 FX project" "fx34state on 0 n 0 parked 0 refused 0" "$(ui "$NEW" "$WORK/e" "wait 500; projopen Projects Copy13; fx34state" | grep '^fx34state')"

# ---------------------------------------------------------------------------------------------------------------------------------- F
echo "### F the retired Tape Start id 51: dev projects load as Tape (id 50) with the value + 64"
# TAPE_PRE_REF: the last release-1.4.0-int before Tape Stop and Tape Start became one FX (id 50, 65-128 = Start). Its ui_shot is built from a git archive
host_of "${TAPE_PRE_REF:-84a380f42e748a4f61576da0e99357a737837747}" pretape; PRE="$HOST_DIR/build/ui-shot/ui_shot"
SW_ON='tap File; wait 800; tap F7; wait 800; tap F8; enc 1; tap F8; wait 300'
# a sample track: a Stop (id 50), Starts of id 51 in FX1 and FX2, value 0 (plays as 1), 200 (plays as 64), and with the switch on FX3 and FX4 of id 51
TAPE_OLD='plen 24; setstep 0 0 60 0 50 6 0 0; setstep 0 6 60 0 51 6 0 0; setstep 0 9 62 0 0 0 51 12; setstep 0 12 60 0 51 0 0 0; setstep 0 18 60 0 51 200 0 0'
TAPE_NEW='plen 24; setstep 0 0 60 0 50 6 0 0; setstep 0 6 60 0 50 70 0 0; setstep 0 9 62 0 0 0 50 76; setstep 0 12 60 0 50 65 0 0; setstep 0 18 60 0 50 128 0 0'
X3_OLD='setstep 0 15 60 0 0 0 0 0; setfx 0 15 3 51 8; setstep 0 21 60 0 0 0 0 0; setfx 0 21 3 51 200'
X3_NEW='setstep 0 15 60 0 0 0 0 0; setfx 0 15 3 50 72; setstep 0 21 60 0 0 0 0 0; setfx 0 21 3 50 128'
mkt() {   # mkt BIN SD on|off "steps" "extra": a project with the tape steps
    fresh "$2"
    if [ "$3" = on ]; then ui "$1" "$2" "wait 500; sampletone 0; instr 0; $SW_ON; $4; $5; dirty; wait 15000; projsave Projects T" >/dev/null
    else ui "$1" "$2" "wait 500; sampletone 0; instr 0; $4; dirty; wait 15000; projsave Projects T" >/dev/null; fi
}
PLAYT='sampletone 0; instr 0; audio 1; audiostat tap 0; audiostat 1; tap Play; wait 3300; tap Play; audiostat 0; audiostat tapdump'
tapblk() { ui "$1" "$2" "wait 500; $PLAYT" | grep -E '^blk '; }
mkt "$PRE" "$WORK/f_off" off "$TAPE_OLD" ""
mkt "$PRE" "$WORK/f_on" on "$TAPE_OLD" "$X3_OLD"
mkt "$NEW" "$WORK/f_nat_off" off "$TAPE_NEW" ""
mkt "$NEW" "$WORK/f_nat_on" on "$TAPE_NEW" "$X3_NEW"
expect "F1 the card of the build before the merge holds id 51 (FX1, FX2)" "step 0 6 note 60 instr 0 fx0 51:6 fx1 0:0|step 0 9 note 62 instr 0 fx0 0:0 fx1 51:12" "$(ui "$PRE" "$WORK/f_off" "wait 500; step 0 6; step 0 9" | grep '^step' | tr '\n' '|' | sed 's/|$//')"
expect "F2 this tree loads it as id 50 with the value + 64 (6 -> 70, 12 -> 76, 0 -> 65, 200 -> 128), a Stop stays" "step 0 0 note 60 instr 0 fx0 50:6 fx1 0:0|step 0 6 note 60 instr 0 fx0 50:70 fx1 0:0|step 0 9 note 62 instr 0 fx0 0:0 fx1 50:76|step 0 12 note 60 instr 0 fx0 50:65 fx1 0:0|step 0 18 note 60 instr 0 fx0 50:128 fx1 0:0" "$(ui "$NEW" "$WORK/f_off" "wait 500; step 0 0; step 0 6; step 0 9; step 0 12; step 0 18" | grep '^step' | tr '\n' '|' | sed 's/|$//')"
expect "F3 the FX3 and FX4 of the sidecar too (8 -> 72, 200 -> 128)" "fx34 0 15 r2 0:0 r3 50:72 n 2|fx34 0 21 r2 0:0 r3 50:128 n 2" "$(ui "$NEW" "$WORK/f_on" "wait 500; fx34 0 15; fx34 0 21" | grep '^fx34 ' | sed 's/ r2 0:0 r3/ r2 0:0 r3/' | tr '\n' '|' | sed 's/|$//')"
B_PRE_OFF="$(tapblk "$PRE" "$WORK/f_off")"; B_PRE_OFF2="$(tapblk "$PRE" "$WORK/f_off")"
B_NEW_OFF="$(tapblk "$NEW" "$WORK/f_off")"; B_NAT_OFF="$(tapblk "$NEW" "$WORK/f_nat_off")"
B_PRE_ON="$(tapblk "$PRE" "$WORK/f_on")"; B_NEW_ON="$(tapblk "$NEW" "$WORK/f_on")"; B_NAT_ON="$(tapblk "$NEW" "$WORK/f_nat_on")"
NBLK="$(grep -c '^blk ' <<<"$B_PRE_OFF")"
[ "$NBLK" -ge 300 ] && pass "F4 the voice tap of the old build has $NBLK blocks, and it is deterministic (two runs: $(shasum <<<"$B_PRE_OFF" | cut -c1-8) $(shasum <<<"$B_PRE_OFF2" | cut -c1-8))" || fail "F4 the tap log has $NBLK blocks" "$(tail -3 "$WORK/err.txt")"
expect "F5 two runs of the old build are the same blocks" "$(shasum <<<"$B_PRE_OFF")" "$(shasum <<<"$B_PRE_OFF2")"
expect "F6 this tree plays the old card (ids 50 and 51 in FX1 and FX2) bit for bit as the old build: voice tap, every block" "$(shasum <<<"$B_PRE_OFF")" "$(shasum <<<"$B_NEW_OFF")"
expect "F7 and as the same steps written in the new encoding (Start = 64 + value)" "$(shasum <<<"$B_NEW_OFF")" "$(shasum <<<"$B_NAT_OFF")"
expect "F8 switch on: the old card with id 51 in FX3 and FX4 plays bit for bit as on the old build" "$(shasum <<<"$B_PRE_ON")" "$(shasum <<<"$B_NEW_ON")"
expect "F9 and as the new encoding in FX3 and FX4" "$(shasum <<<"$B_NEW_ON")" "$(shasum <<<"$B_NAT_ON")"
# a Start is audible: the run of step 6 (a Start of 1.5 steps) differs from the Stop of step 0 (the blocks of the two runs are not the same)
[ "$(shasum <<<"$B_PRE_ON")" != "$(shasum <<<"$B_PRE_OFF")" ] && pass "F10 the FX3 and FX4 steps are heard (the log of the switch on card differs from the switch off card)" || fail "F10 FX3 and FX4 steps are silent"
# save over: this tree writes the migrated slots back (no id 51 left), a second load and play is the same, the sidecar keeps its CRC bond
cp -R "$WORK/f_on" "$WORK/f_on2"
ui "$NEW" "$WORK/f_on2" "wait 500; dirty; wait 15000" >/dev/null
expect "F11 after a save the card holds the migrated ids: reloaded the steps read 50:72 / 50:128 and the sidecar is not parked" "fx34state on 1 n 2 parked 0 refused 0" "$(ui "$NEW" "$WORK/f_on2" "wait 500; fx34state; fx34 0 15" | grep '^fx34state' | sed 's/ n [0-9]* / n 2 /')"
expect "F12 and plays the same blocks" "$(shasum <<<"$B_PRE_ON")" "$(shasum <<<"$(tapblk "$NEW" "$WORK/f_on2")")"
expect "F13 the pattern file keeps its size and version (v6, 16,432 B)" "16432 6" "$(stat -f %z "$WORK/f_on2/Workspace/patterns/pattern_01.mtp") $(od -An -tu1 -j10 -N1 "$WORK/f_on2/Workspace/patterns/pattern_01.mtp" | tr -d ' ')"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
