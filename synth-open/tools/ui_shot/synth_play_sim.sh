#!/bin/bash
# synth_play_sim.sh
#
# The Synth play page ("tracker as a synth", ref/synth-play-r1.md) on the real UI, the real mtMidi.cpp note handlers, the real router
# (core/source/synthPlayRouter.cpp), the real shim (synth-open/open_synth.cpp) and the real engines of this tree (ui_shot, the host build,
# nothing here touches a device). Every scenario has its own SD card folder and starts with the project empty and the page entered with
# Shift + Perform from the Pattern page. The MIDI in is injected with the script commands midiono / midionoff (USB, no wait), midich, midicc,
# midibend, dinon / dinoff / dincc / dinbend, midirt (ui_shot_main.cpp). "poke synthplay" prints what the firmware holds (host_poke.cpp):
# the MIDI mode (5 = page), the instrument, the voice budget, per part the live bend (semitones), the bend range and the Voices, the router's
# table (note:state, 1 held, 2 held by the pedal) and "poke voices" the voices of the parts (slot:gate:note:id:fade).
#
# Rows:
#   P1  entry: Shift + Perform opens the page (mode 5), budget 8, the page part has the Voices setting and bend range 2 st, the other parts 0
#       voices. Exit by a page key: mode, budget, bend range, voice counts and instrument are back
#   P2  a chord sounds as 3 voices and ends with the note offs, 8 voices fit and the 9th note steals the oldest (cap 8)
#   P3  the sustain pedal: the note offs wait for the pedal, the pedal up releases them
#   P4  CC 123 and CC 120 release every voice and clear the table, CC 121 centres the bend
#   P5  pitch bend: 2 st default range (+-8192 = +-2 st), Bend column 12 st, DIN bend works the same
#   P6  exit with notes held, pedal down and the wheel bent: every voice released, bend 0, range 1, table empty, budget 4
#   P7  the transport: Play does nothing, Rec starts a take (not the song, synth_rec_sim.sh), external Start, Continue, Song Position and Stop are ignored on the page, MIDI clock is let
#       through, and Start works again after the page (the control that shows the gate is what stops it)
#   P8  entry with a song playing: the stop confirm (Cancel keeps the song and the page it was on, Stop stops the song and opens the page)
#   P9  settings: Input (USB, DIN, set by "poke synthset", it has no column), Channel, Voices, Instrument and Transpose columns
#   P10 pads play the page part, Shift + Delete is the panic
#   P14 plugin voice cap: an engine that declares max_voices 4 has cap 8 on the page and cap 4 outside it ("poke capsim")
#   P4V built-in voice cap: ACD (declares 8) stops at 4 on the Instrument Parameters page, a stored 8 shows as 4, the page still gives 8
#   AB1..AB10 the A/B mode (ref/synth-ab-design.md): Synth B column, 16 voices, never the same part on both sides, the morph (encoder, Shift, CC 11,
#       equal power gains), the Bank columns (the patch swap with notes held), exit restore, the pooled pair 4+4 ("poke ptype"), the morph by audio
#       ("poke abstate", the audio dump and ab_audio_check.py)
#   P11 a page that must not be left half way: Sample Recorder and Sample Editor do not open the page
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)". SAN=address,undefined: the sanitizer build. EXTHOOKS=1 adds P12 (the settings file).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
EXTHOOKS="${EXTHOOKS:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}${EXTHOOKS:+-ext}"
BIN="$BUILD/ui_shot"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/synth_play_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" ${SAN:+SAN=$SAN} ${EXTHOOKS:+EXTHOOKS=$EXTHOOKS} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }

# run NAME "script": the project starts empty, part 1 is instrument 64. $WORK/NAME.poke.out holds what the pokes printed
run() {
    local n="$1"
    rm -rf "$WORK/$n"; mkdir -p "$WORK/$n"
    [ -n "${SEED:-}" ] && { mkdir -p "$WORK/$n/Features"; printf '%b' "$SEED" >"$WORK/$n/Features/synthplay.cfg"; }
    UISHOT_POKE="$WORK/$n.poke" ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$n" "wait 500; instr ${INSTR:-64}; audio 1; $2" >"$WORK/$n.out" 2>"$WORK/$n.err"
    [ -f "$WORK/$n.poke.out" ] || : >"$WORK/$n.poke.out"
}
# line NAME PATTERN N: the Nth line of the pokes that matches the pattern (grep -E), with the key words kept
line() { grep -E "$2" "$WORK/$1.poke.out" | sed -n "${3:-1}p"; }
# one "poke synthplay" prints 6 lines: mode, part 0..2, cells, pedal. sps NAME K WHAT: the Kth print's line WHAT (mode part0 part1 part2 cells pedal)
sps() {
    local k="$2" w="$3" base off
    case "$w" in mode) off=1;; part0) off=2;; part1) off=3;; part2) off=4;; cells) off=5;; pedal) off=6;; esac
    grep '^synthplay' "$WORK/$1.poke.out" | sed -n "$(( (k - 1) * 6 + off ))p" | sed 's/^synthplay //'
}
bendv() { sps "$1" "$2" part0 | awk '{print $4}'; }     # the live bend of part 0 in semitones in the Kth print
rangev() { sps "$1" "$2" part0 | awk '{print $6}'; }    # its bend range
voices() { grep '^voices 0 ' "$WORK/$1.poke.out" | sed -n "$2p" | sed 's/^voices 0 //'; }   # the Kth "poke voices" of part 0
gov() { grep '^govcap' "$WORK/$1.poke.out" | sed -n "$2p" | sed 's/^govcap //'; }
# the gates of the voices of part 0 as "G:N G:N": gate and note of every voice that is on, in record order, a voice already released shows gate 0
gates() { voices "$1" "$2" | sed 's/n=[0-9]* \[ *//; s/ *\]//' | tr ' ' '\n' | awk -F: 'NF>=5 {printf "%s%s:%s", (n++ ? " " : ""), $2, $3} END {print ""}'; }

PV='poke voices'; PS='poke synthplay'; PG='poke govcap'
ENTER="hold Shift; tap Performance; unhold Shift; wait 300"
FROMP="tap Pattern; wait 400"

# ===================================================================== P1 entry and exit
run p1 "poke voicecount 0 2; poke voicecount 1 3; poke voicecount 2 1; $PS; $ENTER; $PS; $FROMP; $PS"
expect "P1 before: pattern mode (0), the project's voice counts 2 3 1, budget 4, range 1" "mode 0 instr 64 budget 4 open 0 confirm 0|part 0 bend 0.000 range 1.0 voices 2|part 1 bend 0.000 range 1.0 voices 3|part 2 bend 0.000 range 1.0 voices 1" \
  "$(sps p1 1 mode)|$(sps p1 1 part0)|$(sps p1 1 part1)|$(sps p1 1 part2)"
expect "P1 on the page: mode 5, budget 8, page part 8 voices and 2 st, the others no voices, range 1" "mode 5 instr 64 budget 16 open 1 confirm 0|part 0 bend 0.000 range 2.0 voices 8|part 1 bend 0.000 range 1.0 voices 0|part 2 bend 0.000 range 1.0 voices 0" \
  "$(sps p1 2 mode)|$(sps p1 2 part0)|$(sps p1 2 part1)|$(sps p1 2 part2)"
expect "P1 after a page key: mode 0, budget 4, ranges 1, the voice counts of the project, the instrument as before" "mode 0 instr 64 budget 4 open 0 confirm 0|part 0 bend 0.000 range 1.0 voices 2|part 1 bend 0.000 range 1.0 voices 3|part 2 bend 0.000 range 1.0 voices 1" \
  "$(sps p1 3 mode)|$(sps p1 3 part0)|$(sps p1 3 part1)|$(sps p1 3 part2)"
INSTR=65 run p1b "$ENTER; $PS; $FROMP; $PS"
expect "P1 the page plays the part of the instrument that was selected (instrument 65 = Synth 2)" "mode 5 instr 65 budget 16 open 1 confirm 0|part 0 bend 0.000 range 1.0 voices 0|part 1 bend 0.000 range 2.0 voices 8" \
  "$(sps p1b 1 mode)|$(sps p1b 1 part0)|$(sps p1b 1 part1)"
run p1c "tap Song; wait 400; $PS; hold Shift; tap Performance; unhold Shift; wait 300; $PS"
expect "P1 Shift + Perform from the Song page opens the page too" "mode 5" "$(sps p1c 2 mode | cut -c1-6)"
run p1d "$ENTER; hold Shift; tap Params; unhold Shift; wait 200; $PS"
expect "P1 Shift + Params on the page itself is the plain Params key: the page closes (to Instrument Parameters)" "mode 0 instr 64 budget 4 open 0 confirm 0" "$(sps p1d 1 mode)"
run p1x "$ENTER; hold Shift; tap Performance; unhold Shift; wait 300; $PS"
expect "P1 Shift + Perform on the page itself is the plain Perform key: the page closes (to Performance)" "mode 0 instr 64 budget 4 open 0 confirm 0" "$(sps p1x 1 mode)"
run p1y "tap Pattern; wait 400; hold Shift; tap Params; unhold Shift; wait 400; $PS"
expect "P1 Shift + Params from the Pattern page is the plain Params key again: the page does not open" "mode 0 instr 64 budget 4 open 0 confirm 0" "$(sps p1y 1 mode)"

