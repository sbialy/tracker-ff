#!/usr/bin/env python3
"""Host harness for a .plg (spike): the firmware loader re-done in Python, the plugin's ARM code run in Unicorn.

    python -I emu_plug.py <file.plg> [--base 0x1FFF0BD0] [--blocks 64] [--wav out.wav] [--inwav in.wav] [--param 0=2.5] [--raw PREFIX]

Checks the header and CRC, copies the image to --base, applies the rebase table, calls plug_entry, checks the descriptor
like plug::checkDesc, then init() and process() on stereo 128-frame float blocks. Prints the instruction count per block
(a lower bound of the M4 cycles: no wait states, no FPU latencies) and an FNV checksum of the output, which must not depend
on --base. The input is a saw + noise (or --in, a 16-bit stereo wav at 44.1 kHz), the output can be written as a wav.
"""
import argparse
import struct
import sys
import wave
import zlib
from fractions import Fraction

from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB, Uc
import math

from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_S0, UC_ARM_REG_S1, UC_ARM_REG_SP,
                               UC_CPU_ARM_CORTEX_A15)

N = 128
SRAM, SRAM_SIZE = 0x1FFF0000, 0x40000
SDRAM, SDRAM_SIZE = 0x70000000, 0x1000000
WORK = 0x20020000            # host-side data: host table, instance, io buffers, trap
TRAP = WORK + 0xF000


def f32(x):
    """Round an exact Fraction to the nearest float32 (ties to even), as one FPU operation does."""
    x = Fraction(x)
    if x == 0:
        return 0.0
    sign, a = (-1 if x < 0 else 1), abs(x)
    e = a.numerator.bit_length() - a.denominator.bit_length()
    if Fraction(2) ** e > a:
        e -= 1
    scale = Fraction(2) ** (23 - max(e, -126))   # 24 bit significand, denormals below 2^-126
    m = a * scale
    q, r = divmod(m.numerator, m.denominator)
    if 2 * r > m.denominator or (2 * r == m.denominator and q & 1):
        q += 1
    return sign * float(Fraction(q) / scale)


def bench_input(blocks):
    """The input of plug_bench.cpp, bit exact: every float operation rounded once, fmaf rounded once (the bench spells the
    fused ones with __builtin_fmaf so the firmware flags cannot change them)."""
    c02, c08, c005 = f32(Fraction(2, 10)), f32(Fraction(8, 10)), f32(Fraction(5, 100))
    lcg, base_l, base_r = 12345, [], []
    for i in range(N):
        lcg = (lcg * 1664525 + 1013904223) & 0xFFFFFFFF
        noise = f32(Fraction(((lcg ^ 0x80000000) - 0x80000000) >> 8, 8388608))
        saw = f32(Fraction(2 * i, N) - 1)
        base_l.append(f32(Fraction(noise) * Fraction(c02) + Fraction(saw)))
        base_r.append(f32(Fraction(noise) * Fraction(c08)))
    src = []
    for b in range(blocks):
        g = f32(Fraction(b & 15) * Fraction(c005) + Fraction(1, 4))
        src.append(([f32(Fraction(x) * Fraction(g)) for x in base_l], [f32(Fraction(x) * Fraction(g)) for x in base_r]))
    print("input chk=%08x (unscaled block, L then R, as the bench prints it)" % fnv(struct.unpack("<%dI" % (2 * N), struct.pack(
        "<%df" % (2 * N), *(base_l + base_r)))))
    return src


