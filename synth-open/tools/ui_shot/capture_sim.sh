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
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/capture_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
# CAP_MUTANT=<name> rewrites the scripts so that one thing the checks guard is not done: the checks of it must go RED (run_tests asserts it for each
# mutant with CAP_SKIP16=1, which leaves out the take file section). off: Capture switched off, nopress: button 3 never pressed, noundo: Undo never pressed,
# nopat: the pattern is not changed, twice: a further note and a second press at every press, emptynote: a note before the press with no take,
# nocopy: the user's Copy is not made, nohint: the track channel notes come in as USB notes, pastend: the notes are moved across the pattern end
# (and the long take inside it), nocollide: the pattern step the capture lands on is empty, spillcap: the third note of the cap row lands inside the
# 16 patterns, spillundo: no Undo after the spill, spillorder: pattern 2 is in the song already, spillparts: the pattern plays instead of the Part,
# spillelse: the second press stays in pattern 1, spillsame: the second press is in another pattern, spillgrow: no further note before the second press,
# spilloff: 62 is released at step 35, in its own pattern, spillend: the held note is released early, nopace: the spill's file writes are not
# paced, repundo: the second Undo of a same-pattern re-press is not pressed
mutate() {
    case "${CAP_MUTANT:-}" in
        off) sed 's/capture 1/capture 0/g' ;;
        nopress) sed 's/tap F4/wait 1/g' ;;
        noundo) sed 's/tap F6/wait 1/g' ;;
        nopat) sed 's/hold F1; enc 1; unhold F1/wait 1/g' ;;
        twice) sed 's/tap F4/tap F4; wait 100; midion 66 100 100; midioff 66 0 100; tap F4/g' ;;
        emptynote) sed 's/wait 300; tap F4; wait 100; capind;/wait 300; midion 60 100 100; midioff 60 0 200; tap F4; wait 100; capind;/' ;;
        nocopy) sed 's/tap Copy/wait 1/g' ;;
        nohint) sed 's/midich 5 60 100; midichoff 5 60/midion 60 100 100; midioff 60 0 100/' ;;
        pastend) sed 's/midioff 60 0 4300/midioff 60 0 300/; s/midioff 60 0 2800/midioff 60 0 4300/' ;;
        nocollide) sed 's/setstep 0 0 50 5 4 24 0 0;//' ;;
        spillcap) sed 's/midioff 62 0 5000/midioff 62 0 100/' ;;
        spillundo) sed 's/tap F6; wait 600; songlist/wait 1; songlist/' ;;
        spillorder) sed 's/playlist 1 7 3;/playlist 1 2 7 3;/' ;;
        spillparts) sed 's/tap Up 3; audio 1; tap F2; wait 300; tap Pattern; wait 300;/tap Pattern; wait 300; audio 1; tap Play; wait 300;/' ;;
        spillelse) sed 's/tap F4; wait 100; pattern 9; wait 300;/tap F4; wait 100; wait 300;/' ;;
        spillsame) sed 's/tap F4; wait 100; tap F4; wait 100; fx34popup/tap F4; wait 100; pattern 2; wait 300; tap F4; wait 100; fx34popup/' ;;
        spillgrow) sed 's/wait 6000; midion 64 100 100;/wait 600; midion 64 100 100;/' ;;
        spilloff) sed 's/midion 64 100 100; midioff 64 0 4000;/midioff 62 0 100; midion 64 100 100; midioff 64 0 3900;/' ;;
        spillend) sed 's/midion 62 100 1300; midioff 62 0 100; tap F4/midion 62 100 100; midioff 62 0 100; tap F4/' ;;
        nopace) sed 's/spillpace 1;/spillpace 0;/' ;;
        repundo) sed 's/tap F6; wait 601/wait 1; wait 601/' ;;
        *) cat ;;
    esac
}
run() {   # run NAME "script": the script's read-back lines, one per line
    local sd="$WORK/$1" sc
    sc=$(printf '%s' "$2" | mutate)
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; $sc" 2>"$WORK/$1.err" | grep -E '^(step|stepv|cursor|title|capring|cap|armed|status|capbuf|capsel|capind|capfile|capread|capev|ftmode|capflush|fspool|fx34popup|songlist|patfile|parts|spillpace) '
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

if [ "${CAP_ONLY16:-}" != 1 ]; then   # CAP_ONLY16=1: only section 16 (the take file), for its own development
# 1. the feature is off: MIDI and pads are not kept, the button does nothing (the pattern and the copy buffer keep what they had)
out=$(run off "instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; padon 0 100; padoff 0 100; capring; tap F4; wait 100; $(shot capture-off); capsel; $(bsteps 0); $(psteps 0)")
expect "off: nothing is kept" "capring count 0" "$(grep '^capring' <<<"$out")"
expect "off: button 3 writes nothing in the pattern, puts nothing in the copy buffer and sets no selection" "capsel 0 0 0 0|||" "$(grep '^capsel' <<<"$out")|$(grep '^capbuf' <<<"$out" | notes)|$(grep '^step ' <<<"$out" | notes)|"

# 2. one MIDI note and one pad note, quantised (Only notes, the default), written straight into the pattern (the destination track, here the cursor
#    track 0). The first note on is step 0 (a take starts on step 1), the others by the time since it at the tempo now, on the step grid the first
#    note was played on: the MIDI note on at ustep 13 of step 3 (t 377 ms) is step 0, its off 101 ms later 1, the pad note 302 ms later 3 (2.87 steps
#    plus the 0.25 of the first note's ustep, nearest step), its off 400 ms after the first 4
out=$(run basic "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; padon 0 100; padoff 0 100; capring; $(shot capture-before); tap F4; wait 100; $(shot capture-after); capsel; capind; $(bsteps 0); $(psteps 0)")
expect "ring: the four events, flags 1 (running), MIDI with its own velocity and source 2 (USB), the pad with 127 and source 0, no destination hint (15)" "4" "$(grep -c '^cap ' <<<"$out")"
c=$(grep '^cap ' <<<"$out")
m_on=$(sed -n 1p <<<"$c"); p_on=$(sed -n 3p <<<"$c")
expect "ring: MIDI note on" "cap 36 on v100 pos 3 ustep 13 instr 64 pat 1 flags 1 src 2 dest 15" "$(sed -E 's/ t [0-9]+$//' <<<"$m_on")"
expect "ring: pad note on, from the pads" "cap 72 on v127 pos 5 ustep 43 instr 64 pat 1 flags 1 src 0 dest 15" "$(sed -E 's/ t [0-9]+$//' <<<"$p_on")"
expect "capture: the notes are in the pattern, the first on step 0, the OFFs on the release steps" "0:36:64:0:0:0:0 1:-4:0:0:0:0:0 3:72:64:0:0:0:0 4:-4:0:0:0:0:0" "$(grep '^step ' <<<"$out" | notes)"
expect "capture: the copy buffer and its selection are untouched by the press" "capsel 0 0 0 0||" "$(grep '^capsel' <<<"$out")|$(grep '^capbuf' <<<"$out" | notes)|"
expect "capture: the take is not used up by the press, the indicator is still ready (state 3) and the 4 events are still there" "capind state 3 draws 2 take 1 ring 4 seq 4" "$(grep '^capind' <<<"$out")"

# 2b. a second press in the same pattern writes the same notes on the same steps (no doubling, no second set of OFFs), also after another note was
#     played in between (the take grew: the first notes stay where they were, the new one is added)
out=$(run repress "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; padon 0 100; padoff 0 100; tap F4; wait 100; $(psteps 0); tap F4; wait 100; $(psteps 0); midion 62 100 100; midioff 62 0 100; tap F4; wait 100; $(psteps 0)")
st=$(grep '^step ' <<<"$out")
expect "re-press: the first press wrote 4 steps" "0:36:64:0:0:0:0 1:-4:0:0:0:0:0 3:72:64:0:0:0:0 4:-4:0:0:0:0:0" "$(sed -n 1,32p <<<"$st" | notes)"
expect "re-press: the second press in the same pattern leaves exactly the same steps (nothing doubled)" "$(sed -n 1,32p <<<"$st" | notes)" "$(sed -n 33,64p <<<"$st" | notes)"
expect "re-press: a note played in between is added by the third press, the others did not move" "0:36:64:0:0:0:0 1:-4:0:0:0:0:0 3:72:64:0:0:0:0 4:-4:0:0:0:0:0 8:38:64:0:0:0:0 9:-4:0:0:0:0:0" "$(sed -n 65,96p <<<"$st" | notes)"

# 2c. the same take pressed in another pattern lands there too (a press does not use the take up), the first pattern keeps its own
out=$(run otherpat "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap F4; wait 100; hold F1; enc 1; unhold F1; wait 500; $(psteps 0); tap F4; wait 100; $(psteps 0); hold F1; enc -1; unhold F1; wait 500; $(psteps 0)")
st=$(grep '^step ' <<<"$out")
expect "another pattern: pattern 2 is empty before the press" "" "$(sed -n 1,32p <<<"$st" | notes)"
expect "another pattern: the press there lands the same take (the note on step 0, its OFF on step 1), the first pattern is still as the first press left it" "0:36:64:0:0:0:0 1:-4:0:0:0:0:0|0:36:64:0:0:0:0 1:-4:0:0:0:0:0" "$(sed -n 33,64p <<<"$st" | notes)|$(sed -n 65,96p <<<"$st" | notes)"

# 2d. one Undo (red mode, button 6) takes one press back, nothing else: two presses are two Undo revisions
out=$(run undo "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap F4; wait 100; midion 64 100 100; midioff 64 0 100; tap F4; wait 100; $EDIT; $(psteps 0); tap F6; wait 600; $(psteps 0); tap F6; wait 600; $(psteps 0)")
st=$(grep '^step ' <<<"$out")
expect "undo: after two presses the pattern holds both notes" "0:36:64:0:0:0:0 1:-4:0:0:0:0:0 4:40:64:0:0:0:0 5:-4:0:0:0:0:0" "$(sed -n 1,32p <<<"$st" | notes)"
expect "undo: one Undo takes the second press back (the first one stays), the next takes the first press back" "0:36:64:0:0:0:0 1:-4:0:0:0:0:0|" "$(sed -n 33,64p <<<"$st" | notes)|$(sed -n 65,96p <<<"$st" | notes)"

# 2e. a press with no take writes nothing, makes no Undo revision (the Undo after it has nothing of the capture to take back) and is told
out=$(run emptypress "capture 1; instr 64; setstep 0 3 50 5 0 0 0 0; audio 1; tap Play; wait 300; tap F4; wait 100; capind; $EDIT; $(psteps 0); tap F6; wait 600; $(psteps 0)")
st=$(grep '^step ' <<<"$out")
expect "empty take: the press writes nothing (the note of the pattern is the only one), the indicator stays hollow" "3:50:5:0:0:0:0|capind state 1 draws 1 take 0 ring 0 seq 0" "$(sed -n 1,32p <<<"$st" | notes)|$(grep '^capind' <<<"$out")"
expect "empty take: and the press made no Undo revision (the Undo changes nothing)" "3:50:5:0:0:0:0" "$(sed -n 33,64p <<<"$st" | notes)"

# 2f. a user Copy is not overwritten by a press: Copy a step in the red mode, play and press, the buffer and its selection are as the Copy left them
out=$(run bufkeep "capture 1; instr 64; setstep 0 5 60 9 0 0 0 0; audio 1; $EDIT; $HOME; tap Down 5; tap Copy; wait 200; capsel; capbuf 0 5; tap Play; wait 300; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap F4; wait 100; capsel; capbuf 0 5; $(psteps 0)")
expect "copy buffer: the press leaves the user's Copy alone (the buffer and its selection are the same before and after)" "$(grep -E '^(capsel|capbuf)' <<<"$out" | sed -n 1,2p | paste -sd'|' -)" "$(grep -E '^(capsel|capbuf)' <<<"$out" | sed -n 3,4p | paste -sd'|' -)"
expect "copy buffer: and it holds the user's note (60), not a captured one" "1" "$(grep -E '^capbuf' <<<"$out" | sed -n 1p | grep -c 'note 60')"

# 3. the record options, the same as the stock live record writes: velocity (Velocity), micro move (Micro timing), both
for ro in 0 1 2 3; do
  out=$(run "ropt$ro" "recopt $ro; capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 80 100; midioff 60 0 200; tap F4; wait 100; stepv 0 0; stepv 0 1")
  case $ro in
    0) want="stepv 0 0 vol -1 micro -1|stepv 0 1 vol -1 micro -1" ;;
    1) want="stepv 0 0 vol -1 micro 25|stepv 0 1 vol -1 micro 12" ;;
    2) want="stepv 0 0 vol 49 micro -1|stepv 0 1 vol -1 micro -1" ;;
    3) want="stepv 0 0 vol 49 micro 25|stepv 0 1 vol -1 micro 12" ;;
  esac
  expect "record option $ro: volume and micro move of the note and of its OFF" "$want" "$(grep '^stepv' <<<"$out" | sed -E 's/ move / micro /' | paste -sd'|' -)"
