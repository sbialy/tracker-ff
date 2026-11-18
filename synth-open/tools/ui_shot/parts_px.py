#!/usr/bin/env python3
"""parts_px.py PNG X Y [X Y ...]: the RGB of the pixels of a ui_shot frame, one "r,g,b" per point (parts_sim.sh reads the Build
column band and the red Build rule with it). Uses the PNG decoder of png_diff.py."""
import importlib.util
import os
import sys

spec = importlib.util.spec_from_file_location("png_diff", os.path.join(os.path.dirname(os.path.abspath(__file__)), "png_diff.py"))
pd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pd)
w, h, bpp, rows = pd.load(sys.argv[1])
v = sys.argv[2:]
for i in range(0, len(v) - 1, 2):
    x, y = int(v[i]), int(v[i + 1])
    print(",".join(str(c) for c in rows[y][x * bpp:x * bpp + 3]))
