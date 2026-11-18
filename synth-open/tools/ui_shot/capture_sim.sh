#!/bin/bash
# capture_sim.sh [PNG_DIR]
#
# Capture (Config > Features > Capture, core/source/mtSequencer_capture.cpp) on the real UI code of this tree (ui_shot) on the Mac. Host only: no
# device, every scenario has its own SD card folder. Phase 1: the transport runs. Notes go in through the pads ("padon" / "padoff", pad 0 is C6,
# note 72 on a fresh project) and the USB MIDI input ("midion", "midioff", MIDI note 60 lands as 36), the under-screen button 3 is "tap F4",
# the ring is read with "capring", a step of the pattern with "step" / "stepv", a step of the copy buffer (where Capture puts the notes, the
# stock Copy/Paste buffer) with "capbuf", its selection with "capsel", the cursor with "cursor" (ui_shot_main.cpp). The stock Paste is the real
# key chord Shift + Copy in the red mode. The default tempo of a fresh project is 130 BPM (115.4 ms a step, the pattern of 32 steps is one
# pass of 3.69 s).
# Checks: the ring (what is kept, in which screens, the wrap), the step each note lands on in the buffer (the stock live record rules, also
# cross-checked against the stock record itself), velocity and micro move per record option, chords (matcher, held first note, sample
# instruments spread over the following sample tracks and the surplus dropped), the window of one pass, the transport stopped, Rec, the
# feature off (the button does nothing, nothing is kept), the pattern untouched until a Paste, the stock Paste of the buffer at several
# cursor positions, tracks and on another pattern, the buffer kept over a pattern change, one Undo takes a Paste back.
# Section 12: the title bar indicator (hidden, empty, stale, ready, redrawn only on a state change, on other screens too), the idle time that empties
# the ring (boundary, default 20 s), the long press of button 3 that clears it, and PNG goldens of the screens in goldens/capture/ (byte equal,
# UPDATE_GOLDENS=1 writes them).
# PNG_DIR: also writes the screens. SAN=address,undefined: the sanitizer build (build/ui-shot-san), a report fails the run except the stock
# mtWavetableEnvelopes.cpp:206 (also allowlisted by the sim gate).
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="$BUILD/ui_shot"
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/capture_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() {   # run NAME "script": the script's read-back lines, one per line
    local sd="$WORK/$1"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; $2" 2>"$WORK/$1.err" | grep -E '^(step|stepv|cursor|title|capring|cap|armed|status|capbuf|capsel|capind) '
}
shot() { [ -n "$PNG_DIR" ] && echo "shot $PNG_DIR/$1.png" || echo "wait 1"; }
# a golden screen: the script line that writes it (gshot) and the byte-equal check against goldens/capture/ (golden), UPDATE_GOLDENS=1 writes them
GOLD="$HERE/goldens/capture"
mkdir -p "$WORK/png"
gshot() { echo "shot $WORK/png/$1.png"; }
golden() {
    local f="$WORK/png/$1.png"
    [ -n "$PNG_DIR" ] && [ -f "$f" ] && cp "$f" "$PNG_DIR/"
    if [ -n "${UPDATE_GOLDENS:-}" ]; then mkdir -p "$GOLD"; cp "$f" "$GOLD/$1.png"; expect "golden $1 written" "yes" "$([ -s "$GOLD/$1.png" ] && echo yes || echo no)"; return; fi
    expect "golden $1: the screen is byte equal to goldens/capture/$1.png" "equal" "$([ -f "$f" ] && cmp -s "$f" "$GOLD/$1.png" && echo equal || echo differs)"
}
# the steps 0..31 of a track of the copy buffer ("capbuf" lines) and of the pattern ("step" and "stepv" lines)
bsteps() { local t=$1 s; for s in $(seq 0 31); do printf 'capbuf %d %d; ' "$t" "$s"; done; }
psteps() { local t=$1 s; for s in $(seq 0 31); do printf 'step %d %d; stepv %d %d; ' "$t" "$s" "$t" "$s"; done; }
# the note steps of the output of bsteps() or psteps(): "<step>:<note>:<instr>:<fx0>:<fx1>" per step that is not empty, the OFFs as -4
notes() { grep -E '^(step|capbuf) ' | grep -v 'note -1 instr 0 fx0 0:0 fx1 0:0' | awk '{print $3 ":" $5 ":" $7 ":" $9 ":" $11}' | paste -sd' ' -; }
# the volume and micro move of a buffer step, in the words the old direct write test used
bvol() { sed -E 's/^capbuf ([0-9]+) ([0-9]+) .* (vol .*)$/stepv \1 \2 \3/'; }
# stop, red mode (edit), the cursor to step 0 of the track the cursor is on, the stock Paste (Shift + Copy)
EDIT="tap Play; wait 300; tap Rec; wait 200"
HOME="hold Shift; tap Insert; unhold Shift; wait 100"
PASTE="hold Shift; tap Copy; unhold Shift; wait 300"
# the stock step of an event of the ring: the step it was played in, the next one in the second half of the step (uStep over 24)
stockstep() { echo $(( ($1 + ($2 > 24 ? 1 : 0)) % 32 )); }

