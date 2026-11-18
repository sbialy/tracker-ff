#!/usr/bin/env python3
"""A/B morph level check for synth_play_sim.sh (rows AB9).

usage: ab_audio_check.py RAW MARKS...   MARKS = M A B triples: morph value (0..100), first and last frame of the window

RAW is the UISHOT_AUDIO_DUMP of the run (16 bit L R interleaved). For every window the RMS power is measured. The first and the last triple must be
morph 0 (part A alone) and 100 (part B alone): they give PA and PB. The equal power law predicts cos(m pi/200)^2 PA + sin(m pi/200)^2 PB + 2 cos sin C. Prints "ab_audio ends A=<rms> B=<rms> corr=<C / sqrt(PA PB)> worst_db=<largest deviation of a middle window from the fit, dB>
nogap=<1|0>" where nogap means no window of the sweep falls under half the power of the weaker end (the level never dips).
"""
import array
import math
import sys

raw = array.array('h')
raw.frombytes(open(sys.argv[1], 'rb').read())
args = [int(x) for x in sys.argv[2:]]
wins = [(args[i], args[i + 1], args[i + 2]) for i in range(0, len(args), 3)]


def power(a, b):
    seg = raw[2 * a:2 * b]
    return sum(x * x for x in seg) / max(len(seg), 1)


pw = [(m, power(a, b)) for m, a, b in wins]
pa, pb = pw[0][1], pw[-1][1]
assert pw[0][0] == 0 and pw[-1][0] == 100, 'the first window must be morph 0 and the last 100'
# The two parts play the same keys, so their outputs are correlated by a constant C (the phase of the two engines at the note on is fixed):
# P(m) = cos^2 PA + sin^2 PB + 2 cos sin C. C is the median of the C of the middle windows, the check is that one C fits them all
# (the shape of the law: a linear fade or a wrong table does not fit)
mid = pw[1:-1]
cs = []
for m, p in mid:
    x = m / 100.0 * math.pi / 2
    cs.append((p - math.cos(x) ** 2 * pa - math.sin(x) ** 2 * pb) / (2 * math.cos(x) * math.sin(x)))
cs.sort()
c = cs[len(cs) // 2]
worst = 0.0
floor = 0.5 * min(pa, pb)
gap = False
for m, p in mid:
    x = m / 100.0 * math.pi / 2
    pred = math.cos(x) ** 2 * pa + math.sin(x) ** 2 * pb + 2 * math.cos(x) * math.sin(x) * c
    worst = max(worst, abs(10 * math.log10(max(p, 1e-9) / max(pred, 1e-9))))
    gap = gap or p < floor
print('ab_audio ends A=%.1f B=%.1f corr=%.2f worst_db=%.2f nogap=%d' % (math.sqrt(pa), math.sqrt(pb), c / math.sqrt(pa * pb), worst, 0 if gap else 1))
