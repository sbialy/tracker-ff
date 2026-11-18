#!/bin/bash
# scale_snap_sample_sim.sh [EVIDENCE_DIR]
#
# Scale snap on a SAMPLE track, proved by the pitch that is heard (scale_snap_sim.sh only proves the stored setting and the MIDI notes).
# snapStepNote (core/source/mtSequencer.cpp) has no track-type gate, so a sequenced note of a sample track is snapped before the sample
# voice gets it, and a live pad or MIDI-in note is not (it never goes through the sequencer step path). The sim plays instrument 0 as a
# 441 Hz sine ("sampletone 0", original pitch at note 60, one semitone is x1.0595) on track 1 with the firmware audio callback ("audio 1")
# and measures the pitch at the sample voice itself ("audiostat tap T": the output of the track's sample player, zero crossings of the
# rendered blocks, the tap reads about 0.8 % low). It must be within 2 % of 441 * 2^((note - 60) / 12) where note is the note the scale snap
# should have produced (a semitone is 5.9 %, so a wrong note cannot pass). The master mix of the sim carries no sample voices (the WAV of
# "wav" is silent for sample tracks), so the voice tap is the measurement point, as in tape_fx_sim.sh.
# Scales: Penta Maj (18, C D E G A, root 24 = C): F (65) is between E and G and nearer E, B (71) is nearer C (72) than A (69), so Nearest,
# Down and Up give three different pitches. Major (2): C# (61) is a tie, Nearest goes down.
# Every run is on the plain ui_shot of this tree (build/ui-shot, the build run_tests.sh makes): BIN overrides the ui_shot binary. Host only: no device, every run has
# its own SD card folder. Snap FX: id 52, value 0 Off 1 Nearest 2 Down 3 Up 4 Track.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)". EVIDENCE_DIR keeps the WAVs, the block logs and a table.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
EVID="${1:-}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/scale_snap_sample_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
if [ -z "${BIN:-}" ]; then   # the plain ui_shot of this tree (build/ui-shot, the one run_tests.sh builds), no plugin needed
    BIN="$ROOT/build/ui-shot${SAN:+-san}/ui_shot"
    make -s -C "$HERE" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" ${SAN:+SAN=$SAN} >"$WORK/make.log" 2>&1 || { tail -30 "$WORK/make.log"; exit 2; }
fi
[ -x "$BIN" ] || { echo "scale_snap_sample_sim.sh: no ui_shot at $BIN" >&2; exit 99; }
[ -n "$EVID" ] && mkdir -p "$EVID"
KEEP="${EVID:-$WORK}"

