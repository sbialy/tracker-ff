#!/usr/bin/env python3
"""Bridge proof of emu parity T7 (ref/emu-parity-plan.md): every synth engine plugin of the card rendered by the reference (poly-engines
tools/emu_render.py: the .plg and the render harness in a Python Unicorn machine, libm = the release image's own newlib through
--image-libm) and by ui_shot's bridge (BRIDGE=1: the card .plg installed through the firmware's synthEnsure / loadEngine / start() into the
FlexNVM model, driven through its ISynthEngine by the `plugrender` verb, host_plugrender.cpp), byte compared run by run. Host tool, NOT
firmware. Grown from twin_render_check.py, retired at T9 (the ALCO scenario set is its set, 451 runs), every other engine gets a per engine sweep.

    PY=venv/bin/python bridge_check.py --engines-tree ENG --card CARD --axf AXF --ui-shot BIN [--harness H] [--quick] [--only a,b]
                                       [--jobs N] [--keep DIR] [--unit-dir DIR]

ENG: a poly-engines checkout with emu_render.py --image-libm (tools/emu_render.py, tools/alco/alco_checks.py, the vendored SDK) whose
Run(count=False) installs no Unicorn hook (poly-engines ep-t7-refhook and later: a memory read hook breaks IT blocks, see Run's doc).
CARD: a card folder with Plugins/Synth/*.plg (the pinned emu's card). AXF: the release axf the bridge maps (its libm, its loader).
--unit-dir writes the unit leg: one short transcript per engine (<file>.tr, only what the unit console's plugrender takes: strum 0..100,
files 0, run) with the reference's and the bridge's per block FNV (<file>.ref.pb, <file>.bridge.pb, the @pb lines of the console).
Sessions per engine (one Unicorn Run and one ui_shot start each, a fresh engine, as the twin check):
  sweep  host of 1.4.0 (strum 100, files 0): C3 C4 C5, a 4 voice chord, every parameter at its min and at its max, 6 seeded random
         patches, a ramp, a glide, velocity, slot 2
  strum  engines with a Strum parameter: the project's ramp depth 0, 50, 100 against Strum -20, 20, 40 (with Chord / Chord Mode)
  old    a host before 1.4.0 (no PLUG_EXT_STRUM): two runs
  ALCO   twin_render_check.py's sessions: sweep (ABI 1.1 host), strum, 11 wave mode sessions with a transcript card (files 3 and 6)
Exit 0 = every run of every engine byte equal, the descriptor (identity, parameter table) equal, no guest fault.
"""
import argparse
import multiprocessing
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ap = argparse.ArgumentParser()
ap.add_argument("--engines-tree", required=True)
ap.add_argument("--card", required=True)
ap.add_argument("--axf", required=True)
ap.add_argument("--ui-shot", required=True)
ap.add_argument("--harness", help="a built render harness plugin (else built with TOOLCHAIN)")
ap.add_argument("--quick", action="store_true", help="fewer blocks per run, the scenario list is the same")
ap.add_argument("--only", help="only these card files (comma separated base names: pluck,alco)")
ap.add_argument("--jobs", type=int, default=4)
ap.add_argument("--keep", help="keep transcripts and outputs in this folder")
ap.add_argument("--unit-dir", help="write the unit leg transcripts and expected per block FNVs here")
a = ap.parse_args()
TREE = os.path.abspath(a.engines_tree)
sys.path.insert(0, os.path.join(TREE, "tools"))
sys.path.insert(0, os.path.join(TREE, "tools", "alco"))
import numpy as np  # noqa: E402
import emu_render as R  # noqa: E402

BLK = 24 if a.quick else 48
CFG_BYTES = 712


