#!/usr/bin/env python3
"""The published plugin modules against the public identity this repository keeps (engines/engines.json).

    python3 engines/check_modules.py verify-external DIR --host-abi synth-open/plug/plug_abi.h [--visibility public|private|all]

DIR is one flat directory with the published files of the module repositories (engines/external.py fetch DIR puts them there):
poly-engines-os and poly-engines (synth engines: <id>.plg, <id>.plgi, <id>.wasm, <id>.json), poly-effects-os and poly-effects
(master inserts: <id>.plg, <id>.wasm, <id>.json). For every row of engines.json of the chosen visibility:
  - synth engine: <id>.plg is there, is a synth plugin this host accepts (header, CRC, kind, cpu, ABI major, min_host) and carries
    exactly the name, numbers (stored, patch, type) and version of its row, <id>.plgi is there
  - master insert: <id>.plg is there, is a master insert this host accepts, and its version equals the row
  - when DIR has the module manifest <id>.json: kind, visibility and source repository equal the row, the numbers of a synth
    equal the row, and the sha256 of the .wasm and .plg it names equal the files
Exit 1 on any difference. The firmware's own checkSynth runs separately (synth-open/tools/validate_synth.py, engines/crosscheck.sh).
"""
import argparse
import hashlib
import json
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
HDR = "<IHBBBBHIIIIIIIIIII8x"
HDR_FIELDS = ("magic", "header_size", "abi_major", "abi_minor", "kind", "cpu", "flags", "uid", "version", "min_host", "image_size",
              "bss_size", "rebase_count", "entry_offset", "ram_bytes", "cpu_cycles", "crc32", "rw_offset")
MAN = "<I16s32s16sHBBIIIII4x"
MAN_FIELDS = ("struct_size", "name", "author", "licence", "num_params", "role", "r0", "logo_off", "stored", "patch_engine", "type_id", "magic")
PLUG_MAGIC, SYN1, KIND_MASTER, KIND_SYNTH, CPU_CM4F = 0x31474C50, 0x314E5953, 1, 4, 1


def table():
    return json.load(open(os.path.join(HERE, "engines.json")))


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def abi_of(header):
    t = open(header).read()
    g = lambda n: int(re.search(r"#define\s+%s\s+(\d+)" % n, t).group(1))
    return g("PLUG_ABI_MAJOR"), g("PLUG_ABI_MINOR"), g("PLUG_HOST_LEVEL")


def parse_plg(path, synth):
    data = open(path, "rb").read()
    h = dict(zip(HDR_FIELDS, struct.unpack_from(HDR, data, 0)))
    body = data[64:h["header_size"] + h["image_size"] + 4 * h["rebase_count"]]
    h["crc_ok"] = (zlib.crc32(body) & 0xFFFFFFFF) == h["crc32"]
    m = None
    if synth:
        m = dict(zip(MAN_FIELDS, struct.unpack_from(MAN, data, 64)))
        for k in ("name", "author", "licence"):
            m[k] = m[k].split(b"\0")[0].decode()
    return h, m


def version_of(h):
    return "%d.%d.%d" % (h["version"] >> 16, (h["version"] >> 8) & 255, h["version"] & 255)


def module_manifest(d, row, kind):
    mf = os.path.join(d, row["id"] + ".json")
    if not os.path.exists(mf):
        return []
    man = json.load(open(mf))
    errs = []
    if (man.get("format") != "ffmod" or man.get("kind") != kind or man.get("visibility") != row["visibility"]
            or man.get("source_repo") != row["repo"]):
        errs.append("%s.json: not a %s %s module of %s (says %s %s of %s)" % (row["id"], row["visibility"], kind, row["repo"],
                                                                          man.get("visibility"), man.get("kind"), man.get("source_repo")))
    if kind == "synth" and (man["synth"]["stored"], man["synth"]["patch_engine"], man["synth"]["type_id"]) != (row["stored"], row["patch"], row["type"]):
        errs.append(row["id"] + ".json: numbers differ from engines.json")
    for k in ("wasm", "plg"):
        fl = man.get("files", {}).get(k)
        if fl:
            p = os.path.join(d, fl["name"])
            if not os.path.exists(p):
                errs.append("%s.json names %s, which is missing" % (row["id"], fl["name"]))
            elif sha256(p) != fl["sha256"]:
                errs.append("%s.json: %s sha256 differs" % (row["id"], fl["name"]))
    return errs


def verify_external(a):
    t = table()
    host = abi_of(a.host_abi)
    want = lambda r: a.visibility == "all" or r["visibility"] == a.visibility
    errs, seen = [], []
    for e in [x for x in t["engines"] if want(x)]:
        f = os.path.join(a.dir, e["id"] + ".plg")
        if not os.path.exists(f):
            errs.append("%s: %s.plg is missing (published by %s)" % (e["id"], e["id"], e["repo"]))
            continue
        h, m = parse_plg(f, True)
        if not h["crc_ok"] or h["magic"] != PLUG_MAGIC or h["kind"] != KIND_SYNTH or h["cpu"] != CPU_CM4F:
            errs.append(e["id"] + ": bad header or CRC")
        if h["abi_major"] != host[0] or h["min_host"] > host[2]:
            errs.append(e["id"] + ": the host refuses it (abi major %d vs %d, min_host %d vs level %d)" % (h["abi_major"], host[0], h["min_host"], host[2]))
        if (m["name"], m["stored"], m["patch_engine"], m["type_id"], m["magic"]) != (e["name"], e["stored"], e["patch"], e["type"], SYN1):
            errs.append("%s: manifest %s/%d/%d/%d differs from engines.json %s/%d/%d/%d" % (e["id"], m["name"], m["stored"], m["patch_engine"],
                                                                                           m["type_id"], e["name"], e["stored"], e["patch"], e["type"]))
        if version_of(h) != e["version"]:
            errs.append("%s: version %s, engines.json %s" % (e["id"], version_of(h), e["version"]))
        if not os.path.exists(os.path.join(a.dir, e["id"] + ".plgi")):
            errs.append(e["id"] + ": no .plgi card logo")
        errs += module_manifest(a.dir, e, "synth")
        seen.append(e["id"])
    for e in [x for x in t["inserts"] if want(x)]:
        f = os.path.join(a.dir, e["id"] + ".plg")
        if not os.path.exists(f):
            errs.append("%s: %s.plg is missing (published by %s)" % (e["id"], e["id"], e["repo"]))
            continue
        h, _ = parse_plg(f, False)
        if h["magic"] != PLUG_MAGIC or h["kind"] != KIND_MASTER or not h["crc_ok"]:
            errs.append(e["id"] + ": not a master insert plugin with a valid CRC")
        if h["abi_major"] != host[0] or h["min_host"] > host[2]:
            errs.append(e["id"] + ": the host refuses it")
        if version_of(h) != e["version"]:
            errs.append("%s: version %s, engines.json %s" % (e["id"], version_of(h), e["version"]))
        errs += module_manifest(a.dir, e, "master")
        seen.append(e["id"])
    if errs:
        print("verify-external FAILED: " + a.dir)
        for x in errs:
            print("  " + x)
        return 1
    print("verify-external ok: %s (%d modules, %s: %s)" % (a.dir, len(seen), a.visibility, ", ".join(seen)))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    ve = sp.add_parser("verify-external")
    ve.add_argument("dir")
    ve.add_argument("--host-abi", required=True)
    ve.add_argument("--visibility", default="all", choices=("public", "private", "all"))
    a = ap.parse_args()
    return {"verify-external": verify_external}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
