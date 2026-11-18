#!/usr/bin/env python3
"""Proof helper for FW_LTO builds: the machine code of every function that comes from a NON link-time-optimised object is the same as in
the plain build of the same tree, apart from the names of the functions it calls (LTO renames callees: foo.lto_priv.0, foo.constprop.1).

  lto_audio_cmp.py --toolchain T --base-axf plain.axf --new-axf lto.axf --objs NEWOBJDIR [--hot base_hot.json]

NEWOBJDIR is the object dir of the LTO build. An LTO object is slim (no code symbols), a plain object has them, so the functions to compare are
the defined text symbols of the objects that have any. Each such function is looked up by name in both images and its instruction text
(addresses and callee suffixes stripped) compared. With --hot the count is repeated for the members of the realtime set saved by
audio_hot_set.py (save), so the audio path is covered by name. Exit 0 only when every compared function is identical.
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audio_hot_set as ahs  # noqa: E402

SUFFIX = re.compile(r"\.(?:constprop|isra|part|lto_priv|cold|localalias)(?:\.\d+)*")


def norm(lines):
    # literal pool words (the pool of a function runs into the next symbol, its size follows the layout) and undecodable data are not code
    return [SUFFIX.sub("", re.sub(r"\.\d+(?=[>\s]|$)", "", s)) for s in lines if s not in (".word", ".byte", ".short") and "UNDEFINED" not in s]


def plain_functions(tc, objdir):
    names = {}
    for dp, _, fns in os.walk(objdir):
        for fn in fns:
            if not fn.endswith(".o"):
                continue
            path = os.path.join(dp, fn)
            for line in ahs.run([tc + "nm", "-S", "--defined-only", path]).splitlines():
                p = line.split()
                if len(p) == 4 and p[2] in "tTwW":
                    names[p[3]] = os.path.relpath(path, objdir)
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--toolchain", default="arm-none-eabi-")
    ap.add_argument("--base-axf", required=True)
    ap.add_argument("--new-axf", required=True)
    ap.add_argument("--objs", required=True)
    ap.add_argument("--hot")
    a = ap.parse_args()
    bf, _, _ = ahs.disassemble(a.toolchain, a.base_axf)
    nf, _, _ = ahs.disassemble(a.toolchain, a.new_axf)
    plain = plain_functions(a.toolchain, a.objs)
    hot = set(json.load(open(a.hot))["code"]) if a.hot else set()
    same = diff = gone = 0
    hot_same = hot_diff = hot_gone = 0
    first = []
    for n, obj in sorted(plain.items()):
        if n not in bf:
            continue
        if n not in nf:
            gone += 1
            hot_gone += n in hot
            first.append(("gone", n, obj))
            continue
        if norm(bf[n][1]) == norm(nf[n][1]):
            same += 1
            hot_same += n in hot
        else:
            diff += 1
            hot_diff += n in hot
            first.append(("differs", n, obj))
    print("functions of plain objects: %d identical, %d differ, %d not in the LTO image" % (same, diff, gone))
    if hot:
        print("members of the realtime set among them: %d identical, %d differ, %d gone" % (hot_same, hot_diff, hot_gone))
    for k, n, o in first[:25]:
        print("  %s %s (%s)" % (k, n, o))
    return 0 if diff == 0 and gone == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
