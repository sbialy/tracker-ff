#!/bin/bash
# scale_snap_sim.sh [PNG_DIR]
#
# Scale snap at playback (core/source/scales.cpp snapToScale, mtSequencer.cpp snapStepNote, Project Settings page > Scale snap, the "snap" line of the
# project sidecar Workspace/project.mtff) on the real UI and sequencer code of this tree (ui_shot) on the Mac. Host only: no device, every
# scenario has its own SD card folder. The sequencer plays MIDI instrument steps, "midilog 1" prints every note sent to USB MIDI out
# ("midi on NOTE VEL ch C t T"). A step note N is sent as MIDI note N + 24. "scale ROOT SCALE" sets the project Notes scale and Root
# note (24 = C), "snap T M" sets a track's mode (0 Off 1 Nearest 2 Down 3 Up), "snap" prints all 16 (ui_shot_main.cpp).
# Scales used: Major (2, C D E F G A B, every other note has two neighbours at the same distance) and Penta Maj (18, C D E G A: F is
# nearer E than G, B is nearer C than A, which tells Nearest from Down and Up).
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/scale_snap_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
# run NAME "script": stdout lines of the script (midi, snap, step, cat output) with the SD folder $WORK/NAME, the second run of a name reuses it
run() {
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$1" "wait 500; $2" 2>"$WORK/$1.err"
}
ons() { grep '^midi on' | awk '{printf "%s%s", (n++ ? " " : ""), $3}'; }              # "73 75 78"
onsch() { grep '^midi on' | awk -v c="$1" '$6 == c {printf "%s%s", (n++ ? " " : ""), $3}'; }
offs() { grep '^midi off' | awk '{printf "%s%s", (n++ ? " " : ""), $3}'; }
sorted() { tr ' ' '\n' | sort -n | tr '\n' ' ' | sed 's/ $//'; }
shot() { [ -n "$PNG_DIR" ] && echo "shot $PNG_DIR/$1.png" || echo "wait 1"; }
# three steps of track 0 on MIDI instrument 48 (channel 1), a quarter note apart is 12 steps of the grid: steps 0 4 8 are 460 ms apart
PLAY3() { echo "audio 1; midilog 1; tap Play; wait 3400; tap Play; wait 200; midilog 0"; }

# 1. Off: the notes play as written (Major, C# D# F# are out of scale)
out=$(run off "instr 48; scale 24 2; setstep 0 0 49 48 0 0 0 0; setstep 0 4 51 48 0 0 0 0; setstep 0 8 54 48 0 0 0 0; $(PLAY3); snap")
expect "off: the written notes play (73 75 78)" "73 75 78" "$(ons <<<"$out")"
expect "off: no track has a mode" "snap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" "$(grep '^snap' <<<"$out")"

# 2. Nearest in Major: a tie goes down (C# D# F# -> C D F)
out=$(run nearest "instr 48; scale 24 2; snap 0 1; setstep 0 0 49 48 0 0 0 0; setstep 0 4 51 48 0 0 0 0; setstep 0 8 54 48 0 0 0 0; $(PLAY3); step 0 0; step 0 4; step 0 8")
expect "nearest: ties go down (72 74 77)" "72 74 77" "$(ons <<<"$out")"
expect "nearest: every note off names the note that was sent" "72 74 77" "$(offs <<<"$out")"
expect "nearest: the pattern keeps the written note" "step 0 0 note 49 instr 48 fx0 0:0 fx1 0:0" "$(grep '^step 0 0 ' <<<"$out")"
expect "nearest: the pattern keeps the written note (2)" "step 0 8 note 54 instr 48 fx0 0:0 fx1 0:0" "$(grep '^step 0 8 ' <<<"$out")"

# 3. Penta Maj, F (53) and B (59) and C (48, in scale): Nearest E C C, Down E A C, Up G C C
for m in "1 Nearest 76 84 72" "2 Down 76 81 72" "3 Up 79 84 72"; do
    set -- $m
    out=$(run penta$1 "instr 48; scale 24 18; snap 0 $1; setstep 0 0 53 48 0 0 0 0; setstep 0 4 59 48 0 0 0 0; setstep 0 8 48 48 0 0 0 0; $(PLAY3)")
    expect "penta $2: F B C play $3 $4 $5" "$3 $4 $5" "$(ons <<<"$out")"
    expect "penta $2: note offs match" "$3 $4 $5" "$(offs <<<"$out")"