# 1. the feature is off (the default): MIDI and pads are not kept, the button does nothing (the buffer keeps what it had)
out=$(run off "instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; padon 0 100; padoff 0 100; capring; tap F4; wait 100; $(shot capture-off); capsel; $(bsteps 0)")
expect "off: nothing is kept" "capring count 0" "$(grep '^capring' <<<"$out")"
expect "off: button 3 puts nothing in the buffer and sets no selection" "capsel 0 0 0 0||" "$(grep '^capsel' <<<"$out")|$(notes <<<"$out")|"

# 2. one MIDI note and one pad note, quantised (Only notes, the default): the step the stock record would take, and the OFF at the release.
#    They land in the copy buffer (one pass tall, the start track wide), the pattern stays as it was
out=$(run basic "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; padon 0 100; padoff 0 100; capring; $(shot capture-before); tap F4; wait 100; $(shot capture-after); capsel; $(bsteps 0); $(psteps 0)")
expect "ring: the four events, MIDI flags 2, pad flags 3, velocity 127 (the record options do not take it)" "4" "$(grep -c '^cap ' <<<"$out")"
c=$(grep '^cap ' <<<"$out")
m_on=$(sed -n 1p <<<"$c"); m_off=$(sed -n 2p <<<"$c"); p_on=$(sed -n 3p <<<"$c"); p_off=$(sed -n 4p <<<"$c")
expect "ring: MIDI note on" "cap 36 on v127 pos 3 ustep 13 instr 64 pat 1 flags 2" "$(sed -E 's/ t [0-9]+$//' <<<"$m_on")"
expect "ring: pad note on, from the pads" "cap 72 on v127 pos 5 ustep 43 instr 64 pat 1 flags 3" "$(sed -E 's/ t [0-9]+$//' <<<"$p_on")"
set -- $m_on;  s_on1=$(stockstep "$6" "$8"); set -- $m_off; s_off1=$(stockstep "$6" "$8")
set -- $p_on;  s_on2=$(stockstep "$6" "$8"); set -- $p_off; s_off2=$(stockstep "$6" "$8")
expect "capture: the notes sit on the steps of the stock rule (ustep over 24 goes to the next step), the OFFs on the release steps, in the buffer" \
  "$s_on1:36:64:0:0:0:0 $s_off1:-4:0:0:0:0:0 $s_on2:72:64:0:0:0:0 $s_off2:-4:0:0:0:0:0" "$(grep '^capbuf' <<<"$out" | notes)"
expect "capture: absolute steps 3 (note) 4 (OFF), 6 (pad note) 7 (OFF)" "3:36:64:0:0:0:0 4:-4:0:0:0:0:0 6:72:64:0:0:0:0 7:-4:0:0:0:0:0" "$(grep '^capbuf' <<<"$out" | notes)"
expect "capture: the selection is one pass tall (steps 0..31) and one track wide (track 0)" "capsel 0 0 31 0" "$(grep '^capsel' <<<"$out")"
expect "capture: the pattern itself is not touched" "" "$(grep '^step' <<<"$out" | notes)"
out2=$(run basic2 "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap F4; wait 100; capring")
expect "capture: the ring is used up, a second press finds nothing" "capring count 0" "$(grep '^capring' <<<"$out2")"

