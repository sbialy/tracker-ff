#!/bin/bash
# stereo_fx_sweep.sh [BIN]
#
# Linked stereo pair (core/source/stereoPairs.h) against two unlinked mono tracks, one render per step FX and per play mode, on the real firmware code of
# this tree (ui_shot, host only). A pair with a stereo sample (L tone 441 Hz, R tone 882 Hz) and an effect X must give
#   - a left bus equal to a plain mono track that plays 441 Hz with X ("L"),
#   - a right bus equal to a plain mono track that plays 882 Hz with X ("R"),
# and X written on the FOLLOWER row of the pair (a note-less step, the pair's notes without X) must change nothing ("F", round 2 D1).
# Each row is rendered to WAV through the firmware's own audio callback (wav PATH, 1500 ms of Play) and compared per channel in 16 bit LSB.
# The R chain of a stereo sample starts at the frame the L chain snapped to (play_memory*.cpp: samplePointsSource, stock behaviour, both channels of a stereo
# sample must start on the same frame to stay phase locked), while a mono track snaps on its own data. With a different tone on R the two snaps land on
# different frames (441 / 882 Hz, Position 40: L 70600, mono 882 Hz 70575, 25 frames), so the main sweep runs with the project's snap to zero off (snapzero 0,
# every chain starts on the same unsnapped frame, bit exact against the mono tones). The "snap on" section at the end turns it on again for Position, Slice and
# Beat slice with a stereo sample whose R differs from L in level only (441 Hz, 8000 / 4000: the same zero crossings, so the mono track snaps on the same frame).
# Columns: maxL maxR = largest difference of the left bus vs the mono 441 Hz track and of the right bus vs the mono 882 Hz track, nL nR = samples
# that differ, F = the follower row render is byte identical to the pair without the effect, rms = level of the pair's L / R bus (a silent render proves
# nothing). Verdict OK when maxL and maxR are <= TOL (default 2 LSB) and F is yes. The effects that cannot be compared are listed as n/a with the reason
# (the sends are mono collapsed by design, the Random FX draw per voice).
# Usage:  bash synth-open/tools/ui_shot/stereo_fx_sweep.sh [BIN]     (TOL=2, JOBS=4, KEEP=1 keeps the WAVs, TABLE=path writes the table)
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n rows)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="${1:-$ROOT/build/ui-shot/ui_shot}"
TOL="${TOL:-2}"
JOBS="${JOBS:-4}"
[ -x "$BIN" ] || { echo "stereo_fx_sweep.sh: no $BIN (make -C synth-open/tools/ui_shot builds it)" >&2; exit 2; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/stereo_fx_sweep.XXXXXX")"
[ -n "${KEEP:-}" ] || trap 'rm -rf "$WORK"' EXIT
render() {   # NAME SETUP: 1500 ms of Play into $WORK/NAME.wav
    local sd="$WORK/sd_$1"; rm -rf "$sd"
    ASAN_OPTIONS=detect_leaks=0 "$BIN" "$sd" "wait 500; snapzero ${SNAPV:-0}; $2 audio 1; wav $WORK/$1.wav; tap Play; wait 1500; tap Play; wav off" >/dev/null 2>"$WORK/$1.err"
    rm -rf "$sd"
}
# row ID FXTYPE FXVALUE MODE: the four renders of one row (the notes are on steps 0 and 6 of track 1, the effect on both)
PAIR_S="samplestereo 0 441 882 8000;"
MONOL_S="sampletonehz 0 441 8000;"
MONOR_S="sampletonehz 0 882 8000;"
NOTES() { echo "setstep 0 0 60 0 $1 $2 0 0; setstep 0 6 64 0 $1 $2 0 0;"; }
row() {
    local id="$1" fx="$2" val="$3" mode="${4:-0}"
    local pm=""; [ "$mode" != 0 ] && pm="playmode 0 $mode;"
    render "p_$id"  "pair 1; $PAIR_S $pm instr 0; $(NOTES $fx $val)" &
    render "l_$id"  "$MONOL_S $pm instr 0; $(NOTES $fx $val)" &
    render "r_$id"  "$MONOR_S $pm instr 0; $(NOTES $fx $val)" &
    # the follower row: the pair's notes WITHOUT the effect, the effect alone on track 2 (a note-less step 0 and 6)
    render "s_$id"  "pair 1; samplestereo 0 441 441 8000; $pm instr 0; $(NOTES $fx $val)" &
    # a play mode row has no FX on the leader: the follower row carries a Tune (40 100) instead and the base is the pair's own render
    local ffx="$fx" fval="$val"; [ "$mode" != 0 ] && { ffx=40; fval=100; }
    render "f_$id"  "pair 1; $PAIR_S $pm instr 0; $(NOTES 0 0) setstep 1 0 -1 0 $ffx $fval 0 0; setstep 1 6 -1 0 $ffx $fval 0 0;" &
    wait
}
cmpwav() {   # P L R S F BASE -> "maxL maxR nL nR F rmsL rmsR maxS"
python3 -I - "$@" <<'PY'
import sys, struct
def rd(p):
    d = open(p, 'rb').read()
    n = (len(d) - 44) // 4
    x = struct.unpack('<%dh' % (n * 2), d[44:44 + n * 4])
    return x[0::2], x[1::2]
try:
    pl, pr = rd(sys.argv[1]); ml, _ = rd(sys.argv[2]); _, mr = rd(sys.argv[3])
    sl, sr = rd(sys.argv[4]); _, mr2 = rd(sys.argv[2])
    fb = open(sys.argv[5], 'rb').read(); base = open(sys.argv[6], 'rb').read()
except Exception as e:
    print('error', e); sys.exit(0)
n = min(len(pl), len(ml), len(mr))
dl = [abs(a - b) for a, b in zip(pl[:n], ml[:n])]; dr = [abs(a - b) for a, b in zip(pr[:n], mr[:n])]
rms = lambda s: int((sum(v * v for v in s) / max(len(s), 1)) ** 0.5)
# S: the pair with L == R content (441 / 441) against the mono 441 track on both buses: what is left is the playback logic without the zero crossing snap
ds = [abs(a - b) for a, b in zip(sr[:n], mr2[:n])] + [abs(a - b) for a, b in zip(sl[:n], ml[:n])]
print(max(dl), max(dr), sum(1 for v in dl if v), sum(1 for v in dr if v), 'yes' if fb == base else 'NO', rms(pl[:n]), rms(pr[:n]), max(ds))
PY
}
fails=0; rows=0
TABLE="${TABLE:-$WORK/table.txt}"
: >"$TABLE"
printf '%-26s %5s %5s %7s %7s %3s %7s %7s  %s\n' "row" "maxL" "maxR" "nL" "nR" "F" "rmsL" "rmsR" "verdict" | tee -a "$TABLE"
# the baseline of the follower rows: the pair's notes with no effect at all
render "base" "pair 1; $PAIR_S instr 0; $(NOTES 0 0)"
report() {   # ID LABEL
    local base="$WORK/base.wav"; [ "${SNAPV:-0}" = 1 ] && base="$WORK/base_sn.wav"; [ "${3:-}" = mode ] && base="$WORK/p_$1.wav"
    local out; out=$(cmpwav "$WORK/p_$1.wav" "$WORK/l_$1.wav" "$WORK/r_$1.wav" "$WORK/s_$1.wav" "$WORK/f_$1.wav" "$base")
    set -- "$2" $out
    local label="$1" ml="${2:-x}" mr="${3:-x}" nl="${4:-x}" nr="${5:-x}" f="${6:-x}" rl="${7:-0}" rr="${8:-0}" ms="${9:-x}" verdict
    rows=$((rows+1))
    if [ "$ml" = error ] || [ "$ml" = x ]; then verdict="RED (no render: $out)"; fails=$((fails+1))
    elif [ "$rl" -lt 100 ] || [ "$rr" -lt 100 ]; then verdict="RED (silent render)"; fails=$((fails+1))
    elif [ "$ml" -le "$TOL" ] && [ "$mr" -le "$TOL" ] && [ "$f" = yes ]; then verdict=OK
    else verdict="RED"; fails=$((fails+1)); fi
    printf '%-26s %5s %5s %7s %7s %3s %7s %7s  %s\n' "$label" "$ml" "$mr" "$nl" "$nr" "$f" "$rl" "$rr" "$verdict" | tee -a "$TABLE"
}
# the step FX of mtSequencer.h (id value): name
FXS=(
"27 40 LowPass" "28 60 HighPass" "29 50 BandPass" "38 80 Overdrive" "39 6 BitDepth" "40 100 Tune" "41 40 SlideUp" "42 40 SlideDown"
"19 30 Glide" "22 40 Position" "26 1 Reverse" "25 3 Slice" "3 4 Roll" "23 60 VolumeLFO" "24 60 PanLFO" "33 60 FinetuneLFO" "35 60 FilterLFO"
"18 50 Volume" "31 20 Pan" "31 80 Pan" "2 30 Micromove" "17 20 Swing"
)
batch=0
ids=()
for e in "${FXS[@]}"; do
    set -- $e; id="fx$1_$2"; ids+=("$id|$1|$2|$3")
done
run_rows() {   # entries of "id|fx|val|label|mode"
    local i=0
    for e in "$@"; do
        IFS='|' read -r id fx val label mode <<<"$e"
        row "$id" "$fx" "$val" "${mode:-0}" &
        i=$((i+1)); if [ $((i % (JOBS > 4 ? JOBS / 4 : 1))) -eq 0 ]; then wait; fi
    done
    wait
}
echo "## step FX (one shot)"
run_rows "${ids[@]}"
for e in "${ids[@]}"; do IFS='|' read -r id fx val label mode <<<"$e"; report "$id" "FX $fx:$val $label"; done
MODES=(
"m1|0|0|LoopForward|1" "m2|0|0|LoopBackward|2" "m3|0|0|Pingpong|3" "m4|0|0|Slice|4" "m5|0|0|BeatSlice|5" "m6|0|0|Wavetable|6" "m7|0|0|Granular|7"
)
echo "## play modes (no FX)"
run_rows "${MODES[@]}"
for e in "${MODES[@]}"; do IFS='|' read -r id fx val label mode <<<"$e"; report "$id" "mode $mode $label" mode; done
echo "## snap to zero ON (the R chain follows the L chain's snapped start, R differs from L in level only)"
SNAPV=1
PAIR_S="samplestereo 0 441 441 8000 ampr=4000;"
MONOR_S="sampletonehz 0 441 4000;"
render "base_sn" "pair 1; $PAIR_S instr 0; $(NOTES 0 0)"
SNAPROWS=("sn_pos|22|40|Position|0" "sn_m4|0|0|Slice|4" "sn_m5|0|0|BeatSlice|5")
run_rows "${SNAPROWS[@]}"
for e in "${SNAPROWS[@]}"; do IFS='|' read -r id fx val label mode <<<"$e"
    if [ "$mode" = 0 ]; then report "$id" "snap on FX $fx:$val $label"; else report "$id" "snap on mode $mode $label" mode; fi; done
cat <<'NA'
## not compared (by design)
Delay send 30 and Reverb send 32: both chains go into one mono send mixer at half gain, the tails differ from a mono track by rounding (stereo_pairs_sim.sh section 2 holds the 16 LSB check).
Random Note 5, Random Instrument 6, Random Volume 7, Chance 4, Condition: one draw per step on the leader row, covered by section 9 of stereo_pairs_sim.sh (amp LFO random).
NA
echo
echo "$rows rows"
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; else echo "RESULT: RED ($fails rows)"; exit 1; fi