done
# the stock live record with the same note and velocity writes the same volume (Rec + Play, one armed track)
out=$(run stockvel "recopt 2; instr 64; arm 0; audio 1; tap Play; wait 100; hold Rec; tap Play; unhold Rec; wait 300; midion 60 80 100; midioff 60 0 200; wait 300; tap Play; wait 100; $(psteps 0)")
stock=$(grep -A1 'note 36 instr 64' <<<"$out" | grep '^stepv' | head -1 | sed 's/^stepv [0-9]* [0-9]* //')
expect "velocity: the capture writes the volume of the stock record" "vol 49 move -1" "$stock"

# 4. a chord on a synth instrument: C E G is maj (the bass is the step note, the chord goes into FX 1), the OFF after the last release
out=$(run chord "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 10; midion 64 100 10; midion 67 100 100; midioff 60 0 20; midioff 64 0 20; midioff 67 0 200; capring; tap F4; wait 100; $(shot capture-chord); $(psteps 0)")
expect "chord: C E G is one step with maj in FX 1, the OFF at the last release (160 ms after the first note: step 2)" "0:36:64:14:5:0:0 2:-4:0:0:0:0:0" "$(grep '^step ' <<<"$out" | notes)"
# the first note is released before the next one starts: no join (the stock record needs the first still held), two notes
out=$(run legato "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 10; midioff 60 0 300; midion 64 100 10; midioff 64 0 200; capring; tap F4; wait 100; $(psteps 0)")
expect "chord: a released first note does not join, two single notes with their OFFs (an OFF in the step of its onset goes one step on)" "0:36:64:0:0:0:0 1:-4:0:0:0:0:0 3:40:64:0:0:0:0 4:-4:0:0:0:0:0" "$(grep '^step ' <<<"$out" | notes)"
# notes 120 ms apart are past the join window of 100 ms, even with the first still held
out=$(run late "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 120; midion 64 100 300; midioff 60 0 20; midioff 64 0 200; tap F4; wait 100; $(psteps 0)")
expect "chord: 120 ms apart is two notes, no chord FX" "0" "$(grep -c 'fx0 14:' <<<"$out")"
# the pattern step holds a chord and another effect from before: the captured note wins its step (its own chord and strum go), the other effect stays
out=$(run busy "capture 1; instr 64; setstep 0 0 -1 0 4 50 3 2; audio 1; tap Play; wait 300; midion 60 100 10; midion 64 100 10; midion 67 100 100; midioff 60 0 20; midioff 64 0 20; midioff 67 0 200; tap F4; wait 100; $(shot capture-busy); step 0 0")
expect "chord: the step held two other effects (slot 0 and 1): the matcher needs a free slot, none: the first note alone, the effects stay" "step 0 0 note 36 instr 64 fx0 4:50 fx1 3:2" "$out"
# no chord in the table (C C# D): the first note alone
out=$(run nochord "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 10; midion 61 100 10; midion 62 100 100; midioff 60 0 20; midioff 61 0 20; midioff 62 0 200; tap F4; wait 100; step 0 0")
expect "chord: no match, the first note alone" "step 0 0 note 36 instr 64 fx0 0:0 fx1 0:0" "$out"

# 5. a chord on sample instruments: one note per track, the following sample tracks (a track that holds a note of its own at the step is skipped),
#    the surplus is dropped. A second press writes the same tracks again (the note it finds there is its own)
out=$(run sample "capture 1; instr 0; setstep 1 0 50 0 0 0 0 0; audio 1; tap Play; wait 300; midion 60 100 10; midion 64 100 10; midion 67 100 100; midioff 60 0 20; midioff 64 0 20; midioff 67 0 200; tap F4; wait 100; $(shot capture-sample); step 0 0; step 1 0; step 2 0; step 3 0; step 4 0; tap F4; wait 100; step 0 0; step 1 0; step 2 0; step 3 0; step 4 0")
expect "sample chord: track 0 the first note, the note of the pattern on track 1 is skipped and stays, tracks 2 and 3 the other two, nothing on track 4, a second press changes nothing" \
  "step 0 0 note 36 instr 0 fx0 0:0 fx1 0:0|step 1 0 note 50 instr 0 fx0 0:0 fx1 0:0|step 2 0 note 40 instr 0 fx0 0:0 fx1 0:0|step 3 0 note 43 instr 0 fx0 0:0 fx1 0:0|step 4 0 note -1 instr 0 fx0 0:0 fx1 0:0" \
  "$(grep '^step ' <<<"$out" | sed -n 1,5p | paste -sd'|' -)"
expect "sample chord: the second press (the second 5 lines) is the same" "$(grep '^step ' <<<"$out" | sed -n 1,5p | paste -sd'|' -)" "$(grep '^step ' <<<"$out" | sed -n 6,10p | paste -sd'|' -)"
out=$(run surplus "capture 1; instr 0; arm 6; audio 1; tap Play; wait 300; midion 60 100 10; midion 62 100 10; midion 64 100 10; midion 65 100 100; midioff 60 0 20; midioff 62 0 20; midioff 64 0 20; midioff 65 0 200; tap F4; wait 100; $(shot capture-surplus); step 5 0; step 6 0; step 7 0; step 8 0")
expect "sample chord: armed track 6 takes two notes, the other two are dropped (no wrap to the MIDI tracks)" "step 5 0 note -1 instr 0 fx0 0:0 fx1 0:0|step 6 0 note 36 instr 0 fx0 0:0 fx1 0:0|step 7 0 note 38 instr 0 fx0 0:0 fx1 0:0|step 8 0 note -1 instr 0 fx0 0:0 fx1 0:0" "$(grep '^step ' <<<"$out" | paste -sd'|' -)"

