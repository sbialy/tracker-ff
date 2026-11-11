# Host side of part_cycles/run_bench.sh. Unicorn Cortex-M4F emulation, run with python -I.
#   run_bench.py <elf> <toolchain-prefix> <scenario> <topn>
# Cycles: Thumb-2 instruction count mapped through the cost table of tools/audio_opt/run_graph.py (loads and stores 2, taken branches 3, ...), no
# flash or SDRAM wait states. SDRAM (0x70000000..) reads and writes are counted separately: device cycles = emulated * 0.93 + 16.9 * SDRAM accesses.
# Output: per function (exclusive) and per source line of the hottest functions, for the whole bench_run(NB) per block.
import os, subprocess, sys
from collections import defaultdict
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "audio_opt"))
import run_graph as rg

elf, tc, sc, topn = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_SP

uc, syms, ranges = rg.load(elf, tc)
rg.call(uc, rg.find(syms, "bench_setup"), sc)
rg.call(uc, rg.find(syms, "bench_run"), 4)
uc.ctl_flush_tb()
import struct
print("output hash after setup + 4 blocks: %08x" % struct.unpack("<I", bytes(uc.mem_read(syms["gHash"], 4)))[0])
per_r, per_w = defaultdict(int), defaultdict(int)
cur = [0]


def hr(u, a, addr, size, v, ud):
    per_r[cur[0]] += 1


def hw(u, a, addr, size, v, ud):
    per_w[cur[0]] += 1


uc.hook_add(UC_HOOK_MEM_READ, hr, begin=rg.SDR, end=rg.SDR + rg.SDR_SIZE - 1)
uc.hook_add(UC_HOOK_MEM_WRITE, hw, begin=rg.SDR, end=rg.SDR + rg.SDR_SIZE - 1)
md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
cache = {}


def info(addr):
    if addr not in cache:
        ins = next(md.disasm(bytes(uc.mem_read(addr, 4)), addr))
        cache[addr] = (ins.size, ins.mnemonic, ins.op_str)
    return cache[addr]


NB = 3
trace = []
uc.reg_write(UC_ARM_REG_SP, rg.TRAP - 0x1000)
uc.reg_write(UC_ARM_REG_LR, rg.TRAP | 1)
uc.reg_write(UC_ARM_REG_R0, NB)
pc = rg.find(syms, "bench_run") | 1
while (pc & ~1) != rg.TRAP:
    cur[0] = pc & ~1
    trace.append(pc & ~1)
    uc.emu_start(pc, rg.TRAP, count=1)
    pc = uc.reg_read(UC_ARM_REG_PC) | 1


def func_of(addr):
    lo_i, hi_i = 0, len(ranges) - 1
    while lo_i <= hi_i:
        mid = (lo_i + hi_i) // 2
        lo, hi, name = ranges[mid]
        if addr < lo:
            hi_i = mid - 1
        elif addr >= hi:
            lo_i = mid + 1
        else:
            return name
    return "?"


cyc_pc = defaultdict(float)
for i, a in enumerate(trace):
    size, mn, ops = info(a)
    taken = i + 1 < len(trace) and trace[i + 1] != a + size
    cyc_pc[a] += rg.cost(mn, ops, taken)
pcs = sorted(set(cyc_pc) | set(per_r) | set(per_w))
# source lines
lines = {}
out = subprocess.run([tc + "-addr2line", "-e", elf, "-C", "-i"] + [hex(a) for a in pcs], capture_output=True, text=True).stdout.splitlines()
# -i prints several lines per address; use the innermost (first) only: rerun without -i for a 1:1 map
out = subprocess.run([tc + "-addr2line", "-e", elf, "-f", "-C"] + [hex(a) for a in pcs], capture_output=True, text=True).stdout.splitlines()
for k, a in enumerate(pcs):
    lines[a] = (out[2 * k], out[2 * k + 1])
per_fn = defaultdict(lambda: [0.0, 0, 0])
per_ln = defaultdict(lambda: [0.0, 0, 0])
for a in pcs:
    fn = func_of(a)
    for tbl, key in ((per_fn, fn), (per_ln, lines[a][1].replace("/Users/sbialy/prj/", "~/prj/") + "  " + lines[a][0][:40])):
        tbl[key][0] += cyc_pc.get(a, 0)
        tbl[key][1] += per_r.get(a, 0)
        tbl[key][2] += per_w.get(a, 0)
tot = sum(v[0] for v in per_fn.values()) / NB
sd = sum(v[1] + v[2] for v in per_fn.values()) / NB
dev = tot * 0.93 + 16.9 * sd
print(f"{os.path.basename(elf)} sc={sc}: {tot:.0f} emulated cycles per block, {sd:.0f} SDRAM accesses -> device estimate {dev:.0f} cycles = {dev / 180:.1f} us")
print("  per function (exclusive): emulated cyc/block, SDRAM r/w, device-est us")
for k, v in sorted(per_fn.items(), key=lambda kv: -(kv[1][0] * 0.93 + 16.9 * (kv[1][1] + kv[1][2])))[:topn]:
    print(f"    {v[0] / NB:>8.0f} {v[1] / NB:>6.0f}/{v[2] / NB:<6.0f} {(v[0] * 0.93 + 16.9 * (v[1] + v[2])) / NB / 180:>7.2f}  {k[:110]}")
print("  per source line:")
for k, v in sorted(per_ln.items(), key=lambda kv: -(kv[1][0] * 0.93 + 16.9 * (kv[1][1] + kv[1][2])))[:topn]:
    print(f"    {v[0] / NB:>8.0f} {v[1] / NB:>6.0f}/{v[2] / NB:<6.0f} {(v[0] * 0.93 + 16.9 * (v[1] + v[2])) / NB / 180:>7.2f}  {k[:130]}")
