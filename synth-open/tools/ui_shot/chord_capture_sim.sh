#!/bin/bash
# chord_capture_sim.sh [PNG_DIR]
#
# The chord capture of the pattern editor (core/source/mtSequencer_chord.cpp) on the real UI code of this tree (ui_shot) on the Mac.
# Host only: no device, every scenario has its own SD card folder (an empty card: the first start writes the Workspace). Pads are held
# with "padon" and released with "padoff", MIDI notes come through mtMidi.cpp's USB input ("midion", "midioff"), the step is read back
# with "step" and the cursor with "cursor" (ui_shot_main.cpp). Pad 0 is C6 (note 72) on a fresh project, MIDI note 64 lands as 40.
# PNG_DIR: also writes the screens with the popups. SAN=address,undefined: the sanitizer build (build/ui-shot-san), a report fails the run
# except the stock mtWavetableEnvelopes.cpp:206 (also allowlisted by the sim gate). The pattern editor read of mtProject.instrument[64]
# with a synth or MIDI instrument (lightUpPadBoard and functPads, fPatternEdit.cpp) is fixed and no longer allowlisted.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="$BUILD/ui_shot"
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/chord_capture_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() {   # run NAME "script": the script's "step" and "cursor" lines, one per line
    local sd="$WORK/$1"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; $2" 2>"$WORK/$1.err" | grep -E '^(step|cursor|title|grid|midi) '
}
shot() { [ -n "$PNG_DIR" ] && echo "shot $PNG_DIR/$1.png" || echo "wait 1"; }

# 1. pads on a MIDI Chord cell of a synth step: C E G B held one after the other, the step follows the chord, the cursor stays until
#    the last pad comes up (the FX column under the cursor is fx[0], the Fx button)
out=$(run pads "instr 64; setstep 0 0 -1 0 14 0 0 0; tap Rec; tap Fx; padon 0; step 0 0; padon 4; step 0 0; padon 7; step 0 0; padon 11; step 0 0; cursor; $(shot capture-pads-maj7); padoff 0; padoff 4; padoff 7; cursor; padoff 11; cursor; step 0 0")
expect "pads: one pad is the note alone (the chord FX goes)" "step 0 0 note 72 instr 64 fx0 0:0 fx1 0:0" "$(sed -n 1p <<<"$out")"
expect "pads: C E is ~maj (missing fifth, cost 1)" "step 0 0 note 72 instr 64 fx0 14:5 fx1 0:0" "$(sed -n 2p <<<"$out")"
expect "pads: C E G is maj (5)" "step 0 0 note 72 instr 64 fx0 14:5 fx1 0:0" "$(sed -n 3p <<<"$out")"
expect "pads: C E G B is maj7 (24)" "step 0 0 note 72 instr 64 fx0 14:24 fx1 0:0" "$(sed -n 4p <<<"$out")"
expect "pads: the cursor stays while pads are held" "cursor track 0 step 0 param 3 edit 1" "$(sed -n 5p <<<"$out")"
expect "pads: three of four released, still there" "cursor track 0 step 0 param 3 edit 1" "$(sed -n 6p <<<"$out")"
expect "pads: the last release moves the cursor one step" "cursor track 0 step 1 param 3 edit 1" "$(sed -n 7p <<<"$out")"
expect "pads: the chord stays after the release" "step 0 0 note 72 instr 64 fx0 14:24 fx1 0:0" "$(sed -n 8p <<<"$out")"

# 2. approximate (C E B: maj7 without the fifth) and rejected (C C# D: the step goes back to how it was before the chord)
out=$(run apx "instr 64; setstep 0 0 -1 0 14 0 0 0; setstep 0 1 -1 0 14 0 0 0; tap Rec; tap Fx; padon 0; padon 4; padon 11; $(shot capture-pads-approx); padoff 0; padoff 4; padoff 11; step 0 0; padon 0; padon 1; padon 2; $(shot capture-pads-reject); padoff 0; padoff 1; padoff 2; step 0 1; cursor")
expect "approximate: C E B is maj7 (cost 1, ~)" "step 0 0 note 72 instr 64 fx0 14:24 fx1 0:0" "$(sed -n 1p <<<"$out")"
expect "no chord: the step is as before the chord" "step 0 1 note -1 instr 0 fx0 14:0 fx1 0:0" "$(sed -n 2p <<<"$out")"
expect "no chord: the cursor moved on after the release" "cursor track 0 step 2 param 3 edit 1" "$(sed -n 3p <<<"$out")"