# 6. the captured note wins its step on the destination track, nothing else of the pattern changes: other tracks, other steps and the effects of the
#    steps keep what they had. The OFF only goes to an empty step
out=$(run collide "capture 1; instr 64; setstep 0 0 50 5 4 24 0 0; setstep 1 0 52 6 0 0 0 0; setstep 0 9 55 7 0 0 0 0; setstep 0 1 57 8 0 0 0 0; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap F4; wait 100; step 0 0; step 1 0; step 0 9; step 0 1")
expect "collision: the captured note wins step 0 of track 0 (its instrument) and the effect of the step stays, track 1, the other steps and the note under the OFF (step 1) are untouched, no OFF there" \
  "step 0 0 note 36 instr 64 fx0 4:24 fx1 0:0|step 1 0 note 52 instr 6 fx0 0:0 fx1 0:0|step 0 9 note 55 instr 7 fx0 0:0 fx1 0:0|step 0 1 note 57 instr 8 fx0 0:0 fx1 0:0" "$(paste -sd'|' - <<<"$out")"
# the armed track is the destination when exactly one is armed
out=$(run armed "capture 1; instr 64; arm 5; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap F4; wait 100; step 0 0; step 5 0")
expect "destination: the only armed track" "step 0 0 note -1 instr 0 fx0 0:0 fx1 0:0|step 5 0 note 36 instr 64 fx0 0:0 fx1 0:0" "$(paste -sd'|' - <<<"$out")"
# the cursor track when nothing is armed
out=$(run cursortrack "capture 1; instr 64; tap Right 3; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap F4; wait 100; step 0 0; step 3 0")
expect "destination: no armed track, the cursor track" "step 0 0 note -1 instr 0 fx0 0:0 fx1 0:0|step 3 0 note 36 instr 64 fx0 0:0 fx1 0:0" "$(paste -sd'|' - <<<"$out")"
# the track the first note came in on (track channel MIDI, decision D7) when nothing is armed
out=$(run hinttrack "capture 1; instr 0; trackch 3 5; audio 1; tap Play; wait 300; midich 5 60 100; midichoff 5 60; tap F4; wait 100; step 0 0; step 3 0")
expect "destination: no armed track, the notes came in on the listening channel of track 4: they land on track 4, not on the cursor track" "step 0 0 note -1 instr 0 fx0 0:0 fx1 0:0|step 3 0 note 36 instr 0 fx0 0:0 fx1 0:0" "$(paste -sd'|' - <<<"$out")"

# 7. the whole take is written, not only the last pass: two notes 2.9 s apart (step 25) are both in a pattern of 32 steps. A note past the end of the
#    pattern spills (section 7b): a note 4.4 s after the first one is step 38, step 6 of a new pattern put into the song after this one
out=$(run longtake "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 2800; midion 62 100 100; midioff 62 0 100; tap F4; wait 100; $(psteps 0)")
expect "take longer than a pass: the old note is not dropped (the window is the take), both are written, the second on step 25" "0:36:64:0:0:0:0 1:-4:0:0:0:0:0 25:38:64:0:0:0:0 26:-4:0:0:0:0:0" "$(grep '^step ' <<<"$out" | notes)"
out=$(run pastend "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 4300; midion 62 100 100; midioff 62 0 100; tap F4; wait 100; fx34popup; songlist; patfile 2 0; $(psteps 0)")
expect "take past the end of the pattern: the pattern on screen holds only the notes up to step 31" "0:36:64:0:0:0:0 1:-4:0:0:0:0:0" "$(grep '^step ' <<<"$out" | notes)"
expect "spill: the note past step 31 (and its OFF) is in pattern 2, put into the song right after pattern 1, the popup counts the patterns" "songlist pos 0 1 2|patfile 2 0 bits 1:0 len 32 6:38 7:-4|fx34popup 'Captured 2 notes' '2 patterns'" "$(grep -E '^(songlist|patfile|fx34popup)' <<<"$out" | sort -r | paste -sd'|' -)"
# the length of a pass is the length the track plays (Sequencer::getTrackLastStep, polymeter), not the raw stored byte: an unmarked pattern from an old
# file can hold a stale length in a track other than 1 (rawlen seeds 3 into track 3), which the player ignores. A note 1 s after the first one is step 9
# and must be written, with the raw byte the track would be 4 steps long
out=$(run stalelen "capture 1; instr 64; audio 1; arm 2; rawlen 2 3; tap Play; wait 300; midion 60 100 100; midioff 60 0 1000; midion 62 100 100; midioff 62 0 100; tap F4; wait 100; $(psteps 2)")
expect "stale stored track length: the length is the track's played length: the note 1 s later is written on track 3" "1" "$(grep -E '^step ' <<<"$out" | notes | tr ' ' '\n' | grep -c ':38:64:')"

# 7b. the spill (mtCapture.h capspill): a take longer than the pattern fills it from step 1, the rest goes into new empty patterns put into the song
#     right after the slot of the pattern, 16 patterns at most (the rest left out and counted), one Undo takes the notes, the new patterns and their
#     song slots back, and there is no Redo of it. Pattern numbers come from the first ones nobody uses (no notes, not on screen, not in the song).
# 16 patterns of 4 steps (7.4 s): the second note (3.1 s after the first, step 27) lands in the 7th pattern, the third (8.2 s, step 71) is left out
out=$(run spillcap "capture 1; instr 64; audio 1; plen 4; tap Play; wait 300; midion 60 100 100; midioff 60 0 3000; midion 62 100 100; midioff 62 0 5000; midion 64 100 100; midioff 64 0 100; tap F4; wait 100; fx34popup; songlist; patfile 7 0; patfile 17 0")
expect "spill cap: 16 patterns (1 and 15 new ones in song order), the note past the 16th is left out and counted, no 17th pattern" "fx34popup 'Captured 2 notes' '16 patterns, 1 left out'|songlist pos 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16|patfile 7 0 bits 1:0 len 4 3:38|patfile 17 0 bits 0:0 none" "$(grep -E '^(songlist|patfile|fx34popup)' <<<"$out" | paste -sd'|' -)"
# song order: the song is 1 7 3, so the free pattern numbers are 2 and 4 (3 and 7 are in the song) and the new ones go between 1 and 7. Undo (red
# mode, button 6) puts the song back to 1 7 3, removes the files of 2 and 4 and empties pattern 1. Redo (button 7) then brings nothing back
SPILL="midion 60 100 100; midioff 60 0 4300; midion 62 100 100; midioff 62 0 6000; midion 64 100 100; midioff 64 0 100; tap F4; wait 100"
out=$(run spillundo "capture 1; instr 64; playlist 1 7 3; audio 1; tap Play; wait 300; $SPILL; songlist; patfile 2 0; patfile 4 0; $EDIT; tap F6; wait 600; songlist; patfile 2 0; patfile 4 0; $(psteps 0); tap F7; wait 600; songlist; patfile 2 0; $(psteps 0)")
sl=$(grep -E '^(songlist|patfile)' <<<"$out" | paste -sd'|' -)
st=$(grep '^step ' <<<"$out")
expect "spill order: the new patterns 2 and 4 sit between 1 and 7, in time order" "songlist pos 0 1 2 4 7 3|patfile 2 0 bits 1:0 len 32 6:38 7:-4|patfile 4 0 bits 1:0 len 32 27:40 28:-4" "$(cut -d'|' -f1-3 <<<"$sl")"
expect "spill undo: one Undo gives the song its order back (1 7 3), the new pattern files are gone and their pattern bits clear, pattern 1 is empty again" "songlist pos 0 1 7 3|patfile 2 0 bits 0:0 none|patfile 4 0 bits 0:0 none|" "$(cut -d'|' -f4-6 <<<"$sl")|$(sed -n 1,32p <<<"$st" | notes)"
expect "spill redo: Redo after the spill's Undo brings neither the notes nor the patterns back" "songlist pos 0 1 7 3|patfile 2 0 bits 0:0 none|" "$(cut -d'|' -f7-8 <<<"$sl")|$(sed -n 33,64p <<<"$st" | notes)"
# not in the song: the pattern on screen (4) has no slot in the song (1), so there is no place for new patterns: the rest is left out as before
out=$(run spillnosong "capture 1; instr 64; pattern 4; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 4300; midion 62 100 100; midioff 62 0 100; tap F4; wait 100; fx34popup; songlist")
expect "spill: a pattern with no slot in the song does not spill, the note past it is left out and counted" "fx34popup 'Captured 1 note' '1 left out'|songlist pos 0 1" "$(grep -E '^(songlist|fx34popup)' <<<"$out" | paste -sd'|' -)"
# a Part plays (the stock "no undo while a Part plays" case: no Undo snapshot, so no spill either): the press writes what fits, the rest is left out
out=$(run spillparts "capture 1; instr 64; pattern 1; plen 4; setstep 1 0 60 5 0 0 0 0; dirty; wait 300; tap Song; wait 300; hold Shift; tap F8; unhold Shift; wait 200; tap Rec; wait 100; tap Insert; wait 100; tap Rec; wait 200; partset 0 rep 30; tap Up 3; audio 1; tap F2; wait 300; tap Pattern; wait 300; midion 60 100 100; midioff 60 0 1000; midion 62 100 100; midioff 62 0 100; tap F4; wait 100; fx34popup; songlist; patfile 2 0; parts")
expect "spill while a Part plays: no new pattern, the song unchanged, the note past the pattern left out" "fx34popup 'Captured 1 note' '1 left out'|songlist pos 0 1|patfile 2 0 bits 0:0 none|1" "$(grep -E '^(songlist|patfile|fx34popup)' <<<"$out" | paste -sd'|' -)|$(grep -c '^parts count .* playing 1 ' <<<"$out")"
# a press in another pattern writes the whole take again from there: the song 1 9, a press in 1 puts 2 after it, a press in 9 puts 3 after 9
out=$(run spillelse "capture 1; instr 64; playlist 1 9; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 4300; midion 62 100 100; midioff 62 0 100; tap F4; wait 100; pattern 9; wait 300; tap F4; wait 100; songlist; patfile 1 0; patfile 2 0; patfile 3 0; $(psteps 0)")
expect "spill re-press: the press in pattern 9 lands the take from its step 1 and spills into a new pattern 3 after 9, the first press's patterns stay" "songlist pos 2 1 2 9 3|patfile 1 0 bits 1:0 len 32 0:36 1:-4|patfile 2 0 bits 1:0 len 32 6:38 7:-4|patfile 3 0 bits 1:0 len 32 6:38 7:-4|0:36:64:0:0:0:0 1:-4:0:0:0:0:0" "$(grep -E '^(songlist|patfile)' <<<"$out" | paste -sd'|' -)|$(grep '^step ' <<<"$out" | notes)"