# 2b. the stock Paste (Shift + Copy in the red mode) puts the buffer on the cursor: step 0 gives the steps the direct write gave, step 10 the
#     same shifted by 10, another track the same on that track. Paste replaces the rows it covers (stock), so a second Paste on the same track
#     overwrites the first one from its start row on
out=$(run paste "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; padon 0 100; padoff 0 100; tap F4; wait 100; $EDIT; $HOME; $PASTE; $(psteps 0)")
expect "paste at step 0: the same steps the direct write gave (3 36, 4 OFF, 6 72, 7 OFF)" "3:36:64:0:0:0:0 4:-4:0:0:0:0:0 6:72:64:0:0:0:0 7:-4:0:0:0:0:0" "$(grep '^step ' <<<"$out" | notes)"
out=$(run paste10 "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; padon 0 100; padoff 0 100; tap F4; wait 100; $EDIT; $HOME; tap Down 10; cursor; $PASTE; $(psteps 0)")
expect "paste at step 10: the same steps shifted by 10" "13:36:64:0:0:0:0 14:-4:0:0:0:0:0 16:72:64:0:0:0:0 17:-4:0:0:0:0:0" "$(grep '^step ' <<<"$out" | notes)"
out=$(run paste2 "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap F4; wait 100; $EDIT; $HOME; $PASTE; tap Down 10; $PASTE; tap Right 3; $PASTE; $(psteps 0); $(psteps 3)")
expect "paste twice on a track and once on track 3: both pastes on track 0, the one on track 3 at step 10" "3:36:64:0:0:0:0 4:-4:0:0:0:0:0 13:36:64:0:0:0:0 14:-4:0:0:0:0:0 13:36:64:0:0:0:0 14:-4:0:0:0:0:0" \
  "$(grep '^step 0 ' <<<"$out" | notes) $(grep '^step 3 ' <<<"$out" | notes)"
# the buffer is not used up by a Paste: it pastes again, also after other pastes, and survives a pattern change (stock buffer, only a boot empties it)
out=$(run bufkeep "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap F4; wait 100; capsel; capbuf 0 3; $EDIT; $HOME; $PASTE; hold F1; enc 1; unhold F1; wait 500; capsel; capbuf 0 3; $HOME; $PASTE; $(psteps 0); hold F1; enc -1; unhold F1; wait 500; $(psteps 0)")
expect "another pattern: the buffer and its selection are still there after the pattern change" "capsel 0 0 31 0|capbuf 0 3 note 36 instr 64 fx0 0:0 fx1 0:0 vol -1 micro -1|capsel 0 0 31 0|capbuf 0 3 note 36 instr 64 fx0 0:0 fx1 0:0 vol -1 micro -1" \
  "$(grep -E '^(capsel|capbuf)' <<<"$out" | paste -sd'|' -)"
n=$(grep -c '^step ' <<<"$out")
expect "another pattern: the Paste lands in pattern 2, pattern 1 keeps its own Paste" "3:36:64:0:0:0:0 4:-4:0:0:0:0:0|3:36:64:0:0:0:0 4:-4:0:0:0:0:0" \
  "$(grep '^step ' <<<"$out" | sed -n 1,32p | notes)|$(grep '^step ' <<<"$out" | sed -n 33,64p | notes)"

# 3. the record options, the same as the stock live record writes: velocity (Velocity), micro move (Micro timing), both
for ro in 0 1 2 3; do
  out=$(run "ropt$ro" "recopt $ro; capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 80 100; midioff 60 0 200; tap F4; wait 100; capbuf 0 3; capbuf 0 4")
  case $ro in
    0) want="stepv 0 3 vol -1 micro -1|stepv 0 4 vol -1 micro -1" ;;
    1) want="stepv 0 3 vol -1 micro 25|stepv 0 4 vol -1 micro 14" ;;
    2) want="stepv 0 3 vol 49 micro -1|stepv 0 4 vol -1 micro -1" ;;
    3) want="stepv 0 3 vol 49 micro 25|stepv 0 4 vol -1 micro 14" ;;
  esac
  expect "record option $ro: volume and micro move of the note and of its OFF" "$want" "$(bvol <<<"$out" | paste -sd'|' -)"
done
# the stock live record with the same note and velocity writes the same volume (Rec + Play, one armed track)
out=$(run stockvel "recopt 2; instr 64; arm 0; audio 1; tap Play; wait 100; hold Rec; tap Play; unhold Rec; wait 300; midion 60 80 100; midioff 60 0 200; wait 300; tap Play; wait 100; $(psteps 0)")
stock=$(grep -A1 'note 36 instr 64' <<<"$out" | grep '^stepv' | head -1 | sed 's/^stepv [0-9]* [0-9]* //')
expect "velocity: the capture writes the volume of the stock record" "vol 49 move -1" "$stock"