# 3. MIDI keyboard: E G C (first inversion) in the note column joins into one step, the cursor moves on after the last note-off
out=$(run midi "instr 64; tap Rec; midion 64; midion 67; midion 72; $(shot capture-midi-inv1); step 0 0; cursor; midioff 64; midioff 67; midioff 72; cursor; step 0 1")
expect "MIDI: E G C is maj inv1 (30) on E" "step 0 0 note 40 instr 64 fx0 14:30 fx1 0:0" "$(sed -n 1p <<<"$out")"
expect "MIDI: the cursor stays while notes are held" "cursor track 0 step 0 param 0 edit 1" "$(sed -n 2p <<<"$out")"
expect "MIDI: the cursor moves on after the last note-off" "cursor track 0 step 1 param 0 edit 1" "$(sed -n 3p <<<"$out")"
expect "MIDI: the next step is untouched" "step 0 1 note -1 instr 0 fx0 0:0 fx1 0:0" "$(sed -n 4p <<<"$out")"

# 4. both FX slots hold other effects: the chord is refused, the first note stays as stock writes it
out=$(run busy "instr 64; setstep 0 0 -1 0 4 50 3 2; tap Rec; midion 60; midion 64; midion 67; $(shot capture-midi-busy); step 0 0; midioff 60; midioff 64; midioff 67")
expect "busy: Chance and Roll stay, the step note is the first note" "step 0 0 note 36 instr 64 fx0 4:50 fx1 3:2" "$(sed -n 1p <<<"$out")"

# 5. a sample instrument step keeps the stock pads (the pad sets the FX value and the cursor moves on every press)
out=$(run sample "instr 0; setstep 0 0 -1 0 14 0 0 0; tap Rec; tap Fx; padon 47; padoff 47; step 0 0; cursor")
expect "sample step: pad 47 is the top FX value (stock)" "step 0 0 note -1 instr 0 fx0 14:67 fx1 0:0" "$(sed -n 1p <<<"$out")"
expect "sample step: the cursor moved at the press (stock)" "cursor track 0 step 1 param 3 edit 1" "$(sed -n 2p <<<"$out")"

# 6. a MIDI instrument step captures like a synth step
out=$(run midiinstr "instr 48; setstep 0 0 -1 0 14 0 0 0; tap Rec; tap Fx; padon 0; padon 3; padon 7; padoff 0; padoff 3; padoff 7; step 0 0")
expect "MIDI instrument: C Eb G is m (4)" "step 0 0 note 72 instr 48 fx0 14:4 fx1 0:0" "$(sed -n 1p <<<"$out")"

# 7. green mode pads on a synth instrument (functPads, not a capture): the pads play, nothing is written, and the SAN build has no
#    report (the beat slice check read mtProject.instrument[64], past the 48 sample instruments, before the fix)
out=$(run greenpads "instr 64; padon 0; wait 100; padoff 0; padon 4; padoff 4; cursor; step 0 0")
expect "green pads: the cursor stays, no edit mode" "cursor track 0 step 0 param 0 edit 0" "$(sed -n 1p <<<"$out")"
expect "green pads: the step is untouched" "step 0 0 note -1 instr 0 fx0 0:0 fx1 0:0" "$(sed -n 2p <<<"$out")"

# 8. Undo of a captured chord: the step goes back and the title bar names the FX of the step it brought back (functUndoRedo calls
#    showFxInfo, before it the title kept the captured chord until the cursor moved)
out=$(run undo "instr 64; setstep 0 0 -1 0 14 0 0 0; tap Rec; tap Fx; padon 0; padon 4; padon 7; padon 11; padoff 0; padoff 4; padoff 7; padoff 11; wait 300; tap Up; wait 300; title; tap F6; wait 600; title; step 0 0")
expect "undo: the title names the captured chord" "title MIDI Chord: Cmaj7" "$(sed -n 1p <<<"$out")"
expect "undo: the title follows the undo" "title MIDI Chord: note" "$(sed -n 2p <<<"$out")"
expect "undo: the step is back to the empty chord cell" "step 0 0 note -1 instr 0 fx0 14:0 fx1 0:0" "$(sed -n 3p <<<"$out")"