# the same pattern again: a second press reuses the first block (pattern numbers and song slots stay), it never inserts a second one. A press with
# more notes (a further note 6 s later, step 91, in a third pattern) grows the block by the one pattern it needs. One Undo takes the press back
RP1="midion 60 100 100; midioff 60 0 4300; midion 62 100 100; midioff 62 0 100; tap F4; wait 100"
out=$(run spillsame "capture 1; instr 64; audio 1; tap Play; wait 300; $RP1; tap F4; wait 100; fx34popup; songlist; patfile 2 0; patfile 3 0; $(psteps 0)")
expect "spill same pattern: the second press on pattern 1 rewrites pattern 1 and the first block, no second block (song 1 2, no pattern 3)" "songlist pos 0 1 2|patfile 2 0 bits 1:0 len 32 6:38 7:-4|patfile 3 0 bits 0:0 none|0:36:64:0:0:0:0 1:-4:0:0:0:0:0" "$(grep -E '^(songlist|patfile)' <<<"$out" | paste -sd'|' -)|$(grep '^step ' <<<"$out" | notes)"
expect "spill same pattern: the popup of the second press still counts the 2 patterns" "fx34popup 'Captured 2 notes' '2 patterns'" "$(grep '^fx34popup' <<<"$out")"
out=$(run spillgrow "capture 1; instr 64; audio 1; tap Play; wait 300; $RP1; wait 6000; midion 64 100 100; midioff 64 0 100; tap F4; wait 100; songlist; patfile 2 0; patfile 3 0; $EDIT; tap F6; wait 600; songlist; patfile 2 0; patfile 3 0")
expect "spill same pattern, longer take: the block grows by the pattern it needs (song 1 2 3), pattern 2 keeps its notes, Undo takes the whole press back (song 1, both files gone)" "songlist pos 0 1 2 3|patfile 2 0 bits 1:0 len 32 6:38 7:-4|patfile 3 0 bits 1:0 len 32 30:40 31:-4|songlist pos 0 1|patfile 2 0 bits 0:0 none|patfile 3 0 bits 0:0 none" "$(grep -E '^(songlist|patfile)' <<<"$out" | paste -sd'|' -)"

# the spill's file writes are paced (capspill::save serves the sample streams after a slow pass, mtSequencer_capture.cpp): the host counts every write
# as a slow one with "spillpace 1" (the 2 new patterns of this take), the files must all be there, the song in order
out=$(run spillpace "capture 1; instr 64; playlist 1 7 3; spillpace 1; audio 1; tap Play; wait 300; $SPILL; spillpace; songlist; patfile 2 0; patfile 4 0")
expect "spill pace: a pass that took long serves the streams before the next file (2 new files, 2 paced passes), every file and the song are complete" "spillpace 2|songlist pos 0 1 2 4 7 3|patfile 2 0 bits 1:0 len 32 6:38 7:-4|patfile 4 0 bits 1:0 len 32 27:40 28:-4" "$(grep -E '^(spillpace|songlist|patfile)' <<<"$out" | paste -sd'|' -)"
# D9: a same-pattern re-press needs two Undos, not three: the first takes the press (and the block) back, pattern 1 keeps the first press's notes
# (the block is the one thing Undo restores of a re-press), the second takes the first press back. Read from the pattern in RAM (the file follows
# with the autosave, up to 10 s later)
out=$(run spillrepundo "capture 1; instr 64; audio 1; tap Play; wait 300; $RP1; tap F4; wait 100; songlist; $EDIT; tap F6; wait 600; songlist; $(psteps 0); tap F6; wait 601; songlist; $(psteps 0)")
sl=$(grep -E '^songlist' <<<"$out" | paste -sd'|' -)
st=$(grep '^step ' <<<"$out")
expect "re-press undo: press and re-press make song 1 2, the first Undo takes the block back (song 1)" "songlist pos 0 1 2|songlist pos 0 1|songlist pos 0 1" "$sl"
expect "re-press undo: the first Undo leaves pattern 1 with the notes of press 1, the second Undo empties it (two Undos, no dead step)" "0:36:64:0:0:0:0 1:-4:0:0:0:0:0|" "$(sed -n 1,32p <<<"$st" | notes)|$(sed -n 33,64p <<<"$st" | notes)"

# a note off across a pattern boundary (7c): 62 starts at step 27 in pattern 1, 64 at step 36 in pattern 2, 66 at step 71 in pattern 3. 62 is released at
# step 72 (two patterns on), 64 at step 37, 66 at step 73: each OFF is written in the pattern it falls in
out=$(run spilloff "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 3000; midion 62 100 1000; midion 64 100 100; midioff 64 0 4000; midion 66 100 100; midioff 62 0 100; midioff 66 0 100; tap F4; wait 100; songlist; patfile 2 0; patfile 3 0; $(psteps 0)")
expect "spill note off across: the OFFs of the notes of patterns 1 and 2 that fall in a later pattern are written there (pattern 3 gets the OFF of 62 two patterns on), no note rings on" "songlist pos 0 1 2 3|patfile 2 0 bits 1:0 len 32 4:40 5:-4|patfile 3 0 bits 1:0 len 32 7:42 8:-4 9:-4|0:36:64:0:0:0:0 1:-4:0:0:0:0:0 27:38:64:0:0:0:0" "$(grep -E '^(songlist|patfile)' <<<"$out" | paste -sd'|' -)|$(grep '^step ' <<<"$out" | notes)"
# a note still sounding past the last pattern of the press ends at the end of that pattern: 62 at step 27, released at step 38 of a take with no pattern 2
out=$(run spillend "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 3000; midion 62 100 1300; midioff 62 0 100; tap F4; wait 100; songlist; $(psteps 0)")
expect "note off past the last pattern: the OFF lands on the last step of the last pattern" "songlist pos 0 1|0:36:64:0:0:0:0 1:-4:0:0:0:0:0 27:38:64:0:0:0:0 31:-4:0:0:0:0:0" "$(grep -E '^songlist' <<<"$out" | paste -sd'|' -)|$(grep '^step ' <<<"$out" | notes)"

# 8. where the ring is fed and where the button works: stopped, Rec, red mode, another screen
out=$(run stopped "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; tap Play; wait 300; tap F4; wait 100; $(psteps 0); capring")
expect "stopped: the press does nothing (it needs playback), the events stay" "|capring count 2" "$(grep '^step ' <<<"$out" | notes)|$(grep '^capring' <<<"$out")"
out=$(run notplayed "capture 1; instr 64; audio 1; midion 60 100 100; midioff 60 0 200; capring")
expect "stopped: notes played while the transport is stopped are kept, flagged" "capring count 2" "$(grep '^capring' <<<"$out")"
out=$(run notplayed2 "capture 1; instr 64; audio 1; midion 60 100 100; midioff 60 0 200; tap Play; wait 300; tap F4; wait 100; $(psteps 0)")
expect "stopped notes are part of the take: a press once the transport runs writes them (the first on step 0)" "0:36:64:0:0:0:0 1:-4:0:0:0:0:0" "$(grep '^step ' <<<"$out" | notes)"
out=$(run rec "capture 1; instr 64; arm 0; audio 1; tap Play; wait 100; hold Rec; tap Play; unhold Rec; wait 300; midion 60 100 100; midioff 60 0 200; capring; tap F4; wait 100; tap Play")
expect "Rec: the stock record writes the notes and the take keeps them too (decision D4, a Rec toggle never splits a take)" "capring count 2" "$(grep '^capring' <<<"$out")"
out=$(run red "capture 1; instr 64; tap Rec; midion 60 100 100; midioff 60 0 200; padon 0 100; padoff 0 100; capring")
expect "red mode (step entry): the notes are kept (decision D2, every played note)" "capring count 4" "$(grep '^capring' <<<"$out")"
# (a sample instrument: the pad board of the Master and Song screens reads mtProject.instrument[64] for a synth one, a stock bug the SAN build reports)
out=$(run screen "capture 1; instr 0; audio 1; tap Play; wait 300; tap Master; wait 400; midion 60 100 100; midioff 60 0 200; capring; tap Pattern; wait 200; midion 62 100 100; midioff 62 0 200; capring")
expect "another screen: kept there too, and back in the pattern editor" "capring count 2|capring count 4" "$(grep '^capring' <<<"$out" | paste -sd'|' -)"

