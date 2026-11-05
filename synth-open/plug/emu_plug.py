#!/usr/bin/env python3
"""Host harness for a .plg (spike): the firmware loader re-done in Python, the plugin's ARM code run in Unicorn.

    python -I emu_plug.py <file.plg> [--base 0x1FFF0BD0] [--blocks 64] [--wav out.wav] [--in in.wav]

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

from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB, Uc
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_CPU_ARM_CORTEX_A15)

N = 128
SRAM, SRAM_SIZE = 0x1FFF0000, 0x40000
SDRAM, SDRAM_SIZE = 0x70000000, 0x1000000
WORK = 0x20020000            # host-side data: host table, instance, io buffers, trap
TRAP = WORK + 0xF000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("plg")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0x1FFF0BD0)
    ap.add_argument("--blocks", type=int, default=64)
    ap.add_argument("--param", type=float, default=None, help="value for parameter 0 after init")
    ap.add_argument("--wav")
    ap.add_argument("--inwav", dest="inwav")
    a = ap.parse_args()
    data = open(a.plg, "rb").read()
    (magic, hsize, amaj, amin, kind, cpu, flags, uid, ver, min_host, img, bss, nreb, entry, ram, cyc,
     crc) = struct.unpack_from("<IHBBBBHIIIIIIIIII", data, 0)
    assert magic == 0x31474C50 and hsize >= 64, "bad header"
    assert amaj == 1 and cpu == 1 and min_host <= 1, "abi"
    payload = data[hsize:hsize + img + 4 * nreb]
    assert zlib.crc32(payload) & 0xFFFFFFFF == crc, "crc"
    image = bytearray(payload[:img])
    for k in range(nreb):
        off, = struct.unpack_from("<I", payload, img + 4 * k)
        v, = struct.unpack_from("<I", image, off)
        assert off % 4 == 0 and off + 4 <= img and v < img + bss, "rebase %x" % off
        struct.pack_into("<I", image, off, v + a.base)

    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
    uc.mem_map(SRAM, SRAM_SIZE)
    uc.mem_map(SDRAM, SDRAM_SIZE)
    uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))
    uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    uc.mem_write(a.base, bytes(image) + bytes(bss))
    uc.mem_write(TRAP, b"\xfe\xe7")
    # host table: every service traps (Dirt's default preset needs none); level 1, 44117.647 Hz
    stub = TRAP + 0x10
    uc.mem_write(stub, b"\x00\xbe")   # bkpt: a service call is reported, not emulated
    host = WORK
    fields = [15 * 4, 1, struct.unpack("<I", struct.pack("<f", 44117.64706))[0]] + [stub | 1] * 12
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
    lo, hi = a.base, a.base + img + bss
    assert lo <= desc < hi, "descriptor outside the image"
    d = struct.unpack("<IIIIIIIIIIII", bytes(uc.mem_read(desc, 48)))
    size, dkind, name_p, author_p, nparams, params_p, inst_size, f_init, f_reset, f_set, f_proc, f_query = d
    assert dkind == kind and size >= 44
    for f in (f_init, f_proc):
        assert f & 1 and lo <= (f & ~1) < hi, "function outside the image"
    cstr = lambda p: bytes(uc.mem_read(p, 32)).split(b"\0")[0].decode()
    print("plugin %s by %s, uid 0x%08x, %d params, instance %d B, base 0x%08x, %d rebase words" % (
        cstr(name_p), cstr(author_p), uid, nparams, inst_size, a.base, nreb))
    for i in range(nparams):
        pn, pu, mn, mx, df, st, ec = struct.unpack("<IIffffH", bytes(uc.mem_read(params_p + 32 * i, 26)))
        print("  param %d %s [%g..%g] def %g%s" % (i, cstr(pn), mn, mx, df, (" %d names" % ec) if ec else ""))
    inst = WORK + 0x1000
    uc.mem_write(inst, bytes(inst_size))
    r, _ = call(f_init, inst, host)
    assert r == 0, "init failed"
    if a.param is not None:
        sys.exit("set_param needs float args in s0 and libm through the host table: not in the spike harness")

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
        lcg = 12345
        base_l, base_r = [], []
        for i in range(N):
            lcg = (lcg * 1664525 + 1013904223) & 0xFFFFFFFF
            noise = (((lcg ^ 0x80000000) - 0x80000000) >> 8) / 8388608.0
            base_l.append(2.0 * i / N - 1.0 + 0.2 * noise)
            base_r.append(0.8 * noise)
        for b in range(a.blocks):
            g = 0.25 + 0.05 * (b & 15)
            src.append(([x * g for x in base_l], [x * g for x in base_r]))
    chk, counts, out = 0x811C9DC5, [], []
    for b in range(a.blocks):
        uc.mem_write(L, struct.pack("<%df" % N, *src[b][0]))
        uc.mem_write(R, struct.pack("<%df" % N, *src[b][1]))
        _, c = call(f_proc, inst, io, 2, N)
        counts.append(c)
        ob = bytes(uc.mem_read(L, 4 * N)) + bytes(uc.mem_read(R, 4 * N))
        for w in struct.unpack("<%dI" % (2 * N), ob):
            chk = ((chk ^ w) * 0x01000193) & 0xFFFFFFFF
        fl = struct.unpack("<%df" % (2 * N), ob)
        out.append((fl[:N], fl[N:]))
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
