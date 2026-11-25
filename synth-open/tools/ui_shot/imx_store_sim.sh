#!/bin/bash
# imx_store_sim.sh UI_SHOT_BINARY [NAME]
#
# The project store (core/FileManager/projectSidecar.cpp, project.mtff) on a Tracker+ or Mini simulator: the IMX core has the master insert
# host and Parts (core/makefile PLATFORM=IMX) but, in the plus and mini pins, no open synth stack, and the store runs on its own card layer
# (projectStoreIo.h cardStat). Host only, NOT firmware, no device. Every run has its own SD card folder.
#   S1 a seed set and the project dirty: the Workspace save (autosave) writes Workspace/project.mtff with the header and "seed.take 42"
#   S2 a restart (a new process on the same card) reads it back: the seed is 42 before anything is set
#   S3 Save As SeedSong, the seed Off, Save As PlainSong, New, Open SeedSong: the seed is back, PlainSong opens Off, New is Off
#   S4 a project without the file plays as before: a fresh card reads seed 0 and writes no seed key
#   S5 the stamp: the "mt" line is the CRC and size of the project.mt the same save wrote
#   S6 a file from an original Tracker (strum ramp, scale snap, Parts, a key of a later build): a Tracker+ or Mini save keeps every line, the
#      Parts list in the codec's dense form (parts 1..count, as on the original Tracker), a key it does not know verbatim
# Called by plus_smoke.sh and mini_smoke.sh. Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)".
set -uo pipefail
BIN="${1:?usage: imx_store_sim.sh UI_SHOT_BINARY [NAME]}"
NAME="${2:-imx}"
[ -x "$BIN" ] || { echo "imx_store_sim.sh: no ui_shot at $BIN"; echo "RESULT: RED (no binary)"; exit 1; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/imx_store_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s %s\n' "$NAME" "$1"; else fails=$((fails+1)); printf 'FAIL  %s %s\n      expected: %s\n      got:      %s\n' "$NAME" "$1" "$2" "$3"; fi; }
run() { "$BIN" "$1" "$2" 2>"$WORK/err.log"; }

sd="$WORK/persist"
run "$sd" "wait 500; seed 42; dirty; wait 12000" >/dev/null
f="$sd/Workspace/project.mtff"
expect "S1 the Workspace save writes project.mtff" "present" "$([ -s "$f" ] && echo present || echo absent)"
expect "S1 the file starts with the tffproj header" "tffproj" "$(head -c 7 "$f" 2>/dev/null)"
expect "S1 the file holds the seed" "seed.take 42" "$(grep '^seed' "$f" 2>/dev/null)"
expect "S1 no plugins.txt (no insert in the project)" "absent" "$([ -e "$sd/Workspace/plugins.txt" ] && echo present || echo absent)"
expect "S2 a restart reads the seed back" "seed 42" "$(run "$sd" "wait 500; seed" | grep '^seed ' | tail -1)"

sd="$WORK/saveas"
out=$(run "$sd" "wait 500; seed 77; dirty; wait 500; saveproj SeedSong; seed 0; saveproj PlainSong; newproj; seed; openproj SeedSong; seed; openproj PlainSong; seed" \
      | grep -E '^(seed|saveproj|openproj|newproj) ' | tr '\n' '|' | sed 's/|$//')
expect "S3 Save As, New, Open: the seed is saved with the project and comes back" \
  "seed 77|saveproj ok status 0|seed 0|saveproj ok status 0|newproj ok status 0|seed 0|openproj ok status 0|seed 77|openproj ok status 0|seed 0" "$out"
expect "S3 SeedSong holds the key, PlainSong has none" "seed.take 77 absent" \
  "$(grep -h '^seed' "$sd"/Projects/SeedSong/project.mtff 2>/dev/null) $(grep -q '^seed' "$sd"/Projects/PlainSong/project.mtff 2>/dev/null && echo present || echo absent)"

sd="$WORK/fresh"
expect "S4 a fresh card reads seed 0 (a project without the file plays as before)" "seed 0" "$(run "$sd" "wait 500; seed" | grep '^seed ' | tail -1)"
run "$sd" "wait 500; dirty; wait 12000" >/dev/null
expect "S4 a save with nothing set writes no seed key" "absent" "$(grep -q '^seed' "$sd/Workspace/project.mtff" 2>/dev/null && echo present || echo absent)"

# the CRC32 in the last four bytes of a project.mt (little endian), as the file manager reads it, 8 hex digits
crc_of() { od -An -tx1 -j "$(( $(stat -f %z "$1") - 4 ))" -N4 "$1" | awk '{printf "%s%s%s%s", $4, $3, $2, $1}'; }
sd="$WORK/stamp"
run "$sd" "wait 500; seed 5; dirty; wait 12000" >/dev/null
f="$sd/Workspace/project.mtff"; mt="$sd/Workspace/project.mt"
expect "S5 the mt line is the CRC and size of the saved project.mt" "mt $(crc_of "$mt" 2>/dev/null) $(stat -f %z "$mt" 2>/dev/null)" "$(grep '^mt ' "$f" 2>/dev/null)"

OG="strum.ramp 40|track.3.snap 1|pat.parts.mode 1|pat.part.3 01020304050607080910111213141516 16 2 1 0|zz.later 9"
P0="00000000000000000000000000000000 0 0 0 0"
OGW="strum.ramp 40|track.3.snap 1|pat.parts.mode 1|pat.part.1 $P0|pat.part.2 $P0|pat.part.3 01020304050607080910111213141516 16 2 1 0|zz.later 9"
printf '%s\n' "strum.ramp 40" "track.3.snap 1" "pat.parts.mode 1" "pat.part.3 01020304050607080910111213141516 16 2 1 0" "zz.later 9" >>"$f"
expect "S6 the original Tracker lines load without changing the seed" "seed 5" "$(run "$sd" "wait 500; seed" | grep '^seed ' | tail -1)"
run "$sd" "wait 500; seed 6; dirty; wait 12000" >/dev/null
expect "S6 the rewrite holds the new seed" "seed.take 6" "$(grep '^seed' "$f" 2>/dev/null)"
expect "S6 the rewrite keeps every original Tracker line" "$OGW" "$(grep -E '^(strum|track\.3\.snap|pat\.|zz\.)' "$f" 2>/dev/null | tr '\n' '|' | sed 's/|$//')"
expect "S6 the stamp follows the new project.mt" "mt $(crc_of "$mt" 2>/dev/null) $(stat -f %z "$mt" 2>/dev/null)" "$(grep '^mt ' "$f" 2>/dev/null)"

if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
