#!/bin/bash
# midi_pc_sim.sh
#
# MIDI Program Change in (Config > MIDI > Program in, ref/midi-pc-in.md) outside the Synth play page, on the real UI, the real mtMidi.cpp handlers
# and the real engines of this tree (ui_shot, the host build, nothing here touches a device). The page rows are PC2 in synth_play_sim.sh.
# midipc / dinpc CH PROGRAM inject the program on USB / DIN, "pcin 0|1" sets the Config value like the menu row does, "poke programin" reads it,
# "poke rawvals P" prints the values synth part P plays now (what the Patch FX writes, the open slot stays the same).
#
# Rows:
#   C1  Config > MIDI: Program in is the last row of the group (20 rows), Off by default, the menu turns it On
#   PC1 sequencer mode: channel N is Synth N, program 0..15 is the bank slot, applied like the Patch FX (no card access, the open slot stays),
#       a program above 15, a slot with no file, another channel and the row Off change nothing
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
WORK="$(mktemp -d "${TMPDIR:-/tmp}/midi_pc_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }

# run NAME "script": the project starts empty, part 1 is instrument 64. $WORK/NAME.poke.out holds what the pokes printed
run() {
    local n="$1"
    rm -rf "$WORK/$n"; mkdir -p "$WORK/$n"
    UISHOT_POKE="$WORK/$n.poke" ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$n" "wait 500; instr 64; audio 1; $2" >"$WORK/$n.out" 2>"$WORK/$n.err"
    [ -f "$WORK/$n.poke.out" ] || : >"$WORK/$n.poke.out"
}
sps() { grep '^synthplay' "$WORK/$1.poke.out" | sed -n "$2p" | sed 's/^synthplay //'; }
raw() { grep '^rawvals' "$WORK/$1.poke.out" | sed -n "$2p" | awk '{print $NF}'; }
PS='poke synthplay'
# slots of part 0: 0 1 3, of part 1: 0 2 (the Synth editor's save, "poke slotfill PART SLOT PERCENT")
SETUPAB="tap Params; wait 1500; poke slotfill 0 0 50; poke slotfill 0 1 20; poke slotfill 0 3 80; poke slotfill 1 0 30; poke slotfill 1 2 70; tap Pattern; wait 1200"

# --- C1 the Config row: last of the MIDI group, Off, the menu turns it On (Config, MIDI is the third group, 20 rows, F8 edits, Down steps the value up, Up down)
run c1 "poke programin; tap Config; wait 800; tap Down 2; wait 300; tap F8; wait 500; tap Down 19; wait 300; tap F8; wait 300; tap Down; wait 200; tap F8; wait 500; poke programin; tap F8; wait 300; tap Up; wait 200; tap F8; wait 500; poke programin"
expect "C1 Program in is Off by default" "programin 0" "$(grep '^programin' "$WORK/c1.poke.out" | sed -n 1p)"
expect "C1 the last row of Config > MIDI (the 20th) turns it On, and Off again" "programin 1|programin 0" "$(grep '^programin' "$WORK/c1.poke.out" | sed -n 2p)|$(grep '^programin' "$WORK/c1.poke.out" | sed -n 3p)"

# --- PC1 sequencer mode (no page): channel N is Synth N, program = bank slot, applied like the Patch FX, nothing changes with the row Off
run pc1 "$SETUPAB; $PS; poke rawvals 0; poke rawvals 1; midipc 1 1; poke rawvals 0; pcin 1; midipc 1 1; poke rawvals 0; midipc 1 2; poke rawvals 0; midipc 1 3; poke rawvals 0; dinpc 1 0; poke rawvals 0; midipc 1 16; midipc 1 127; dinpc 1 17; poke rawvals 0; midipc 2 2; poke rawvals 1; midipc 4 1; midipc 3 1; midipc 16 5; poke rawvals 0; poke rawvals 1; pcin 0; midipc 1 3; poke rawvals 0"
expect "PC1 the sequencer is on its own page (mode 0), the page is not open" "mode 0 instr 64 budget 4 open 0 confirm 0" "$(sps pc1 1 mode)"
expect "PC1 Program in Off: a program on channel 1 changes nothing" "same" "$([ "$(raw pc1 1)" = "$(raw pc1 3)" ] && echo same || echo different)"
expect "PC1 Program in On, USB program 1 on channel 1: slot 1 of Synth 1 is applied, values differ from slot 0's" "different" "$([ "$(raw pc1 1)" != "$(raw pc1 4)" ] && echo different || echo same)"
expect "PC1 the open slot of Synth 1 stays 0 in every print (the Patch FX way, nothing is saved)" "0" "$(grep '^rawvals part 0 ' "$WORK/pc1.poke.out" | awk '{print $5}' | sort -u | tr -d '\n')"
expect "PC1 program 2 (the slot has no file of the part): the values of slot 1 stay" "same" "$([ "$(raw pc1 4)" = "$(raw pc1 5)" ] && echo same || echo different)"
expect "PC1 program 3: slot 3, other values than slot 1 and slot 0" "different|different" "$([ "$(raw pc1 5)" != "$(raw pc1 6)" ] && echo different || echo same)|$([ "$(raw pc1 1)" != "$(raw pc1 6)" ] && echo different || echo same)"
expect "PC1 DIN program 0: slot 0, the values it had at the start" "same" "$([ "$(raw pc1 1)" = "$(raw pc1 7)" ] && echo same || echo different)"
expect "PC1 programs 16, 127 and 17 (above 15) are dropped, slot 0 stays" "same" "$([ "$(raw pc1 1)" = "$(raw pc1 8)" ] && echo same || echo different)"
expect "PC1 channel 2 program 2: Synth 2 gets slot 2, its values differ from the start" "different" "$([ "$(raw pc1 2)" != "$(raw pc1 9)" ] && echo different || echo same)"
expect "PC1 channel 4, channel 3 (Synth 3 has no slot files) and channel 16: Synth 1 and 2 untouched" "same|same" "$([ "$(raw pc1 1)" = "$(raw pc1 10)" ] && echo same || echo different)|$([ "$(raw pc1 9)" = "$(raw pc1 11)" ] && echo same || echo different)"
expect "PC1 the row Off again: a program changes nothing" "same" "$([ "$(raw pc1 10)" = "$(raw pc1 12)" ] && echo same || echo different)"

if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