def cfg_hex(voices, off, notes, over=(), vel=None, ramp_block=0, ramp=(), glide=None, slot=0):
    """the Cfg block of tools/render_harness.cpp, as emu_render.Run.run packs it"""
    c = struct.pack("<III4I", voices, off, len(over), *(list(notes) + [0] * (4 - len(notes))))
    c += struct.pack("<64I", *([i for i, _ in over] + [0] * (64 - len(over)))) + struct.pack("<64f", *([v for _, v in over] + [0.0] * (64 - len(over))))
    vel = list(vel or []) + [0.0] * (4 - len(vel or []))
    c += struct.pack("<4f", *vel) + struct.pack("<II", ramp_block, len(ramp))
    c += struct.pack("<16I", *([i for i, _ in ramp] + [0] * (16 - len(ramp)))) + struct.pack("<16f", *([v for _, v in ramp] + [0.0] * (16 - len(ramp))))
    gl = glide or (0, 0.0, 0.0, 0)
    c += struct.pack("<IIff", gl[3], gl[0], gl[1], gl[2]) + struct.pack("<I", slot)
    assert len(c) == CFG_BYTES
    return c.hex()


class Session:
    """One engine in the reference Unicorn machine plus the transcript of what was done to it, for the bridge to repeat"""

    def __init__(self, plg, harness, lm, label, **kw):
        self.r = R.Run(plg, harness, image_libm=lm, count=False, **kw)
        self.label = label
        self.lines = []
        self.expect = []
        self.depth = kw.get("strum_depth")
        self.sent = None

    def state(self):
        r = self.r
        tail = 0 if r.files is None else (3 if r.refs is None else 6)
        files = [] if r.files is None else [(r.names[i] if r.names else "w%04d.wav" % i, np.asarray(w, dtype="<i2").tobytes()) for i, w in enumerate(r.files)]
        refs = sorted((r.refs or {}).items())
        return (self.depth, tail, files, refs, sorted(r.bad))

    def sync(self):
        st = self.state()
        if st == self.sent:
            return
        depth, tail, files, refs, bad = st
        self.lines.append("strum %d" % (-1 if depth is None else depth))
        self.lines.append("files %d" % tail)
        if files or self.sent and self.sent[2]:
            self.lines.append("nfiles %d" % len(files))
            self.lines += ["f %s %s" % (n, w.hex() or "-") for n, w in files]
        if refs or self.sent and self.sent[3]:
            self.lines.append("refs %d" % len(refs))
            self.lines += ["r %d %d %s" % (k[0], k[1], v) for k, v in refs]
        if bad or self.sent and self.sent[4]:
            self.lines.append("bad %d" % len(bad))
            self.lines += ["b %s" % b for b in bad]
        self.sent = st

    def set_depth(self, pct):
        self.r.set_strum_depth(pct)
        self.depth = pct

    def run(self, name, blocks, voices, notes, over, off=10 ** 6, **kw):
        self.sync()
        fl, _, _ = self.r.run(blocks, voices, notes, off, over, **kw)
        cfg = bytes(self.r.uc.mem_read(R.CFG, CFG_BYTES))
        self.lines.append("run %d %s" % (blocks, cfg.hex()))
        self.expect.append((name, np.ascontiguousarray(fl, dtype="<f4").tobytes()))
        return fl

    def service(self, part, vals, times=4):
        self.sync()
        r = self.r
        val_addr = R.SDRAM + 0x780000
        r.uc.mem_write(val_addr, struct.pack("<%df" % len(vals), *vals))
        u32 = lambda p: struct.unpack("<I", bytes(r.uc.mem_read(p, 4)))[0]   # noqa: E731
        for _ in range(times):
            r.call(u32(r.ext + 100), part, val_addr, len(vals))
        self.lines.append("service %d %d %s" % (part, times, struct.pack("<%df" % len(vals), *vals).hex()))


def over_of(r, d):
    return [(r.pidx(k), float(v)) for k, v in d.items()]


def ext_name(r):
    u32 = lambda p: struct.unpack("<I", bytes(r.uc.mem_read(p, 4)))[0]   # noqa: E731
    return bytes(r.uc.mem_read(u32(r.ext + 20), 16)).split(b"\0")[0].decode()


