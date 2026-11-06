#!/usr/bin/env python3
"""One PNG with every logo preview (make_plgi.py --preview) stacked top to bottom, a dark gap between them, in the order
given: the sheet the hub shows before flashing.

    python3 -I plgi_sheet.py -o sheet.png a.png b.png ...
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python -I leaves the script directory out
from make_plgi import read_png, write_png  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("png", nargs="+")
    a = ap.parse_args()
    imgs = [read_png(p)[2] for p in a.png]
    w = max(len(r[0]) for r in imgs)
    gap = [[40] * w for _ in range(12)]
    rows = []
    for i, r in enumerate(imgs):
        if i:
            rows += gap
        rows += [line + [0] * (w - len(line)) for line in r]
    write_png(a.out, rows)
    print("%s: %d logos, %d x %d" % (a.out, len(imgs), w, len(rows)))


if __name__ == "__main__":
    main()