# the Pattern page stop resets the live instrument to the step under the cursor (a sample instrument here): the page still opens on the
# part that was selected at the key press, with the pattern stopped and with the song stopped by the confirm (every step holds a note,
# the cursor follows the playing song)
INSTR=65 run p1e "setstep 0 0 60 5 0 0 0 0; cursor; $ENTER; $PS; $FROMP; $PS"
expect "P1 the cursor step holds a sample instrument: the page still plays the selected part (instrument 65 = Synth 2)" "mode 5 instr 65 budget 16 open 1 confirm 0|part 0 bend 0.000 range 1.0 voices 0|part 1 bend 0.000 range 2.0 voices 8" \
  "$(sps p1e 1 mode)|$(sps p1e 1 part0)|$(sps p1e 1 part1)"
expect "P1 and the exit gives the selected instrument back (65), not the step's" "mode 0 instr 65 budget 4 open 0 confirm 0" "$(sps p1e 2 mode)"
INSTR=66 run p1f "$(for st in 0 1 2 3 4 5 6 7 8 9; do printf 'setstep 0 %s 60 5 0 0 0 0; ' $st; done)tap Play; wait 300; $ENTER; $PS; tap F8; wait 300; $PS; midiono 1 60 100; wait 40; $PV"
expect "P1 the stop confirm (the Pattern page stop already reset the live instrument to the step's 5): the page opens on the selected part (instrument 66 = Synth 3) once the song is stopped" "mode 0 instr 5 budget 4 open 0 confirm 1|mode 5 instr 66 budget 16 open 1 confirm 0" "$(sps p1f 1 mode)|$(sps p1f 2 mode)"
expect "P1 the USB note plays on that part (part 2), part 0 stays silent" "no|yes" "$(grep '^voices 0 ' "$WORK/p1f.poke.out" | sed -n 1p | grep -q '1:60' && echo yes || echo no)|$(grep '^voices 2 ' "$WORK/p1f.poke.out" | sed -n 1p | grep -q '1:60' && echo yes || echo no)"

# ===================================================================== P2 chord, 8 voices, stealing
run p2 "$ENTER; midiono 1 60 100; midiono 1 64 100; midiono 1 67 90; wait 40; $PV; $PS; midionoff 1 60; midionoff 1 64; midionoff 1 67; wait 40; $PV; $PS"
expect "P2 a chord: 3 voices gated, table holds 3" "1:60 1:64 1:67|60:1 64:1 67:1" "$(gates p2 1)|$(sps p2 1 cells | sed 's/^cells //')"
expect "P2 the note offs: gates 0 (released), the table is empty" "0:60 0:64 0:67|cells" "$(gates p2 2)|$(sps p2 2 cells)"
run p2b "$ENTER; $(for n in 60 62 64 65 67 69 71 72; do printf 'midiono 1 %s 100; ' $n; done) wait 40; $PV; $PG; midiono 1 74 100; wait 40; $PV; $PG; $PS"
expect "P2 8 notes: 8 voices on, all gated (the pool and the per instrument cap are 8)" "1:60 1:62 1:64 1:65 1:67 1:69 1:71 1:72" "$(gates p2b 1)"
expect "P2 8 notes: the pool counts 8 of 8, no cut" "budget 16 cap 16 sounding 8 cuts 0" "$(gov p2b 1)"
expect "P2 the 9th note takes the oldest voice (60), still 8 voices, still 8 sounding" "1:74 1:62 1:64 1:65 1:67 1:69 1:71 1:72|budget 16 cap 16 sounding 8 cuts 0" "$(gates p2b 2)|$(gov p2b 2)"
expect "P2 the router's table agrees: the oldest note replaced by the 9th" "cells 74:1 62:1 64:1 65:1 67:1 69:1 71:1 72:1" "$(sps p2b 1 cells)"

# ===================================================================== P3 sustain
run p3 "$ENTER; midicc 1 64 127; midiono 1 60 100; midiono 1 64 100; wait 40; midionoff 1 60; wait 40; $PV; $PS; midicc 1 64 0; wait 40; $PV; $PS"
expect "P3 pedal down, key 60 up: both voices still gated, the table shows 60 held by the pedal" "1:60 1:64|cells 60:2 64:1|pedal 1" "$(gates p3 1)|$(sps p3 1 cells)|$(sps p3 1 pedal)"
expect "P3 pedal up: 60 is released, 64 (key still down) is not" "0:60 1:64|cells 64:1|pedal 0" "$(gates p3 2)|$(sps p3 2 cells)|$(sps p3 2 pedal)"

# ===================================================================== P4 CC 123, 120, 121
run p4 "$ENTER; midiono 1 60 100; midiono 1 64 100; midicc 1 64 127; midionoff 1 64; midibend 1 4000; wait 40; midicc 1 123 0; wait 40; $PV; $PS; midiono 1 62 100; wait 20; dincc 1 120 0; wait 40; $PV; $PS; midibend 1 4000; midicc 1 64 127; wait 20; midicc 1 121 0; wait 20; $PS"
expect "P4 CC 123: every voice released, table and pedal empty, bend centred" "0:60 0:64|cells|pedal 0|bend 0.000" "$(gates p4 1)|$(sps p4 1 cells)|$(sps p4 1 pedal)|$(sps p4 1 part0 | awk '{print $3" "$4}')"
expect "P4 CC 120 (from the DIN jack): the same" "0:62|cells|pedal 0" "$(gates p4 2 | awk '{print $NF}')|$(sps p4 2 cells)|$(sps p4 2 pedal)"
expect "P4 CC 121: bend 0, pedal up" "bend 0.000|pedal 0" "$(sps p4 3 part0 | awk '{print $3" "$4}')|$(sps p4 3 pedal)"

# ===================================================================== P5 bend
run p5 "$ENTER; midibend 1 8191; wait 20; $PS; midibend 1 -8192; wait 20; $PS; midibend 1 4096; wait 20; $PS; dinbend 1 -4096; wait 20; $PS; tap F7; enc 10; wait 100; midibend 1 8191; wait 20; $PS; midibend 1 0; wait 20; $PS"
expect "P5 full wheel up (8191), default range: 2 st" "2.000 2.0" "$(bendv p5 1) $(rangev p5 1)"
expect "P5 full wheel down (-8192)" "-2.000" "$(bendv p5 2)"
expect "P5 half wheel up (4096) = 1 st" "1.000" "$(bendv p5 3)"
expect "P5 the DIN jack bends too (-4096 = -1 st)" "-1.000" "$(bendv p5 4)"
expect "P5 Bend column at 12: full wheel is 12 st (11.999 = 8191 / 8192 of it)" "11.999 12.0" "$(bendv p5 5) $(rangev p5 5)"
expect "P5 wheel centred" "0.000" "$(bendv p5 6)"

# ===================================================================== P6 exit cleanup
run p6 "$ENTER; midiono 1 60 100; midiono 1 64 100; midiono 1 67 100; midicc 1 64 127; midionoff 1 67; midibend 1 6000; midicc 1 1 100; wait 100; $PS; $FROMP; wait 50; $PV; $PS; $PG"
expect "P6 before the exit: bend up, pedal down, 3 notes in the table" "cells 60:1 64:1 67:2|pedal 1" "$(sps p6 1 cells)|$(sps p6 1 pedal)"
expect "P6 after the exit: all 3 voices released (gate 0), none held" "0:60 0:64 0:67" "$(gates p6 1)"
expect "P6 after the exit: bend 0, range 1, project voice counts (1 1 1), budget 4, mode 0, table empty, pedal up" "mode 0 instr 64 budget 4 open 0 confirm 0|part 0 bend 0.000 range 1.0 voices 1|cells|pedal 0" \
  "$(sps p6 2 mode)|$(sps p6 2 part0)|$(sps p6 2 cells)|$(sps p6 2 pedal)"
run p6b "$ENTER; midiono 1 60 100; $FROMP; wait 300; midiono 1 62 100; wait 40; $PV"
expect "P6 a USB note after the exit does not reach the part (the page's input is closed, Config > MIDI Notes in is off)" "n=0 [ ]" "$(voices p6b 1 | sed 's/ *$//')"

