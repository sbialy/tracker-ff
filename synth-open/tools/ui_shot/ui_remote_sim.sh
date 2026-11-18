#!/bin/bash
# ui_remote_sim.sh
#
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
BUILD="$ROOT/build/ui-shot-ext"
BIN="$BUILD/ui_shot"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/ui_remote_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
make -s -C "$HERE" -j4 EXTHOOKS=1 >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() {   # run NAME "script": stdout of the script plus the poke lines
    mkdir -p "$WORK/$1"; rm -f "$WORK/poke" "$WORK/poke.out"
    UISHOT_POKE="$WORK/poke" "$BIN" "$WORK/$1" "wait 500; $2" 2>"$WORK/$1.err" | grep -E '^(pattern|step|cursor) '
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
# R5 encoder, on the synth patch editor value (Ratio 1)
SYN='instr 64; tap Params; wait 1200; tap F8; wait 1500; tap Right; wait 400; tap Down; wait 800'
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
run r7 "tap Config; wait 800; tap Down 4; wait 300; tap F8; wait 500; tap Down 3; wait 300; tap F8; wait 500; tap F8; wait 300; tap Down; wait 200; tap F8; wait 3000" >/dev/null
expect "R7 the page writes midirmt.cfg with on 1" "yes" "$(grep -q '^on 1' "$WORK/r7/Features/midirmt.cfg" 2>/dev/null && echo yes || echo no)"
out=$(run r7 "tap Master; wait 500; midich 16 58; midichoff 16 58; wait 300; status" | sed 's/^pattern \([01]\) .*/\1/')
expect "R7 and the next start has the remote on" "1" "$out"

echo
if [ "$fails" = 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
