#!/bin/bash
# snap_fx_sim.sh [PNG_DIR]
#
# The latching Snap step FX (FX id 52, FX_TYPE_SNAP, values 0 Off 1 Nearest 2 Down 3 Up 4 Track) on top of the per track scale snap
# (core/source/mtSequencer.cpp snapStepNote / snapFxEnter / SNAP_FX_RESET, scales.h) on the real UI and sequencer code of this tree
# (ui_shot) on the Mac. Host only: no device, every scenario has its own SD card folder. Same MIDI log method as scale_snap_sim.sh:
# "midilog 1" prints every note sent to USB MIDI out, a step note N is sent as MIDI note N + 24 on MIDI instrument 48 (channel 1) and 49
# (channel 2). "scale ROOT SCALE" sets the project scale (24 = C, scale 2 Major, 18 Penta Maj), "snap T M" the project mode of a track,
# "snapfx" prints the latch of every track (255 none), "grid T S" the FX cells of a step as the pattern grid draws them.
# FX ids: Roll 3, Chance 4, MIDI Chord 14, Random value 16, Arp 21, Chance FX 48, Snap 52. At the default tempo a step is 115.4 ms.
# The rules checked (ref/snap-fx.md): the latch starts at the step the Snap FX sits on (its own note included), holds until the next Snap FX
# of the track or the end of the pass, then the track falls back to its project setting. Value 4 (Track) clears the latch from its step: the
# track plays its project setting (mtff track.N.snap) again, before the end of the pass. Pass end = pattern loop, song pattern change, Stop.
# Play from the cursor (Shift + Play) starts with the latch of the last Snap FX above the cursor step.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/snap_fx_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