# 10. the ring wraps at 1,024 events and keeps the newest: 520 on/off pairs, then a last note. The reader stays 8 events behind the head
loop=""; for i in $(seq 1 520); do loop="$loop midion 61 100 0; midioff 61 0 0;"; done
out=$(run wrap "capture 1; instr 64; audio 1; tap Play; wait 300; $loop midion 62 100 100; midioff 62 0 200; capring; tap F4; wait 100; $(psteps 0)")
expect "ring: 1,042 events in, 1,016 readable (the reader stays 8 behind the head of the 1,024 ring)" "capring count 1016" "$(grep '^capring' <<<"$out")"
got=$(grep '^step ' <<<"$out" | notes | tr ' ' '\n' | awk -F: '{print $2}' | sort -u | paste -sd' ' -)
expect "wrap: the last note wins the step of the 520 earlier ones (38, no 37 left), one OFF behind it" "-4 38" "$got"
expect "wrap: exactly two steps in the pattern" "2" "$(grep '^step ' <<<"$out" | notes | wc -w | tr -d ' ')"

# 11. the feature switched on later
out=$(run flag "capture 0; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 200; capring; capture 1; midion 60 100 100; midioff 60 0 200; capring")
expect "feature switched on later: the ring starts then" "capring count 0|capring count 2" "$(grep '^capring' <<<"$out" | paste -sd'|' -)"

# 12. the indicator in the title bar (TitleBar::setCaptureState, polled every 60 ms by Interface::commonThingsUpdate on every screen). States: 0 hidden
#     (feature off), 1 empty (hollow: no take, or all of it used), 2 stale (a take the Capture would not land now: older than one pass, transport
#     stopped), 3 ready (a Capture takes notes now), 4 past the gap (green outline: the next note starts a new take). "draws" counts the redraws:
#     the square is redrawn only when the state changes. "take" is the take id, "ring" the events of the take, "seq" every event ever written
PHRASE="midion 60 100 100; midioff 60 0 200"   # a note, 100 ms held, then 200 ms of quiet
out=$(run indoff "instr 64; audio 1; tap Play; wait 300; $PHRASE; wait 300; capind; $(gshot ind-off)")
expect "indicator: the feature is off, nothing is drawn and nothing is written" "capind state 0 draws 0 take 0 ring 0 seq 0" "$(grep '^capind' <<<"$out")"
out=$(run indempty "capture 1; wait 300; capind; $(gshot ind-empty)")
expect "indicator: on, no take, hollow square" "capind state 1 draws 1 take 0 ring 0 seq 0" "$(grep '^capind' <<<"$out")"
out=$(run indready "capture 1; instr 64; audio 1; tap Play; wait 300; $PHRASE; wait 300; capind; $(gshot ind-ready); wait 500; capind")
expect "indicator: notes played in the running pattern, ready (filled), and no redraw while it stays so" "capind state 3 draws 2 take 1 ring 2 seq 2|capind state 3 draws 2 take 1 ring 2 seq 2" "$(grep '^capind' <<<"$out" | paste -sd'|' -)"
out=$(run indstale "capture 1; instr 64; audio 1; tap Play; wait 300; $PHRASE; tap Play; wait 400; capind; $(gshot ind-stale)")
expect "indicator: the transport stopped, the take is kept but a Capture would not take it: stale" "capind state 2 draws 3 take 1 ring 2 seq 2" "$(grep '^capind' <<<"$out")"
out=$(run indcap "capture 1; instr 64; audio 1; tap Play; wait 300; $PHRASE; wait 100; capind; tap F4; wait 300; capind; capring")
expect "indicator: a press does not use the take up, it stays ready (no redraw, the 2 events still in the ring)" "capind state 3 draws 2 take 1 ring 2 seq 2|capind state 3 draws 2 take 1 ring 2 seq 2|capring count 2" "$(grep -E '^capind|^capring' <<<"$out" | paste -sd'|' -)"
# the long press of button 3 (the first hold event, 333 ms on the unit) ends the take and captures nothing, also not at the release
out=$(run indclear "capture 1; instr 64; audio 1; tap Play; wait 300; $PHRASE; wait 100; capsel; capind; hold F4; wait 350; repeat F4 1; wait 100; $(gshot ind-cleared); repeat F4 2; unhold F4; wait 400; capind; capsel; capbuf 0 3; $(gshot ind-after-clear)")
expect "long press: the take ends, the square is hollow, the buffer and its selection are untouched (nothing captured at the press, the hold or the release)" \
  "capsel 0 0 0 0|capind state 3 draws 2 take 1 ring 2 seq 2|capind state 1 draws 3 take 1 ring 0 seq 2|capsel 0 0 0 0|capbuf 0 3 note -1 instr 0 fx0 0:0 fx1 0:0 vol -1 micro -1" "$(grep -E '^(capsel|capind|capbuf)' <<<"$out" | paste -sd'|' -)"
out=$(run indclear2 "capture 1; instr 64; audio 1; tap Play; wait 300; $PHRASE; hold F4; repeat F4 1; unhold F4; wait 200; $PHRASE; wait 100; capind; tap F4; wait 300; capsel; $(psteps 0)")
expect "long press: notes played after the clear start take 2 and are captured by a short press, the phrase before the clear is not in it" \
  "capind state 3 draws 4 take 2 ring 2 seq 4|capsel 0 0 0 0|1 note, 1 OFF" \
  "$(grep -E '^(capind|capsel)' <<<"$out" | paste -sd'|' -)|$(grep '^step ' <<<"$out" | notes | tr ' ' '\n' | grep -c ':36:64:') note, $(grep '^step ' <<<"$out" | notes | tr ' ' '\n' | grep -c ':-4:') OFF"
# the feature off: no square, and the button has no long press (the take is left alone)
out=$(run indnoshortcut "capture 1; instr 64; audio 1; tap Play; wait 300; $PHRASE; capture 0; hold F4; repeat F4 1; unhold F4; wait 300; capind; capring")
expect "feature off again: the square is hidden and a long press of button 3 clears nothing" "capind state 0 draws 3 take 1 ring 2 seq 2|capring count 2" "$(grep -E '^(capind|capring)' <<<"$out" | paste -sd'|' -)"
# the square is in the shared title bar: another screen shows it too (a sample instrument: the Master pad board reads mtProject.instrument[64] for a synth one)
out=$(run indmaster "capture 1; instr 0; audio 1; tap Play; wait 300; $PHRASE; tap Master; wait 400; capind; $(gshot ind-master); tap Pattern; wait 300; capind")
expect "indicator: the Master screen shows the same state (ready), and the pattern editor again, without redraws in between" "capind state 3 draws 2 take 1 ring 2 seq 2|capind state 3 draws 2 take 1 ring 2 seq 2" "$(grep '^capind' <<<"$out" | paste -sd'|' -)"

# 13. the feed: every played note on every screen (decision D2). Pads on the Master screen, MIDI on a track's own listening channel (with the
#     track as the destination hint), the Synth play page, DIN. Not: Notes in Off, an instrument pick
out=$(run feedmaster "capture 1; instr 0; audio 1; tap Master; wait 400; padon 0 100; padoff 0 100; capring")
expect "feed: a pad on the Master screen is kept (source 0)" "capring count 2|cap 72 on v127 src 0 dest 15" "$(grep '^capring' <<<"$out")|$(grep '^cap ' <<<"$out" | head -1 | awk '{print $1, $2, $3, $4, $15, $16, $17, $18}')"
out=$(run feedtrack "capture 1; instr 0; audio 1; trackch 3 5; midich 5 60 100; midichoff 5 60; capring")
expect "feed: MIDI on the listening channel of track 4 (handleTrackMidiNoteOn, not through the sequencer) is kept, source 4, destination 3" \
  "capring count 2|cap 36 on src 4 dest 3|cap 36 off src 4 dest 3" "$(grep '^capring' <<<"$out")|$(grep '^cap ' <<<"$out" | awk '{print $1, $2, $3, $15, $16, $17, $18}' | paste -sd'|' -)"
out=$(run feeddin "capture 1; instr 64; audio 1; notesin 3; dinon 1 60 90; dinoff 1 60; capring")
expect "feed: DIN MIDI is kept with source 1" "capring count 2|cap 36 on v90 src 1" "$(grep '^capring' <<<"$out")|$(grep '^cap ' <<<"$out" | head -1 | awk '{print $1, $2, $3, $4, $15, $16}')"
out=$(run feednotesoff "capture 1; instr 64; audio 1; notesin 0; dinon 1 60 90; dinoff 1 60; midich 1 62 90; midichoff 1 62; capring")
expect "feed: Notes in Off, the Tracker ignores the notes and so does Capture" "capring count 0" "$(grep '^capring' <<<"$out")"
out=$(run feedsynth "capture 1; instr 64; audio 1; hold Shift; tap Performance; unhold Shift; wait 300; midiono 1 60 100; wait 200; midionoff 1 60; wait 200; capring")
expect "feed: the Synth play page (synthplay::noteOn, not through the sequencer) is kept with source 6 (USB)" "capring count 2|cap 36 on src 6" "$(grep '^capring' <<<"$out")|$(grep '^cap ' <<<"$out" | head -1 | awk '{print $1, $2, $3, $15, $16}')"

