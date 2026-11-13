#!/usr/bin/env python3
"""Build a Tracker FF runtime plugin (.plg, ABI 1).

    build_plug.py --id ff.dirt --kind master --out dirt.plg src1.cpp src2.cpp ... [-I dir] [--opt "-O2 -ffp-contract=off"]

Compile for the Cortex-M4F, link at address 0 with plug.ld and --emit-relocs, then:
  - refuse static constructors (.init_array), GOT access and any absolute relocation other than R_ARM_ABS32
  - write every R_ARM_ABS32 target word offset into the rebase table (the loader adds the run address)
  - read name, author and num_params from the descriptor (PLUG_DESCRIPTOR, symbol __plug_desc) into the 96 byte manifest
  - prepend the 64 byte plug_file_header and the manifest, CRC-32 over manifest, image and table.
--logo x.plgi appends a logo record (synth-open/plug_logo.h, made by tools/make_plgi.py) after the rebase table and puts its
file offset in plug_manifest.reserved[0]. The record has its own CRC, the file CRC does not cover it.
Per source flags (--src-opt NAME=FLAGS): a port of firmware DSP is only bit identical when its objects are built with the
firmware object's own flags (a fused vfma would otherwise differ from the firmware build).
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
ROLES = {"insert": 0, "reverb": 1, "delay": 2}
MANIFEST_SIZE = 96
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
    if os.environ.get("TOOLCHAIN"):   # same variable as make: TOOLCHAIN=/path/agt/10.3/bin/arm-none-eabi-
        return os.environ["TOOLCHAIN"]
    for c in [os.path.join(HERE, "..", "..", "agt", "10.3", "bin", "arm-none-eabi-"), "arm-none-eabi-"]:
        if os.path.exists(c + "gcc") or c == "arm-none-eabi-":
            return os.path.abspath(c) if os.path.exists(c + "gcc") else c
    return "arm-none-eabi-"


def run(cmd):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode != 0:
        sys.exit("FAILED: " + " ".join(cmd) + "\n" + r.stderr.decode())
    if r.stderr:   # compiler warnings (-Wall -Wextra) of a build that succeeded
        sys.stderr.write(r.stderr.decode())
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
    ap.add_argument("--licence", default="MIT", help="SPDX id for the manifest")
    ap.add_argument("--role", default="insert", choices=ROLES)
    ap.add_argument("--synth", help="STORED,PATCH_ENGINE,TYPE_ID of a synth engine (decimal or 0x hex) into the manifest, so a "
                    "host lists the engine before it is installed (plug_abi.h PLUG_SYNTH_MANIFEST)")
    ap.add_argument("--logo", help="a .plgi logo record to append as a trailer (synth engines, plug_logo.h)")
    ap.add_argument("--zero-ctors", dest="zero_ctors", action="append", default=[], metavar="NAME",
                    help="the source with basename NAME has a static initializer that only stores zero into zero-initialised .bss "
                    "(CHIP's SDK voice base: a static_vector's size, the plugin's bss is zeroed at attach anyway): drop its .init_array. "
                    "The caller vouches for it, the render compare proves it")
    ap.add_argument("--strip-ctors", dest="strip_ctors", action="store_true",
                    help="test references: drop .init_array from prebuilt objects (firmware objects of a render compare)")
    a = ap.parse_args()
    tc = toolchain()
    with tempfile.TemporaryDirectory() as tmp:
        objs = []
        for src in a.sources + [os.path.join(HERE, "plug_rt.c")]:
            if src.endswith(".o"):   # a prebuilt object (e.g. the firmware's own, to run the in-image code in emu_plug.py)
                hdrs = run([tc + "objdump", "-h", src])
                if (".init_array" in hdrs or ".ctors" in hdrs) and a.strip_ctors:
                    # test references only: a firmware object registers its engine's destructor with atexit at static init
                    # (never runs on the device either), the engine code itself is what the comparison needs
                    o = os.path.join(tmp, "noctor-" + os.path.basename(src))
                    run([tc + "objcopy", "--remove-section=.init_array", "--remove-section=.rel.init_array", src, o])
                    src, hdrs = o, run([tc + "objdump", "-h", o])
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
                "-Wall", "-Wextra", "-I" + HERE] + ["-I" + i for i in a.inc]
            if cxx:
                cmd += ["-std=gnu++20", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics", "-fno-use-cxa-atexit"]
            if src.endswith("plug_rt.c") and a.kind == "synth":
                cmd += ["-DPLUG_RT_LIBM11"]   # the ABI 1.1 libm forwards (exp2f and log2f from the host)
            run(cmd + ["-c", src, "-o", obj])
            hdrs = run([tc + "objdump", "-h", obj])
            if os.path.basename(src) in a.zero_ctors and ".init_array" in hdrs:
                run([tc + "objcopy", "--remove-section=.init_array", "--remove-section=.rel.init_array", obj])
                hdrs = run([tc + "objdump", "-h", obj])
            if ".init_array" in hdrs or ".ctors" in hdrs:
                sys.exit("%s has static constructors: not allowed in a plugin (use init())" % src)
            objs.append(obj)
        elf = os.path.join(tmp, "plugin.elf")
        libgcc = run([tc + "gcc"] + CPU_FLAGS + ["-print-libgcc-file-name"]).strip()
        defsym = [] if a.entry == "plug_entry" else ["--defsym=plug_entry=" + a.entry]
        # binutils 2.39+ warns about the RWX segment (the plugin is one RWX blob by design, plug.ld). Older ld (10.3) has no such flag
        ld_help = subprocess.run([tc + "ld", "--help"], stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.decode()
        quiet = ["--no-warn-rwx-segments"] if "--no-warn-rwx-segments" in ld_help else []
        run([tc + "ld", "-T", os.path.join(HERE, "plug.ld"), "--gc-sections", "--emit-relocs"] + quiet + ["-o", elf] + defsym + objs + [libgcc])
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
        # the manifest, from the descriptor (link base 0: its pointers are image offsets)
        if "__plug_desc" not in syms:
            sys.exit("no __plug_desc: use plug.ld")
        dsc = syms["__plug_desc"]
        dsize, dkind, pname, pauthor, nparams = struct.unpack_from("<IIIII", image, dsc)
        if dsize < 44 or dsize > 256 or dkind != KINDS[a.kind] or pname >= image_end:
            sys.exit("descriptor at 0x%x is not a plug_desc of kind %s: mark it PLUG_DESCRIPTOR" % (dsc, a.kind))
        cstr = lambda p: image[p:image.index(b"\0", p)] if p and p < image_end else b""
        name, author = cstr(pname), cstr(pauthor)
        if not 0 < len(name) <= 15 or nparams > 32:
            sys.exit("descriptor name must be 1..15 chars and num_params <= 32")
        logo = b""
        if a.logo:
            logo = open(a.logo, "rb").read()
            magic, ver, fmt, mw, mh, dw, dh, _fl, lcrc = struct.unpack_from("<IBBBBBBHI", logo, 0) if len(logo) >= 16 else (0,) * 9
            if len(logo) != 1676 or magic != 0x49474C50 or ver != 1 or (fmt, mw, mh, dw, dh) != (2, 80, 29, 108, 40) or \
                    zlib.crc32(logo[16:]) & 0xFFFFFFFF != lcrc:
                sys.exit("%s is not a PLGI 1 logo record (make it with tools/make_plgi.py)" % a.logo)
        # the trailer's file offset: after the header, the manifest, the image and the rebase table
        logo_off = 64 + MANIFEST_SIZE + image_end + 4 * len(table) if logo else 0
        syn = (0, 0, 0, 0)
        if a.synth:
            if a.kind != "synth":
                sys.exit("--synth is for --kind synth")
            nums = [int(x, 0) for x in a.synth.split(",")]
            if len(nums) != 3 or min(nums) < 0 or max(nums) > 0x7FFFFFFF:
                sys.exit("--synth wants STORED,PATCH_ENGINE,TYPE_ID")
            syn = (nums[0], nums[1], nums[2], 0x314E5953)   # PLUG_SYNTH_MANIFEST "SYN1"
        manifest = struct.pack("<I16s32s16sHBBIIIII4x", MANIFEST_SIZE, name, author[:31], a.licence.encode()[:15], nparams,
                               ROLES[a.role], 0, logo_off, *syn)
        assert len(manifest) == MANIFEST_SIZE
        payload = manifest + image + b"".join(struct.pack("<I", o) for o in table)
        assert not logo or 64 + len(payload) == logo_off
        maj, mi, pa = (int(x) for x in a.version.split("."))
        hdr = struct.pack("<IHBBBBHIIIIIIIIIII8x", 0x31474C50, 64 + MANIFEST_SIZE, 1, 0, KINDS[a.kind], 1, 0, fnv1a(a.id),
                          (maj << 16) | (mi << 8) | pa, 1, image_end, bss_end - image_end, len(table),
                          syms["plug_entry"], a.ram, a.cycles, zlib.crc32(payload) & 0xFFFFFFFF, syms["__rw_start"])
        assert len(hdr) == 64
        with open(a.out, "wb") as f:
            f.write(hdr + payload + logo)
        if a.keep:
            import shutil
            shutil.copy(elf, a.keep)
        print("%s: %s, %d params, image %d B (text %d, rw %d), bss %d B, %d rebase words, entry 0x%x, uid 0x%08x, file %d B" % (
            a.out, name.decode(), nparams, image_end, syms["__rw_start"], image_end - syms["__rw_start"], bss_end - image_end,
            len(table), syms["plug_entry"], fnv1a(a.id), 64 + len(payload) + len(logo)) + (", logo trailer at %d" % logo_off if logo else "")
            + (", synth numbers %s" % a.synth if a.synth else ""))


if __name__ == "__main__":
    main()