done

# 4. per track: track 0 (channel 1) Nearest, track 1 (channel 2) Off, same steps
out=$(run pertrack "instr 48; scale 24 2; snap 0 1; setstep 0 0 49 48 0 0 0 0; setstep 1 0 49 49 0 0 0 0; setstep 0 4 51 48 0 0 0 0; setstep 1 4 51 49 0 0 0 0; $(PLAY3); snap")
expect "per track: track 0 is snapped" "72 74" "$(onsch 1 <<<"$out")"
expect "per track: track 1 plays as written" "73 75" "$(onsch 2 <<<"$out")"
expect "per track: only track 0 has a mode" "snap 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" "$(grep '^snap' <<<"$out")"

# 5. Chromatic (scale 0) changes nothing in any mode, and root C# (25) Major keeps C# in scale
out=$(run chroma "instr 48; scale 24 0; snap -1 1; setstep 0 0 49 48 0 0 0 0; setstep 0 4 51 48 0 0 0 0; $(PLAY3)")
expect "chromatic: nothing moves in Nearest" "73 75" "$(ons <<<"$out")"
out=$(run root "instr 48; scale 25 2; snap 0 1; setstep 0 0 49 48 0 0 0 0; setstep 0 4 50 48 0 0 0 0; $(PLAY3)")
expect "root C#: C# stays, D (out of C# Major) goes down to C#" "73 73" "$(ons <<<"$out")"

# 6. an arp (chord major 5, arp up, 1/4 period) of a C# step: every note it plays is a Major scale note and the pattern is untouched
#    (fx0 chord 14:5, fx1 arp 21:3)
out=$(run arp "instr 48; scale 24 2; snap 0 1; setstep 0 0 49 48 14 5 21 3; audio 1; midilog 1; tap Play; wait 1500; tap Play; wait 200; midilog 0; step 0 0")
bad=$(grep '^midi on' <<<"$out" | awk '{n=($3-24)%12; if (n!=0&&n!=2&&n!=4&&n!=5&&n!=7&&n!=9&&n!=11) b++} END {print b+0}')
cnt=$(grep -c '^midi on' <<<"$out")
expect "arp: more than one note plays" "yes" "$([ "$cnt" -gt 1 ] && echo yes || echo no)"
expect "arp: every arp note is in the scale" "0" "$bad"
# the arp sends a note off at every change of note, and a second one from stopOpenNoteOnTrack (stock): the notes must be the same set
expect "arp: every note off names a note that was sent" "$(ons <<<"$out" | tr ' ' '\n' | sort -nu | tr '\n' ' ')" "$(offs <<<"$out" | tr ' ' '\n' | sort -nu | tr '\n' ' ')"

# 7. rolls: 1/8 note roll up (N, value 36 + 6 = 42) from F# in Major climbs by scale notes (a Nearest roll that stepped one semitone
#    from the snapped note would stay on F), roll down (n, 27 + 6 = 33) falls
out=$(run rollup "instr 48; scale 24 2; snap 0 1; setstep 0 0 54 48 3 42 0 0; audio 1; midilog 1; tap Play; wait 1200; tap Play; wait 200; midilog 0")
expect "roll up: 77 79 81 83 84 86 ..." "77 79 81 83 84 86" "$(ons <<<"$out" | cut -d' ' -f1-6)"
out=$(run rolldown "instr 48; scale 24 2; snap 0 1; setstep 0 0 54 48 3 33 0 0; audio 1; midilog 1; tap Play; wait 1200; tap Play; wait 200; midilog 0")
expect "roll down: 77 76 74 72 71 69 ..." "77 76 74 72 71 69" "$(ons <<<"$out" | cut -d' ' -f1-6)"
out=$(run rollrand "instr 48; scale 24 2; snap 0 1; setstep 0 0 54 48 3 51 0 0; audio 1; midilog 1; tap Play; wait 1500; tap Play; wait 200; midilog 0")
bad=$(grep '^midi on' <<<"$out" | awk '{n=($3-24)%12; if (n!=0&&n!=2&&n!=4&&n!=5&&n!=7&&n!=9&&n!=11) b++} END {print b+0}')
expect "random roll: every note is in the scale" "0" "$bad"
expect "random roll: note offs match the note ons" "$(ons <<<"$out" | sorted)" "$(offs <<<"$out" | sorted)"