# 14. the gap (decision D3, 30 s fixed): measured from the moment no note is held to the next note on. The old take is never emptied by time
G1="midion 60 100 100; midioff 60 0 100"
out=$(run gap29 "capture 1; instr 64; audio 1; tap Play; wait 300; $G1; wait 28800; capind; midion 62 100 100; midioff 62 0 200; capind")
expect "gap: a note 29 s after the release is the same take (take 1, 4 events), the take stayed ready the whole time" "capind state 3 draws 2 take 1 ring 2 seq 2|capind state 3 draws 2 take 1 ring 4 seq 4" "$(grep '^capind' <<<"$out" | paste -sd'|' -)"
out=$(run gap31 "capture 1; instr 64; audio 1; tap Play; wait 300; $G1; wait 30900; capind; $(gshot ind-gap); midion 62 100 100; midioff 62 0 200; capind")
expect "gap: at 31 s the square is the green outline (4), the take is still there, the next note starts take 2 with only its own events" \
  "capind state 4 draws 3 take 1 ring 2 seq 2|capind state 3 draws 4 take 2 ring 2 seq 4" "$(grep '^capind' <<<"$out" | paste -sd'|' -)"
out=$(run gaphold "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 60000; midioff 60 0 28800; midion 62 100 100; midioff 62 0 200; capind")
expect "gap: a note held 60 s does not start a take, a note 29 s after its release is the same take" "capind state 3 take 1 ring 4 seq 4" "$(grep '^capind' <<<"$out" | sed -E 's/ draws [0-9]+//')"
out=$(run gapold "capture 1; instr 64; audio 1; tap Play; wait 300; $G1; wait 300000; capind; capring")
expect "gap: 5 min later with no new note the take is still there (outline), nothing was emptied" "capind state 4 draws 3 take 1 ring 2 seq 2|capring count 2" "$(grep -E '^(capind|capring)' <<<"$out" | paste -sd'|' -)"
out=$(run gaplost "capture 1; instr 64; audio 1; tap Play; wait 300; midion 60 100 100000; midion 62 100 100; midioff 62 0 200; capind; wait 121000; midion 64 100 100; capind")
expect "gap: a held note (lost note off) keeps the take at 100 s, a note 121 s after the last event starts take 2 (the 120 s rule)" \
  "capind state 3 take 1 ring 3 seq 3|capind state 3 take 2 ring 1 seq 4" "$(grep '^capind' <<<"$out" | sed -E 's/ draws [0-9]+//' | paste -sd'|' -)"
out=$(run gapcap "capture 1; instr 64; audio 1; tap Play; wait 300; $G1; wait 30900; midion 62 100 100; midioff 62 0 200; tap F4; wait 100; $(psteps 0)")
expect "gap: a press after the gap writes only the new take (its first note on step 0)" "1:0:0:38" "$(grep '^step ' <<<"$out" | notes | tr ' ' '\n' | grep -c ':38:64:'):$(grep '^step ' <<<"$out" | notes | tr ' ' '\n' | grep -c ':36:64:'):$(grep '^step 0 0 ' <<<"$out" | notes | cut -d: -f1):$(grep '^step 0 0 ' <<<"$out" | notes | cut -d: -f2)"

for g in ind-off ind-empty ind-ready ind-stale ind-cleared ind-after-clear ind-master ind-gap; do golden "$g"; done
fi

if [ "${CAP_SKIP16:-}" != 1 ]; then   # CAP_SKIP16=1: not the take file section (the mutant runs)
# 16. the take file (phase 2, mtCapture.h namespace capfile, core/source/mtSequencer_captureFile.cpp): /Capture/take.cap on the card, the writer and the
#     reader of the second Capture tenant of extras.plg (linked in on this host). capfile_check.py decodes the file on its own (its own CRC-8 and layout),
#     so the firmware reader does not check the firmware writer alone. "capfile" prints the writer's counters, "capread [all]" the firmware reader,
#     "capgen N PER_TICK" N events through the real tap, PER_TICK per 60 ms tick (a script of midion lines cannot hold 40,000), "fsbusy 1" a sample stream
#     reading the card (isAsyncReadBusy), "fmstatus N" the file manager busy, "ftmode on|off" the USB storage mode (the card lent). A card folder with a
#     whole file (524,800 B of zeros) is made first where the test is not about the creation (the creation takes 129 ticks of 8 sectors)
CAPF=524800
cfmk() { mkdir -p "$WORK/$1/Capture"; head -c $CAPF /dev/zero > "$WORK/$1/Capture/take.cap"; }
cfpy() { python3 -I "$HERE/capfile_check.py" "$WORK/$1/Capture/take.cap" "${@:2}"; }
cfsize() { stat -f %z "$WORK/$1/Capture/take.cap" 2>/dev/null || stat -c %s "$WORK/$1/Capture/take.cap"; }
cfv() { awk -v k="$2" '{ for (i = 1; i <= NF; i++) if ($i == k) { print $(i + 1); exit } }' <<<"$1"; }   # cfv "<capfile line>" written
# the events of the firmware reader (capev) and of the python decoder (ev) in one shape: seq note on|off velocity pos
cfk() { local l="$1" k o=""; shift; for k in "$@"; do o="$o$k $(cfv "$l" "$k") "; done; echo "${o% }"; }   # cfk "<capfile line>" written pending -> "written 0 pending 31"
evs_cpp() { grep '^capev ' | awk '{ v = $5; sub(/^v/, "", v); print $7, $3, $4, v, $11 }'; }
evs_py() { grep '^ev ' | awk '{ print $2, $3, $4, $5, $6 }'; }
G16="capture 1; instr 64; audio 1; tap Play; wait 300"
pairs() { local n=$1 i w=${2:-20}; for i in $(seq 1 "$n"); do printf 'midion 60 100 %d; midioff 60 0 %d; ' "$w" "$w"; done; }   # 2 n events

# 16.1 the file is created at the first flush, 8 sectors per tick, hidden directory, whole size, the header and the records equal the ring
out=$(run cf1 "$G16; midion 60 100 100; midioff 60 0 200; capring; capfile; wait 1000; capfile; wait 12000; capfile; capread all")
c=$(grep '^capfile' <<<"$out")
expect "file: nothing is written to the card before the 2 s pause (no file, no directory yet) and nothing at all without a take" "capfile state 0 flushes 0 sectors 0 skips 0 written 0 lost 0 pending 2 take 0 headers 0 created 0 errors 0|capfile state 0 flushes 0 sectors 0 skips 0 written 0 lost 0 pending 2 take 0 headers 0 created 0 errors 0" "$(sed -n 1,2p <<<"$c" | paste -sd'|' -)"
expect "file: made with 129 steps of 8 zero sectors (1,025 sectors), then ready, one flush wrote the header and the 2 events" "capfile state 2 flushes 1 sectors 1 skips 0 written 2 lost 0 pending 0 take 1 headers 1 created 129 errors 0" "$(sed -n 3p <<<"$c")"
expect "file: the size is 524,800 B (512 B header + 32,768 records of 16 B)" "524800" "$(cfsize cf1)"
py=$(cfpy cf1)
expect "file: the header (python decoder): take 1, 32,768 slots, tempo 130.00, no lost event" "hdr take 1 slots 32768|bpm100 13000|lost 0 flags 0" "$(grep '^hdr' <<<"$py" | awk '{print $1, $2, $3, $4, $5}')|$(grep '^hdr' <<<"$py" | awk '{print $8, $9}')|$(grep '^hdr' <<<"$py" | awk '{print $12, $13, $14, $15}')"
expect "file: the records equal the ring (note, on|off, velocity, position), the CRC-8 and the take id are filled (python: 2 valid)" \
  "$(grep '^cap ' <<<"$out" | awk '{ v = $4; sub(/^v/, "", v); print $2, $3, v, $6 }' | paste -sd'|' -)|recs take=1 valid 2 newest 1 oldest 0 span 2" \
  "$(grep '^ev ' <<<"$py" | awk '{ print $3, $4, $5, $6 }' | paste -sd'|' -)|$(grep '^recs' <<<"$py")"
expect "file: the firmware reader agrees with the python decoder, event for event" "$(evs_py <<<"$py" | paste -sd'|' -)" "$(evs_cpp <<<"$out" | paste -sd'|' -)"
expect "file: the header is the reader's: take 1, 2 valid events, newest 1 oldest 0" "capread take 1 lost 0/0 bpm100 13000|span 2 valid 2 newest 1 oldest 0" "$(grep '^capread' <<<"$out" | awk '{print $1, $2, $3, $4, $5, $6, $7}')|$(grep '^capread' <<<"$out" | awk '{print $12, $13, $14, $15, $16, $17, $18, $19}')"
out=$(run cfoff "capture 0; instr 64; audio 1; tap Play; wait 300; midion 60 100 100; midioff 60 0 3000; capfile")
expect "file: the feature off writes nothing and creates no file" "no|capfile state 0 flushes 0 sectors 0 skips 0 written 0 lost 0 pending 0 take 0 headers 0 created 0 errors 0" "$([ -e "$WORK/cfoff/Capture" ] && echo yes || echo no)|$(grep '^capfile' <<<"$out")"

