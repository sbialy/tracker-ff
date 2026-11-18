#!/usr/bin/env python3
"""Host test of tail_pad.py: a pad that gets applied (to a 8 B aligned link, as the real one is) always leaves the last 4 KB sector at 0
or at least 0x800 B within two rounds, and a good tail is never padded."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tail_pad as tp

bad = 0


def link(base, pad):
    # sections after the pad are 8 B aligned: the pad moves the end by pad rounded up to 8
    return base + ((pad + 7) & ~7)


for base in range(0xF0000, 0xF3000, 4):
    pad = 0
    for rnd in range(4):
        add = tp.extra_pad(link(base, pad))
        if not add:
            break
        pad += add
    end = link(base, pad)
    tail = end & 0xFFF
    ok = (tail == 0 or tail >= 0x800) and pad <= 0x800 + 32 and (pad == 0) == (tp.extra_pad(base) == 0) and rnd <= 2
    if not ok:
        bad += 1
        if bad < 5:
            print("FAIL base 0x%X pad %d end 0x%X rounds %d" % (base, pad, end, rnd))
for end, want in ((0xFBAE4, 0), (0xFA8E4, 0), (0xFA7E4, 0x20), (0xFB000, 0), (0xFB010, 0x7F0)):
    got = tp.extra_pad(end)
    if got != want:
        bad += 1
        print("FAIL end 0x%X extra %d want %d" % (end, got, want))
print("tail_pad_test: %s" % ("PASS" if not bad else "FAIL (%d)" % bad))
sys.exit(1 if bad else 0)