# 4. a chord on a synth instrument: C E G is maj (the bass is the step note, the chord goes into FX 1), the OFF after the last release
out=$(run chord "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 10; midion 64 100 10; midion 67 100 100; midioff 60 0 20; midioff 64 0 20; midioff 67 0 200; capring; tap F4; wait 100; $(shot capture-chord); $(bsteps 0); $EDIT; $HOME; tap Right 2; $PASTE; $(psteps 2)")
expect "chord: C E G is one step with maj in FX 1, the OFF at the last release (ustep 33 of step 4 goes to step 5)" "3:36:64:14:5:0:0 5:-4:0:0:0:0:0" "$(grep '^capbuf' <<<"$out" | notes)"
expect "chord: the Paste on track 2 carries the chord FX" "3:36:64:14:5:0:0 5:-4:0:0:0:0:0" "$(grep '^step 2 ' <<<"$out" | notes)"
# the first note is released before the next one starts: no join (the stock record needs the first still held), two notes
out=$(run legato "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 10; midioff 60 0 300; midion 64 100 10; midioff 64 0 200; capring; tap F4; wait 100; $(bsteps 0)")
expect "chord: a released first note does not join, two single notes with their OFFs (an OFF in the step of its onset goes one step on)" "3:36:64:0:0:0:0 4:-4:0:0:0:0:0 6:40:64:0:0:0:0 7:-4:0:0:0:0:0" "$(notes <<<"$out")"
# notes 120 ms apart are past the join window of 100 ms, even with the first still held
out=$(run late "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 120; midion 64 100 300; midioff 60 0 20; midioff 64 0 200; tap F4; wait 100; $(bsteps 0)")
expect "chord: 120 ms apart is two notes, no chord FX" "0" "$(grep -c 'fx0 14:' <<<"$out")"
# the pattern step holds two other effects: the buffer is its own block, the chord is written there and the pattern keeps its effects
out=$(run busy "capture 1; instr 64; setstep 0 3 -1 0 4 50 3 2; audio 1; tap Play; wait 300; midion 60 100 10; midion 64 100 10; midion 67 100 100; midioff 60 0 20; midioff 64 0 20; midioff 67 0 200; tap F4; wait 100; $(shot capture-busy); capbuf 0 3; step 0 3")
expect "chord: the buffer holds the chord, the pattern step keeps its two effects" "capbuf 0 3 note 36 instr 64 fx0 14:5 fx1 0:0 vol -1 micro -1|step 0 3 note -1 instr 0 fx0 4:50 fx1 3:2" "$(paste -sd'|' - <<<"$out")"
# no chord in the table (C C# D): the first note alone
out=$(run nochord "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 10; midion 61 100 10; midion 62 100 100; midioff 60 0 20; midioff 61 0 20; midioff 62 0 200; tap F4; wait 100; capbuf 0 3")
expect "chord: no match, the first note alone" "capbuf 0 3 note 36 instr 64 fx0 0:0 fx1 0:0 vol -1 micro -1" "$out"

# 5. a chord on sample instruments: one note per track, the following sample tracks of the block (a note of the pattern is not in the block),
#    the surplus is dropped. The selection is as wide as the tracks used
out=$(run sample "capture 1; instr 0; setstep 1 3 50 0 0 0 0 0; audio 1; tap Play; wait 300; midion 60 100 10; midion 64 100 10; midion 67 100 100; midioff 60 0 20; midioff 64 0 20; midioff 67 0 200; tap F4; wait 100; $(shot capture-sample); capsel; capbuf 0 3; capbuf 1 3; capbuf 2 3; capbuf 3 3; step 1 3; $EDIT; $HOME; tap Right 4; $PASTE; step 4 3; step 5 3; step 6 3; step 7 3")
expect "sample chord: the block is tracks 0..2 (the note of the pattern on track 1 is not in it), nothing on track 3, the pattern keeps its note" \
  "capsel 0 0 31 2|capbuf 0 3 note 36 instr 0 fx0 0:0 fx1 0:0 vol -1 micro -1|capbuf 1 3 note 40 instr 0 fx0 0:0 fx1 0:0 vol -1 micro -1|capbuf 2 3 note 43 instr 0 fx0 0:0 fx1 0:0 vol -1 micro -1|capbuf 3 3 note -1 instr 0 fx0 0:0 fx1 0:0 vol -1 micro -1|step 1 3 note 50 instr 0 fx0 0:0 fx1 0:0|step 4 3 note 36 instr 0 fx0 0:0 fx1 0:0|step 5 3 note 40 instr 0 fx0 0:0 fx1 0:0|step 6 3 note 43 instr 0 fx0 0:0 fx1 0:0|step 7 3 note -1 instr 0 fx0 0:0 fx1 0:0" \
  "$(paste -sd'|' - <<<"$out")"
out=$(run surplus "capture 1; instr 0; arm 6; audio 1; tap Play; wait 300; midion 60 100 10; midion 62 100 10; midion 64 100 10; midion 65 100 100; midioff 60 0 20; midioff 62 0 20; midioff 64 0 20; midioff 65 0 200; tap F4; wait 100; $(shot capture-surplus); capsel; capbuf 6 3; capbuf 7 3; capbuf 8 3")
expect "sample chord: armed track 6 takes two notes, the other two are dropped (no wrap to the MIDI tracks), the selection is tracks 6..7" "capsel 0 6 31 7|capbuf 6 3 note 36 instr 0 fx0 0:0 fx1 0:0 vol -1 micro -1|capbuf 7 3 note 38 instr 0 fx0 0:0 fx1 0:0 vol -1 micro -1|capbuf 8 3 note -1 instr 0 fx0 0:0 fx1 0:0 vol -1 micro -1" "$(paste -sd'|' - <<<"$out")"

