#!/usr/bin/env python3
"""Entering Asym leaves no DC step (opentracker-fx cbedea9): dirt.plg in emu_plug.py on a 441 Hz sine (a period of exactly
100 frames), Type Soft, Hard or Fold for 100 blocks, then Asym. The DC is the mean over whole periods. Before the follower the
mean after the switch reached 0.156 of full scale (from Soft, Drive 100, amplitude 0.5) and drained over the 10 Hz block.

    python3 dirt_asym_check.py <python with unicorn> <emu_plug.py> <dirt.plg> <work dir>
Exit 0 when every case stays under 0.01 (the upstream test's limit).
"""
import math
import struct
import subprocess
import sys
import wave

py, emu, plg, work = sys.argv[1:5]
N, BLOCKS, LIMIT = 128, 200, 0.01
worst_all = 0.0
for amp in (0.15, 0.5):
    inw = "%s/asym_in_%s.wav" % (work, amp)
    w = wave.open(inw, "wb")
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(44100)
    w.writeframes(b"".join(struct.pack("<hh", *(2 * [int(amp * 32767 * math.sin(2 * math.pi * i / 100))]))
                           for i in range(BLOCKS * N)))
    w.close()
    for frm in (0, 1, 2):
        for drive in (40, 100):
            out = "%s/asym_out.wav" % work
            r = subprocess.run([py, "-I", emu, plg, "--blocks", str(BLOCKS), "--inwav", inw, "--wav", out,
                                "--param", "1=%d" % drive, "--param", "0=%d" % frm, "--param-at", "100:0=3"],
                               capture_output=True, text=True)
            if r.returncode:
                print(r.stdout + r.stderr)
                sys.exit(1)
            w = wave.open(out)
            s = struct.unpack("<%dh" % (2 * w.getnframes()), w.readframes(w.getnframes()))[0::2]
            dc = max(abs(sum(s[f:f + 100]) / 100.0) for f in range(101 * N, 160 * N - 100, 20)) / 32768.0
            worst_all = max(worst_all, dc)
            if dc >= LIMIT:
                print("dirt  FAIL entering Asym from type %d, Drive %d, amplitude %.2f: DC %.4f" % (frm, drive, amp, dc))
print("dirt  %s entering Asym: worst DC %.4f of full scale (limit %.2f, 12 cases)" % ("ok " if worst_all < LIMIT else "FAIL", worst_all, LIMIT))
sys.exit(0 if worst_all < LIMIT else 1)
