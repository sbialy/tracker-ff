#!/bin/bash
# mtff_sim.sh [BIN]
#
# project.mtff (core/FileManager/mtff_codec.h, plug_host.cpp) on the real UI, file manager and plugin host of this tree (ui_shot, the PLUG_HOST
# configuration) over a card folder on the Mac: the five hooks (open incl. the open the USB console calls, Workspace load, Workspace save,
# Save / Save As, new project), the stamp against the real project.mt CRC, the Project Settings page rows Strum volume and Macro depth and unknown lines on a rewrite.
# Host only: no device, no audio, virtual clock. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)".
# Build first (ui_shot.sh pattern builds build/ui-shot/ui_shot).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="${1:-${UI_SHOT_BIN:-$ROOT/build/ui-shot/ui_shot}}"
[ -x "$BIN" ] || { echo "mtff_sim.sh: no $BIN (ui_shot.sh pattern builds it)" >&2; exit 2; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/mtff_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
check() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected: $2
      got:      $3"; fi; }

# the CRC32 in the last four bytes of a project.mt, as the file manager reads it (little endian), 8 hex digits
crc_of() { od -An -tx1 -j "$(( $(stat -f %z "$1") - 4 ))" -N4 "$1" | awk '{printf "%s%s%s%s", $4, $3, $2, $1}'; }
size_of() { stat -f %z "$1"; }
# run SD SCRIPT: one ui_shot run, the pokes' answers in $WORK/poke.out
run() {
    rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$BIN" "$1" "$2" >"$WORK/out.txt" 2>"$WORK/err.txt" || { echo "ui_shot failed:"; tail -5 "$WORK/err.txt"; fails=$((fails+1)); }
    [ -f "$WORK/poke.out" ] || : >"$WORK/poke.out"
}
# The header the firmware writes first: "tffproj <format> ff=<version of this tree> build=<the host's build id> cap=<kMtffMax>"
VER="$(sed -n 's/^SYNTH_OPEN_VER := *//p' "$ROOT/version.mk" | head -1 | cut -d- -f1)"
CAP="$(sed -n 's/^constexpr int kMtffMax = \([0-9]*\);.*/\1/p' "$ROOT/core/FileManager/mtff_codec.h")"   # kMtffMax of this tree
HDR="tffproj 1 ff=$VER build=uishot cap=$CAP"
mtffver() { grep '^mtffver ' "$WORK/poke.out" | tail -1 | cut -d' ' -f2-; }
# the Project Settings page (File > F7), Strum volume row: strum N moves the list by N steps of 10 (the old File row stepped by 10 and wrapped, the page list does not wrap)
PAGE='tap File; wait 800; tap F7; wait 800'
# (one enc per step: a bigger delta in one enc is limited by the list)
strum() { local n="$1" d=1 e=""; [ "$n" -lt 0 ] && { d=-1; n=$((-n)); }; while [ "$n" -gt 0 ]; do e="$e enc $d;"; n=$((n-1)); done; echo "tap Down 2; tap F8;$e tap F8; wait 800"; }
depth() { grep '^strumdepth ' "$WORK/poke.out" | tail -1 | awk '{print $2}'; }
fresh() { rm -rf "$1"; mkdir -p "$1/Plugins/Master" "$1/Plugins/Synth"; }

echo "### 1: a new project (first power up of an empty card): Strum ramp 100, written with the stamp of the project.mt"
sd="$WORK/s1"; fresh "$sd"
run "$sd" "wait 500; poke strumdepth"
check "the new project starts at 100" "100" "$(depth)"
check "Workspace/project.mtff is the three lines, stamp = the CRC and size in the project.mt" \
    "$HDR|mt $(crc_of "$sd/Workspace/project.mt") $(size_of "$sd/Workspace/project.mt")|strum.ramp 100" "$(tr '\n' '|' <"$sd/Workspace/project.mtff" 2>/dev/null | sed 's/|$//')"
echo "### 2: power up again: the Workspace project loads, in sync, depth 100 from the file"
run "$sd" "wait 500; poke strumdepth"
check "depth 100 after the load" "100" "$(depth)"
n="$(grep -c 'changed outside' "$sd/Plugins/host.txt" 2>/dev/null || true)"
check "no stale note" "0" "${n:-0}"

echo "### 3: a project without the file plays with the ramp off (the 1.3.0 sound)"
rm -f "$sd/Workspace/project.mtff"
run "$sd" "wait 500; poke strumdepth"
check "depth 0 with no project.mtff" "0" "$(depth)"
check "and nothing is written for it" "absent" "$([ -e "$sd/Workspace/project.mtff" ] && echo present || echo absent)"

echo "### 4: a file with no mt line (written by hand): not checked, project-wide key applies; a file without the key is 0"
printf 'tffproj 1\nstrum.ramp 40\n' >"$sd/Workspace/project.mtff"
run "$sd" "wait 500; poke strumdepth"
check "depth 40" "40" "$(depth)"
printf 'tffproj 1\nx.only-unknown 1\n' >"$sd/Workspace/project.mtff"
run "$sd" "wait 500; poke strumdepth"
check "no strum.ramp line: depth 0" "0" "$(depth)"

echo "### 5: a stale stamp (project.mt written by something else): strum.ramp applies, slot-bound lines are parked, one status line"
printf 'tffproj 1\nmt 12345678 2324\nstrum.ramp 70\npart.1.engine 83\nx.future 5\n' >"$sd/Workspace/project.mtff"
run "$sd" "wait 500; poke strumdepth"
check "depth 70 applies" "70" "$(depth)"
check "host.txt carries the one status line" "1" "$(grep -c 'FF settings of 1 line parked' "$sd/Plugins/host.txt" 2>/dev/null)"
echo "### 5b: the next save (the UI row) writes the fresh stamp, keeps the parked and the unknown line, applies the new depth"
run "$sd" "wait 500; ${PAGE}; $(strum 1); poke strumdepth; wait 15000"
check "page steps 70 to 80" "80" "$(depth)"
check "the file: fresh stamp, strum.ramp 80, parked line and unknown line kept" \
    "$HDR|mt $(crc_of "$sd/Workspace/project.mt") $(size_of "$sd/Workspace/project.mt")|strum.ramp 80|parked.part.1.engine 83|x.future 5" "$(tr '\n' '|' <"$sd/Workspace/project.mtff" 2>/dev/null | sed 's/|$//')"

echo "### 6: the page row Strum volume: 100 down to 0, the file is removed (no setting differs from the old behaviour), 0 back up to 100"
sd="$WORK/s6"; fresh "$sd"
run "$sd" "wait 500; ${PAGE}; $(strum -10); poke strumdepth; wait 15000"
check "100 -> 0" "0" "$(depth)"
check "project.mtff is gone" "absent" "$([ -e "$sd/Workspace/project.mtff" ] && echo present || echo absent)"
run "$sd" "wait 500; ${PAGE}; $(strum 10); poke strumdepth; wait 15000"
check "0 -> 100" "100" "$(depth)"
check "the file is back, stamp matches the saved project.mt" "$HDR|mt $(crc_of "$sd/Workspace/project.mt") $(size_of "$sd/Workspace/project.mt")|strum.ramp 100" \
    "$(tr '\n' '|' <"$sd/Workspace/project.mtff" 2>/dev/null | sed 's/|$//')"
run "$sd" "wait 500; tap File; wait 1200; shot $WORK/file.png"
[ -s "$WORK/file.png" ] && cp "$WORK/file.png" "${MTFF_PNG:-/dev/null}" 2>/dev/null

echo "### 7: Save As into a nested folder carries the file, open (the open the USB console calls) brings it back, a project without it opens at 0"
sd="$WORK/s7"; fresh "$sd"
NEST="Projects/Albums_2026/Summer_Tour_Warmup_Sessions"
mkdir -p "$sd/$NEST"
run "$sd" "wait 500; poke strumdepth 60; ${PAGE}; $(strum 4); poke strumdepth; wait 500"
check "60 + four steps = 100" "100" "$(depth)"
run "$sd" "wait 500; poke strumdepth 60; poke fmsave $NEST Rehearsal_Take_One; wait 4000; poke strumdepth"
check "Save As started and the depth is untouched" "60" "$(depth)"
P="$sd/$NEST/Rehearsal_Take_One"
check "the project folder has project.mtff with depth 60 and the stamp of its project.mt" \
    "$HDR|mt $(crc_of "$P/project.mt") $(size_of "$P/project.mt")|strum.ramp 60" "$(tr '\n' '|' <"$P/project.mtff" 2>/dev/null | sed 's/|$//')"
check "no other file named like it anywhere under Projects" "1" "$(find "$sd/Projects" -name 'project.mtff*' | wc -l | tr -d ' ')"
# a second project of the same folder without a setting (depth 0), saved after the first
run "$sd" "wait 500; poke strumdepth 0; poke fmsave $NEST Rehearsal_Take_Two; wait 4000"
check "the project saved at depth 0 has no project.mtff" "absent" "$([ -e "$sd/$NEST/Rehearsal_Take_Two/project.mtff" ] && echo present || echo absent)"
check "and the first one's file is untouched" "60" "$(grep strum.ramp "$P/project.mtff" | awk '{print $2}')"
# open both: depth follows the project (the open of the USB console is newFileManager.openProject)
run "$sd" "wait 500; poke strumdepth 5; poke fmopen $NEST Rehearsal_Take_One; wait 6000; poke strumdepth"
check "open of the first project: depth 60 (from its folder via the Workspace)" "60" "$(depth)"
check "Workspace/project.mtff came with it" "1" "$(grep -c '^strum.ramp 60' "$sd/Workspace/project.mtff" 2>/dev/null)"
run "$sd" "wait 500; poke fmopen $NEST Rehearsal_Take_Two; wait 6000; poke strumdepth"
check "open of the second (no file): depth 0, no file in the Workspace" "0 absent" "$(depth) $([ -e "$sd/Workspace/project.mtff" ] && echo present || echo absent)"

echo "### 8: the keys of the feature branches (seed, groove, snap, follow chord, Parts): in sync they apply, stale they apply too (track and pattern numbers never move, the pat. keys apply since 2026-11-18)"
sd="$WORK/s8"; fresh "$sd"
run "$sd" "wait 500"
KEYS='seed.take 9\ntrack.2.groove 20\ntrack.3.snap 1\ntrack.4.follow 1 2\npat.parts.mode 1\npat.part.1 01010101010101010101010101010101 0 1 1 0\n'
printf "tffproj 1\nmt $(crc_of "$sd/Workspace/project.mt") $(size_of "$sd/Workspace/project.mt")\n$KEYS" >"$sd/Workspace/project.mtff"
run "$sd" "wait 500; seed; groove; snap; follow; parts"
check "in sync: the seed applies" "seed 9" "$(grep '^seed ' "$WORK/out.txt")"
check "in sync: the groove of track 2 applies" "groove 0 20 0 0 0 0 0 0 0 0 0 0 0 0 0 0" "$(grep '^groove ' "$WORK/out.txt")"
check "in sync: the snap of track 3 applies" "snap 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0" "$(grep '^snap ' "$WORK/out.txt")"
check "in sync: the follow chord of track 4 applies" "1" "$(grep -c '^follow .* t4<-t1:' "$WORK/out.txt")"
check "in sync: the Parts list is loaded" "1" "$(grep -c '^parts count 1 mode 1' "$WORK/out.txt")"
printf "tffproj 1\nmt 12345678 2324\n$KEYS" >"$sd/Workspace/project.mtff"
run "$sd" "wait 500; seed; groove; snap; follow; parts; dirty; wait 4000"
check "stale: the seed is project-wide and applies" "seed 9" "$(grep '^seed ' "$WORK/out.txt")"
check "stale: the groove of track 2 and the snap of track 3 still apply" "groove 0 20 0 0 0 0 0 0 0 0 0 0 0 0 0 0|snap 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0" "$(grep '^groove ' "$WORK/out.txt")|$(grep '^snap ' "$WORK/out.txt")"
check "stale: the follow chord of track 4 still applies" "1" "$(grep -c '^follow .* t4<-t1:' "$WORK/out.txt")"
check "stale: the Parts (pat.parts.mode, pat.part.1) apply, the Parts list is loaded" "1" "$(grep -c '^parts count 1 mode 1' "$WORK/out.txt")"
check "stale: host.txt has no parked line, the one status line says the settings applied" "0|1" "$(grep -c 'parked' "$sd/Plugins/host.txt" 2>/dev/null)|$(grep -c 'project.mt was changed outside FF, settings applied' "$sd/Plugins/host.txt" 2>/dev/null)"
check "stale: the next save has the fresh stamp, seed.take, the track keys and the pat. lines live" \
    "$HDR|mt $(crc_of "$sd/Workspace/project.mt") $(size_of "$sd/Workspace/project.mt")|seed.take 9|track.2.groove 20|track.3.snap 1|track.4.follow 1 2|pat.parts.mode 1|pat.part.1 01010101010101010101010101010101 0 1 1 0" \
    "$(tr '\n' '|' <"$sd/Workspace/project.mtff" 2>/dev/null | sed 's/|$//')"
echo "### 8b: a stale file whose only lines are track.N keys: they apply, nothing is parked, no status line"
sd="$WORK/s8b"; fresh "$sd"
run "$sd" "wait 500"
printf "tffproj 1\nmt 12345678 2324\ntrack.2.groove 20\ntrack.3.snap 1\ntrack.4.follow 1 2\n" >"$sd/Workspace/project.mtff"
run "$sd" "wait 500; groove; snap; follow; wait 4000"
check "stale, track keys only: groove, snap and follow apply" "groove 0 20 0 0 0 0 0 0 0 0 0 0 0 0 0 0|snap 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0|1" "$(grep '^groove ' "$WORK/out.txt")|$(grep '^snap ' "$WORK/out.txt")|$(grep -c '^follow .* t4<-t1:' "$WORK/out.txt")"
check "stale, track keys only: nothing parked, no parked line in the file" "0|0" "$(grep -c 'lines\? parked' "$sd/Plugins/host.txt" 2>/dev/null)|$(grep -c '^parked\.' "$sd/Workspace/project.mtff" 2>/dev/null)"

echo "### 8c: pat.start.N (the start step of every track cell of a Part, written only when a Part has a start step)"
sd="$WORK/s8c"; fresh "$sd"
run "$sd" "wait 500"
ST1='pat.start.1 10000000000000000000000000000000'
KEYS8C="pat.parts.mode 1\npat.part.1 01010101010101010101010101010101 0 1 1 0\n$ST1\n"
printf "tffproj 1\nmt $(crc_of "$sd/Workspace/project.mt") $(size_of "$sd/Workspace/project.mt")\n$KEYS8C" >"$sd/Workspace/project.mtff"
run "$sd" "wait 500; parts; partstart; dirty; wait 4000"
check "in sync: the Part loads with the start step of track 1 (16) and the others 0" "partstart 0 16 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" "$(grep '^partstart 0 ' "$WORK/out.txt" | head -1)"
check "in sync: the next save writes the same pat.start line (and nothing else new)" "$ST1" "$(grep '^pat\.start' "$sd/Workspace/project.mtff")"
printf "tffproj 1\nmt $(crc_of "$sd/Workspace/project.mt") $(size_of "$sd/Workspace/project.mt")\npat.parts.mode 1\npat.part.1 01010101010101010101010101010101 0 1 1 0\n" >"$sd/Workspace/project.mtff"
run "$sd" "wait 500; partstart; dirty; wait 4000"
check "no pat.start line: every start is 0 and the save writes none (a project without start steps is byte for byte as before)" "partstart 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0|0" "$(grep '^partstart 0 ' "$WORK/out.txt" | head -1)|$(grep -c '^pat\.start' "$sd/Workspace/project.mtff")"
printf "tffproj 1\nmt $(crc_of "$sd/Workspace/project.mt") $(size_of "$sd/Workspace/project.mt")\npat.parts.mode 1\npat.part.1 01010101010101010101010101010101 0 1 1 0\npat.start.1 zz\npat.start.7 10000000000000000000000000000000\n" >"$sd/Workspace/project.mtff"
run "$sd" "wait 500; partstart; dirty; wait 4000"
check "a malformed pat.start line and one for a Part that is not there are ignored (start 0, no crash)" "partstart 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" "$(grep '^partstart 0 ' "$WORK/out.txt" | head -1)"
printf "tffproj 1\nmt 12345678 2324\n$KEYS8C" >"$sd/Workspace/project.mtff"
run "$sd" "wait 500; parts; partstart; dirty; wait 4000"
check "stale: pat.start.1 applies with the Parts (start 16) and the next save writes it live" "partstart 0 16 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0|$ST1|0" \
    "$(grep '^partstart 0 ' "$WORK/out.txt" | head -1)|$(grep '^pat\.start' "$sd/Workspace/project.mtff")|$(grep -c '^parked\.' "$sd/Workspace/project.mtff")"
# 1.4.0-int (no pat.start): the line is an unknown line, kept byte for byte, and the Parts play with start 0. Skipped when no 1.4.0-int ui_shot is there
BIN_INT="${BIN_INT:-$ROOT/build/ui-shot-int/ui_shot}"
if [ -x "$BIN_INT" ]; then
    sd="$WORK/s8d"; fresh "$sd"
    run "$sd" "wait 500"
    printf "tffproj 1 ff=1.4.0 build=int cap=20480\nmt $(crc_of "$sd/Workspace/project.mt") $(size_of "$sd/Workspace/project.mt")\n$KEYS8C" >"$sd/Workspace/project.mtff"
    BIN_SAVE="$BIN"; BIN="$BIN_INT"
    run "$sd" "wait 500; parts; dirty; wait 4000"
    BIN="$BIN_SAVE"
    check "1.4.0-int: the Part loads (start ignored) and its save keeps the pat.start line byte for byte" "1|$ST1" "$(grep -c '^parts count 1 mode 1' "$WORK/out.txt")|$(grep '^pat\.start' "$sd/Workspace/project.mtff")"
    run "$sd" "wait 500; partstart"
    check "this build reads the file 1.4.0-int saved: the start step is back (16)" "partstart 0 16 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" "$(grep '^partstart 0 ' "$WORK/out.txt" | head -1)"
    sd="$WORK/s8e"; fresh "$sd"
    BIN_SAVE="$BIN"; BIN="$BIN_INT"
    run "$sd" "wait 500"
    printf "tffproj 1 ff=1.4.0 build=int cap=20480\nmt $(crc_of "$sd/Workspace/project.mt") $(size_of "$sd/Workspace/project.mt")\npat.parts.mode 1\npat.part.1 01010101010101010101010101010101 0 1 1 0\n" >"$sd/Workspace/project.mtff"
    run "$sd" "wait 500; dirty; wait 4000"
    BIN="$BIN_SAVE"
    cp "$sd/Workspace/project.mtff" "$WORK/s8e.int"
    run "$sd" "wait 500; partstart; dirty; wait 4000"
    check "a file written by 1.4.0-int without start lines: start 0, and this build's save has no pat.start line" "partstart 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0|0" "$(grep '^partstart 0 ' "$WORK/out.txt" | head -1)|$(grep -c '^pat\.start' "$sd/Workspace/project.mtff")"
    check "and the lines of that file are the same as 1.4.0-int wrote (header aside)" "$(grep -v '^tffproj' "$WORK/s8e.int")" "$(grep -v '^tffproj' "$sd/Workspace/project.mtff")"
else
    echo "skip  8c-int: no $BIN_INT (build 1.4.0-int ui_shot there or set BIN_INT)"
fi

echo "### 8f: stale and a Part names a pattern that is gone (1.3.x deleted it): only that cell is blanked and logged, the rest of the Part applies, play does not crash"
sd="$WORK/s8f"; fresh "$sd"
run "$sd" "wait 500"
ls "$sd/Workspace/patterns/" >"$WORK/s8f.pats" 2>/dev/null
check "the fresh project has pattern 1 and no pattern 42" "1|0" "$(grep -c '^pattern_01.mtp$' "$WORK/s8f.pats")|$(grep -c '^pattern_42.mtp$' "$WORK/s8f.pats")"
printf "tffproj 1\nmt 12345678 2324\npat.parts.mode 1\npat.part.1 2a010101010101010101010101010101 0 2 1 0\npat.part.2 0101012a2a0101010101010101010101 8 1 0 0\npat.start.2 00000000000000000000000000000004\n" >"$sd/Workspace/project.mtff"
run "$sd" "wait 500; parts; partstart; tap Song; wait 300; audio 1; tap F2; wait 3000; parts; tap Play; wait 300; audio 0; tap Pattern; wait 300; dirty; wait 6000"   # the Song screen holds the autosave: back to Pattern first
check "stale: both Parts load, the cells of pattern 42 are 0, every other cell keeps pattern 1" \
    "part 0 len 0 rep 2 flags 1 baked 0 src 0 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1|part 1 len 8 rep 1 flags 0 baked 0 src 1 1 1 0 0 1 1 1 1 1 1 1 1 1 1 1" \
    "$(grep '^part 0 ' "$WORK/out.txt" | head -1)|$(grep '^part 1 ' "$WORK/out.txt" | head -1)"
check "stale: the start step of Part 2 applies" "partstart 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 4" "$(grep '^partstart 1 ' "$WORK/out.txt" | head -1)"
check "host.txt: the status line counts the 3 blanked cells and names the pattern" "1" "$(grep -c 'project.mt was changed outside FF, settings applied, 3 Part cells blanked: pattern 42 gone' "$sd/Plugins/host.txt" 2>/dev/null)"
check "the Parts play (F2 in the Parts view): a Part is the playing pattern" "1" "$(grep '^parts count' "$WORK/out.txt" | sed -n 2p | grep -c ' playing 1 ')"
check "play ran, no crash, the next save writes the blanked cells and no pattern 42" "0|pat.part.1 00010101010101010101010101010101 0 2 1 0" \
    "$(grep -c '^pat\.part\.[0-9]* .*2a' "$sd/Workspace/project.mtff")|$(grep '^pat\.part\.1 ' "$sd/Workspace/project.mtff")"
rm -f "$sd/Plugins/host.txt"   # written on a load only when there is something to say: gone = nothing said
run "$sd" "wait 500; parts"
check "in sync again: loads as saved, nothing blanked, no status line" "1|0" "$(grep -c '^parts count 2 mode 1' "$WORK/out.txt")|$(cat "$sd/Plugins/host.txt" 2>/dev/null | grep -c blanked)"

echo "### 9: versions: the header is read at load (plug::mtffWriterVersion, plug::mtffFormatVersion), a dev file, a newer writer and a newer format"
IFS=. read -r VA VB VC <<<"$VER"
VHEX="$(printf '%x' $(( (VA << 16) | (VB << 8) | VC )))"
sd="$WORK/s9"; fresh "$sd"
run "$sd" "wait 500"
run "$sd" "wait 500; poke mtffver"
check "power up with the file this build wrote: format 1, writer $VER" "1 $VHEX" "$(mtffver)"
rm -f "$sd/Workspace/project.mtff"
run "$sd" "wait 500; poke mtffver"
check "no file: format 0, writer 0 (unknown)" "0 0" "$(mtffver)"
# a dev file: the bare header
printf 'tffproj 1\nstrum.ramp 40\nx.keep me\n' >"$sd/Workspace/project.mtff"
run "$sd" "wait 500; poke strumdepth; poke mtffver"
check "bare header: format 1, writer unknown (0), the key applies" "40|1 0" "$(depth)|$(mtffver)"
n="$(grep -c 'written by FF\|newer format' "$sd/Plugins/host.txt" 2>/dev/null || true)"
check "bare header: no status line" "0" "${n:-0}"
run "$sd" "wait 500; ${PAGE}; $(strum 1); poke strumdepth; wait 15000"
check "bare header: after a save the file has this build's header, the key and the unknown line" \
    "$HDR|mt $(crc_of "$sd/Workspace/project.mt") $(size_of "$sd/Workspace/project.mt")|strum.ramp 50|x.keep me" "$(tr '\n' '|' <"$sd/Workspace/project.mtff" 2>/dev/null | sed 's/|$//')"
# a newer writer, same format (a downgrade)
printf 'tffproj 1 ff=1.5.0 build=cafe123 cap=40000\nstrum.ramp 40\nfx.future 3 4\nx.newkey hello world\n' >"$sd/Workspace/project.mtff"
run "$sd" "wait 500; poke strumdepth; poke mtffver"
check "newer writer: loads, the known key applies, format 1 writer 1.5.0" "40|1 10500" "$(depth)|$(mtffver)"
check "newer writer: host.txt carries the one status line" "1" "$(grep -c 'project.mtff was written by FF 1.5.0: settings this build does not know are kept' "$sd/Plugins/host.txt" 2>/dev/null)"
run "$sd" "wait 500; ${PAGE}; $(strum 1); poke strumdepth; wait 15000"
check "newer writer: after a save the header is this build's, the unknown lines are kept byte exact" \
    "$HDR|mt $(crc_of "$sd/Workspace/project.mt") $(size_of "$sd/Workspace/project.mt")|strum.ramp 50|fx.future 3 4|x.newkey hello world" "$(tr '\n' '|' <"$sd/Workspace/project.mtff" 2>/dev/null | sed 's/|$//')"
# a newer format: nothing applied, never rewritten
NEWER='tffproj 2 ff=1.5.0 build=cafe123 cap=40000\nstrum.ramp 40\nfx.future 3 4\n'
printf "$NEWER" >"$sd/Workspace/project.mtff"
cp "$sd/Workspace/project.mtff" "$WORK/newer.keep"
run "$sd" "wait 500; poke strumdepth; poke mtffver"
check "newer format: nothing applied (depth 0), format 2 writer 1.5.0" "0|2 10500" "$(depth)|$(mtffver)"
check "newer format: host.txt carries the one status line" "1" "$(grep -c 'project.mtff is of a newer format (2, FF 1.5.0): settings not applied, file kept' "$sd/Plugins/host.txt" 2>/dev/null)"
run "$sd" "wait 500; ${PAGE}; $(strum 1); poke strumdepth; wait 15000"
check "newer format: a change and a save leave the file byte for byte as it was" "same" "$(cmp -s "$sd/Workspace/project.mtff" "$WORK/newer.keep" && echo same || echo changed)"

echo "### 10: the on screen notice (core/interface/core/interfaceMisc.cpp, plug::takeOpenNotice): a user's open of a newer writer or a newer format shows one popup"
sd="$WORK/s10"; fresh "$sd"
NEST10="Projects/Albums_2026/Summer_Tour_Warmup_Sessions"
mkdir -p "$sd/$NEST10"
run "$sd" "wait 500; poke strumdepth 60; poke fmsave $NEST10 Notice_Take; wait 4000"
P10="$sd/$NEST10/Notice_Take"
pops() { grep '^watch ' "$WORK/poke.out" | tail -1 | sed 's/.* pops=\([0-9]*\) .*/\1/'; }
popfirst() { grep '^watch ' "$WORK/poke.out" | tail -1 | sed "s/.* text='\(.*\)'.*/\1/"; }
# the popup screens against goldens/mtff/ (byte equal PNGs, MTFF_UPDATE=1 writes them), a copy to $MTFF_PNG_DIR when set
GOLD="$HERE/goldens/mtff"
golden() {
    local name="$1" shot="$2"
    [ -s "$shot" ] || { fail "golden $name" "no screenshot taken"; return; }
    [ -z "${MTFF_PNG_DIR:-}" ] || cp "$shot" "$MTFF_PNG_DIR/$name.png"
    if [ -n "${MTFF_UPDATE:-}" ]; then mkdir -p "$GOLD"; cp "$shot" "$GOLD/$name.png"; pass "golden $name written"; return; fi
    if cmp -s "$shot" "$GOLD/$name.png"; then pass "golden $name: the screen is byte equal to goldens/mtff/$name.png"
    else fail "golden $name" "differs from goldens/mtff/$name.png (MTFF_UPDATE=1 rewrites it)"; fi
}
OPEN10="poke watch reset; poke fmopen $NEST10 Notice_Take; wait 6000; poke popuplines; poke watch"
# no file, then this build's own file: no popup
run "$sd" "wait 500; $OPEN10"
check "a project of this build: no popup" "0" "$(pops)"
# a newer writer, same format
printf 'tffproj 1 ff=1.5.0 build=cafe123 cap=40000\nstrum.ramp 40\nfx.future 3 4\n' >"$P10/project.mtff"
run "$sd" "wait 500; $OPEN10"
check "newer writer: one popup, the first line names the writer" "1|Saved by newer FF 1.5.0" "$(pops)|$(popfirst)"
check "newer writer: the host.txt line stays" "1" "$(grep -c 'project.mtff was written by FF 1.5.0: settings this build does not know are kept' "$sd/Plugins/host.txt" 2>/dev/null)"
run "$sd" "wait 500; poke fmopen $NEST10 Notice_Take; wait 1500; poke popuplines; shot $WORK/pop_a.png; wait 6000; poke popuplines"
check "newer writer: both lines on screen, gone after the time" "popuplines 'Saved by newer FF 1.5.0' 'Some settings ignored'|popuplines none" \
    "$(grep '^popuplines' "$WORK/poke.out" | head -1)|$(grep '^popuplines' "$WORK/poke.out" | tail -1)"
golden mtff-popup-newer-writer "$WORK/pop_a.png"
# not on a Workspace reload of the same project (a new ui_shot run loads the Workspace, as the boot does), not on a save
run "$sd" "poke watch reset; wait 3000; poke watch; ${PAGE}; $(strum 1); poke watch; wait 15000; poke watch"
check "newer writer: no popup on the Workspace reload at power up, none on a change and save (autosave path)" "0" "$(pops)"
check "newer writer: the Workspace file still holds the unknown lines (reload did not rewrite)" "1" "$(grep -c '^fx.future 3 4' "$sd/Workspace/project.mtff")"
# opening again is a new open: one more popup
run "$sd" "wait 500; $OPEN10"
check "newer writer: a second open shows it once more" "1" "$(pops)"
# a newer format: refused
printf 'tffproj 2 ff=1.5.0 build=cafe123 cap=40000\nstrum.ramp 40\nfx.future 3 4\n' >"$P10/project.mtff"
run "$sd" "wait 500; $OPEN10"
check "newer format: one popup naming the format" "1|Project from newer FF (format 2)" "$(pops)|$(popfirst)"
check "newer format: the host.txt line stays" "1" "$(grep -c 'project.mtff is of a newer format (2, FF 1.5.0): settings not applied, file kept' "$sd/Plugins/host.txt" 2>/dev/null)"
run "$sd" "wait 500; poke fmopen $NEST10 Notice_Take; wait 1500; poke popuplines; shot $WORK/pop_b.png; wait 6000"
check "newer format: both lines on screen" "popuplines 'Project from newer FF (format 2)' 'FF settings not loaded'" "$(grep '^popuplines' "$WORK/poke.out" | head -1)"
golden mtff-popup-newer-format "$WORK/pop_b.png"
run "$sd" "poke watch reset; wait 3000; poke watch; ${PAGE}; $(strum 1); poke watch; wait 15000; poke watch"
check "newer format: no popup on the Workspace reload, none on a change and save" "0" "$(pops)"
cp "$P10/project.mtff" "$WORK/s10.keep"
run "$sd" "wait 500; poke fmopen $NEST10 Notice_Take; wait 6000"
run "$sd" "wait 500; poke fmsave $NEST10 Notice_Copy; wait 6000; poke popuplines"
check "newer format: Save As copies the file unchanged and shows nothing more" "same|1" \
    "$(cmp -s "$sd/$NEST10/Notice_Copy/project.mtff" "$WORK/s10.keep" && echo same || echo changed)|$(grep -c '^popuplines none' "$WORK/poke.out")"
# an older writer and a bare dev header: no popup
printf 'tffproj 1 ff=1.0.0 build=old cap=20480\nstrum.ramp 40\n' >"$P10/project.mtff"
run "$sd" "wait 500; $OPEN10"
check "older writer: no popup" "0" "$(pops)"

echo "### 11: Save and Save As of a project whose project.mtff is of a newer format always work: the file stays byte for byte, settings changed here are dropped"
sd="$WORK/s11"; fresh "$sd"
mkdir -p "$sd/Projects/Refuse"
P11="$sd/Projects/Refuse/Take"
run "$sd" "wait 500; poke strumdepth 60; poke fmsave Projects/Refuse Take; wait 4000"
printf 'tffproj 2 ff=1.5.0 build=cafe123 cap=40000\nmt 0000aaaa 2324\nstrum.ramp 40\nfx.future 3 4\n' >"$P11/project.mtff"
cp "$P11/project.mtff" "$WORK/s11.keep"
same() { cmp -s "$1" "$WORK/s11.keep" && echo same || echo changed; }
# Save (in place): open, change a setting, the Workspace save and the project step return success, nothing of the change reaches a file
run "$sd" "wait 500; poke fmopen Projects/Refuse Take; wait 6000; poke strumdepth; poke strumdepth 30; poke sidecar wssave Workspace; poke sidecar save Workspace Projects/Refuse/Take"
check "Save: open applies nothing, the change is live (0 then 30), the Workspace save and the project step both report success" \
    "0|30|1|1" "$(grep '^strumdepth ' "$WORK/poke.out" | sed -n 1p | awk '{print $2}')|$(grep '^strumdepth ' "$WORK/poke.out" | sed -n 2p | awk '{print $2}')|$(grep '^sidecar wssave' "$WORK/poke.out" | sed 's/.*rc=//')|$(grep '^sidecar save' "$WORK/poke.out" | sed 's/.*rc=//')"
check "Save: Workspace file and the project's file are the newer file byte for byte" "same|same" "$(same "$sd/Workspace/project.mtff")|$(same "$P11/project.mtff")"
check "Save: host.txt says the changed settings are not saved" "1" "$(grep -c 'project.mtff is of a newer format (2): FF settings changed here are not saved, file kept' "$sd/Plugins/host.txt" 2>/dev/null)"
check "Save: no .tmp or .bak left next to either file" "0" "$(ls "$sd/Workspace" "$P11" | grep -c 'project.mtff\.\(tmp\|bak\)')"
# the real Save, the file manager's (same project folder and name) with the change made first
run "$sd" "wait 500; poke fmopen Projects/Refuse Take; wait 6000; poke strumdepth 30; poke fmsave Projects/Refuse Take; wait 6000"
check "Save (file manager): both files unchanged" "same|same" "$(same "$sd/Workspace/project.mtff")|$(same "$P11/project.mtff")"
# Save As into another folder: the file is copied unchanged
run "$sd" "wait 500; poke fmopen Projects/Refuse Take; wait 6000; poke strumdepth 30; poke fmsave Projects/Refuse Take2; wait 6000"
check "Save As: the copy in the new folder, the Workspace file and the old project's file are all the newer file byte for byte" "same|same|same" \
    "$(same "$sd/Projects/Refuse/Take2/project.mtff")|$(same "$sd/Workspace/project.mtff")|$(same "$P11/project.mtff")"
# autosave and Workspace: a change through the UI row, the autosave path, then a power up (Workspace reload): the edit did not leak, the file is as it was
run "$sd" "wait 500; poke fmopen Projects/Refuse Take; wait 6000; ${PAGE}; $(strum 1); poke strumdepth; wait 15000"
check "autosave: the change was made (depth not 0)" "1" "$([ "$(depth)" != 0 ] && echo 1 || echo 0)"
check "autosave: the Workspace file and the project's file stay as they were" "same|same" "$(same "$sd/Workspace/project.mtff")|$(same "$P11/project.mtff")"
run "$sd" "wait 500; poke strumdepth; poke mtffver"
check "Workspace reload at power up: the dropped edit does not come back (depth 0), the file still refuses (format 2)" "0|2 10500" "$(depth)|$(mtffver)"
# power loss mid save: what a cut leaves behind in the project folder (the replace of the project's file: file gone and .bak holding it, a half .tmp) and in the Workspace (half .tmp)
mv "$P11/project.mtff" "$P11/project.mtff.bak"
head -c 20 "$WORK/s11.keep" >"$P11/project.mtff.tmp"
head -c 30 "$WORK/s11.keep" >"$sd/Workspace/project.mtff.tmp"
run "$sd" "wait 500; poke fmopen Projects/Refuse Take; wait 6000; poke strumdepth 30; poke fmsave Projects/Refuse Take; wait 6000; poke strumdepth"
check "power loss leftovers: the open recovers the .bak, the save succeeds, both files are the newer file again" "same|same|30" \
    "$(same "$sd/Workspace/project.mtff")|$(same "$P11/project.mtff")|$(depth)"
run "$sd" "wait 500; poke strumdepth; poke mtffver"
check "after the power loss round: reload still refuses and applies nothing" "0|2 10500" "$(depth)|$(mtffver)"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
