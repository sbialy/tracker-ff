#!/usr/bin/env python3
"""skipback_check.py WAV REF.raw MARK_FRAMES [--min-rms N]
The Skip-back WAV against the reference of ui_shot (UISHOT_AUDIO_DUMP: every rendered block, 16 bit L R interleaved): the file must hold
exactly the frames that ended at MARK_FRAMES (the audiomark before the Shift + Sample Rec press), stereo as they were, mono as (L + R) >> 1.
--min-rms: the saved audio must not be silence (the sim plays a synth part). Prints one PASS or FAIL line, exit 0 on PASS."""
import struct
import sys


def main():
    wav, ref, mark = sys.argv[1], sys.argv[2], int(sys.argv[3])
    min_rms = float(sys.argv[sys.argv.index("--min-rms") + 1]) if "--min-rms" in sys.argv else 0.0
    d = open(wav, "rb").read()
    if len(d) < 44 or d[0:4] != b"RIFF" or d[8:16] != b"WAVEfmt " or d[36:40] != b"data":
        print("FAIL %s: not a canonical WAV" % wav)
        return 1
    fmt, ch, rate, byte_rate, align, bits = struct.unpack("<HHIIHH", d[20:36])
    size = struct.unpack("<I", d[40:44])[0]
    riff = struct.unpack("<I", d[4:8])[0]
    if fmt != 1 or bits != 16 or ch not in (1, 2) or rate != 44100 or align != 2 * ch or byte_rate != rate * align:
        print("FAIL %s: format %d ch %d rate %d bits %d" % (wav, fmt, ch, rate, bits))
        return 1
    if size != len(d) - 44 or riff != size + 36:
        print("FAIL %s: sizes data %d riff %d file %d" % (wav, size, riff, len(d)))
        return 1
    frames = size // align
    r = open(ref, "rb").read()
    total = len(r) // 4
    if mark > total or frames > mark:
        print("FAIL: mark %d, reference %d frames, file %d frames" % (mark, total, frames))
        return 1
    ref_lr = struct.unpack("<%dh" % (2 * mark), r[: 4 * mark])
    got = struct.unpack("<%dh" % (frames * ch), d[44:])
    first = mark - frames
    sq = 0
    for k in range(frames):
        L, R = ref_lr[2 * (first + k)], ref_lr[2 * (first + k) + 1]
        if ch == 2:
            ok = got[2 * k] == L and got[2 * k + 1] == R
            sq += L * L + R * R
        else:
            ok = got[k] == (L + R) >> 1
            sq += got[k] * got[k]
        if not ok:
            print("FAIL %s: frame %d (rendered frame %d) differs" % (wav, k, first + k))
            return 1
    rms = (sq / max(1, frames * ch)) ** 0.5
    if rms < min_rms:
        print("FAIL %s: rms %.1f, the saved audio is silence" % (wav, rms))
        return 1
    print("PASS %s: %d ch, %d frames (%.2f s) = rendered frames %d..%d, rms %.1f" % (wav, ch, frames, frames / 44100.0, first, mark - 1, rms))
    return 0


if __name__ == "__main__":
    sys.exit(main())
