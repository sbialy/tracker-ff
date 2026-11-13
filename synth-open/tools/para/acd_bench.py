#!/usr/bin/env python3
"""ACD paraphony on the Mac (host only, NOT firmware): render diff and emulated CPU of the built-in ACD engine, run as a plugin twin
(acd_plug.cpp, the engine sources with the firmware's engine flags -O2 -ffp-contract=off) in Unicorn under render_harness.cpp.

    TOOLCHAIN=.../12.3.rel1/bin/arm-none-eabi- python3 -I synth-open/tools/para/acd_bench.py BASE_TREE OUT [DEVCOST_DIR]

BASE_TREE: a tracker-ff tree without paraphony (git archive 974c0b8), OUT: plugins, harness and acd_bench.json. DEVCOST_DIR: the
tools/perfmodel folder of the console repository main (devcost.py), the CPU rows are skipped without it.
1. render diff, bit for bit, Cortex-M4 code: the base ACD against this tree's ACD on 400 random patches (the 29 old parameters, 1 to 4
   voices, random notes and gate ends), and this tree's ACD at Chord Mode Para with Chord 0 against Poly. Exit 1 on any difference.
2. chord sanity: Para with Chord maj7 on C3 (one voice) has C3 E3 G3 B3 in its spectrum and the level of the 4 voice Poly chord.
3. CPU: estimated cycles (the run_bench cost table, the one devcost is fitted on) per block, kernel only (engine render and the harness
   voice skeleton, no part path), devcost class engine_voice: 1 voice, the Poly 4 note chord (4 voices) and the Para 4 note chord.
"""
import json
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import emu_render as R  # noqa: E402
import capstone  # noqa: E402

TC = os.environ.get("TOOLCHAIN") or sys.exit("set TOOLCHAIN")
BASE = os.path.abspath(sys.argv[1])
OUT = os.path.abspath(sys.argv[2])
DEVCOST = sys.argv[3] if len(sys.argv) > 3 else None
os.makedirs(OUT, exist_ok=True)

FW = ("-std=gnu++20 -felide-constructors -include cstdint -fno-rtti -fno-exceptions -fno-common -ffunction-sections -fdata-sections "
      "-ffreestanding -fno-builtin -O2 -DNDEBUG -mfp16-format=ieee")
DSP = "-O2 -ffp-contract=off"


def build_acd(tree, out, chord):
    so = os.path.join(tree, "synth-open")
    opt = "-Os -fno-builtin" + (" -DACD_PLUG_CHORD" if chord else "")
    cmd = [sys.executable, os.path.join(so, "plug", "build_plug.py"), "--id", "ff.test.acdtwin", "--kind", "synth", "--version", "1.0.0",
           "--licence", "Apache-2.0", "--opt", opt, "--synth", "200,0x7A00,1",
           "--src-opt", "acd.cpp=%s %s -DSYNTH_ENGINE_ACD" % (FW, DSP), "--src-opt", "engine_acd.cpp=%s -DSYNTH_ENGINE_ACD" % FW,
           "--keep", out.replace(".plg", ".elf"), "--out", out, "-I", ".", "-I", "ot", "-I", "plug", "-I", "../synth/include",
           os.path.join(HERE, "acd_plug.cpp"), "engine_acd.cpp", "ot/audio/engines/acd.cpp"]
    subprocess.run(cmd, cwd=so, env=dict(os.environ, TOOLCHAIN=TC), check=True, stdout=subprocess.DEVNULL)


md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB | capstone.CS_MODE_MCLASS)
LDST = ("ldr", "ldrb", "ldrh", "ldrsb", "ldrsh", "ldrd", "str", "strb", "strh", "strd")
MULTI = ("push", "pop", "ldm", "ldmia", "stm", "stmia", "stmdb", "ldmdb", "vpush", "vpop", "vldmia", "vstmia", "vldmdb", "vstmdb")


