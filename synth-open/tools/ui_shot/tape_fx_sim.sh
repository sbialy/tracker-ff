#!/bin/bash
# tape_fx_sim.sh [PNG_DIR]
#
# The Tape step FX (id 50: values 1-64 Stop, 65-128 Start, core/source/tapeFx.h) on the real firmware code of this tree (ui_shot) on the Mac. Host only, no
# device. The sim plays a pattern with the firmware's own audio callback (audio 1) and reads the audio back block by block (audiostat):
# the level (rms) and the pitch (zero crossings) of the sample voice of track 1 (a 441 Hz sine in instrument 1, "sampletone", read by
# "audiostat tap 0" at the voice itself) and of the final mix for the Poly synth part (ACD note 60, about 259 Hz). Tempo 120, one step
# is 125 ms = 43 audio blocks of 128 frames. Windows below are in blocks from Play.
#   1-4  sample voice: Tape Stop and Tape Start on a note, on an empty step while a note rings, and after a Stop the next note is normal
#   5-8  Poly synth part: the same
#   9    a Tape Stop on a part that sounds nothing does not touch the next note
#   10   grid letters and labels of the one FX (W for 1-64, Z for 65-128, the cell text), the retired id 51 is a hole
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)". SAN=address,undefined: the sanitizer build.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="$BUILD/ui_shot"
PNG_DIR="${1:-}"
[ -n "$PNG_DIR" ] && mkdir -p "$PNG_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/tape_fx_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
make -s -C "$HERE" -j4 ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }

