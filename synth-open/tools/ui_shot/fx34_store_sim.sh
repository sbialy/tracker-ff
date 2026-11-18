#!/bin/bash
# fx34_store_sim.sh [PNG_DIR]
#
# Where FX3 and FX4 of a pattern step live (core/source/fx34.h, core/FileManager/fx34_io.h): the sparse set in RAM, the sidecar
# pattern_NN.mtx next to every pattern file, bound to the CRC of the .mtp. On the real file manager, undo and Parts of this tree (ui_shot).
#
#   A  the Project Settings page row 4 FX Slots (File F7): on, off, the popups, project.mtff key, the sidecar written only for a non empty set and removed again
#   B  Workspace reload: a power up brings every entry back. Pattern change: the set follows its pattern, also through the double buffer
#   C  the stale rule: the pattern file changed behind the sidecar's back: parked, not applied, not touched, steps aside as .mtx.old
#      when a new FX3 must be written, a damaged sidecar is not used and not deleted
#   D  undo and redo carry FX3 and FX4
#   E  Save As copies the sidecar, Open brings it back, an overwritten project does not keep an old sidecar of a pattern that has none now
#   F  the switch off keeps the data: warning popup, nothing on the card touched, nothing played, switch on restores it
#   G  the cap: 256 extra FX per pattern, the 257th is refused (put says full, the refusal flag is set), nothing is dropped silently
#   H  Parts: a baked Part carries the FX3 and FX4 of every source pattern, a Part image of the playback too
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
NCPU="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/fx34_store.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
SAN="${SAN:-}"   # SAN=address,undefined: the sanitizer build (build/ui-shot-san), a report fails the run
mkdir -p "$ROOT/build/ui-shot${SAN:+-san}"
make -s -C "$HERE" -j"$NCPU" ${SAN:+SAN=$SAN} >"$ROOT/build/ui-shot.make.log" 2>&1 || { tail -30 "$ROOT/build/ui-shot.make.log"; exit 2; }
BIN="$ROOT/build/ui-shot${SAN:+-san}/ui_shot"

fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
expect() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected: $2
      got:      $3"; fi; }
fresh() { rm -rf "$1"; mkdir -p "$1/Plugins/Master" "$1/Plugins/Synth"; }
# ui SD "script": the stdout of the script, the pokes' answers after it
ui() {
    rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" ASAN_OPTIONS=detect_leaks=0 "$BIN" "$1" "$2" 2>"$WORK/err.txt" || { echo "ui_shot failed:"; tail -5 "$WORK/err.txt"; fails=$((fails+1)); }
    [ -f "$WORK/poke.out" ] && cat "$WORK/poke.out"
}
sha() { shasum "$1" 2>/dev/null | cut -c1-12; }
sz() { stat -f %z "$1" 2>/dev/null || echo absent; }
# the CRC in the last four bytes of a .mtp and the one a sidecar is bound to (bytes 8..11), 8 hex digits, little endian
crc_mtp() { od -An -tx1 -j "$(( $(stat -f %z "$1") - 4 ))" -N4 "$1" | awk '{printf "%s%s%s%s", $4, $3, $2, $1}'; }
crc_mtx() { od -An -tx1 -j8 -N4 "$1" | awk '{printf "%s%s%s%s", $4, $3, $2, $1}'; }
# the Project Settings page (File > F7), row 4 FX Slots: a list of Off and On, F8 opens it, enc picks, F8 applies
ON='tap File; wait 800; tap F7; wait 800; tap F8; enc 1; tap F8; wait 300'
SAVE='dirty; wait 15000'