# ===================================================================== P7 transport
run p7 "$ENTER; poke playing; tap Play; wait 300; poke playing; hold Shift; tap Play; unhold Shift; wait 300; poke playing; tap Rec; wait 100; poke playing; midirt start; wait 200; poke playing; midirt continue; wait 200; poke playing; midirt songpos 64; wait 100; poke playing; midirt stop; wait 100; midiclk 3; poke clockpulses; wait 100; poke playing; $PS"
expect "P7 Play, Shift + Play, Rec (a take, not the song), external Start, Continue, Song Position, Stop, Clock: the song never plays (8 readings)" "playing 0|playing 0|playing 0|playing 0|playing 0|playing 0|playing 0|playing 0" "$(grep '^playing' "$WORK/p7.poke.out" | tr '\n' '|' | sed 's/|$//')"
expect "P7 MIDI clock is let through on the page: 3 pulses in a row are counted" "clockpulses 3" "$(grep '^clockpulses' "$WORK/p7.poke.out" | sed -n 1p)"
expect "P7 the page is still open after all of it" "mode 5 instr 64 budget 16 open 1 confirm 0" "$(sps p7 1 mode)"
run p7b "$ENTER; $FROMP; midirt start; wait 300; poke playing; midirt stop; wait 100; poke playing"
expect "P7 control: after the page the external Start plays again (the gate is what stopped it) and Stop stops" "playing 1|playing 0" "$(grep '^playing' "$WORK/p7b.poke.out" | tr '\n' '|' | sed 's/|$//')"

# ===================================================================== P8 entry with a song playing
run p8 "setstep 0 0 60 1 0 0 0 0; tap Play; wait 300; poke playing; $ENTER; poke playing; $PS; tap F7; wait 300; poke playing; $PS; $ENTER; $PS; tap F8; wait 300; poke playing; $PS; midiono 1 60 100; wait 40; $PV"
expect "P8 a song is playing before" "playing 1" "$(grep '^playing' "$WORK/p8.poke.out" | sed -n 1p)"
expect "P8 Shift + Perform: the confirm shows (the song still plays, the page not open, MIDI mode unchanged)" "playing 1|mode 0 instr 64 budget 4 open 0 confirm 1" "$(grep '^playing' "$WORK/p8.poke.out" | sed -n 2p)|$(sps p8 1 mode)"
expect "P8 Cancel: back on the page it came from, the song still plays, nothing changed" "playing 1|mode 0 instr 64 budget 4 open 0 confirm 0" "$(grep '^playing' "$WORK/p8.poke.out" | sed -n 3p)|$(sps p8 2 mode)"
expect "P8 the confirm again after a Cancel" "mode 0 instr 64 budget 4 open 0 confirm 1" "$(sps p8 3 mode)"
expect "P8 Stop: the song stops, the page opens (mode 5, budget 8)" "playing 0|mode 5 instr 64 budget 16 open 1 confirm 0" "$(grep '^playing' "$WORK/p8.poke.out" | sed -n 4p)|$(sps p8 4 mode)"
expect "P8 and plays" "1:60" "$(gates p8 1)"

# ===================================================================== P9 settings
run p9 "$ENTER; poke synthset 0 1; wait 50; dinon 1 60 100; wait 40; $PV; midiono 1 61 100; wait 40; $PV; poke synthset 0 2; wait 50; midiono 1 62 100; wait 40; dinon 1 63 100; wait 40; $PV; $PS"
expect "P9 Input USB: DIN note ignored" "-" "$(voices p9 1 | sed 's/^n=0 \[ *\]/-/')"
expect "P9 Input USB: the USB note plays" "1:61" "$(gates p9 2)"
expect "P9 Input DIN: the USB note ignored, the DIN note plays (61 still sounding)" "1:61 1:63" "$(gates p9 3)"
run p9b "$ENTER; tap F8; enc 3; wait 50; midich 1 60 100; wait 40; $PV; midich 3 62 100; wait 40; $PV; midicc 1 123 0; midicc 3 123 0; wait 40; $PV"
expect "P9 Channel 3: channel 1 ignored" "-" "$(voices p9b 1 | sed 's/^n=0 \[ *\]/-/')"
expect "P9 Channel 3: channel 3 plays" "1:62" "$(gates p9b 2)"
expect "P9 Channel 3: CC 123 of channel 1 is not the page's, of channel 3 releases" "0:62" "$(gates p9b 3)"
run p9c "$ENTER; enc 1; wait 100; $PS; midiono 1 60 100; wait 40; $PV; midionoff 1 60"
expect "P9 Instrument column: part 1 now has the voices and 2 st, part 0 none and 1 st, instrument 65" "mode 5 instr 65 budget 16 open 1 confirm 0|part 0 bend 0.000 range 1.0 voices 0|part 1 bend 0.000 range 2.0 voices 8" \
  "$(sps p9c 1 mode)|$(sps p9c 1 part0)|$(sps p9c 1 part1)"
expect "P9 the note goes to part 1, part 0 stays silent" "n=0 [ ]" "$(grep '^voices 0 ' "$WORK/p9c.poke.out" | sed -n 1p | sed 's/^voices 0 //')"
expect "P9 the note sounds on part 1" "yes" "$(grep '^voices 1 ' "$WORK/p9c.poke.out" | sed -n 1p | grep -q '1:60' && echo yes || echo no)"
run p9d "$ENTER; tap F1; enc -4; wait 100; $PS; $(for n in 60 62 64 65 67; do printf 'midiono 1 %s 100; ' $n; done) wait 40; $PV; $PG"
expect "P9 Voices column 8 -> 4" "part 0 bend 0.000 range 2.0 voices 4" "$(sps p9d 1 part0)"
expect "P9 5 notes at 4 voices: 4 voices on" "4" "$(voices p9d 1 | sed 's/n=\([0-9]*\).*/\1/')"
run p9e "$ENTER; tap F6; tap F6; enc 12; wait 50; midiono 1 60 100; wait 40; $PV; $PS"
expect "P9 Transpose +12 moves the note" "1:72" "$(gates p9e 1)"
run p9f "$ENTER; enc 5; wait 50; $PS"
expect "P9 Instrument column stops at Synth 3 (part 2 = instrument 66)" "mode 5 instr 66 budget 16 open 1 confirm 0" "$(sps p9f 1 mode)"
run p9g "$ENTER; tap F6; enc 1; wait 50; enc 1; wait 50; enc 1; wait 50; midiono 1 100 100; midiono 1 10 1; midiono 1 11 127; wait 40; $PS"
expect "P9 Velocity column cycles Soft, Linear, Hard, Fixed and stops there (no error, notes play)" "cells 100:1 10:1 11:1" "$(sps p9g 1 cells)"

# the Voices setting is what the user asked for, the engine's cap only limits what the part gets (part 1 rolled to PERC, one voice)
PERC1='instr 64; tap Params; wait 1500; enc 1; wait 1500'
run p9h "$PERC1; $ENTER; $PS; enc 1; wait 100; $PS; enc -1; wait 100; $PS"
expect "P9 a part with a lower cap gets its cap (PERC: 1 voice)" "part 0 bend 0.000 range 2.0 voices 1" "$(sps p9h 1 part0)"
expect "P9 the next part still gets the setting, it was not lowered by the PERC part (8 voices)" "part 1 bend 0.000 range 2.0 voices 8" "$(sps p9h 2 part1)"
expect "P9 and back on the PERC part: 1 voice again" "part 0 bend 0.000 range 2.0 voices 1" "$(sps p9h 3 part0)"
run p9i "$PERC1; $ENTER; enc 1; wait 100; tap F1; enc -4; wait 100; $PS; tap F1; enc -1; wait 100; $PS; enc 1; wait 100; $PS"
expect "P9 Voices turned down to 4 on the 8 voice part, the PERC part has its 1, back on the first part the 4 is still the setting" "part 1 bend 0.000 range 2.0 voices 4|part 0 bend 0.000 range 2.0 voices 1|part 1 bend 0.000 range 2.0 voices 4" "$(sps p9i 1 part1)|$(sps p9i 2 part0)|$(sps p9i 3 part1)"

# ===================================================================== P14 plugin voice cap (a plugin engine declares max_voices 4, "poke capsim")
# the page lifts the cap of an engine that declares 4 to the pool's 8 (pageVoiceCap), nowhere else: the pool budget is the switch
run p14 "poke capsim 0 4; $PS; $ENTER; poke capsim 0; $PS; $(for n in 60 62 64 65 67 69 71 72; do printf 'midiono 1 %s 100; ' $n; done) wait 40; $PV; $PG; midiono 1 74 100; wait 40; $PG; $FROMP; poke capsim 0; $PS"
expect "P14 outside the page the engine's own cap is the cap (declares 4, cap 4: the Voices row of the Synth screen stops at 4)" "capsim part 0 declared 4 cap 4" "$(grep '^capsim' "$WORK/p14.poke.out" | sed -n 1p)"
expect "P14 on the page the same engine has cap 8 (declares 4, cap 8) and the part gets 8 voices" "capsim part 0 declared 4 cap 8|part 0 bend 0.000 range 2.0 voices 8" "$(grep '^capsim' "$WORK/p14.poke.out" | sed -n 2p)|$(sps p14 2 part0)"
expect "P14 on the page 8 notes sound as 8 voices of that engine, the pool counts 8 of 8, no cut, the 9th note steals" "1:60 1:62 1:64 1:65 1:67 1:69 1:71 1:72|budget 16 cap 16 sounding 8 cuts 0|budget 16 cap 16 sounding 8 cuts 0" "$(gates p14 1)|$(gov p14 1)|$(gov p14 2)"
expect "P14 after the page the cap is 4 again and the part has the project's voices (1)" "capsim part 0 declared 4 cap 4|part 0 bend 0.000 range 1.0 voices 1" "$(grep '^capsim' "$WORK/p14.poke.out" | sed -n 3p)|$(sps p14 3 part0)"

