#!/usr/bin/env python3
"""Build a Tracker FF runtime plugin (.plg, ABI 1, spike).

    build_plug.py --id ff.dirt --kind master --out dirt.plg src1.cpp src2.cpp ... [-I dir] [--opt "-O2 -ffp-contract=off"]

Compile for the Cortex-M4F, link at address 0 with plug.ld and --emit-relocs, then:
  - refuse static constructors (.init_array), GOT access and any absolute relocation other than R_ARM_ABS32
  - write every R_ARM_ABS32 target word offset into the rebase table (the loader adds the run address)
  - prepend the 64 byte plug_file_header with a CRC-32 of image + table.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
KINDS = {"master": 1, "send": 2, "track": 3, "synth": 4, "offline": 5}
CPU_FLAGS = ["-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16"]

# Relocation types ld resolves to PC-relative values (position independent after a flat copy).
PC_RELATIVE = {
    3: "R_ARM_REL32", 10: "R_ARM_THM_CALL", 11: "R_ARM_THM_PC8", 30: "R_ARM_THM_JUMP24", 42: "R_ARM_PREL31",
    51: "R_ARM_THM_JUMP19", 102: "R_ARM_THM_JUMP11", 103: "R_ARM_THM_JUMP8", 47: "R_ARM_THM_MOVW_PREL_NC",
    48: "R_ARM_THM_MOVT_PREL", 53: "R_ARM_THM_ALU_PREL_11_0", 54: "R_ARM_THM_PC12", 0: "R_ARM_NONE",
    40: "R_ARM_V4BX", 25: "R_ARM_BASE_PREL",
}
R_ARM_ABS32 = 2


def toolchain():
    for c in [os.path.join(HERE, "..", "..", "agt", "10.3", "bin", "arm-none-eabi-"), "arm-none-eabi-"]:
        if os.path.exists(c + "gcc") or c == "arm-none-eabi-":
            return os.path.abspath(c) if os.path.exists(c + "gcc") else c
    return "arm-none-eabi-"


def run(cmd):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode != 0:
        sys.exit("FAILED: " + " ".join(cmd) + "\n" + r.stderr.decode())
    return r.stdout.decode()


def fnv1a(s):
    h = 0x811C9DC5
    for b in s.encode():
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def elf_sections(data):
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    secs = []
    for i in range(shnum):
        secs.append(struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize))
    strtab = secs[shstrndx]
    def name(s):
        off = strtab[4] + s[0]
        return data[off:data.index(b"\0", off)].decode()
    return [(name(s),) + s for s in secs]


def rebase_table(elf):
    data = open(elf, "rb").read()
    secs = elf_sections(data)
    symtab = next(s for s in secs if s[2] == 2)   # SHT_SYMTAB
    offsets, bad = [], []
    for s in secs:
        if s[2] != 9:   # SHT_REL
            continue
        target = secs[s[8]]   # sh_info: the section the relocations apply to
        if not (target[3] & 2) or target[0] in (".bss",):   # SHF_ALLOC
            continue
        for k in range(s[6] // 8):
            r_offset, r_info = struct.unpack_from("<II", data, s[5] + k * 8)
            rtype, rsym = r_info & 0xFF, r_info >> 8
            if rtype == R_ARM_ABS32:
                shndx, = struct.unpack_from("<H", data, symtab[5] + rsym * 16 + 14)
                if shndx == 0xFFF1:   # SHN_ABS: a constant, not an address in the image
                    continue
                # --emit-relocs on an executable: r_offset is the address (link base 0) of the word
                offsets.append(r_offset)
            elif rtype not in PC_RELATIVE:
                bad.append((target[0], hex(r_offset), rtype))
            elif rtype not in (0, 40):
                # PC-relative: fine only inside one segment (text may run from flash, rw from RAM)
                shndx, = struct.unpack_from("<H", data, symtab[5] + rsym * 16 + 14)
                if 0 < shndx < len(secs):
                    rw = lambda name: name.startswith((".data", ".bss"))
                    if rw(secs[shndx][0]) != rw(target[0]):
                        bad.append((target[0], hex(r_offset), PC_RELATIVE[rtype] + " across segments"))
    if bad:
        sys.exit("refused relocations (not position independent after a copy): %s" % bad[:10])
    return sorted(set(offsets))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sources", nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--id", required=True, help="reverse-dns id, hashed into the uid")
    ap.add_argument("--kind", default="master", choices=KINDS)
    ap.add_argument("--version", default="0.1.0")
    ap.add_argument("--opt", default="-O2 -ffp-contract=off")
    ap.add_argument("--ram", type=int, default=0)
    ap.add_argument("--cycles", type=int, default=0)
    ap.add_argument("-I", dest="inc", action="append", default=[])
    ap.add_argument("--keep", help="keep the ELF here")
    ap.add_argument("--src-opt", dest="src_opt", action="append", default=[], type=lambda v: tuple(v.split("=", 1)),
                    help="NAME=FLAGS: compile the source with basename NAME with FLAGS instead of --opt")
    ap.add_argument("--entry", default="plug_entry", help="entry symbol (plug_entry_dirt for the firmware object)")
    a = ap.parse_args()
    tc = toolchain()
    with tempfile.TemporaryDirectory() as tmp:
        objs = []
        for src in a.sources + [os.path.join(HERE, "plug_rt.c")]:
            if src.endswith(".o"):   # a prebuilt object (e.g. the firmware's own, to run the in-image code in emu_plug.py)
                hdrs = run([tc + "objdump", "-h", src])
                if ".init_array" in hdrs or ".ctors" in hdrs:
                    sys.exit("%s has static constructors: not allowed in a plugin (use init())" % src)
                objs.append(src)
                continue
            obj = os.path.join(tmp, os.path.basename(src) + ".o")
            cxx = src.endswith((".cpp", ".cc"))
            opt = a.opt
            for pat, o in a.src_opt:   # per source flags: --src-opt ff_dirt_ctl.cpp=-Os (same flags as the firmware object)
                if os.path.basename(src) == pat:
                    opt = o
            cmd = [tc + ("g++" if cxx else "gcc")] + CPU_FLAGS + opt.split() + [
                "-ffunction-sections", "-fdata-sections", "-ffreestanding", "-fno-common", "-fvisibility=hidden",
                "-I" + HERE] + ["-I" + i for i in a.inc]
            if cxx:
                cmd += ["-std=gnu++20", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics", "-fno-use-cxa-atexit"]
            run(cmd + ["-c", src, "-o", obj])
            hdrs = run([tc + "objdump", "-h", obj])
            if ".init_array" in hdrs or ".ctors" in hdrs:
                sys.exit("%s has static constructors: not allowed in a plugin (use init())" % src)
            objs.append(obj)
        elf = os.path.join(tmp, "plugin.elf")
        libgcc = run([tc + "gcc"] + CPU_FLAGS + ["-print-libgcc-file-name"]).strip()
        defsym = [] if a.entry == "plug_entry" else ["--defsym=plug_entry=" + a.entry]
        run([tc + "ld", "-T", os.path.join(HERE, "plug.ld"), "--gc-sections", "--emit-relocs", "-o", elf] + defsym + objs + [libgcc])
        syms = {}
        for line in run([tc + "nm", elf]).splitlines():
            p = line.split()
            if len(p) == 3:
                syms[p[2]] = int(p[0], 16)
        image_end, bss_end = syms["__image_end"], syms["__bss_end"]
        binf = os.path.join(tmp, "plugin.bin")
        run([tc + "objcopy", "-O", "binary", "-j", ".text", "-j", ".data", elf, binf])
        image = open(binf, "rb").read()
        image += b"\0" * (image_end - len(image))
        assert len(image) == image_end and image_end % 4 == 0
        table = rebase_table(elf)
        for off in table:
            assert off % 4 == 0 and off + 4 <= image_end, "rebase word outside the image: %x" % off
            v, = struct.unpack_from("<I", image, off)
            assert v < bss_end, "ABS32 word at %x holds %x, not an image address" % (off, v)
        payload = image + b"".join(struct.pack("<I", o) for o in table)
        maj, mi, pa = (int(x) for x in a.version.split("."))
        hdr = struct.pack("<IHBBBBHIIIIIIIIIII8x", 0x31474C50, 64, 1, 0, KINDS[a.kind], 1, 0, fnv1a(a.id),
                          (maj << 16) | (mi << 8) | pa, 1, image_end, bss_end - image_end, len(table),
                          syms["plug_entry"], a.ram, a.cycles, zlib.crc32(payload) & 0xFFFFFFFF, syms["__rw_start"])
        assert len(hdr) == 64
        with open(a.out, "wb") as f:
            f.write(hdr + payload)
        if a.keep:
            import shutil
            shutil.copy(elf, a.keep)
        print("%s: image %d B (text %d, rw %d), bss %d B, %d rebase words, entry 0x%x, uid 0x%08x, file %d B" % (
            a.out, image_end, syms["__rw_start"], image_end - syms["__rw_start"], bss_end - image_end, len(table), syms["plug_entry"], fnv1a(a.id), 64 + len(payload)))


if __name__ == "__main__":
    main()