def cost_rb(code):   # the run_bench.py cost table (the one devcost is fitted on)
    ins = next(md.disasm(code, 0), None)
    if ins is None:
        return 1
    base, ops = ins.mnemonic.split(".")[0], ins.op_str
    if (base.startswith("b") and base not in ("bic", "bics", "bfc", "bfi", "bkpt", "bxj")) or base in ("cbz", "cbnz"):
        return 1
    if base in ("vdiv", "vsqrt"):
        return 14
    if base in ("vmla", "vmls", "vnmla", "vnmls", "vfma", "vfms", "vfnma", "vfnms"):
        return 3
    if base in ("vldr", "vstr") or base in LDST:
        return 2
    if base in MULTI:
        return 1 + ops.count(",") + 1
    if base in ("sdiv", "udiv"):
        return 6
    if base in ("mla", "mls", "smlal", "umlal"):
        return 2
    return 1


R.cost_of = cost_rb
harness = os.path.join(OUT, "render_harness.plg")
R.build_harness(TC, harness)
base_plg, new_plg, chord_plg = (os.path.join(OUT, n) for n in ("acd_base.plg", "acd_new.plg", "acd_chord.plg"))
build_acd(BASE, base_plg, False)
build_acd(os.path.dirname(SO), new_plg, False)
build_acd(os.path.dirname(SO), chord_plg, True)
val = subprocess.run([sys.executable, "-I", os.path.join(SO, "tools", "validate_synth.py"), chord_plg], capture_output=True, text=True,
                     env=dict(os.environ, TOOLCHAIN=TC))
print("validate_synth (the firmware's checkSynth) on the chord twin: rc %d, %s" % (val.returncode, val.stdout.strip().splitlines()[-1]))
rb, rn = R.Run(base_plg, harness, count_cycles=False), R.Run(new_plg, harness, count_cycles=False)
assert rn.nparams == rb.nparams + 3 == 32, (rb.nparams, rn.nparams)   # + Chord Mode, Chord, Strum
MODE, CHORD = rn.pidx("Chord Mode"), rn.pidx("Chord")
ENUMS = {"Sub Mode", "Type", "Waveform", "Retrigger", "Glide Mode"}

# 1. render diff
rng = np.random.default_rng(20261112)
BLOCKS = 48
diff = same = para0 = 0
for case in range(400):
    over = []
    for i, (nm, df, mn, mx) in enumerate(rb.params):
        if rng.random() < 0.6:
            v = float(rng.integers(int(mn), int(mx) + 1)) if nm in ENUMS else float(mn + (mx - mn) * rng.random())
            over.append((i, v))
    nv = int(rng.integers(1, 5))
    notes = [int(x) for x in rng.integers(24, 100, nv)]
    off = int(rng.integers(2, BLOCKS))
    a = rb.run(BLOCKS, nv, notes, off, over)[0]
    b = rn.run(BLOCKS, nv, notes, off, over)[0]
    if a.tobytes() == b.tobytes():
        same += 1
    else:
        diff += 1
        print("DIFF case %d voices %d notes %s max %.3g" % (case, nv, notes, float(np.max(np.abs(a - b)))))
    if case % 4 == 0:   # Para with Chord 0 (the note alone) against Poly, same patch
        c = rn.run(BLOCKS, nv, notes, off, over + [(MODE, 1.0), (CHORD, 0.0)])[0]
        para0 += c.tobytes() == b.tobytes()
print("render diff base vs new (Poly, 400 random patches, Cortex-M4 code in Unicorn): %d bit identical, %d differ" % (same, diff))
print("Para with Chord 0 vs Poly: %d of 100 bit identical" % para0)

# 2. chord sanity: an open filter so the partials show, maj7 on C3
OPEN = [(rn.pidx("Cutoff"), 20000.0), (rn.pidx("Env Amt"), 0.0), (rn.pidx("Sustain"), 1.0)]
pf = rn.run(96, 1, [48], 10 ** 6, OPEN + [(MODE, 1.0), (CHORD, 24.0)])[0][16:, 0, :].ravel()
pp = rn.run(96, 4, [48, 52, 55, 59], 10 ** 6, OPEN)[0][16:, 0, :].ravel()
spec = np.abs(np.fft.rfft(pf * np.hanning(len(pf))))
fr = np.fft.rfftfreq(len(pf), 1.0 / 44117.647)
hz = lambda n: 440.0 * 2 ** ((n - 69) / 12.0)
peak = lambda f: float(spec[np.argmin(np.abs(fr - f))])
noise = float(np.median(spec[(fr > 100) & (fr < 300)]))
rms = lambda x: float(np.sqrt(np.mean(x * x)))
sanity = dict(C3=peak(hz(48)), E3=peak(hz(52)), G3=peak(hz(55)), B3=peak(hz(59)), D3=peak(hz(50)), A3=peak(hz(57)), floor=noise,
              rms_para=rms(pf), rms_poly4=rms(pp), finite=bool(np.isfinite(pf).all()), peak_para=float(np.max(np.abs(pf))))