# ---------------------------------------------------------------------------------------------------------------------------------- A
echo "### A the Project Settings page row 4 FX Slots"
sd="$WORK/a"; fresh "$sd"
out="$(ui "$sd" "wait 500; tap File; wait 800; tap F7; wait 800; ${PNG_DIR:+shot $PNG_DIR/fx34-project-off.png;} fx34popup clear; tap F8; enc 1; tap F8; wait 300; fx34popup; fx34state; ${PNG_DIR:+shot $PNG_DIR/fx34-project-on.png;} $SAVE")"
expect "A1 the page row turns the switch on" "1" "$(grep '^fx34state' <<<"$out" | awk '{print $3}')"
expect "A2 the popup says it is not CE compatible" "fx34popup '4 FX on' 'FX3 and FX4, not CE compatible'" "$(grep '^fx34popup' <<<"$out")"
expect "A3 the key was written by the save of the project" "fx.slots 4" "$(grep fx.slots "$sd/Workspace/project.mtff")"
out="$(ui "$sd" "wait 500; instr 48; fx34state; setfx 0 0 2 14 3; setfx 0 1 3 8 22; fx34 0 0; $SAVE")"
expect "A4 after a power up the switch is on, from the key" "1" "$(grep '^fx34state' <<<"$out" | awk '{print $3}')"
expect "A5 the sidecar exists: 12 + 2 entries x 4 + 4 B" "24" "$(sz "$sd/Workspace/patterns/pattern_01.mtx")"
expect "A6 it is bound to the CRC of the pattern file" "$(crc_mtp "$sd/Workspace/patterns/pattern_01.mtp")" "$(crc_mtx "$sd/Workspace/patterns/pattern_01.mtx")"
ui "$sd" "wait 500; setfx 0 0 2 0 0; $SAVE" >/dev/null
expect "A7 one FX cleared: the sidecar has the other one (20 B)" "20" "$(sz "$sd/Workspace/patterns/pattern_01.mtx")"
ui "$sd" "wait 500; setfx 0 1 3 0 0; $SAVE" >/dev/null
expect "A8 the last one cleared: the sidecar is removed" "absent" "$(sz "$sd/Workspace/patterns/pattern_01.mtx")"
out="$(ui "$sd" "wait 500; fx34popup clear; tap File; wait 800; tap F7; wait 800; tap F8; enc -1; tap F8; wait 300; fx34popup; fx34state; $SAVE")"
expect "A9 off with no FX3 and FX4 anywhere: 2 FX slots again" "fx34popup '4 FX off' '2 FX slots again'" "$(grep '^fx34popup' <<<"$out")"
expect "A10 the key is gone from project.mtff" "0" "$(grep -c fx.slots "$sd/Workspace/project.mtff")"
out="$(ui "$sd" "wait 500; tap File; wait 800; tap Play; wait 200; tap F7; wait 800; fx34popup clear; tap F8; enc 1; tap F8; wait 300; fx34popup; fx34state; tap Left; wait 500; tap Play")"
expect "A11 the row is refused while the sequencer plays" "fx34popup '4 FX' 'stop the playback first'" "$(grep '^fx34popup' <<<"$out")"
expect "A12 and the switch did not change" "0" "$(grep '^fx34state' <<<"$out" | awk '{print $3}')"

# ---------------------------------------------------------------------------------------------------------------------------------- B
echo "### B Workspace reload and pattern change"
sd="$WORK/b"; fresh "$sd"
SET='setfx 0 0 2 14 3; setfx 0 2 3 7 11; setfx 3 127 3 8 22; setfx 15 5 2 9 99'
ui "$sd" "wait 500; instr 48; $ON; $SET; $SAVE" >/dev/null
out="$(ui "$sd" "wait 500; fx34state; fx34 0 0; fx34 0 2; fx34 3 127; fx34 15 5")"
expect "B1 a power up: all four entries are back, the switch is on" "fx34state on 1 n 4 parked 0 refused 0" "$(grep '^fx34state' <<<"$out")"
expect "B2 step 0 of track 1: FX4 14:3" "fx34 0 0 r2 14:3 r3 0:0 n 4" "$(grep '^fx34 0 0' <<<"$out")"
expect "B3 step 2: FX3 7:11, last step of track 4: FX3 8:22, track 16: FX4 9:99" "r2 0:0 r3 7:11|r2 0:0 r3 8:22|r2 9:99 r3 0:0" \
    "$(grep -E '^fx34 (0 2|3 127|15 5)' <<<"$out" | sed -E 's/fx34 [0-9]+ [0-9]+ //; s/ n [0-9]+//' | tr '\n' '|' | sed 's/|$//')"
# a second pattern with its own set, back and forth (the double buffer swaps the sets with the patterns)
out="$(ui "$sd" "wait 500; pattern 2; setfx 1 1 3 5 5; fx34state; dirty; wait 15000; pattern 1; fx34state; fx34 0 0; pattern 2; fx34 1 1; fx34 0 0; fx34state; pattern 1; fx34 3 127")"
expect "B4 pattern 2 holds its 1 entry, pattern 1 comes back with its 4, pattern 2 again with its 1" "n 1|n 4|n 4|n 1|n 1|n 1|n 4" \
    "$(grep -E '^fx34' <<<"$out" | grep -oE ' n [0-9]+' | sed 's/^ //' | tr '\n' '|' | sed 's/|$//')"