# 16.2 the flush cadence: 32 events or a 2 s pause, the partial sector rewritten when it fills, at most 4 sectors (the header counts) per tick
cfmk cad1
out=$(run cad1 "$G16; $(pairs 15) midion 62 100 20; wait 100; capfile; wait 1400; capfile; wait 800; capfile")
c=$(grep '^capfile' <<<"$out")
expect "cadence: 31 events wait (nothing written, 31 pending) and still wait 1.5 s after the last one" "written 0 pending 31|written 0 pending 31" "$(cfk "$(sed -n 1p <<<"$c")" written pending)|$(cfk "$(sed -n 2p <<<"$c")" written pending)"
expect "cadence: 2 s after the last event the partial sector goes out (31 records, header + 1 sector, one flush)" "capfile state 2 flushes 1 sectors 1 skips 0 written 31 lost 0 pending 0 take 1 headers 1 created 0 errors 0" "$(sed -n 3p <<<"$c")"
expect "cadence: python reads the 31 records" "recs take=1 valid 31 newest 30 oldest 0 span 31" "$(cfpy cad1 | grep '^recs')"
cfmk cad2
out=$(run cad2 "$G16; $(pairs 16); wait 100; capfile")
expect "cadence: the 32nd event flushes at once (a full sector, 100 ms later, not 2 s)" "capfile state 2 flushes 1 sectors 1 skips 0 written 32 lost 0 pending 0 take 1 headers 1 created 0 errors 0" "$(grep '^capfile' <<<"$out")"
cfmk cad3
out=$(run cad3 "$G16; $(pairs 15) midion 62 100 20; wait 2500; capfile; $(pairs 17 5); wait 100; capfile; wait 2500; capfile")
c=$(grep '^capfile' <<<"$out")
expect "cadence: 31 written (sector 0 partial), then 34 more events: the tick that sees 64 writes sector 0 again (its record 31) and the full sector 1 (3 sectors in all), the newest event came after that tick and waits" \
  "written 31 pending 0 sectors 1 flushes 1|written 64 pending 1 sectors 3 flushes 2" "$(cfk "$(sed -n 1p <<<"$c")" written pending sectors flushes)|$(cfk "$(sed -n 2p <<<"$c")" written pending sectors flushes)"
expect "cadence: 2 s after it the last event goes out, in sector 2 (4 sectors in all, 3 flushes)" "written 65 pending 0 sectors 4 flushes 3" "$(cfk "$(sed -n 3p <<<"$c")" written pending sectors flushes)"
expect "cadence: the 65 records are all there, in order (python), the record 31 of sector 0 that was written the second time included" "recs take=1 valid 65 newest 64 oldest 0 span 65" "$(cfpy cad3 | grep '^recs')"
# 300 events in one tick: the first tick writes the header and 3 sectors (4 sector writes), the second 4 sectors, the rest later
cfmk cad4
out=$(run cad4 "$G16; capgen 300 300; capfile; wait 400; capfile")
c=$(grep '^capfile' <<<"$out")
expect "cadence: 300 events in one tick go out at most 4 sectors per 60 ms tick (2 ticks: header + 3 sectors, then 4: 224 events), the rest at the next tick" \
  "capfile state 2 flushes 2 sectors 7 skips 0 written 224 lost 0 pending 76 take 1 headers 1 created 0 errors 0|capfile state 2 flushes 3 sectors 10 skips 0 written 300 lost 0 pending 0 take 1 headers 1 created 0 errors 0" "$(paste -sd'|' - <<<"$c")"
expect "cadence: and the file holds all 300 in order" "recs take=1 valid 300 newest 299 oldest 0 span 300" "$(cfpy cad4 | grep '^recs')"

# 16.3 busy: no flush while a sample stream reads the card, the file manager works, or the card is lent. The ring keeps the events (1,016 readable), the
#      flush catches up afterwards
cfmk busy
out=$(run busy "$G16; capgen 40 40; wait 300; capfile; fsbusy 1; capgen 100 100; wait 600; capfile; fsbusy 0; wait 200; capfile; fmstatus 9; capgen 100 100; wait 600; capfile; fmstatus 0; wait 200; capfile")
c=$(grep '^capfile' <<<"$out")
w() { awk '{ for (i = 1; i <= NF; i++) { if ($i == "written") a = $(i + 1); if ($i == "pending") p = $(i + 1); if ($i == "skips") k = $(i + 1) } print "written " a " pending " p " skips>0:" (k > 0 ? "yes" : "no") }' <<<"$1"; }
expect "busy: idle card, 40 events written" "written 40 pending 0 skips>0:no" "$(w "$(sed -n 1p <<<"$c")")"
expect "busy: a sample stream reads the card (isAsyncReadBusy): 100 events wait, nothing is written, the ticks count the skips" "written 40 pending 100 skips>0:yes" "$(w "$(sed -n 2p <<<"$c")")"
expect "busy: the stream is done: the 100 events are written" "written 140 pending 0 skips>0:yes" "$(w "$(sed -n 3p <<<"$c")")"
sk=$(cfv "$(sed -n 3p <<<"$c")" skips)
expect "busy: the file manager is loading a project (fmLoadingProjectFromProjects): 100 events wait, more skips" "written 140 pending 100 skips-grew:yes" "$(w "$(sed -n 4p <<<"$c")" | awk '{print $1, $2, $3, $4}') skips-grew:$([ "$(cfv "$(sed -n 4p <<<"$c")" skips)" -gt "$sk" ] && echo yes || echo no)"
expect "busy: the file manager is idle again: written" "written 240 pending 0 skips>0:yes" "$(w "$(sed -n 5p <<<"$c")")"
expect "busy: nothing was lost (the ring held the waiting events), all 240 in the file in order" "recs take=1 valid 240 newest 239 oldest 0 span 240" "$(cfpy busy | grep '^recs')"
# the ring lapped while the card is busy: the oldest events are lost, the header is written again with the count, the reader shows the hole.
# 40 events are written first, then 2,000 more over 20 ticks while a stream reads the card: only the newest 1,016 stay in the ring
cfmk lap
out=$(run lap "$G16; capgen 40 40; wait 300; capfile; fsbusy 1; capgen 2000 100; wait 300; capfile; fsbusy 0; wait 2500; capfile; capread")
c=$(grep '^capfile' <<<"$out")
expect "lap: 40 events written, then 2,000 while the card is busy: 984 lost (counted), 1,016 pending" "written 40 lost 0 pending 0 headers 1|written 40 lost 984 pending 1016 headers 1" "$(cfk "$(sed -n 1p <<<"$c")" written lost pending headers)|$(cfk "$(sed -n 2p <<<"$c")" written lost pending headers)"
expect "lap: the card is free again: the 1,016 are written, the header was written again with the lost count (2 in all)" "written 1056 lost 984 pending 0 headers 2" "$(cfk "$(sed -n 3p <<<"$c")" written lost pending headers)"
expect "lap: the reader: lost 984 flagged, events 0..39 and 1024..2039: span 2,040 with 1,056 valid (the hole is the lost part)" "capread take 1 lost 984/1|span 2040 valid 1056 newest 2039 oldest 0" "$(grep '^capread' <<<"$out" | awk '{print $1, $2, $3, $4, $5}')|$(grep '^capread' <<<"$out" | awk '{print $12, $13, $14, $15, $16, $17, $18, $19}')"
expect "lap: python agrees: header lost 984 flags 1, the same extent" "lost 984 flags 1|recs take=1 valid 1056 newest 2039 oldest 0 span 2040" "$(cfpy lap | grep '^hdr' | awk '{print $12, $13, $14, $15}')|$(cfpy lap | grep '^recs')"

# 16.4 USB drive mode: the pending events are flushed before the card is lent (handleFileTransferMode -> Sequencer::captureFlushNow), nothing is written
#      while it lasts, the writer carries on after it (the file is checked again, no handle was kept)
cfmk drive
out=$(run drive "$G16; $(pairs 3); capring; capfile; ftmode on; capfile; capgen 100 100; wait 600; capfile; ftmode off; wait 400; capfile")
c=$(grep '^capfile' <<<"$out")
n=$(grep '^capring' <<<"$out" | awk '{print $3}')
expect "drive: 6 events pending (under the 2 s, under 32) before the mode" "written 0 pending 6" "$(w "$(sed -n 1p <<<"$c")" | awk '{print $1, $2, $3, $4}')"
nw=$(cfv "$(sed -n 2p <<<"$c")" written)
expect "drive: entering the mode flushed them first (flushNow): all written, none pending, 1 flush" "$n|0|1" "$nw|$(cfv "$(sed -n 2p <<<"$c")" pending)|$(cfv "$(sed -n 2p <<<"$c")" flushes)"
expect "drive: in the mode 100 events are not written (skips, pending 100)" "$nw|100|yes" "$(cfv "$(sed -n 3p <<<"$c")" written)|$(cfv "$(sed -n 3p <<<"$c")" pending)|$([ "$(cfv "$(sed -n 3p <<<"$c")" skips)" -gt 0 ] && echo yes || echo no)"
expect "drive: after the mode the 100 events are written, nothing lost" "$((nw + 100))|0|0" "$(cfv "$(sed -n 4p <<<"$c")" written)|$(cfv "$(sed -n 4p <<<"$c")" pending)|$(cfv "$(sed -n 4p <<<"$c")" lost)"
expect "drive: the file holds every event in order (python)" "recs take=1 valid $((nw + 100)) newest $((nw + 99)) oldest 0 span $((nw + 100))" "$(cfpy drive | grep '^recs')"
cfmk flushnow
out=$(run flushnow "$G16; capflush; $(pairs 2); capflush; capfile; capflush")
expect "flushNow: no take, nothing to do (1), then 4 pending events written at once and 1 again" "capflush 1|capflush 1|capfile state 2 flushes 1 sectors 1 skips 0 written 4 lost 0 pending 0 take 1 headers 1 created 0 errors 0|capflush 1" \
  "$(grep -E '^(capflush|capfile)' <<<"$out" | paste -sd'|' -)"

