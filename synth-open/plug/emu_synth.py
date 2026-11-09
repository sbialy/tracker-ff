#!/usr/bin/env python3
"""Render a synth engine plugin (PLUG_KIND_SYNTH, PLUG_EXT_SYNTH) in Unicorn and print an output checksum.

    python -I emu_synth.py <engine.plg> <harness.plg> [--base 0x10000000] [--rw-base 0x20027000] [--scenario 0|1]
                           [--blocks 64] [--wav out.wav]

The engine is loaded like the firmware's installer does it (text rebased for --base, e.g. its FlexNVM address, rw and bss
at --rw-base), the harness (synth/synth_harness.cpp, a .plg of its own) drives it through a fixed note and parameter script.
Two engines with the same code give the same checksum at any base: the render compare builds one from the plugin sources
and one from the firmware's own objects. libm comes from Python, rounded to float32 once, the same for both.
"""
import argparse
import math
import os
import struct
import sys
import wave
import zlib
from fractions import Fraction

from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB, Uc
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_S0, UC_ARM_REG_S1, UC_ARM_REG_SP,
                               UC_CPU_ARM_CORTEX_A15)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python -I leaves the script directory out
from emu_plug import f32  # noqa: E402

N = 128
SRAM, SRAM_SIZE = 0x1FFF0000, 0x40000
WORK = 0x20020000
TRAP = WORK + 0xF000
HARNESS = 0x1FFF4000          # harness text, its rw after it
OUT = 0x70000000              # SDRAM: the rendered blocks, then one instruction count per block
WORK_MEM = 0x70080000         # SDRAM: the engine's work memory (work_bytes)


