#!/usr/bin/env python3
"""Strum velocity ramp renders and checks (Mac only, never shipped): ref/strum-velocity.md.

    TOOLCHAIN=.../arm-none-eabi- VENV/bin/python -I strum_vel_render.py --bins DIR --plugins DIR --raw DIR --out DIR

A maj7 chord at C4 (MIDI 60, the step note 60 of a synth part) in four strums: down slow (Strum value 40, lowest note first), up slow (104 = -40,
highest note first), tight (4) and Strum 0. Every case in three builds: base (the code before the ramp), ramp (the branch) and flat (the
branch built with STRUM_RAMP 0, the old behaviour). Four engines:
  poly-acd, para-acd   the simulator (ui_shot --wav), the real sequencer and ACD of this tree. DIR/ui_shot_base|ramp|flat
  para-ju6, para-alco   the .plg in Unicorn (emu_render.py, render_harness.cpp). DIR/<engine>-<base|ramp|flat>/<engine>.plg.
      The harness gives the engine velocity 1.0 and the host applies the velocity as the gain on the whole voice (vel / 127 with the
      Velocity macro at 1.0), so the script applies that gain: 100 for a step without a Vol, 88 on the upstroke of a ramp build.
Checks (exit 1 when one fails):
  1. flat == base, byte for byte, every engine and case (the switch OFF is the old behaviour)
  2. ramp Strum 0 == base Strum 0, byte for byte
  3. ramp differs from base on the strummed cases (the ramp does something)
  4. the level of each chord note, from a least squares fit of the four fundamentals in a window where all notes sound, ramp over flat,
     equals the designed percent of its strum order (and falls in time order)
Writes OUT/<engine>_<case>_<ramp|flat>.wav (44.1 kHz header, 16 bit stereo, about 3 s) and OUT/CHECKS.txt. One gain per engine is applied to
all of its files so the A/B level relation is kept.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
import emu_render as R  # noqa: E402

FS = 44117.647
CASES = (("down40", 40), ("up104", -40), ("tight4", 4), ("strum0", 0))
CASE_VALUE = {"down40": 40, "up104": 104, "tight4": 4, "strum0": 0}   # the Strum FX value in the pattern
OFFS = (0, 4, 7, 11)
NOTE = 60
GATE_S = 1.8
LEN_S = 3.0


def header_consts():
    t = open(os.path.join(HERE, "..", "..", "plug", "strum_ramp.h")).read()
    g = lambda n: int(re.search(r"%s = (\d+);" % n, t).group(1))
    return g("kFallPct"), g("kFullUnits"), g("kFloorPct"), g("kUpStrokePct")


FALL, FULL, FLOOR, UP = header_consts()


def fall_pct(order, units):
    return max(FLOOR, 100 - FALL * order * min(units, FULL) // FULL)


def stroke_pct(strum):
    return UP if strum < 0 else 100


def sim_render(binary, path, para, value):
    pre = "wait 500; instr 64; tap Params; wait 1500; tap Up 3; wait 1800; tap Pattern; wait 300"
    pre += "; poke synthparam 0 Sustain 1"   # the Amp Sustain: the chord holds until Stop
    pre += "; poke synthparam 0 Chord_Mode 1" if para else "; poke synthvoices 0 4"
    script = "%s; setstep 0 0 60 64 14 24 47 %d; wait 200; audio 1; wav %s; tap Play; wait %d; tap Play; wait %d; wav off" % (
        pre, value, path, int(GATE_S * 1000), int((LEN_S - GATE_S) * 1000))
    with tempfile.TemporaryDirectory(prefix="svsim") as sd:
        subprocess.run([binary, sd, script], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True, timeout=90)
    w = wave.open(path)
    a = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2).astype(np.float64) / 32768.0
    w.close()
    return a


def plugin_render(run, strum_units, variant, blocks, off_block):
    ov = [(run.pidx("Chord"), 24.0), (run.pidx("Chord Mode"), 1.0), (run.pidx("Strum"), float(strum_units))]
    # ALCO: B Level 0. The ramp scales the chord notes of oscillator A, the played note's oscillator B is not a chord note and keeps its level
    # (at the default B Level 0.6 the played note of an upstroke, the last one, reads 87 % instead of 79 %). Set here so the level check is clean
    if any(p[0] == "B Level" for p in run.params):
        ov.append((run.pidx("B Level"), 0.0))
    fl, _, _ = run.run(blocks, 1, [NOTE], off_block, ov)
    x = np.stack([fl[:, 0, :].reshape(-1), fl[:, 1, :].reshape(-1)], 1).astype(np.float64)
    vel = 100
    if variant == "ramp" and strum_units < 0:
        vel = vel * stroke_pct(strum_units) // 100
    return x * (vel / 127.0)


def write_wav(path, a, gain):
    q = np.clip(np.rint(a * gain * 32767.0), -32768, 32767).astype("<i2")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    w = wave.open(path, "wb")
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(44100)
    w.writeframes(q.tobytes())
    w.close()
    return q


def fund_amps(x, t0, t1):
    n0, n1 = int(t0 * FS), int(t1 * FS)
    t = np.arange(n0, n1) / FS
    cols = []
    for o in OFFS:
        f = 440.0 * 2 ** ((NOTE + o - 69) / 12.0)
        cols += [np.sin(2 * np.pi * f * t), np.cos(2 * np.pi * f * t)]
    c, *_ = np.linalg.lstsq(np.stack(cols, 1), x[n0:n1], rcond=None)
    return np.array([np.hypot(c[2 * i], c[2 * i + 1]) for i in range(len(OFFS))])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bins", required=True)
    ap.add_argument("--plugins", required=True)
    ap.add_argument("--raw", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    harness = os.path.join(a.raw, "harness.plg")
    os.makedirs(a.raw, exist_ok=True)
    R.build_harness(os.environ["TOOLCHAIN"], harness)
    blocks = int(LEN_S * FS / 128)
    off_block = int(GATE_S * FS / 128)
    ren = {}   # (engine, case, variant) -> float array (n, 2)
    for v in ("base", "ramp", "flat"):
        for eng, para in (("poly-acd", False), ("para-acd", True)):
            for case, val in CASE_VALUE.items():
                ren[(eng, case, v)] = sim_render(os.path.join(a.bins, "ui_shot_" + v), os.path.join(a.raw, "%s_%s_%s.wav" % (eng, case, v)), para, val)
        for eng in ("ju6", "alco"):
            run = R.Run(os.path.join(a.plugins, "%s-%s" % (eng, v), eng + ".plg"), harness, count_cycles=False)
            for case, units in CASES:
                ren[("para-" + eng, case, v)] = plugin_render(run, units, v, blocks, off_block)
    log, bad = [], 0

    def check(ok, msg):
        nonlocal bad
        log.append("%s   %s" % ("ok  " if ok else "FAIL", msg))
        bad += not ok

    engines = ("poly-acd", "para-acd", "para-ju6", "para-alco")
    for eng in engines:
        n = min(len(ren[(eng, c, v)]) for c, _ in CASES for v in ("base", "ramp", "flat"))
        for case, _ in CASES:
            b, r, f = (ren[(eng, case, v)][:n] for v in ("base", "ramp", "flat"))
            check(np.array_equal(b, f), "%s %s: ramp OFF (flat) is bit identical to the base build" % (eng, case))
            if case == "strum0":
                check(np.array_equal(b, r), "%s %s: Strum 0 with the ramp ON is bit identical to the base build" % (eng, case))
            else:
                check(not np.array_equal(b, r), "%s %s: the ramp changes the render" % (eng, case))
        # levels per chord note, ramp over flat, window where all four sound
        for case, units in CASES:
            if units == 0:
                continue
            u = abs(units)
            order = [k if units > 0 else 3 - k for k in range(4)]
            want = np.array([fall_pct(o, u) * stroke_pct(units) / 100.0 for o in order])
            # the sim's Poly path applies the integer velocity (100 -> v), the engines the host gain on the whole voice: both give want
            got = np.zeros(4)
            for ch in (0, 1):
                fa = fund_amps(ren[(eng, case, "flat")][:, ch], 0.45, 0.65)
                ra = fund_amps(ren[(eng, case, "ramp")][:, ch], 0.45, 0.65)
                got += ra / fa / 2
            got *= 100.0
            by_order = [got[order.index(o)] for o in range(4)]
            # time order (stroke factor aside) falls: compare against want with a tolerance of 6 points
            ok = bool(np.all(np.abs(got - want) < 6.0))
            falls = all(by_order[i] >= by_order[i + 1] - 1.5 for i in range(3)) if u >= 4 else True
            check(ok and falls, "%s %s: level per note C E G B %% of flat: got %s, designed %s, in strum order %s" % (
                eng, case, np.round(got, 1).tolist(), np.round(want, 1).tolist(), np.round(by_order, 1).tolist()))
    # files
    for eng in engines:
        peak = max(np.abs(ren[(eng, c, "base")]).max() for c, _ in CASES)
        gain = 0.7 / peak
        log.append("gain %s %.3f (peak of the base renders %.4f)" % (eng, gain, peak))
        for case, _ in CASES:
            for v, name in (("ramp", "ramp"), ("flat", "flat")):
                x = ren[(eng, case, v)]
                write_wav(os.path.join(a.out, "%s_%s_%s.wav" % (eng, case, name)), x, gain)
    open(os.path.join(a.out, "CHECKS.txt"), "w").write("\n".join(log) + "\n")
    print("\n".join(log))
    print("RESULT: %s" % ("PASS" if not bad else "FAIL (%d)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