def generic_sessions(plg, harness, lm):
    out = []
    s = Session(plg, harness, lm, "sweep", strum_depth=100)
    out.append(s)
    r = s.r
    names = [p[0] for p in r.params]
    for nm, note in (("C3", 48), ("C4", 60), ("C5", 72)):
        s.run("default %s" % nm, BLK, 1, [note], [], off=BLK - 8)
    s.run("chord4", BLK, 4, [48, 52, 55, 59], [], off=BLK - 8)
    short = max(16, BLK // 2)
    for i, (nm, df, mn, mx) in enumerate(r.params):
        s.run("p%d %s min" % (i, nm), short, 2, [48, 60], [(i, mn)], off=short - 6)
        s.run("p%d %s max" % (i, nm), short, 2, [48, 60], [(i, mx)], off=short - 6)
    rng = random.Random(7)
    for k in range(6):
        over = [(i, mn + (mx - mn) * rng.random()) for i, (nm, df, mn, mx) in enumerate(r.params)][:64]
        s.run("random %d" % k, BLK, 3, [45, 57, 64], over, off=BLK - 8)
    p0 = 0
    lo, hi = r.params[p0][2], r.params[p0][3]
    s.run("ramp", BLK, 1, [60], [(p0, lo)], ramp_block=6, ramp=[(p0, hi)])
    s.run("glide", BLK, 1, [60], [], glide=(p0, lo, hi, 20))
    s.run("velocity", BLK, 2, [60, 67], [], vel=[0.4, 1.0])
    s.run("slot 2", BLK, 1, [60], [], slot=2)
    if "Strum" in names:
        s2 = Session(plg, harness, lm, "strum", strum_depth=100)
        out.append(s2)
        base = {}
        if "Chord Mode" in names:
            base["Chord Mode"] = 1
        if "Chord" in names:
            base["Chord"] = min(24, s2.r.params[s2.r.pidx("Chord")][3])
        for depth in (0, 50, 100):
            s2.set_depth(depth)
            for st in (-20, 20, 40):
                s2.run("depth %d strum %+d" % (depth, st), BLK + 8, 1, [60], over_of(s2.r, dict(base, Strum=st)))
    s3 = Session(plg, harness, lm, "old")
    out.append(s3)
    s3.run("old host C4", BLK, 1, [60], [], off=BLK - 8)
    s3.run("old host chord", BLK, 3, [48, 55, 64], [], off=BLK - 8)
    return out


def unit_session(plg, harness, lm):
    """the unit leg: only what the console's plugrender takes (strum 0..100, files 0, run), about 280 blocks"""
    s = Session(plg, harness, lm, "unit", strum_depth=100)
    r = s.r
    s.run("C4", 48, 1, [60], [], off=40)
    s.run("chord4", 48, 4, [48, 52, 55, 59], [], off=40)
    rng = random.Random(11)
    for k in range(2):
        over = [(i, mn + (mx - mn) * rng.random()) for i, (nm, df, mn, mx) in enumerate(r.params)][:64]
        s.run("random %d" % k, 40, 3, [45, 57, 64], over, off=32)
    lo, hi = r.params[0][2], r.params[0][3]
    s.run("ramp", 32, 1, [60], [(0, lo)], ramp_block=6, ramp=[(0, hi)])
    s.run("glide", 32, 1, [60], [], glide=(0, lo, hi, 20))
    s.run("velocity", 32, 2, [60, 67], [], vel=[0.4, 1.0])
    return s


def alco_sessions(plg, harness, lm):
    """twin_render_check.py's ALCO scenario set, unchanged"""
    import alco_checks as C
    out = []
    s = Session(plg, harness, lm, "sweep")
    out.append(s)
    r = s.r
    para = {"Chord Mode": 1, "Chord": 24}
    for alg in C.ALGS:
        base = dict(C.LISTEN[alg], **{"Algorithm": C.ALGS.index(alg), "Amp Attack": 0.0, "Amp Sustain": 1.0, "Vel Sens": 0.0})
        for nm, note in (("C3", 48), ("C4", 60), ("C5", 72)):
            s.run("%s %s" % (alg, nm), BLK, 1, [note], over_of(r, base), off=BLK - 8)
        s.run("%s chord4 poly" % alg, BLK, 4, [48, 52, 55, 59], over_of(r, base), off=BLK - 8)
        s.run("%s para maj7 A" % alg, BLK, 1, [60], over_of(r, dict(base, **para, **{"Chord Osc": 0})))
        s.run("%s para maj7 A and B" % alg, BLK, 1, [60], over_of(r, dict(base, **para, **{"Chord Osc": 1})))
        s.run("%s para strum +20" % alg, BLK + 8, 1, [60], over_of(r, dict(base, **para, **{"Chord Osc": 1, "Strum": 20})))
        s.run("%s para strum -20" % alg, BLK + 8, 1, [60], over_of(r, dict(base, **para, **{"Chord Osc": 0, "Strum": -20})))
        for aa in range(4):
            s.run("%s antialias %d" % (alg, aa), BLK, 1, [60], over_of(r, dict(base, Antialias=aa)))
    names = [p[0] for p in r.params]
    for ty in range(10 if "Type" in names else 0):
        for alg in ("Sum", "PM", "Ring", "Fold", "Compare"):
            base = dict(C.LISTEN[alg], **{"Algorithm": C.ALGS.index(alg), "Type": ty, "Resonance": 0.7, "Cutoff": 1200})
            s.run("filter type %d %s" % (ty, alg), BLK, 2, [57, 64], over_of(r, base), off=BLK - 8)
    for k, d in list(C.EXTREME.items()) + list(C.LFO.items()):
        s.run("extreme %s" % k, BLK, 2, [48, 60], over_of(r, d), off=BLK - 8)
        s.run("extreme %s para" % k, BLK, 1, [60], over_of(r, dict(d, **para)))
    for w in range(11 if "A Wave" in names else 0):
        s.run("A Wave %d B Wave %d" % (w, 10 - w), BLK, 1, [60], over_of(r, {"A Wave": w, "B Wave": 10 - w, "B Level": 0.8}))
    s.run("ramp", BLK, 1, [60], over_of(r, {"Cutoff": 800}), ramp_block=6, ramp=[(r.pidx("Cutoff"), 4000.0)])
    s.run("glide", BLK, 1, [60], over_of(r, {}), glide=(r.pidx("Cutoff"), 300.0, 5000.0, 20))
    s.run("velocity", BLK, 2, [60, 67], over_of(r, {"Vel Sens": 0.8}), vel=[0.4, 1.0])
    s.run("slot 2", BLK, 1, [60], over_of(r, {}), slot=2)
    s2 = Session(plg, harness, lm, "strum", strum_depth=100)
    out.append(s2)
    for alg in ("Sum", "Ring", "PM", "Fold", "AM Soft"):
        base = dict(C.LISTEN[alg], **{"Algorithm": C.ALGS.index(alg)})
        for depth in (0, 50, 100):
            s2.set_depth(depth)
            for st in (-20, 20, 40):
                for osc in (0, 1):
                    s2.run("%s depth %d strum %+d osc %d" % (alg, depth, st, osc), 160, 1, [60], over_of(s2.r, dict(base, **para, **{"Chord Osc": osc, "Strum": st})))

    def cycle(kind, n=600):
        t = np.arange(n) / n
        x = {"saw": 2 * t - 1, "square": np.where(t < 0.5, 1.0, -1.0), "tri": 4 * np.abs(t - 0.5) - 1, "h3": np.sin(2 * np.pi * t) + 0.6 * np.sin(2 * np.pi * 3 * t),
             "pulse": np.where(t < 0.2, 1.0, -0.2), "noise": np.convolve(np.r_[np.random.RandomState(7).uniform(-1, 1, n), np.zeros(8)], np.ones(8) / 8, "valid")[:n]}[kind]
        return (x / max(1e-9, np.abs(x).max()) * 30000).astype("<i2")

    def card(spec):
        nm = sorted(spec, key=lambda s_: s_.encode())
        return nm, [cycle(spec[n]) for n in nm]
    base = {"bass/b1_saw.wav": "saw", "bass/b2_square.wav": "square", "lead/l1_tri.wav": "tri", "lead/l2_h3.wav": "h3", "zz_pulse.wav": "pulse"}
    plus = dict(base, **{"aaa/a0_noise.wav": "noise", "aaa/a1_pulse.wav": "pulse"})
    TRI, SAW = "lead/l1_tri.wav", "bass/b1_saw.wav"
    nA, wA = card(base)
    nB, wB = card(plus)
    pidx = s.r.pidx
    AF, BF = pidx("A File"), pidx("B File")
    defaults = [p[1] for p in s.r.params]

    def vals(**kw):
        v = list(defaults)
        for k, x in kw.items():
            v[pidx(k.replace("_", " "))] = float(x)
        return v

    def render(w, v, part=0, times=4, blocks=BLK, name=""):
        w.service(part, v, times)
        over = [(i, float(x)) for i, x in enumerate(v) if struct.pack("<f", x) != struct.pack("<f", defaults[i])]
        w.run(name, blocks, 1, [60], over, off=BLK - 8, slot=part)

    cases = (("tail6 index only", 6, nA, wA, {}, set(), lambda n: vals(A_Wave=10, A_File=n.index(TRI) + 1)),
             ("tail6 named on its card", 6, nA, wA, {(0, AF): TRI}, set(), lambda n: vals(A_Wave=10, A_File=n.index(TRI) + 1)),
             ("tail6 named, indices shifted", 6, nB, wB, {(0, AF): TRI}, set(), lambda n: vals(A_Wave=10, A_File=nA.index(TRI) + 1)),
             ("tail6 no name, indices shifted", 6, nB, wB, {}, set(), lambda n: vals(A_Wave=10, A_File=nA.index(TRI) + 1)),
             ("tail6 named, file gone", 6, [x for x in nA if x != TRI], [w for x, w in zip(nA, wA) if x != TRI], {(0, AF): TRI}, set(), lambda n: vals(A_Wave=10, A_File=nA.index(TRI) + 1)),
             ("tail6 read_path fails, same index", 6, nA, wA, {(0, AF): TRI}, {TRI}, lambda n: vals(A_Wave=10, A_File=n.index(TRI) + 1)),
             ("tail6 read_path fails, shifted", 6, nB, wB, {(0, AF): TRI}, {TRI}, lambda n: vals(A_Wave=10, A_File=nA.index(TRI) + 1)),
             ("tail6 wave mix A and B by name", 6, nB, wB, {(0, AF): TRI, (0, BF): SAW}, set(),
              lambda n: vals(A_Wave=10, A_File=nA.index(TRI) + 1, B_Wave=9, B_File=nA.index(SAW) + 1, Algorithm=18, Amount=0.5)),
             ("tail6 wave mix only A named", 6, nB, wB, {(0, AF): TRI}, set(),
              lambda n: vals(A_Wave=10, A_File=nA.index(TRI) + 1, B_Wave=9, B_File=nA.index(SAW) + 1, Algorithm=18, Amount=0.5)),
             ("tail3 no by path tail", 3, nB, wB, None, set(), lambda n: vals(A_Wave=10, A_File=nA.index(TRI) + 1)),
             ("no files extension", None, None, None, None, set(), lambda n: vals(A_Wave=10, A_File=3)))
    for label, tail, nm, wv, refs, bad, mk in cases:
        w = Session(plg, harness, lm, "wave-" + label.replace(" ", "_").replace(",", ""), files=None if tail is None else [np.asarray(x) for x in wv],
                    paths=nm, refs=None if tail != 6 else dict(refs))
        w.r.bad = set(bad)
        out.append(w)
        render(w, mk(nm or []), name=label)
        if tail == 6 and refs and (0, AF) in refs:
            w.r.refs = {(0, AF): TRI, (1, AF): SAW}
            v = mk(nm)
            w.service(0, v, 1)
            w.service(1, v, 1)
            over = [(i, float(x)) for i, x in enumerate(v) if struct.pack("<f", x) != struct.pack("<f", defaults[i])]
            w.run(label + " part 0", BLK, 1, [60], over, off=BLK - 8, slot=0)
            w.run(label + " part 1", BLK, 1, [60], over, off=BLK - 8, slot=1)
            w.r.names = nB
            w.r.files = [np.asarray(x) for x in wB]
            render(w, v, name=label + " card changed")
    return out


def fnv(b, h=0x811C9DC5):
    for c in b:
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h


def bridge(plg, name, label, lines, tmp):
    """the transcript on the bridge: a fresh card with this engine, ui_shot plugrender ... install. (ok, out bytes, ext lines, pb lines, log)"""
    tr = os.path.join(tmp, label + ".tr")
    out = os.path.join(tmp, label + ".out")
    open(tr, "w").write("\n".join(lines) + "\n")
    sd = os.path.join(tmp, "sd-" + label)
    shutil.rmtree(sd, ignore_errors=True)
    os.makedirs(os.path.join(sd, "Plugins", "Synth"))
    for ext in (".plg", ".plgi"):
        if os.path.exists(plg[:-4] + ext):
            shutil.copy(plg[:-4] + ext, os.path.join(sd, "Plugins", "Synth"))
    env = dict(os.environ, UISHOT_NO_SETTLE="1", UISHOT_AXF=os.path.abspath(a.axf))
    p = subprocess.run([os.path.abspath(a.ui_shot), sd, "plugrender %s %s %s install" % (name, tr, out)], env=env, capture_output=True, text=True, timeout=3600)
    log = (p.stdout + p.stderr).strip()
    ok = p.returncode == 0 and os.path.exists(out)
    got = open(out, "rb").read() if ok else b""
    ext = open(out + ".ext").read().splitlines() if os.path.exists(out + ".ext") else []
    pb = open(out + ".pb").read().splitlines() if os.path.exists(out + ".pb") else []
    shutil.rmtree(sd, ignore_errors=True)
    return ok, got, ext, pb, log



def check_engine(plg):
    """one card engine: every session in the reference, then on the bridge. Returns (file, name, lines, fails, checks, runs, cost)"""
    msgs = []
    fails = checks = 0

    def check(ok, msg):
        nonlocal fails, checks
        checks += 1
        fails += 0 if ok else 1
        if not ok or not a.quick:
            msgs.append(("ok    " if ok else "FAIL  ") + msg)
    lm = R.ImageLibm(os.path.abspath(a.axf))
    harness = HARNESS
    probe = R.Run(plg, harness, image_libm=lm, count=False)
    name = ext_name(probe)
    t0 = time.time()
    sessions = alco_sessions(plg, harness, lm) if name == "ALCO" else generic_sessions(plg, harness, lm)
    tref = time.time() - t0
    tmp = tempfile.mkdtemp(prefix="bridgecheck-" + os.path.basename(plg)[:-4] + "-")
    cost = []
    runs = 0
    for s in sessions:
        ok, got, ext, pb, log = bridge(plg, name, s.label, s.lines, tmp)
        last = log.splitlines()[-1] if log else "no output"
        check(ok, "%s %s: the bridge ran the transcript (%s)" % (name, s.label, last))
        if not ok:
            continue
        if "bridge cost" in last:
            cost.append(last)
        pos = 0
        bad = []
        for rn, exp in s.expect:
            chunk = got[pos:pos + len(exp)]
            pos += len(exp)
            runs += 1
            if chunk != exp:
                bad.append(rn)
                if len(bad) <= 3:
                    e = np.frombuffer(exp, dtype="<f4")
                    g = np.frombuffer(chunk, dtype="<f4") if len(chunk) == len(exp) else np.zeros_like(e)
                    d = np.nonzero(e.view("<u4") != g.view("<u4"))[0]
                    msgs.append("      %s: %d of %d samples differ, first at block %d sample %d (reference %.9g, bridge %.9g)" % (
                        rn, len(d), len(e), d[0] // 256 if len(d) else -1, d[0] % 256 if len(d) else -1, e[d[0]] if len(d) else 0, g[d[0]] if len(d) else 0))
        check(pos == len(got), "%s %s: the bridge wrote exactly %d bytes" % (name, s.label, len(got)))
        check(not bad, "%s %s: %d runs byte equal%s" % (name, s.label, len(s.expect), "" if not bad else ", DIFFER: " + ", ".join(bad[:12])))
        if s is sessions[0] and ext:   # the descriptor against the plugin's own (layout is the host's after mirrorSynth: not compared)
            r = s.r
            x = struct.unpack("<IIiiiIIIIIIIiII", bytes(r.uc.mem_read(r.ext, 60)))
            head = ext[0].split()
            check((int(head[2]), int(head[3]), int(head[4])) == (x[2], x[3], x[4]) and head[5] == name,
                  "%s descriptor: stored %s patch engine %s type id %s name equal" % (name, head[2], head[3], head[4]))
            okp = len(ext) - 1 == len(r.params) and int(head[6]) == x[7] and int(head[9]) == x[12]
            for i, (pn, df, mn, mx) in enumerate(r.params):
                if i + 1 >= len(ext):
                    break
                q = ext[i + 1].split()
                want = [struct.unpack("<I", struct.pack("<f", v))[0] for v in (df, mn, mx)]
                okp = okp and q[0] == "p" and " ".join(q[1:-5]) == pn and [int(t, 16) for t in q[-5:-2]] == want
            check(okp, "%s descriptor: %d parameters, volume parameter, defaults and ranges equal bit for bit" % (name, len(r.params)))
        if a.keep:
            d = os.path.join(a.keep, os.path.basename(plg)[:-4])
            os.makedirs(d, exist_ok=True)
            for f in (s.label + ".tr", s.label + ".out", s.label + ".out.pb"):
                if os.path.exists(os.path.join(tmp, f)):
                    shutil.copy(os.path.join(tmp, f), d)
    if a.unit_dir:
        u = unit_session(plg, harness, lm)
        base = os.path.join(a.unit_dir, os.path.basename(plg)[:-4])
        open(base + ".tr", "w").write("\n".join(u.lines) + "\n")
        ref = []
        for k, (rn, data) in enumerate(u.expect):
            for b in range(len(data) // 1024):
                ref.append("pb %d %d %08x" % (k, b, fnv(data[b * 1024:(b + 1) * 1024])))
        open(base + ".ref.pb", "w").write("\n".join(ref) + "\n")
        ok, got, ext, pb, log = bridge(plg, name, "unit", u.lines, tmp)
        open(base + ".bridge.pb", "w").write("\n".join(pb) + "\n")
        open(base + ".name", "w").write(name + "\n")
        check(ok and [l for l in pb if l.startswith("pb ")] == ref, "%s unit transcript: bridge and reference per block FNV equal (%d blocks)" % (name, len(ref)))
    shutil.rmtree(tmp, ignore_errors=True)
    msgs.append("%s: %d sessions, %d runs, reference %.0f s, libm calls %d (%d cached values)" % (name, len(sessions), runs, tref, lm.calls, len(lm.cache)))
    return os.path.basename(plg), name, msgs, fails, checks, runs, cost


HARNESS = None


def init(h):
    global HARNESS
    HARNESS = h


def main():
    tmp = tempfile.mkdtemp(prefix="bridgecheck")
    harness = a.harness
    if not harness:
        harness = os.path.join(tmp, "harness.plg")
        R.build_harness(os.environ["TOOLCHAIN"], harness)
    if a.unit_dir:
        os.makedirs(a.unit_dir, exist_ok=True)
    sd = os.path.join(a.card, "Plugins", "Synth")
    plgs = sorted(os.path.join(sd, f) for f in os.listdir(sd) if f.endswith(".plg"))
    if a.only:
        plgs = [p for p in plgs if os.path.basename(p)[:-4] in a.only.split(",")]
    print("bridge_check: %d synth .plg of %s, axf %s, %s blocks per run, %d jobs" % (len(plgs), sd, a.axf, BLK, a.jobs), flush=True)
    t0 = time.time()
    with multiprocessing.Pool(a.jobs, initializer=init, initargs=(harness,)) as pool:
        res = pool.map(check_engine, sorted(plgs, key=lambda p: -os.path.getsize(p)), chunksize=1)
    fails = checks = runs = 0
    green = []
    for f, name, msgs, fl, ck, rn, cost in sorted(res):
        print("\n== %s (%s)" % (name, f))
        for m in msgs:
            print(m)
        for c in cost:
            print("   " + c)
        fails += fl
        checks += ck
        runs += rn
        if not fl:
            green.append(name)
    print("\nsynth engines byte equal: %d of %d (%s)" % (len(green), len(res), " ".join(green)))
    print("%s: %d engines, %d runs compared, %d checks, %d failed, %.0f s" % ("RESULT: GREEN" if not fails and res else "RESULT: RED", len(res), runs, checks,
                                                                           fails, time.time() - t0))
    shutil.rmtree(tmp, ignore_errors=True)
    return 1 if fails or not res else 0


if __name__ == "__main__":
    sys.exit(main())
