import os, sys, numpy as np
sys.path.insert(0, os.path.join(os.environ.get("ENGINES_DIR", os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../../poly-engines")), "tools"))
import emu_render as R
plg, harness, out, blocks, voices = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
r = R.Run(plg, harness)
fl, _, _ = r.run(blocks, voices, [48, 55, 60, 64][:voices], 10 ** 6, [])
open(out, "wb").write(fl.astype("<f4").tobytes())