print("chord sanity maj7 on C3 (Para, 1 voice): partials C3 %.0f E3 %.0f G3 %.0f B3 %.0f, off chord D3 %.0f A3 %.0f, floor %.1f, "
      "rms para %.3f vs poly 4 voices %.3f, peak %.3f" % (sanity["C3"], sanity["E3"], sanity["G3"], sanity["B3"], sanity["D3"],
                                                          sanity["A3"], noise, sanity["rms_para"], sanity["rms_poly4"], sanity["peak_para"]))
chord_ok = (sanity["finite"] and min(sanity["C3"], sanity["E3"], sanity["G3"], sanity["B3"]) > 10 * max(sanity["D3"], sanity["A3"])
            and 0.5 < sanity["rms_para"] / sanity["rms_poly4"] < 2.0)

# 3. CPU, kernel only (the instances with the cycle hook)
rb, rn = R.Run(base_plg, harness), R.Run(new_plg, harness)
rows = []
model = None
if DEVCOST:
    sys.path.insert(0, DEVCOST)
    import devcost  # noqa: E402
    model = devcost.load_model(None)
SKIP, NB = 4, 64
CHORD4 = [48, 52, 55, 59]
CASES = [("default (LP SVF 24, saw)", []), ("LP SVF 12", [(rn.pidx("Type"), 0.0)]), ("LP RD3 ladder", [(rn.pidx("Type"), 2.0)]),
         ("saw + square (pulse)", [(rn.pidx("Square Mix"), 0.6)]), ("saw + sub + pre noise", [(rn.pidx("Sub Mix"), 0.5), (rn.pidx("Noise Mix"), 0.3)])]
for name, over in CASES:
    est = {}
    est["base1"] = float(rb.run(NB, 1, CHORD4[:1], 10 ** 6, over)[2][SKIP:].mean())
    est["poly1"] = float(rn.run(NB, 1, CHORD4[:1], 10 ** 6, over)[2][SKIP:].mean())
    est["base4"] = float(rb.run(NB, 4, CHORD4, 10 ** 6, over)[2][SKIP:].mean())
    est["poly4"] = float(rn.run(NB, 4, CHORD4, 10 ** 6, over)[2][SKIP:].mean())
    est["para4"] = float(rn.run(NB, 1, CHORD4[:1], 10 ** 6, over + [(MODE, 1.0), (CHORD, 24.0)])[2][SKIP:].mean())
    est["para3"] = float(rn.run(NB, 1, CHORD4[:1], 10 ** 6, over + [(MODE, 1.0), (CHORD, 5.0)])[2][SKIP:].mean())
    row = dict(case=name, emu=est)
    if model:
        for k, v in est.items():
            p, lo, hi = model.predict("engine_voice", v)
            row[k + "_us"] = p / devcost.US
        row["label"] = model.label("engine_voice")
        row["block_us"] = devcost.BLOCK / devcost.US
    rows.append(row)
    if model:
        print("%-26s base 1v %6.1f | new 1v %6.1f | poly chord 4v %6.1f | para chord 4 notes %6.1f (%.0f %% of poly) | para 3 notes %6.1f us  %s"
              % (name, row["base1_us"], row["poly1_us"], row["poly4_us"], row["para4_us"], 100 * row["para4_us"] / row["poly4_us"],
                 row["para3_us"], row["label"]))
    else:
        print(name, est)
json.dump(dict(render_same=same, render_diff=diff, para0_same=para0, sanity=sanity, chord_ok=chord_ok, cpu=rows,
               validate_rc=val.returncode), open(os.path.join(OUT, "acd_bench.json"), "w"), indent=1)
ok = diff == 0 and para0 == 100 and chord_ok and val.returncode == 0
print("ACD BENCH " + ("PASS" if ok else "FAIL"))
sys.exit(0 if ok else 1)
