#!/usr/bin/env python3
# Kernel emu of the USB audio block and packet paths of a linked image (tracker-ff, host only, NOT firmware).
#
#   /tmp/phzvenv/bin/python -I usb_mc_kernel_emu.py IMAGE.axf TOOLCHAIN_PREFIX PERFMODEL_DIR [--blocks N] [--ring-md5]
#
# Loads the axf into Unicorn (Thumb-2, decoded with the toolchain's objdump, the cost table of synth-open/tools/phz_cycles/run_bench.py: no wait states, no cache, loads and
# stores 2 cycles, taken branches 3), calls the real functions of the image and counts cycles per call:
#   block side  ffUsbAudioMultiBlock (8+3: buffer, interleaveMulti with the synth delay, commit) and ffUsbAudioBlock (stereo, today)
#   USB side    usb_audio_transmit_callback (packet(): PI loop and the memcpy of one packet from the SDRAM ring), 8+3 and stereo
# SDRAM accesses are classified with the perfmodel stride meter (PERFMODEL_DIR/stride.py) and the cycles go through devcost.py (class
# int16) for an E(emu+devcost) figure next to the raw E(kernel emu, uncalibrated) one. Kernel only: these exclude the rest of the timed
# window (the tap's receive/release, update_all) and, for the USB side, the interrupt entry and the BDT handling in usb_isr.
import os
import struct
import subprocess
import sys

from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB, Uc
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP

AXF, TC, PM = sys.argv[1], sys.argv[2], sys.argv[3]
BLOCKS = int(sys.argv[sys.argv.index("--blocks") + 1]) if "--blocks" in sys.argv else 64
sys.path.insert(0, PM)
import devcost  # noqa: E402
from stride import SdramMeter  # noqa: E402

US = 180.0
BLOCK_US = 128 / 44117.647 * 1e6
TRAP = 0x0FF00000

# ---- the image
syms = {}
for line in subprocess.run([TC + "nm", "-C", AXF], capture_output=True, text=True, check=True).stdout.splitlines():
    p = line.split(None, 2)
    if len(p) == 3:
        syms[p[2]] = int(p[0], 16)


def sym(name):
    hits = [k for k in syms if k == name or k.endswith("::" + name)]
    if len(hits) != 1:
        sys.exit("symbol %s: %s" % (name, hits))
    return syms[hits[0]]


uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
for base, size in ((0x0, 0x100000), (TRAP, 0x1000), (0x1FFF0000, 0x40000), (0x70000000, 0x1000000), (0xE0001000, 0x1000)):
    uc.mem_map(base, size)
raw = open(AXF, "rb").read()
phoff, phentsize, phnum = struct.unpack_from("<I", raw, 28)[0], struct.unpack_from("<H", raw, 42)[0], struct.unpack_from("<H", raw, 44)[0]
for i in range(phnum):
    typ, off, vaddr, paddr, filesz, memsz = struct.unpack_from("<IIIIII", raw, phoff + i * phentsize)
    if typ == 1 and filesz:
        uc.mem_write(vaddr, raw[off:off + filesz])

_dec = {}


def decode(addr):
    """(mnemonic, operands) of the instruction at addr, from objdump of the 1 KB around it (cached)."""
    if addr not in _dec:
        out = subprocess.run([TC + "objdump", "-d", "--no-show-raw-insn", "--start-address=0x%x" % addr, "--stop-address=0x%x" % (addr + 0x400), AXF],
                             capture_output=True, text=True, check=True).stdout
        for line in out.splitlines():
            parts = line.strip().split(None, 2)
            if len(parts) >= 2 and parts[0].endswith(":"):
                try:
                    a = int(parts[0][:-1], 16)
                except ValueError:
                    continue
                if a not in _dec:
                    _dec[a] = (parts[1], parts[2].split(";")[0].strip() if len(parts) > 2 else "")
        _dec.setdefault(addr, ("nop", ""))
    return _dec[addr]
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../../synth-open/tools/phz_cycles"))