# ===================================================================== P4V built-in voice cap (ACD declares 8 voice records, the A/B pair)
# outside the page every engine stops at 4 as in 1.3.0 (pageVoiceCap): the Voices row, its readout and the sum of the parts. On the page 8
rhsv() { line "$1" '^rhs' "$2" | sed 's/.* maxvoices\[\([0-9]*\)\].* voices\[\([0-9]*\)\].*labels\[[^|]*|\([^|]*\)|.*/engine max \1 stored \2 shown \3/'; }
ACD='tap Params; wait 1500; enc -1; wait 300; enc -1; wait 300; enc -1; wait 2500'   # WTFM -> FAT -> VAP -> ACD in the Model list
run pv4 "$ACD; poke rhs; tap Right; $(for k in 1 2 3 4 5 6 7 8; do printf 'enc 1; '; done)wait 200; poke rhs; poke capsim 0; poke voicecount 0 8; tap Left; tap Right; wait 100; poke rhs; poke capsim 1; enc -1; wait 100; poke rhs; $ENTER; poke capsim 0; $PS; $FROMP; poke capsim 0"
expect "P4V part 0 plays ACD" "rhs engine[ACD]" "$(line pv4 '^rhs' 1 | cut -d' ' -f1-2)"
expect "P4V the Voices row of ACD stops at 4 (8 steps up from 1)" "engine max 4 stored 4 shown 4" "$(rhsv pv4 2)"
expect "P4V outside the page ACD declares 8 and has cap 4" "capsim part 0 declared 8 cap 4" "$(line pv4 '^capsim' 1)"
expect "P4V a stored 8 (a 1.4.0 test build) shows 4 and counts as 4: part 1 may take 3 (8 minus the 4 of part 0 and the 1 of part 2)" "engine max 4 stored 8 shown 4|cap 3" "$(rhsv pv4 3)|$(line pv4 '^capsim' 2 | grep -o 'cap [0-9]*$')"
expect "P4V one step down from the shown 4 is 3" "engine max 4 stored 3 shown 3" "$(rhsv pv4 4)"
expect "P4V on the Synth play page the ACD part has cap 8 and 8 voices" "capsim part 0 declared 8 cap 8|part 0 bend 0.000 range 2.0 voices 8" "$(line pv4 '^capsim' 3)|$(sps pv4 1 part0)"
expect "P4V after the page the cap is 4 again" "capsim part 0 declared 8 cap 4" "$(line pv4 '^capsim' 4)"

# ===================================================================== P10 pads and panic
run p10 "$ENTER; padon 12 60; $PV; $PS; padoff 12 40; wait 40; $PV; padon 14 60; padon 16 60; midiono 1 60 100; wait 40; $PS; hold Shift; tap Delete; unhold Shift; wait 60; $PV; $PS"
expect "P10 a pad plays the page part" "1" "$(voices p10 1 | sed 's/n=\([0-9]*\).*/\1/')"
expect "P10 the pad goes into the router's table" "1" "$(sps p10 1 cells | sed 's/^cells //' | wc -w | tr -d ' ')"
expect "P10 pad released: gate 0" "yes" "$(voices p10 2 | grep -q ':0:' && echo yes || echo no)"
expect "P10 two pads and a USB note held: three notes in the table" "3" "$(sps p10 2 cells | sed 's/^cells //' | wc -w | tr -d ' ')"
expect "P10 Shift + Delete: the table is empty and no voice is gated" "cells|0" "$(sps p10 3 cells)|$(gates p10 3 | tr ' ' '\n' | grep -c '^1:')"

# ===================================================================== P11 pages that do not let go
run p11 "tap SampleRec; wait 500; hold Shift; tap Performance; unhold Shift; wait 300; $PS"
expect "P11 Shift + Perform on the Sample Recorder does not open the page (it is the plain Perform key there)" "budget 4 open 0 confirm 0" "$(sps p11 1 mode | sed 's/^mode [0-9]* instr [0-9]* //')"


# ===================================================================== AB: the A/B mode (Synth B, morph, banks, 16 voices, 4+4)
# helpers for "poke abstate" (7 lines per print: the page values, the gain of the 3 parts, the slots of the 3 parts)
abh() { grep '^abstate partb' "$WORK/$1.poke.out" | sed -n "$2p" | sed 's/^abstate //'; }
abg() { grep '^abstate part [0-9] gain' "$WORK/$1.poke.out" | sed -n "$(( ($2 - 1) * 3 + $3 + 1 ))p" | sed 's/^abstate part [0-9] //'; }
abs() { grep '^abstate slots' "$WORK/$1.poke.out" | sed -n "$(( ($2 - 1) * 3 + $3 + 1 ))p" | sed 's/^abstate slots [0-9] //'; }
gatesp() { grep "^voices $2 " "$WORK/$1.poke.out" | sed -n "$3p" | sed 's/n=[0-9]* \[ *//; s/ *\]//' | tr ' ' '\n' | awk -F: 'NF>=5 {printf "%s%s:%s", (n++ ? " " : ""), $2, $3} END {print ""}'; }
nvoices() { grep "^voices $2 " "$WORK/$1.poke.out" | sed -n "$3p" | sed 's/^voices [0-9] n=\([0-9]*\).*/\1/'; }
raw() { grep '^rawvals' "$WORK/$1.poke.out" | sed -n "$2p" | awk '{print $NF}'; }
# slots of part 0: 0 1 3, of part 1: 0 2 (the Synth editor's save, "poke slotfill PART SLOT PERCENT")
SETUPAB="tap Params; wait 1500; poke slotfill 0 0 50; poke slotfill 0 1 20; poke slotfill 0 3 80; poke slotfill 1 0 30; poke slotfill 1 2 70; tap Pattern; wait 1200"
PAB='poke abstate'
K8=""; for n in 60 62 64 65 67 69 71 72; do K8="$K8 midiono 1 $n 100;"; done
KD="${K8}"

# --- AB1 Synth B on: both parts have 8 voices, the pool is 16, the notes go to both
run ab1 "$ENTER; $PS; $PAB; tap F5; enc 1; wait 100; $PS; $PAB; $KD wait 60; $PV; $PG; $PAB; midiono 1 74 100; wait 40; $PV; $PG; tap F5; enc -5; wait 100; $PS; $PAB; $PV"
expect "AB1 the page opens with B off, morph 50, banks on the open slot, every part gain 1" "partb 0 morph 50 banka 0 bankb 0|gain 1.0000 vgain -1.0000|gain 1.0000 vgain -1.0000" "$(abh ab1 1)|$(abg ab1 1 0)|$(abg ab1 1 1)"
expect "AB1 Synth B + 1 from Off: Synth 1 is the A part, the encoder steps on to Synth 2 (value 2)" "partb 2 morph 50 banka 0 bankb 0" "$(abh ab1 2)"
expect "AB1 B on: parts 0 and 1 have 8 voices and 2 st range, part 2 none" "part 0 bend 0.000 range 2.0 voices 8|part 1 bend 0.000 range 2.0 voices 8|part 2 bend 0.000 range 1.0 voices 0" "$(sps ab1 2 part0)|$(sps ab1 2 part1)|$(sps ab1 2 part2)"
expect "AB1 at the middle both parts are at 0.7071" "gain 0.7071|gain 0.7071" "$(abg ab1 2 0 | awk '{print $1" "$2}')|$(abg ab1 2 1 | awk '{print $1" "$2}')"
expect "AB1 8 keys: all 8 voices of part A are on and gated" "1:60 1:62 1:64 1:65 1:67 1:69 1:71 1:72" "$(gatesp ab1 0 1)"
expect "AB1 8 keys: all 8 voices of part B, the same notes" "1:60 1:62 1:64 1:65 1:67 1:69 1:71 1:72" "$(gatesp ab1 1 1)"
expect "AB1 8 keys = 16 voices: the pool is 16 of 16, no cut" "budget 16 cap 16 sounding 16 cuts 0" "$(gov ab1 1)"
expect "AB1 both voices carry the morph gain (equal gain at the middle, both sound)" "same" "$(abg ab1 3 0 | awk '{a=$4} END {print a}' >/dev/null; [ "$(abg ab1 3 0 | awk '{print $4}')" = "$(abg ab1 3 1 | awk '{print $4}')" ] && [ "$(abg ab1 3 0 | awk '{print $4}')" != "-1.0000" ] && echo same || echo "$(abg ab1 3 0) / $(abg ab1 3 1)")"
expect "AB1 the 9th key steals the oldest voice on both sides, still 16 sounding" "1:74 1:62 1:64 1:65 1:67 1:69 1:71 1:72|1:74 1:62 1:64 1:65 1:67 1:69 1:71 1:72|budget 16 cap 16 sounding 16 cuts 0" "$(gatesp ab1 0 2)|$(gatesp ab1 1 2)|$(gov ab1 2)"
expect "AB1 Synth B down to Off: part 1 has no voices and range 1 again, B off, gain 1 on part A" "partb 0 morph 50 banka 0 bankb 0|part 1 bend 0.000 range 1.0 voices 0|gain 1.0000" "$(abh ab1 4)|$(sps ab1 3 part1)|$(abg ab1 4 0 | awk '{print $1" "$2}')"
expect "AB1 B turned off released every voice of both parts (no gate left)" "0|0" "$(gatesp ab1 0 3 | tr ' ' '\n' | grep -c '^1:')|$(gatesp ab1 1 3 | tr ' ' '\n' | grep -c '^1:')"

