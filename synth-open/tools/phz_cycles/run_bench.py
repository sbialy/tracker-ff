# Engine cycle bench, host side (see run_bench.sh). Runs bench_arm.elf's bench_run() in a Unicorn emulator, counts the
# executed Thumb-2 instructions (single stepped) and
# converts them to Cortex-M4F cycles with the cost table below. Not cycle accurate: no flash wait states, no cache,
# loads and stores at 2 cycles, taken branches at 3, VMLA at 3, VDIV and VSQRT at 14. Run with python -I.
import os
import struct
import subprocess
import sys
from collections import Counter, defaultdict

from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs
from unicorn import UC_ARCH_ARM, UC_MODE_THUMB, Uc
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_A15, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0,
                               UC_ARM_REG_S0, UC_ARM_REG_SP)

ELF, TC = sys.argv[1], sys.argv[2]
BASE, SIZE = 0x20000000, 0x400000
TRAP = BASE + 0x3F0000
BLOCK_CYCLES = 128 * 180e6 / 44117.647   # cycles available per 128 frame block at 180 MHz
NB = 16                                  # blocks measured per scenario (after 6 warm up blocks)


def sh(*a):
    return subprocess.run(a, check=True, capture_output=True, text=True).stdout


subprocess.run([TC + "-objcopy", "-O", "binary", ELF, ELF + ".bin"], check=True)
image = open(ELF + ".bin", "rb").read()
syms = {}
ranges = []
for line in sh(TC + "-nm", "-S", "-C", "--defined-only", ELF).splitlines():
    parts = line.split(None, 3)
    if len(parts) == 4 and parts[2] in "tTwW":
        a, n, name = int(parts[0], 16), int(parts[1], 16), parts[3]
        syms[name] = a
        ranges.append((a, a + n, name))
ranges.sort()
syms_d = {}
for line in sh(TC + "-nm", "--defined-only", ELF).splitlines():
    parts = line.split()
    if len(parts) == 3:
        syms_d[parts[2]] = int(parts[0], 16)
LAST_HASH = 0


def func_of(addr):
    for lo, hi, name in ranges:
        if lo <= addr < hi:
            return name
    return "?"


uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
# the default core is an ARMv5 one (no Thumb-2, no VFP): use an ARMv7-A core, it runs the M4F code unchanged
uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
uc.mem_map(BASE, SIZE)
uc.mem_write(BASE, image)
uc.mem_write(TRAP, b"\xfe\xe7")   # b .
uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))
uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)


def find(prefix):
    for k in syms:
        if k == prefix or k.startswith(prefix + "("):
            return k
    raise KeyError(prefix)


NS, NR, PK = find("bench_setup"), find("bench_run"), find("bench_peak")


def stepped(fn, arg, visit=None):
    """Runs fn(arg) one instruction at a time (the translated block mode of Unicorn mishandles Thumb-2 IT blocks that
    hold VFP instructions, single stepping is exact). visit(pc) is called before each instruction."""
    uc.reg_write(UC_ARM_REG_SP, BASE + SIZE - 0x10000)
    uc.reg_write(UC_ARM_REG_LR, TRAP | 1)
    uc.reg_write(UC_ARM_REG_R0, arg)
    pc = syms[fn] | 1
    while (pc & ~1) != TRAP:
        if visit:
            visit(pc & ~1)
        uc.emu_start(pc, TRAP, count=1)
        pc = uc.reg_read(UC_ARM_REG_PC) | 1


md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
md.detail = False
_cache = {}


def info(addr):
    if addr not in _cache:
        ins = next(md.disasm(bytes(uc.mem_read(addr, 4)), addr))
        _cache[addr] = (ins.size, ins.mnemonic, ins.op_str)
    return _cache[addr]


def cost(mn, ops, taken):
    base = mn.split(".")[0]
    if base.startswith("b") and base not in ("bic", "bics", "bfc", "bfi", "bkpt", "bxj", "bl", "blx", "bx") or base in ("cbz", "cbnz"):
        return 3 if taken else 1
    if base in ("bl", "blx", "bx"):
        return 3
    if base in ("vdiv", "vsqrt"):
        return 14
    if base in ("vmla", "vmls", "vnmla", "vnmls", "vfma", "vfms", "vfnma", "vfnms"):
        return 3
    if base in ("vldr", "vstr"):
        return 2
    if base in ("ldr", "ldrb", "ldrh", "ldrsb", "ldrsh", "ldrd", "str", "strb", "strh", "strd"):
        return 2
    if base in ("push", "pop", "ldm", "ldmia", "stm", "stmia", "stmdb", "ldmdb", "vpush", "vpop", "vldmia", "vstmia", "vldmdb", "vstmdb"):
        return 1 + ops.count(",") + 1 + (2 if "pc" in ops else 0)
    if base in ("sdiv", "udiv"):
        return 6
    if base in ("mla", "mls", "smlal", "umlal"):
        return 2
    return 1


