#!/usr/bin/env python3
"""capfile_check.py FILE [corrupt BYTE_OFFSET | truncate N | header-corrupt]  decode /Capture/take.cap on its own (the format of core/source/mtCapture.h
namespace capfile, written by core/source/mtSequencer_captureFile.cpp), so capture_sim.sh section 16 does not trust the firmware reader to check
the firmware writer.

  FILE                    prints  size S hdr take T slots N start U bpm100 B hash H lost L  and  recs take=T valid V newest N oldest O span S
                          then one line per valid record of the take, oldest first:  ev SEQ NOTE on|off VEL POS USTEP
  FILE other              prints the count of valid records of every take id: other ID COUNT
  FILE corrupt OFFSET     flips the low bit of the byte at OFFSET (a torn record)
  FILE truncate N         cuts the file to N bytes (a power cut inside a sector)
"""
import struct
import sys


def crc8(b):
    c = 0
    for x in b:
        c ^= x
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def main():
    path = sys.argv[1]
    mode = sys.argv[2] if len(sys.argv) > 2 else ""
    if mode == "corrupt":
        with open(path, "r+b") as f:
            off = int(sys.argv[3])
            f.seek(off)
            v = f.read(1)[0]
            f.seek(off)
            f.write(bytes([v ^ 1]))
        return
    if mode == "truncate":
        with open(path, "r+b") as f:
            f.truncate(int(sys.argv[3]))
        return
    data = open(path, "rb").read()
    print("size", len(data))
    if len(data) < 32:
        print("hdr none")
        return
    magic, ver, take, flags, slots, start, bpm, nhash, lost = struct.unpack_from("<IHBBIIIII", data, 0)
    hcrc = data[31]
    okh = magic == 0x50434646 and ver == 1 and slots == 32768 and take != 0 and hcrc == crc8(data[:31])
    if not okh:
        print("hdr none")
        return
    print("hdr take %d slots %d start %d bpm100 %d hash %08x lost %d flags %d" % (take, slots, start, bpm, nhash, lost, flags))
    ids = {}
    recs = []
    for slot in range(slots):
        o = 512 + slot * 16
        r = data[o:o + 16]
        if len(r) < 16:
            break
        t, note, vel, src, inst, pat, pos, ust, fl, seq, tk, crc = struct.unpack("<IBBBBBBBBHBB", r)
        if crc != crc8(r[:15]):
            continue
        if tk:
            ids[tk] = ids.get(tk, 0) + 1
        if tk == take and (seq & 32767) == slot:
            recs.append((seq, note, vel, pos, ust, t))
    if mode == "other":
        for k in sorted(ids):
            print("other", k, ids[k])
        return
    if not recs:
        print("recs take=%d valid 0" % take)
        return
    newest = oldest = recs[0][0]
    for r in recs[1:]:
        if (r[0] - newest) & 0xFFFF < 0x8000:
            newest = r[0]
        if (oldest - r[0]) & 0xFFFF < 0x8000:
            oldest = r[0]
    print("recs take=%d valid %d newest %d oldest %d span %d" % (take, len(recs), newest, oldest, ((newest - oldest) & 0xFFFF) + 1))
    for r in sorted(recs, key=lambda r: (r[0] - oldest) & 0xFFFF):
        print("ev %d %d %s %d %d %d" % (r[0], r[1], "on" if r[2] & 0x80 else "off", r[2] & 0x7F, r[3], r[4]))


main()