def cost(mn, ops, taken):   # run_bench.py cost(), the shared table (copied: run_bench.py runs its bench at import)
    base = mn.split(".")[0]
    if base.startswith("b") and base not in ("bic", "bics", "bfc", "bfi", "bkpt", "bxj", "bl", "blx", "bx") or base in ("cbz", "cbnz"):
        return 3 if taken else 1
    if base in ("bl", "blx", "bx"):
        return 3
    if base in ("ldr", "ldrb", "ldrh", "ldrsb", "ldrsh", "ldrd", "str", "strb", "strh", "strd", "vldr", "vstr"):
        return 2
    if base in ("push", "pop", "ldm", "ldmia", "stm", "stmia", "stmdb", "ldmdb"):
        return 1 + ops.count(",") + 1 + (2 if "pc" in ops else 0)
    if base in ("sdiv", "udiv"):
        return 6
    if base in ("mla", "mls", "smlal", "umlal"):
        return 2
    return 1


state = {"cyc": 0, "prev": None, "key": "-"}
meter = SdramMeter(lambda: state["key"], heads=(8,))
meter.attach(uc)


def on_code(u, addr, size, _):
    prev = state["prev"]
    if prev is not None:
        pa, ps, pm, po = prev
        state["cyc"] += cost(pm, po, addr != pa + ps)
    mn, op = decode(addr)
    state["prev"] = (addr, size, mn, op)


uc.hook_add(UC_HOOK_CODE, on_code)


def call(fn, key, *args):
    """Calls fn(args) in the image, returns its cycles (the last instruction, the return, counted as taken)."""
    state.update(cyc=0, prev=None, key=key)
    regs = (UC_ARM_REG_R0, UC_ARM_REG_R1)
    for r, v in zip(regs, args):
        uc.reg_write(r, v)
    uc.reg_write(UC_ARM_REG_SP, 0x20030000 - 0x400)
    uc.reg_write(UC_ARM_REG_LR, TRAP | 1)
    uc.emu_start(fn | 1, TRAP)
    pa, ps, pm, po = state["prev"]
    state["cyc"] += cost(pm, po, True)
    return state["cyc"], uc.reg_read(UC_ARM_REG_R0)


IN = 0x2002A000            # 14 input blocks of 128 int16 (SRAM, as the audio blocks are)
PTRS = IN + 14 * 256       # the pointer array the tap hands over
PKT = sym("usb_audio_transmit_buffer")
L, R = IN, IN + 256


def fill_inputs(live):
    import math
    for i in range(14):
        data = struct.pack("<128h", *[int(8000 * math.sin(0.01 * (i + 1) * f)) for f in range(128)])
        uc.mem_write(IN + i * 256, data)
        uc.mem_write(PTRS + 4 * i, struct.pack("<I", IN + i * 256 if live[i] else 0))


def run(channels, live, label):
    uc.mem_write(sym("ff_usb_audio_on"), b"\x01")
    uc.mem_write(sym("ff_usb_audio_channels"), bytes([channels]))
    call(sym("ffUsbAudioStart"), "start")
    fill_inputs(live)
    multi = channels == 11
    blk_fn = sym("ffUsbAudioMultiBlock") if multi else sym("ffUsbAudioBlock")
    blk_args = (PTRS,) if multi else (L, R)
    cb = sym("usb_audio_transmit_callback")
    bc, pc, pbytes = [], [], []
    acc = 0.0
    warm = 24
    for b in range(warm + BLOCKS):
        c, _ = call(blk_fn, "blk%d" % (b >= warm), *blk_args)
        if b >= warm:
            bc.append(c)
        acc += 128 / 44.117647   # packets per block at the codec rate
        while acc >= 1.0:
            acc -= 1.0
            c, n = call(cb, "pkt%d" % (b >= warm), PKT)
            if b >= warm:
                pc.append(c)
                pbytes.append(n)
    blocks = len(bc)
    pk = len(pc)
    bavg, pavg = sum(bc) / blocks, sum(pc) / pk
    per_block_pkt = sum(pc) / blocks
    st_b, sc_b = meter.stream[8]["blk1"] / blocks, meter.scatter[8]["blk1"] / blocks
    st_p, sc_p = meter.stream[8]["pkt1"] / blocks, meter.scatter[8]["pkt1"] / blocks
    model = devcost.load_model()
    db = model.predict("int16", bavg, st_b, sc_b)
    dp = model.predict("int16", per_block_pkt, st_p, sc_p)
    print("%-30s block %6.0f cyc = %5.1f us (%4.2f %%)  E(kernel emu, uncalibrated) | SDRAM stream %5.0f scatter %4.0f | devcost %5.1f us [%5.1f..%5.1f]"
          % (label, bavg, bavg / US, 100 * bavg / US / BLOCK_US, st_b, sc_b, db[0] / US, db[1] / US, db[2] / US))
    print("%-30s packets/block %.2f, %5.0f cyc each, %4.0f B; per block %5.0f cyc = %5.1f us (%4.2f %%) | SDRAM stream %4.0f scatter %3.0f | devcost %5.1f us [%5.1f..%5.1f]"
          % ("", pk / blocks, pavg, sum(pbytes) / pk, per_block_pkt, per_block_pkt / US, 100 * per_block_pkt / US / BLOCK_US, st_p, sc_p,
             dp[0] / US, dp[1] / US, dp[2] / US))
    meter.stream[8].clear(); meter.scatter[8].clear()
    return bavg + per_block_pkt, db[0] + dp[0]


