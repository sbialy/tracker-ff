#!/usr/bin/env python3
"""Bank swap click check for plus_smoke.sh / mini_smoke.sh row 21 (the A/B page on Tracker+ and Mini, P12 round 7).

usage: ab_click_check.py F32 EVENTS

F32 is a plus sim guest dump (UISHOT_PLUS_DUMP part<N>.f32: float L R interleaved, 128 frames a block), EVENTS its part<N>.events
("ev BLOCK type ch d0 d1", type 4 note on, 5 note off, or "par BLOCK id bits"). The held note is the first note on, the swap is the first
par block after it (the slot's values reach the lib before that block renders), the release is the first note off after the note on.
The largest step between two samples in the 6 ms around the swap (the spike) is held against the largest step of the 40 ms before it and
of the 40 ms from 15 ms after it (the old and the new patch, a click is a step that neither of them makes). Prints
"abclick on N swap S off F held H pre P post Q spike K ratio R level L" and exits 0 when the swap is between the note on and the note off
with no note event between (held 1), the ratio is at most 1.25 and the note still sounds after the swap (level: post RMS / pre RMS > 0.05).
"""
import array
import math
import sys

BLOCK = 128
raw = array.array('f')
raw.frombytes(open(sys.argv[1], 'rb').read())
ev = [ln.split() for ln in open(sys.argv[2]) if ln.startswith(('ev ', 'par '))]
on = next(int(e[1]) for e in ev if e[0] == 'ev' and e[2] == '4')
swap = next(int(e[1]) for e in ev if e[0] == 'par' and int(e[1]) > on)
off = next(int(e[1]) for e in ev if e[0] == 'ev' and e[2] == '5' and int(e[1]) > on)
held = int(on < swap < off and not any(e[0] == 'ev' and e[2] in ('4', '5') and on < int(e[1]) < swap for e in ev))
s = swap * BLOCK
ms = 48  # frames per ms


def steps(a, b):
    a, b = max(a, 1), min(b, len(raw) // 2)
    m = 0.0
    for i in range(a, b):
        for c in (0, 1):
            m = max(m, abs(raw[2 * i + c] - raw[2 * (i - 1) + c]))
    return m


def rms(a, b):
    seg = raw[2 * max(a, 0):2 * min(b, len(raw) // 2)]
    return math.sqrt(sum(x * x for x in seg) / max(len(seg), 1))


pre, post = steps(s - 42 * ms, s - 2 * ms), steps(s + 15 * ms, s + 55 * ms)
spike = steps(s - 2 * ms, s + 4 * ms)
ratio = spike / max(pre, post, 1e-9)
level = rms(s + 15 * ms, s + 55 * ms) / max(rms(s - 42 * ms, s - 2 * ms), 1e-9)
print('abclick on %d swap %d off %d held %d pre %.5f post %.5f spike %.5f ratio %.3f level %.3f' % (on, swap, off, held, pre, post, spike, ratio, level))
sys.exit(0 if held and ratio <= 1.25 and level > 0.05 and s + 55 * ms < off * BLOCK else 1)