expect "B5 pattern 2 has its own sidecar (20 B), pattern 1 kept its (32 B)" "20 32" "$(sz "$sd/Workspace/patterns/pattern_02.mtx") $(sz "$sd/Workspace/patterns/pattern_01.mtx")"
expect "B6 the sidecar of pattern 2 is bound to pattern 2's file" "$(crc_mtp "$sd/Workspace/patterns/pattern_02.mtp")" "$(crc_mtx "$sd/Workspace/patterns/pattern_02.mtx")"

# ---------------------------------------------------------------------------------------------------------------------------------- C
echo "### C stale and damaged sidecars"
sd="$WORK/c"; fresh "$sd"
ui "$sd" "wait 500; instr 48; $ON; setfx 0 0 2 14 3; setfx 0 2 3 7 11; $SAVE" >/dev/null
MTX0="$(sha "$sd/Workspace/patterns/pattern_01.mtx")"
python3 -I - "$sd/Workspace/patterns/pattern_01.mtp" <<'PY'
import sys
b = bytearray(open(sys.argv[1], 'rb').read()); b[-1] ^= 0xFF; open(sys.argv[1], 'wb').write(b)   # another CRC, as a 1.3.0 save leaves it
PY
out="$(ui "$sd" "wait 500; fx34state; fx34 0 0")"
expect "C1 the pattern file changed: nothing applied, one sidecar parked, the switch still on" "fx34state on 1 n 0 parked 1 refused 0" "$(grep '^fx34state' <<<"$out")"
expect "C2 the parked sidecar is not touched by the load or by the autosave that follows" "$MTX0" "$(sha "$sd/Workspace/patterns/pattern_01.mtx")"
ui "$sd" "wait 500; instr 48; setstep 0 9 60 48 0 0 0 0; $SAVE" >/dev/null
expect "C3 an edit of the pattern (FX3 and FX4 still empty) saves it and leaves the parked sidecar alone" "$MTX0" "$(sha "$sd/Workspace/patterns/pattern_01.mtx")"
expect "C4 no .mtx.old yet" "absent" "$(sz "$sd/Workspace/patterns/pattern_01.mtx.old")"
ui "$sd" "wait 500; setfx 2 2 3 6 6; $SAVE" >/dev/null
expect "C5 a new FX3: the parked file steps aside, byte for byte" "$MTX0" "$(sha "$sd/Workspace/patterns/pattern_01.mtx.old")"
expect "C6 the new sidecar is bound to the current pattern file (1 entry, 20 B)" "$(crc_mtp "$sd/Workspace/patterns/pattern_01.mtp") 20" "$(crc_mtx "$sd/Workspace/patterns/pattern_01.mtx") $(sz "$sd/Workspace/patterns/pattern_01.mtx")"
ui "$sd" "wait 500; setfx 2 2 3 0 0; $SAVE" >/dev/null
expect "C7 the new one cleared: its sidecar goes, the .mtx.old stays" "absent $MTX0" "$(sz "$sd/Workspace/patterns/pattern_01.mtx") $(sha "$sd/Workspace/patterns/pattern_01.mtx.old")"
# a damaged sidecar: not used, not deleted
sd="$WORK/c2"; fresh "$sd"
ui "$sd" "wait 500; instr 48; $ON; setfx 0 0 2 14 3; setfx 0 2 3 7 11; $SAVE" >/dev/null
printf 'garbage' >"$sd/Workspace/patterns/pattern_01.mtx"
out="$(ui "$sd" "wait 500; fx34state")"
expect "C8 a damaged sidecar is not used (n 0) and counted as parked" "fx34state on 1 n 0 parked 1 refused 0" "$(grep '^fx34state' <<<"$out")"
expect "C9 and not deleted" "garbage" "$(cat "$sd/Workspace/patterns/pattern_01.mtx")"
sd="$WORK/c3"; fresh "$sd"
ui "$sd" "wait 500; instr 48; $ON; setfx 0 0 2 14 3; setfx 0 2 3 7 11; $SAVE" >/dev/null
python3 -I - "$sd/Workspace/patterns/pattern_01.mtx" <<'PY'
import sys
b = bytearray(open(sys.argv[1], 'rb').read()); b[13] ^= 0x01; open(sys.argv[1], 'wb').write(b)
PY
expect "C10 one flipped bit in the entries: the file CRC refuses it" "fx34state on 1 n 0 parked 1 refused 0" "$(ui "$sd" "wait 500; fx34state" | grep '^fx34state')"