# --- AB2 never the same part on both sides, the encoders step over it
run ab2 "$ENTER; tap F5; enc 1; $PAB; enc -1; $PAB; enc 1; enc 1; $PAB; enc 1; $PAB; tap F1; enc 1; $PS; $PAB; enc 1; $PS; enc -1; enc -1; $PS; $PAB"
expect "AB2 B at Synth 2, one step down would be Synth 1 (= A): it steps over to Off" "partb 2|partb 0" "$(abh ab2 1 | cut -d' ' -f1-2)|$(abh ab2 2 | cut -d' ' -f1-2)"
expect "AB2 two steps up from Off: Synth 2 (Synth 1 skipped), then Synth 3, and it stays at Synth 3" "partb 3|partb 3" "$(abh ab2 3 | cut -d' ' -f1-2)|$(abh ab2 4 | cut -d' ' -f1-2)"
expect "AB2 Synth A + 1 with B on Synth 3: part 1 (instrument 65) plays" "mode 5 instr 65 budget 16 open 1 confirm 0" "$(sps ab2 1 mode)"
expect "AB2 A has part 1, B part 2: part 0 has no voices, parts 1 and 2 have 8, bend range 2 on both" "part 0 bend 0.000 range 1.0 voices 0|part 1 bend 0.000 range 2.0 voices 8|part 2 bend 0.000 range 2.0 voices 8" "$(sps ab2 1 part0)|$(sps ab2 1 part1)|$(sps ab2 1 part2)"
expect "AB2 A one step up would be Synth 3 (= B): it stays on Synth 2" "mode 5 instr 65 budget 16 open 1 confirm 0" "$(sps ab2 2 mode)"
expect "AB2 A back down to Synth 1: instrument 64, part 0 plays, part 0 range 2" "mode 5 instr 64 budget 16 open 1 confirm 0|part 0 bend 0.000 range 2.0 voices 8|part 1 bend 0.000 range 1.0 voices 0" "$(sps ab2 3 mode)|$(sps ab2 3 part0)|$(sps ab2 3 part1)"

# --- AB3 the morph: encoder steps of 5, Shift steps of 1, CC 11, equal power gains, B off has no morph
run ab3 "$ENTER; tap F5; enc 1; tap F3; enc 1; $PAB; hold Shift; enc 1; unhold Shift; $PAB; enc -2; $PAB; midicc 1 11 127; $PAB; midicc 1 11 0; $PAB; midicc 1 11 64; $PAB; tap F5; enc -5; midicc 1 11 127; $PAB; midicc 5 11 20; $PAB"
expect "AB3 morph + one click: 55 (steps of 5)" "morph 55|gain 0.6489 vgain|gain 0.7598 vgain" "$(abh ab3 1 | cut -d' ' -f3-4)|$(abg ab3 1 0 | cut -d' ' -f1-3)|$(abg ab3 1 1 | cut -d' ' -f1-3)"
expect "AB3 Shift + one click: 56 (single steps)" "morph 56|gain 0.6373|gain 0.7704" "$(abh ab3 2 | cut -d' ' -f3-4)|$(abg ab3 2 0 | cut -d' ' -f1-2)|$(abg ab3 2 1 | cut -d' ' -f1-2)"
expect "AB3 two clicks down: 46" "morph 46|gain 0.7493|gain 0.6606" "$(abh ab3 3 | cut -d' ' -f3-4)|$(abg ab3 3 0 | cut -d' ' -f1-2)|$(abg ab3 3 1 | cut -d' ' -f1-2)"
expect "AB3 CC 11 = 127: morph 100, A silent, B full" "morph 100|gain 0.0000|gain 1.0000" "$(abh ab3 4 | cut -d' ' -f3-4)|$(abg ab3 4 0 | cut -d' ' -f1-2)|$(abg ab3 4 1 | cut -d' ' -f1-2)"
expect "AB3 CC 11 = 0: morph 0, A full, B silent" "morph 0|gain 1.0000|gain 0.0000" "$(abh ab3 5 | cut -d' ' -f3-4)|$(abg ab3 5 0 | cut -d' ' -f1-2)|$(abg ab3 5 1 | cut -d' ' -f1-2)"
expect "AB3 CC 11 = 64: morph 50, both at 0.7071" "morph 50|gain 0.7071|gain 0.7071" "$(abh ab3 6 | cut -d' ' -f3-4)|$(abg ab3 6 0 | cut -d' ' -f1-2)|$(abg ab3 6 1 | cut -d' ' -f1-2)"
expect "AB3 B off: CC 11 moves the value, part A stays at gain 1, part 1 back at 1" "morph 100|gain 1.0000|gain 1.0000" "$(abh ab3 7 | cut -d' ' -f3-4)|$(abg ab3 7 0 | cut -d' ' -f1-2)|$(abg ab3 7 1 | cut -d' ' -f1-2)"
expect "AB3 CC 11 on another channel than the page's channel is the page's too (Omni): morph 20 x 100 / 127 = 15" "morph 15" "$(abh ab3 8 | cut -d' ' -f3-4)"