# 6. the buffer is the pass only: the notes of the pattern on the start track are not in it, the other tracks keep them. Nothing of the pattern changes
#    at the press. A Paste replaces the rows it covers on the tracks of the selection (the stock rule), the tracks outside stay
out=$(run collide "capture 1; instr 64; setstep 0 3 50 5 14 24 0 0; setstep 1 3 52 6 0 0 0 0; setstep 0 9 55 7 0 0 0 0; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap F4; wait 100; capbuf 0 3; capbuf 0 9; step 0 3; step 1 3; step 0 9; $EDIT; $HOME; $PASTE; step 0 3; step 1 3; step 0 9")
expect "collision: the buffer block holds only the captured note, the pattern is unchanged, the Paste replaces track 0 and leaves track 1" \
  "capbuf 0 3 note 36 instr 64 fx0 0:0 fx1 0:0 vol -1 micro -1|capbuf 0 9 note -1 instr 0 fx0 0:0 fx1 0:0 vol -1 micro -1|step 0 3 note 50 instr 5 fx0 14:24 fx1 0:0|step 1 3 note 52 instr 6 fx0 0:0 fx1 0:0|step 0 9 note 55 instr 7 fx0 0:0 fx1 0:0|step 0 3 note 36 instr 64 fx0 0:0 fx1 0:0|step 1 3 note 52 instr 6 fx0 0:0 fx1 0:0|step 0 9 note -1 instr 0 fx0 0:0 fx1 0:0" \
  "$(paste -sd'|' - <<<"$out")"
# the OFF goes into the buffer block (empty there), a Paste then replaces the note of the pattern in that row (stock rule)
out=$(run offkeep "capture 1; instr 64; setstep 0 4 55 7 0 0 0 0; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap F4; wait 100; capbuf 0 3; capbuf 0 4; step 0 4; $EDIT; $HOME; $PASTE; step 0 4")
expect "OFF: in the buffer at the release step, the pattern keeps its note until the Paste, which replaces it" "capbuf 0 3 note 36 instr 64 fx0 0:0 fx1 0:0 vol -1 micro -1|capbuf 0 4 note -4 instr 0 fx0 0:0 fx1 0:0 vol -1 micro -1|step 0 4 note 55 instr 7 fx0 0:0 fx1 0:0|step 0 4 note -4 instr 0 fx0 0:0 fx1 0:0" "$(paste -sd'|' - <<<"$out")"
# the armed track is the start track when exactly one is armed
out=$(run armed "capture 1; instr 64; arm 5; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap F4; wait 100; capsel; capbuf 0 3; capbuf 5 3")
expect "destination: the only armed track starts the block" "capsel 0 5 31 5|capbuf 0 3 note -1 instr 0 fx0 0:0 fx1 0:0 vol -1 micro -1|capbuf 5 3 note 36 instr 64 fx0 0:0 fx1 0:0 vol -1 micro -1" "$(paste -sd'|' - <<<"$out")"
# the cursor track starts it when nothing is armed
out=$(run cursortrack "capture 1; instr 64; tap Right 3; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap F4; wait 100; capsel; capbuf 3 3")
expect "destination: no armed track, the cursor track starts the block" "capsel 0 3 31 3|capbuf 3 3 note 36 instr 64 fx0 0:0 fx1 0:0 vol -1 micro -1" "$(paste -sd'|' - <<<"$out")"

# 7. the window is one pass of the pattern (32 steps, 3.69 s): a note played 4.2 s before the press is not taken, a note 1 s before is
out=$(run window "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 3000; midion 62 100 100; midioff 62 0 1000; tap F4; wait 100; $(bsteps 0)")
expect "window: only the note of the last pass is in the buffer (the older one is past one pass)" "1" "$(notes <<<"$out" | tr ' ' '\n' | grep -c ':38:64:')"
expect "window: the old note 60 (36) is not in it" "0" "$(notes <<<"$out" | tr ' ' '\n' | grep -c ':36:64:')"
# Nothing to capture leaves the buffer as it was (here a first capture, then an old note only)
out=$(run nothing "capture 1; instr 64; audio 1; tap Play; wait 300; midion 62 100 100; midioff 62 0 200; tap F4; wait 100; capsel; $(bsteps 0) midion 60 100 100; midioff 60 0 4200; tap F4; wait 100; $(shot capture-nothing); capsel; $(bsteps 0); capring")
half=$(grep -c . <<<"$(grep -E '^(capbuf|capsel)' <<<"$out")")
expect "window: everything older than one pass is Nothing to capture, the ring is kept and the buffer keeps the first capture" "$(grep -E '^(capbuf|capsel)' <<<"$out" | sed -n 1,33p | md5 -q)|capring count 2" \
  "$(grep -E '^(capbuf|capsel)' <<<"$out" | sed -n 34,66p | md5 -q)|$(grep '^capring' <<<"$out")"
