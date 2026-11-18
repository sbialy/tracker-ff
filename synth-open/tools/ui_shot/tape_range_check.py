#!/usr/bin/env python3
# tape_range_check.py BLK_FILE stop|start THRESHOLD -> "T_ms steady"
# Length of a Tape Stop / Tape Start run from the block levels of "audiostat" (tape_range_sim.sh), time from the first block of the log
# (the note is at block 0), a block is 128 frames of 44,117.647 Hz.
#  stop : the run ends in silence (the voice is cut at the end of the run, core/libs/Audio/play_memory.cpp tapeProcess, and the synth part
#         kills its voices), so T = the end of the last block louder than THRESHOLD
#  start: the speed is a straight ramp 0 -> 1 over T, so the run-up has made only half of the crossings the same time at full speed
#         would: T = 2 N - 2 C / c, N blocks, C the crossings of all N blocks, c the crossings per block of the steady end of the log
# (the level of a block is its rms, which reads low while the pitch is so low that a block holds a fraction of a cycle, so the level is
# used for the end of a stop only and the crossings for the start)
import sys

BLK = 128 / 44117.647 * 1000.0

def main(path, mode, thr):
    rms = [float(l.split()[3]) for l in open(path) if l.startswith("blk ")]
    if not rms or max(rms) < 50: print("0 0"); return
    S = sorted(rms)[int(len(rms) * 0.95)]
    if mode == "stop":
        last = max((i for i, r in enumerate(rms) if r > thr), default=-1)
        print("%.1f %.0f" % ((last + 1) * BLK, S)); return
    z = [int(l.split()[5]) for l in open(path) if l.startswith("blk ")]
    last = max((i for i, r in enumerate(rms) if r > thr), default=-1)   # the log runs on a little after Stop was pressed: trim the silence
    z = z[:last + 1]
    f2 = sum(z[-60:]) / 60.0            # steady crossings per block (two per cycle)
    if f2 <= 0: print("0 %.0f" % S); return
    # crossings counted from the note: C = f2 * (N - T/2) because the run-up adds half of the crossings of the same time at full speed
    n = len(z)
    print("%.1f %.0f" % ((2 * n - 2 * sum(z) / f2) * BLK, S))

main(sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 5.0)