# 9. Chord names (Config > Features, on by default): the grid cell shows the short name, off the stock interval digits. The Strum FX
#    cell shows +n / -n / off
out=$(run names "instr 48; setstep 0 0 48 48 14 24 47 10; setstep 0 1 48 48 14 1 47 74; setstep 0 2 48 48 14 57 47 64; setstep 0 3 48 48 14 34 0 0; grid 0 0; grid 0 1; grid 0 2; grid 0 3; $(shot names-on); chordnames 0; grid 0 0; grid 0 1; grid 0 3; $(shot names-off); chordnames 1")
expect "names on: maj7 is ^7, strum +10" "grid 0 0 fx0 [0^7 ] fx1 [u+10]" "$(sed -n 1p <<<"$out")"
expect "names on: sus2 is Su2, strum 74 is -10" "grid 0 1 fx0 [0Su2] fx1 [u-10]" "$(sed -n 2p <<<"$out")"
expect "names on: 7sus4 inv1 keeps the digits, strum 64 is off" "grid 0 2 fx0 [0257] fx1 [uoff]" "$(sed -n 3p <<<"$out")"
expect "names on: maj7 inv2 is ^72" "grid 0 3 fx0 [0^72] fx1 [----]" "$(sed -n 4p <<<"$out")"
expect "names off: maj7 is 47B" "grid 0 0 fx0 [047B] fx1 [u+10]" "$(sed -n 5p <<<"$out")"
expect "names off: sus2 is 27" "grid 0 1 fx0 [0 27] fx1 [u-10]" "$(sed -n 6p <<<"$out")"
expect "names off: maj7 inv2 is 459" "grid 0 3 fx0 [0459] fx1 [----]" "$(sed -n 7p <<<"$out")"

# 10. Strum on a MIDI instrument (USB MIDI out, the audio callback runs): +10 starts the maj7 notes 20.3 ms apart (7 blocks of
#     2.9 ms for 20 ms), bottom up; 74 (-10) top down; the slots the other way round work the same
out=$(run strum "instr 48; setstep 0 0 48 48 14 24 47 10; setstep 0 4 48 48 47 74 14 24; audio 1; midilog 1; tap Play; wait 900; tap Play; wait 200; midilog 0")
expect "strum up: notes at 0 / 20.3 / 40.6 / 60.9 ms, low to high" "midi on 72 127 ch 1 t 0.0|midi on 76 127 ch 1 t 20.3|midi on 79 127 ch 1 t 40.6|midi on 83 127 ch 1 t 60.9" "$(grep '^midi on' <<<"$out" | sed -n 1,4p | paste -sd'|' -)"
expect "strum down (Strum in FX1, chord in FX2): high to low" "midi on 83 127 ch 1 t 461.3|midi on 79 127 ch 1 t 481.6|midi on 76 127 ch 1 t 501.9|midi on 72 127 ch 1 t 522.2" "$(grep '^midi on' <<<"$out" | sed -n 5,8p | paste -sd'|' -)"
expect "strum: Stop sends the offs, nothing starts after them" "midi off 72 ch 1 t 977.7" "$(grep '^midi' <<<"$out" | tail -4 | head -1)"

# 11. Strum edges: +63 (126 ms a gap) cut by the next step at 116 ms (the pending notes never start), 64 is off (all at once), a Strum
#     without a MIDI Chord in the other slot plays the step note alone
out=$(run strumcut "instr 48; setstep 0 0 48 48 47 63 14 24; setstep 0 1 50 48 0 0 0 0; setstep 0 4 48 48 14 24 47 64; setstep 0 8 48 48 0 0 47 10; audio 1; midilog 1; tap Play; wait 1000; tap Play; wait 100")
expect "strum cut: only the first note started before the next step" "midi on 72 127 ch 1 t 0.0|midi on 74 127 ch 1 t 116.1" "$(grep '^midi on' <<<"$out" | sed -n 1,2p | paste -sd'|' -)"
expect "strum 64: the four notes at once" "midi on 72 127 ch 1 t 461.3|midi on 76 127 ch 1 t 461.3|midi on 79 127 ch 1 t 461.3|midi on 83 127 ch 1 t 461.3" "$(grep '^midi on' <<<"$out" | sed -n 3,6p | paste -sd'|' -)"
expect "strum without a chord: the note alone" "midi on 72 127 ch 1 t 922.6" "$(grep '^midi on' <<<"$out" | sed -n 7p)"