# --- AB4 the Bank columns: the slot's patch goes on the part at once with the notes held (no note off, no new voice), empty slots are skipped
run ab4 "$SETUPAB; $ENTER; poke rawvals 0; poke rawvals 1; tap F2; midiono 1 60 100; midiono 1 64 100; wait 40; $PV; enc 1; wait 40; $PAB; poke rawvals 0; $PV; enc 1; wait 40; $PAB; poke rawvals 0; enc 1; wait 40; $PAB; enc -1; wait 40; $PAB; poke rawvals 0; enc -5; wait 40; $PAB; poke rawvals 0; $PV; midionoff 1 60; wait 40; $PV"
expect "AB4 slots of part 0 are 0 1 3, of part 1 are 0 2 (the files the Synth editor saved)" "cur 0 bank 0 1 3|cur 0 bank 0 2" "$(abs ab4 1 0)|$(abs ab4 1 1)"
expect "AB4 Bank A + 1: slot 1 (slot 2 has no file) is applied, the values are slot 1's" "banka 1|different" "$(abh ab4 1 | awk '{print $5" "$6}')|$([ "$(raw ab4 1)" != "$(raw ab4 3)" ] && echo different || echo same)"
expect "AB4 Bank A + 1 again: slot 3 (slot 2 skipped), values differ from slot 1's" "banka 3|different" "$(abh ab4 2 | awk '{print $5" "$6}')|$([ "$(raw ab4 3)" != "$(raw ab4 4)" ] && echo different || echo same)"
expect "AB4 Bank A + 1 at the last slot stays on slot 3" "banka 3" "$(abh ab4 3 | awk '{print $5" "$6}')"
expect "AB4 Bank A - 1: slot 1 again, the same values as before (the bank is the same data)" "banka 1|same" "$(abh ab4 4 | awk '{print $5" "$6}')|$([ "$(raw ab4 3)" = "$(raw ab4 5)" ] && echo same || echo different)"
expect "AB4 Bank A - 5: slot 0, the edited values of the open slot are back (what the page opened with)" "banka 0|same" "$(abh ab4 5 | awk '{print $5" "$6}')|$([ "$(raw ab4 1)" = "$(raw ab4 6)" ] && echo same || echo different)"
expect "AB4 with 2 keys held the first swap changes nothing about the voices: same gates and ids (no note off, no new voice)" "$(grep '^voices 0 ' "$WORK/ab4.poke.out" | sed -n 1p)" "$(grep '^voices 0 ' "$WORK/ab4.poke.out" | sed -n 2p)"
expect "AB4 ... after the whole walk of part A too" "$(grep '^voices 0 ' "$WORK/ab4.poke.out" | sed -n 1p)" "$(grep '^voices 0 ' "$WORK/ab4.poke.out" | sed -n 3p)"
expect "AB4 the note off of key 60 releases its voice, 64 still sounds" "0:60 1:64" "$(gatesp ab4 0 4)"
# part B: turning B on releases what was held (both sides), then the keys are played with B on
run ab4c "$SETUPAB; $ENTER; tap F5; enc 1; wait 50; poke rawvals 1; tap F4; midiono 1 60 100; midiono 1 64 100; wait 40; $PV; enc 1; wait 40; $PAB; poke rawvals 1; $PV; enc -1; wait 40; $PAB; poke rawvals 1; $PV; tap F2; enc 1; wait 40; $PAB; $PV; midionoff 1 60; wait 40; $PV"
expect "AB4 Bank B + 1 with B on Synth 2: slot 2 of part 1 (slot 1 has no file), its values differ from the open slot's" "bankb 2|different" "$(abh ab4c 1 | awk '{print $7" "$8}')|$([ "$(raw ab4c 1)" != "$(raw ab4c 2)" ] && echo different || echo same)"
expect "AB4 Bank B - 1: slot 0 and the first values again" "bankb 0|same" "$(abh ab4c 2 | awk '{print $7" "$8}')|$([ "$(raw ab4c 1)" = "$(raw ab4c 3)" ] && echo same || echo different)"
expect "AB4 Bank A + 1 with B on: part A on slot 1 (part B stays on slot 0)" "banka 1 bankb 0" "$(abh ab4c 3 | awk '{print $5" "$6" "$7" "$8}')"
expect "AB4 the swaps of both parts with 2 keys held: the voices of both parts are the same before and after (no note off, no new voice)" "same|same|same|same|same|same" "$(for k in 2 3 4; do [ "$(grep '^voices 0 ' "$WORK/ab4c.poke.out" | sed -n 1p)" = "$(grep '^voices 0 ' "$WORK/ab4c.poke.out" | sed -n ${k}p)" ] && echo same || echo different; [ "$(grep '^voices 1 ' "$WORK/ab4c.poke.out" | sed -n 1p)" = "$(grep '^voices 1 ' "$WORK/ab4c.poke.out" | sed -n ${k}p)" ] && echo same || echo different; done | tr '\n' '|' | sed 's/|$//')"
expect "AB4 both parts have the two keys gated" "1:60 1:64|1:60 1:64" "$(gatesp ab4c 0 1)|$(gatesp ab4c 1 1)"
expect "AB4 the note off of key 60 releases it on both parts" "0:60 1:64|0:60 1:64" "$(gatesp ab4c 0 5)|$(gatesp ab4c 1 5)"
run ab4b "$SETUPAB; $ENTER; tap F4; enc 1; $PAB; tap F5; enc 1; tap F4; enc 1; $PAB; tap F2; enc 1; $PAB"
expect "AB4 Bank B with B off does nothing (value 0), Bank A is not affected by it" "bankb 0|bankb 2|banka 1" "$(abh ab4b 1 | awk '{print $7" "$8}')|$(abh ab4b 2 | awk '{print $7" "$8}')|$(abh ab4b 3 | awk '{print $5" "$6}')"

# --- AB5 exit: everything back as the editor has it, with 16 voices sounding, the pedal down, morph 30 and bank A on slot 3
run ab5 "$SETUPAB; poke rawvals 0; poke rawvals 1; $PS; $ENTER; tap F5; enc 1; tap F2; enc 5; tap F3; enc -4; midicc 1 64 127; $K8 midionoff 1 60; wait 60; $PV; $PAB; $FROMP; wait 60; $PV; $PS; $PAB; poke rawvals 0; poke rawvals 1; $PG; $ENTER; $PAB; $PS"
expect "AB5 before the exit: B on Synth 2, morph 30, bank A on slot 3, both parts have notes" "partb 2 morph 30 banka 3 bankb 0|gain 0.8903|gain 0.4536|8" "$(abh ab5 1)|$(abg ab5 1 0 | cut -d' ' -f1-2)|$(abg ab5 1 1 | cut -d' ' -f1-2)|$(nvoices ab5 1 1)"
expect "AB5 after the exit: every voice of both parts released (gate 0)" "0|0" "$(gatesp ab5 0 2 | tr ' ' '\n' | grep -c '^1:')|$(gatesp ab5 1 2 | tr ' ' '\n' | grep -c '^1:')"
expect "AB5 after the exit: both gains back at 1, the part's slot is still the one the editor has open (0)" "gain 1.0000|gain 1.0000|cur 0|cur 0" "$(abg ab5 2 0 | cut -d' ' -f1-2)|$(abg ab5 2 1 | cut -d' ' -f1-2)|$(abs ab5 2 0 | cut -d' ' -f1-2)|$(abs ab5 2 1 | cut -d' ' -f1-2)"
expect "AB5 after the exit: the values of part 0 are the editor's again (slot 0 edited values), part 1 untouched" "same|same" "$([ "$(raw ab5 1)" = "$(raw ab5 3)" ] && echo same || echo different)|$([ "$(raw ab5 2)" = "$(raw ab5 4)" ] && echo same || echo different)"
expect "AB5 after the exit: the project's voice counts (1 1 1), budget 4, ranges 1, mode 0" "mode 0 instr 64 budget 4 open 0 confirm 0|part 0 bend 0.000 range 1.0 voices 1|part 1 bend 0.000 range 1.0 voices 1|part 2 bend 0.000 range 1.0 voices 1" "$(sps ab5 2 mode)|$(sps ab5 2 part0)|$(sps ab5 2 part1)|$(sps ab5 2 part2)"
expect "AB5 the pool is back to 4 and nothing was cut by the exit" "budget 4 cap 4" "$(gov ab5 1 | awk '{print $1" "$2" "$3" "$4}')"
expect "AB5 the next entry on the same instrument has the page as it was left: B on Synth 2, morph 30, bank A on slot 3 (ref/synth-play-state-sends.md)" "partb 2 morph 30 banka 3 bankb 0" "$(abh ab5 3)"

# --- AB6 the pooled pair (the same engine with one voice pool of 8 on both sides: stored 18 PLK): 4+4. ALCO (22) has 16 blocks since 1.1.0 and CHIP (6) 16 voice objects since 1.1.0: 8+8
# "poke ptype PART N" sets the stored number the page reads, the engine of the part stays (the voice cap comes from the page)
pair() {   # NAME TYPE_A TYPE_B KEYS: A and B (Synth 2) on, the pooled test, 8 keys held
    run "$1" "$ENTER; poke ptype 0 $2; poke ptype 1 $3; tap F5; enc 1; wait 100; $PS; $K8 wait 60; $PV; $PG"
}
for e in "18 PLK"; do
  t="${e% *}"; n="${e#* }"
  pair "ab6$n" "$t" "$t"
  expect "AB6 $n on A and B: 4 voices each (4+4), the sum is 8" "voices 4|voices 4" "$(sps ab6$n 1 part0 | awk '{print $7" "$8}')|$(sps ab6$n 1 part1 | awk '{print $7" "$8}')"
  expect "AB6 $n 8 keys: 4 voices on each side, 8 sounding in all, no cut" "4|4|budget 16 cap 16 sounding 8 cuts 0" "$(nvoices ab6$n 0 1)|$(nvoices ab6$n 1 1)|$(gov ab6$n 1)"
