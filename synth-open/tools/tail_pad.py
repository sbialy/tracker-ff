#!/usr/bin/env python3
"""Size of the tail pad (FF_TAIL_PAD, synth-open/ext_module.cpp) that keeps the last 4 KB flash sector from being short.

The unit's bootloader once left a short last sector unprogrammed, so an image must end on a sector line (tail 0) or at least
0x800 B into its last sector (check_ptf.py --min-tail 0x800, pack_ext.py). The pad is
0 when the unpadded image already ends well, otherwise the deficit rounded up to 16 B. Sections after the pad are aligned, so a pad of
N B moves the end by N to N + 7 B: the tool is called again on the relinked image and adds 16 B per round until the rule holds.

  tail_pad.py [--toolchain PREFIX] [--have N] <image.axf>      prints the pad the image needs
    --have N   the pad the axf was linked with (default 0). The same N back means the image ends well
"""
import subprocess
import sys

MIN_TAIL = 0x800


def image_end(toolchain, axf):
    out = subprocess.run([toolchain + "size", axf], check=True, capture_output=True, text=True).stdout.split("\n")
    text, data = (int(x) for x in out[1].split()[:2])
    return text + data   # data is 0 when .data carries the X flag, the sum is the end address either way


def extra_pad(end):
    """Bytes to add to the pad of an image that ends at `end`: 0 when its last sector is full enough"""
    tail = end & 0xFFF
    if tail == 0 or tail >= MIN_TAIL:
        return 0
    return (MIN_TAIL - tail + 15) & ~15


def main(argv):
    toolchain, have, args = "arm-none-eabi-", 0, argv[1:]
    while len(args) >= 2 and args[0] in ("--toolchain", "--have"):
        if args[0] == "--toolchain":
            toolchain = args[1]
        else:
            have = int(args[1], 0)
        args = args[2:]
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    end = image_end(toolchain, args[0])
    if have > end:
        print("tail_pad: --have %d is larger than the image (%d B)" % (have, end), file=sys.stderr)
        return 1
    print(have + extra_pad(end))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