# 12. Record join (Rec + Play, one armed track, synth part 1): C E G within 20 ms is one step with maj, the release writes the OFF;
#     two legato notes 200 ms apart stay two steps
out=$(run recjoin "instr 64; arm 0; audio 1; tap Play; wait 100; hold Rec; tap Play; unhold Rec; wait 300; midion 60 100 10; midion 64 100 10; midion 67; wait 300; midioff 60; midioff 64; midioff 67; wait 200; midion 62; wait 200; midion 65; wait 100; midioff 62; midioff 65; wait 300; tap Play; wait 100; step 0 0; step 0 1; step 0 2; step 0 3; step 0 4; step 0 5; step 0 6; step 0 7; step 0 8; step 0 9; step 0 10; step 0 11; step 0 12; step 0 13; step 0 14; step 0 15; grid 0 6; $(shot recjoin-grid)")
expect "record join: C E G is one step, maj" "step 0 6 note 36 instr 64 fx0 14:5 fx1 0:0" "$(grep '^step 0 6 ' <<<"$out")"
expect "record join: the OFF after the last release" "step 0 9 note -4 instr 0 fx0 0:0 fx1 0:0" "$(grep '^step 0 9 ' <<<"$out")"
expect "record join: legato notes 200 ms apart stay two steps" "step 0 11 note 38 instr 64 fx0 0:0 fx1 0:0|step 0 14 note 41 instr 64 fx0 0:0 fx1 0:0" "$(grep -E '^step 0 1[14] ' <<<"$out" | paste -sd'|' -)"
expect "record join: the grid names the chord" "grid 0 6 fx0 [0maj] fx1 [----]" "$(grep '^grid' <<<"$out")"
expect "record join: nothing else written (chord, OFF, 2 legato notes, OFF)" "5" "$(grep '^step' <<<"$out" | grep -vc 'note -1 instr 0 fx0 0:0 fx1 0:0')"

# 13. Record join edges (midion N V MS: the loop runs MS after the note): a note 120 ms after the first (past the 100 ms gap window) is not joined (stock: its step is taken, it is dropped);
#     several armed tracks, cursor track armed: the join runs on the cursor track (recjoin-cursor, recjoin_cursor_sim.sh has the stock spread for a cursor track that is not armed); the inversion E G C on MIDI instrument 1 joins as maj inv1 on E
out=$(run reclate "instr 64; arm 0; audio 1; tap Play; wait 100; hold Rec; tap Play; unhold Rec; wait 300; midion 60 100 120; midion 64; wait 300; midioff 60; midioff 64; wait 100; tap Play; wait 100; step 0 6")
expect "late note: not joined, the step keeps the first note alone" "step 0 6 note 36 instr 64 fx0 0:0 fx1 0:0" "$out"
out=$(run recmulti "instr 64; audio 1; tap Play; wait 100; hold Rec; tap Play; unhold Rec; wait 300; midion 60 100 10; midion 64 100 10; midion 67; wait 300; midioff 60; midioff 64; midioff 67; wait 100; tap Play; wait 100; step 0 6; step 1 6; step 2 6")
expect "several armed tracks, the cursor track (1) armed: the join runs there, one step with maj, nothing on tracks 2 and 3" "step 0 6 note 36 instr 64 fx0 14:5 fx1 0:0|step 1 6 note -1 instr 0 fx0 0:0 fx1 0:0|step 2 6 note -1 instr 0 fx0 0:0 fx1 0:0" "$(paste -sd'|' - <<<"$out")"
out=$(run recinv "instr 48; arm 0; audio 1; tap Play; wait 100; hold Rec; tap Play; unhold Rec; wait 300; midion 64 100 15; midion 67 100 15; midion 72; wait 300; midioff 64; midioff 67; midioff 72; wait 100; tap Play; wait 100; step 0 6")
expect "MIDI instrument: E G C joins as maj inv1 (30) on E" "step 0 6 note 40 instr 48 fx0 14:30 fx1 0:0" "$out"