done
# ALCO on both sides as the plugin it is (declares max_voices 4, "poke capsim", lifted to 8 on the page): 8+8, 16 sounding, no cut
run ab6alco "poke capsim 0 4; poke capsim 1 4; $ENTER; poke ptype 0 22; poke ptype 1 22; tap F5; enc 1; wait 100; $PS; $K8 wait 60; $PV; $PG; $FROMP; poke capsim 0; poke capsim 1"
expect "AB6 ALCO on A and B (22, 16 blocks since ALCO 1.1.0, not pooled): 8 voices each, the sum is 16" "voices 8|voices 8" "$(sps ab6alco 1 part0 | awk '{print $7" "$8}')|$(sps ab6alco 1 part1 | awk '{print $7" "$8}')"
expect "AB6 ALCO 8 keys: 8 voices on each side, 16 sounding in all, no cut" "8|8|budget 16 cap 16 sounding 16 cuts 0" "$(nvoices ab6alco 0 1)|$(nvoices ab6alco 1 1)|$(gov ab6alco 1)"
# CHIP (6) the same way as ALCO since CHIP 1.1.0: 12 objects in bss and 4 in the work arena, the plugin declares 4, the page lifts it to 8
run ab6chip "poke capsim 0 4; poke capsim 1 4; $ENTER; poke ptype 0 6; poke ptype 1 6; tap F5; enc 1; wait 100; $PS; $K8 wait 60; $PV; $PG; $FROMP; poke capsim 0; poke capsim 1"
expect "AB6 CHIP on A and B (6, 16 voice objects since CHIP 1.1.0, not pooled): 8 voices each, the sum is 16" "voices 8|voices 8" "$(sps ab6chip 1 part0 | awk '{print $7" "$8}')|$(sps ab6chip 1 part1 | awk '{print $7" "$8}')"
expect "AB6 CHIP 8 keys: 8 voices on each side, 16 sounding in all, no cut" "8|8|budget 16 cap 16 sounding 16 cuts 0" "$(nvoices ab6chip 0 1)|$(nvoices ab6chip 1 1)|$(gov ab6chip 1)"
pair ab6j 7 7
expect "AB6 JU6 on both sides (7 = not a pooled engine): 8 and 8, 16 sounding" "voices 8|voices 8|16" "$(sps ab6j 1 part0 | awk '{print $7" "$8}')|$(sps ab6j 1 part1 | awk '{print $7" "$8}')|$(gov ab6j 1 | awk '{print $6}')"
pair ab6m 18 22
expect "AB6 PLK on A and ALCO on B (two engines, two pools): 8 and 8" "voices 8|voices 8" "$(sps ab6m 1 part0 | awk '{print $7" "$8}')|$(sps ab6m 1 part1 | awk '{print $7" "$8}')"
pair ab6n 17 17
expect "AB6 Arc-Type on both (17, no pool): 8 and 8" "voices 8|voices 8" "$(sps ab6n 1 part0 | awk '{print $7" "$8}')|$(sps ab6n 1 part1 | awk '{print $7" "$8}')"
run ab6c "$ENTER; poke ptype 0 18; poke ptype 1 18; tap F5; enc 1; wait 100; $PS; tap F1; tap F1; enc -2; wait 50; $PS; tap F1; tap F5; enc 1; wait 100; $PS; tap F1; tap F1; enc 6; $PS"
expect "AB6 4+4 with Voices 2: min(Voices, 4) = 2 on both sides" "voices 4|voices 4|voices 2|voices 2" "$(sps ab6c 1 part0 | awk '{print $7" "$8}')|$(sps ab6c 1 part1 | awk '{print $7" "$8}')|$(sps ab6c 2 part0 | awk '{print $7" "$8}')|$(sps ab6c 2 part1 | awk '{print $7" "$8}')"
expect "AB6 B moved to another part (Synth 3, stored number 2 is not pooled): the cap lifts at once, Voices 2 stays what the user asked for" "voices 2|voices 0|voices 2" "$(sps ab6c 3 part0 | awk '{print $7" "$8}')|$(sps ab6c 3 part1 | awk '{print $7" "$8}')|$(sps ab6c 3 part2 | awk '{print $7" "$8}')"
expect "AB6 Voices up with no pooled pair: 8 on both sides" "voices 8|voices 8" "$(sps ab6c 4 part0 | awk '{print $7" "$8}')|$(sps ab6c 4 part2 | awk '{print $7" "$8}')"

# AB6 the cap table (kPairCap in fSynthPlay.cpp) is what the page reads: "poke paircap STORED N" overrides one row for a run (one override at a time), the
# shipped table is not edited. CHIP 8+8 -> 6+6 and PLK 4+4 -> 2+2 are each a one number change. The shipped rows are read back first (CHIP 8, PLK 4, JU6 8, unlisted 8)
pc() { grep '^paircap' "$WORK/$1.poke.out" | sed -n "$2p" | awk '{print $3"="$5}'; }
CAPSIM="poke capsim 0 4; poke capsim 1 4"; CAPOFF="poke capsim 0; poke capsim 1"
run ab6cap "poke paircap 6; poke paircap 18; poke paircap 7; poke paircap 31; poke paircap 6 6; poke paircap 6; poke paircap 7; $CAPSIM; $ENTER; poke ptype 0 6; poke ptype 1 6; tap F5; enc 1; wait 100; $PS; $K8 wait 60; $PV; $PG; $FROMP; $CAPOFF"
expect "AB6 the shipped cap table: CHIP 8, PLK 4, JU6 8, an engine that is not listed 8" "6=8|18=4|7=8|31=8" "$(pc ab6cap 1)|$(pc ab6cap 2)|$(pc ab6cap 3)|$(pc ab6cap 4)"
expect "AB6 an override of the CHIP row to 6 is read by the table, JU6 stays 8" "6=6|7=8" "$(pc ab6cap 5)|$(pc ab6cap 7)"
expect "AB6 CHIP row 6: 6 voices on each side (6+6), 8 keys held, 12 sounding in all, no cut" "voices 6|voices 6|6|6|budget 16 cap 16 sounding 12 cuts 0" "$(sps ab6cap 1 part0 | awk '{print $7" "$8}')|$(sps ab6cap 1 part1 | awk '{print $7" "$8}')|$(nvoices ab6cap 0 1)|$(nvoices ab6cap 1 1)|$(gov ab6cap 1)"
run ab6cap2 "poke paircap 18 2; poke paircap 18; poke paircap 7; $ENTER; poke ptype 0 18; poke ptype 1 18; tap F5; enc 1; wait 100; $PS; $K8 wait 60; $PV; $PG; $FROMP"
expect "AB6 an override of the PLK row to 2 is read by the table, JU6 stays 8" "18=2|7=8" "$(pc ab6cap2 2)|$(pc ab6cap2 3)"
expect "AB6 PLK row 2: 2 voices on each side (2+2), 4 sounding in all, no cut" "voices 2|voices 2|2|2|budget 16 cap 16 sounding 4 cuts 0" "$(sps ab6cap2 1 part0 | awk '{print $7" "$8}')|$(sps ab6cap2 1 part1 | awk '{print $7" "$8}')|$(nvoices ab6cap2 0 1)|$(nvoices ab6cap2 1 1)|$(gov ab6cap2 1)"
run ab6cap3 "poke paircap 6 6; poke paircap 6 -1; poke paircap 6; poke paircap 18; $CAPSIM; $ENTER; poke ptype 0 6; poke ptype 1 6; tap F5; enc 1; wait 100; $PS; $FROMP; $CAPOFF"
expect "AB6 an override cleared with N -1: the shipped rows are back (CHIP 8, PLK 4) and CHIP plays 8+8 again" "6=8|18=4|voices 8|voices 8" "$(pc ab6cap3 3)|$(pc ab6cap3 4)|$(sps ab6cap3 1 part0 | awk '{print $7" "$8}')|$(sps ab6cap3 1 part1 | awk '{print $7" "$8}')"

# --- AB7 exit with the 4+4 pair: the project counts are back, the 8 + 8 sum assert of the firmware does not fire (the build asserts)
run ab7 "$ENTER; tap F5; enc 1; wait 50; $K8 wait 60; $FROMP; wait 60; $PS"
expect "AB7 exit from 16 voices: the project's voice counts (1 1 1), budget 4" "mode 0 instr 64 budget 4 open 0 confirm 0|part 0 bend 0.000 range 1.0 voices 1|part 1 bend 0.000 range 1.0 voices 1|part 2 bend 0.000 range 1.0 voices 1" "$(sps ab7 1 mode)|$(sps ab7 1 part0)|$(sps ab7 1 part1)|$(sps ab7 1 part2)"

# --- AB8 the second items of the columns: press the selected column's button again
run ab8 "$ENTER; tap F1; enc -3; $PS; tap F1; enc 1; $PS; tap F6; tap F6; enc 3; tap F7; tap F7; enc 2; tap F8; tap F8; midiono 1 60 100; wait 40; $PS; $PV"
expect "AB8 Synth A pressed again is Voices (3 less: 5), pressed a third time Synth A (Synth 2, voices stay 5)" "part 0 bend 0.000 range 2.0 voices 5|mode 5 instr 65 budget 16 open 1 confirm 0|part 1 bend 0.000 range 2.0 voices 5" "$(sps ab8 1 part0)|$(sps ab8 2 mode)|$(sps ab8 2 part1)"

