#!/usr/bin/env python3
"""Refuse .ptf images that cannot be flashed safely on the original Tracker (MK66, 1 MiB flash).

Modelled on tools/check_image.py of OpenTracker. Checks an Intel HEX .ptf for:
  * Every data byte below FLASH_IMG_MAX (default 0xFF000): the top sector stays untouched.
    The ld -print-memory-usage FLASH line omits the .data load image, so only the highest
    ihex address tells the truth.
  * Only record types 00 (data), 01 (EOF) and 02 (segment address), exactly like the stock
    1.9.2 and CE 1.0.0 images. Type 04 means the image crossed 1 MiB.
  * Flash configuration field (0x400-0x40F) identical to stock: FSEC 0xDE, FOPT 0xF9.
  * A sane vector table: SP inside SRAM, Thumb reset vector inside the image.
  * Valid record checksums and lengths.
  * Last 4 KB sector: fewer than 0x800 B in it is a warning. With --min-tail N (the release gate passes 0x800) fewer than N B
    is an error. A tail of 0 (the image ends on a sector line) is a full last sector and passes.
  * With --proven ADDR (the release gate passes the device-proven end, 0xFC2AC): an image that ends past ADDR is an error, the unit has not
    booted code up there. ALLOW_UNPROVEN_END=1 in the environment turns it into a loud warning: set it only together with the device row
    that proved the new end. --warn-proven ADDR (make original) is the same check as a warning only.
Usage: check_ptf.py [--max 0xFF000] [--min-tail 0x800] [--proven 0xFC2AC | --warn-proven 0xFC2AC] <image.ptf>
"""
import os
import sys

FLASH_IMG_MAX = 0xFF000
ALLOWED_RECORDS = {0, 1, 2}
SHORT_TAIL = 0x800   # a last 4 KB sector with fewer bytes than this is suspect
STOCK_CFG = bytes([0xFF] * 12 + [0xDE, 0xF9, 0xFF, 0xFF])


def load_ihex(path):
    mem, upper, types = {}, 0, set()
    for n, line in enumerate(open(path), 1):
        line = line.strip()
        if not line:
            continue
        if line[0] != ":":
            sys.exit(f"line {n}: not Intel HEX")
        raw = bytes.fromhex(line[1:])
        if sum(raw) & 0xFF:
            sys.exit(f"line {n}: bad checksum")
        count, addr, rtype, data = raw[0], (raw[1] << 8) | raw[2], raw[3], raw[4:4 + raw[0]]
        if len(raw) != count + 5:
            sys.exit(f"line {n}: record length mismatch")
        types.add(rtype)
        if rtype == 0:
            for i, b in enumerate(data):
                mem[upper + addr + i] = b
        elif rtype == 2:
            upper = ((data[0] << 8) | data[1]) << 4
        elif rtype == 4:
            upper = ((data[0] << 8) | data[1]) << 16
        elif rtype == 1:
            break
    return mem, types


def main(argv):
    limit = FLASH_IMG_MAX
    min_tail = 0
    proven = None   # (address, fatal)
    args = argv[1:]
    while len(args) >= 2 and args[0] in ("--max", "--min-tail", "--proven", "--warn-proven"):
        if args[0] == "--max":
            limit = int(args[1], 0)
        elif args[0] == "--min-tail":
            min_tail = int(args[1], 0)
        else:
            proven = (int(args[1], 0), args[0] == "--proven")
        args = args[2:]
    if len(args) != 1:
        sys.exit(__doc__)
    path = args[0]
    mem, types = load_ihex(path)
    if not mem:
        sys.exit("IMAGE CHECK FAILED: no data records")

    def byte(a): return mem.get(a, 0xFF)
    def word(a): return sum(byte(a + i) << (8 * i) for i in range(4))

    errors, notes = [], []
    top = max(mem) + 1
    if proven and top > proven[0]:
        msg = (f"the image ends at 0x{top:X}, {top - proven[0]} B past 0x{proven[0]:X}, the end of the last image that was run on the unit "
               f"(device-proven). Nothing has booted code up there: prove the new end on the device first")
        if not proven[1]:
            notes.append("WARNING " + msg + " (the release gate fails on it)")
        elif os.environ.get("ALLOW_UNPROVEN_END") == "1":
            notes.append("WARNING " + msg + ". ALLOW_UNPROVEN_END=1 is set: allowed, cite the device row that proved this end")
        else:
            errors.append(msg + ". Set ALLOW_UNPROVEN_END=1 together with that device row to override")
    if top > limit:
        errors.append(f"highest address 0x{top:X} beyond limit 0x{limit:X} ({top - limit} B over)")
    if types - ALLOWED_RECORDS:
        errors.append(f"unexpected Intel HEX record types {sorted(types - ALLOWED_RECORDS)} (stock uses 00/01/02)")
    cfg = bytes(byte(0x400 + i) for i in range(16))
    if cfg != STOCK_CFG:
        errors.append(f"flash config {cfg.hex()} != stock {STOCK_CFG.hex()} (risk of locking the MCU)")
    sp, reset = word(0), word(4)
    if not (0x1FFF0000 < sp <= 0x20030000):
        errors.append(f"initial SP 0x{sp:08X} outside SRAM")
    if not (reset & 1) or not (0x40 <= (reset & ~1) < top):
        errors.append(f"reset vector 0x{reset:08X} not a Thumb address inside the image")
    if min(mem) != 0:
        errors.append(f"image starts at 0x{min(mem):X}, expected 0")
    last = top % 4096
    if min_tail and 0 < last < min_tail:
        errors.append(f"the image ends {last} B (0x{last:X}) into its last 4 KB sector at 0x{top - last:X}, the release needs at least 0x{min_tail:X} B "
                      f"there: the unit's bootloader is suspected of leaving a short tail erased. "
                      f"Grow the image by {min_tail - last} B or more (FF_TAIL_PAD, make original sizes it with synth-open/tools/tail_pad.py)")
    name = path.rsplit("/", 1)[-1]
    if errors:
        print(f"PTF CHECK FAILED: {name}\n  " + "\n  ".join(errors))
        print(f"  (highest address 0x{top:X}, record types {sorted(types)})")
        return 1
    print(f"ptf ok: {name}: highest address 0x{top:X} ({limit - top} B under 0x{limit:X}"
          + (f", {proven[0] - top} B under the device-proven 0x{proven[0]:X}" if proven and top <= proven[0] else "") + "), "
          f"record types {sorted(types)}, SP=0x{sp:08X}, reset=0x{reset:08X}, flash config = stock")
    for n in notes:
        print("  " + n)
    if 0 < last < SHORT_TAIL:
        # not an error for a stock style image (they ended anywhere), but an image bound to a module must not (pack_ext.py refuses)
        print(f"  WARNING short last sector: the image ends {last} B into the 4 KB sector at 0x{top - last:X}. The unit's bootloader is "
              f"suspected of leaving such a tail erased: the data there reads 0xFF, a module "
              f"bound to this image is refused. Grow the image by {SHORT_TAIL - last} B or more (FF_TAIL_PAD, make original sizes it with synth-open/tools/tail_pad.py).")
    return 0


sys.exit(main(sys.argv))
