#!/usr/bin/env python3
"""Zero in, exact zero out (the mid-tread quantiser): dirt.plg in emu_plug.py on a non-NULL bus of zeros, for each Type, each
Bits 4..16 and Drive 15 and 100, with Rate 4 and Tone 70 (Dirt Crush's) and with neither. Near zero input (+-1 LSB, from
Bits 8 down) must give zero too: the mid-rise quantiser it replaced never output 0 and left a constant floor of half a step.

    python3 dirt_zero_check.py <python with unicorn> <emu_plug.py> <dirt.plg> <work dir>
Exit 0 when every case is exactly zero.
"""
import struct
import subprocess
import sys
import wave

py, emu, plg, work = sys.argv[1:5]
N, BLOCKS = 128, 6


def wav(path, vals):
    w = wave.open(path, "wb")
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(44100)
    w.writeframes(b"".join(struct.pack("<hh", v, v) for v in vals))
    w.close()


zero = "%s/zero_in.wav" % work
near = "%s/near_in.wav" % work
wav(zero, [0] * (BLOCKS * N))
wav(near, [(1 if (i // 3) & 1 else -1) for i in range(BLOCKS * N)])
out = "%s/zero_out.wav" % work
cases = bad = 0
for name, inw, bits_hi in (("zero", zero, 16), ("+-1 LSB", near, 8)):
    for typ in range(4):
        for bits in range(4, bits_hi + 1):
            for drive in (15, 100):
                for rate, tone in ((1, 100), (4, 70)):
                    r = subprocess.run([py, "-I", emu, plg, "--blocks", str(BLOCKS), "--inwav", inw, "--wav", out,
                                        "--param", "0=%d" % typ, "--param", "1=%d" % drive, "--param", "2=%d" % bits,
                                        "--param", "3=%d" % rate, "--param", "4=%d" % tone],
                                       capture_output=True, text=True)
                    if r.returncode:
                        print(r.stdout + r.stderr)
                        sys.exit(1)
                    w = wave.open(out)
                    s = struct.unpack("<%dh" % (2 * w.getnframes()), w.readframes(w.getnframes()))
                    nz = sum(1 for v in s if v)
                    cases += 1
                    if nz:
                        bad += 1
                        print("dirt  FAIL %s in: type %d bits %d drive %d rate %d tone %d: %d non zero samples out" %
                              (name, typ, bits, drive, rate, tone, nz))
print("dirt  %s  zero (Bits 4..16) and +-1 LSB (Bits 4..8) in, exact zero out: %d cases, %d failed" %
      ("ok" if not bad else "FAIL", cases, bad))
sys.exit(1 if bad else 0)