# 8. a sample instrument track (instrument 0 is not a MIDI one: nothing on the MIDI log) is accepted and the step stays
out=$(run sample "instr 0; scale 24 2; snap 0 1; setstep 0 0 49 0 0 0 0 0; $(PLAY3); step 0 0")
expect "sample track: the pattern keeps the written note" "step 0 0 note 49 instr 0 fx0 0:0 fx1 0:0" "$(grep '^step' <<<"$out")"

# 9. Project Settings page (File, F7) > Scale snap on the real menu: down to Scale snap, F8, down to Track 3, F8, one detent (Nearest), F8 Apply
out=$(run ui "tap File; wait 800; tap F7; wait 800; tap Down 14; tap F8; wait 200; $(shot snap-group); tap Down 2; tap F8; wait 100; enc 1; wait 100; $(shot snap-pick); snap; tap F8; wait 300; snap; tap Down 1; tap F8; wait 100; enc 3; wait 100; tap F8; wait 300; snap")
expect "ui: the pick is not applied before Apply" "snap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" "$(grep '^snap' <<<"$out" | sed -n 1p)"
expect "ui: Apply sets track 3 to Nearest" "snap 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0" "$(grep '^snap' <<<"$out" | sed -n 2p)"
expect "ui: track 4 to Up (three detents) with track 3 kept" "snap 0 0 1 3 0 0 0 0 0 0 0 0 0 0 0 0" "$(grep '^snap' <<<"$out" | sed -n 3p)"

# 10. project.mtff: a mode is saved at the Workspace save (track.N.snap), a second start loads it, all Off removes the keys again
out=$(run keep "snap 0 1; snap 9 3; snap 15 2; dirty; wait 2500; cat Workspace/project.mtff")
expect "project.mtff: the snap keys are written, one per track that has a mode" "track.1.snap 1|track.10.snap 3|track.16.snap 2" "$(grep '\.snap ' <<<"$out" | paste -sd'|' -)"
expect "project.mtff: the file starts with its header (format 1, then the ff= build= cap= tokens)" "tffproj 1 ff=" "$(grep -m1 '^tffproj' <<<"$out" | cut -c1-13)"
out=$(run keep "snap")
expect "project.mtff: a second start loads the modes" "snap 1 0 0 0 0 0 0 0 0 3 0 0 0 0 0 2" "$(grep '^snap' <<<"$out")"
out=$(run keep "snap -1 0; dirty; wait 2500; cat Workspace/project.mtff; snap")
expect "project.mtff: every track Off writes no snap key" "0" "$(grep -c '\.snap ' <<<"$out")"
out=$(run keep "snap")
expect "project.mtff: a third start has no mode" "snap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" "$(grep '^snap' <<<"$out")"

# 11. an old project (a plugins.txt of 1.3.0 with an insert, no project.mtff) loads with every track Off and keeps its slot after a save
out=$(run oldproj "put Workspace/plugins.txt tffplug 1\\nformat 2\\nmixer 10\\nslot 1 92b586d4 00010000 Dirt\\np Type=2\\np Drive=62\\n; snap")
out=$(run oldproj "snap; snap 2 2; dirty; wait 2500; cat Workspace/plugins.txt; cat Workspace/project.mtff")
expect "old project: no snap key, every track Off" "snap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" "$(grep '^snap' <<<"$out" | sed -n 1p)"
expect "old project: the insert slot survives a save with a snap key" "slot 1 92b586d4 00010000 Dirt" "$(grep '^slot' <<<"$out")"
expect "old project: the snap key is added to project.mtff, not to plugins.txt" "track.3.snap 2 0" "$(grep '\.snap ' <<<"$out" | tail -1) $(grep -c '^snap [0-9]\{16\}' <<<"$out")"
expect "old project: its parameters are kept" "p Type=2" "$(grep -m1 '^p Type' <<<"$out")"

# 12. no ui_shot report: a failed run leaves its stderr, the SAN build's reports are the ones that matter
errs=$(cat "$WORK"/*.err 2>/dev/null | grep -c "ERROR: AddressSanitizer\|runtime error" | tr -d ' ')
expect "no sanitizer report in any run" "0" "$errs"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
