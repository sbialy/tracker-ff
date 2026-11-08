#!/usr/bin/env python3
"""Keeps the engine project's file lists honest.

    TOOLCHAIN=.../arm-none-eabi- python3 -I engines/closure.py [--check | --write] [--root REPO]

The engine build (build_set.py) copies exactly the files of sdk/headers.txt, sdk/sources.txt and sdk/tools.txt into a scratch
tree and compiles only there, so a header that is not listed cannot be seen. This tool computes the real include closure of
every engine plugin with the compiler (-MM, the same flags as the build) and compares it with the lists:

  sources.txt  files the engines own: the 12 wrappers (plug/synth/*_plug.cpp), definitions (engine_*.cpp), DSP (ot-synths/,
               phz/, pluck/) and their private headers
  headers.txt  the plugin SDK header set the engines compile against and the firmware also uses (plug_abi.h, synth_plug.h,
               open_engine.h, engine_list.h, ot/ ...): what a separate engine repo would vendor, versioned by sdk/VERSION
  tools.txt    the plugin toolchain and host side checks (build_plug.py, plug_rt.c, plug.ld, emulators, logo tools). Written by
               hand, not derived

It also checks that sdk/VERSION names the ABI of plug_abi.h, and that engines.json agrees with synth-open/engines.mk (the
sources and the names of the 12 external engines), so the firmware's built-in list and the plugin list cannot drift apart.
Exit code 0 when everything matches. Paths in the lists are relative to the repository root.
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def table():
    t = json.load(open(os.path.join(HERE, "engines.json")))
    t["engines"] = [e for e in t["engines"] if e.get("visibility", "public") == "public"]   # the private rows have no source here
    return t


def read_list(path):
    return [l.strip() for l in open(path) if l.strip() and not l.startswith("#")]


def abi_of(header):
    t = open(header).read()
    g = lambda n: int(re.search(r"#define\s+%s\s+(\d+)" % n, t).group(1))
    return g("PLUG_ABI_MAJOR"), g("PLUG_ABI_MINOR"), g("PLUG_HOST_LEVEL")


def sdk_version(path):
    v = {}
    for l in read_list(path):
        k, _, val = l.partition(" ")
        v[k] = val.strip()
    return v


def tc_prefix():
    tc = os.environ.get("TOOLCHAIN")
    if not tc:
        sys.exit("closure.py: set TOOLCHAIN to the arm-none-eabi- prefix")
    return tc


def deps(tc, root, args, src):
    """Headers and sources one compile line reads, root relative."""
    so = os.path.join(root, "synth-open")
    cxx = src.endswith((".cpp", ".cc"))
    cmd = [tc + ("g++" if cxx else "gcc"), "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16"] + args + [
        "-I", "plug", "-I", ".", "-I", "ot", "-I", "../synth/include", "-MM", src]
    r = subprocess.run(cmd, cwd=so, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode:
        sys.exit("closure.py: %s\n%s" % (" ".join(cmd), r.stderr))
    toks = r.stdout.replace("\\\n", " ").split(None)
    out = set()
    for t in toks[1:]:
        if t.endswith(":"):
            continue
        out.add(os.path.relpath(os.path.normpath(os.path.join(so, t)), root))
    return out


def compute(root, tc):
    t = table()
    owned_prefix = ("synth-open/ot-synths/", "synth-open/phz/", "synth-open/pluck/", "synth-open/plug/synth/")
    listed = set()
    for e in t["engines"]:
        listed.add("synth-open/" + e["def"])
        listed.add("synth-open/" + e["dsp"])
        listed.add("synth-open/" + e["wrapper"])
    tools = set(read_list(os.path.join(HERE, "sdk", "tools.txt")))
    seen = set()
    for e in t["engines"]:
        flags = ["-std=gnu++20", "-include", "cstdint", "-fno-rtti", "-fno-exceptions", "-ffreestanding", "-fno-builtin",
                 "-mfp16-format=ieee", "-DSYNTH_ENGINE_" + e["name"]] + [f for f in e["extra"].split() if f.startswith("-D")]
        for src in (e["wrapper"], e["def"], e["dsp"]):
            seen |= deps(tc, root, flags, src)
    # the plugin runtime and the emulator harness are built for every set
    seen |= deps(tc, root, ["-std=gnu11", "-ffreestanding"], "plug/plug_rt.c")
    seen |= deps(tc, root, ["-std=gnu++20", "-ffreestanding", "-fno-exceptions", "-fno-rtti"], "plug/synth/synth_harness.cpp")
    seen -= tools
    sources = {p for p in seen if p.startswith(owned_prefix) or p in listed}
    headers = seen - sources
    return sorted(sources), sorted(headers)


def mk_vars(path):
    """SYNTH_ENGINE_SRCS_<N> and the engine lists of synth-open/engines.mk (plain text, no make run)."""
    v = {}
    for l in open(path):
        m = re.match(r"^([A-Z_0-9]+)\s*:?=\s*(.*)$", l.rstrip())
        if m:
            v[m.group(1)] = m.group(2).split("#")[0].split()
    return v


def check_mk(root):
    t = table()
    v = mk_vars(os.path.join(root, "synth-open", "engines.mk"))
    errs = []
    ext = v.get("SYNTH_ENGINES_EXT")
    if ext is None:
        errs.append("engines.mk has no SYNTH_ENGINES_EXT")
        ext = []
    names = [e["name"] for e in t["engines"]]
    if sorted(ext) != sorted(names):
        errs.append("engines.mk SYNTH_ENGINES_EXT %s != engines.json %s" % (sorted(ext), sorted(names)))
    for e in t["engines"]:
        want = [e["def"], e["dsp"]]
        got = v.get("SYNTH_ENGINE_SRCS_" + e["name"])
        if got != want:
            errs.append("engines.mk SYNTH_ENGINE_SRCS_%s %s != engines.json %s" % (e["name"], got, want))
    stock = v.get("SYNTH_ENGINES_STOCK", [])
    if sorted(stock + ext) != sorted(v["SYNTH_ENGINES_ALL"]):
        errs.append("engines.mk STOCK + EXT is not SYNTH_ENGINES_ALL")
    return errs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.dirname(HERE))
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--check", action="store_true", help="default")
    g.add_argument("--write", action="store_true", help="rewrite sdk/sources.txt and sdk/headers.txt")
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    errs = []
    # 1. the ABI the version file names
    ver = sdk_version(os.path.join(HERE, "sdk", "VERSION"))
    maj, mi, lvl = abi_of(os.path.join(root, "synth-open", "plug", "plug_abi.h"))
    if ver.get("abi") != "%d.%d" % (maj, mi) or ver.get("host_level") != str(lvl):
        errs.append("sdk/VERSION says abi %s host_level %s, plug_abi.h is %d.%d level %d (bump sdk_version and fix the file)" % (
            ver.get("abi"), ver.get("host_level"), maj, mi, lvl))
    # 2. the engine list against the firmware's
    errs += check_mk(root)
    # 3. the include closure
    sources, headers = compute(root, tc_prefix())
    for name, new in (("sources.txt", sources), ("headers.txt", headers)):
        path = os.path.join(HERE, "sdk", name)
        if a.write:
            with open(path, "w") as f:
                f.write("# generated by engines/closure.py --write, root relative, sorted\n")
                f.write("".join(p + "\n" for p in new))
            continue
        old = read_list(path) if os.path.exists(path) else []
        if old != new:
            errs.append("sdk/%s differs: missing %s, stale %s" % (name, sorted(set(new) - set(old)), sorted(set(old) - set(new))))
    for p in read_list(os.path.join(HERE, "sdk", "tools.txt")):
        if not os.path.exists(os.path.join(root, p)):
            errs.append("sdk/tools.txt names a missing file: " + p)
    if errs:
        print("closure.py: FAILED")
        for e in errs:
            print("  " + e)
        return 1
    print("closure.py: ok (%d source files, %d SDK headers, sdk %s abi %s)" % (len(sources), len(headers), ver["sdk_version"], ver["abi"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