expect "window: that first capture is in the buffer" "1" "$(grep -E '^capbuf' <<<"$out" | sed -n 1,32p | notes | tr ' ' '\n' | grep -c ':38:64:')"

# 7b. the length of a pass is the length the track plays (Sequencer::getTrackLastStep, polymeter), not the raw stored byte: an unmarked pattern from an old
# file can hold a stale length in a track other than 1 (rawlen seeds 3 into track 3), which the player ignores. The note played 1 s before the press
# is inside the 3.69 s pass and must be taken, with the raw byte the pass would be 4 steps (0.46 s) and it would read Nothing to capture
out=$(run stalelen "capture 1; instr 64; audio 1; arm 2; rawlen 2 3; tap Play; wait 300; midion 60 100 100; midioff 60 0 1000; tap F4; wait 100; capsel; $(bsteps 2)")
expect "stale stored track length: the pass is the track's played length, the note of the last second is captured on track 3" "1" "$(grep -E '^capbuf' <<<"$out" | notes | tr ' ' '\n' | grep -c ':36:64:')"

# 8. where the ring is fed and where the button works: stopped, Rec, red mode, another screen
out=$(run stopped "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap Play; wait 300; tap F4; wait 100; $(bsteps 0); capring")
expect "stopped: Capture does nothing (phase 1 needs playback), the events stay" "|capring count 2" "$(notes <<<"$out")|$(grep '^capring' <<<"$out")"
out=$(run notplayed "capture 1; instr 64; audio 1; midion 60 100 100; midioff 60 0 200; capring")
expect "stopped: notes played while the transport is stopped are kept but flagged, not captured later" "capring count 2" "$(grep '^capring' <<<"$out")"
out=$(run notplayed2 "capture 1; instr 64; audio 1; midion 60 100 100; midioff 60 0 200; tap Play; wait 300; tap F4; wait 100; $(bsteps 0)")
expect "stopped notes (transport not running at the event) are never captured" "" "$(notes <<<"$out")"
out=$(run rec "capture 1; instr 64; arm 0; audio 1; tap Play; wait 100; hold Rec; tap Play; unhold Rec; wait 300; midion 60 100 100; midioff 60 0 200; capring; tap F4; wait 100; tap Play")
expect "Rec: the stock record writes the notes, the ring does not take them" "capring count 0" "$(grep '^capring' <<<"$out")"
out=$(run red "capture 1; instr 64; tap Rec; midion 60 100 100; midioff 60 0 200; padon 0 100; padoff 0 100; capring")
expect "red mode (step entry): the notes are not kept" "capring count 0" "$(grep '^capring' <<<"$out")"
# (a sample instrument: the pad board of the Master and Song screens reads mtProject.instrument[64] for a synth one, a stock bug the SAN build reports)
out=$(run screen "capture 1; instr 0; audio 1; tap Play; wait 300; tap Master; wait 400; midion 60 100 100; midioff 60 0 200; capring; tap Pattern; wait 200; midion 62 100 100; midioff 62 0 200; capring")
expect "another screen: not kept, back in the pattern editor: kept" "capring count 0|capring count 2" "$(grep '^capring' <<<"$out" | paste -sd'|' -)"

# 9. no undo revision at the press (the pattern is not touched), the stock Paste makes its own: one Undo (red mode, button 6) takes the whole Paste back
out=$(run undo "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 10; midion 64 100 10; midion 67 100 100; midioff 60 0 20; midioff 64 0 20; midioff 67 0 200; padon 0 100; padoff 0 100; tap F4; wait 100; $EDIT; tap F6; wait 600; $(psteps 0); $HOME; $PASTE; $(psteps 0); tap F6; wait 600; $(psteps 0)")
st=$(grep '^step ' <<<"$out")
expect "undo: an Undo before the Paste has nothing of the capture to take back, the pattern is empty" "0" "$(sed -n 1,32p <<<"$st" | grep -vc 'note -1 instr 0 fx0 0:0 fx1 0:0')"
expect "undo: the Paste wrote steps (chord, OFF, pad note, OFF)" "4" "$(sed -n 33,64p <<<"$st" | grep -vc 'note -1 instr 0 fx0 0:0 fx1 0:0')"
expect "undo: one Undo takes the whole Paste back" "0" "$(sed -n 65,96p <<<"$st" | grep -vc 'note -1 instr 0 fx0 0:0 fx1 0:0')"