# 14. Record join on a step whose FX slots are taken: stock (the step is not empty, nothing is written, nothing joins)
out=$(run recbusy "instr 64; arm 0; setstep 0 6 -1 0 4 50 3 2; audio 1; tap Play; wait 100; hold Rec; tap Play; unhold Rec; wait 300; midion 60 100 10; midion 64 100 10; midion 67; wait 300; midioff 60; midioff 64; midioff 67; wait 100; tap Play; wait 100; step 0 6")
expect "busy FX step: unchanged (stock)" "step 0 6 note -1 instr 0 fx0 4:50 fx1 3:2" "$out"

# 15. Record join against the arrival time at the handler (Config Recording options Only notes, the unit's setting). A note joins when it comes
#     within 100 ms of the PREVIOUS note of the group (the window slides) and within 300 ms of the first, because the USB MIDI path of the unit
#     spread notes sent 13 ms apart over 40 to 70 ms on the unit (J1). 10, 40 and 70 ms gaps join the whole triad, 120 ms gaps join nothing
#     (the notes go to their own steps), a gap of 90 ms still joins and the total span cap (3 notes 90 ms apart = 180 ms) holds
for g in 10 40 70 90 120; do
  out=$(run "rjgap$g" "recopt 0; instr 64; arm 2; audio 1; tap Play; wait 200; hold Rec; tap Play; unhold Rec; wait 300; midion 60 100 $g; midion 64 100 $g; midion 67; wait 400; midioff 60; midioff 64; midioff 67; wait 300; tap Play; wait 300; step 2 0; step 2 1; step 2 2; step 2 3; step 2 4; step 2 5; step 2 6; step 2 7; step 2 8; step 2 9; step 2 10; step 2 11; step 2 12; step 2 13; step 2 14; step 2 15; step 2 16; step 2 17; step 2 18; step 2 19; step 2 20; step 2 21; step 2 22; step 2 23; step 2 24; step 2 25; step 2 26; step 2 27; step 2 28; step 2 29; step 2 30; step 2 31;")
  got=$(grep -v 'note -1 instr 0 fx0 0:0' <<<"$out" | grep -v 'note -4' | paste -sd'|' -)
  case $g in
    10|40|70|90) want="step 2 6 note 36 instr 64 fx0 14:5 fx1 0:0" ;;
    120) want="step 2 6 note 36 instr 64 fx0 0:0 fx1 0:0|step 2 7 note 40 instr 64 fx0 0:0 fx1 0:0|step 2 8 note 43 instr 64 fx0 0:0 fx1 0:0" ;;
  esac
  expect "record join, arrival gaps $g ms" "$want" "$got"
done
# 16. A pair that is no chord yet does not block the group: C then G (no entry) then E completes C E G (the refused G stays in the group)
out=$(run rjorder "recopt 0; instr 64; arm 2; audio 1; tap Play; wait 200; hold Rec; tap Play; unhold Rec; wait 300; midion 60 100 20; midion 67 100 20; midion 64; wait 400; midioff 60; midioff 67; midioff 64; wait 300; tap Play; wait 300; step 2 6; step 2 7")
expect "record join: C G E (the pair C G is no chord) ends as maj" "step 2 6 note 36 instr 64 fx0 14:5 fx1 0:0|step 2 7 note -1 instr 0 fx0 0:0 fx1 0:0" "$(paste -sd'|' - <<<"$out")"

new=$(cat "$WORK"/*.err 2>/dev/null | grep "SUMMARY" | grep -v "mtWavetableEnvelopes.cpp:206" | sort | uniq -c)
if [ -n "$new" ]; then fails=$((fails+1)); echo "FAIL  sanitizer report in a run:"; echo "$new"; fi
echo "$checks checks"
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fails failing)"; exit 1
