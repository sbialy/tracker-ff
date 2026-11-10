#!/usr/bin/env python3
"""Pack an extension module (core/include/ext_abi.h) into a plugin file of kind EXT, bound to one firmware image.

    pack_ext.py --elf module.elf --axf PolyendTracker.axf --ptf PolyendTracker_EXT_1.2.0.ptf --out module.plg [--name NAME]

module.elf is linked at the image's SDRAM slot (ld --just-symbols=the axf, a fixed address, no relocations left) with the
symbols __ext_base, __rw_start, __image_end, __bss_end and __noinit_start, and the entry symbol ext_entry. The file:
  header      PLUG_MAGIC, kind 6, rebase_count 0, rw_offset, entry_offset, CRC-32 of everything after the 64 bytes,
              reserved[0] = CRC-32 of the image's flash [0, _image_end) as the bootloader programs it (the .ptf, 0xFF gaps),
              reserved[1] = the run address (__ext_base, must be the axf's slot)
  manifest    96 bytes (name, author, licence), then the image (text, data)
Prints one line "EXT_MODULE=<out> <image crc> <run address>" (the arguments of tools/ext_loader_test.cpp).
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ABI_H = os.path.join(HERE, "..", "..", "core", "include", "ext_abi.h")
SLOT_SYMBOL = "_ZN12_GLOBAL__N_15s_extE"   # synth-open/ext_module.cpp s_ext


def run(cmd):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode:
        sys.exit("%s failed: %s" % (" ".join(cmd), p.stderr.decode()))
    return p.stdout.decode()


def syms(nm, elf):
    out = {}
    for line in run([nm, elf]).splitlines():
        p = line.split()
        if len(p) == 3:
            out[p[2]] = int(p[0], 16)
    return out


def abi_consts():
    text = open(ABI_H).read()
    def val(name):
        m = re.search(r"#define %s \(?([0-9]+)u?(?: \* ([0-9]+)u)?\)?" % name, text)
        return int(m.group(1)) * (int(m.group(2)) if m.group(2) else 1)
    return val("EXT_SLOT_BYTES"), val("EXT_NOINIT_BYTES"), val("EXT_PLUG_KIND")


def ihex_flash(path, end):
    """The flash bytes [0, end) of an Intel HEX file, 0xFF where it has none (erased flash)."""
    mem = bytearray(b"\xff" * end)
    upper, top = 0, 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(":"):
            continue
        rec = bytes.fromhex(line[1:])
        if sum(rec) & 0xFF:
            sys.exit("%s: bad record checksum" % path)
        n, addr, typ, data = rec[0], (rec[1] << 8) | rec[2], rec[3], rec[4:4 + rec[0]]
        if typ == 0:
            a = upper + addr
            if a + n > end:
                sys.exit("%s: data at 0x%x past the image end 0x%x (ptf and axf differ?)" % (path, a, end))
            mem[a:a + n] = data
            top = max(top, a + n)
        elif typ == 4:
            upper = ((data[0] << 8) | data[1]) << 16
        elif typ == 2:
            upper = ((data[0] << 8) | data[1]) << 4
    if top != end:
        sys.exit("%s ends at 0x%x, the axf image at 0x%x: not the same build" % (path, top, end))
    return bytes(mem)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True)
    ap.add_argument("--axf", required=True, help="the firmware image the module was linked against")
    ap.add_argument("--ptf", required=True, help="that image's .ptf (what the bootloader programs)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", default="ext module")
    ap.add_argument("--author", default="")
    ap.add_argument("--licence", default="none")
    ap.add_argument("--version", default="0.1.0")
    ap.add_argument("--toolchain", default=os.environ.get("TOOLCHAIN", "arm-none-eabi-"))
    a = ap.parse_args()
    tc = a.toolchain
    slot_bytes, noinit_bytes, kind = abi_consts()
    m = syms(tc + "nm", a.elf)
    img = syms(tc + "nm", a.axf)
    for s in ("__ext_base", "__rw_start", "__image_end", "__bss_end", "__noinit_start", "ext_entry"):
        if s not in m:
            sys.exit("%s has no %s (link with the module.ld of poly-devtools)" % (a.elf, s))
    base = m["__ext_base"]
    if img.get(SLOT_SYMBOL) != base:
        sys.exit("module linked at 0x%x, the image's slot is at %s" % (base, hex(img[SLOT_SYMBOL]) if SLOT_SYMBOL in img else "?"))
    rw, end, bss_end, noinit = (m[s] - base for s in ("__rw_start", "__image_end", "__bss_end", "__noinit_start"))
    entry = (m["ext_entry"] & ~1) - base
    load_bytes = slot_bytes - noinit_bytes
    if rw % 8 or end % 4 or not 0 <= entry < rw <= end <= bss_end:
        sys.exit("module layout: entry 0x%x rw 0x%x end 0x%x bss end 0x%x" % (entry, rw, end, bss_end))
    if bss_end > load_bytes or noinit != load_bytes:
        sys.exit("module needs %d B of the %d B load part, its noinit starts at +%d (want +%d)" % (bss_end, load_bytes, noinit, load_bytes))
    binf = a.out + ".bin.tmp"
    run([tc + "objcopy", "-O", "binary", "-j", ".text", "-j", ".data", a.elf, binf])
    image = open(binf, "rb").read()
    os.remove(binf)
    image += b"\0" * (end - len(image))
    if len(image) != end:
        sys.exit("binary is %d B, the image %d B" % (len(image), end))
    if "_image_end" not in img:
        sys.exit("%s has no _image_end" % a.axf)
    flash = ihex_flash(a.ptf, img["_image_end"])
    image_crc = zlib.crc32(flash) & 0xFFFFFFFF
    # the device sums [0, _etext) and [_etext, _etext + .data size): the same range when .data loads at _etext
    if img["_etext"] + img["_edata"] - img["_sdata"] != img["_image_end"]:
        sys.exit("_image_end is not _etext + .data size: ext_module.cpp imageCrc() sums another range")
    manifest = struct.pack("<I16s32s16sHBBIIIII4x", 96, a.name.encode()[:15], a.author.encode()[:31], a.licence.encode()[:15],
                           0, 0, 0, 0, 0, 0, 0, 0)
    payload = manifest + image
    maj, mi, pa = (int(x) for x in a.version.split("."))
    uid = 0x811C9DC5
    for c in ("ext." + a.name).encode():
        uid = ((uid ^ c) * 0x01000193) & 0xFFFFFFFF
    hdr = struct.pack("<IHBBBBH" + "I" * 13, 0x31474C50, 64 + 96, 1, 1, kind, 1, 0, uid, (maj << 16) | (mi << 8) | pa, 1, end,
                      bss_end - end, 0, entry, bss_end, 0, zlib.crc32(payload) & 0xFFFFFFFF, rw, image_crc, base)
    assert len(hdr) == 64
    with open(a.out, "wb") as f:
        f.write(hdr + payload)
    print("%s: image %d B (text %d, data %d), bss %d B, load part %d of %d B, noinit tail %d B, entry +0x%x, file %d B" % (
        a.out, end, rw, end - rw, bss_end - end, bss_end, load_bytes, noinit_bytes, entry, 64 + len(payload)))
    print("bound to image %s: crc 0x%08x over [0, 0x%x), slot 0x%08x" % (os.path.basename(a.ptf), image_crc, img["_image_end"], base))
    print("EXT_MODULE=%s 0x%08x 0x%08x" % (a.out, image_crc, base))


if __name__ == "__main__":
    main()
