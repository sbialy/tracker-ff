#!/bin/bash
# plus_remote_sim.sh [plus|mini]   (ref/ff-imx-port-plan.md P9)
#
# The MIDI remote (ui_remote_sim.sh, core/source/midiRemote.h) and the Play stop lock on the Tracker+ and Mini sims (FEATURE_FF_FEATURES: the
# Features registry without the extension module, the settings in /Features/<id>.cfg). Run against this tree's BRIDGE=1 plus or mini ui_shot
# (plus_smoke.sh / mini_smoke.sh build it). R5 (the encoder on the synth patch editor of the open stack) is replaced by R5 on the Project
# Settings free rows are not needed: the CC check uses the Config > Metronome tempo value, as the encoder moves the selected list value.
# Idea 17, the MIDI remote (ref/idea-ui.md, core/source/midiRemote.h) on the EXTHOOKS=1 ui_shot. A USB note or CC on the remote's channel
# (default 16, first note 36) drives the Tracker's own keys, pads and encoder, through the same entry points the hardware uses.
#   R0  feature off (no file): a key note does nothing, the note is played as before
#   R1  on: note 58 (36 + 22) is the Pattern key, a note off releases it
#   R2  Shift held by note: Shift + Play stops a song under the Play stop lock, Play alone does not (the Shift combo reads the held key)
#   R3  another channel and notes below the first note are not the remote's
#   R4  pads: note 36 + 33 + 12 is pad 12, the same step as the `pad 12` script command
#   R5  CC 96 / 97 turn the encoder up / down (the synth patch value moves up / down), a value under 64 does nothing
#   R6  the page writes midirmt.cfg and the settings are read at the next start (channel 3, first note 48)
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
M="${1:-plus}"
BUILD="$ROOT/build/ui-shot-$M-console-bridge"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"
[ -x "$BIN" ] || { echo "plus_remote_sim: no $BIN (build it with plus_smoke.sh or mini_smoke.sh)" >&2; exit 2; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/ui_remote_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() {   # run NAME "script": stdout of the script plus the poke lines
    mkdir -p "$WORK/$1"; rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$BIN" "$WORK/$1" "wait 500; $2" 2>"$WORK/$1.err" | grep -E '^(pattern|step|cursor|capbuf) '
    cat "$WORK/poke.out" 2>/dev/null
}
cfg() { mkdir -p "$WORK/$1/Features"; printf 'feature 1\nid midirmt\nversion 1\non 1\n%s' "$2" > "$WORK/$1/Features/midirmt.cfg"; }
screen() { sed -n "$1p" | sed 's/^pattern \([01]\) .*/\1/'; }   # pattern screen active, the Nth status line

# R0 off
out=$(run r0 "tap Master; wait 500; status; midich 16 58; midichoff 16 58; wait 300; status")
expect "R0 off: Pattern key note does nothing" "0 0" "$(echo "$out" | sed 's/^pattern \([01]\) .*/\1/' | tr '\n' ' ' | sed 's/ $//')"
# R1 on
cfg r1 ""
out=$(run r1 "tap Master; wait 500; status; midich 16 58; midichoff 16 58; wait 300; status")
expect "R1 on: note 58 opens Pattern (Master first)" "0 1" "$(echo "$out" | sed 's/^pattern \([01]\) .*/\1/' | tr '\n' ' ' | sed 's/ $//')"
# R2 Shift by note under the Play stop lock
mkdir -p "$WORK/r2/Features"; cp "$WORK/r1/Features/midirmt.cfg" "$WORK/r2/Features/"; printf 'feature 1\nid playlock\nversion 1\non 1\n' > "$WORK/r2/Features/playlock.cfg"
out=$(run r2 "tap Pattern; wait 300; midich 16 59; midichoff 16 59; poke playing; midich 16 59; midichoff 16 59; poke playing; midich 16 65; midich 16 59; midichoff 16 59; midichoff 16 65; poke playing" | awk '{printf "%s ", $2}')
expect "R2 start, Play alone does not stop, Shift (note 65) + Play stops" "1 1 0 " "$out"
# R3 other channel, note below the base
out=$(run r3 "tap Master; wait 500; midich 15 58; midichoff 15 58; midich 16 57; midichoff 16 57; wait 300; status" | sed 's/^pattern \([01]\) .*/\1/')
expect "R3 channel 15 and note 57 are not the Pattern key" "0" "$out"
# R4 pads: note 36 + 33 + 12 = 81 is pad 12
cp -r "$WORK/r1" "$WORK/r4"
a=$(run r4 "tap Pattern; wait 300; tap Rec; wait 200; pad 12; step 0 0" | awk '{print $5}')
b=$(run r4 "tap Pattern; wait 300; tap Rec; wait 200; midich 16 81; midichoff 16 81; wait 100; step 0 0" | awk '{print $5}')
expect "R4 remote pad 12 writes the note the pad command writes ($a)" "$a" "$b"
expect "R4 and it is a note" "yes" "$([ "${b:-}" != "" ] && [ "$b" != "-1" ] && echo yes || echo no)"
# R5 encoder, on the synth patch editor value. Plus: Params, F8, Right puts the focus on the parameter list, Down picks Ratio 1.
# Mini (P8): Menu box 1 (Instrument Parameters), F8, the editor starts in its section grid, Mid enters the section (setSection, the value focus),
# Down x3 picks Shape 1, a number value: the Mini runs encoder steps on a list the other way (interfaceGlobals.invertValForList), a number is up on both
if [ "$M" = mini ]; then
    SYN='instr 64; hold Menu; wait 300; tap Right; wait 100; unhold Menu; wait 1200; tap F8; wait 1500; tap Mid; wait 400; tap Down 3; wait 800'
else
    SYN='instr 64; tap Params; wait 1200; tap F8; wait 1500; tap Right; wait 400; tap Down; wait 800'
fi
cp -r "$WORK/r1" "$WORK/r5"
out=$(run r5 "$SYN; poke synthval; midicc 16 96 127; midicc 16 96 127; midicc 16 96 127; poke synthval; midicc 16 97 127; poke synthval; midicc 16 96 10; poke synthval" | sed -n 's/.*norm \(.*\)/\1/p' | tr '\n' ' ')
expect "R5 three steps up, one down, a weak value ignored" "yes" "$(echo "$out" | awk '{print ($2>$1 && $3<$2 && $3>$1 && $4==$3) ? "yes" : "no:" $0}')"
# R6 channel 3, first note 48 from the file
cfg r6 'channel 2
base 48
'
out=$(run r6 "tap Master; wait 500; midich 16 58; midichoff 16 58; wait 200; status; midich 3 70; midichoff 3 70; wait 300; status" | sed 's/^pattern \([01]\) .*/\1/' | tr '\n' ' ')
expect "R6 file channel 3 and base 48: the old setting is dead, note 70 (48 + 22) on channel 3 is Pattern" "0 1 " "$out"
rm -rf "$WORK/r7"; mkdir -p "$WORK/r7"
run r7 "tap Config; wait 800; tap Down 5; wait 300; tap F8; wait 500; tap Down 2; wait 300; tap F8; wait 500; tap F8; wait 300; tap Down; wait 200; tap F8; wait 3000" >/dev/null
expect "R7 the page writes midirmt.cfg with on 1" "yes" "$(grep -q '^on 1' "$WORK/r7/Features/midirmt.cfg" 2>/dev/null && echo yes || echo no)"
out=$(run r7 "tap Master; wait 500; midich 16 58; midichoff 16 58; wait 300; status" | sed 's/^pattern \([01]\) .*/\1/')
expect "R7 and the next start has the remote on" "1" "$out"

# R8 Capture (2.0.0: on by default on every card and image, no /.develop, the take logic is built in here): no capture.cfg, button 3 of the
#    pattern editor captures a played note (capture_sim.sh and features.sh have the rules on the original Tracker). A file of the current
#    version with on 0 is the kill switch, a file of an older version loses its on line (features.cpp version rule)
CAPRUN="tap Pattern; wait 300; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap F4; wait 100; step 0 0"
rm -rf "$WORK/r8" "$WORK/r8off" "$WORK/r8v2"; mkdir -p "$WORK/r8" "$WORK/r8off/Features" "$WORK/r8v2/Features"
printf 'feature 1\nid capture\nversion 3\non 0\n' > "$WORK/r8off/Features/capture.cfg"
printf 'feature 1\nid capture\nversion 2\non 0\n' > "$WORK/r8v2/Features/capture.cfg"
expect "R8 no capture.cfg, no /.develop: Capture is on, button 3 captures the note" "step 0 0 note 36 instr 64" "$(run r8 "$CAPRUN" | grep '^step ' | cut -c1-25)"
expect "R8 capture.cfg version 3 with on 0: off, button 3 captures nothing" "step 0 0 note -1 instr 0" "$(run r8off "$CAPRUN" | grep '^step ' | cut -c1-24)"
expect "R8 capture.cfg version 2 with on 0 (older than the descriptor): on again" "step 0 0 note 36 instr 64" "$(run r8v2 "$CAPRUN" | grep '^step ' | cut -c1-25)"

echo
if [ "$fails" = 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
