#!/usr/bin/env python3
"""m8_modes_check.py: measurements on the master.wav of the sample play mode sim (m8_modes_sim.sh). Left channel only.
  mirror FWD.wav REV.wav        the level envelope of REV is the time reverse of FWD (1 ms bins, correlation >= 0.97)
  startlevel WAV MIN MAX        the RMS of the first 10 ms after the onset as a fraction of the loudest 10 ms RMS, inside MIN..MAX
  freq WAV HZ TOL_PCT           the steady state frequency (zero crossings over the middle of the sound) and the autocorrelation at that period (>= 0.9)
  dur WAV SEC TOL_PCT           the audible length (first to last frame above 2 % of the peak) and
  silent WAV                    nothing above 1 % of full scale
  freqwin WAV T0 T1 HZ TOL_PCT  the frequency inside the window T0..T1 seconds after the onset
The environment variable M8CH=1 measures the right channel (default 0, the left).
Prints one line, exit 0 = pass."""
import array, math, os, sys, wave


def left(path):
    w = wave.open(path)
    a = array.array('h')
    a.frombytes(w.readframes(w.getnframes()))
    ch = w.getnchannels()
    side = int(os.environ.get('M8CH', '0'))
    return [a[i * ch + side] for i in range(w.getnframes())], w.getframerate()


def span(x, frac=0.02):
    pk = max(abs(v) for v in x) or 1
    thr = max(frac * pk, 60)
    idx = [i for i, v in enumerate(x) if abs(v) > thr]
    return (idx[0], idx[-1], pk) if idx else (None, None, pk)


def envelope(x, a, b, bin_=44):
    return [math.sqrt(sum(v * v for v in x[i:i + bin_]) / bin_) for i in range(a, b - bin_, bin_)]


def corr(p, q):
    n = min(len(p), len(q))
    p, q = p[:n], q[:n]
    mp, mq = sum(p) / n, sum(q) / n
    num = sum((u - mp) * (v - mq) for u, v in zip(p, q))
    den = math.sqrt(sum((u - mp) ** 2 for u in p) * sum((v - mq) ** 2 for v in q)) or 1
    return num / den


def main(argv):
    cmd = argv[1]
    if cmd == 'mirror':
        f, _ = left(argv[2]); r, _ = left(argv[3])
        fa, fb, _ = span(f); ra, rb, _ = span(r)
        ef = envelope(f, fa, fb); er = envelope(r, ra, rb)
        n = min(len(ef), len(er))
        c = corr(ef[:n][::-1] if len(ef) == n else ef[-n:][::-1], er[:n])
        print('mirror: fwd %d..%d rev %d..%d bins %d corr %.4f' % (fa, fb, ra, rb, n, c))
        return 0 if c >= 0.97 and abs((fb - fa) - (rb - ra)) < 600 else 1
    if cmd == 'startlevel':
        x, _ = left(argv[2]); lo, hi = float(argv[3]), float(argv[4])
        a, b, pk = span(x)
        if a is None:
            print('startlevel: silent'); return 1
        rms = lambda i: math.sqrt(sum(v * v for v in x[i:i + 441]) / 441)
        lvl = rms(a) / (max(rms(i) for i in range(a, b - 441, 441)) or 1)
        print('startlevel: onset %d RMS of the first 10 ms %.3f of the loudest 10 ms (want %.2f..%.2f)' % (a, lvl, lo, hi))
        return 0 if lo <= lvl <= hi else 1
    if cmd == 'freq':
        x, sr = left(argv[2]); want = float(argv[3]); tol = float(argv[4])
        a, b, pk = span(x)
        if a is None:
            print('freq: silent'); return 1
        m0 = a + (b - a) // 4; m1 = a + 3 * (b - a) // 4
        seg = x[m0:m1]
        zc = [i for i in range(1, len(seg)) if seg[i - 1] < 0 <= seg[i]]
        if len(zc) < 4:
            print('freq: %d crossings' % len(zc)); return 1
        hz = sr * (len(zc) - 1) / (zc[-1] - zc[0])
        lag = int(round(sr / hz))
        n = len(seg) - lag
        ac = sum(seg[i] * seg[i + lag] for i in range(n)) / (math.sqrt(sum(v * v for v in seg[:n]) * sum(v * v for v in seg[lag:lag + n])) or 1)
        err = abs(hz - want) / want * 100
        print('freq: %.2f Hz want %.2f err %.2f %% autocorr %.3f' % (hz, want, err, ac))
        return 0 if err <= tol and ac >= 0.9 else 1
    if cmd == 'freqwin':
        x, sr = left(argv[2]); t0, t1, want, tol = (float(v) for v in argv[3:7])
        a, b, pk = span(x)
        if a is None:
            print('freqwin: silent'); return 1
        seg = x[a + int(t0 * sr):a + int(t1 * sr)]
        zc = [i for i in range(1, len(seg)) if seg[i - 1] < 0 <= seg[i]]
        if len(zc) < 4:
            print('freqwin: %d crossings' % len(zc)); return 1
        hz = sr * (len(zc) - 1) / (zc[-1] - zc[0])
        err = abs(hz - want) / want * 100
        print('freqwin: %.2f..%.2f s after the onset %.2f Hz want %.2f err %.2f %%' % (t0, t1, hz, want, err))
        return 0 if err <= tol else 1
    if cmd == 'dur':
        x, sr = left(argv[2]); want = float(argv[3]); tol = float(argv[4])
        a, b, pk = span(x)
        if a is None:
            print('dur: silent'); return 1
        d = (b - a + 1) / sr
        err = abs(d - want) / want * 100
        print('dur: %.4f s want %.4f err %.2f %%' % (d, want, err))
        return 0 if err <= tol else 1
    if cmd == 'silent':
        x, _ = left(argv[2]); pk = max(abs(v) for v in x)
        print('silent: peak %d' % pk)
        return 0 if pk <= 330 else 1
    print(__doc__); return 2


sys.exit(main(sys.argv))