SN=52; COND=48; CH=4; ROLL=3; CHORD=14; ARP=21; RV=16

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
run() { ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$1" "wait 500; $2" 2>"$WORK/$1.err"; }
ons() { grep '^midi on' | awk '{printf "%s%s", (n++ ? " " : ""), $3}'; }
onsch() { grep '^midi on' | awk -v c="$1" '$6 == c {printf "%s%s", (n++ ? " " : ""), $3}'; }
offs() { grep '^midi off' | awk '{printf "%s%s", (n++ ? " " : ""), $3}'; }
sorted() { tr ' ' '\n' | sort -n | tr '\n' ' ' | sed 's/ $//'; }
shot() { [ -n "$PNG_DIR" ] && echo "shot $PNG_DIR/$1.png" || echo "wait 1"; }
inscale() { grep '^midi on' | awk '{n=($3-24)%12; if (n!=0&&n!=2&&n!=4&&n!=5&&n!=7&&n!=9&&n!=11) b++} END {print b+0}'; }
# play from the top for MS ms, then stop
PLAY() { echo "audio 1; midilog 1; tap Play; wait $1; tap Play; wait 200; midilog 0"; }
# Shift + Play with the cursor on step $1 (the real "play from the cursor" key chord), for MS ms, then stop
FROM() { echo "tap Down $1; audio 1; midilog 1; hold Shift; tap Play; unhold Shift; wait $2; tap Play; wait 200; midilog 0"; }
# Penta Maj notes F B C (53 59 48) on steps 0 4 8 of track 0: the sent notes are F 77, B 83, C 72 as written
FBC="setstep 0 0 53 48 0 0 0 0; setstep 0 4 59 48 0 0 0 0; setstep 0 8 48 48 0 0 0 0"

# 1. the four values of the FX on the first step, project mode Off: the note of the Snap step itself is moved, the later steps too
for m in "0 Off 77 83 72" "1 Nearest 76 84 72" "2 Down 76 81 72" "3 Up 79 84 72"; do
    set -- $m
    out=$(run mode$1 "instr 48; scale 24 18; setstep 0 0 53 48 $SN $1 0 0; setstep 0 4 59 48 0 0 0 0; setstep 0 8 48 48 0 0 0 0; $(PLAY 3400)")
    expect "value $1 ($2) on step 0, project Off: F B C play $3 $4 $5" "$3 $4 $5" "$(ons <<<"$out")"
    expect "value $1 ($2): every note off names the note that was sent" "$3 $4 $5" "$(offs <<<"$out")"
done

# 2. latching: the next Snap FX takes over, an explicit Off overrides the project mode
out=$(run chg "instr 48; scale 24 18; setstep 0 0 53 48 $SN 2 0 0; setstep 0 4 59 48 $SN 3 0 0; setstep 0 8 48 48 0 0 0 0; $(PLAY 3400)")
expect "Down on step 0, Up on step 4: F B C play 76 84 72 (Down, Up, Up held to step 8)" "76 84 72" "$(ons <<<"$out")"
out=$(run offover "instr 48; scale 24 18; snap 0 1; setstep 0 0 53 48 0 0 0 0; setstep 0 4 59 48 $SN 0 0 0; setstep 0 8 53 48 0 0 0 0; $(PLAY 3400)")
expect "project Nearest, Snap Off on step 4: 76 (project), 83 and 77 as written (the Off holds)" "76 83 77" "$(ons <<<"$out")"

# 2b. Track (4): the latch is cleared from that step, the project setting plays again (its own note included)
out=$(run trkprj "instr 48; scale 24 18; snap 0 1; setstep 0 0 53 48 $SN 2 0 0; setstep 0 4 59 48 $SN 4 0 0; setstep 0 8 48 48 0 0 0 0; $(PLAY 3400)")
expect "project Nearest, Down on step 0, Track on step 4: F B C play 76 (Down) 84 (Nearest, not Down 81) 72" "76 84 72" "$(ons <<<"$out")"
out=$(run trkoff "instr 48; scale 24 18; setstep 0 0 53 48 $SN 3 0 0; setstep 0 4 59 48 $SN 4 0 0; setstep 0 8 48 48 0 0 0 0; $(PLAY 3400)")
expect "project Off, Up on step 0, Track on step 4: 79 (Up) 83 and 72 as written (project Off)" "79 83 72" "$(ons <<<"$out")"
out=$(run trkfirst "instr 48; scale 24 18; snap 0 1; setstep 0 0 53 48 $SN 4 0 0; setstep 0 4 59 48 0 0 0 0; setstep 0 8 48 48 0 0 0 0; $(PLAY 3400)")
expect "Track on step 0 alone: the project mode plays (Nearest 76 84 72)" "76 84 72" "$(ons <<<"$out")"
out=$(run trkagain "instr 48; scale 24 18; setstep 0 0 53 48 $SN 3 0 0; setstep 0 4 59 48 $SN 4 0 0; setstep 0 8 48 48 $SN 2 0 0; setstep 0 12 53 48 0 0 0 0; $(PLAY 3400)")
expect "Up, Track, Down, then held: F B C F play 79 83 72 (Down of C is C) 76 (Down of F)" "79 83 72 76" "$(ons <<<"$out")"
out=$(run trklatch "instr 48; scale 24 2; patlen 8; setstep 0 0 49 48 $SN 3 0 0; setstep 0 4 49 48 $SN 4 0 0; audio 1; midilog 1; tap Play; wait 300; snapfx; wait 300; snapfx; tap Play; wait 200; midilog 0")
expect "latch Up (3) before the Track step, cleared (255) after it" "snapfx 3 255 255 255 255 255 255 255 255 255 255 255 255 255 255 255|snapfx 255 255 255 255 255 255 255 255 255 255 255 255 255 255 255 255" "$(grep '^snapfx' <<<"$out" | paste -sd'|' -)"
out=$(run trkgate "instr 48; scale 24 2; snap 0 1; setstep 0 0 49 48 $SN 3 0 0; setstep 0 4 49 48 $SN 4 $COND 0; setstep 0 6 49 48 0 0 0 0; $(PLAY 1000)")
expect "a gated Track does not clear the latch: Up on 74 74 74 (project Nearest would play 72)" "74 74 74" "$(ons <<<"$out")"

# 3. a pattern without the FX is the project setting only, the latch stays empty (255)
out=$(run noimpact "instr 48; scale 24 18; snap 0 1; $FBC; audio 1; midilog 1; tap Play; wait 1500; snapfx; tap Play; wait 200; midilog 0")
expect "no Snap FX: the project mode plays (Nearest 76 84 72)" "76 84 72" "$(ons <<<"$out")"
expect "no Snap FX: no track holds a latch" "snapfx 255 255 255 255 255 255 255 255 255 255 255 255 255 255 255 255" "$(grep '^snapfx' <<<"$out")"

# 4. the end of the pass: the pattern is 8 steps (923 ms), C# on steps 0 4 6, Snap Up on step 4. Pass 1: 73 (project Off) 74 74. The loop: the
#    latch is gone at the start of pass 2 (73), then Up again from step 4
S3="patlen 8; setstep 0 0 49 48 0 0 0 0; setstep 0 4 49 48 $SN 3 0 0; setstep 0 6 49 48 0 0 0 0"
out=$(run loop "instr 48; scale 24 2; $S3; $(PLAY 2100)")
expect "loop: 73 74 74, then the project mode again (73) at the top of the next pass, 74 74, 73" "73 74 74 73 74 74 73" "$(ons <<<"$out")"
out=$(run loopnear "instr 48; scale 24 2; snap 0 1; $S3; $(PLAY 2100)")
expect "loop, project Nearest: 72 74 74 72 74 74 72 (Nearest of C# is C, Up is D)" "72 74 74 72 74 74 72" "$(ons <<<"$out")"

# 5. Stop: the latch of the pass is gone, the next Play starts from the project mode
out=$(run stop "instr 48; scale 24 2; $S3; audio 1; midilog 1; tap Play; wait 600; snapfx; tap Play; wait 200; snapfx; audio 1; tap Play; wait 250; tap Play; wait 100; midilog 0")
expect "Stop: the latch is Up (3) on track 1 while it plays, none after Stop" "snapfx 3 255 255 255 255 255 255 255 255 255 255 255 255 255 255 255|snapfx 255 255 255 255 255 255 255 255 255 255 255 255 255 255 255 255" "$(grep '^snapfx' <<<"$out" | paste -sd'|' -)"
expect "Stop: the next Play starts with the project mode (the first note of both takes is 73)" "73 74 73" "$(ons <<<"$out" | awk '{print $1, $2, $(NF)}')"

# 6. Play from the cursor: the latch of the last Snap FX above the cursor step, so a step sounds the same on the first pass as on every later one
out=$(run from5 "instr 48; scale 24 2; $S3; $(FROM 5 1150)")
expect "cursor on step 5: step 6 plays Up (74) at once, then the next pass 73 74 74" "74 73 74 74" "$(ons <<<"$out")"
out=$(run from2 "instr 48; scale 24 2; $S3; $(FROM 2 800)")
expect "cursor on step 2 (above the Snap FX): 74 74, then the project mode (73)" "74 74 73" "$(ons <<<"$out")"
out=$(run from4 "instr 48; scale 24 2; $S3; $(FROM 4 700)")
expect "cursor on the Snap step itself: its own FX applies (74 74)" "74 74" "$(ons <<<"$out" | cut -d' ' -f1-2)"
out=$(run fromlast "instr 48; scale 24 2; patlen 8; setstep 0 1 49 48 $SN 2 0 0; setstep 0 3 49 48 $SN 3 0 0; setstep 0 5 49 48 0 0 0 0; $(FROM 5 300)")
expect "cursor on step 5, Down on step 1 and Up on step 3: the later one (Up, 74) is held" "74" "$(ons <<<"$out" | cut -d' ' -f1)"
out=$(run fromoff "instr 48; scale 24 2; snap 0 1; patlen 8; setstep 0 1 49 48 $SN 0 0 0; setstep 0 5 49 48 0 0 0 0; $(FROM 5 300)")
expect "cursor on step 5, project Nearest, Snap Off on step 1: the Off is held (73, not 72)" "73" "$(ons <<<"$out" | cut -d' ' -f1)"
out=$(run fromnone "instr 48; scale 24 2; snap 0 1; patlen 8; setstep 0 5 49 48 0 0 0 0; $(FROM 5 300)")
expect "cursor on step 5, no Snap FX above it: the project mode (72)" "72" "$(ons <<<"$out" | cut -d' ' -f1)"
# the play from the cursor chase with the 4 FX project switch: FX3 / FX4 are read too (rank 2 = FX4, rank 3 = FX3)
out=$(run fromx "instr 48; scale 24 2; patlen 8; fx34on 1; setfx 0 1 3 $SN 3; setstep 0 5 49 48 0 0 0 0; $(FROM 5 300)")
expect "cursor on step 5, Snap Up in FX3 on step 1: held (74)" "74" "$(ons <<<"$out" | cut -d' ' -f1)"
out=$(run fromx2 "instr 48; scale 24 2; patlen 8; fx34on 1; setfx 0 1 3 $SN 3; setfx 0 3 2 $SN 2; setstep 0 5 49 48 0 0 0 0; $(FROM 5 300)")
expect "cursor on step 5, Up in FX3 on step 1, Down in FX4 on step 3: the later step wins (72)" "72" "$(ons <<<"$out" | cut -d' ' -f1)"
out=$(run fromx3 "instr 48; scale 24 2; patlen 8; fx34on 1; setfx 0 1 3 $SN 3; setstep 0 1 49 48 $SN 2 0 0; setstep 0 5 49 48 0 0 0 0; $(FROM 5 300)")
expect "cursor on step 5, Down in FX2 and Up in FX3 on one step: FX2 wins (72)" "72" "$(ons <<<"$out" | cut -d' ' -f1)"

# the chase knows Track too: Up on step 1, Track on step 3, the cursor on step 5 with project Nearest plays 72 (not Up 74)
out=$(run fromtrk "instr 48; scale 24 2; snap 0 1; patlen 8; setstep 0 1 49 48 $SN 3 0 0; setstep 0 3 49 48 $SN 4 0 0; setstep 0 5 49 48 0 0 0 0; $(FROM 5 300)")
expect "cursor on step 5, Up on step 1, Track on step 3: the project mode (72)" "72" "$(ons <<<"$out" | cut -d' ' -f1)"
out=$(run fromtrk2 "instr 48; scale 24 2; snap 0 1; patlen 8; fx34on 1; setstep 0 1 49 48 $SN 3 0 0; setfx 0 3 3 $SN 4; setstep 0 5 49 48 0 0 0 0; $(FROM 5 300)")
expect "cursor on step 5, Up in FX1 on step 1, Track in FX3 on step 3: the later step wins (72)" "72" "$(ons <<<"$out" | cut -d' ' -f1)"
out=$(run fromtrk3 "instr 48; scale 24 2; snap 0 1; patlen 8; setstep 0 1 49 48 $SN 4 0 0; setstep 0 3 49 48 $SN 3 0 0; setstep 0 5 49 48 0 0 0 0; $(FROM 5 300)")
expect "cursor on step 5, Track on step 1, Up on step 3: the later step wins (Up, 74)" "74" "$(ons <<<"$out" | cut -d' ' -f1)"

# 7. song: the latch does not cross into the next pattern. Pattern 1 ends with the Up latch held, pattern 2 plays its C# with the project mode
out=$(run song "instr 48; scale 24 2; pattern 1; patlen 8; setstep 0 0 49 48 0 0 0 0; setstep 0 4 49 48 $SN 3 0 0; dirty; pattern 2; patlen 8; setstep 0 0 49 48 0 0 0 0; dirty; pattern 1; playlist 1 2; tap Song; wait 300; audio 1; midilog 1; tap F2; wait 1500; tap Play; wait 200; midilog 0")
expect "song: pattern 1 plays 73 74, pattern 2 starts with the project mode (73)" "73 74 73" "$(ons <<<"$out" | cut -d' ' -f1-3)"

# 8. per track: the latch belongs to the track it sits on, project modes of the other tracks are kept
out=$(run tracks "instr 48; scale 24 2; snap 2 1; setstep 0 0 49 48 $SN 3 0 0; setstep 1 0 49 49 0 0 0 0; setstep 2 0 49 50 0 0 0 0; setstep 1 4 49 49 $SN 2 0 0; setstep 0 4 49 48 0 0 0 0; setstep 1 8 49 49 0 0 0 0; $(PLAY 3400)")
expect "tracks: track 1 Up, track 2 as written, track 3 (project Nearest) unaffected by the other tracks' FX" "74 74" "$(onsch 1 <<<"$out")"
expect "tracks: track 2 plays 73 (no latch, project Off) then Down from its own FX (72) held to step 8" "73 72 72" "$(onsch 2 <<<"$out")"
expect "tracks: track 3 keeps its project mode (72)" "72" "$(onsch 3 <<<"$out")"

# 9. arp, roll, stutter
out=$(run arp0 "instr 48; scale 24 2; setstep 0 0 49 48 $CHORD 5 $ARP 3; audio 1; midilog 1; tap Play; wait 1500; tap Play; wait 200; midilog 0")
expect "arp without the FX: out of scale notes play (project Off)" "yes" "$([ "$(inscale <<<"$out")" -gt 0 ] && echo yes || echo no)"
out=$(run arp1 "instr 48; scale 24 2; fx34on 1; setstep 0 0 49 48 $CHORD 5 $ARP 3; setfx 0 0 2 $SN 1; audio 1; midilog 1; tap Play; wait 1500; tap Play; wait 200; midilog 0")
expect "arp with Snap Nearest in FX4: every arp note is in the scale" "0" "$(inscale <<<"$out")"
expect "arp with Snap Nearest: more than one note, every note off names a note that was sent" "yes $(ons <<<"$out" | tr ' ' '\n' | sort -nu | tr '\n' ' ')" "$([ "$(grep -c '^midi on' <<<"$out")" -gt 1 ] && echo yes) $(offs <<<"$out" | tr ' ' '\n' | sort -nu | tr '\n' ' ')"
out=$(run rollup "instr 48; scale 24 2; setstep 0 0 54 48 $ROLL 42 $SN 1; audio 1; midilog 1; tap Play; wait 1200; tap Play; wait 200; midilog 0")
expect "roll up with Snap Nearest (project Off): scale note by scale note (77 79 81 83 84 86)" "77 79 81 83 84 86" "$(ons <<<"$out" | cut -d' ' -f1-6)"
out=$(run rolldown "instr 48; scale 24 2; setstep 0 0 54 48 $ROLL 33 $SN 1; audio 1; midilog 1; tap Play; wait 1200; tap Play; wait 200; midilog 0")
expect "roll down with Snap Nearest: 77 76 74 72 71 69" "77 76 74 72 71 69" "$(ons <<<"$out" | cut -d' ' -f1-6)"
out=$(run rolloff "instr 48; scale 24 2; snap 0 1; setstep 0 0 54 48 $ROLL 42 $SN 0; audio 1; midilog 1; tap Play; wait 1200; tap Play; wait 200; midilog 0")
expect "roll up, project Nearest, Snap Off: no snap, one semitone a note (78 79 80 81 82 83)" "78 79 80 81 82 83" "$(ons <<<"$out" | cut -d' ' -f1-6)"
out=$(run rollrand "instr 48; scale 24 2; setstep 0 0 54 48 $ROLL 51 $SN 1; audio 1; midilog 1; tap Play; wait 1500; tap Play; wait 200; midilog 0")
expect "random roll with Snap Nearest: every note is in the scale" "0" "$(inscale <<<"$out")"
out=$(run stut "instr 48; scale 24 2; setstep 0 0 49 48 $SN 1 0 0; snapfx stutter 0 11; audio 1; midilog 1; tap Play; wait 700; tap Play; wait 100; midilog 0")
expect "stutter repeats of a snapped step: every repeat is the snapped note (72), more than 5" "yes 72" "$([ "$(grep -c '^midi on' <<<"$out")" -gt 5 ] && echo yes) $(ons <<<"$out" | tr ' ' '\n' | sort -nu | tr '\n' ' ' | sed 's/ $//')"

# 10. Chance FX gating (the slot pair model: a Chance FX gates only the other slot of its pair, the note always plays)
out=$(run gate0 "instr 48; scale 24 2; setstep 0 0 49 48 $SN 3 $COND 0; $(PLAY 400)")
expect "Chance FX 0 % next to Snap Up: the Snap is gated off, the note plays as written (73), nothing is latched" "73" "$(ons <<<"$out" | cut -d' ' -f1)"
out=$(run gate100 "instr 48; scale 24 2; setstep 0 0 49 48 $SN 3 $COND 100; $(PLAY 400)")
expect "Chance FX 100 % next to Snap Up: the Snap applies (74)" "74" "$(ons <<<"$out" | cut -d' ' -f1)"
out=$(run gateslot "instr 48; scale 24 2; setstep 0 0 49 48 $COND 0 $SN 3; $(PLAY 400)")
expect "Chance FX in the other slot order (FX2 Chance FX, FX1 Snap): gated as well (73)" "73" "$(ons <<<"$out" | cut -d' ' -f1)"
out=$(run gatehold "instr 48; scale 24 2; setstep 0 0 49 48 $SN 3 0 0; setstep 0 4 49 48 $SN 2 $COND 0; setstep 0 6 49 48 0 0 0 0; $(PLAY 1000)")
expect "a gated Snap does not change the latch held from before: Up, Up (the Down on step 4 is gated), Up" "74 74 74" "$(ons <<<"$out")"
out=$(run gatex "instr 48; scale 24 2; fx34on 1; setstep 0 0 49 48 0 0 0 0; setfx 0 0 2 $COND 0; setfx 0 0 3 $SN 3; $(PLAY 400)")
expect "4 FX: Chance FX in FX4 gates the Snap in FX3 (73)" "73" "$(ons <<<"$out" | cut -d' ' -f1)"
out=$(run gatexo "instr 48; scale 24 2; fx34on 1; setstep 0 0 49 48 $COND 0 $CHORD 5; setfx 0 0 3 $SN 3; $(PLAY 400)")
expect "4 FX: Chance FX in the FX1/FX2 pair does not gate the Snap of the FX3/FX4 pair (74 on the root)" "74" "$(ons <<<"$out" | sort -n | tail -1 | cut -d' ' -f1)"
out=$(run gatech "instr 48; scale 24 2; setstep 0 0 49 48 0 0 0 0; setstep 0 4 49 48 $CH 0 $SN 3; setstep 0 6 49 48 0 0 0 0; $(PLAY 1000)")
expect "Chance 0 % drops the whole step with its Snap: no note on step 4, step 6 is not latched (73 73)" "73 73" "$(ons <<<"$out")"

# 11. two Snap FX on one step: the play loop order FX2 > FX1 > FX4 > FX3 decides
out=$(run two1 "instr 48; scale 24 2; setstep 0 0 49 48 $SN 3 $SN 2; $(PLAY 400)")
expect "two Snap FX on one step, FX2 Up and FX1 Down: FX2 wins (74)" "74" "$(ons <<<"$out" | cut -d' ' -f1)"
out=$(run two2 "instr 48; scale 24 2; setstep 0 0 49 48 $SN 2 $SN 3; $(PLAY 400)")
expect "FX2 Down and FX1 Up: FX2 wins (72)" "72" "$(ons <<<"$out" | cut -d' ' -f1)"
out=$(run two3 "instr 48; scale 24 2; fx34on 1; setstep 0 0 49 48 0 0 0 0; setfx 0 0 2 $SN 3; setfx 0 0 3 $SN 2; $(PLAY 400)")
expect "FX4 Up and FX3 Down: FX4 wins (74)" "74" "$(ons <<<"$out" | cut -d' ' -f1)"
out=$(run two4 "instr 48; scale 24 2; fx34on 1; setstep 0 0 49 48 $SN 2 0 0; setfx 0 0 2 $SN 3; $(PLAY 400)")
expect "FX2 Down and FX4 Up: the FX1/FX2 pair wins (72)" "72" "$(ons <<<"$out" | cut -d' ' -f1)"

# 12. Random value next to the Snap: the drawn value stays in 0..3, the latch never leaves the mode range
out=$(run rnd "instr 48; scale 24 2; patlen 4; setstep 0 0 49 48 $RV 100 $SN 1; audio 1; midilog 1; tap Play; wait 2400; snapfx; tap Play; wait 100; midilog 0")
expect "Random value next to Snap: the latch of the track is a mode 0..3 (or none, a drawn 4 Track clears it)" "ok" "$(grep '^snapfx' <<<"$out" | awk '{ v=$2; print (v<=3 || v==255) ? "ok" : "bad " v }')"

# 13. the FX list: the grid cells, shots of the FX columns
out=$(run grid "setstep 0 0 49 48 $SN 0 0 0; setstep 0 1 49 48 $SN 1 0 0; setstep 0 2 49 48 $SN 2 0 0; setstep 0 3 49 48 $SN 3 0 0; setstep 0 4 49 48 $SN 4 0 0; setstep 0 5 49 48 $SN 9 0 0; grid 0 0; grid 0 1; grid 0 2; grid 0 3; grid 0 4; grid 0 5; tap Pattern; wait 300; $(shot snap-grid)")
expect "grid: the type letter N and the cells off nea dwn up trk (a value past 4 reads as Track)" "grid 0 0 fx0 [Noff] fx1 [----]|grid 0 1 fx0 [Nnea] fx1 [----]|grid 0 2 fx0 [Ndwn] fx1 [----]|grid 0 3 fx0 [N up] fx1 [----]|grid 0 4 fx0 [Ntrk] fx1 [----]|grid 0 5 fx0 [Ntrk] fx1 [----]" "$(grep '^grid' <<<"$out" | paste -sd'|' -)"

# 14. no sanitizer report in any run
errs=$(cat "$WORK"/*.err 2>/dev/null | grep -c "ERROR: AddressSanitizer\|runtime error" | tr -d ' ')
expect "no sanitizer report in any run" "0" "$errs"

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