def fnv(words, h=0x811C9DC5):
    for w in words:
        h = ((h ^ w) * 0x01000193) & 0xFFFFFFFF
    return h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("plg")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0x1FFF0BD0)
    ap.add_argument("--rw-base", dest="rwbase", type=lambda s: int(s, 0), default=None,
                    help="put the writable segment here (default: right after the text)")
    ap.add_argument("--blocks", type=int, default=64)
    ap.add_argument("--param", action="append", default=[], help="INDEX=VALUE, set_param after init (repeatable)")
    ap.add_argument("--param-at", dest="param_at", action="append", default=[],
                    help="BLOCK:INDEX=VALUE, set_param just before that block runs (repeatable, tests the ramps after the first block)")
    ap.add_argument("--wav")
    ap.add_argument("--inwav", dest="inwav")
    ap.add_argument("--raw", help="PREFIX: write PREFIX.in.f32 and PREFIX.out.f32, the float blocks as run (per block 128 L then 128 R)")
    a = ap.parse_args()
    data = open(a.plg, "rb").read()
    (magic, hsize, amaj, amin, kind, cpu, flags, uid, ver, min_host, img, bss, nreb, entry, ram, cyc,
     crc, rwoff) = struct.unpack_from("<IHBBBBHIIIIIIIIIII", data, 0)
    rwoff = rwoff or img
    assert magic == 0x31474C50 and hsize >= 64, "bad header"
    assert amaj == 1 and cpu == 1 and min_host <= 1, "abi"
    # a logo trailer (plug_logo.h, build_plug.py --logo) after the rebase table is outside the CRC: cut it off
    logo_off = struct.unpack_from("<I", data, 64 + 72)[0] if hsize >= 64 + 96 else 0
    if logo_off and logo_off == hsize + img + 4 * nreb and len(data) == logo_off + 1676:
        data = data[:logo_off]
    assert zlib.crc32(data[64:]) & 0xFFFFFFFF == crc and len(data) == hsize + img + 4 * nreb, "crc or size"
    if hsize >= 64 + 96:
        msz, mname, mauthor, mlic, mnp, mrole = struct.unpack_from("<I16s32s16sHB", data, 64)
        z = lambda b: b.split(b"\0")[0].decode()
        print("manifest: %s by %s, licence %s, %d params, role %d, flags 0x%x, version %d.%d.%d" % (
            z(mname), z(mauthor), z(mlic), mnp, mrole, flags, ver >> 16, (ver >> 8) & 255, ver & 255))
    payload = data[hsize:hsize + img + 4 * nreb]
    image = bytearray(payload[:img])
    rwbase = a.rwbase if a.rwbase is not None else a.base + rwoff
    for k in range(nreb):
        off, = struct.unpack_from("<I", payload, img + 4 * k)
        v, = struct.unpack_from("<I", image, off)
        assert off % 4 == 0 and off + 4 <= img and v < img + bss, "rebase %x" % off
        struct.pack_into("<I", image, off, (a.base + v) if v < rwoff else (rwbase + v - rwoff))

    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
    uc.mem_map(SRAM, SRAM_SIZE)
    uc.mem_map(SDRAM, SDRAM_SIZE)
    uc.mem_map(0x10000000, 0x40000)   # FlexNVM
    uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))
    uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    uc.mem_write(a.base, bytes(image[:rwoff]))
    uc.mem_write(rwbase, bytes(image[rwoff:]) + bytes(bss))
    if a.base < 0x20000000 and a.base >= 0x10000000 and a.base < 0x10040000:
        uc.mem_protect(0x10000000, 0x40000, 5)   # flash: read and execute only, a store from the plugin faults here too
    uc.mem_write(TRAP, b"\xfe\xe7")
    # host table (plug_host, level 1): every service is a "bx lr" stub, a code hook computes the result in Python first.
    # libm results are rounded to float32 once; they may differ from the device libm in the last bit (Dirt's presets call
    # no libm in process, so the Dirt checksum rule is unaffected).
    stubs = TRAP + 0x100
    names = ["sinf", "cosf", "tanf", "tanhf", "expf", "logf", "powf", "memcpy", "memset", "alloc", "cycles", "query"]
    uc.mem_write(stubs, b"\x70\x47\x00\xbf" * len(names))
    heap = [WORK + 0x8000]   # alloc: a bump pointer in the work area (flags 0 and 1 alike)
    f2 = lambda v: f32(Fraction(v))
    rf = lambda r: struct.unpack("<f", struct.pack("<I", uc.reg_read(r) & 0xFFFFFFFF))[0]
    def wf(r, v):
        uc.reg_write(r, struct.unpack("<I", struct.pack("<f", v))[0])
    def service(u, addr, size, ud):
        k = (addr - stubs) // 4
        n = names[k]
        if n in ("sinf", "cosf", "tanf", "tanhf", "expf", "logf"):
            wf(UC_ARM_REG_S0, f2(getattr(math, n[:-1])(rf(UC_ARM_REG_S0))))
        elif n == "powf":
            wf(UC_ARM_REG_S0, f2(math.pow(rf(UC_ARM_REG_S0), rf(UC_ARM_REG_S1))))
        elif n == "memcpy":
            d, sr, ln = (u.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
            u.mem_write(d, bytes(u.mem_read(sr, ln)))
        elif n == "memset":
            d, c, ln = (u.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
            u.mem_write(d, bytes([c & 255]) * ln)
        elif n == "alloc":
            ln = (u.reg_read(UC_ARM_REG_R0) + 7) & ~7
            u.mem_write(heap[0], bytes(ln))
            u.reg_write(UC_ARM_REG_R0, heap[0])
            heap[0] += ln
        else:
            u.reg_write(UC_ARM_REG_R0, 0)
    uc.hook_add(UC_HOOK_CODE, service, begin=stubs, end=stubs + 4 * len(names) - 1)
    host = WORK
    fields = [15 * 4, 1, struct.unpack("<I", struct.pack("<f", 44117.64706))[0]] + [(stubs + 4 * i) | 1 for i in range(12)]
    uc.mem_write(host, struct.pack("<%dI" % len(fields), *fields))

    count = [0]
    uc.hook_add(UC_HOOK_CODE, lambda u, addr, size, ud: count.__setitem__(0, count[0] + 1))

    def call(fn, *args):
        uc.reg_write(UC_ARM_REG_SP, WORK + 0xE000)
        uc.reg_write(UC_ARM_REG_LR, TRAP | 1)
        for r, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            uc.reg_write(r, v)
        count[0] = 0
        uc.emu_start(fn | 1, TRAP)
        return uc.reg_read(UC_ARM_REG_R0), count[0]

    desc, _ = call(a.base + entry, host, (1 << 16) | 0)
    lo, hi = a.base, a.base + rwoff
    assert lo <= desc < hi, "descriptor outside the text segment"
    d = struct.unpack("<IIIIIIIIIIII", bytes(uc.mem_read(desc, 48)))
    size, dkind, name_p, author_p, nparams, params_p, inst_size, f_init, f_reset, f_set, f_proc, f_query = d
    assert dkind == kind and size >= 44
    for f in (f_init, f_proc):
        assert f & 1 and lo <= (f & ~1) < hi, "function outside the image"
    cstr = lambda p: bytes(uc.mem_read(p, 32)).split(b"\0")[0].decode()
    print("plugin %s by %s, uid 0x%08x, %d params, instance %d B, text 0x%08x (%d B), rw 0x%08x (%d B + bss %d), %d rebase words" % (
        cstr(name_p), cstr(author_p), uid, nparams, inst_size, a.base, rwoff, rwbase, img - rwoff, bss, nreb))
    for i in range(nparams):
        pn, pu, mn, mx, df, st, ec = struct.unpack("<IIffffH", bytes(uc.mem_read(params_p + 32 * i, 26)))
        print("  param %d %s [%g..%g] def %g%s" % (i, cstr(pn), mn, mx, df, (" %d names" % ec) if ec else ""))
    inst = WORK + 0x1000
    uc.mem_write(inst, bytes(inst_size))
    r, _ = call(f_init, inst, host)
    assert r == 0, "init failed"
    dump = lambda when: print("inst %s (%d B): %s" % (when, inst_size, " ".join(
        "%08x" % w for w in struct.unpack("<%dI" % (inst_size // 4), bytes(uc.mem_read(inst, inst_size & ~3))))))
    dump("init")
    def set_param(pv):
        idx, val = pv.split("=")
        uc.reg_write(UC_ARM_REG_SP, WORK + 0xE000)
        uc.reg_write(UC_ARM_REG_LR, TRAP | 1)
        uc.reg_write(UC_ARM_REG_R0, inst)
        uc.reg_write(UC_ARM_REG_R1, int(idx))
        wf(UC_ARM_REG_S0, float(val))
        count[0] = 0
        uc.emu_start(f_set | 1, TRAP)
        print("set_param(%s, %s)" % (idx, val))
        return count[0]
    for pv in a.param:
        set_param(pv)
    at = {}
    for pv in a.param_at:
        blk, rest = pv.split(":")
        at.setdefault(int(blk), []).append(rest)

    L, R = WORK + 0x2000, WORK + 0x2200
    io = WORK + 0x2400
    uc.mem_write(io, struct.pack("<II", L, R))
    src = []
    if a.inwav:
        w = wave.open(a.inwav)
        fr = w.readframes(w.getnframes())
        for b in range(a.blocks):
            blk = struct.unpack_from("<%dh" % (2 * N), fr, b * 4 * N) if (b + 1) * 4 * N <= len(fr) else (0,) * (2 * N)
            src.append(([x / 32768 for x in blk[0::2]], [x / 32768 for x in blk[1::2]]))
    else:
        src = bench_input(a.blocks)
    chk, counts, out = 0x811C9DC5, [], []
    for b in range(a.blocks):
        for pv in at.get(b, []):
            print("  (before block %d, set_param took %d instructions)" % (b, set_param(pv)))
        uc.mem_write(L, struct.pack("<%df" % N, *src[b][0]))
        uc.mem_write(R, struct.pack("<%df" % N, *src[b][1]))
        _, c = call(f_proc, inst, io, 2, N)
        counts.append(c)
        ob = bytes(uc.mem_read(L, 4 * N)) + bytes(uc.mem_read(R, 4 * N))
        for w in struct.unpack("<%dI" % (2 * N), ob):
            chk = ((chk ^ w) * 0x01000193) & 0xFFFFFFFF
        fl = struct.unpack("<%df" % (2 * N), ob)
        out.append((fl[:N], fl[N:]))
    if a.raw:
        with open(a.raw + ".in.f32", "wb") as f:
            for b in range(a.blocks):
                f.write(struct.pack("<%df" % N, *src[b][0]) + struct.pack("<%df" % N, *src[b][1]))
        with open(a.raw + ".out.f32", "wb") as f:
            for b in range(a.blocks):
                f.write(struct.pack("<%df" % N, *out[b][0]) + struct.pack("<%df" % N, *out[b][1]))
    dump("after warm")
    print("process: %d blocks, instructions per block min/avg/max %d/%d/%d (%.2f%% of a 522,240 cycle block at 1 cycle each)" % (
        a.blocks, min(counts), sum(counts) // len(counts), max(counts), 100.0 * max(counts) / 522240))
    changed = sum(1 for b in range(a.blocks) for i in range(N) if abs(out[b][0][i] - src[b][0][i]) > 1e-3)
    print("output checksum %08x, %d of %d left samples changed by the effect" % (chk, changed, a.blocks * N))
    if a.wav:
        w = wave.open(a.wav, "wb")
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        clip = lambda x: max(-32768, min(32767, int(x * 32768)))
        w.writeframes(b"".join(struct.pack("<hh", clip(l), clip(r)) for blk in out for l, r in zip(*blk)))
        w.close()


if __name__ == "__main__":
    main()