fails=0; checks=0
check() { checks=$((checks+1)); if [ "$2" = "ok" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
# play SECONDS_MS "setup script" tap|mix  ->  the block lines
play() {
    local sd="$WORK/$1"; rm -rf "$sd"
    local src=tapdump; [ "$4" = mix ] && src=dump
    local tap=""; [ "$4" = tap ] && tap="audiostat tap 0;"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$sd" "wait 500; $3 audio 1; $tap audiostat 1; tap Play; wait $2; tap Play; audiostat 0; audiostat $src" 2>"$WORK/$1.err" | grep -E '^blk '
}
# window FILE A B -> "rms freq"   (mean level, pitch in Hz from the crossings of blocks A..B)
window() { python3 -I - "$1" "$2" "$3" <<'PY'
import sys
rows=[l.split() for l in open(sys.argv[1])]
a,b=int(sys.argv[2]),int(sys.argv[3])
w=[(float(r[3]),int(r[5])) for r in rows[a:b+1]]
n=len(w)
print("%.0f %.0f" % (sum(r for r,_ in w)/n, sum(z for _,z in w)/2/(n*128/44117.647)))
PY
}
# expect NAME FILE A B RMS_MIN RMS_MAX FREQ_MIN FREQ_MAX
expect() {
    local out; out=$(window "$2" "$3" "$4"); local r=${out% *} f=${out#* }
    if [ "$r" -ge "$5" ] && [ "$r" -le "$6" ] && [ "$f" -ge "$7" ] && [ "$f" -le "$8" ]; then check "$1" ok
    else check "$1" "blocks $3-$4: rms $r (want $5..$6) pitch $f Hz (want $7..$8)"; fi
}

S="sampletone 0; instr 0;"
# 1. sample: note + Tape Stop (value 8 = 2 steps = 86 blocks), the note of step 8 is normal again
play s1 1800 "$S setstep 0 0 60 0 50 8 0 0; setstep 0 8 60 0 0 0 0 0;" tap > "$WORK/s1.txt"
expect "sample stop: starts at normal speed and level" "$WORK/s1.txt" 0 5 3500 6000 380 480
expect "sample stop: slower half way" "$WORK/s1.txt" 36 45 3000 6000 150 330
expect "sample stop: nearly stopped and quiet near the end" "$WORK/s1.txt" 70 79 0 1800 0 130
expect "sample stop: silent after the run" "$WORK/s1.txt" 90 300 0 5 0 0
expect "sample stop: the next note is at normal speed" "$WORK/s1.txt" 380 430 3500 6000 380 480
# 2. sample: note + Tape Start (value 64 + 8 = 72)
play s2 1200 "$S setstep 0 0 60 0 50 72 0 0;" tap > "$WORK/s2.txt"
expect "sample start: starts slow and quiet" "$WORK/s2.txt" 0 5 0 2500 0 130
expect "sample start: faster half way" "$WORK/s2.txt" 36 45 3000 6000 150 330
expect "sample start: normal speed and level after the run" "$WORK/s2.txt" 110 150 3500 6000 380 480
# 3. sample: a ringing note (step 0), Tape Stop 4 (1 step = 43 blocks) on the empty step 4 (block 172)
play s3 1800 "$S setstep 0 0 60 0 0 0 0 0; setstep 0 4 -1 0 50 4 0 0;" tap > "$WORK/s3.txt"
expect "sample stop on an empty step: the note rings before" "$WORK/s3.txt" 100 160 3500 6000 380 480
expect "sample stop on an empty step: silent after the run" "$WORK/s3.txt" 225 330 0 5 0 0
# 4. sample: Tape Start on an empty step does nothing to a ringing note
play s4 1200 "$S setstep 0 0 60 0 0 0 0 0; setstep 0 4 -1 0 50 68 0 0;" tap > "$WORK/s4.txt"
expect "sample start on an empty step: the note keeps ringing" "$WORK/s4.txt" 175 330 3500 6000 380 480
# 5. synth: note + Tape Stop, normal note at step 12
play y1 2200 "instr 64; setstep 0 0 60 64 50 8 0 0; setstep 0 12 60 64 0 0 0 0;" mix > "$WORK/y1.txt"
expect "synth stop: starts at normal speed and level" "$WORK/y1.txt" 10 25 4500 8000 190 300
expect "synth stop: slower and quieter half way" "$WORK/y1.txt" 42 53 3500 6500 100 200
expect "synth stop: nearly stopped" "$WORK/y1.txt" 72 83 0 1500 0 100
expect "synth stop: silent after the run" "$WORK/y1.txt" 90 400 0 5 0 0
expect "synth stop: the next note is at normal speed" "$WORK/y1.txt" 500 560 4500 8000 220 300
# 6. synth: note + Tape Start
play y2 1200 "instr 64; setstep 0 0 60 64 50 72 0 0;" mix > "$WORK/y2.txt"
expect "synth start: starts slow and quiet" "$WORK/y2.txt" 0 7 0 2500 0 120
expect "synth start: normal after the run" "$WORK/y2.txt" 110 150 4500 8000 220 300
# 7. synth: ringing note, Tape Stop 4 on the empty step 4
play y3 1800 "instr 64; setstep 0 0 60 64 0 0 0 0; setstep 0 4 -1 64 50 4 0 0;" mix > "$WORK/y3.txt"
expect "synth stop on an empty step: rings before" "$WORK/y3.txt" 100 160 4500 8000 220 300
expect "synth stop on an empty step: silent after the run" "$WORK/y3.txt" 225 330 0 5 0 0
# 8. synth: Tape Start over a ringing note (the legato note takes the run)
play y4 1200 "instr 64; setstep 0 0 60 64 0 0 0 0; setstep 0 4 60 64 50 72 0 0;" mix > "$WORK/y4.txt"
expect "synth start over a ringing note: rings before" "$WORK/y4.txt" 100 160 4500 8000 220 300
expect "synth start over a ringing note: slow again at the note" "$WORK/y4.txt" 172 180 0 4500 0 200
expect "synth start over a ringing note: normal after the run" "$WORK/y4.txt" 290 340 4500 8000 220 300
# 9. a Tape Stop on a part that sounds nothing is dropped: the note of step 8 plays normally
play y5 1500 "instr 64; setstep 0 2 -1 64 50 8 0 0; setstep 0 8 60 64 0 0 0 0;" mix > "$WORK/y5.txt"
expect "idle part: the Tape Stop does not touch the next note" "$WORK/y5.txt" 360 420 4500 8000 220 300
# 10. grid: the one FX, id 50 (list entry "W - Tape"): the letter follows the value (W 1-64 Stop, Z 65-128 Start), the cell text is the run length
# (value, or value - 64 for a Start, right aligned in 3 characters). Values above 128 are reserved and read as 128. The retired id 51 is not in the list: a hole ("----").
out=$(ASAN_OPTIONS=detect_leaks=0 "$BIN" "$WORK/grid" "wait 500; instr 64; setstep 0 0 60 64 50 8 0 0; setstep 0 1 60 64 50 70 0 0; setstep 0 2 60 64 50 1 0 0; setstep 0 3 60 64 50 64 0 0; setstep 0 4 60 64 50 65 0 0; setstep 0 5 60 64 50 128 0 0; setstep 0 6 60 64 50 200 0 0; setstep 0 7 60 64 51 8 0 0; step 0 0; step 0 1; grid 0 0; grid 0 1; grid 0 2; grid 0 3; grid 0 4; grid 0 5; grid 0 6; grid 0 7; tap Pattern; wait 300; ${PNG_DIR:+shot $PNG_DIR/tape-grid.png;} wait 1" 2>/dev/null | grep -E '^(step|grid)')
check "grid: the FX ids and values are kept" "$([ "$(sed -n 1p <<<"$out")" = "step 0 0 note 60 instr 64 fx0 50:8 fx1 0:0" ] && [ "$(sed -n 2p <<<"$out")" = "step 0 1 note 60 instr 64 fx0 50:70 fx1 0:0" ] && echo ok || echo "$out")"
gridcell() { grep "^grid 0 $1 " <<<"$out" | sed 's/.* fx0 \[\([^]]*\)\].*/\1/'; }
for row in "0 W__2" "1 Z1.5" "2 W1/4" "3 W_16" "4 Z1/4" "5 Z_16" "6 Z_16" "7 ----"; do
    set -- $row
    want="${2//_/ }"
    got=$(gridcell "$1")
    check "grid: step $1 cell reads '$want'" "$([ "$got" = "$want" ] && echo ok || echo "got '$got'")"
done

echo
if [ "$fails" -eq 0 ]; then echo "$checks checks"; echo "RESULT: GREEN"; else echo "$checks checks"; echo "RESULT: RED ($fails failing)"; exit 1; fi