# 10. the ring wraps at 512 events and keeps the newest: 520 on/off pairs, then a last note. The reader stays 8 events behind the head
loop=""; for i in $(seq 1 260); do loop="$loop midion 61 100 0; midioff 61 0 0;"; done
out=$(run wrap "capture 1; instr 64; audio 1; tap Play; wait 300; $loop midion 62 100 100; midioff 62 0 200; capring; tap F4; wait 100; $(bsteps 0)")
expect "ring: 522 events in, 512 kept" "capring count 512" "$(grep '^capring' <<<"$out")"
got=$(notes <<<"$out" | tr ' ' '\n' | awk -F: '{print $2}' | sort -u | paste -sd' ' -)
expect "wrap: the last note wins the step of the 260 earlier ones (38, no 37 left), one OFF behind it" "-4 38" "$got"
expect "wrap: exactly two steps in the buffer" "2" "$(notes <<<"$out" | wc -w | tr -d ' ')"

# 11. Capture on, a note played in the green mode, the chord names and the other pattern editor features stay
out=$(run flag "capture 0; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; capring; capture 1; midion 60 100 100; midioff 60 0 200; capring")
expect "feature switched on later: the ring starts then" "capring count 0|capring count 2" "$(grep '^capring' <<<"$out" | paste -sd'|' -)"

# 12. the indicator in the title bar (TitleBar::setCaptureState, polled every 60 ms by Interface::commonThingsUpdate on every screen). States: 0 hidden
#     (feature off), 1 empty (hollow), 2 stale (notes the Capture would not take now: older than one pass, transport stopped), 3 ready (a Capture takes notes now).
#     "draws" counts the redraws: the square is redrawn only when the state changes
PHRASE="midion 60 100 100; midioff 60 0 200"   # a note, 100 ms held, then 200 ms of quiet
out=$(run indoff "instr 64; audio 1; tap Play; wait 300; $PHRASE; wait 300; capind; $(gshot ind-off)")
expect "indicator: the feature is off, nothing is drawn" "capind state 0 draws 0 idle 20 ring 0" "$(grep '^capind' <<<"$out")"
out=$(run indempty "capture 1; wait 300; capind; $(gshot ind-empty)")
expect "indicator: on, ring empty, hollow square" "capind state 1 draws 1 idle 20 ring 0" "$(grep '^capind' <<<"$out")"
out=$(run indready "capture 1; instr 64; audio 1; tap Play; wait 300; $PHRASE; wait 300; capind; $(gshot ind-ready); wait 500; capind")
expect "indicator: notes played in the running pattern, ready (filled), and no redraw while it stays so" "capind state 3 draws 2 idle 20 ring 2|capind state 3 draws 2 idle 20 ring 2" "$(grep '^capind' <<<"$out" | paste -sd'|' -)"
out=$(run indstale "capture 1; instr 64; audio 1; tap Play; wait 300; $PHRASE; tap Play; wait 400; capind; $(gshot ind-stale)")
expect "indicator: the transport stopped, the notes are kept but a Capture would not take them: stale" "capind state 2 draws 3 idle 20 ring 2" "$(grep '^capind' <<<"$out")"
out=$(run indcap "capture 1; instr 64; audio 1; tap Play; wait 300; $PHRASE; wait 100; capind; tap F4; wait 300; capind; capring")
expect "indicator: after a Capture the ring is used up, hollow again" "capind state 3 draws 2 idle 20 ring 2|capind state 1 draws 3 idle 20 ring 0|capring count 0" "$(grep -E '^capind|^capring' <<<"$out" | paste -sd'|' -)"
# the pass of the pattern is 3.69 s: a note 3.1 s old is ready, 4.8 s old is stale, the idle time (5 s here) then empties the ring
out=$(run indidle "capidle 5; capture 1; instr 64; audio 1; tap Play; wait 300; $PHRASE; wait 2800; capind; wait 1800; capind; wait 500; capind; capring")
expect "idle time: ready at 3.1 s, stale (ring kept) at 4.8 s, the ring is emptied at 5.3 s and the square goes hollow" \
  "capind state 3 draws 2 idle 5 ring 2|capind state 2 draws 3 idle 5 ring 2|capind state 1 draws 4 idle 5 ring 0|capring count 0" "$(grep -E '^capind|^capring' <<<"$out" | paste -sd'|' -)"
