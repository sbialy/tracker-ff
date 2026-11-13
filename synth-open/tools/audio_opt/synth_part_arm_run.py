# Host side of run_synth_part_arm.sh: random and edge blocks through the old and the new mono part path in the emulator, int16 blocks compared.
import importlib.util, math, os, random, struct, sys

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("run_graph", os.path.join(here, "run_graph.py"))
rg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rg)

elf, tc, nblocks = sys.argv[1], sys.argv[2], int(sys.argv[3])
uc, syms, _ = rg.load(elf, tc)
rg.call(uc, rg.find(syms, "bench_setup"), 0)
rng = random.Random(12345)
NAN = struct.unpack("<f", struct.pack("<I", 0x7FC00000))[0]


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def pick_sample():
    k = rng.randrange(10)
    if k == 0:
        return struct.unpack("<f", struct.pack("<I", rng.getrandbits(32)))[0]
    if k == 1:
        return f32(rng.uniform(-2, 2))
    if k == 2:
        return f32(rng.choice((1.5, -1.5)) + rng.randrange(-4, 5) * 1e-6)
    if k == 3:
        return f32(rng.choice((1.4967, -1.4967)) + rng.randrange(-4, 5) * 1e-5)
    if k == 4:
        return f32(rng.choice((1.0, -1.0)) * (1.0 + rng.randrange(-4, 5) * 1e-7))
    if k == 5:
        return rng.choice((0.0, -0.0))
    if k == 6:
        return struct.unpack("<f", struct.pack("<I", rng.getrandbits(32) & 0x807FFFFF))[0]
    return f32(rng.uniform(-1.2, 1.2))


def pick_gain():
    k = rng.randrange(10)
    if k == 0:
        return 0.0
    if k == 1:
        return -90.0
    if k == 2:
        return f32(-90.0 + rng.randrange(-2, 3) * 0.01)
    if k == 3:
        return float("-inf")
    if k == 4:
        return NAN
    if k == 5:
        return float(rng.randrange(49) - 24)
    return f32(rng.uniform(-30, 12))


def put(sym, fmt, *v):
    uc.mem_write(syms[sym], struct.pack(fmt, *v))


def get(sym, n):
    return bytes(uc.mem_read(syms[sym], n))


bad = 0
for b in range(nblocks):
    blk = [pick_sample() for _ in range(128)]
    gain = pick_gain()
    reuse = 1 if gain == 0.0 else 0
    skip = 0 if reuse else int(rng.randrange(3) == 0)
    put("gRender", "<128f", *blk)
    put("gGainDb", "<f", gain)
    put("gReuse", "<i", reuse)
    put("gSkip", "<i", skip)
    for sym in ("gOldSend", "gOldPost", "gNewSend", "gNewPost"):
        uc.mem_write(syms[sym], b"\x55" * 256)
    rg.call(uc, rg.find(syms, "bench_run"), 0)
    rg.call(uc, rg.find(syms, "bench_run"), 1)
    ok = get("gOldPost", 256) == get("gNewPost", 256) and (skip or get("gOldSend", 256) == get("gNewSend", 256))
    if not ok:
        bad += 1
        if bad <= 5:
            print(f"DIFFERENT at block {b}: gain {gain} reuse {reuse} skip {skip}")
print(f"synth_part_arm: {nblocks} blocks x 128 samples through the Cortex-M4 code of the old passes and of synth_part_clip.h, {bad} differ")
print("PASS" if bad == 0 else "FAIL")
sys.exit(1 if bad else 0)
