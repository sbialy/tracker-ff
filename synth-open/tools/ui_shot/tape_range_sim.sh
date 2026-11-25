#!/bin/bash
# tape_range_sim.sh [EVIDENCE_DIR]
#
# Every value of the Tape step FX (id 50, W/Z): 1-64 a Stop, 65-128 a Start of v - 64 quarter steps, measured: the length of the run from the
# block levels of the firmware's own audio callback ("audio 1", "audiostat") against the expected length (tapeFx.h: the run is v quarters of a
# step for a Stop and v - 64 for a Start, 0 reads as 1, above 128 reads as 128, lengthFrames = floor(ms * 44.1176)). Two kinds of track: a
# SAMPLE track (instrument 0 = the 441 Hz "sampletone", level read at the sample voice, "audiostat tap 0": the path
# mtAudioNoteOnHandle.cpp for a note and mtAudioSequencer.cpp fxTape for an empty step) and a Poly synth track (ACD, instrument 64, level of
# the final mix). Values 0, 1..128, 129, 200, 255 at tempo 120 (a step is 125 ms), then 1 2 3 5 17 33 64 (Stop) and 65 66 67 69 81 97 128
# (Start) at tempo 70 and 174 on the sample track (the rounding of quarter steps at other tempos), and a Tape Stop on an EMPTY step while a
# note rings (the second path). tape_range_check.py turns the blocks into the length. Tolerances (tol): sample stop 7.5 ms (a stop is cut at
# a block end of 2.9 ms), sample start 15 ms or 9 % (zero crossings of a very slow sine read a little high, the speed curve itself is
# linear: 10 bins of the run read within 0.03 of the model), synth stop 15 ms (the part ends its voices a few blocks after the run), synth
# start 25 ms or 10 % (the ACD tone has harmonics, its crossings are noisy). On top of the length: the lengths of 1..64 must grow with the
# value and so must those of 65..128 (no dead zone, no repeat), 0 must equal 1 and 129 200 255 must equal 128 (clamps).
# BIN overrides the ui_shot binary (default: the plain build of this tree, build/ui-shot). Host only. Ends with RESULT: GREEN/RED.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
EVID="${1:-}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/tape_range_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
if [ -z "${BIN:-}" ]; then   # the plain ui_shot of this tree (build/ui-shot, the one run_tests.sh builds), no plugin needed
    BIN="${UI_SHOT_BIN:-$ROOT/build/ui-shot${SAN:+-san}/ui_shot}"
    [ -n "${UI_SHOT_BIN:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$WORK/make.log" 2>&1 || { tail -30 "$WORK/make.log"; exit 2; }
fi
[ -x "$BIN" ] || { echo "tape_range_sim.sh: no ui_shot at $BIN" >&2; exit 99; }
[ -n "$EVID" ] && mkdir -p "$EVID"
KEEP="${EVID:-$WORK}"
JOBS="${JOBS:-$(sh "$HERE/../jobs.sh" n 4)}"

# one run: run KIND FX VALUE TEMPO -> $KEEP/KIND-FX-VALUE-tTEMPO.blk
run1() {
    local kind=$1 fx=$2 v=$3 tempo=$4 name="$1-$2-$3-t$4" setup src tapcmd="" ms
    ms=$(awk -v v="$v" -v t="$tempo" 'BEGIN {if (v < 1) v = 1; if (v > 128) v = 128; if (v > 64) v -= 64; printf "%d", v * 0.25 * 15000 / t + 450}')
    if [ "$kind" = sample ]; then setup="sampletone 0; instr 0; setstep 0 0 60 0 $fx $v 0 0;"; src=tapdump; tapcmd="audiostat tap 0;"
    elif [ "$kind" = synth ]; then setup="instr 64; setstep 0 0 60 64 $fx $v 0 0;"; src=dump
    else setup="sampletone 0; instr 0; setstep 0 0 60 0 0 0 0 0; setstep 0 4 -1 0 $fx $v 0 0;"; src=tapdump; tapcmd="audiostat tap 0;"; ms=$((ms + 600)); fi   # empty: a note rings, the FX on the empty step 4
    rm -rf "$WORK/sd-$name"
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/sd-$name" "wait 500; tempo $tempo; $setup audio 1; $tapcmd audiostat 1; tap Play; wait $ms; tap Play; audiostat 0; audiostat $src" 2>/dev/null | grep -E '^blk ' > "$KEEP/$name.blk"
}
sweep() {   # sweep KIND FX "values" TEMPO
    local k=$1 fx=$2 t=$4 v
    for v in $3; do echo "$k $fx $v $t"; done
}
{
  fx=50
  allv="0 $(seq -s' ' 1 128) 129 200 255"
  sweep sample $fx "$allv" 120
  sweep synth $fx "$allv" 120
  sweep sample $fx "1 2 3 5 17 33 64 65 66 67 69 81 97 128" 70
  sweep sample $fx "1 2 3 5 17 33 64 65 66 67 69 81 97 128" 174
  sweep empty 50 "1 4 16 64" 120
} > "$WORK/jobs.txt"
export -f run1; export BIN WORK KEEP
xargs -P "$JOBS" -L 1 bash -c 'run1 "$@"' _ < "$WORK/jobs.txt"

fails=0; checks=0
check() { checks=$((checks+1)); if [ "$2" = ok ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
# the measured length of every run, one line per job: kind fx value tempo expected measured
: > "$KEEP/table.txt"
printf 'kind   fx value tempo  expected_ms measured_ms  err_ms\n' > "$KEEP/table.txt"
while read -r kind fx v t; do
    name="$kind-$fx-$v-t$t"
    mode=stop; [ "$v" -gt 64 ] && mode=start
    eff=$v; [ "$eff" -lt 1 ] && eff=1; [ "$eff" -gt 128 ] && eff=128; [ "$eff" -gt 64 ] && eff=$((eff - 64))
    exp=$(awk -v v="$eff" -v t="$t" 'BEGIN {ms = v * 0.25 * 15000 / t; printf "%.1f", int(ms * 44.1176470588) / 44.1176470588}')
    got=$(python3 -I "$HERE/tape_range_check.py" "$KEEP/$name.blk" "$mode" 5 | cut -d' ' -f1)
    # the Stop on an empty step starts at step 4 (500 ms at tempo 120): measure from that moment
    [ "$kind" = empty ] && got=$(awk -v g="$got" 'BEGIN {printf "%.1f", g - 500}')
    printf '%-6s %2s %4s %4s  %9s %11s %7.1f\n' "$kind" "$fx" "$v" "$t" "$exp" "$got" "$(awk -v e="$exp" -v g="$got" 'BEGIN {print g - e}')" >> "$KEEP/table.txt"
done < "$WORK/jobs.txt"
res=$(python3 -I - "$KEEP/table.txt" <<'PY'
import sys, collections
rows = [l.split() for l in open(sys.argv[1])][1:]
g = collections.defaultdict(dict)
bad = []; n = 0
def tol(kind, start, e):
    if not start and kind in ("sample", "empty"): return 7.5
    if kind == "sample": return max(15.0, 0.09 * e)
    if not start: return 15.0
    return max(25.0, 0.10 * e)
for k, fx, v, t, e, m, d in rows:
    v = int(v); e = float(e); m = float(m); n += 1
    g[(k, fx, t)][v] = m
    if abs(m - e) > tol(k, v >= 65, e): bad.append("%s value %d tempo %s: expected %.1f measured %.1f" % (k, v, t, e, m))
print("LEN", n, len(bad))
for b in bad[:12]: print("LENBAD", b)
for key, d in sorted(g.items()):
    if 64 not in d or 0 not in d: continue
    k, fx, t = key
    # distinct and growing: each value longer than the one before (sample stop is exact: one step), the others over two steps (noise)
    for part, lo, hi in (("stop", 1, 64), ("start", 65, 128)):
        gap = 1 if (k == "sample" and part == "stop") else 2
        flat = [v for v in range(lo, hi + 1 - gap) if not d[v + gap] > d[v]]
        print("MONO", k, part, t, len(flat), flat[:8])
    cl = [d[0] - d[1]] + [d[x] - d[128] for x in (129, 200, 255)]
    print("CLAMP", k, fx, t, " ".join("%.1f" % c for c in cl))
PY
)
echo "$res" | grep '^LENBAD'
read -r _ nruns nbad <<<"$(grep '^LEN ' <<<"$res")"
check "all $nruns runs: the measured run length is the expected one within the tolerance" "$([ "$nbad" = 0 ] && echo ok || echo "$nbad outside")"
while read -r _ kind part t nflat list; do
    check "$kind $part tempo $t: every value of the range is longer than the one before it (no dead zone)" "$([ "$nflat" = 0 ] && echo ok || echo "flat at $nflat values: $list")"
done < <(grep '^MONO' <<<"$res")
while read -r _ kind fx t c0 c129 c200 c255; do
    check "$kind tempo $t: value 0 reads as 1 and 129 200 255 read as 128 (clamps)" "$(awk -v a="$c0" -v b="$c129" -v c="$c200" -v e="$c255" 'BEGIN {m = 2.0; ok = 1; split(a " " b " " c " " e, x, " "); for (i = 1; i <= 4; i++) {y = x[i] < 0 ? -x[i] : x[i]; if (y > m) ok = 0}; print ok ? "ok" : "differs: " a " " b " " c " " e}')"
done < <(grep '^CLAMP' <<<"$res")
echo
echo "$checks checks ($nruns runs, table: $KEEP/table.txt)"
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fails failing)"; exit 1