# ---------------------------------------------------------------------------------------------------------------------------------- D
echo "### D undo and redo"
sd="$WORK/d"; fresh "$sd"
out="$(ui "$sd" "wait 500; instr 48; $ON; setfx 0 0 2 14 3; setfx 0 1 3 8 22; fx34undo store; setfx 0 0 2 9 99; setfx 0 1 3 0 0; setfx 0 2 2 5 5; fx34 0 0; fx34 0 1; fx34undo undo; fx34 0 0; fx34 0 1; fx34 0 2; fx34undo redo; fx34 0 0; fx34 0 1; fx34 0 2")"
fx() { grep -E '^fx34 ' <<<"$out" | sed -E 's/^fx34 //; s/ n [0-9]+$//' | sed -n "$1p"; }
expect "D1 after the edits: step 0 FX4 9:99, step 1 empty" "0 0 r2 9:99 r3 0:0|0 1 r2 0:0 r3 0:0" "$(fx 1)|$(fx 2)"
expect "D2 undo brings the old FX4 and FX3 back, drops the new FX4 of step 2" "0 0 r2 14:3 r3 0:0|0 1 r2 0:0 r3 8:22|0 2 r2 0:0 r3 0:0" "$(fx 3)|$(fx 4)|$(fx 5)"
expect "D3 redo goes forward again" "0 0 r2 9:99 r3 0:0|0 1 r2 0:0 r3 0:0|0 2 r2 5:5 r3 0:0" "$(fx 6)|$(fx 7)|$(fx 8)"

# ---------------------------------------------------------------------------------------------------------------------------------- E
echo "### E Save As and Open"
sd="$WORK/e"; fresh "$sd"
ui "$sd" "wait 500; instr 48; $ON; setfx 0 0 2 14 3; setfx 0 2 3 7 11; dirty; wait 15000; pattern 2; setfx 5 5 3 8 22; dirty; wait 15000; pattern 1; projsave Projects Song1" >/dev/null
expect "E1 Save As copied the sidecar of pattern 1 and of pattern 2" "$(sha "$sd/Workspace/patterns/pattern_01.mtx") $(sha "$sd/Workspace/patterns/pattern_02.mtx")" \
    "$(sha "$sd/Projects/Song1/patterns/pattern_01.mtx") $(sha "$sd/Projects/Song1/patterns/pattern_02.mtx")"
expect "E2 and the key is in the project's project.mtff" "fx.slots 4" "$(grep fx.slots "$sd/Projects/Song1/project.mtff")"
sd2="$WORK/e2"; fresh "$sd2"; mkdir -p "$sd2/Projects"; cp -R "$sd/Projects/Song1" "$sd2/Projects/Song1"
out="$(ui "$sd2" "wait 500; projopen Projects Song1; fx34state; fx34 0 0; pattern 2; fx34 5 5")"
expect "E3 Open on a card that only has the project folder: switch on, pattern 1 has its 2, pattern 2 its 1" "on 1 n 2|r2 14:3 r3 0:0 n 2|r2 0:0 r3 8:22 n 1" \
    "$(grep -E '^fx34' <<<"$out" | sed -E 's/^fx34state on ([01]) n ([0-9]+).*/on \1 n \2/; s/^fx34 [0-9]+ [0-9]+ //' | tr '\n' '|' | sed 's/|$//')"
expect "E4 the Workspace got the sidecars (a Save of it writes them back)" "$(sha "$sd/Projects/Song1/patterns/pattern_01.mtx") $(sha "$sd/Projects/Song1/patterns/pattern_02.mtx")" "$(sha "$sd2/Workspace/patterns/pattern_01.mtx") $(sha "$sd2/Workspace/patterns/pattern_02.mtx")"
# overwrite the project after FX3 and FX4 were cleared: no old sidecar stays behind in the project folder
ui "$sd" "wait 500; setfx 0 0 2 0 0; setfx 0 2 3 0 0; dirty; wait 15000; projsave Projects Song1" >/dev/null
expect "E5 Save As over the same project after clearing pattern 1: its sidecar is gone, pattern 2's stays" "absent $(sha "$sd/Workspace/patterns/pattern_02.mtx")" "$(sz "$sd/Projects/Song1/patterns/pattern_01.mtx") $(sha "$sd/Projects/Song1/patterns/pattern_02.mtx")"

