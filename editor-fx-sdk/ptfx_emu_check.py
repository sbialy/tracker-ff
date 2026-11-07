#!/usr/bin/env python3
"""Host check for .ptfx plugins: loads each file the way core/effector/ptfx_loader.cpp does (header, flat copy to a
non-zero base, no relocation, bss cleared, entry points at base + offset | 1), runs plugin_init, plugin_set_param and
plugin_process_block in Unicorn and checks the output. A load through a stale link-time address (base 0) faults here,
because nothing is mapped there.

    /tmp/phzvenv/bin/python -I editor-fx-sdk/ptfx_emu_check.py build/Effects/plate_reverb.ptfx [...]

Needs unicorn. Not a replacement for the device, libm results are rounded to float32 by Python.
"""
import math
import struct
import sys

from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB, Uc, UcError
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_S0, UC_ARM_REG_S1, UC_ARM_REG_SP,
                               UC_CPU_ARM_CORTEX_A15)

CODE, HOST, BUF, STACK = 0x60000000, 0x50000000, 0x51000000, 0x52000000
SCRATCH = 0x62000000
TRAP = HOST + 0x1000
STUBS = HOST + 0x2000
HOST_FNS = ["sinf", "cosf", "tanf", "tanhf", "powf", "expf", "sqrtf", "logf", "log10f", "fminf", "fmaxf", "fabsf",
            "memcpy", "memset", "rand", "set_extra_samples"]
F32 = lambda v: struct.unpack("<f", struct.pack("<f", v))[0]


