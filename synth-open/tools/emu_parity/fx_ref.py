#!/usr/bin/env python3
"""Reference renders of the REAL card master inserts (.plg) and Sample Editor effects (.ptfx) for the bridge proof of emu parity T6
(ref/emu-parity-plan.md). Same method as T2's synth proof: this file loads the plugin bytes by itself (its own header, CRC, rebase,
.ptfx placement) into a Python Unicorn machine that holds the release axf, so the host services a plugin calls (sinf ... log2f,
memcpy, memset, the .ptfx host API) are the release image's own newlib Thumb code, not Python's math. The bridge side
(host_ucbridge_fx_test.cc) runs the very same transcript through host_ucbridge and writes the same files, `cmp` them.

  TOOLCHAIN=.../arm-none-eabi- /tmp/r140venv/bin/python -I fx_ref.py AXF CARD_DIR OUT_DIR

CARD_DIR holds Plugins/Master/*.plg and Effects/se_*.ptfx. OUT_DIR gets fx_in_ins.f32, fx_in_ptfx.i16 (the shared inputs, the
bridge test reads them), ref_ins_<name>.f32 (+ .q), ref_ptfx_<name>.i16 and ref_summary.txt (FNV-1a per plugin).

Insert transcript (BLOCKS blocks of 128 frames): params all def at block 0, all max at 40, all min at 80, a spread at 120, all def at
200. Before every block set_tempo(120), set_transport(beat = b / 4, playing = (b / 16) even) for a plugin that answers PLUG_EXT_TEMPO,
quiet() after the params for one that answers PLUG_EXT_TAIL (recorded in .q), then process(io pair, 2, 128).
.ptfx transcript (mtEffectModular's order): four runs (def, max, min, spread), each memset(scratch), init(scratch, 640 KB, api),
set_param for the 12 slots (the unused ones 0), then process_block in <= 8192 sample pieces over prepend + input + append, the
extra samples a plugin asked for with set_extra_samples. The .ptfx run in sorted file order on ONE machine (the image's rand state
is shared), the bridge test does the same.
"""
import os, struct, subprocess, sys, zlib
import numpy as np
from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_S0, UC_ARM_REG_SP, UC_CPU_ARM_CORTEX_A15)

N = 128
BLOCKS = 400
PT_IN = 20000
PT_CHUNK = 8192
PT_SCRATCH_OFF, PT_SCRATCH_SIZE = 128 * 1024, (768 - 128) * 1024
INS_BASE = 0x70800000   # the bridge runs the same bytes at the SRAM_L insert slot: another address, same output
STACK = 0x2002E000
TRAP = 0x2002F000
HEAP0 = 0x70900000
IO = 0x70A00000


def f2u(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def fnv(b):
    h = 2166136261
    for c in b:
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h


class Img:
    def __init__(self, axf, toolchain):
        raw = open(axf, "rb").read()
        e_phoff, = struct.unpack_from("<I", raw, 28)
        e_phnum, = struct.unpack_from("<H", raw, 44)
        uc = self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
        uc.mem_map(0, 0x100000)
        uc.mem_map(0x1FFF0000, 0x40000)
        uc.mem_map(0x70000000, 0x1000000)
        for i in range(e_phnum):
            t, off, va, pa, fs, ms, fl, al = struct.unpack_from("<8I", raw, e_phoff + 32 * i)
            if t == 1 and fs and (va < 0x100000 or 0x1FFF0000 <= va < 0x20030000):
                uc.mem_write(va, raw[off:off + fs])
        uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))
        uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        uc.mem_write(TRAP, b"\xfe\xe7")
        nm = subprocess.run([toolchain + "nm", "-C", axf], capture_output=True, text=True, check=True).stdout
        self.syms = {}
        for l in nm.splitlines():
            p = l.split(None, 2)
            if len(p) == 3:
                self.syms.setdefault(p[2], int(p[0], 16))
        self.heap = HEAP0

    def sym(self, name):
        for k, v in self.syms.items():
            if k == name or k.startswith(name + "("):
                return v
        raise KeyError(name)

    def u32(self, a):
        return struct.unpack("<I", bytes(self.uc.mem_read(a, 4)))[0]

    def service(self, fn, handler):
        """the image function fn answered here: `bx lr` in the mapped copy plus a code hook (what the bridge does natively)"""
        fn &= ~1
        self.uc.mem_write(fn, b"\x70\x47")
        self.uc.hook_add(UC_HOOK_CODE, lambda uc, addr, size, ud: handler(uc), begin=fn, end=fn)

    def call(self, fn, ints=(), floats=()):
        uc = self.uc
        uc.reg_write(UC_ARM_REG_SP, STACK)
        uc.reg_write(UC_ARM_REG_LR, TRAP | 1)
        for r, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), ints):
            uc.reg_write(r, v & 0xFFFFFFFF)
        for i, f in enumerate(floats):
            uc.reg_write(UC_ARM_REG_S0 + i, f2u(f))
        uc.emu_start(fn | 1, TRAP)
        return uc.reg_read(UC_ARM_REG_R0)

    def alloc(self, uc):
        n = (uc.reg_read(UC_ARM_REG_R0) + 7) & ~7
        uc.reg_write(UC_ARM_REG_R0, self.heap)
        self.heap += max(n, 8)


