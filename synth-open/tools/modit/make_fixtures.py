#!/usr/bin/env python3
"""The MOD and IT inputs of the MOD/IT golden test (synth-open/tools/ui_shot/modit_golden_sim.sh), written byte for byte the same every run.

    make_fixtures.py OUTDIR      writes OUTDIR/golden.mod and OUTDIR/golden.it

golden.mod: Protracker "M.K.", 3 samples (a looped sine, a ramp, an empty slot), 2 patterns, order 0 1 0, notes, volumes and
            the effects the importer maps (arpeggio, slides, 9xx, Cxx, Dxx, Exy, Fxx).
golden.it:  Impulse Tracker 2.14, instruments mode, 3 instruments over 3 samples (16 bit looped, 8 bit, 16 bit with a C5 speed
            other than 44100), 2 patterns of 32 rows (note, instrument, volume, panning, command columns, mask reuse), order 0 1.
"""
import math
import os
import struct
import sys


def mod_bytes():
    def name(s):
        return s.encode().ljust(22, b"\0")[:22]

    sine = bytes(int(127 * math.sin(2 * math.pi * i / 32)) & 0xFF for i in range(64))
    ramp = bytes(((i * 2) - 128) & 0xFF for i in range(128))
    samples = [(name("sine loop"), len(sine) // 2, 0, 64, 0, len(sine) // 2, sine),
               (name("ramp"), len(ramp) // 2, 3, 48, 0, 1, ramp),
               (name(""), 0, 0, 0, 0, 1, b"")]
    out = bytearray(name("modit golden"))
    for i in range(31):
        if i < len(samples):
            n, ln, ft, vol, ls, ll, _ = samples[i]
            out += n + struct.pack(">HBBHH", ln, ft, vol, ls, ll)
        else:
            out += name("") + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
    order = bytearray(128)
    order[0:3] = bytes([0, 1, 0])
    out += bytes([3, 127]) + bytes(order) + b"M.K."

    periods = [856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453]   # octave 1, C..B

    def cell(period, inst, fx, par):
        return struct.pack(">I", ((inst & 0xF0) << 24) | (period << 16) | ((inst & 0x0F) << 12) | (fx << 8) | par)

    def pattern(variant):
        rows = []
        for r in range(64):
            ch = []
            for c in range(4):
                if (r + c * 3) % 8 == 0:
                    n = (r // 4 + c * 2 + variant * 5) % 12
                    inst = 1 + ((r // 8 + c) % 2)
                    fx, par = [(0, 0), (0xC, 0x30 + 4 * c), (0x1, 0x04), (0xE, 0x91)][(r // 8 + c) % 4]
                    ch.append(cell(periods[n] >> (c % 2), inst, fx, par))
                elif r % 16 == 5 and c == 1:
                    ch.append(cell(0, 0, [0x3, 0x4, 0x9, 0xA, 0xF, 0xD][(r // 16 + variant) % 6], 0x12))
                else:
                    ch.append(cell(0, 0, 0, 0))
            rows.append(b"".join(ch))
        return b"".join(rows)

    out += pattern(0) + pattern(1)
    for s in samples:
        out += s[6]
    return bytes(out)


def it_bytes():
    ord_num, ins_num, smp_num, pat_num = 3, 3, 3, 2   # orders 0 1 255 (the end marker)
    base = 0xC0 + ord_num
    ins_off = [base + 4 * ins_num + 4 * smp_num + 4 * pat_num + 554 * i for i in range(ins_num)]
    smp_base = ins_off[-1] + 554
    smp_off = [smp_base + 80 * i for i in range(smp_num)]
    data_base = smp_base + 80 * smp_num

    s16 = b"".join(struct.pack("<h", int(12000 * math.sin(2 * math.pi * i / 40))) for i in range(160))
    s8 = bytes((int(90 * math.sin(2 * math.pi * i / 24)) + 128) & 0xFF for i in range(96))   # IT 8 bit: the importer shifts it, unsigned bytes
    s16b = b"".join(struct.pack("<h", (i * 300) % 20000 - 10000) for i in range(80))
    sdata = [(s16, 0x02 | 0x10, 160, 20, 140, 44100, 64), (s8, 0x00, 96, 0, 0, 22050, 40), (s16b, 0x02, 80, 0, 0, 33000, 55)]
    s_ptr, p = [], data_base
    for d in sdata:
        s_ptr.append(p)
        p += len(d[0])
    pat_start = p

    def pattern(variant):
        rows = []
        for r in range(32):
            row = bytearray()
            if r % 4 == 0:
                note = 48 + (r // 4 * 2 + variant * 7) % 24
                inst = 1 + (r // 4) % 3
                row += bytes([1 | 0x80, 0x01 | 0x02 | 0x04, note, inst, 20 + r])
                row += bytes([2 | 0x80, 0x01 | 0x02 | 0x08, note + 5, 1 + (inst % 3), 0x0A, r])
            if r % 8 == 2:
                row += bytes([3 | 0x80, 0x01 | 0x04, 60 + r % 12, 128 + 10 + r])
            row += b"\0"
            rows.append(bytes(row))
        body = b"".join(rows)
        return struct.pack("<HHI", len(body), 32, 0) + body

    pats = [pattern(0), pattern(1)]
    pat_off, p = [], pat_start
    for pt in pats:
        pat_off.append(p)
        p += len(pt)

    hdr = bytearray(b"IMPM" + b"modit golden".ljust(26, b"\0") + struct.pack("<H", 0x0408))
    hdr += struct.pack("<HHHHHHHH", ord_num, ins_num, smp_num, pat_num, 0x0214, 0x0214, 0x0005 | 0x0008, 0x0000)
    hdr += struct.pack("<BBBBBBHI", 128, 48, 6, 125, 128, 0, 0, 0) + bytes(4)
    assert len(hdr) == 0x40, len(hdr)
    hdr += bytes([32] * 64) + bytes([64] * 64)
    out = bytearray(hdr)
    out += bytes([0, 1, 255])
    out += b"".join(struct.pack("<I", o) for o in ins_off)
    out += b"".join(struct.pack("<I", o) for o in smp_off)
    out += b"".join(struct.pack("<I", o) for o in pat_off)
    for i in range(ins_num):
        ins = bytearray(554)
        ins[0:4] = b"IMPI"
        ins[4:4 + 9] = ("ins%d.iti" % i).encode()
        ins[0x11] = 0
        ins[0x14:0x16] = struct.pack("<H", 256 * i)
        ins[0x18] = 128 - 16 * i
        ins[0x19] = 32
        ins[0x1C:0x1E] = struct.pack("<H", 0x0214)
        ins[0x1E] = 1
        nm = ("instrument %d" % (i + 1)).encode()
        ins[0x20:0x20 + len(nm)] = nm
        for n in range(120):
            ins[0x40 + n * 2] = n
            ins[0x41 + n * 2] = i + 1
        out += ins
    for i, (d, flg, ln, lb, le, c5, vol) in enumerate(sdata):
        smp = bytearray(80)
        smp[0:4] = b"IMPS"
        smp[4:4 + 9] = ("smp%d.wav" % i).encode()
        smp[0x11] = 64
        smp[0x12] = flg | 0x01
        smp[0x13] = vol
        nm = ("sample %d" % (i + 1)).encode()
        smp[0x14:0x14 + len(nm)] = nm
        smp[0x2E] = 0x01 if flg & 0x02 else 0x00
        smp[0x2F] = 0x80 | 32
        smp[0x30:0x34] = struct.pack("<I", ln)
        smp[0x34:0x38] = struct.pack("<I", lb)
        smp[0x38:0x3C] = struct.pack("<I", le)
        smp[0x3C:0x40] = struct.pack("<I", c5)
        smp[0x48:0x4C] = struct.pack("<I", s_ptr[i])
        out += smp
    for d in sdata:
        out += d[0]
    for pt in pats:
        out += pt
    return bytes(out)


def main(argv):
    if len(argv) != 2:
        sys.exit(__doc__)
    os.makedirs(argv[1], exist_ok=True)
    for n, b in (("golden.mod", mod_bytes()), ("golden.it", it_bytes())):
        with open(os.path.join(argv[1], n), "wb") as f:
            f.write(b)
        print("%s %d bytes" % (n, len(b)))


main(sys.argv)