# ---------------------------------------------------------------------------------------------------------------------------------- F
echo "### F switch off keeps the data"
sd="$WORK/f"; fresh "$sd"
ui "$sd" "wait 500; instr 48; $ON; setfx 0 0 2 14 3; setfx 0 2 3 7 11; $SAVE" >/dev/null
H0="$(sha "$sd/Workspace/patterns/pattern_01.mtx") $(sha "$sd/Workspace/patterns/pattern_01.mtp")"
out="$(ui "$sd" "wait 500; fx34popup clear; tap File; wait 800; tap F7; wait 800; tap F8; enc -1; tap F8; wait 300; fx34popup; fx34state; fx34 0 0; $SAVE; ${PNG_DIR:+shot $PNG_DIR/fx34-off-warning.png}")"
expect "F1 off: the popup says the data stays on the card and is not played" "fx34popup '4 FX off' 'FX3 and FX4 kept on the card, not played'" "$(grep '^fx34popup' <<<"$out")"
expect "F2 nothing is applied while off (the set reads empty)" "fx34state on 0 n 0 parked 0 refused 0" "$(grep '^fx34state' <<<"$out")"
expect "F3 the sidecar and the pattern file are the same bytes after the off and its save" "$H0" "$(sha "$sd/Workspace/patterns/pattern_01.mtx") $(sha "$sd/Workspace/patterns/pattern_01.mtp")"
expect "F4 the key is gone from project.mtff" "0" "$(grep -c fx.slots "$sd/Workspace/project.mtff")"
out="$(ui "$sd" "wait 500; fx34state; $SAVE")"
expect "F5 a power up: still off, still nothing applied" "fx34state on 0 n 0 parked 0 refused 0" "$(grep '^fx34state' <<<"$out")"
expect "F6 and the sidecar is still there, untouched" "$H0" "$(sha "$sd/Workspace/patterns/pattern_01.mtx") $(sha "$sd/Workspace/patterns/pattern_01.mtp")"
out="$(ui "$sd" "wait 500; tap File; wait 800; tap F7; wait 800; tap F8; enc 1; tap F8; wait 300; fx34state; fx34 0 0; fx34 0 2")"
expect "F7 switched on again: everything is back" "fx34state on 1 n 2 parked 0 refused 0|fx34 0 0 r2 14:3 r3 0:0 n 2|fx34 0 2 r2 0:0 r3 7:11 n 2" "$(grep '^fx34' <<<"$out" | tr '\n' '|' | sed 's/|$//')"
# the pattern on screen is written with its sidecar at the moment of the off, even without a save before
sd="$WORK/f2"; fresh "$sd"
ui "$sd" "wait 500; instr 48; $ON; setfx 0 0 2 14 3; tap F8; enc -1; tap F8; wait 300; wait 15000" >/dev/null
expect "F8 off right after an edit (not saved yet): the pattern goes to the card with its sidecar first (1 entry, 20 B)" "20" "$(sz "$sd/Workspace/patterns/pattern_01.mtx")"
# Save As of a switched off project carries the sidecars that exist
ui "$sd" "wait 500; projsave Projects Kept" >/dev/null
expect "F9 Save As while off still copies the sidecar that exists (the data is not lost on the way)" "$(sha "$sd/Workspace/patterns/pattern_01.mtx")" "$(sha "$sd/Projects/Kept/patterns/pattern_01.mtx")"
expect "F10 and the copy is a 2 FX project (no key)" "0" "$(grep -c fx.slots "$sd/Projects/Kept/project.mtff")"

# ---------------------------------------------------------------------------------------------------------------------------------- G
echo "### G the cap of 256"
sd="$WORK/g"; fresh "$sd"
out="$(ui "$sd" "wait 500; instr 48; $ON; fx34fill 300; fx34state; setfx 0 0 2 0 0; setfx 3 90 3 77 7; fx34state; setfx 15 100 3 77 7; setfx 0 0 3 1 1; fx34state; $SAVE")"
expect "G1 300 puts: 256 stored, 44 refused" "fx34fill stored 256 full 44" "$(grep '^fx34fill' <<<"$out")"
expect "G2 the refusal flag is set (the pattern editor shows its popup from it) and the set holds 256" "fx34state on 1 n 256 parked 0 refused 1" "$(grep '^fx34state' <<<"$out" | sed -n 1p)"
expect "G3 one entry cleared and one put: n 256 again, and nothing was refused this time" "fx34state on 1 n 256 parked 0 refused 0" "$(grep '^fx34state' <<<"$out" | sed -n 2p)"
expect "G4 one more is refused with a message, nothing dropped, a changed value of an existing entry is not a new one" "setfx full|setfx ok" "$(grep -E '^setfx (full|ok)' <<<"$out" | sed -n '3,4p' | tr '\n' '|' | sed 's/|$//')"
expect "G5 the full sidecar is 12 + 1024 + 4 bytes" "1040" "$(sz "$sd/Workspace/patterns/pattern_01.mtx")"
expect "G6 and loads back as 256" "fx34state on 1 n 256 parked 0 refused 0" "$(ui "$sd" "wait 500; fx34state" | grep '^fx34state')"

