#!/bin/bash
# idea_gen_sim.sh [PNG_DIR]
#
# The pattern idea generator (Fill dialog, Note column, Fill Type "Role", core/source/mtPatternGen.cpp and Sequencer::fillRoleNotes) on the
# real UI code of this tree (ui_shot) on the Mac. Host only: no device, every scenario has its own SD card folder (an empty card).
# The scripts drive the buttons like a hand does: Rec (edit mode), Shift + arrows (the selection), F4 (Fill), Left/Right (the place in
# the dialog: 1 Density, 2 Scale, 3 Fill Type, 4 Role), the encoder (the value), F8 (Fill), F6 (Undo). The steps are read back with
# "step" and "stepv" (ui_shot_main.cpp). The seed of an apply comes from the random generator of the firmware, so the checks are on what
# every seed must give (the anchors, the register, the scale, what stays), never on a fixed pattern. The exact output of one seed is in
# pattern_gen_test.cpp.
# PNG_DIR: also writes the screens of the dialog. SAN=address,undefined: the sanitizer build (build/ui-shot-san), any report fails the run
# except the stock mtWavetableEnvelopes.cpp:206 (also allowlisted by the sim gate).
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/idea_gen_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"${JOBS:-$(sh "$HERE/../jobs.sh" n)}" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
expect() { checks=$((checks+1)); if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
truth() { checks=$((checks+1)); if [ "$2" = 1 ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n' "$1"; fi; }
run() {   # run NAME "script": the lines of "step", "stepv" and "cursor"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$1" "wait 500; $2" 2>"$WORK/$1.err" | grep -E '^(step|stepv|cursor) '
    if [ -n "$SAN" ] && grep -E "runtime error|AddressSanitizer" "$WORK/$1.err" | grep -v "mtWavetableEnvelopes.cpp:206" | grep -q .; then
        echo "SANITIZER $1"; grep -E "runtime error|AddressSanitizer" "$WORK/$1.err" | grep -v "mtWavetableEnvelopes.cpp:206" | head -3
    fi
}
shot() { [ -n "$PNG_DIR" ] && echo "shot $PNG_DIR/$1.png" || echo "wait 1"; }
dump() { local t=$1 a=$2 b=$3 s; for ((s=a; s<=b; s++)); do printf 'step %d %d; stepv %d %d; ' "$t" "$s" "$t" "$s"; done; }
notes() { awk -v t="$2" '/^step /&&$2==t{printf "%s ", $5}' <<<"$1"; }   # notes of a track, in step order
note_at() { awk -v t="$2" -v s="$3" '/^step /&&$2==t&&$3==s{print $5}' <<<"$1"; }
vol_at() { awk -v t="$2" -v s="$3" '/^stepv /&&$2==t&&$3==s{print $5}' <<<"$1"; }
hitcount() { awk -v t="$2" '/^step /&&$2==t&&$5>=0{n++} END{print n+0}' <<<"$1"; }
all_notes() { awk -v t="$2" -v re="$3" '/^step /&&$2==t&&$5>=0{ if ($5 !~ re) bad++ } END{print (bad+0==0)}' <<<"$1"; }   # 1: every hit matches re
range_ok() { awk -v t="$2" -v lo="$3" -v hi="$4" '/^step /&&$2==t&&$5>=0{ if ($5<lo||$5>hi) bad++ } END{print (bad+0==0)}' <<<"$1"; }
in_scale() { awk -v t="$2" -v pcs="$3" '/^step /&&$2==t&&$5>=0{ pc=$5%12; if (index(" " pcs " ", " " pc " ")==0) bad++ } END{print (bad+0==0)}' <<<"$1"; }
ge() { [ "$1" -ge "$2" ] && echo 1 || echo 0; }
MAJOR="0 2 4 5 7 9 11"   # C major: the pad board root is C (note 24), scale list index 2

# what a hand does: edit mode, select the steps from the cursor, open the Fill, Fill Type = Role (3 detents from Constant)
SEL1="tap Rec; hold Shift; tap Down 15; unhold Shift"
SEL5="tap Rec; hold Shift; tap Down 15; tap Right 4; unhold Shift"
OPEN="tap F4; tap Right 3; enc 3"          # the dialog on Fill Type, switched to Role (the place stays on Type)
ROLE="tap Right"                           # Fill Type -> From slot, which is the role; then "enc N": 0 Kick 1 Snare 2 Hat 3 Bass 4 Lead 5 Auto

# 1. Kick: the downbeat is there, a hit keeps the drum note and gets a volume, the rest of the pattern is empty, other tracks stay
out=$(run kick "$SEL1; $OPEN; $(shot role-dialog-kick); tap F8; wait 300; $(dump 0 0 15) step 1 0; step 0 16; cursor")
expect "kick: the downbeat is a hit with the default drum note" "60" "$(note_at "$out" 0 0)"
expect "kick: the downbeat is at full volume" "100" "$(vol_at "$out" 0 0)"
truth "kick: every step is a hit (note 60) or empty (-1)" "$(all_notes "$out" 0 '^60$')"
expect "kick: a miss has no volume, a hit has 1..100" "1" "$(awk '/^step /&&$2==0{n[$3]=$5} /^stepv /&&$2==0{ if (n[$3]<0 && $5!=-1) bad++; if (n[$3]>=0 && ($5<1||$5>100)) bad++ } END{print (bad+0==0)}' <<<"$out")"
expect "kick: the track beside the selection is untouched" "-1" "$(awk '/^step 1 0 /{print $5}' <<<"$out")"
expect "kick: the step below the selection is untouched" "-1" "$(awk '/^step 0 16 /{print $5}' <<<"$out")"

# 2. Snare: the backbeat is there on every seed, never on the 1 or the 3
bad=0
for i in 1 2 3 4 5 6; do
    out=$(run snare$i "$SEL1; $OPEN; $ROLE; enc 1; tap F8; wait 300; $(dump 0 0 15)")
    [ "$(note_at "$out" 0 4)" = 60 ] && [ "$(note_at "$out" 0 12)" = 60 ] && [ "$(vol_at "$out" 0 4)" = 100 ] || bad=$((bad+1))
    [ "$(note_at "$out" 0 0)" = -1 ] && [ "$(note_at "$out" 0 8)" = -1 ] || bad=$((bad+1))
done
expect "snare: the backbeat (steps 5 and 13) at full volume, steps 1 and 9 empty, six applies" "0" "$bad"

# 3. Hat: a busy track
out=$(run hat "$SEL1; $OPEN; $ROLE; enc 2; $(shot role-dialog-hat); tap F8; wait 300; $(dump 0 0 15)")
truth "hat: at least 6 hits in 16 steps" "$(ge "$(hitcount "$out" 0)" 6)"
truth "hat: every hit has the drum note" "$(all_notes "$out" 0 '^60$')"

# 4. Bass in C major: the root on the downbeat, only scale notes, the sample track register (48..60)
out=$(run bass "$SEL1; $OPEN; $ROLE; enc 3; tap Left 2; enc 2; $(shot role-dialog-bass-major); tap F8; wait 300; $(dump 0 0 15)")
expect "bass: the downbeat is the root (C, one octave below the lead)" "48" "$(note_at "$out" 0 0)"
truth "bass: every note is in C major" "$(in_scale "$out" 0 "$MAJOR")"
truth "bass: every note is in 48..60" "$(range_ok "$out" 0 48 60)"
truth "bass: there is more than the downbeat" "$(ge "$(hitcount "$out" 0)" 3)"

# 5. Lead in C major: only scale notes, 60..79
out=$(run lead "$SEL1; $OPEN; $ROLE; enc 4; tap Left 2; enc 2; tap F8; wait 300; $(dump 0 0 15)")
truth "lead: every note is in C major" "$(in_scale "$out" 0 "$MAJOR")"
truth "lead: every note is in 60..79" "$(range_ok "$out" 0 60 79)"
truth "lead: at least 2 notes" "$(ge "$(hitcount "$out" 0)" 2)"

# 6. Auto over five tracks: kick, snare, hat, bass, lead in selection order
out=$(run auto "$SEL5; $OPEN; $ROLE; enc 5; $(shot role-dialog-auto); tap F8; wait 300; $(dump 0 0 15) $(dump 1 0 15) $(dump 2 0 15) $(dump 3 0 15) $(dump 4 0 15) step 5 0")
expect "auto: track 1 is a kick (downbeat, full volume)" "60 100" "$(note_at "$out" 0 0) $(vol_at "$out" 0 0)"
expect "auto: track 2 is a snare (backbeat, no downbeat)" "-1 60 60" "$(note_at "$out" 1 0) $(note_at "$out" 1 4) $(note_at "$out" 1 12)"
truth "auto: track 3 is a hat (6 or more hits)" "$(ge "$(hitcount "$out" 2)" 6)"
expect "auto: track 4 is a bass (the root, one octave below the lead)" "48" "$(note_at "$out" 3 0)"
truth "auto: track 5 is a lead (60..79, chromatic scale here)" "$(range_ok "$out" 4 60 79)"
expect "auto: the sixth track is untouched" "-1" "$(awk '/^step 5 0 /{print $5}' <<<"$out")"

# 7. What stays and what goes: a note inside the selection is replaced or cleared, its FX stay, a note outside stays, Undo restores
out=$(run keep "setstep 0 5 70 0 3 2 0 0; setstep 0 20 70 0 0 0 0 0; $SEL1; $OPEN; tap F8; wait 300; step 0 5; step 0 20; tap F6; wait 300; step 0 5; step 0 0")
l1=$(sed -n 1p <<<"$out")
truth "keep: step 6 is a hit (60) or empty, never the old 70" "$([[ "$l1" == "step 0 5 note 60 "* || "$l1" == "step 0 5 note -1 "* ]] && echo 1 || echo 0)"
truth "keep: the FX of step 6 stay (fx0 3:2)" "$([[ "$l1" == *"fx0 3:2 "* ]] && echo 1 || echo 0)"
expect "keep: a note below the selection stays" "step 0 20 note 70 instr 0 fx0 0:0 fx1 0:0" "$(sed -n 2p <<<"$out")"
expect "undo: the old note is back" "step 0 5 note 70 instr 0 fx0 3:2 fx1 0:0" "$(sed -n 3p <<<"$out")"
expect "undo: the downbeat kick is gone" "step 0 0 note -1 instr 0 fx0 0:0 fx1 0:0" "$(sed -n 4p <<<"$out")"

# 8. A note-off in the selection: a hit replaces it, a miss leaves it (note off -2)
out=$(run noteoff "setstep 0 0 -2 0 0 0 0 0; setstep 0 1 -2 0 0 0 0 0; $SEL1; $OPEN; tap F8; wait 300; step 0 0; step 0 1")
expect "note off: the kick downbeat replaces a note off" "step 0 0 note 60 instr 0 fx0 0:0 fx1 0:0" "$(sed -n 1p <<<"$out")"
expect "note off: a step without a hit keeps its note off" "step 0 1 note -2 instr 0 fx0 0:0 fx1 0:0" "$(sed -n 2p <<<"$out")"

# 9. Fill again gives another idea (hats, three applies), each Undo-able
out=$(run again "$SEL1; $OPEN; $ROLE; enc 2; tap F8; wait 300; $(dump 0 0 15); tap F4; tap F8; wait 300; $(dump 0 0 15); tap F4; tap F8; wait 300; $(dump 0 0 15)")
a=$(notes "$(sed -n 1,32p <<<"$out")" 0); b=$(notes "$(sed -n 33,64p <<<"$out")" 0); c=$(notes "$(sed -n 65,96p <<<"$out")" 0)
truth "again: the second apply is another pattern than the first" "$([ "$a" != "$b" ] && echo 1 || echo 0)"
truth "again: the third apply is another pattern than the second" "$([ "$b" != "$c" ] && echo 1 || echo 0)"

# 10. Density: 1 gives a near empty kick (no downbeat below 8), 128 a full hat bar
out=$(run dlow "$SEL1; $OPEN; tap Left 2; enc -63; tap F8; wait 300; $(dump 0 0 15)")
truth "density 1: at most 2 hits and no anchor" "$([ "$(hitcount "$out" 0)" -le 2 ] && [ "$(note_at "$out" 0 0)" = -1 ] && echo 1 || echo 0)"
out=$(run dhigh "$SEL1; $OPEN; $ROLE; enc 2; tap Left 3; enc 64; $(shot role-dialog-dense); tap F8; wait 300; $(dump 0 0 15)")
truth "density 128: at least 9 hats in 16 steps" "$(ge "$(hitcount "$out" 0)" 9)"

# 11. The other Fill Types are as before: Constant (Each, density 1) puts the pad board root note on every selected step
out=$(run constant "$SEL1; tap F4; tap F8; wait 300; $(dump 0 0 15)")
expect "constant: 16 steps of the pad board root note (24)" "24 24 24 24 24 24 24 24 24 24 24 24 24 24 24 24 " "$(notes "$out" 0)"
out=$(run backto "$SEL1; $OPEN; tap F8; wait 300; tap F4; enc -3; tap F8; wait 300; $(dump 0 0 15)")
expect "back to Constant after a role: 16 steps of the root note" "24 24 24 24 24 24 24 24 24 24 24 24 24 24 24 24 " "$(notes "$out" 0)"

# 12. The Instrument column has no Role (the Fill Type list stays at three, the encoder stops at Random)
out=$(run instr "$SEL1; tap Instr; tap F4; tap Right 2; enc 9; $(shot role-dialog-instr-column); tap F7; wait 300; $(dump 0 0 3)")
expect "instrument column: Cancel leaves the pattern as it was" "-1 -1 -1 -1 " "$(notes "$out" 0)"

# 13. One step selected, and a two bar selection (32 steps)
out=$(run one "tap Rec; tap F4; tap Right 3; enc 3; tap F8; wait 300; $(dump 0 0 1)")
expect "one step: the kick downbeat is there, the next step untouched" "60 -1 " "$(notes "$out" 0)"
out=$(run two "tap Rec; hold Shift; tap Down 31; unhold Shift; $OPEN; tap F8; wait 300; $(dump 0 0 31)")
expect "two bars: the downbeat of bar two is a kick too" "60 60" "$(note_at "$out" 0 0) $(note_at "$out" 0 16)"

printf '\n%d checks, %d failed\n' "$checks" "$fails"
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; else echo "RESULT: RED ($fails failing)"; exit 1; fi
