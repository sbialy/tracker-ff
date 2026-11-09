# Runs bench_dirt.elf (run_bench.sh with KEEP_DIR set) in the Unicorn Cortex-M4F for <preset> and <blocks> blocks and writes the test block
# the source repeats (in.bin) and the last block the sink got (out.bin), for `dirt_render --check`: the ARM asm (vcvt Q31 clamp, mid-tread
# quantiser (qadd), ssat) against the plain float code of the host render. Run with python -I.
#   check_arm.py <elf> <toolchain-prefix> <preset 1..5> <blocks> <in.bin> <out.bin>
import subprocess, sys
from unicorn import UC_ARCH_ARM, UC_MODE_THUMB, Uc
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_A15, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_SP)
ELF, TC, preset, blocks, fin, fout = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5], sys.argv[6]
BASE, SIZE = 0x20000000, 0x400000
TRAP = BASE + 0x3F0000
subprocess.run([TC + "-objcopy", "-O", "binary", ELF, ELF + ".bin"], check=True)
image = open(ELF + ".bin", "rb").read()
syms = {}
for line in subprocess.run([TC + "-nm", "-C", "--defined-only", ELF], check=True, capture_output=True, text=True).stdout.splitlines():
    p = line.split(None, 2)
    if len(p) == 3: syms[p[2]] = int(p[0], 16)
uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB); uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
uc.mem_map(BASE, SIZE); uc.mem_write(BASE, image); uc.mem_write(TRAP, b"\xfe\xe7")
uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20)); uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
def call(fn, arg):
    uc.reg_write(UC_ARM_REG_SP, BASE + SIZE - 0x10000); uc.reg_write(UC_ARM_REG_LR, TRAP | 1); uc.reg_write(UC_ARM_REG_R0, arg)
    uc.emu_start(syms[fn] | 1, TRAP)
call("bench_setup", preset)
pat = bytes(uc.mem_read(syms["bench_pat"], 512))
call("bench_run", blocks)
open(fin, "wb").write(pat); open(fout, "wb").write(bytes(uc.mem_read(syms["bench_out"], 512)))
