#!/usr/bin/env python3
"""Wall time of one 128 frame stereo block of the REAL release master insert .plg under Unicorn (loader re-done as in
tracker-ff synth-open/plug/emu_plug.py: header, CRC, rebase, entry, init, then the most expensive param set we know: all
params at max, then all at default). fast = no per-instruction hook, count = instruction count per block.
Usage: bench_insert.py MASTER_DIR OUT.json"""
import json, math, os, struct, sys, time, zlib
from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB, Uc
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_S0, UC_ARM_REG_S1, UC_ARM_REG_SP, UC_CPU_ARM_CORTEX_A15)
N = 128
SRAM, SRAM_SIZE = 0x1FFF0000, 0x40000
SDRAM, SDRAM_SIZE = 0x70000000, 0x100000
WORK = 0x20020000
TRAP = WORK + 0xF000
BASE = 0x1FFF0BD0


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def make(path, count_mode):
    data = open(path, "rb").read()
    (magic, hsize, amaj, amin, kind, cpu, flags, uid, ver, min_host, img, bss, nreb, entry, ram, cyc,
     crc, rwoff) = struct.unpack_from("<IHBBBBHIIIIIIIIIII", data, 0)
    rwoff = rwoff or img
    payload = data[hsize:hsize + img + 4 * nreb]
    image = bytearray(payload[:img])
    rwbase = BASE + rwoff
    for k in range(nreb):
        off, = struct.unpack_from("<I", payload, img + 4 * k)
        v, = struct.unpack_from("<I", image, off)
        struct.pack_into("<I", image, off, (BASE + v) if v < rwoff else (rwbase + v - rwoff))
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
    uc.mem_map(SRAM, SRAM_SIZE)
    uc.mem_map(SDRAM, SDRAM_SIZE)
    uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))
    uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    uc.mem_write(BASE, bytes(image[:rwoff]))
    uc.mem_write(rwbase, bytes(image[rwoff:]) + bytes(bss))
    uc.mem_write(TRAP, b"\xfe\xe7")
    stubs = TRAP + 0x100
    names = ["sinf", "cosf", "tanf", "tanhf", "expf", "logf", "powf", "memcpy", "memset", "alloc", "cycles", "query",
             "exp2f", "log2f"]
    uc.mem_write(stubs, b"\x70\x47\x00\xbf" * len(names))
    heap = [SDRAM + 0x80000]
    rf = lambda r: struct.unpack("<f", struct.pack("<I", uc.reg_read(r) & 0xFFFFFFFF))[0]

    def wf(r, v):
        uc.reg_write(r, struct.unpack("<I", struct.pack("<f", v))[0])
    nsvc = [0]

    def service(u, addr, size, ud):
        nsvc[0] += 1
        n = names[(addr - stubs) // 4]
        if n in ("sinf", "cosf", "tanf", "tanhf", "expf", "logf"):
            wf(UC_ARM_REG_S0, f32(getattr(math, n[:-1])(rf(UC_ARM_REG_S0))))
        elif n == "exp2f":
            wf(UC_ARM_REG_S0, f32(2.0 ** rf(UC_ARM_REG_S0)))
        elif n == "log2f":
            x = rf(UC_ARM_REG_S0)
            wf(UC_ARM_REG_S0, f32(math.log2(x)) if x > 0 else -math.inf)
        elif n == "powf":
            wf(UC_ARM_REG_S0, f32(math.pow(rf(UC_ARM_REG_S0), rf(UC_ARM_REG_S1))))
        elif n == "memcpy":
            d, sr, ln = (u.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
            u.mem_write(d, bytes(u.mem_read(sr, ln)))
        elif n == "memset":
            d, c, ln = (u.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
            u.mem_write(d, bytes([c & 255]) * ln)
        elif n == "alloc":
            ln = (u.reg_read(UC_ARM_REG_R0) + 7) & ~7
            u.reg_write(UC_ARM_REG_R0, heap[0])
            heap[0] += ln
        else:
            u.reg_write(UC_ARM_REG_R0, 0)
    uc.hook_add(UC_HOOK_CODE, service, begin=stubs, end=stubs + 4 * len(names) - 1)
    fields = [(len(names) + 3) * 4, 1, struct.unpack("<I", struct.pack("<f", 44117.64706))[0]] + [(stubs + 4 * i) | 1 for i in range(len(names))]
    uc.mem_write(WORK, struct.pack("<%dI" % len(fields), *fields))
    count = [0]
    if count_mode:
        uc.hook_add(UC_HOOK_CODE, lambda u, addr, size, ud: count.__setitem__(0, count[0] + 1))

    def call(fn, *args, s0=None):
        uc.reg_write(UC_ARM_REG_SP, WORK + 0xE000)
        uc.reg_write(UC_ARM_REG_LR, TRAP | 1)
        for r, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            uc.reg_write(r, v)
        if s0 is not None:
            wf(UC_ARM_REG_S0, s0)
        count[0] = 0
        uc.emu_start(fn | 1, TRAP)
        return uc.reg_read(UC_ARM_REG_R0)
    desc = call(BASE + entry, WORK, (1 << 16) | 4)
    d = struct.unpack("<IIIIIIIIIIII", bytes(uc.mem_read(desc, 48)))
    size, dkind, name_p, author_p, nparams, params_p, inst_size, f_init, f_reset, f_set, f_proc, f_query = d
    inst = SDRAM + 0x10000
    uc.mem_write(inst, bytes(inst_size))
    assert call(f_init, inst, WORK) == 0
    params = []
    for i in range(nparams):
        pn, pu, mn, mx, df, st, ec = struct.unpack("<IIffffH", bytes(uc.mem_read(params_p + 32 * i, 26)))
        params.append((mn, mx, df))
    L, R, io = WORK + 0x2000, WORK + 0x2200, WORK + 0x2400
    uc.mem_write(io, struct.pack("<II", L, R))
    return uc, call, inst, f_set, f_proc, params, count, (L, R, io), kind, nsvc


def bench(path, setting, count_mode, blocks):
    uc, call, inst, f_set, f_proc, params, count, (L, R, io), kind, nsvc = make(path, count_mode)
    for i, (mn, mx, df) in enumerate(params):
        call(f_set, inst, i, s0=mx if setting == "max" else df)
    src = [struct.pack("<%df" % N, *[0.5 * math.sin(2 * math.pi * 110 * (b * N + i) / 44100) for i in range(N)]) for b in range(8)]
    tot_i, t0 = 0, None
    for b in range(blocks + 4):
        if b == 4:
            t0 = time.perf_counter()
        uc.mem_write(L, src[b % 8])
        uc.mem_write(R, src[(b + 3) % 8])
        call(f_proc, inst, io, 2, N)
        if b >= 4:
            tot_i += count[0]
    dt = (time.perf_counter() - t0) / blocks
    return dt, tot_i / blocks, len(params)


d = sys.argv[1]
res = {}
for f in sorted(os.listdir(d)):
    if not f.endswith(".plg"):
        continue
    row = {}
    for setting in ("def", "max"):
        dt, _, np_ = bench(os.path.join(d, f), setting, False, 400)
        _, ins, _ = bench(os.path.join(d, f), setting, True, 40)
        row[setting] = {"us_per_block": dt * 1e6, "instr_per_block": ins, "mips": ins / dt / 1e6}
    res[f[:-4]] = row
    print("%-8s def %6.0f us %7.0f ins | max %6.0f us %7.0f ins %4.0f MIPS (incl. Python call per block)" % (
        f[:-4], row["def"]["us_per_block"], row["def"]["instr_per_block"], row["max"]["us_per_block"],
        row["max"]["instr_per_block"], row["max"]["mips"]), flush=True)
json.dump(res, open(sys.argv[2], "w"), indent=1)