def make_inputs(out):
    rng = np.random.RandomState(20261116)
    t = np.arange(BLOCKS * N, dtype=np.float64)
    x = 0.45 * np.sin(2 * np.pi * 110.0 * t / 44117.647) + 0.2 * np.sin(2 * np.pi * 1730.0 * t / 44117.647)
    env = np.where((t // N) % 100 < 90, 1.0, 0.0)   # a gap of silence every 100 blocks (tails, denormals)
    L = ((x + 0.08 * rng.uniform(-1, 1, t.size)) * env).astype(np.float32)
    R = ((0.8 * x[::-1] + 0.08 * rng.uniform(-1, 1, t.size)) * env).astype(np.float32)
    ins = np.stack([L.reshape(BLOCKS, N), R.reshape(BLOCKS, N)], axis=1).astype("<f4")
    open(os.path.join(out, "fx_in_ins.f32"), "wb").write(ins.tobytes())
    ti = np.arange(PT_IN, dtype=np.float64)
    pt = 12000 * np.sin(2 * np.pi * 220.0 * ti / 44100.0) + 3000 * rng.uniform(-1, 1, PT_IN)
    pt[PT_IN // 2:PT_IN // 2 + 2000] *= 0.02
    pt = np.clip(np.rint(pt), -32768, 32767).astype("<i2")
    open(os.path.join(out, "fx_in_ptfx.i16"), "wb").write(pt.tobytes())
    return ins, pt


def spread(mn, mx, i):
    f = np.float32((i * 37) % 100) / np.float32(100)
    return float(np.float32(mn) + (np.float32(mx) - np.float32(mn)) * f)


def run_insert(img, path, ins):
    data = open(path, "rb").read()
    (magic, hsize, amaj, amin, kind, cpu, flags, uid, ver, min_host, imgsz, bss, nreb, entry, ram, cyc, crc, rwoff) = \
        struct.unpack_from("<IHBBBBHIIIIIIIIIII", data, 0)
    rwoff = rwoff or imgsz
    body = data[hsize:hsize + imgsz + 4 * nreb]
    logo = struct.unpack_from("<I", data, 64 + 72)[0] if hsize >= 64 + 96 else 0
    end = hsize + imgsz + 4 * nreb
    assert zlib.crc32(data[64:end]) & 0xFFFFFFFF == crc, "crc"
    code = bytearray(body[:imgsz])
    rwbase = INS_BASE + rwoff
    for k in range(nreb):
        off, = struct.unpack_from("<I", body, imgsz + 4 * k)
        v, = struct.unpack_from("<I", code, off)
        struct.pack_into("<I", code, off, (INS_BASE + v) if v < rwoff else (rwbase + v - rwoff))
    uc = img.uc
    uc.mem_write(INS_BASE, bytes(code) + bytes(bss))
    img.heap = HEAP0
    host = img.sym("plug::(anonymous namespace)::s_host")
    desc = img.call(INS_BASE + entry, (host, (1 << 16) | 4))
    d = struct.unpack("<IIIIIIIIIIII", bytes(uc.mem_read(desc, 48)))
    size, dkind, name_p, author_p, nparams, params_p, inst_size, f_init, f_reset, f_set, f_proc, f_query = d
    inst = img.heap
    img.heap += (max(inst_size, 8) + 7) & ~7
    uc.mem_write(inst, bytes(max(inst_size, 8)))
    assert img.call(f_init, (inst, host)) == 0, "init"
    params = []
    for i in range(nparams):
        pn, pu, mn, mx, df, st, ec = struct.unpack("<IIffffH", bytes(uc.mem_read(params_p + 32 * i, 26)))
        params.append((mn, mx, df))
    tempo = tail = 0
    if os.environ.get('FX_DBG'):
        print('  query', hex(f_query), 'inst', inst_size, 'kind', dkind)
    if f_query:
        tempo = img.call(f_query, (4,))
        tail = img.call(f_query, (2,))
    f_tempo = img.u32(tempo + 4) if tempo else 0
    f_trans = img.u32(tempo + 8) if tempo and img.u32(tempo) >= 12 else 0
    f_quiet = img.u32(tail + 8) if tail and img.u32(tail) >= 12 else 0
    L, R, io = IO, IO + 0x400, IO + 0x800
    uc.mem_write(io, struct.pack("<II", L, R))
    out = np.zeros((BLOCKS, 2, N), "<f4")
    quiet = []
    for b in range(BLOCKS):
        sets = None
        if b == 0 or b == 200:
            sets = [df for mn, mx, df in params]
        elif b == 40:
            sets = [mx for mn, mx, df in params]
        elif b == 80:
            sets = [mn for mn, mx, df in params]
        elif b == 120:
            sets = [spread(mn, mx, i) for i, (mn, mx, df) in enumerate(params)]
        if f_tempo:
            img.call(f_tempo, (inst,), (120.0,))
        if f_trans:   # r0 = inst, r1 = playing, s0 = beat
            img.call(f_trans, (inst, 1 if (b // 16) % 2 == 0 else 0), (float(np.float32(b) * np.float32(0.25)),))
        if sets:
            for i, v in enumerate(sets):
                img.call(f_set, (inst, i), (v,))
        if f_quiet:
            quiet.append(img.call(f_quiet, (inst,)))
        uc.mem_write(L, ins[b, 0].tobytes())
        uc.mem_write(R, ins[b, 1].tobytes())
        img.call(f_proc, (inst, io, 2, N))
        out[b, 0] = np.frombuffer(bytes(uc.mem_read(L, N * 4)), "<f4")
        out[b, 1] = np.frombuffer(bytes(uc.mem_read(R, N * 4)), "<f4")
    return out, quiet, nparams, bool(f_tempo), bool(f_quiet)


def run_ptfx(img, path, pin):
    data = open(path, "rb").read()
    hdr = struct.unpack_from("<I2H32s16sB3sIIIIIII", data, 0)
    magic, ver, flags, name, author, nparams, _, scratch_size, code_off, code_size, bss_size, init_off, set_off, proc_off = hdr
    assert magic == 0x58465450 and ver == 1
    params = []
    for i in range(12):
        lab, mn, mx, df, st, unit, dt, pad = struct.unpack_from("<8sffff6sBB", data, 0x58 + 32 * i)
        params.append((mn, mx, df))
    uc = img.uc
    code_base = img.sym("externalRamScratchBuff")
    uc.mem_write(code_base, data[code_off:code_off + code_size] + bytes(bss_size))
    uc.ctl_flush_tb()   # the translated blocks of the previous .ptfx at the same addresses (the bridge: ucb::codeChanged)
    api = img.sym("s_ptfxHostApi")
    scratch = code_base + PT_SCRATCH_OFF
    ib, ob = IO, IO + 0x10000   # the whole input and the whole output are contiguous guest memory, as the sample memory is on the unit
    ia, oa = IO + 0xA000, IO + 0xA010
    cap = (0x71000000 - ob) // 2
    uc.mem_write(ib, pin.tobytes())
    res = bytearray()
    log = []
    for cfg in range(4):
        del img.extra[:]
        uc.mem_write(scratch, bytes(PT_SCRATCH_SIZE))
        img.call(code_base + init_off, (scratch, PT_SCRATCH_SIZE, api))
        for i in range(12):
            if i >= nparams:
                v = 0.0
            else:
                mn, mx, df = params[i]
                v = df if cfg == 0 else mx if cfg == 1 else mn if cfg == 2 else spread(mn, mx, i)
            img.call(code_base + set_off, (i,), (v,))
        pre = sum(e[0] for e in img.extra)
        app = sum(e[1] for e in img.extra)
        total = pre + PT_IN + app
        uc.mem_write(ob, bytes(min(total + PT_CHUNK, cap) * 2))
        p = 0
        while p < total:
            bs = min(PT_CHUNK, total - p)
            src = 0
            if p < pre:
                bs = min(bs, pre - p)
            elif p < pre + PT_IN:
                bs = min(bs, pre + PT_IN - p)
                src = ib + 2 * (p - pre)
            assert p + bs <= cap, "output buffer"
            uc.mem_write(ia, struct.pack("<I", src))
            uc.mem_write(oa, struct.pack("<I", ob + 2 * p))
            cnt = img.call(code_base + proc_off, (ia, oa, bs, 1))
            cnt = struct.unpack("<i", struct.pack("<I", cnt))[0]
            if cnt <= 0:
                break
            p += cnt
        out = np.frombuffer(bytes(uc.mem_read(ob, p * 2)), "<i2")
        res += out[:p].tobytes()
        log.append("run%d pre=%d app=%d done=%d" % (cfg, pre, app, p))
    return bytes(res), log, name.split(b"\0")[0].decode()


def main():
    axf, card, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    tc = os.environ["TOOLCHAIN"]
    ins, pin = make_inputs(out)
    summ = []
    md = os.path.join(card, "Plugins", "Master")
    for f in sorted(os.listdir(md)):
        if not f.endswith(".plg"):
            continue
        img = Img(axf, tc)
        sh = img.sym("plug::(anonymous namespace)::s_host")
        img.service(img.u32(sh + 12 + 4 * 9), img.alloc)
        img.service(img.u32(sh + 12 + 4 * 10), lambda u: u.reg_write(UC_ARM_REG_R0, 0))
        img.service(img.u32(sh + 12 + 4 * 11), lambda u: u.reg_write(UC_ARM_REG_R0, 0))
        name = f[:-4]
        o, q, np_, tempo, tail = run_insert(img, os.path.join(md, f), ins)
        open(os.path.join(out, "ref_ins_%s.f32" % name), "wb").write(o.tobytes())
        open(os.path.join(out, "ref_ins_%s.q" % name), "wb").write(struct.pack("<%dI" % len(q), *q))
        summ.append("ins %-8s params %2d tempo %d tail %d fnv %08x peak %.4f" % (name, np_, tempo, tail, fnv(o.tobytes()), float(np.abs(o).max())))
        print(summ[-1], flush=True)
    ed = os.path.join(card, "Effects")
    img = Img(axf, tc)
    img.extra = []
    img.service(img.sym("hostSetExtraSamples"), lambda u: img.extra.append((u.reg_read(UC_ARM_REG_R0), u.reg_read(UC_ARM_REG_R1))))
    for f in sorted(os.listdir(ed)):
        if not f.endswith(".ptfx"):
            continue
        res, log, disp = run_ptfx(img, os.path.join(ed, f), pin)
        name = f[:-5]
        open(os.path.join(out, "ref_ptfx_%s.i16" % name), "wb").write(res)
        summ.append("ptfx %-14s %-12s %d B fnv %08x %s" % (name, disp, len(res), fnv(res), " ".join(log)))
        print(summ[-1], flush=True)
    open(os.path.join(out, "ref_summary.txt"), "w").write("\n".join(summ) + "\n")


main()