# the default is 20 s: kept at 19.2 s, emptied at 21.7 s
out=$(run indidle20 "capture 1; instr 64; audio 1; tap Play; wait 300; $PHRASE; wait 19000; capind; wait 2500; capind")
expect "idle time: the default 20 s keeps the notes at 19.2 s and empties the ring at 21.7 s" "capind state 2 draws 3 idle 20 ring 2|capind state 1 draws 4 idle 20 ring 0" "$(grep '^capind' <<<"$out" | paste -sd'|' -)"
# a new phrase after the idle time starts from an empty ring: a Capture takes only the latest phrase
out=$(run idlephrase "capidle 5; capture 1; instr 64; audio 1; tap Play; wait 300; $PHRASE; wait 5400; midion 62 100 100; midioff 62 0 200; capring; tap F4; wait 100; $(bsteps 0)")
expect "idle time: the second phrase is alone in the ring (2 events, not 4) and alone in the buffer" "capring count 2|1:0" "$(grep '^capring' <<<"$out")|$(notes <<<"$out" | tr ' ' '\n' | grep -c ':38:64:'):$(notes <<<"$out" | tr ' ' '\n' | grep -c ':36:64:')"
# the long press of button 3 (the first hold event, 333 ms on the unit) empties the ring and captures nothing, also not at the release
out=$(run indclear "capture 1; instr 64; audio 1; tap Play; wait 300; $PHRASE; wait 100; capsel; capind; hold F4; wait 350; repeat F4 1; wait 100; $(gshot ind-cleared); repeat F4 2; unhold F4; wait 400; capind; capsel; capbuf 0 3; $(gshot ind-after-clear)")
expect "long press: the ring is empty and the square hollow, the buffer and its selection are untouched (nothing captured at the press, the hold or the release)" \
  "capsel 0 0 0 0|capind state 3 draws 2 idle 20 ring 2|capind state 1 draws 3 idle 20 ring 0|capsel 0 0 0 0|capbuf 0 3 note -1 instr 0 fx0 0:0 fx1 0:0 vol -1 micro -1" "$(grep -E '^(capsel|capind|capbuf)' <<<"$out" | paste -sd'|' -)"
out=$(run indclear2 "capture 1; instr 64; audio 1; tap Play; wait 300; $PHRASE; hold F4; repeat F4 1; unhold F4; wait 200; $PHRASE; wait 100; capind; tap F4; wait 300; capsel; $(bsteps 0)")
expect "long press: notes played after the clear are captured by a short press again (a short press is a Capture, on the release), the phrase before the clear is not in it" \
  "capind state 3 draws 4 idle 20 ring 2|capsel 0 0 31 0|1 note, 1 OFF" \
  "$(grep -E '^(capind|capsel)' <<<"$out" | paste -sd'|' -)|$(notes <<<"$out" | tr ' ' '\n' | grep -c ':36:64:') note, $(notes <<<"$out" | tr ' ' '\n' | grep -c ':-4:') OFF"
# the feature off: no square, and the button has no long press (the ring is left alone, a ring that exists only because the feature was on a moment ago)
out=$(run indnoshortcut "capture 1; instr 64; audio 1; tap Play; wait 300; $PHRASE; capture 0; hold F4; repeat F4 1; unhold F4; wait 300; capind; capring")
expect "feature off again: the square is hidden and a long press of button 3 clears nothing" "capind state 0 draws 3 idle 20 ring 2|capring count 2" "$(grep -E '^(capind|capring)' <<<"$out" | paste -sd'|' -)"
# the square is in the shared title bar: another screen shows it too (a sample instrument: the Master pad board reads mtProject.instrument[64] for a synth one)
out=$(run indmaster "capture 1; instr 0; audio 1; tap Play; wait 300; $PHRASE; tap Master; wait 400; capind; $(gshot ind-master); tap Pattern; wait 300; capind")
expect "indicator: the Master screen shows the same state (ready), and the pattern editor again, without redraws in between" "capind state 3 draws 2 idle 20 ring 2|capind state 3 draws 2 idle 20 ring 2" "$(grep '^capind' <<<"$out" | paste -sd'|' -)"
for g in ind-off ind-empty ind-ready ind-stale ind-cleared ind-after-clear ind-master; do golden "$g"; done

new=$(cat "$WORK"/*.err 2>/dev/null | grep "SUMMARY" | grep -v "mtWavetableEnvelopes.cpp:206" | sort | uniq -c)
if [ -n "$new" ]; then fails=$((fails+1)); echo "FAIL  sanitizer report in a run:"; echo "$new"; fi
echo "$checks checks"
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fails failing)"; exit 1
