#!/usr/bin/env python3
"""Check that an extension module file (core/include/ext_abi.h, pack_ext.py output) is bound to one firmware image.

    check_ext.py MODULE.plg IMAGE.ptf [--slot 0x70d80000]

The same sum the firmware computes before it runs the module (ext_module.cpp imageCrc): the CRC-32 of the flash [0, end) the bootloader
programs from the .ptf, 0xFF where the .ptf has no data, end = the highest address of the .ptf. It must be the image id in the module
header (reserved[0]), and the module's own CRC, kind, rebase table and slot address must hold. Exit 0 when it matches, 1 and the reason
when not. A module is only good for the exact image it was packed with: the tail pad relink of `make original` changes the id, so build
the module after the final image (synth-open/modit/build_modit.sh does) and check it again in the package step."""
import struct
import sys
import zlib


def flash_of(path):
    mem = {}
    upper = 0
    lo, top = None, 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(":"):
            continue
        rec = bytes.fromhex(line[1:])
        if sum(rec) & 0xFF:
            sys.exit("%s: bad record checksum" % path)
        n, addr, typ, data = rec[0], (rec[1] << 8) | rec[2], rec[3], rec[4:4 + rec[0]]
        if typ == 0:
            a = upper + addr
            for i in range(n):
                mem[a + i] = data[i]
            top = max(top, a + n)
        elif typ == 4:
            upper = ((data[0] << 8) | data[1]) << 16
        elif typ == 2:
            upper = ((data[0] << 8) | data[1]) << 4
    if not mem:
        sys.exit("%s: no data records" % path)
    if min(mem) != 0:
        sys.exit("%s: the image does not start at address 0" % path)
    flash = bytearray(b"\xff" * top)
    for a, b in mem.items():
        flash[a] = b
    return bytes(flash)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    slot = None
    if "--slot" in sys.argv:
        slot = int(sys.argv[sys.argv.index("--slot") + 1], 0)
        args = [a for a in args if a != sys.argv[sys.argv.index("--slot") + 1]]
    if len(args) != 2:
        sys.exit(__doc__)
    plg = open(args[0], "rb").read()
    if len(plg) < 64:
        print("%s: too short" % args[0]); return 1
    (magic, hsize, major, minor, kind, cpu, flags, uid, version, min_host, image_size, bss_size, rebase, entry, ram, cycles, crc, rw,
     image_id, run) = struct.unpack("<IHBBBBH" + "I" * 13, plg[:64])
    if magic != 0x31474C50 or kind != 6 or rebase != 0:
        print("%s: not an extension module (magic %08x kind %d rebase %d)" % (args[0], magic, kind, rebase)); return 1
    if zlib.crc32(plg[64:]) & 0xFFFFFFFF != crc:
        print("%s: file CRC mismatch (damaged)" % args[0]); return 1
    if hsize + image_size != len(plg):
        print("%s: size %d, header says %d" % (args[0], len(plg), hsize + image_size)); return 1
    flash = flash_of(args[1])
    want = zlib.crc32(flash) & 0xFFFFFFFF
    if want != image_id:
        print("%s is bound to image id %08x, %s has %08x over [0, 0x%x): NOT the same image" % (args[0], image_id, args[1], want, len(flash)))
        return 1
    if slot is not None and run != slot:
        print("%s is linked for 0x%08x, the image's slot is 0x%08x" % (args[0], run, slot)); return 1
    print("%s: bound to %s (image id %08x over [0, 0x%x), slot 0x%08x, %d B)" % (args[0], args[1], want, len(flash), run, len(plg)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