# --ring-md5: bit exactness of the 8+3 block side. 88 blocks of seeded random input (full range samples, 20 % of the inputs null),
# a packet callback between the blocks, then the md5 of the ring. The value is the one of the channel by channel kernel of 3a2e2f3
# and of every kernel since (the pair kernel, the SRAM delay line): any change that is not bit exact moves it.
RING_MD5 = "085e6002dd13021b1b3925b54885f0b5"
if "--ring-md5" in sys.argv:
    import hashlib
    import random

    random.seed(11)
    uc.mem_write(sym("ff_usb_audio_on"), b"\x01")
    uc.mem_write(sym("ff_usb_audio_channels"), bytes([11]))
    call(sym("ffUsbAudioStart"), "start")
    gout = [a for k, a in syms.items() if k.endswith("g_out")]
    assert len(gout) == 1, gout
    for b in range(88):
        for i in range(14):
            data = struct.pack("<128h", *[random.choice((-32768, 32767, random.randint(-32768, 32767), random.randint(-300, 300))) for f in range(128)])
            uc.mem_write(IN + i * 256, data)
            uc.mem_write(PTRS + 4 * i, struct.pack("<I", IN + i * 256 if random.random() > 0.2 else 0))
        call(sym("ffUsbAudioMultiBlock"), "blk1", PTRS)
        for _ in range(3 if b % 7 else 2):
            call(sym("usb_audio_transmit_callback"), "pkt1", PKT)
    md5 = hashlib.md5(bytes(uc.mem_read(gout[0], 11 * 512 * 2))).hexdigest()
    print("ring md5 %s %s" % (md5, "ok" if md5 == RING_MD5 else "MISMATCH (expected %s)" % RING_MD5))
    sys.exit(0 if md5 == RING_MD5 else 1)

print("image %s, %d blocks after 24 warm up, Unicorn + run_bench cost table, perfmodel %s" % (os.path.basename(AXF), BLOCKS, PM))
m = devcost.load_model()
import json  # noqa: E402
fit = json.load(open(os.path.join(PM, "perf.json"))).get("fit", {})
print("devcost class int16: %s, groups in perf.json %s" % (m.label("int16"), fit.get("groups_per_class", {}).get("int16", "?")))
s_tot, s_dev = run(2, [True] * 14, "stereo (today)")
m_tot, m_dev = run(11, [True] * 14, "8+3, all 14 inputs live")
i_tot, i_dev = run(11, [True, True, True, False, False, False, False, False, True, False, False, True, False, False], "8+3, 3 tracks + 1 part live")
print("total per block, block + packets: stereo %.1f us, 8+3 live %.1f us (+%.1f), 8+3 sparse %.1f us (+%.1f)  E(kernel emu, uncalibrated)"
      % (s_tot / US, m_tot / US, (m_tot - s_tot) / US, i_tot / US, (i_tot - s_tot) / US))
print("total per block with devcost: stereo %.1f us, 8+3 live %.1f us (+%.1f), 8+3 sparse %.1f us (+%.1f)"
      % (s_dev / US, m_dev / US, (m_dev - s_dev) / US, i_dev / US, (i_dev - s_dev) / US))
