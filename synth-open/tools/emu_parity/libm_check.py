#!/usr/bin/env python3
"""device newlib libm (release image Thumb code under Unicorn) vs the Python double-rounded libm the emu tools answer
host->sinf etc. with. Usage: TOOLCHAIN=... libm_check.py AXF N (AXF: platforms/mk66/build-trim/PolyendTracker.axf of make original-release)"""
import math, os, random, struct, subprocess, sys
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from unicorn.arm_const import *
axf, n = sys.argv[1], int(sys.argv[2])

raw = open(axf, "rb").read()
# program headers
e_phoff, = struct.unpack_from("<I", raw, 28); e_phnum, = struct.unpack_from("<H", raw, 44)
uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB); uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
for a, s in ((0, 0x100000), (0x1fff0000, 0x40000)):
    uc.mem_map(a, s)
for i in range(e_phnum):
    t, off, va, pa, fs, ms, fl, al = struct.unpack_from("<8I", raw, e_phoff + 32 * i)
    if t == 1 and fs and (va < 0x100000 or 0x1fff0000 <= va < 0x20030000):
        uc.mem_write(va, raw[off:off + fs])
uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20)); uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
nm = subprocess.run([os.environ["TOOLCHAIN"] + "nm", "-C", axf], capture_output=True, text=True, check=True).stdout
host = next(int(l.split()[0], 16) for l in nm.splitlines() if l.endswith("plug::(anonymous namespace)::s_host"))
fields = struct.unpack("<17I", bytes(uc.mem_read(host, 68)))
names = ["sinf", "cosf", "tanf", "tanhf", "expf", "logf", "powf", "memcpy", "memset", "alloc", "cycles", "query", "exp2f", "log2f"]
fn = dict(zip(names, fields[3:]))
print("s_host size %d level %d fs %r" % (fields[0], fields[1], struct.unpack("<f", struct.pack("<I", fields[2]))[0]))
TRAP = 0x2002F000; uc.mem_write(TRAP, b"\xfe\xe7")
f2u = lambda f: struct.unpack("<I", struct.pack("<f", f))[0]; u2f = lambda u: struct.unpack("<f", struct.pack("<I", u & 0xFFFFFFFF))[0]
def call(addr, x, y=None):
    uc.reg_write(UC_ARM_REG_SP, 0x2002E000); uc.reg_write(UC_ARM_REG_LR, TRAP | 1)
    uc.reg_write(UC_ARM_REG_S0, f2u(x))
    if y is not None: uc.reg_write(UC_ARM_REG_S1, f2u(y))
    uc.emu_start(addr | 1, TRAP, count=200000)
    return u2f(uc.reg_read(UC_ARM_REG_S0))
f32 = lambda v: struct.unpack("<f", struct.pack("<f", v))[0]
ref = {"sinf": math.sin, "cosf": math.cos, "tanf": math.tan, "tanhf": math.tanh, "expf": math.exp, "logf": math.log,
       "exp2f": lambda x: 2.0 ** x, "log2f": math.log2}
import ctypes
libm = ctypes.CDLL("libm.dylib" if sys.platform == "darwin" else "libm.so.6")
cf = {}
for k in ["sinf", "cosf", "tanf", "tanhf", "expf", "logf", "exp2f", "log2f"]:
    f = getattr(libm, k); f.restype = ctypes.c_float; f.argtypes = [ctypes.c_float]; cf[k] = f   # what a native twin calls
rng = random.Random(1)
dom = {"sinf": (-50, 50), "cosf": (-50, 50), "tanf": (-1.5, 1.5), "tanhf": (-8, 8), "expf": (-80, 80), "logf": (1e-6, 1e4),
       "exp2f": (-100, 100), "log2f": (1e-6, 1e4)}
for k in ["sinf", "cosf", "tanf", "tanhf", "expf", "logf", "exp2f", "log2f"]:
    lo, hi = dom[k]
    diff = 0; worst = 0; ex = None; hd = 0
    for i in range(n):
        x = f32(rng.uniform(lo, hi))
        d = call(fn[k], x)
        try:
            p = f32(ref[k](x))
        except OverflowError:
            p = math.inf
        if struct.pack("<f", d) != struct.pack("<f", cf[k](x)):
            hd += 1
        if struct.pack("<f", d) != struct.pack("<f", p):
            diff += 1
            ul = abs(f2u(d) - f2u(p))
            if ul > worst: worst, ex = ul, (x, d, p)
    print("%-6s at 0x%05x: %6d of %d differ from Python double->float (max %d ulp), %6d from the host libm float call %s" % (k, fn[k], diff, n, worst, hd, ex if ex else ""))
diff = 0
for i in range(n):
    x, y = f32(rng.uniform(0.01, 10)), f32(rng.uniform(-4, 4))
    if struct.pack("<f", call(fn["powf"], x, y)) != struct.pack("<f", f32(math.pow(x, y))): diff += 1
print("powf  : %d of %d differ" % (diff, n))
