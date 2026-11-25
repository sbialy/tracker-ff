#!/usr/bin/env python3
"""wav_band.py PLUS.wav OG.wav [--rms-db 0.5] [--peak-db 1.0] [--line-db 0.75] [--onset 128]

The banded WAV compare of the plus sim against the OG sim on the same sample-only project (ref/ff-imx-port-plan.md P4 leftover a). The two
mixer chains differ (Tracker+: stereo voice chains, its own pan law and mixer, 21 inputs), so the WAVs are not sample identical and there is no
shared golden. What is stable across the chains and is checked here: frame count, first onset, RMS and peak of the whole capture, the level of the
two spectral lines of the project (Goertzel at 330 Hz and 60 Hz, insensitive to the phase), and the RMS of every 250 ms window. SIM FIGURES.
Exit 0 when every figure is inside its band."""
import sys, wave, array, math

def load(p):
    w = wave.open(p); a = array.array('h'); a.frombytes(w.readframes(w.getnframes()))
    ch = w.getnchannels()
    return [a[c::ch] for c in range(ch)], w.getframerate()

def goertzel(x, hz, rate):
    k = 2.0 * math.cos(2.0 * math.pi * hz / rate); s1 = s2 = 0.0
    for v in x:
        s0 = v + k * s1 - s2; s2 = s1; s1 = s0
    p = s1 * s1 + s2 * s2 - k * s1 * s2
    return math.sqrt(max(p, 0.0)) * 2.0 / len(x)

def db(a, b):
    return 20.0 * math.log10(a / b) if a > 0 and b > 0 else float('inf')

def main():
    args = sys.argv[1:]
    opt = {'--rms-db': 0.5, '--peak-db': 1.0, '--line-db': 0.75, '--onset': 128.0}
    files = []
    i = 0
    while i < len(args):
        if args[i] in opt: opt[args[i]] = float(args[i + 1]); i += 2
        else: files.append(args[i]); i += 1
    (pa, ra), (pb, rb) = load(files[0]), load(files[1])
    bad = 0
    def row(name, va, vb, band, unit):
        nonlocal bad
        ok = abs(va - vb) <= band
        bad += 0 if ok else 1
        print("%s %-22s plus %10.3f  og %10.3f  diff %7.3f %s  band %.3f" % ("ok  " if ok else "FAIL", name, va, vb, va - vb, unit, band))
    row("frames", len(pa[0]), len(pb[0]), 0, "frames")
    for c in range(min(len(pa), len(pb))):
        a, b = pa[c], pb[c]
        n = min(len(a), len(b))
        a, b = a[:n], b[:n]
        on = lambda x: next((j for j, v in enumerate(x) if abs(v) > 3000), -1)
        row("ch%d first onset" % c, on(a), on(b), opt['--onset'], "frames")
        ea = math.sqrt(sum(v * v for v in a) / n); eb = math.sqrt(sum(v * v for v in b) / n)
        row("ch%d rms" % c, 20 * math.log10(ea), 20 * math.log10(eb), opt['--rms-db'], "dB")
        row("ch%d peak" % c, 20 * math.log10(max(map(abs, a))), 20 * math.log10(max(map(abs, b))), opt['--peak-db'], "dB")
        for hz in (330, 60):
            row("ch%d line %d Hz" % (c, hz), 20 * math.log10(goertzel(a, hz, ra)), 20 * math.log10(goertzel(b, hz, rb)), opt['--line-db'], "dB")
        w = 11025
        worst = 0.0
        for s in range(0, n - w, w):
            wa = math.sqrt(sum(v * v for v in a[s:s + w]) / w); wb = math.sqrt(sum(v * v for v in b[s:s + w]) / w)
            if wa > 100 and wb > 100: worst = max(worst, abs(db(wa, wb)))
        row("ch%d worst 250 ms rms" % c, worst, 0.0, 1.0, "dB")
    print("RESULT: GREEN" if not bad else "RESULT: RED (%d)" % bad)
    return 1 if bad else 0

sys.exit(main())