fails=0; checks=0
check() { checks=$((checks+1)); if [ "$2" = ok ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
[ -n "$EVID" ] && : > "$EVID/table.txt"
# play NAME "setup script" -> $KEEP/NAME.blk (tap blocks of the sample voice of track TAPTRK, default 0) and $KEEP/NAME.out (everything else the script printed). Play 700 ms.
play() {
    local sd="$WORK/$1"; rm -rf "$sd"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; sampletone 0; instr 0; $2 audio 1; audiostat tap ${TAPTRK:-0}; audiostat 1; ${3:-tap Play;} wait 700; ${4:-tap Play;} audiostat 0; audiostat tapdump" \
        2>"$WORK/$1.err" >"$WORK/$1.raw"
    grep -E '^blk ' "$WORK/$1.raw" >"$KEEP/$1.blk"; grep -vE '^blk |^audiostat blocks|^wav ' "$WORK/$1.raw" >"$KEEP/$1.out"
}
# tap pitch: mean zero crossings of the blocks the voice sounds in (rms > 500), the first 10 blocks left out
hz_tap() { awk '$4 > 500 && NR > 10 {z += $6; n++} END {printf "%.1f", n ? z / 2 / (n * 128 / 44117.647) : 0}' "$KEEP/$1.blk"; }
# want NOTE: 441 * 2^((NOTE - 60) / 12)
want() { awk -v n="$1" 'BEGIN {printf "%.1f", 441 * 2 ^ ((n - 60) / 12)}'; }
near() { awk -v a="$1" -v b="$2" 'BEGIN {d = a - b; if (d < 0) d = -d; exit !(b > 0 && d <= b * 0.02)}'; }
# expect_note NAME LABEL NOTE: the run NAME played the pitch of step note NOTE (voice tap)
expect_note() {
    local t e; t=$(hz_tap "$1"); e=$(want "$3")
    [ -n "$EVID" ] && printf '%-34s want note %s %8s Hz   tap %8s Hz\n' "$1" "$3" "$e" "$t" >> "$EVID/table.txt"
    if near "$t" "$e"; then check "$2 (note $3, $e Hz)" ok; else check "$2 (note $3)" "want $e Hz, tap $t Hz"; fi
}

# 1. Penta Maj, sequenced notes on the sample track (track 1, instrument 0): mode Off keeps the written note, Nearest / Down / Up move it
#    "mode name F(65) B(71) C(60)" = the note each of the three written notes must play
while read -r mode name nf nb nc; do
    for pair in "f 65 $nf" "b 71 $nb" "c 60 $nc"; do
        set -- $pair
        play "penta-$name-$1" "scale 24 18; snap 0 $mode; setstep 0 0 $2 0 0 0 0 0;"
        expect_note "penta-$name-$1" "sample track, Penta Maj, $name: written $2 plays" "$3"
    done
done <<'T'
0 off 65 71 60
1 nearest 64 72 60
2 down 64 69 60
3 up 67 72 60
T
# 2. Major: C# (61) is a tie, Nearest and Down go to C, Up to D, Off keeps C#
while read -r mode name n; do
    play "major-$name" "scale 24 2; snap 0 $mode; setstep 0 0 61 0 0 0 0 0;"
    expect_note "major-$name" "sample track, Major, $name: written 61 plays" "$n"
done <<'T'
0 off 61
1 nearest 60
2 down 60
3 up 62
T
# 3. only the snapped track moves: track 1 Nearest, track 2 (instrument 1, same tone) Off, both play 65 at the same step. Track 2 alone on its run
play pertrack-t1 "scale 24 18; snap 0 1; sampletone 1; setstep 0 0 65 0 0 0 0 0; setstep 1 0 -1 1 0 0 0 0;"
expect_note "pertrack-t1" "track 1 (Nearest): 65 plays 64" 64
TAPTRK=1 play pertrack-t2 "scale 24 18; snap 0 1; sampletone 1; setstep 0 0 -1 0 0 0 0 0; setstep 1 0 65 1 0 0 0 0;"
expect_note "pertrack-t2" "track 2 (Off) shares the tone: 65 plays 65" 65
# 4. the pattern keeps the written note (the snap is at playback only)
play stored "scale 24 18; snap 0 1; setstep 0 0 65 0 0 0 0 0;" "tap Play;" "tap Play; step 0 0;"
check "pattern keeps the written note 65 on the sample track" "$([ "$(grep '^step' "$KEEP/stored.out")" = "step 0 0 note 65 instr 0 fx0 0:0 fx1 0:0" ] && echo ok || grep '^step' "$KEEP/stored.out")"
# 5. Snap step FX (id 52) on the sample track overrides the project mode, its own note included. Project mode Off unless noted.
#    "name project fxvalue written want"
while read -r name proj fxv wr n; do
    play "snapfx-$name" "scale 24 18; snap 0 $proj; setstep 0 0 $wr 0 52 $fxv 0 0;"
    expect_note "snapfx-$name" "Snap FX $fxv over project mode $proj: written $wr plays" "$n"
done <<'T'
nearest 0 1 65 64
down 0 2 71 69
up 0 3 65 67
up-b 0 3 71 72
off-over-up 3 0 65 65
track-over-up 3 4 65 67
track-over-off 0 4 65 65
fx-nearest-over-up 3 1 65 64
T
# 6. LIVE notes on the sample track are NOT snapped: a pad (pad 1 = note 73 = 933 Hz, C#, not in Penta Maj, Nearest would give C 72 = 882 Hz and
#    Down 72, Up 74 = 988 Hz) and a MIDI in note (midion 61 = 61 on the MIDI keyboard path) are heard as played in every mode
#    (pads and MIDI notes, "pad N" row of ui_shot: pad 0 = note 72, each pad one semitone up)
for m in "0 off" "1 nearest" "2 down" "3 up"; do
    set -- $m
    play "pad-$2" "scale 24 18; snap 0 $1;" "padon 1 700;" "padoff 1 40;"
    expect_note "pad-$2" "live pad 1 on the sample track, project mode $2: not snapped, plays 73" 73
done
play pad-ref5 "scale 24 18; snap 0 1;" "padon 5 700;" "padoff 5 40;"
expect_note "pad-ref5" "live pad 5 (note 77, F) with Nearest: not snapped, plays 77 not 76" 77
# the same step note IS snapped (negative control, from section 1): 65 under Nearest plays 64, so the pad test above can tell
t=$(hz_tap penta-nearest-f); p=$(hz_tap pad-ref5)
check "negative control: the same Nearest snaps a sequenced F (64) and leaves a live F alone (77)" "$(awk -v t="$t" -v p="$p" 'BEGIN {exit !(t < 600 && p > 1100)}' && echo ok || echo "tap $t Hz vs $p Hz")"
# 7. no sanitizer report in any run
errs=$(cat "$WORK"/*.err 2>/dev/null | grep -c "ERROR: AddressSanitizer\|runtime error" | tr -d ' ')
check "no sanitizer report in any run" "$([ "$errs" = 0 ] && echo ok || echo "$errs reports")"

echo
if [ "$fails" -eq 0 ]; then echo "$checks checks"; echo "RESULT: GREEN"; exit 0; fi
echo "$checks checks"; echo "RESULT: RED ($fails failing)"; exit 1