class Plugin:
    def __init__(self, path):
        d = open(path, "rb").read()
        h = struct.unpack_from("<IHH32s16sB3xIIIIIII", d, 0)
        (self.magic, self.ver, self.flags, name, author, self.nparams, self.scratch, code_off, code_size, bss,
         self.init, self.setp, self.proc) = h
        assert self.magic == 0x58465450 and self.ver == 1, "bad magic or version"
        assert code_off == 472 and code_off + code_size == len(d), "size fields do not match the file"
        self.name = name.split(b"\0")[0].decode()
        self.author = author.split(b"\0")[0].decode()
        self.params = []
        for i in range(self.nparams):
            lbl, mn, mx, df, st, unit, dt = struct.unpack_from("<8sffff6sBx", d, 88 + 32 * i)
            self.params.append((lbl.split(b"\0")[0].decode(), mn, mx, df, st, chr(dt)))
        self.code = d[code_off:code_off + code_size]
        self.bss = bss
        assert max(self.init, self.setp, self.proc) < code_size
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
        for base, size in ((CODE, 0x40000), (HOST, 0x10000), (BUF, 0x400000), (STACK, 0x10000),
                           (SCRATCH, (self.scratch + 0x1000) & ~0xFFF)):
            uc.mem_map(base, size)
        uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))
        uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        uc.mem_write(CODE, self.code)            # flat copy, no relocation, like the loader
        uc.mem_write(TRAP, b"\xfe\xe7")
        uc.mem_write(STUBS, b"\x70\x47\x00\xbf" * len(HOST_FNS))
        words = [44100] + [(STUBS + 4 * i) | 1 for i in range(len(HOST_FNS))]
        uc.mem_write(HOST, struct.pack("<%dI" % len(words), *words))
        self.extra = []
        self.count = 0
        self.rng = 12345
        uc.hook_add(UC_HOOK_CODE, self._service, begin=STUBS, end=STUBS + 4 * len(HOST_FNS) - 1)
        uc.hook_add(UC_HOOK_CODE, self._tick)

    def _tick(self, u, a, s, ud):
        self.count += 1

    def _rf(self, r):
        return struct.unpack("<f", struct.pack("<I", self.uc.reg_read(r) & 0xFFFFFFFF))[0]

    def _wf(self, r, v):
        self.uc.reg_write(r, struct.unpack("<I", struct.pack("<f", v))[0])

    def _service(self, u, addr, size, ud):
        n = HOST_FNS[(addr - STUBS) // 4]
        s0, s1 = self._rf(UC_ARM_REG_S0), self._rf(UC_ARM_REG_S1)
        one = {"sinf": math.sin, "cosf": math.cos, "tanf": math.tan, "tanhf": math.tanh, "expf": math.exp,
               "sqrtf": math.sqrt, "logf": math.log, "log10f": math.log10, "fabsf": abs}
        if n in one:
            self._wf(UC_ARM_REG_S0, F32(one[n](s0)))
        elif n == "powf":
            self._wf(UC_ARM_REG_S0, F32(math.pow(s0, s1)))
        elif n == "fminf":
            self._wf(UC_ARM_REG_S0, min(s0, s1))
        elif n == "fmaxf":
            self._wf(UC_ARM_REG_S0, max(s0, s1))
        elif n == "memcpy":
            d, sr, ln = (u.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
            u.mem_write(d, bytes(u.mem_read(sr, ln)))
        elif n == "memset":
            d, c, ln = (u.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
            u.mem_write(d, bytes([c & 255]) * ln)
        elif n == "rand":
            self.rng = (self.rng * 1103515245 + 12345) & 0x7FFFFFFF
            u.reg_write(UC_ARM_REG_R0, self.rng)
        elif n == "set_extra_samples":
            self.extra.append((u.reg_read(UC_ARM_REG_R0), u.reg_read(UC_ARM_REG_R1)))

    def call(self, off, *args, sfloat=None):
        uc = self.uc
        uc.reg_write(UC_ARM_REG_SP, STACK + 0xF000)
        uc.reg_write(UC_ARM_REG_LR, TRAP | 1)
        for r, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            uc.reg_write(r, v)
        if sfloat is not None:
            self._wf(UC_ARM_REG_S0, sfloat)
        self.count = 0
        uc.emu_start((CODE + off) | 1, TRAP)
        return uc.reg_read(UC_ARM_REG_R0) & 0xFFFFFFFF

    def load(self):
        self.uc.mem_write(CODE + len(self.code), bytes(self.bss))
        self.uc.mem_write(SCRATCH, bytes(self.scratch))
        self.call(self.init, SCRATCH, self.scratch, HOST)

    def set_param(self, i, v):
        self.call(self.setp, i, sfloat=v)

    def process(self, ins, n, channels):
        """ins: list of per-channel int16 lists or None. Returns (outs, return value, instructions)."""
        uc = self.uc
        inp, outp, tab = BUF, BUF + 0x1000, BUF + 0x10000
        in_ptrs, out_ptrs = [], []
        for c in range(channels):
            ib, ob = tab + 0x2000 * c, tab + 0x2000 * c + 0x1000
            if ins is not None:
                uc.mem_write(ib, struct.pack("<%dh" % n, *ins[c]))
            in_ptrs.append(ib if ins is not None else 0)
            out_ptrs.append(ob)
            uc.mem_write(ob, bytes(2 * n))
        uc.mem_write(inp, struct.pack("<2I", *(in_ptrs + [0, 0])[:2]))
        uc.mem_write(outp, struct.pack("<2I", *(out_ptrs + [0, 0])[:2]))
        r = self.call(self.proc, inp, outp, n, channels)
        outs = [list(struct.unpack("<%dh" % n, bytes(uc.mem_read(out_ptrs[c], 2 * n)))) for c in range(channels)]
        return outs, r, self.count


def burst(n, start, ch):
    """a decaying noise-and-sine hit at sample `start` of the test signal, sample i of the whole run"""
    out = []
    seed = 7 + ch
    for i in range(start, start + n):
        seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
        noise = ((seed >> 16) / 32768.0 - 1.0)
        env = math.exp(-i / 2500.0)
        out.append(int(max(-32768, min(32767, 22000 * env * (0.6 * noise + 0.4 * math.sin(i * 0.05))))))
    return out


def run(path, channels):
    p = Plugin(path)
    p.load()
    tag = "%s [%s ch=%d]" % (p.name, path.split("/")[-1], channels)
    for i, (lbl, mn, mx, df, st, dt) in enumerate(p.params):
        p.set_param(i, df)
    problems = []
    total = 128 * 40
    sig = [burst(total, 0, c) for c in range(channels)]
    outs = [[] for _ in range(channels)]
    worst = 0
    for b in range(0, total, 128):
        o, r, cnt = p.process([s[b:b + 128] for s in sig], 128, channels)
        if r != 128:
            problems.append("process_block returned %d for 128 samples" % r)
        worst = max(worst, cnt)
        for c in range(channels):
            outs[c] += o[c]
    peak = max(abs(v) for c in outs for v in c)
    print("%-44s %d params, scratch %d B, code %d B, max %d instr/block, peak %d, extra %s"
          % (tag, p.nparams, p.scratch, len(p.code), worst, peak, p.extra[-1:] or "-"))
    return p, sig, outs, problems


def check_transient(path, channels):
    p, sig, outs, problems = run(path, channels)
    if outs != sig:
        problems.append("0 dB / 0 dB is not bit-exact pass-through")
    def energy(x):
        return sum(v * v for c in x for v in c)
    p.set_param(0, 12.0)
    p.set_param(1, 0.0)
    a = [[] for _ in range(channels)]
    for b in range(0, 128 * 40, 128):
        o, _, _ = p.process([s[b:b + 128] for s in sig], 128, channels)
        for c in range(channels):
            a[c] += o[c]
    p.load()
    p.set_param(0, 0.0)
    p.set_param(1, -12.0)
    s = [[] for _ in range(channels)]
    for b in range(0, 128 * 40, 128):
        o, _, _ = p.process([sig[c][b:b + 128] for c in range(channels)], 128, channels)
        for c in range(channels):
            s[c] += o[c]
    print("   attack +12: energy x%.2f, sustain -12: energy x%.2f" % (energy(a) / energy(sig), energy(s) / energy(sig)))
    if not energy(a) > energy(sig):
        problems.append("Attack +12 dB did not raise the energy of a hit")
    if not energy(s) < energy(sig):
        problems.append("Sustain -12 dB did not lower the energy")
    return problems


def check_plate(path, channels):
    p, sig, outs, problems = run(path, channels)
    if not p.extra or p.extra[-1][1] == 0:
        problems.append("Ring Out did not request a tail")
    else:
        pre, app = p.extra[-1]
        print("   tail request: prepend %d, append %d samples (%.2f s)" % (pre, app, app / 44100.0))
        if app > 6 * 44100:
            problems.append("tail longer than 6 s")
    # tail: input NULL for 2 s worth of blocks, output must decay and stay finite
    tail = []
    for b in range(0, 128 * 690, 128):
        o, r, _ = p.process(None, 128, channels)
        tail.append(sum(abs(v) for v in o[0]))
        if r != 128:
            problems.append("tail block returned %d" % r)
            break
    n = len(tail)
    print("   tail mean |x| per block: first 10 %.0f, middle %.0f, last 10 %.0f" % (
        sum(tail[:10]) / 10, sum(tail[n // 2 - 5:n // 2 + 5]) / 10, sum(tail[-10:]) / 10))
    if not sum(tail[:10]) > 0:
        problems.append("no tail after the hit")
    if not sum(tail[-10:]) < sum(tail[:10]):
        problems.append("tail does not decay")
    # Mix 0 = dry
    p.load()
    for i, (lbl, mn, mx, df, st, dt) in enumerate(p.params):
        p.set_param(i, 0.0 if lbl == "Mix" else df)
    o, _, _ = p.process([s[:128] for s in sig], 128, channels)
    if o != [s[:128] for s in sig]:
        diff = max(abs(a - b) for c in range(channels) for a, b in zip(o[c], sig[c][:128]))
        print("   Mix 0: max |out - in| = %d" % diff)
        if diff > 2:
            problems.append("Mix 0 is not dry (max diff %d)" % diff)
    return problems


def main(paths):
    bad = 0
    for path in paths:
        for ch in (1, 2):
            try:
                if "transient" in path:
                    pr = check_transient(path, ch)
                elif "plate" in path:
                    pr = check_plate(path, ch)
                else:
                    pr = run(path, ch)[3]
            except UcError as e:
                pr = ["emulator fault: %s" % e]
            for x in pr:
                print("   FAIL", x)
            bad += len(pr)
    print("ptfx_emu_check: %s" % ("FAILED, %d problems" % bad if bad else "all passed"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
