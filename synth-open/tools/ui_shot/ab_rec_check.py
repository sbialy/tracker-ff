#!/usr/bin/env python3
"""ab_rec_check.py RAW MARK PTI LENGTH STOP_FRAMES [--min-rms N]
The take of the Synth play page REC (synth_rec_sim.sh) against the reference of ui_shot (UISHOT_AUDIO_DUMP: every rendered block, 16 bit L R
interleaved). MARK is the audiomark printed right before the REC press that started the take, so take frame k is rendered frame MARK + k, mono
as (L + R) >> 1. PTI is the workspace file of the new instrument, LENGTH its length in frames (instrinfo), STOP_FRAMES the frames the take had
when the stop was pressed (recstate): the file must hold exactly rendered frames MARK .. MARK + LENGTH - 1, its last frame is the last one above
-60 dBFS (|v| > 32) and everything the take dropped (LENGTH .. STOP_FRAMES - 1) is at or below it. Prints one PASS or FAIL line, exit 0 on PASS."""
import struct
import sys


def main():
    raw, mark, pti, length, stop = sys.argv[1], int(sys.argv[2]), sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
    min_rms = float(sys.argv[sys.argv.index("--min-rms") + 1]) if "--min-rms" in sys.argv else 0.0
    d = open(pti, "rb").read()
    if len(d) < 2 * length:
        print("FAIL %s: %d bytes, the take has %d" % (pti, len(d), 2 * length))
        return 1
    data = struct.unpack("<%dh" % length, d[len(d) - 2 * length:])
    r = open(raw, "rb").read()
    need = mark + max(length, stop)
    if len(r) // 4 < need:
        print("FAIL: the reference has %d frames, %d needed" % (len(r) // 4, need))
        return 1
    lr = struct.unpack("<%dh" % (2 * need), r[: 4 * need])
    ref = [(lr[2 * (mark + k)] + lr[2 * (mark + k) + 1]) >> 1 for k in range(max(length, stop))]
    for k in range(length):
        if data[k] != ref[k]:
            print("FAIL %s: frame %d is %d, rendered frame %d is %d" % (pti, k, data[k], mark + k, ref[k]))
            return 1
    if length and abs(data[length - 1]) <= 32:
        print("FAIL %s: the last frame is %d, the take must end on a frame above -60 dBFS" % (pti, data[length - 1]))
        return 1
    for k in range(length, stop):
        if abs(ref[k]) > 32:
            print("FAIL %s: dropped frame %d is %d, louder than -60 dBFS" % (pti, k, ref[k]))
            return 1
    sq = sum(v * v for v in data)
    rms = (sq / max(1, length)) ** 0.5
    if rms < min_rms:
        print("FAIL %s: rms %.1f, silence" % (pti, rms))
        return 1
    print("PASS %s: %d frames = rendered frames %d..%d, %d trimmed of %d, rms %.1f" % (pti, length, mark, mark + length - 1, max(0, stop - length), stop, rms))
    return 0


if __name__ == "__main__":
    sys.exit(main())