# --- AB9 the morph by audio: two different engines (Synth 1 WTFM, Synth 2 ACD), a new note at every morph value, the level against the equal power law
{
  S="$ENTER; tap F5; enc 1; wait 100; audiomark"
  MARKS=""
  for m in 0 10 20 30 40 50 60 70 80 90 100; do
    cc=$(( (m * 127 + 50) / 100 ))
    S="$S; midicc 1 11 $cc; wait 100; midiono 1 60 100; midiono 1 67 100; wait 100; audiomark; wait 250; audiomark; midionoff 1 60; midionoff 1 67; wait 700"
  done
  rm -rf "$WORK/ab9"; mkdir -p "$WORK/ab9"
  UISHOT_AUDIO_DUMP="$WORK/ab9.raw" UISHOT_POKE="$WORK/ab9.poke" ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/ab9" "wait 500; instr 64; audio 1; $S; poke abstate" >"$WORK/ab9.out" 2>"$WORK/ab9.err"
  frames=$(grep '^audio frames' "$WORK/ab9.out" | awk '{print $3}')
  args=""; i=0; set -- $frames
  # frames come in pairs per morph value after the first mark (the first one is the start)
  shift
  for m in 0 10 20 30 40 50 60 70 80 90 100; do args="$args $m $1 $2"; shift 2; done
  ab="$(python3 -I "$HERE/ab_audio_check.py" "$WORK/ab9.raw" $args)"
}
echo "      $ab"
expect "AB9 both ends sound (A alone at morph 0, B alone at 100)" "yes" "$(echo "$ab" | awk '{split($3,a,"="); split($4,b,"="); print (a[2] > 100 && b[2] > 100) ? "yes" : "no"}')"
expect "AB9 the level of the sweep follows the equal power law (one constant correlation of the two parts fits every step within 1 dB) and never dips" "yes" "$(echo "$ab" | awk '{split($6,w,"="); split($7,g,"="); print (w[2] <= 1.0 && g[2] == 1) ? "yes" : "no"}')"

# ===================================================================== P15 state round trip (ref/synth-play-state-sends.md)
# Everything the user sets on the page is there again when the page is entered again: Synth A, Voices, Synth B, Morph, both Banks, Transpose, the
# selected column and the item every column shows. Part 1 has the slots 0 2, part 0 the slots 0 1 3 (SETUPAB)
spl() { grep '^splstate part' "$WORK/$1.poke.out" | sed -n "$2p" | sed 's/^splstate //'; }
splsend() { grep '^splstate send' "$WORK/$1.poke.out" | sed -n "$(( ($2 - 1) * 3 + $3 + 1 ))p" | sed 's/^splstate send [0-9] //'; }
SET15="tap F1; enc -3; tap F1; enc 1; tap F5; enc 1; tap F3; enc 3; tap F2; enc 1; tap F4; enc 1; tap F6; tap F6; enc 2; tap F4"
run p15 "$SETUPAB; $ENTER; poke splstate; $SET15; poke splstate; $PAB; $FROMP; wait 300; poke splstate; $ENTER; poke splstate; $PAB; midiono 1 60 100; wait 40; poke voices 1"
expect "P15 fresh page: Synth 1, B off, Voices 8, no transpose, column 1, first items" "part 0 partb 0 transpose 0 voices 8 sel 0 sub 00000000" "$(spl p15 1)"
expect "P15 everything set on the page: Synth 2, B Synth 1, Voices 5, transpose +2, column 4 selected, Transpose shown" "part 1 partb 1 transpose 2 voices 5 sel 3 sub 00000100" "$(spl p15 2)"
expect "P15 set: morph 65, bank A slot 2 (of part 1), bank B slot 1 (of part 0)" "partb 1 morph 65 banka 2 bankb 1" "$(abh p15 1)"
expect "P15 left for the Pattern page: the page values are still the ones the user set" "part 1 partb 1 transpose 2 voices 5 sel 3 sub 00000100" "$(spl p15 3)"
expect "P15 entered again: Synth 2, B Synth 1, Voices 5, transpose +2, column 4 selected, Transpose shown" "part 1 partb 1 transpose 2 voices 5 sel 3 sub 00000100" "$(spl p15 4)"
expect "P15 entered again: morph 65, bank A slot 2, bank B slot 1" "partb 1 morph 65 banka 2 bankb 1" "$(abh p15 2)"
expect "P15 entered again: a key plays the transposed note on the page part (60 + 2)" "62" "$(grep '^voices 1 ' "$WORK/p15.poke.out" | tail -1 | grep -o '[0-9]*:1:[0-9]*:' | head -1 | cut -d: -f3)"

# # P16 the send columns: Bank A / Delay A / Reverb A and Bank B / Delay B / Reverb B (the same synthPart sends as the Instrument params page)
run p16 "$ENTER; poke splstate; tap F2; tap F2; enc 10; poke splstate; tap F2; enc 20; poke splstate; tap F5; enc 2; tap F4; tap F4; enc 5; tap F4; enc 7; poke splstate; enc -3; poke splstate; tap F2; enc 500; poke splstate; enc -500; poke splstate"
expect "P16 starts with the project's sends (0 0) and the first item on every column" "delay 0 reverb 0|delay 0 reverb 0|00000000" \
  "$(splsend p16 1 0)|$(splsend p16 1 1)|$(spl p16 1 | awk '{print $NF}')"
expect "P16 Bank A pressed again is Delay A (10), pressed again Reverb A (20), the part's sends in the project" "delay 10 reverb 0|delay 10 reverb 20|01000000|02000000" \
  "$(splsend p16 2 0)|$(splsend p16 3 0)|$(spl p16 2 | awk '{print $NF}')|$(spl p16 3 | awk '{print $NF}')"
expect "P16 B on Synth 2: Delay B 5, Reverb B 7 on part 1, part 0 unchanged" "delay 5 reverb 7|delay 10 reverb 20" "$(splsend p16 4 1)|$(splsend p16 4 0)"
expect "P16 the encoder steps the selected send by 1 per click, clamps at 0 and 100" "delay 5 reverb 4|delay 10 reverb 100|delay 10 reverb 0" "$(splsend p16 5 1)|$(splsend p16 6 0)|$(splsend p16 7 0)"

# P15b a page entered with another instrument opens as the single part page on that part, the banks are the project's, the settings stay
run p15b "$SETUPAB; $ENTER; $SET15; $FROMP; wait 300; instr 66; $ENTER; poke splstate; $PAB"
expect "P15b another entry instrument: Synth 3, B off, first items, column 1, the settings (Voices 5, transpose 2) stay" "part 2 partb 0 transpose 2 voices 5 sel 0 sub 00000000" "$(spl p15b 1)"
expect "P15b morph 65 is the setting, the banks are the project's" "partb 0 morph 65 banka 0 bankb 0" "$(abh p15b 1)"

# P17 the sends reach the FX bus with the sequencer stopped: the tail after the note off (the reverb and the delay ring on) against the same take with the sends at 0
tail() {   # NAME SENDSCRIPT: the RMS of the 1.2 s after the note off
  rm -rf "$WORK/$1"; mkdir -p "$WORK/$1"
  UISHOT_AUDIO_DUMP="$WORK/$1.raw" ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$1" "wait 500; instr 64; audio 1; $ENTER; $2; midiono 1 60 100; wait 500; midionoff 1 60; wait 150; audiomark; wait 1200; audiomark" >"$WORK/$1.out" 2>"$WORK/$1.err"
  python3 -I -c '
import array, sys
raw = array.array("h"); raw.frombytes(open(sys.argv[1], "rb").read())
fr = [int(l.split()[2]) for l in open(sys.argv[2]) if l.startswith("audio frames")]
seg = raw[2 * fr[0]:2 * fr[1]]
print(int((sum(x * x for x in seg) / max(len(seg), 1)) ** 0.5))' "$WORK/$1.raw" "$WORK/$1.out"
}
t0=$(tail p17none "wait 10"); tr=$(tail p17rev "tap F2; tap F2; tap F2; enc 100"); td=$(tail p17del "tap F2; tap F2; enc 100")
echo "      tail rms after the note off: sends 0 = $t0, Reverb A 100 = $tr, Delay A 100 = $td"
expect "P17 Reverb A 100 makes the tail at least 4 times louder than with the sends at 0 (the sequencer is stopped)" "yes" "$([ "$tr" -ge $((t0 * 4)) ] && echo yes || echo no)"
expect "P17 Delay A 100 makes the tail at least 4 times louder than with the sends at 0" "yes" "$([ "$td" -ge $((t0 * 4)) ] && echo yes || echo no)"

# ===================================================================== P12 settings file (EXTHOOKS=1 only)
if [ -n "$EXTHOOKS" ]; then
  SEED='synthplay 1\ninput 1\nfixed 90\n' run p12 "$ENTER; tap F7; enc 3; tap F6; enc 2; wait 100; $FROMP; wait 100; cat Features/synthplay.cfg; $ENTER; $PS"
  expect "P12 the settings are written when the page closes (Input and Fixed vel as the file had them)" "synthplay 1|input 1|channel 0|voices 8|curve 3|fixed 90|bend 5|mod 1|sustain 1" "$(awk '/^synthplay 1/ {p=1} p {print} /^sustain/ {exit}' "$WORK/p12.out" | tr '\n' '|' | sed 's/|$//')"
  expect "P12 and read again at the next entry (bend range 5 on the part)" "part 0 bend 0.000 range 5.0 voices 8" "$(sps p12 1 part0)"
  run p13 "$PERC1; $ENTER; tap F7; enc 1; wait 100; $FROMP; wait 100; cat Features/synthplay.cfg"
  expect "P13 the file keeps the Voices setting (8) after the page was left on the PERC part (cap 1)" "voices 8" "$(grep '^voices' "$WORK/p13.out")"
fi

if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
