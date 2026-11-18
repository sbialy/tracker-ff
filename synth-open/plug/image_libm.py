#!/usr/bin/env python3
"""The release image's own libm for the Unicorn plugin harnesses (emu_plug.py, poly-engines tools/emu_render.py): --image-libm AXF.

The device answers a plugin's host->sinf ... log2f with the image's newlib Thumb code (the functions plug::s_host points at). The
harnesses used to answer with Python's math rounded to float (emu_render.py: 0 for tanf, tanhf, expf, logf, powf, exp2f, log2f), so a
plugin that calls libm in its audio path rendered other bits than the unit and the ui_shot bridge (synth-open/tools/ui_shot,
host_ucbridge.cpp runs the same image code). ImageLibm maps the axf's flash and RAM images into a SEPARATE Unicorn machine and runs
the image function there for every call: no address of the harness machine is touched (errno and the newlib state live in the side
machine). The functions are pure, so a result is cached per (function, argument bits).

    lm = ImageLibm("platforms/mk66/build-trim/PolyendTracker.axf")
    bits = lm.call_bits("sinf", f32_bits)          # float32 bits in, float32 bits out
    lm.call_bits("powf", x_bits, y_bits)

Host tool, NOT firmware.
"""
import struct

from unicorn import UC_ARCH_ARM, UC_MODE_THUMB, Uc
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_S0, UC_ARM_REG_S1, UC_ARM_REG_SP,
                               UC_CPU_ARM_CORTEX_A15)

# plug_host (plug_abi.h): struct_size, level, sample_rate, then these function pointers in this order
HOST_FNS = ["sinf", "cosf", "tanf", "tanhf", "expf", "logf", "powf", "memcpy", "memset", "alloc", "cycles", "query", "exp2f", "log2f"]
LIBM = ("sinf", "cosf", "tanf", "tanhf", "expf", "logf", "powf", "exp2f", "log2f")
S_HOST = ("_ZN4plug12_GLOBAL__N_1L6s_hostE", "_ZN4plug12_GLOBAL__N_16s_hostE")   # plug::(anonymous namespace)::s_host (const: the L form)
FLASH, FLASH_SIZE = 0x00000000, 0x100000
RAM, RAM_SIZE = 0x1FFF0000, 0x40000          # SRAM_L + SRAM_U: .data and .bss of the image
STACK = 0x2002E000
TRAP = 0x2002F000


def elf_symbols(raw):
    """name -> (value, size) of the ELF32 little endian .symtab (first definition wins)"""
    shoff, = struct.unpack_from("<I", raw, 32)
    shentsize, shnum = struct.unpack_from("<HH", raw, 46)
    secs = [struct.unpack_from("<10I", raw, shoff + i * shentsize) for i in range(shnum)]
    out = {}
    for s in secs:
        if s[1] != 2:   # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for k in range(s[5] // 16):
            nm, val, size, info, other, shndx = struct.unpack_from("<IIIBBH", raw, s[4] + 16 * k)
            if not nm or shndx == 0:
                continue
            a = strtab[4] + nm
            out.setdefault(raw[a:raw.index(b"\0", a)].decode("latin1"), (val, size))
    return out


class ImageLibm:
    def __init__(self, axf):
        raw = open(axf, "rb").read()
        assert raw[:4] == b"\x7fELF" and raw[4] == 1 and raw[5] == 1, "%s: not an ELF32 little endian file" % axf
        e_phoff, = struct.unpack_from("<I", raw, 28)
        e_phentsize, e_phnum = struct.unpack_from("<HH", raw, 42)
        uc = self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
        uc.mem_map(FLASH, FLASH_SIZE)
        uc.mem_map(RAM, RAM_SIZE)
        for i in range(e_phnum):
            t, off, va, pa, fs, ms, fl, al = struct.unpack_from("<8I", raw, e_phoff + i * e_phentsize)
            if t != 1 or not fs:
                continue
            for lo, n in ((FLASH, FLASH_SIZE), (RAM, RAM_SIZE)):
                if lo <= va < lo + n:
                    uc.mem_write(va, raw[off:off + min(fs, lo + n - va)])
        uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))
        uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        uc.mem_write(TRAP, b"\xfe\xe7")
        syms = elf_symbols(raw)
        hs = [syms[n][0] for n in S_HOST if n in syms]
        assert hs, "%s: no plug::s_host (not a tracker-ff image with the plugin host)" % axf
        self.host = hs[0]
        fields = struct.unpack("<17I", bytes(uc.mem_read(self.host, 68)))
        self.fn = dict(zip(HOST_FNS, fields[3:]))
        self.cache = {}
        self.calls = 0

    def call_bits(self, name, x, y=None):
        """float32 bits of name(x[, y]) as the image computes it"""
        key = (name, x, y)
        r = self.cache.get(key)
        if r is not None:
            return r
        uc = self.uc
        uc.reg_write(UC_ARM_REG_SP, STACK)
        uc.reg_write(UC_ARM_REG_LR, TRAP | 1)
        uc.reg_write(UC_ARM_REG_S0, x & 0xFFFFFFFF)
        if y is not None:
            uc.reg_write(UC_ARM_REG_S1, y & 0xFFFFFFFF)
        uc.emu_start(self.fn[name] | 1, TRAP, count=1000000)
        r = uc.reg_read(UC_ARM_REG_S0) & 0xFFFFFFFF
        self.cache[key] = r
        self.calls += 1
        return r

    def call(self, name, x, y=None):
        """float in, float out (each rounded to float32)"""
        b = lambda v: struct.unpack("<I", struct.pack("<f", v))[0]   # noqa: E731
        r = self.call_bits(name, b(x), None if y is None else b(y))
        return struct.unpack("<f", struct.pack("<I", r))[0]
