#!/bin/bash
# stereo_sends_sim.sh BIN [BASE_BIN]
#
# FEATURE_STEREO_SENDS (core/libs/Audio/stereo_sends.h, poly-stereo-io step 3): the stereo input of the delay and the reverb on the real firmware code
# of this tree (ui_shot built with STEREO_IO=FEATURE_STEREO_SENDS, BASE_BIN = the same tree with the flag off). Host only, no device.
#   1  no pair linked: the render of the new firmware is byte identical to the flag off build (reverb, delay mono and delay ping pong, all eight
#      tracks with sends), and link then unlink gives the unlinked render back
#   2  a mono sample on a linked pair: within 16 LSB of the flag off build with the same pair (the side is 0 to the mixer rounding, +-1 LSB), and within 16 LSB of the
#      centred mono track as before
#   3  a stereo sample (L 441 Hz, R 882 Hz) on a linked pair with a reverb send: the L bus tail keeps more 441 Hz than the R bus, the R bus more
#      882 Hz (flag off: the two buses carry the same mix). Numbers are printed
#   4  the same with the delay in ping pong: the left line is fed with the left channel and the right line with the right channel (delay send: step FX 30)
#   5  the reverb and delay tails still end at exactly 0 (the quiet detectors sleep) after a stereo take. FX mute is not driven here
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="${1:-$ROOT/build/ui-shot-sio-STEREO_SENDS/ui_shot}"
BASE_BIN="${2:-${BASE_BIN:-$ROOT/build/ui-shot/ui_shot}}"
[ -x "$BIN" ] || { echo "stereo_sends_sim.sh: no $BIN" >&2; exit 2; }
[ -x "$BASE_BIN" ] || { echo "stereo_sends_sim.sh: no BASE_BIN $BASE_BIN (the flag off ui_shot)" >&2; exit 2; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/stereo_sends_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
fails=0; checks=0
check() { checks=$((checks+1)); if [ "$2" = "ok" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
# wav BIN NAME SETUP [TAILMS]: 1500 ms of Play, then TAILMS (default 0) more after the stop, into $WORK/NAME.wav
wav() {
    local sd="$WORK/sd_$2"; rm -rf "$sd"
    ASAN_OPTIONS=detect_leaks=0 "$1" "$sd" "wait 500; $3 audio 1; wav $WORK/$2.wav; tap Play; wait 1500; tap Play; wait ${4:-0}; wav off" >/dev/null 2>"$WORK/$2.err"
}
same() { if cmp -s "$WORK/$1.wav" "$WORK/$2.wav" && [ -s "$WORK/$1.wav" ]; then check "$3" ok; else check "$3" "$1.wav and $2.wav differ ($(cmp "$WORK/$1.wav" "$WORK/$2.wav" 2>&1 | head -1))"; fi; }
wavdiff() { python3 -I - "$WORK/$1.wav" "$WORK/$2.wav" <<'PY'
import sys, struct
a = open(sys.argv[1], 'rb').read()[44:]; b = open(sys.argv[2], 'rb').read()[44:]
n = min(len(a), len(b)) // 2
x = struct.unpack('<%dh' % n, a[:n * 2]); y = struct.unpack('<%dh' % n, b[:n * 2])
d = [abs(p - q) for p, q in zip(x, y)]
print(sum(1 for v in d if v), max(d) if d else 0, len(a) == len(b) and n > 0)
PY
}
within() { local out; out=$(wavdiff "$1" "$2"); set -- $out "$3" "$4"
    local cnt=$1 mx=$2 ok=$3 lim=$4 name=$5
    if [ "$ok" = True ] && [ "$mx" -le "$lim" ]; then check "$name (max difference $mx LSB, $cnt samples differ)" ok; else check "$name" "max difference $mx LSB (limit $lim), $cnt samples differ, same length $ok"; fi; }
# tone WAV T0 T1: "L441 L882 R441 R882" = the Goertzel magnitudes of the left and right channel between T0 and T1 seconds
tone() { python3 -I - "$WORK/$1.wav" "$2" "$3" <<'PY'
import sys, struct, math
d = open(sys.argv[1], 'rb').read()[44:]
n = len(d) // 4
v = struct.unpack('<%dh' % (2 * n), d[:n * 4])
t0, t1 = int(float(sys.argv[2]) * 44100), min(n, int(float(sys.argv[3]) * 44100))
def g(ch, f):
    w = 2 * math.pi * f / 44100; c = 2 * math.cos(w); s1 = s2 = 0.0
    for i in range(t0, t1):
        s0 = v[2 * i + ch] + c * s1 - s2; s2 = s1; s1 = s0
    return math.sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / (t1 - t0) * 2
print('%.1f %.1f %.1f %.1f' % (g(0, 441), g(0, 882), g(1, 441), g(1, 882)))
PY
}
rmsw() { python3 -I - "$WORK/$1.wav" "$2" "$3" <<'PY'
import sys, struct, math
d = open(sys.argv[1], 'rb').read()[44:]
n = len(d) // 4
v = struct.unpack('<%dh' % (2 * n), d[:n * 4])
t0, t1 = int(float(sys.argv[2]) * 44100), min(n, int(float(sys.argv[3]) * 44100))
l = math.sqrt(sum(v[2 * i] ** 2 for i in range(t0, t1)) / max(1, t1 - t0)); r = math.sqrt(sum(v[2 * i + 1] ** 2 for i in range(t0, t1)) / max(1, t1 - t0))
print('%.1f %.1f' % (l, r))
PY
}
fgt() { awk -v a="$1" -v b="$2" 'BEGIN{exit !(a>b)}'; }

TONES="sampletonehz 0 441 8000; sampletonehz 1 300 6000; sampletonehz 2 523 7000; sampletonehz 3 700 5000;"
SONGFX="$TONES setstep 0 0 60 0 31 20 30 80; setstep 1 2 62 1 32 90 31 70; setstep 2 4 64 2 30 40 32 60; setstep 3 6 67 3 31 100 0 0; setstep 5 8 55 1 31 0 32 50; setstep 7 10 50 2 31 30 30 60;"
PP="delayparams 128 60;"
MONO="delayparams 0 60;"

echo "### 1: no pair linked == the flag off build"
wav "$BASE_BIN" b1 "$MONO $SONGFX" 400; wav "$BIN" n1 "$MONO $SONGFX" 400
same b1 n1 "8 track song, mono delay and reverb sends, 400 ms tail: byte identical"
wav "$BASE_BIN" b2 "$PP $SONGFX" 400; wav "$BIN" n2 "$PP $SONGFX" 400
same b2 n2 "8 track song, ping pong delay and reverb sends: byte identical"
wav "$BIN" n3 "pair 15; pair 0; $PP $SONGFX" 400
same n2 n3 "link all four pairs then unlink: the render is the never linked one (ping pong)"

echo "### 2: a mono sample on a linked pair"
M="sampletonehz 0 441 8000; setstep 0 0 60 0 30 80 32 90; setstep 0 6 64 0 32 90 30 60;"
wav "$BASE_BIN" m_b "pair 1; $MONO $M" 400; wav "$BIN" m_n "pair 1; $MONO $M" 400
within m_b m_n 16 "pair 1 mono sample, mono delay + reverb: within 16 LSB of the flag off pair (the side is 0 to rounding)"
wav "$BIN" m_u "$MONO $M" 400
within m_u m_n 16 "pair 1 mono sample == the centred mono track, delay + reverb sends"

echo "### 3: a stereo sample on a linked pair feeds the reverb in stereo (L 441 Hz, R 882 Hz, reverb send 100, tail 1.2 s)"
R="samplestereo 0 441 882 8000; instr 0; setstep 0 0 60 0 32 100 0 0;"
wav "$BASE_BIN" r_b "pair 1; $MONO $R" 1200; wav "$BIN" r_n "pair 1; $MONO $R" 1200
t_b=$(tone r_b 1.6 2.6); t_n=$(tone r_n 1.6 2.6)
echo "      tail 1.6..2.6 s  flag off: L441 L882 R441 R882 = $t_b"
echo "      tail 1.6..2.6 s  flag on : L441 L882 R441 R882 = $t_n"
set -- $t_b; bl441=$1 bl882=$2 br441=$3 br882=$4
set -- $t_n; nl441=$1 nl882=$2 nr441=$3 nr882=$4
# side mix: ratio 441 / 882 of the left bus over the right bus. 1 = the same mix on both buses
ratio_b=$(awk -v a="$bl441" -v b="$bl882" -v c="$br441" -v d="$br882" 'BEGIN{ if (b==0||c==0||d==0) {print 0; exit} print (a/b)/(c/d) }')
ratio_n=$(awk -v a="$nl441" -v b="$nl882" -v c="$nr441" -v d="$nr882" 'BEGIN{ if (b==0||c==0||d==0) {print 0; exit} print (a/b)/(c/d) }')
echo "      (L441/L882)/(R441/R882): flag off $ratio_b  flag on $ratio_n"
if fgt "$ratio_n" 1.5 && fgt "$ratio_n" "$(awk -v r="$ratio_b" 'BEGIN{print r*1.5}')"; then check "reverb tail decorrelates: left bus is richer in 441 Hz than the right bus ($ratio_n against $ratio_b flag off)" ok
else check "reverb tail decorrelates" "ratio flag on $ratio_n, flag off $ratio_b (want flag on above 1.5 and 1.5x the flag off one)"; fi

echo "### 4: ping pong delay with a stereo input (feedback 60, delay send 100)"
# wet only: the render with the delay send 100 minus the render with send 0, so the dry sample drops out. L only (441 Hz left, silence right) and
# R only content: the first echoes are fed by the channel, the stereo input puts the left channel into the left line and the right into the right
wetlr() { # BIN NAME HZL HZR -> "rmsL rmsR" of the wet part, 0.1 .. 1.9 s
    wav "$1" "$2_0" "pair 1; $PP samplestereo 0 $3 $4 8000; instr 0; setstep 0 0 60 0 30 0 0 0;" 600
    wav "$1" "$2_1" "pair 1; $PP samplestereo 0 $3 $4 8000; instr 0; setstep 0 0 60 0 30 100 0 0;" 600
    python3 -I - "$WORK/$2_0.wav" "$WORK/$2_1.wav" <<'PY'
import sys, struct, math
def rd(p):
    d = open(p, 'rb').read()[44:]; n = len(d) // 4
    return n, struct.unpack('<%dh' % (2 * n), d[:n * 4])
n0, a = rd(sys.argv[1]); n1, b = rd(sys.argv[2]); n = min(n0, n1)
i0, i1 = int(0.1 * 44100), min(n, int(1.9 * 44100))
r = lambda ch: math.sqrt(sum((b[2 * i + ch] - a[2 * i + ch]) ** 2 for i in range(i0, i1)) / max(1, i1 - i0))
print('%.1f %.1f' % (r(0), r(1)))
PY
}
set -- $(wetlr "$BASE_BIN" pl_b 441 0); blL_l=$1 blL_r=$2
set -- $(wetlr "$BASE_BIN" pr_b 0 441); brR_l=$1 brR_r=$2
set -- $(wetlr "$BIN" pl_n 441 0); nlL_l=$1 nlL_r=$2
set -- $(wetlr "$BIN" pr_n 0 441); nrR_l=$1 nrR_r=$2
echo "      wet rms (left right) left only: flag off $blL_l $blL_r  flag on $nlL_l $nlL_r"
echo "      wet rms (left right) right only: flag off $brR_l $brR_r  flag on $nrR_l $nrR_r"
# The left channel feeds the left line as the mono input did, so the left only take keeps its L/R balance. The right channel now feeds the right line,
# so the balance of the right only take changes (first echo on the other side)
bal_l=$(awk -v a="$nlL_l" -v b="$nlL_r" -v c="$blL_l" -v d="$blL_r" 'BEGIN{ if (b==0||c==0||d==0) {print 0; exit} print (a/b)/(c/d) }')
bal_r=$(awk -v a="$nrR_l" -v b="$nrR_r" -v c="$brR_l" -v d="$brR_r" 'BEGIN{ if (b==0||c==0||d==0) {print 0; exit} print (a/b)/(c/d) }')
echo "      L/R balance flag on over flag off: left only take $bal_l  right only take $bal_r"
if awk -v l="$bal_l" -v r="$bal_r" 'BEGIN{exit !(l>0.8 && l<1.25 && r>2)}'; then check "ping pong: left only keeps its balance ($bal_l), right only moves to the right line (balance x$bal_r)" ok
else check "ping pong stereo input steering" "left only balance ratio $bal_l (want 0.8..1.25), right only $bal_r (want above 2)"; fi

echo "### 5: the quiet detectors still sleep with the side silent (reverb and delay, 14 s tail after a stereo take)"
wav "$BIN" q_n "pair 1; $PP $R" 14000
set -- $(rmsw q_n 14.8 15.4); echo "      last 600 ms of the 14 s tail, flag on: rms L $1 R $2 (0 = the tails ended)"
if [ "$1" = "0.0" ] && [ "$2" = "0.0" ]; then check "reverb and delay tails end at exactly 0 after a stereo take (the side path does not keep them awake)" ok
else check "tails end at 0" "rms L $1 R $2 in the last 600 ms of 14 s"; fi

echo
if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