def measure(sc):
    stepped(NS, sc)
    trace = []
    stepped(NR, NB, trace.append)
    peak_bits = uc.reg_read(UC_ARM_REG_S0) & 0xFFFFFFFF
    stepped(PK, 0)
    peak = struct.unpack("<f", struct.pack("<I", uc.reg_read(UC_ARM_REG_S0) & 0xFFFFFFFF))[0]
    global LAST_HASH
    LAST_HASH = struct.unpack("<I", bytes(uc.mem_read(syms_d["gBenchHash"], 4)))[0]
    uc.mem_write(syms_d["gBenchHash"], struct.pack("<I", 0x811C9DC5))
    if os.environ.get("DUMP_DIR"):
        open(os.path.join(os.environ["DUMP_DIR"], "sc%d.f32" % sc), "wb").write(bytes(uc.mem_read(syms_d["gDump"], 16 * 2 * 128 * 4)))
    uc.mem_write(syms_d["gDumpN"], struct.pack("<I", 0))
    cycles = 0
    per_fn = defaultdict(float)
    for i, a in enumerate(trace):
        size, mn, ops = info(a)
        if func_of(a) in ("bench_run", "bench_hash"):   # the harness: zeroing the two output buffers, not engine work
            continue
        taken = i + 1 < len(trace) and trace[i + 1] != a + size
        c = cost(mn, ops, taken)
        cycles += c
        per_fn[func_of(a)] += c
    return len(trace) - sum(1 for a in trace if func_of(a) in ("bench_run", "bench_hash")), cycles, per_fn, peak


LAST_PEAK = 0.0


def line(name, sc, detail=False):
    global LAST_PEAK
    insns, cycles, per_fn, peak = measure(sc)
    LAST_PEAK = peak if peak == peak else 0.0
    pb = cycles / NB
    print(f"{name:<62} {insns / NB:>9.0f} {pb:>9.0f} {100 * pb / BLOCK_CYCLES:>6.2f}%  peak {peak:.2f}  hash {LAST_HASH:08x}")
    if peak != peak:
        print("      !! output not finite, the figure is not valid")
    elif peak < 0.01 and pb < 5000:
        print("      !! voices ended (silent), the figure is not valid")
    elif peak < 0.01:
        print("      (quiet output, voices still render: cycles count)")
    if detail:
        for fn, c in sorted(per_fn.items(), key=lambda kv: -kv[1])[:8]:
            print(f"      {c / NB:>9.0f}  {fn[:100]}")
    return pb


import os
ENGINE = os.environ.get("ENGINE", "ACD")
print(f"block budget at 180 MHz: {BLOCK_CYCLES:,.0f} cycles. Target: under 6 % = {0.06 * BLOCK_CYCLES:,.0f} cycles per voice per block")
print(f"{'scenario (per block, steady state, all voices of the bench)':<62} {'M4 insn':>9} {'cycles':>9} {'%blk':>7}")
# bench_engine.cpp: scenario 0 = default patch, CHIP: 1 + model, 9 = sustain, 20 / 21 / 22 = every parameter at max / mid / min
name = os.environ.get("ENGINE_NAME", ENGINE)
cases = [(0, "default patch")]
if ENGINE == "CHIP":
    cases += [(1 + i, "model %d %s" % (i, n)) for i, n in enumerate(
        ["2A03 Pulse", "SID Saw/Tri", "Noise / LFSR", "FM Bell", "Super Square", "Zap / Laser", "Arp Chord", "Chip Drum"])]
    cases += [(9, "default patch held in sustain (Decay 0.01 s)")]
cases += [(20, "all parameters max"), (21, "all parameters mid"), (22, "all parameters min")]
# synth automation (bench_engine.cpp): 30 every drivable param moving, 31 the first 4 of the picker order, 32 the modulator alone
auto = [(30, "automation: every drivable parameter moving"), (31, "automation: 4 parameters moving (picker order)"),
        (32, "automation: default patch + 4 slot modulator")]
valid = []
got = {}
if os.environ.get("SCENARIOS"):
    keep = [int(v) for v in os.environ["SCENARIOS"].split()]
    cases = [c for c in cases if c[0] in keep]
for sc, n in cases + auto:
    pb = line(f"{name} {n}", sc, detail=(sc == 0))
    got[sc] = pb
    if pb >= 5000 and sc < 30:   # voices alive and rendering (a dead voice costs about 1.5k cycles); quiet output still costs real cycles
        valid.append((pb, n))
if valid:
    w = max(valid)
    print(f"WORST {name}: {w[0]:.0f} cycles per block = {100 * w[0] / BLOCK_CYCLES:.2f} % of a block ({w[1]})")
p = lambda c: 100 * c / BLOCK_CYCLES
print(f"AUTOMATION {name}: s0 {p(got[0]):.2f} %, s30 {p(got[30]):.2f} % ({p(got[30] - got[0]):+.2f}), "
      f"s31 {p(got[31]):.2f} % ({p(got[31] - got[0]):+.2f}), modulator s32-s0 {got[32] - got[0]:.0f} cycles ({p(got[32] - got[0]):+.2f})")
sys.exit(0)