# 16.5 a take longer than the ring: the file holds it all, the reader reads it back. 1,500 events, the ring only 1,016 of them
cfmk long
out=$(run long "$G16; capgen 1500 100; wait 400; capring; capfile; capread all")
c=$(grep '^capfile' <<<"$out")
expect "long take: the ring holds 1,016 of the 1,500 events, the file all 1,500" "capring count 1016|written 1500 pending 0 lost 0" "$(grep '^capring' <<<"$out")|$(cfk "$c" written pending lost)"
want=$(awk 'BEGIN { for (i = 0; i < 1500; i++) printf "%d %d %s 100 0\n", i, 24 + (int(i / 2) * 5) % 60, (i % 2 == 0 ? "on" : "off") }' | paste -sd'|' -)
expect "long take: the reader returns the 1,500 events 0..1499 with the notes the tap was given (note 24 + i/2*5 % 60, on at even i, the off of the same note at odd i)" "$want" "$(evs_cpp <<<"$out" | awk '{ $5 = 0; print }' | paste -sd'|' -)"
expect "long take: python reads the same 1,500" "$want" "$(cfpy long | evs_py | awk '{ $5 = 0; print }' | paste -sd'|' -)"

# 16.6 the file wraps at 32,768 records: 40,000 events, the newest 32,768 (7,232..39,999) are in the file, the size never changes
cfmk wrap
out=$(run wrap "$G16; capgen 40000 100; wait 400; capfile; capread")
c=$(grep '^capfile' <<<"$out")
expect "wrap: 40,000 events written, none lost, one header (the take start)" "written 40000 pending 0 lost 0 headers 1" "$(cfk "$c" written pending lost headers)"
expect "wrap: the reader: 32,768 valid events, 7,232..39,999 (seq mod 65,536: no wrap of that yet)" "span 32768 valid 32768 newest 39999 oldest 7232" "$(grep '^capread' <<<"$out" | awk '{print $12, $13, $14, $15, $16, $17, $18, $19}')"
expect "wrap: the file size is the same 524,800 B after 40,000 events (the FAT chain and the directory entry were never rewritten)" "524800" "$(cfsize wrap)"
pyw=$(cfpy wrap)
expect "wrap: python: the same extent, and every record of the window has the note of its event number (24 + n/2*5 % 60)" "recs take=1 valid 32768 newest 39999 oldest 7232 span 32768|0 wrong" \
  "$(grep '^recs' <<<"$pyw")|$(evs_py <<<"$pyw" | awk '{ w = 24 + (int($1 / 2) * 5) % 60; if ($2 != w) bad++ } END { print bad + 0, "wrong" }')"

# 16.7 takes: a new take (31 s gap) gets the next id, the records of the old one stay but are not read (no erase), and the id continues over a restart
cfmk takes
out=$(run takes "$G16; capgen 100 100; wait 31000; capgen 40 40; wait 400; capfile; capread")
expect "takes: the second take (after the 31 s gap) has id 2, 40 events, the header was written for both" "take 2|headers 2|capread take 2 lost 0/0|span 40 valid 40 newest 39 oldest 0" \
  "take $(cfv "$(grep '^capfile' <<<"$out")" take)|headers $(cfv "$(grep '^capfile' <<<"$out")" headers)|$(grep '^capread' <<<"$out" | awk '{print $1, $2, $3, $4, $5}')|$(grep '^capread' <<<"$out" | awk '{print $12, $13, $14, $15, $16, $17, $18, $19}')"
expect "takes: the old take stays in the file (the 36 records of id 1 beyond sector 1, which the new take rewrote whole, no erase) and python reads only take 2" "other 1 36|other 2 40|recs take=2 valid 40 newest 39 oldest 0 span 40" "$(cfpy takes other | grep '^other' | paste -sd'|' -)|$(cfpy takes | grep '^recs')"
out=$(run takes "$G16; capgen 10 10; wait 3000; capfile; capread")
expect "takes: after a restart (a second run on the same card) the id continues from the header: take 3" "take 3|capread take 3 lost 0/0" "take $(cfv "$(grep '^capfile' <<<"$out")" take)|$(grep '^capread' <<<"$out" | awk '{print $1, $2, $3, $4, $5}')"

# 16.8 a power cut: the file cut in the middle of a sector. The reader keeps what has a good CRC; the writer builds the file again at the next boot
cfmk pcut
run pcut "$G16; capgen 300 100; wait 400" >/dev/null
for v in a b c d; do mkdir -p "$WORK/pc$v/Capture"; cp "$WORK/pcut/Capture/take.cap" "$WORK/pc$v/Capture/take.cap"; done
cfpy pca truncate $((512 + 5 * 512 + 200))
out=$(run pca "capture 0; wait 300; capread all")
expect "power cut: the file cut 200 B into sector 5 of the records: 5 sectors + 12 whole records = 172 events valid, the 13th (cut inside) fails its CRC" "capread take 1 lost 0/0|span 172 valid 172 newest 171 oldest 0|172" \
  "$(grep '^capread' <<<"$out" | awk '{print $1, $2, $3, $4, $5}')|$(grep '^capread' <<<"$out" | awk '{print $12, $13, $14, $15, $16, $17, $18, $19}')|$(grep -c '^capev ' <<<"$out")"
expect "power cut: python agrees (172 valid)" "recs take=1 valid 172 newest 171 oldest 0 span 172" "$(cfpy pca | grep '^recs')"
out=$(run pca "$G16; $(pairs 20 5); wait 14000; capfile; capread")
c=$(grep '^capfile' <<<"$out")
expect "power cut: the next boot finds a file of a torn size, makes it again (129 steps), and the new take (40 events) is in it, nothing of the old one" "524800|state 2 created 129 written 40|capread take 1 lost 0/0|span 40 valid 40 newest 39 oldest 0" \
  "$(cfsize pca)|state $(cfv "$c" state) created $(cfv "$c" created) written $(cfv "$c" written)|$(grep '^capread' <<<"$out" | awk '{print $1, $2, $3, $4, $5}')|$(grep '^capread' <<<"$out" | awk '{print $12, $13, $14, $15, $16, $17, $18, $19}')"
cfpy pcb truncate $((512 + 5 * 512))
out=$(run pcb "$G16; $(pairs 20 5); wait 14000; capfile; capread")
c=$(grep '^capfile' <<<"$out")
expect "power cut: cut on a sector border: the creation goes on from there (1,019 sectors left = 128 steps, the file is whole again), the old header (take 1) is read, the new take is 2 with 40 events" "524800|state 2 created 128 written 40|capread take 2 lost 0/0|span 40 valid 40 newest 39 oldest 0" \
  "$(cfsize pcb)|state $(cfv "$c" state) created $(cfv "$c" created) written $(cfv "$c" written)|$(grep '^capread' <<<"$out" | awk '{print $1, $2, $3, $4, $5}')|$(grep '^capread' <<<"$out" | awk '{print $12, $13, $14, $15, $16, $17, $18, $19}')"
cfpy pcc corrupt $((512 + 40 * 16 + 4))
out=$(run pcc "capture 0; wait 300; capread all")
expect "power cut: one torn record (a byte of event 40 changed): the reader sees span 300 with 299 valid and gives event 40 back as a hole (take 0)" "span 300 valid 299 newest 299 oldest 0|capev 40 0 off v0 seq 0 take 0 pos 0 t 0" \
  "$(grep '^capread' <<<"$out" | awk '{print $12, $13, $14, $15, $16, $17, $18, $19}')|$(grep '^capev 40 ' <<<"$out")"
cfpy pcd corrupt 8
out=$(run pcd "capture 0; wait 300; capread")
expect "power cut: a torn header sector: no take (the CRC of the header)" "capread none" "$(grep '^capread' <<<"$out")"

# 16.9 the device's SdFile pool (FileSystemImpl::allocateSdFile, 24 slots, FF_FS_LOCK): every flush and every read of the take file gives its slot back. A closeFile(h, false) on a
#      handle the caller does not keep leaks the slot, 24 flushes later no file of the card opens until the next boot (the 2.0.0 RC lockout, poly-rel200-devfix).
#      The slots in use before the take (the pattern file the stock keeps open) are the baseline
cfmk pool
out=$(run pool "$G16; fspool; capgen 300 20; wait 2500; capfile; capread; capread; capread all; fspool")
fp=$(grep '^fspool' <<<"$out")
expect "pool: 300 events in 15 flushes, three reads: the pool is as before (no slot leaked), no open was refused, the file is written whole" "same 0|written 300 errors 0" \
  "$([ "$(sed -n 1p <<<"$fp" | awk '{print $3}')" = "$(sed -n 2p <<<"$fp" | awk '{print $3}')" ] && echo same || echo more) $(sed -n 2p <<<"$fp" | awk '{print $7}')|$(cfk "$(grep '^capfile' <<<"$out")" written errors)"

fi
new=$(cat "$WORK"/*.err 2>/dev/null | grep "SUMMARY" | grep -v "mtWavetableEnvelopes.cpp:206" | sort | uniq -c)
if [ -n "$new" ]; then fails=$((fails+1)); echo "FAIL  sanitizer report in a run:"; echo "$new"; fi
echo "$checks checks"
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fails failing)"; exit 1
