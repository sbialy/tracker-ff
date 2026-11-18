#!/usr/bin/env python3
"""export_bpm_check.py WAV EXPECTED_BPM [--beats-per-onset N] [--rate HZ] [--json]

Measures the tempo of a rendered 4-on-the-floor WAV from the onset positions (host only, no device).
Onset = first sample of a hit whose level crosses a threshold (a fraction of the file's peak) after a quiet gap of at least half a beat.
The tempo is the least squares slope of onset position against beat index over the whole file (one onset per beat, or per N beats with
--beats-per-onset), the same figure from the first and the last onset (endpoint) is printed next to it. Block jitter (a tick falls on a
128 frame block start) does not bias the slope. Rate defaults to 44100, the sample rate in the exporter's WAV header.

Output: measured BPM (4 decimals), ratio to the expected tempo, the fitted drift in samples over the render and the dev band (max minus
min of onset minus ideal position). The firmware starts a step at the first 128 frame block boundary at or after its time, so with an exact
tempo the dev band stays under one block (128 samples) and never grows with the length of the render, a tempo error grows it linearly.
The exact model check recomputes, with the integer arithmetic of internalTickClock.h, in which 128 frame block every beat's tick falls and
compares each onset with it to the sample: zero mismatching onsets means the tempo is exact over the whole render (a tempo error of
1 in 10^6 moves an onset to another block after a few minutes). Exit 0 when the exact model has no mismatch, 1 otherwise.
"""
import json
import struct
import sys
import wave


def read_mono_abs(path):
    with wave.open(path, "rb") as w:
        n, ch, width, rate = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if width != 2:
        raise SystemExit("export_bpm_check: 16 bit WAV expected")
    vals = struct.unpack("<%dh" % (n * ch), raw)
    if ch == 1:
        return [abs(v) for v in vals], rate
    return [max(abs(vals[i]), abs(vals[i + 1])) for i in range(0, n * ch, ch)], rate


def onsets(a, beat_samples, frac=0.25):
    peak = max(a) or 1
    thr = frac * peak
    gap = int(beat_samples * 0.5)
    out, last = [], -10 * gap
    for i, v in enumerate(a):
        if v >= thr and i - last >= gap:
            out.append(i)
        if v >= thr:
            last = i
    return out


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def tick_block_index(m, tempo, rate):
    """Index of the audio block in which the m-th sequencer tick (nano step) falls, the arithmetic of internalTickClock.h in integers:
    block and period in units of 2^-26 us, a tick every time the accumulator reaches the period."""
    block = int(f32(128.0 / f32(rate)) * 1000000.0 * 67108864.0)
    period = int(f32((312500.0 / f32(tempo)) / 12.0) * 67108864.0)
    return -(-m * period // block) - 1


def model_check(onsets, tempo, rate, per_onset=1):
    """The exact test: every onset must sit 128 * (block of its tick - block of the first onset's tick) samples after the first onset.
    The first onset's tick number m0 is not known (the sound starts a few ticks after Play), the best of 0 .. one beat is taken.
    Returns (mismatching onsets, worst error in samples, m0)."""
    nano_per_onset = 2304 * per_onset
    onsets = onsets[1:]   # the first hit is left out, see measure()
    best = None
    for m0 in range(0, nano_per_onset):
        j0 = tick_block_index(m0, tempo, rate)
        bad, worst = 0, 0
        for k, o in enumerate(onsets):
            want = 128 * (tick_block_index(m0 + k * nano_per_onset, tempo, rate) - j0)
            e = abs((o - onsets[0]) - want)
            worst = max(worst, e)
            bad += e != 0
        if best is None or (bad, worst) < best[:2]:
            best = (bad, worst, m0)
        if bad == 0:
            break
    return best


def measure(path, expected_bpm, per_onset=1, rate=None):
    a, wav_rate = read_mono_abs(path)
    rate = rate or wav_rate
    beat = rate * 60.0 / expected_bpm * per_onset
    on = onsets(a, beat)
    if len(on) < 3:
        raise SystemExit("export_bpm_check: only %d onsets in %s" % (len(on), path))
    on_all = on
    on = on[1:]   # the first hit has no earlier hit to fade from: its level ramp crosses the threshold at another sample
    idx = [round((o - on[0]) / beat) for o in on]
    n = len(on)
    mx, my = sum(idx) / n, sum(on) / n
    slope = sum((x - mx) * (y - my) for x, y in zip(idx, on)) / sum((x - mx) ** 2 for x in idx)   # samples per onset
    icpt = my - slope * mx
    endpoint = (on[-1] - on[0]) / float(idx[-1] - idx[0])
    bpm = 60.0 * rate / slope * per_onset
    bpm_end = 60.0 * rate / endpoint * per_onset
    # drift against the expected tempo: the onset position minus the first onset plus the ideal beat spacing
    dev = [on[k] - (on[0] + idx[k] * beat) for k in range(n)]
    fit_drift = (slope - beat) * (idx[-1] - idx[0])   # what the fitted tempo adds over the whole render, in samples
    bad, worst, m0 = model_check(on_all, expected_bpm, rate, per_onset)
    return {
        "model_mismatch": bad, "model_worst_samples": worst, "model_m0": m0,
        "file": path, "onsets": len(on_all), "seconds": len(a) / float(rate),
        "expected_bpm": expected_bpm, "bpm_fit": bpm, "bpm_endpoint": bpm_end,
        "ratio": bpm / expected_bpm, "drift_samples": fit_drift, "dev_band_samples": max(dev) - min(dev),
        "first_onset": on[0], "last_onset": on[-1],
    }


def main(argv):
    args = [x for x in argv[1:] if not x.startswith("--")]
    per = 1
    rate = None
    if "--beats-per-onset" in argv:
        per = int(argv[argv.index("--beats-per-onset") + 1])
        args.remove(str(per))
    if "--rate" in argv:
        rate = float(argv[argv.index("--rate") + 1])
        args.remove(argv[argv.index("--rate") + 1])
    if len(args) != 2:
        print(__doc__)
        return 2
    r = measure(args[0], float(args[1]), per, rate)
    if "--json" in argv:
        print(json.dumps(r))
    else:
        print("%-60s onsets %4d  %7.2f s  expected %9.4f  fit %9.4f  endpoint %9.4f  ratio %.6f  fit drift %+9.1f smp  dev band %6.1f smp  exact model: %d of %d onsets off (worst %d smp)" % (
            r["file"][-60:], r["onsets"], r["seconds"], r["expected_bpm"], r["bpm_fit"], r["bpm_endpoint"], r["ratio"],
            r["drift_samples"], r["dev_band_samples"], r["model_mismatch"], r["onsets"], r["model_worst_samples"]))
    import os
    tol = float(os.environ.get("TOL_SAMPLES", "1.0"))
    return 0 if r["model_mismatch"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
