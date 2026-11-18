#!/usr/bin/env python3
"""arm_clock_check.py TOOLCHAIN_PREFIX [MINUTES]

Runs the sequencer's integer tick clock (core/source/internalTickClock.h) compiled by the release compiler for the Cortex-M4F in the Unicorn
emulator (python with the unicorn module, run with -I) and compares the tick count and the accumulator after MINUTES (default 30) of audio with
the exact integer model (python integers) and with the float64 ideal, for the export clock (128 frames at 44,100 Hz) and the live clock (128
frames at the MK66 rate 44,117.6470588 Hz), at 120 125 128 174 BPM. Prints the ticks, the tempo they imply, and the difference from the exact
model. OPT=-Os (environment) builds at the optimisation of core/source in the TRIM release image, default -O2. Exit 0 when the ARM build gives the model's tick count and accumulator exactly, 1 otherwise. Host only.
"""
import os, struct, subprocess, sys, tempfile
from unicorn import UC_ARCH_ARM, UC_MODE_THUMB, Uc
from unicorn.arm_const import UC_CPU_ARM_CORTEX_A15, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_SP

TC = sys.argv[1]
MINUTES = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
tmp = tempfile.mkdtemp(prefix="arm_clock.")
elf = os.path.join(tmp, "clock_arm.elf")
subprocess.run([TC + "g++", "-std=gnu++20", os.environ.get("OPT", "-O2"), "-mcpu=cortex-m4", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard", "-mthumb", "-ffreestanding", "-fno-builtin",
                "-fno-exceptions", "-fno-rtti", "-ffunction-sections", "-fdata-sections", "-I", os.path.join(ROOT, "core/source"),
                os.path.join(HERE, "clock_arm.cpp"), "-nostartfiles", "-nostdlib", "-Wl,-Ttext=0x20000000", "-Wl,--gc-sections", "-Wl,-e,clock_run", "-o", elf, "-lgcc"],
               check=True)
subprocess.run([TC + "objcopy", "-O", "binary", elf, elf + ".bin"], check=True)
image = open(elf + ".bin", "rb").read()
syms = {}
for line in subprocess.run([TC + "nm", "-C", "--defined-only", elf], check=True, capture_output=True, text=True).stdout.splitlines():
    p = line.split(None, 2)
    if len(p) == 3: syms[p[2]] = int(p[0], 16)
BASE, SIZE = 0x20000000, 0x400000
TRAP = BASE + 0x3F0000
uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB); uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
uc.mem_map(BASE, SIZE); uc.mem_write(BASE, image); uc.mem_write(TRAP, b"\xfe\xe7")
uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20)); uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)

def f32(x): return struct.unpack("<f", struct.pack("<f", x))[0]
def bits(x): return struct.unpack("<I", struct.pack("<f", x))[0]

def arm(tempo, interval, blocks):
    uc.reg_write(UC_ARM_REG_SP, BASE + SIZE - 0x10000); uc.reg_write(UC_ARM_REG_LR, TRAP | 1)
    uc.reg_write(UC_ARM_REG_R0, bits(tempo)); uc.reg_write(UC_ARM_REG_R1, bits(interval)); uc.reg_write(UC_ARM_REG_R2, blocks)
    uc.emu_start(syms["clock_run"] | 1, TRAP, count=2_000_000_000)
    ticks, last, acc, per = struct.unpack("<IIqQ", bytes(uc.mem_read(syms["g_result"], 24))[:24])
    return ticks, acc, per

def model(tempo, interval, blocks):
    block = int(f32(interval) * 1000000.0 * 67108864.0)
    per = int(f32((312500.0 / f32(tempo)) / 12.0) * 67108864.0)
    total = blocks * block
    return total // per, total % per, per

bad = 0
print("compiler flags %s" % os.environ.get("OPT", "-O2"))
print("%-8s %-9s %9s %12s %12s %12s %10s" % ("clock", "tempo", "minutes", "ARM ticks", "model ticks", "ideal ticks", "implied"))
for name, rate in (("export", 44100.0), ("live", 44117.6470588)):
    interval = f32(128.0 / f32(rate))
    blocks = int(MINUTES * 60.0 * rate / 128.0)
    for tempo in (120.0, 125.0, 128.0, 174.0):
        t_arm, acc_arm, per_arm = arm(tempo, interval, blocks)
        t_mod, acc_mod, per_mod = model(tempo, interval, blocks)
        secs = blocks * 128.0 / rate
        ideal = secs * tempo * 2304.0 / 60.0
        implied = t_arm / secs * 60.0 / 2304.0
        ok = t_arm == t_mod and acc_arm == acc_mod and per_arm == per_mod
        bad += not ok
        print("%-8s %-9s %9.1f %12d %12d %12.1f %10.5f %s" % (name, "%g BPM" % tempo, secs / 60, t_arm, t_mod, ideal, implied, "ok" if ok else "MISMATCH"))
print("RESULT: %s" % ("GREEN (ARM build == exact model)" if bad == 0 else "RED (%d mismatches)" % bad))
sys.exit(1 if bad else 0)
