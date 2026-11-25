#!/usr/bin/env python3
"""Pack an extension module (core/include/ext_abi.h) into a plugin file of kind EXT, bound to one firmware image.

    pack_ext.py --elf module.elf --axf PolyendTracker.axf --ptf PolyendTracker_EXT_1.2.0.ptf --out module.plg [--name NAME]
                [--region console|rel] [--slot-symbol SYM] [--base-offset N]

--region console (default) is the console module's slot s_ext (EXT_SLOT_BYTES with its EXT_NOINIT_BYTES reset surviving tail), --region rel the
release region s_extRel of extras.plg (EXT_REL_BYTES, no tail). --slot-symbol names another image symbol, --base-offset N links the module at
symbol + N: the load part is then what is left of the region after N. The image's loader runs only a module at offset 0 of each region
(extras.plg is one bundle), the offset is for the tools.

--region rel writes the sub-slot table (ext_abi.h ext_rel_table_t) after the manifest, inside header_size: one entry per tenant of
SUB_NAMES, read from the elf's __sub_<name>_start / _end symbols (synth-open/modit/extras.ld places them back to back at EXT_SUB_ALIGN
boundaries). Each sub-slot runs to the next one's start, the last one to its end rounded up to EXT_SUB_ALIGN, and the whole table has
to fit EXT_REL_BYTES. The image's loader refuses an extras.plg without it.

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
REL_SYMBOL = "_ZN12_GLOBAL__N_18s_extRelE"   # synth-open/ext_module.cpp s_extRel
SUB_NAMES = ("modit", "theme", "selftest", "takefile", "capture")   # ext_abi.h EXT_SUB_MODIT .. EXT_SUB_CAPTURE, in that order


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
    return val("EXT_SLOT_BYTES"), val("EXT_NOINIT_BYTES"), val("EXT_PLUG_KIND"), val("EXT_REL_BYTES")


def rel_table_consts():
    text = open(ABI_H).read()
    def val(name):
        m = re.search(r"#define %s (0x[0-9A-Fa-f]+|[0-9]+)u" % name, text)
        return int(m.group(1), 0)
    if val("EXT_SUB_COUNT") != len(SUB_NAMES):
        sys.exit("ext_abi.h EXT_SUB_COUNT %d, pack_ext.py knows %d tenants" % (val("EXT_SUB_COUNT"), len(SUB_NAMES)))
    return val("EXT_REL_TABLE_MAGIC"), val("EXT_REL_TABLE_VERSION"), val("EXT_SUB_ALIGN")


def rel_table(m, base, bss_end, region):
    """The sub-slot table of extras.plg from the linked elf: [(name, at, bytes)], used bytes."""
    _, _, align = rel_table_consts()
    starts = []
    for n in SUB_NAMES:
        for e in ("start", "end"):
            if "__sub_%s_%s" % (n, e) not in m:
                sys.exit("%s has no __sub_%s_%s (link with synth-open/modit/extras.ld)" % ("the elf", n, e))
        starts.append((n, m["__sub_%s_start" % n] - base, m["__sub_%s_end" % n] - base))
    rows = []
    for i, (n, at, end) in enumerate(starts):
        if at % align or (i == 0 and at != 0) or end < at:
            sys.exit("sub-slot %s at +0x%x (end +0x%x): not at a %d B boundary, or the first one not at 0" % (n, at, end, align))
        nxt = starts[i + 1][1] if i + 1 < len(starts) else (max(end, bss_end) + align - 1) // align * align
        if nxt < end:
            sys.exit("sub-slot %s ends at +0x%x past the next one's start +0x%x" % (n, end, nxt))
        rows.append((n, at, nxt - at, end - at))
    used = rows[-1][1] + rows[-1][2]
    if used > region:
        sys.exit("the tenants need %d B, the release region has %d B (ext_abi.h EXT_REL_BYTES): grow it" % (used, region))
    return rows, used


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
    ap.add_argument("--axf", help="the firmware image the module was linked against")
    ap.add_argument("--ptf", help="that image's .ptf (what the bootloader programs)")
    ap.add_argument("--imx-image-id", help="Tracker+ / Mini (ext_imx_abi.h): a CM7 module bound to this image id (0x...) instead of an axf "
                    "and its .ptf, the run address is the module's own __ext_base (synth-open/ext_imx.cpp, the plus sim answers 0x494D5831)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", default="ext module")
    ap.add_argument("--author", default="")
    ap.add_argument("--licence", default="none")
    ap.add_argument("--version", default="0.1.0")
    ap.add_argument("--allow-short-tail", action="store_true",
                    help="bind to an image whose last 4 KB sector holds fewer than 0x800 B (the unit may leave that tail erased)")
    ap.add_argument("--region", choices=("console", "rel"), default="console")
    ap.add_argument("--slot-symbol", default=None, help="the image symbol of the region (default: by --region)")
    ap.add_argument("--base-offset", type=lambda v: int(v, 0), default=0, help="link offset inside the region (a sub-slot)")
    ap.add_argument("--toolchain", default=os.environ.get("TOOLCHAIN", "arm-none-eabi-"))
    a = ap.parse_args()
    tc = a.toolchain
    slot_bytes, noinit_bytes, kind, rel_bytes = abi_consts()
    sym = a.slot_symbol or (REL_SYMBOL if a.region == "rel" else SLOT_SYMBOL)
    if a.region == "rel":
        slot_bytes, noinit_bytes = rel_bytes, 0
    if a.base_offset % 16 or not 0 <= a.base_offset < slot_bytes - noinit_bytes:
        sys.exit("base offset 0x%x: not 16 B aligned or outside the %d B load part" % (a.base_offset, slot_bytes - noinit_bytes))
    m = syms(tc + "nm", a.elf)
    imx = a.imx_image_id is not None
    if not imx and not (a.axf and a.ptf):
        sys.exit("--axf and --ptf, or --imx-image-id")
    img = {} if imx else syms(tc + "nm", a.axf)
    for s in ("__ext_base", "__rw_start", "__image_end", "__bss_end", "__noinit_start", "ext_entry"):
        if s not in m:
            sys.exit("%s has no %s (link with the module.ld of the console repository)" % (a.elf, s))
    base = m["__ext_base"]
    if not imx and (sym not in img or img[sym] + a.base_offset != base):
        sys.exit("module linked at 0x%x, the image's %s region + 0x%x is at %s" % (
            base, a.region, a.base_offset, hex(img[sym] + a.base_offset) if sym in img else "? (no %s in the image)" % sym))
    rw, end, bss_end, noinit = (m[s] - base for s in ("__rw_start", "__image_end", "__bss_end", "__noinit_start"))
    entry = (m["ext_entry"] & ~1) - base
    load_bytes = slot_bytes - noinit_bytes - a.base_offset
    if rw % 8 or end % 4 or not 0 <= entry < rw <= end <= bss_end:
        sys.exit("module layout: entry 0x%x rw 0x%x end 0x%x bss end 0x%x" % (entry, rw, end, bss_end))
    if bss_end > load_bytes or noinit != load_bytes:
        sys.exit("module needs %d B of the %d B load part, its noinit starts at +%d (want +%d)" % (bss_end, load_bytes, noinit, load_bytes))
    binf = a.out + ".bin.tmp"
    # .theme, .selftest, .takefile, .cap: the tenants of extras.plg after the MOD/IT one (extras.ld)
    run([tc + "objcopy", "-O", "binary", "-j", ".text", "-j", ".data", "-j", ".theme", "-j", ".selftest", "-j", ".takefile", "-j", ".cap", a.elf, binf])
    image = open(binf, "rb").read()
    os.remove(binf)
    image += b"\0" * (end - len(image))
    if len(image) != end:
        sys.exit("binary is %d B, the image %d B" % (len(image), end))
    if imx:
        image_crc = int(a.imx_image_id, 0) & 0xFFFFFFFF
    else:
        if "_image_end" not in img:
            sys.exit("%s has no _image_end" % a.axf)
        tail = img["_image_end"] % 4096
        if 0 < tail < 0x800 and not a.allow_short_tail:
            sys.exit("the image ends %d B into its last 4 KB sector (0x%x). Round 6 of the paraphony device tests: the unit left such a short "
                     "tail unprogrammed, the module was refused by the image CRC. Grow the image by "
                     "%d B (make original sizes FF_TAIL_PAD, synth-open/tools/tail_pad.py) or pass --allow-short-tail" % (tail, img["_image_end"] - tail, 0x800 - tail))
        flash = ihex_flash(a.ptf, img["_image_end"])
        image_crc = zlib.crc32(flash) & 0xFFFFFFFF
        # the device sums [0, _etext) and [_etext, _etext + .data size): the same range when .data loads at _etext
        if img["_etext"] + img["_edata"] - img["_sdata"] != img["_image_end"]:
            sys.exit("_image_end is not _etext + .data size: ext_module.cpp imageCrc() sums another range")
    manifest = struct.pack("<I16s32s16sHBBIIIII4x", 96, a.name.encode()[:15], a.author.encode()[:31], a.licence.encode()[:15],
                           0, 0, 0, 0, 0, 0, 0, 0)
    table = b""
    if a.region == "rel":
        rows, used = rel_table(m, base, bss_end, rel_bytes)
        magic, version, _ = rel_table_consts()
        table = struct.pack("<IHHII", magic, version, len(rows), rel_bytes, used) + b"".join(struct.pack("<II", at, n) for _, at, n, _ in rows)
        assert len(table) == 16 + 8 * len(SUB_NAMES)
        print("sub-slot table (version %d), %d of %d B of the region:" % (version, used, rel_bytes))
        for n, at, nb, real in rows:
            print("  %-9s +%6d  %6d B  (%d B used)" % (n, at, nb, real))
    payload = manifest + table + image
    maj, mi, pa = (int(x) for x in a.version.split("."))
    uid = 0x811C9DC5
    for c in ("ext." + a.name).encode():
        uid = ((uid ^ c) * 0x01000193) & 0xFFFFFFFF
    hdr = struct.pack("<IHBBBBH" + "I" * 13, 0x31474C50, 64 + 96 + len(table), 1, 1, kind, 2 if imx else 1, 0, uid, (maj << 16) | (mi << 8) | pa, 1, end,
                      bss_end - end, 0, entry, bss_end, 0, zlib.crc32(payload) & 0xFFFFFFFF, rw, image_crc, base)
    assert len(hdr) == 64
    with open(a.out, "wb") as f:
        f.write(hdr + payload)
    print("%s: image %d B (text %d, data %d), bss %d B, load part %d of %d B, noinit tail %d B, entry +0x%x, file %d B" % (
        a.out, end, rw, end - rw, bss_end - end, bss_end, load_bytes, noinit_bytes, entry, 64 + len(payload)))
    if imx:
        print("bound to the IMX image id 0x%08x (CM7), slot 0x%08x" % (image_crc, base))
        print("EXT_MODULE=%s 0x%08x 0x%08x" % (a.out, image_crc, base))
        return
    print("bound to image %s: crc 0x%08x over [0, 0x%x), slot 0x%08x" % (os.path.basename(a.ptf), image_crc, img["_image_end"], base))
    head_end = img["_image_end"] & ~0xFFF   # the pieces the unit logs on a refusal (status.txt): the Mac's numbers to compare
    print("  head [0, 0x%x) crc 0x%08x, last 16 B %s" % (head_end, zlib.crc32(flash[:head_end]) & 0xFFFFFFFF, flash[-16:].hex(" ")))
    print("EXT_MODULE=%s 0x%08x 0x%08x" % (a.out, image_crc, base))


if __name__ == "__main__":
    main()