# ---------------------------------------------------------------------------------------------------------------------------------- H
echo "### H Parts"
sd="$WORK/h"; fresh "$sd"
out="$(ui "$sd" "wait 500; instr 48; $ON; pattern 1; plen 16; setstep 0 0 60 48 0 0 0 0; setfx 0 0 2 14 3; setfx 0 4 3 7 11; dirty; wait 15000; pattern 2; plen 16; setstep 1 0 72 49 0 0 0 0; setfx 1 0 2 9 9; setfx 1 7 3 5 5; dirty; wait 15000; pattern 1; tap Song; wait 300; hold Shift; tap F8; unhold Shift; wait 200; tap Rec; tap Insert; partset 0 src1 2; tap Rec; wait 200; tap F5; wait 3000; parts; wait 15000")"
BAKED="$(grep '^part 0' <<<"$out" | sed 's/.* baked \([0-9]*\) .*/\1/')"
expect "H1 the Part (track 1 from pattern 1, track 2 from pattern 2, the rest pattern 1) was baked into a new pattern" "3" "$BAKED"
expect "H2 the baked pattern's sidecar: 2 entries of track 1 (pattern 1) and 2 of track 2 (pattern 2), 12 + 16 + 4 B" "32" "$(sz "$sd/Workspace/patterns/pattern_0$BAKED.mtx")"
out="$(ui "$sd" "wait 500; pattern 3; fx34 0 0; fx34 0 4; fx34 1 0; fx34 1 7; fx34state")"
expect "H3 reloaded the baked pattern: track 1 step 0 FX4 14:3, step 4 FX3 7:11, track 2 step 0 FX4 9:9, step 7 FX3 5:5" "r2 14:3 r3 0:0|r2 0:0 r3 7:11|r2 9:9 r3 0:0|r2 0:0 r3 5:5" \
    "$(grep -E '^fx34 ' <<<"$out" | sed -E 's/^fx34 [0-9]+ [0-9]+ //; s/ n [0-9]+$//' | tr '\n' '|' | sed 's/|$//')"
expect "H4 the sources are unchanged" "24 24" "$(sz "$sd/Workspace/patterns/pattern_01.mtx") $(sz "$sd/Workspace/patterns/pattern_02.mtx")"
# the Part image the sequencer plays (Play Song in the Parts view) carries the same entries, the user's pattern comes back after Stop
sd="$WORK/h2"; fresh "$sd"
out="$(ui "$sd" "wait 500; instr 48; $ON; pattern 1; plen 16; setstep 0 0 60 48 0 0 0 0; setfx 0 0 2 14 3; setfx 0 4 3 7 11; dirty; wait 15000; pattern 2; plen 16; setstep 1 0 72 49 0 0 0 0; setfx 1 0 2 9 9; setfx 1 7 3 5 5; dirty; wait 15000; pattern 1; tap Song; wait 300; hold Shift; tap F8; unhold Shift; wait 200; tap Rec; tap Insert; partset 0 src1 2; tap Rec; wait 200; audio 1; tap F2; wait 500; fx34 0 0; fx34 1 0; fx34 1 7; fx34state; tap Play; wait 500; fx34state; fx34 1 0")"
expect "H5 while the Part plays the image has the entries of both sources (4), the track 2 ones from pattern 2" \
    "fx34 0 0 r2 14:3 r3 0:0 n 4|fx34 1 0 r2 9:9 r3 0:0 n 4|fx34 1 7 r2 0:0 r3 5:5 n 4" "$(grep -E '^fx34 ' <<<"$out" | sed -n '1,3p' | tr '\n' '|' | sed 's/|$//')"
expect "H6 after Stop pattern 1 is back with its own 2" "fx34state on 1 n 2 parked 0 refused 0" "$(grep '^fx34state' <<<"$out" | tail -1)"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
