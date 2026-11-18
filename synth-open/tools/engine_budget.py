#!/usr/bin/env python3
"""Flash budget of the SYNTH_OPEN engines (called by engine_budget.sh, make original and make synth-engines).

Budget = the real image: text + data of `arm-none-eabi-size` on the linked .axf, against FLASH_IMG_MAX (0xFF000 =
1,044,480 B). NOT the ld "FLASH" line, which leaves out the .data load image.

Per-engine cost = the sizes of the input sections that survived --gc-sections in the linker map and come from the
engine's own archive members (acd.o, engine_acd.o, ...) or from the engine's icon sections. It does not include the
few table entries and factories in shared files (registry.o, open_cold.o), so it is a little below the marginal cost
of a real build (see the combo table in synth-open/README.md). Alignment padding is not counted either.

Modes
  report  --axf X --map M --engines "A B"   image size, free bytes, the engines of this build (writes --txt)
  table   --axf X --map M --pick "A B"      cost of every engine from an all-engines link, base, what fits
Exit status 1 when the image is over the cap (report).
"""
import argparse
import os
import re
import subprocess
import sys

FLASH_END = 0x100000
# Output sections that are not in the flash image even when their address is low (debug info, notes) or have a load
# address without bytes (.bss)
NOT_IMAGE = (".debug", ".comment", ".ARM.attributes", ".bss", ".igot", ".stab", ".note", ".dmabuffers", ".usbbuffers")


def in_image(name, vma, size, load):
    if name.startswith(NOT_IMAGE) or size == 0:
        return False
    return load is not None or vma < FLASH_END


def size_of(tool, path):
    out = subprocess.run([tool + "size", path], capture_output=True, text=True, check=True).stdout.splitlines()
    f = out[1].split()
    return int(f[0]) + int(f[1])


SDRAM_SIZE = 16 * 1024 * 1024   # the external SDRAM of the MK66 board, 16 MiB: .noinit_RAM4 is what the firmware places in it


def sdram_used(tool, path):
    """bytes of the .noinit_RAM4 section of the image (the SDRAM the linker hands out), 0 when the image has none"""
    for line in subprocess.run([tool + "size", "-A", path], capture_output=True, text=True, check=True).stdout.splitlines():
        f = line.split()
        if len(f) >= 2 and f[0] == ".noinit_RAM4":
            return int(f[1])
    return 0


def parse_list(items):
    d = {}
    for it in items:
        name, val = it.split("=", 1)
        d[name] = val.split()
    return d


