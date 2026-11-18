#!/usr/bin/env python3
"""Wall time of one 128 sample block of the REAL release synth .plg bytes under Unicorn on this Mac.

Mode "fast": the per-instruction cost hook of emu_render.Run is dropped (only the host service stubs trap), the way a
production bridge would run. Mode "count": the hook stays, gives the instruction count per block (for MIPS).
Usage: bench_synth.py CARD_SYNTH_DIR HARNESS OUT.json [names...]
"""
import json, os, sys, time
sys.path.insert(0, os.path.join(os.environ.get("ENGINES_DIR", os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../../poly-engines")), "tools"))
import unicorn
import emu_render as R

card, harness, out = sys.argv[1:4]
only = sys.argv[4:]
BLOCKS = int(os.environ.get("BLOCKS", "120"))
orig_hook_add = unicorn.Uc.hook_add
FAST = [True]


def hook_add(self, htype, cb, user_data=None, begin=1, end=0, *a):
    # drop the global per-instruction tick and the SDRAM access counter in fast mode
    if FAST[0] and begin == 1 and end == 0:
        return 0
    if FAST[0] and htype & (unicorn.UC_HOOK_MEM_READ | unicorn.UC_HOOK_MEM_WRITE):
        return 0
    return orig_hook_add(self, htype, cb, user_data, begin, end, *a)


unicorn.Uc.hook_add = hook_add
names = sorted(f[:-4] for f in os.listdir(card) if f.endswith(".plg"))
if only:
    names = [n for n in names if n in only]
res = {}
for n in names:
    row = {}
    for voices in (1, 4):
        FAST[0] = True
        r = R.Run(os.path.join(card, n + ".plg"), harness)
        notes = [48, 55, 60, 64][:voices]
        over = [(i, p[3]) for i, p in enumerate(r.params)][:64] if os.environ.get("SET") == "max" else []
        r.run(4, voices, notes, 10 ** 6, over)   # warm the TB cache
        t0 = time.perf_counter()
        r.run(BLOCKS, voices, notes, 10 ** 6, over)
        dt = (time.perf_counter() - t0) / BLOCKS
        FAST[0] = False
        rc = R.Run(os.path.join(card, n + ".plg"), harness)
        _, instr, est = rc.run(40, voices, notes, 10 ** 6, over)
        ins = float(sum(int(x) for x in instr[8:])) / len(instr[8:])
        es = float(sum(int(x) for x in est[8:])) / len(est[8:])
        row["v%d" % voices] = {"us_per_block": dt * 1e6, "instr_per_block": ins, "est_cycles_per_block": es,
                               "mips": ins / dt / 1e6, "est_pct_device": 100 * es / R.BLOCK_CYCLES}
    res[n] = row
    v1, v4 = row["v1"], row["v4"]
    print("%-8s 1v %7.0f us %8.0f ins %5.1f%% | 4v %7.0f us %8.0f ins %5.1f%% dev, %5.0f MIPS, %4.1fx realtime" % (
        n, v1["us_per_block"], v1["instr_per_block"], v1["est_pct_device"], v4["us_per_block"], v4["instr_per_block"],
        v4["est_pct_device"], v4["mips"], 2902.5 / v4["us_per_block"]), flush=True)
json.dump(res, open(out, "w"), indent=1)