def load(uc, path, base, rwbase=None):
    data = open(path, "rb").read()
    (magic, hsize, amaj, amin, kind, cpu, flags, uid, ver, min_host, img, bss, nreb, entry, ram, cyc,
     crc, rwoff) = struct.unpack_from("<IHBBBBHIIIIIIIIIII", data, 0)
    rwoff = rwoff or img
    assert magic == 0x31474C50 and amaj == 1 and cpu == 1, "header"
    # a logo trailer (plug_logo.h, build_plug.py --logo) after the rebase table is outside the CRC: cut it off
    logo_off = struct.unpack_from("<I", data, 64 + 72)[0] if hsize >= 64 + 96 else 0
    if logo_off and logo_off == hsize + img + 4 * nreb and len(data) == logo_off + 1676:
        data = data[:logo_off]
    assert zlib.crc32(data[64:]) & 0xFFFFFFFF == crc and len(data) == hsize + img + 4 * nreb, "crc or size"
    payload = data[hsize:]
    image = bytearray(payload[:img])
    rwbase = rwbase if rwbase is not None else base + rwoff
    for k in range(nreb):
        off, = struct.unpack_from("<I", payload, img + 4 * k)
        v, = struct.unpack_from("<I", image, off)
        assert off % 4 == 0 and off + 4 <= img and v < img + bss, "rebase %x" % off
        struct.pack_into("<I", image, off, (base + v) if v < rwoff else (rwbase + v - rwoff))
    uc.mem_write(base, bytes(image[:rwoff]))
    uc.mem_write(rwbase, bytes(image[rwoff:]) + bytes(bss))
    # a synth engine's numbers in the manifest (build_plug.py --synth, plug_abi.h PLUG_SYNTH_MANIFEST): reserved[1..4]
    syn = struct.unpack_from("<IIII", data, 64 + 76) if hsize >= 64 + 96 else (0, 0, 0, 0)
    syn = syn[:3] if syn[3] == 0x314E5953 else None
    return dict(kind=kind, uid=uid, entry=base + entry, lo=base, hi=base + rwoff, text=rwoff, rw=img - rwoff + bss, syn=syn)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("plg")
    ap.add_argument("harness")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0x10000000)
    ap.add_argument("--rw-base", dest="rwbase", type=lambda s: int(s, 0), default=0x20027000)
    ap.add_argument("--scenario", type=int, default=0)
    ap.add_argument("--blocks", type=int, default=64)
    ap.add_argument("--wav")
    a = ap.parse_args()

    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
    uc.mem_map(SRAM, SRAM_SIZE)
    uc.mem_map(0x70000000, 0x100000)
    uc.mem_map(0x10000000, 0x40000)   # FlexNVM
    uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))
    uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    eng = load(uc, a.plg, a.base, a.rwbase)
    har = load(uc, a.harness, HARNESS)
    if 0x10000000 <= a.base < 0x10040000:
        uc.mem_protect(0x10000000, 0x40000, 5)   # flash: read and execute only
    uc.mem_write(TRAP, b"\xfe\xe7")
    # host table, ABI 1.1: 15 level 1 words, then exp2f and log2f
    stubs = TRAP + 0x100
    names = ["sinf", "cosf", "tanf", "tanhf", "expf", "logf", "powf", "memcpy", "memset", "alloc", "cycles", "query",
             "exp2f", "log2f"]
    uc.mem_write(stubs, b"\x70\x47\x00\xbf" * len(names))
    f2 = lambda v: f32(Fraction(v))
    rf = lambda r: struct.unpack("<f", struct.pack("<I", uc.reg_read(r) & 0xFFFFFFFF))[0]
    def wf(r, v):
        uc.reg_write(r, struct.unpack("<I", struct.pack("<f", v))[0])
    used = {}
    def service(u, addr, size, ud):
        n = names[(addr - stubs) // 4]
        used[n] = used.get(n, 0) + 1
        x, y = rf(UC_ARM_REG_S0), rf(UC_ARM_REG_S1)
        if n in ("sinf", "cosf", "tanf", "tanhf", "expf", "logf"):
            wf(UC_ARM_REG_S0, f2(getattr(math, n[:-1])(x)))
        elif n == "powf":
            wf(UC_ARM_REG_S0, f2(math.pow(x, y)))
        elif n == "exp2f":
            wf(UC_ARM_REG_S0, f2(2.0 ** x))
        elif n == "log2f":
            wf(UC_ARM_REG_S0, f2(math.log2(x)) if x > 0 else float("-inf"))
        elif n == "memcpy":
            d, sr, ln = (u.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
            u.mem_write(d, bytes(u.mem_read(sr, ln)))
        elif n == "memset":
            d, c, ln = (u.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
            u.mem_write(d, bytes([c & 255]) * ln)
        elif n == "cycles":   # executed instructions so far in this call (1 cycle each: a lower bound)
            u.reg_write(UC_ARM_REG_R0, count[0] & 0xFFFFFFFF)
        else:
            u.reg_write(UC_ARM_REG_R0, 0)
    uc.hook_add(UC_HOOK_CODE, service, begin=stubs, end=stubs + 4 * len(names) - 1)
    host = WORK
    fields = [(15 + 2) * 4, 1, struct.unpack("<I", struct.pack("<f", 44117.64706))[0]] + [(stubs + 4 * i) | 1 for i in range(14)]
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
        return uc.reg_read(UC_ARM_REG_R0)

    u32 = lambda p: struct.unpack("<I", bytes(uc.mem_read(p, 4)))[0]
    cstr = lambda p: bytes(uc.mem_read(p, 32)).split(b"\0")[0].decode()
    desc = call(eng["entry"], host, (1 << 16) | 1)
    assert eng["lo"] <= desc < eng["hi"], "descriptor outside the text segment"
    assert u32(desc + 4) == 4, "not a synth plugin"
    f_init, f_query = u32(desc + 28), u32(desc + 44)
    assert call(f_init, WORK + 0x1000, host) == 0, "init failed"
    ext = call(f_query, 3)
    assert ext, "no PLUG_EXT_SYNTH"
    x = struct.unpack("<IIiiiIIIIIIIiII", bytes(uc.mem_read(ext, 60)))
    print("engine %s: stored %d, patch engine 0x%x, type 0x%x, %d params, %d sections, %d macros, volume %d, voices %d, flags %d, "
          "work %d B, text %d B, rw+bss %d B" % (cstr(x[5]), x[2], x[3], x[4], x[7], x[9], x[11], x[12], x[13], x[14],
                                                 u32(ext + 60), eng["text"], eng["rw"]))
    if eng["syn"] is not None:   # the host lists the engine by these before it runs any code: they must be the extension's
        assert tuple(eng["syn"]) == (x[2] & 0xFFFFFFFF, x[3] & 0xFFFFFFFF, x[4] & 0xFFFFFFFF), \
            "manifest numbers %s differ from the extension's %s" % (eng["syn"], (x[2], x[3], x[4]))
        print("manifest numbers: stored %d, patch engine 0x%x, type 0x%x (equal to the extension)" % tuple(eng["syn"]))
    work = u32(ext + 60)
    if work:   # the host's work memory (plug_synth.cpp: SDRAM arena, zeroed), given before any audio call
        assert work <= 0x80000, "work memory larger than the emulator's arena"
        call(u32(ext + 64), WORK_MEM, work)
    hdesc = call(har["entry"], host, (1 << 16) | 1)
    call(u32(hdesc + 28), WORK + 0x1100, host)   # harness init: keeps the host table (cycles)
    run = call(u32(hdesc + 44), 0x7E)
    call(run, ext, OUT, a.blocks, a.scenario)
    instr = count[0]
    per = struct.unpack("<%dI" % a.blocks, bytes(uc.mem_read(OUT + a.blocks * 2 * N * 4, 4 * a.blocks)))
    ob = bytes(uc.mem_read(OUT, a.blocks * 2 * N * 4))
    chk = 0x811C9DC5
    for w in struct.unpack("<%dI" % (len(ob) // 4), ob):
        chk = ((chk ^ w) * 0x01000193) & 0xFFFFFFFF
    fl = struct.unpack("<%df" % (len(ob) // 4), ob)
    peak = max(abs(v) for v in fl)
    print("libm calls: %s" % ", ".join("%s %d" % kv for kv in sorted(used.items())))
    print("render: %d blocks, scenario %d, %d instructions (%.2f%% of the blocks at 1 cycle each), peak %.4f" % (
        a.blocks, a.scenario, instr, 100.0 * instr / (a.blocks * 522240), peak))
    print("worst block: %d instructions (%.2f%% of a block at 1 cycle each), in block %d" % (
        max(per), 100.0 * max(per) / 522240, per.index(max(per))))
    print("output checksum %08x" % chk)
    if a.wav:
        w = wave.open(a.wav, "wb")
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        clip = lambda v: max(-32768, min(32767, int(v * 32768)))
        frames = []
        for b in range(a.blocks):
            L, R = fl[b * 2 * N: b * 2 * N + N], fl[b * 2 * N + N: (b + 1) * 2 * N]
            frames += [struct.pack("<hh", clip(l), clip(r)) for l, r in zip(L, R)]
        w.writeframes(b"".join(frames))
        w.close()


if __name__ == "__main__":
    main()