def map_costs(path, members, sects):
    """Bytes per engine of flash resident input sections (text, rodata, exidx, data load image)."""
    cost = {e: 0 for e in members}
    in_map = False
    out_flash = False
    pending = None
    num = r"(0x[0-9a-fA-F]+)"
    out_re = re.compile(r"^(\.\S+)\s+" + num + r"\s+" + num + r"(?:\s+load address\s+" + num + r")?")
    out_name_only = re.compile(r"^(\.\S+)\s*$")
    out_cont = re.compile(r"^\s+" + num + r"\s+" + num + r"(?:\s+load address\s+" + num + r")?\s*$")
    in_same = re.compile(r"^ (\S+)\s+" + num + r"\s+" + num + r"\s+(\S.*)$")
    in_name = re.compile(r"^ (\S+)\s*$")
    in_cont = re.compile(r"^\s+" + num + r"\s+" + num + r"\s+(\S.*)$")
    pend_out = None

    def add(section, size, fname):
        if not out_flash or size == 0:
            return
        m = re.search(r"\(([^)]+)\)\s*$", fname)
        member = os.path.basename(m.group(1)) if m else os.path.basename(fname.strip())
        for e, objs in members.items():
            if member in objs or any(p in section for p in sects.get(e, [])):
                cost[e] += size
                return

    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        if not in_map:
            in_map = line.startswith("Linker script and memory map")
            continue
        m = out_re.match(line)
        if m:
            out_flash = in_image(m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4))
            pending = None
            pend_out = None
            continue
        m = out_name_only.match(line)
        if m and not line.startswith(" "):
            pend_out = m.group(1)
            continue
        if pend_out:
            m = out_cont.match(line)
            if m:
                out_flash = in_image(pend_out, int(m.group(1), 16), int(m.group(2), 16), m.group(3))
            pend_out = None
            continue
        m = in_same.match(line)
        if m and not m.group(1).startswith("*"):
            add(m.group(1), int(m.group(3), 16), m.group(4))
            pending = None
            continue
        if pending:
            m = in_cont.match(line)
            if m:
                add(pending, int(m.group(2), 16), m.group(3))
            pending = None
            continue
        m = in_name.match(line)
        if m and not m.group(1).startswith("*"):
            pending = m.group(1)
    return cost


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["report", "table"])
    ap.add_argument("--tool", default=os.environ.get("TOOLCHAIN", "arm-none-eabi-"))
    ap.add_argument("--axf", required=True)
    ap.add_argument("--map", required=True)
    ap.add_argument("--engines", default="", help="engines in the built image (report)")
    ap.add_argument("--pick", default="", help="engines to judge (table)")
    ap.add_argument("--max", type=int, default=1044480)
    ap.add_argument("--proven", type=lambda x: int(x, 0), default=0, help="end of the last image run on the unit: free against it is reported too (report)")
    ap.add_argument("--engine", action="append", default=[], help="NAME=member.o member.o ...")
    ap.add_argument("--sect", action="append", default=[], help="NAME=section-name-part ...")
    ap.add_argument("--txt", help="BUILD.txt to write (report)")
    ap.add_argument("--ptf", default="")
    ap.add_argument("--version", default="")
    ap.add_argument("--commit", default="")
    a = ap.parse_args()

    members, sects = parse_list(a.engine), parse_list(a.sect)
    cost = map_costs(a.map, members, sects)
    img = size_of(a.tool, a.axf)

    if a.mode == "report":
        built = a.engines.split()
        free = a.max - img
        eng_sum = sum(cost[e] for e in built)
        print(f"image (text+data) {img} of {a.max} B, {free} B free" + ("" if free >= 0 else f" - OVER by {-free}"))
        proven_line = ""
        if a.proven:
            pf = a.proven - img
            proven_line = f"device-proven end 0x{a.proven:X}: {pf} B free" if pf >= 0 else f"device-proven end 0x{a.proven:X}: PAST it by {-pf} B (the release gate fails)"
            print(proven_line)
        sd_used = sdram_used(a.tool, a.axf)
        sd_free = SDRAM_SIZE - sd_used
        print(f"SDRAM {sd_used} of {SDRAM_SIZE} B used (.noinit_RAM4), {sd_free} B free")
        print("engines (linked bytes) " + ", ".join(f"{e} {cost[e]}" for e in built))
        if a.txt and free >= 0:
            lines = [f"firmware: {a.ptf}", f"synth-open: {a.version}", f"commit: {a.commit}",
                     f"engines: {' '.join(built)}", f"image text+data: {img}", f"flash cap: {a.max}",
                     f"free: {free}", f"base (image without the engines): {img - eng_sum}"] + ([proven_line] if proven_line else [])
            lines += [f"sdram used: {sd_used}", f"sdram free: {sd_free}"]
            lines += [f"engine {e}: {cost[e]}" for e in built]
            with open(a.txt, "w") as f:
                f.write("\n".join(lines) + "\n")
        if free < 0:
            print(f"ERROR: image (text+data) {img} B exceeds the cap {a.max} B by {-free} B", file=sys.stderr)
            return 1
        return 0

    allsum = sum(cost.values())
    base = img - allsum
    print(f"Linked with all engines: image text+data {img} B (cap {a.max} B, over by {img - a.max} B)")
    print("Flash cost of each engine (linked bytes, after --gc-sections):")
    for e in members:
        print(f"  {e:5s} {cost[e]:7d} B")
    print(f"Base without any engine: {base} B, so {a.max - base} B for engines")
    pick = a.pick.split() or list(members)
    est = base + sum(cost[e] for e in pick)
    print(f"Selected {' '.join(pick)}: {est} B, {a.max - est:+d} B free " + ("(fits)" if est <= a.max else "(OVER)"))
    for e in members:
        if e not in pick:
            e2 = est + cost[e]
            print(f"  + {e:5s} -> {e2} B, {a.max - e2:+d} B free " + ("(fits)" if e2 <= a.max else "(over)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
