#!/usr/bin/env python3
"""The checks of plug_tempo_sim.sh on the tap records (host_tap.cpp) in a poke output file. Host only.

    plug_tempo_check.py POKE_OUT sweepgate|dustbed

Prints "ok    text" or "FAIL  text" lines (the shell counts them)."""
import re
import sys

FS = 44117.6470588
N = 128


def dumps(path):
    out, cur = [], None
    for line in open(path):
        line = line.rstrip("\n")
        if line.startswith("tap lines follow"):
            cur = []
            out.append(cur)
        elif line.startswith("tap ") and cur is not None:
            m = re.match(r"tap (.+) k=(\d+) bpm=([\d.]+) beat=([\d.]+) playing=(\d+) clock=(\d+) in=([\d.]+) out=([\d.]+)$", line)
            cur.append({"name": m.group(1), "k": int(m.group(2)), "bpm": float(m.group(3)), "beat": float(m.group(4)),
                        "playing": int(m.group(5)), "clock": int(m.group(6)), "in": float(m.group(7)), "out": float(m.group(8))})
    return out


def say(ok, text, detail=""):
    print("%s  %s" % ("ok  " if ok else "FAIL", text))
    if not ok and detail:
        print("      " + detail)


def of(d, name):
    return [r for r in d if r["name"] == name]


def sweepgate(ds):
    if len(ds) != 3:
        say(False, "three dumps expected", "got %d" % len(ds))
        return
    d1, d2, d3 = (of(d, "Sweep Gate") for d in ds)
    say(len(d1) > 100 and all(r["clock"] == 1 for r in d1), "stopped: the plugin is called on a silent bus (its noise is not quiet) and the host sent the clock (%d blocks)" % len(d1))
    say(all(r["playing"] == 0 and r["beat"] == 0.0 for r in d1), "stopped: playing 0, beat 0")
    bpm = d1[0]["bpm"] if d1 else 0
    say(40 <= bpm <= 300 and all(r["bpm"] == bpm for r in d1 + d2 + d3), "the host delivers one tempo, the project's (%.2f BPM)" % bpm)
    pl = [r for r in d2 if r["playing"] == 1]
    say(len(pl) > 300, "after Play the host reports playing 1 (%d of %d blocks)" % (len(pl), len(d2)))
    if not pl:
        return
    say(pl[0]["beat"] < 0.1, "the beat starts at the downbeat on Play (first block %.4f)" % pl[0]["beat"])
    step = bpm / 60.0 * N / FS
    tick = 1.0 / 2304.0   # the sequencer clock moves in whole ticks: a block is 14 or 15 of them
    bad = [(a["beat"], b["beat"]) for a, b in zip(pl, pl[1:]) if abs((b["beat"] - a["beat"]) - step) > 1.01 * tick]
    say(len(bad) <= 2, "the beat advances %.5f a block (bpm/60 x 128 / fs) within one tick (1/2304 beat): %d of %d steps off" % (step, len(bad), len(pl) - 1), str(bad[:3]))
    span = pl[-1]["beat"] - pl[0]["beat"]
    exp = (len(pl) - 1) * step
    say(abs(span - exp) < 0.01 * exp, "%.2f beats in %d blocks, %.2f expected from the tempo (the 2304 ticks a beat of the sequencer)" % (span, len(pl) - 1, exp))
    wrong = []
    for r in pl:
        f = r["beat"] % 1.0
        if min(abs(f - 0.5), f, 1.0 - f) < 0.06:
            continue   # the edge of a slot: the gate is smoothing
        open_ = f >= 0.5   # Offbeat 0011 at 1/16: steps 2 and 3 of every beat
        if open_ and r["out"] < 0.02 or not open_ and r["out"] > 0.004:
            wrong.append((round(r["beat"], 3), r["out"]))
    say(not wrong, "the Offbeat gate is closed on steps 0 and 1 of every beat and open on 2 and 3 (%d of %d blocks wrong)" % (len(wrong), len(pl)), str(wrong[:4]))
    lv = [r["out"] for r in pl if r["beat"] % 1.0 >= 0.56 and r["beat"] % 1.0 <= 0.94]
    say(lv and min(lv) > 0.02, "an open slot passes the noise (rms %.3f .. %.3f)" % (min(lv), max(lv)) if lv else "no open slot seen")
    say(len(d3) > 100 and all(r["playing"] == 0 and r["beat"] == 0.0 for r in d3), "stopped again: playing 0 and beat 0 (%d blocks)" % len(d3))


def dustbed(ds):
    if len(ds) != 3:
        say(False, "three dumps expected", "got %d" % len(ds))
        return
    d1, d2, d3 = ds
    say(not of(d1, "Dust Bed") and not of(d1, "Sweep Gate"), "stopped, Play Only on, Sweep Gate quiet: no plugin is called on the silent bus (%d + %d calls)" % (len(of(d1, "Dust Bed")), len(of(d1, "Sweep Gate"))))
    b = of(d2, "Dust Bed")
    say(len(b) > 300, "playing: the bed is called on a silent bus (%d blocks)" % len(b))
    say(all(r["in"] == 0.0 for r in b) and min(r["out"] for r in b if r["playing"] == 1) > 0.0002 if b else False,
        "its input is silence and its output is the bed (rms %.5f .. %.5f)" % (min(r["out"] for r in b), max(r["out"] for r in b)) if b else "no calls")
    say(all(r["playing"] == 1 for r in b[10:]) and b[-1]["beat"] > 0.5, "the host's playing flag and beat reach it (last beat %.2f)" % b[-1]["beat"] if b else "no calls")
    sg = of(d2, "Sweep Gate")
    say(len(sg) == len(b) and all(r["in"] == 0.0 and r["out"] == 0.0 for r in sg),
        "Sweep Gate (Noise 0, quiet) is called too once the bed woke the chain, on silence, and stays silent (%d calls)" % len(sg))
    say(not of(d3, "Dust Bed"), "stopped again: Play Only faded the bed out, the plugin is not called any more (%d calls)" % len(of(d3, "Dust Bed")))


if __name__ == "__main__":
    ds = dumps(sys.argv[1])
    {"sweepgate": sweepgate, "dustbed": dustbed}[sys.argv[2]](ds)
