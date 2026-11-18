#!/usr/bin/env python3
"""The realtime (audio) function set of a built image, and the proof that a flash trim left it alone.

Roots: every function the core objects place in .audio_ram_functions or .audio_flash_functions (AUDIO_CORE_RAM / AUDIO_CORE_FLASH), and every
function of the vector table (the ISRs). The set is the closure of the roots over the direct calls, tail jumps and function addresses in
literal pools of the linked image (objdump -d of the axf). A virtual or pointer call that goes through a table is not followed: the tool
is for choosing cold code to put at -Os, then every function of the set is compared before and after.

  audio_hot_set.py list  --toolchain T --axf X.axf --objs DIR [--obj-prefix AudioEngine/mtAudioEngine.o]
        prints the cold functions (not in the set) of the named objects with their sizes, and the size of the set
  audio_hot_set.py save  --toolchain T --axf X.axf --objs DIR --out FILE
        writes the set with a fingerprint of the machine code of every member (instructions with their symbolic targets, no addresses)
  audio_hot_set.py check --toolchain T --axf X.axf --objs DIR --base FILE
        compares the set of this image with the saved one: same members, same code. Exit 1 on any difference, prints the first ones
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

HOT_SECTIONS = (".audio_ram_functions", ".audio_flash_functions")


def run(cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def object_roots(tc, objdir):
    roots = set()
    for dp, _, fns in os.walk(objdir):
        for fn in fns:
            if not fn.endswith(".o"):
                continue
            for line in run([tc + "objdump", "-t", os.path.join(dp, fn)]).splitlines():
                parts = line.split()
                if len(parts) >= 6 and parts[3] in HOT_SECTIONS and parts[2] == "F":
                    roots.add(parts[-1])
    return roots


def disassemble(tc, axf):
    """name -> (address, [instruction text with symbolic targets], [referenced names]), address -> name"""
    funcs, order, cur = {}, [], None
    dups = {}
    addr_name = {}
    text = run([tc + "objdump", "-d", "--no-show-raw-insn", axf])
    hdr = re.compile(r"^([0-9a-f]{8}) <(.+)>:$")
    ref = re.compile(r"<([^>+]+)(?:\+0x[0-9a-f]+)?>")
    word = re.compile(r"^\s*([0-9a-f]+):\s+\.word\s+0x([0-9a-f]{8})")
    pending_words = []
    for line in text.splitlines():
        m = hdr.match(line)
        if m:
            cur = m.group(2)
            if cur in funcs:   # a local function of the same name in another file: kept apart, told apart by its code (see below)
                dups.setdefault(cur, []).append(len(dups.get(cur, [])) + 2)
                cur = "%s#%d" % (cur, dups[cur][-1])
            funcs[cur] = [int(m.group(1), 16), [], set(), []]
            addr_name[int(m.group(1), 16)] = cur
            order.append(cur)
            continue
        if cur is None or ":\t" not in line:
            continue
        ins = line.split(":\t", 1)[1].strip()
        if ins.startswith("Address 0x"):   # objdump's note after the last function of a range
            continue
        w = word.match(line)
        if w:
            funcs[cur][3].append(int(w.group(2), 16))
            ins = ".word"   # a literal: its value is an address that moves with the image, only function targets matter (below)
        else:
            ins = re.sub(r"\b[0-9a-f]{5,8}\b(?= <)", "", ins)   # "bl 85ebc <foo>" -> "bl <foo>"
            ins = re.sub(r"^(\w+(?:\.\w+)?)\s+[0-9a-f]{3,8}\s+<", r"\1 <", ins)
            ins = re.sub(r"\s+;.*$", "", ins)
            ins = re.sub(r"\.word\s+0x[0-9a-f]+", ".word", ins)
            for r in ref.findall(ins):
                funcs[cur][2].add(r)
        funcs[cur][1].append(ins)
    return funcs, addr_name, order


def vector_roots(tc, axf, addr_name):
    out = run([tc + "objdump", "-s", "-j", ".text", "--start-address=0", "--stop-address=0x1D0", axf])
    names = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 5 and re.fullmatch(r"[0-9a-f]{4,8}", parts[0]):
            for w in parts[1:5]:
                if re.fullmatch(r"[0-9a-f]{8}", w):
                    v = int.from_bytes(bytes.fromhex(w), "little")
                    if v & 1 and (v & ~1) in addr_name:
                        names.add(addr_name[v & ~1])
    return names


def copies(funcs, name):
    out = [name] if name in funcs else []
    k = 2
    while "%s#%d" % (name, k) in funcs:
        out.append("%s#%d" % (name, k))
        k += 1
    return out


def hot_set(tc, axf, objdir):
    funcs, addr_name, _ = disassemble(tc, axf)
    roots = {c for r in object_roots(tc, objdir) for c in copies(funcs, r)} | vector_roots(tc, axf, addr_name)
    seen, todo = set(), list(roots)
    while todo:
        f = todo.pop()
        if f in seen or f not in funcs:
            continue
        seen.add(f)
        refs = set()
        for r in funcs[f][2]:
            refs.update(copies(funcs, r))
        for v in funcs[f][3]:
            if v & 1 and (v & ~1) in addr_name:
                refs.add(addr_name[v & ~1])
        todo.extend(r for r in refs if r not in seen)
    return funcs, seen, roots


# keepForModule only holds the address of the tail pad (a pad of 0 B has none): it changes with the pad, not with the compiler flags
PAD_DEPENDENT = ("_ZN12_GLOBAL__N_113keepForModuleEv",)


def fingerprint(funcs, names):
    """base name -> hash of the sorted code hashes of all its copies (local functions of one name in several files link in any order)"""
    groups = {}
    for n in names:
        if n in PAD_DEPENDENT:
            continue
        h = hashlib.sha1("\n".join(funcs[n][1]).encode()).hexdigest()[:16]
        groups.setdefault(n.split("#")[0], []).append(h)
    return {n: hashlib.sha1(",".join(sorted(h)).encode()).hexdigest()[:16] + ("x%d" % len(h) if len(h) > 1 else "") for n, h in sorted(groups.items())}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("list", "save", "check"))
    ap.add_argument("--toolchain", default="arm-none-eabi-")
    ap.add_argument("--axf", required=True)
    ap.add_argument("--objs", required=True)
    ap.add_argument("--obj-prefix", action="append", default=[])
    ap.add_argument("--out")
    ap.add_argument("--base")
    a = ap.parse_args()
    funcs, hot, roots = hot_set(a.toolchain, a.axf, a.objs)
    if a.cmd == "list":
        print("roots %d, realtime set %d functions, %d B" % (len(roots), len(hot), sum(len(funcs[n][1]) * 2 for n in hot)))
        for pre in a.obj_prefix:
            path = os.path.join(a.objs, pre)
            cold = []
            for line in run([a.toolchain + "nm", "-S", "--defined-only", path]).splitlines():
                p = line.split()
                if len(p) == 4 and p[2] in "tTwW" and p[3] in funcs and p[3] not in hot:
                    cold.append((int(p[1], 16), p[3]))
            print("%s: %d cold functions, %d B" % (pre, len(cold), sum(c[0] for c in cold)))
            for sz, n in sorted(cold, reverse=True):
                print("  %5d %s" % (sz, run([a.toolchain + "c++filt", n]).strip()[:110]))
        return 0
    fp = fingerprint(funcs, hot)
    if a.cmd == "save":
        json.dump({"roots": sorted(roots), "code": fp}, open(a.out, "w"), indent=0)
        print("saved %d functions (%d roots) to %s" % (len(fp), len(roots), a.out))
        return 0
    base = json.load(open(a.base))["code"]
    bad = [("missing", n) for n in base if n not in fp] + [("new", n) for n in fp if n not in base] + \
          [("changed", n) for n in base if n in fp and fp[n] != base[n]]
    print("realtime set: %d functions, base %d, %d differ" % (len(fp), len(base), len(bad)))
    for kind, n in bad[:15]:
        print("  %s %s" % (kind, n))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
