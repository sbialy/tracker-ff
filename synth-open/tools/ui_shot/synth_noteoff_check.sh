#!/bin/bash
# synth_noteoff_check.sh UI_SHOT [OUTDIR]: does the STOCK closed libPolySynth cut a second voice of the same pitch when the first is stopped?
# The question (P1 of the FF -> Tracker+/Mini port): two tracks play the same pitch on the same synth part, one track stops its note. Does the other voice go silent?
# UI_SHOT is a PLATFORM=IMX ui_shot with the closed lib (the plus pin build/emu/current-plus/ui_shot, the mini pin, or a tree build). Host only, NOT firmware.
# The part's guest event log (UISHOT_PLUS_DUMP, part1.events: `ev <block> <4 on | 5 off> <chan> <note> <vel>`) and its float stream (part1.f32, stereo, 128 frames per
# block) are the evidence: the stream is what the closed lib rendered. Scenarios (ACD on instrument 65 = part 1, pattern 16 steps, tempo as the project default):
#   S1 two tracks play 60 at step 0, track 1 puts a note off at step 4 (note 60 stays on track 2)
#   S2 control: track 2 alone plays 60 (nothing stops it)
#   S3 control: track 1 alone plays 60 and stops at step 4
#   S4 control: two tracks, different pitches (60 and 62), track 1 stops at step 4
#   S5 the second voice starts later: track 1 plays 60 at step 0, track 2 plays 60 at step 2, track 1 stops at step 4 (the voice of track 2 must stay)
#   S6 both stop: as S1 with the second note off of track 2 at step 5 (the voice must end: the lib counts the note ons per pitch or the off is lost)
# Window W = blocks 200..600 (0.58 .. 1.74 s), past every release tail. Verdict: CUT when S1 is as quiet as S3 while S2 is sounding, KEPT when S1 is as loud as S2.
set -euo pipefail
UI="${1:?usage: synth_noteoff_check.sh UI_SHOT [OUTDIR]}"
OUT="${2:-$(cd "$(dirname "$0")/../../.." && pwd)/build/synth-noteoff}"
rm -rf "$OUT"; mkdir -p "$OUT"
# the default ACD patch decays to silence in 0.3 s: Sustain of the amp envelope (the 2nd Sustain) to 100 % so a held voice keeps sounding
HEAD='wait 500; patlen 16; poke synthparam 1 Sustain@2 1; poke synthparam 1 Saw_Mix 1;'
TAIL='audio 1; tap Play; wait 1800; tap Play; wait 300'
run() {   # run NAME STEPS
    mkdir -p "$OUT/$1/dump" "$OUT/$1/card"
    (cd "$OUT" && UISHOT_POKE="$OUT/$1/poke" UISHOT_PLUS_DUMP="$OUT/$1/dump" ASAN_OPTIONS=detect_leaks=0 timeout 200 "$UI" "$OUT/$1/card" "$HEAD $2 $TAIL" >"$OUT/$1.log" 2>&1)
}
run s1 'setstep 0 0 60 65 0 0 0 0; setstep 1 0 60 65 0 0 0 0; setstep 0 4 -2 65 0 0 0 0;'
run s2 'setstep 1 0 60 65 0 0 0 0;'
run s3 'setstep 0 0 60 65 0 0 0 0; setstep 0 4 -2 65 0 0 0 0;'
run s4 'setstep 0 0 60 65 0 0 0 0; setstep 1 0 62 65 0 0 0 0; setstep 0 4 -2 65 0 0 0 0;'
run s5 'setstep 0 0 60 65 0 0 0 0; setstep 1 2 60 65 0 0 0 0; setstep 0 4 -2 65 0 0 0 0;'
run s6 'setstep 0 0 60 65 0 0 0 0; setstep 1 0 60 65 0 0 0 0; setstep 0 4 -2 65 0 0 0 0; setstep 1 5 -2 65 0 0 0 0;'
python3 -I - "$OUT" <<'PY'
import sys, array, math
out = sys.argv[1]
def rms(name, a, b):
    d = array.array('f'); d.frombytes(open(f'{out}/{name}/dump/part1.f32', 'rb').read())
    seg = d[a * 256:b * 256]
    return math.sqrt(sum(x * x for x in seg) / max(1, len(seg))), len(d) // 256
def evs(name, upto=300):
    return [l.strip() for l in open(f'{out}/{name}/dump/part1.events') if l.startswith('ev ') and int(l.split()[1]) < upto and l.split()[2] in '45' and l.split()[4] in ('60', '62')]
W = (200, 600)
r = {n: rms(n, *W) for n in ('s1', 's2', 's3', 's4', 's5', 's6')}
for n in ('s1', 's2', 's3', 's4', 's5', 's6'):
    print(f'{n}: window RMS {r[n][0]:.5f} ({r[n][1]} blocks)  note events before block 300: {"; ".join(evs(n))}')
pre1, pre2 = rms('s1', 20, 150)[0], rms('s2', 20, 150)[0]
print(f'before the note off (blocks 20..150): S1 RMS {pre1:.5f}, S2 RMS {pre2:.5f}, ratio {pre1 / pre2 if pre2 else 0:.2f} (1.00: the second note on of a pitch adds no second voice)')
s1, s2, s3 = r['s1'][0], r['s2'][0], r['s3'][0]
if s2 < 0.01: v = 'INCONCLUSIVE: the control S2 is not sounding in the window'
elif s1 < 0.1 * s2 and s3 < 0.1 * s2: v = 'YES: the other voice is cut (S1 as quiet as the stopped voice S3, S2 sounds)'
elif s1 > 0.5 * s2: v = 'NO: the other voice keeps sounding (S1 vs S2 = %.2f)' % (s1 / s2)
else: v = 'PARTIAL: S1 is %.2f of S2 (neither cut nor intact)' % (s1 / s2)
print('S4 (different pitch) / S2 = %.2f, S5 (second voice later) / S2 = %.2f, S6 (both stop) / S2 = %.2f' % tuple((r[k][0] / s2 if s2 else 0) for k in ('s4', 's5', 's6')))
print('VERDICT:', v)
PY
