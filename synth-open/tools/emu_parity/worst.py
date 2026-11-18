import json
S = json.load(open("synth_bench.json")); M = json.load(open("synth_bench_max.json")); I = json.load(open("insert_bench.json"))
BUD = 2902.5; LO, HI = 95.0, 200.0   # measured MIPS band of this Mac (loaded E/P mix .. quiet P core)
def v8(d):  # 8 voice extrapolation: base + 7 x per voice increment (4v - 1v)/3, instructions and est cycles
    pv_i = (d["v4"]["instr_per_block"] - d["v1"]["instr_per_block"]) / 3
    pv_e = (d["v4"]["est_cycles_per_block"] - d["v1"]["est_cycles_per_block"]) / 3
    return d["v1"]["instr_per_block"] + 7 * pv_i, d["v1"]["est_cycles_per_block"] + 7 * pv_e
ins_max = sorted(((max(r["def"]["instr_per_block"], r["max"]["instr_per_block"]), k) for k, r in I.items()), reverse=True)
two = ins_max[0][0] + ins_max[1][0]
print("inserts worst pair %s+%s: %.0f instr/block -> %.0f..%.0f us" % (ins_max[0][1], ins_max[1][1], two, two / HI, two / LO))
print("%-7s %9s %9s %7s | %9s %9s | %9s %9s %9s" % ("engine", "8v instr", "8v dev%", "set", "1 part us", "x RT", "3x8+2ins", "x RT(1thr)", "x RT(4thr)"))
rows = []
for src, tag in ((S, "def"), (M, "max")):
    for k, d in src.items():
        i8, e8 = v8(d)
        dev = 100 * e8 / 522240
        tot = 3 * i8 + two
        rows.append((tot, k, tag, i8, dev))
for tot, k, tag, i8, dev in sorted(rows, reverse=True)[:10]:
    print("%-7s %9.0f %8.0f%% %7s | %4.0f-%4.0f %4.2f-%4.2f | %9.0f %4.1f-%4.1f %4.1f-%4.1f" % (
        k, i8, dev, tag, i8 / HI, i8 / LO, i8 / HI / BUD, i8 / LO / BUD, tot, tot / HI / BUD, tot / LO / BUD,
        max(i8, two) / HI / BUD, max(i8, two) / LO / BUD))
# what the device can reach: governor ~100 % of 522,240 cycles. instructions per device cycle from the est model
r = [d["v4"]["instr_per_block"] / d["v4"]["est_cycles_per_block"] for d in list(S.values()) + list(M.values())]
ipc_lo, ipc_hi = min(r), max(r)
full = 522240
print("device-saturating plugin load (100 %% of 522,240 cycles): %.0f..%.0f instr -> %.0f..%.0f us single thread (%.2f..%.2fx RT)" % (
    full * ipc_lo, full * ipc_hi, full * ipc_lo / HI, full * ipc_hi / LO, full * ipc_lo / HI / BUD, full * ipc_hi / LO / BUD))
