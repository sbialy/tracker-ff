#!/usr/bin/env python3
"""Standalone build of the synth engine .plg set. Needs no firmware build and writes nothing into the source tree.

    TOOLCHAIN=.../arm-none-eabi- python3 engines/build_set.py build  --out DIR [--flavour autof|fused] [--py PYTHON] [--only ju6,fm4]
    python3 engines/build_set.py verify DIR [--host-abi plug_abi.h]
    python3 engines/build_set.py shell                      (the engine table as shell functions, for synth-open/plug/synth_engines.sh)

build:  copies the files of sdk/headers.txt, sdk/sources.txt and sdk/tools.txt into a scratch tree (so the engines can only see
        the listed SDK headers and their own sources), compiles each engine there with the flags of its flavour, writes
        DIR/<id>.plg and DIR/<id>.plgi (card logo) and DIR/previews, checks every plugin, then writes DIR/SET.json (set version,
        flavour and flags, compiler, SDK version and ABI, source digests, one record per .plg) and DIR/SHA256SUMS.
        DIR is byte reproducible: no dates, no paths, the same inputs give the same files.
checks: GOT relocations 0, rebase words = ABS32 relocations, header/manifest/CRC re-read from the file, and when the Python has
        unicorn the render checksum of the harness scenarios 0 and 1 at the FlexNVM home and in SRAM (equal), and the manifest
        numbers equal to the extension's. verify repeats the file level checks on a finished set (SHA256SUMS, SET.json, ABI).
Not in here (they need a firmware tree): checkSynth through tools/validate_synth.py and the render compare against the firmware's
own objects, see engines/crosscheck.sh.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
HDR = "<IHBBBBHIIIIIIIIIII8x"
HDR_FIELDS = ("magic", "header_size", "abi_major", "abi_minor", "kind", "cpu", "flags", "uid", "version", "min_host", "image_size",
              "bss_size", "rebase_count", "entry_offset", "ram_bytes", "cpu_cycles", "crc32", "rw_offset")
MAN = "<I16s32s16sHBBIIIII4x"
MAN_FIELDS = ("struct_size", "name", "author", "licence", "num_params", "role", "r0", "logo_off", "stored", "patch_engine", "type_id", "magic")
PLUG_MAGIC, SYN1, KIND_SYNTH, CPU_CM4F = 0x31474C50, 0x314E5953, 4, 1


def table():
    """engines.json. "engines" is the PUBLIC rows only (the ones whose source is in this repository and that this project builds),
    "all_engines" every row including the private ones (identity only: numbers, names, logo, repo)."""
    t = json.load(open(os.path.join(HERE, "engines.json")))
    t["all_engines"] = t["engines"]
    t["engines"] = [e for e in t["engines"] if e.get("visibility", "public") == "public"]
    return t


def read_list(path):
    return [l.strip() for l in open(path) if l.strip() and not l.startswith("#")]


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


def sdk_version():
    v = {}
    for l in read_list(os.path.join(HERE, "sdk", "VERSION")):
        k, _, val = l.partition(" ")
        v[k] = val.strip()
    return v


def flavour_flags(t, name):
    f = t["flavours"][name]
    return {"engines": os.environ.get("SYNTH_PLUG_OPT_ENGINES", f["engines"]),
            "o3": os.environ.get("SYNTH_PLUG_OPT_O3", f["o3"])}


def engine_opt(e, fl):
    return fl.get(e["opt"], e["opt"])


def parse_plg(path):
    data = open(path, "rb").read()
    h = dict(zip(HDR_FIELDS, struct.unpack_from(HDR, data, 0)))
    m = dict(zip(MAN_FIELDS, struct.unpack_from(MAN, data, 64)))
    for k in ("name", "author", "licence"):
        m[k] = m[k].split(b"\0")[0].decode()
    body = data[64:h["header_size"] + h["image_size"] + 4 * h["rebase_count"]]
    h["crc_ok"] = (zlib.crc32(body) & 0xFFFFFFFF) == h["crc32"]
    h["file_size"] = len(data)
    return h, m


def run(cmd, **kw):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, **kw)
    if r.returncode:
        sys.exit("FAILED: %s\n%s%s" % (" ".join(cmd), r.stdout, r.stderr))
    return r.stdout


def stage(root, dest):
    """The files of the three lists, at their repository paths."""
    n = 0
    for lst in ("headers.txt", "sources.txt", "tools.txt"):
        for p in read_list(os.path.join(HERE, "sdk", lst)):
            src = os.path.join(root, p)
            if not os.path.exists(src):
                sys.exit("sdk/%s names a missing file: %s" % (lst, p))
            d = os.path.join(dest, p)
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copy2(src, d)
            n += 1
    return n


def digest(stage_dir, lists):
    h = hashlib.sha256()
    for lst in lists:
        for p in read_list(os.path.join(HERE, "sdk", lst)):
            h.update(("%s %s\n" % (p, sha256(os.path.join(stage_dir, p)))).encode())
    return h.hexdigest()


def have_unicorn(py):
    return subprocess.run([py, "-c", "import unicorn"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def build_one(e, t, fl, tc, stage_dir, out, tmp, env, extra=()):
    so = os.path.join(stage_dir, "synth-open")
    fwf, name = t["fwflags"], e["name"]
    dspopt = (engine_opt(e, fl) + " " + e["extra"]).strip()
    elf = os.path.join(tmp, e["id"] + ".elf")
    cmd = [sys.executable, os.path.join(so, "plug", "build_plug.py"), "--id", "org.opentracker.synth." + e["id"], "--kind", "synth",
           "--version", e["version"], "--opt", t["wrapper_opt"], "--synth", "%d,0x%X,0x%X" % (e["stored"], e["patch"], e["type"]),
           "--src-opt", "%s=%s %s -DSYNTH_ENGINE_%s" % (os.path.basename(e["dsp"]), fwf, dspopt, name),
           "--src-opt", "%s=%s -DSYNTH_ENGINE_%s" % (e["def"], fwf, name), "--keep", elf,
           "--out", os.path.join(out, e["id"] + ".plg"), "-I", ".", "-I", "ot", "-I", "../synth/include"] + list(extra) + [
           e["wrapper"], e["def"], e["dsp"]]
    r = subprocess.run(cmd, cwd=so, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode:
        sys.exit("FAILED %s\n%s%s" % (e["id"], r.stdout, r.stderr))
    rel = run([tc + "objdump", "-r", elf])
    return {"got": rel.count("R_ARM_GOT"), "abs32": rel.count("R_ARM_ABS32"), "log": r.stdout.strip()}


def emu_checks(e, py, stage_dir, out, harness):
    """Render checksum of scenarios 0 and 1 at the FlexNVM home and in SRAM, and the manifest numbers line."""
    so = os.path.join(stage_dir, "synth-open")
    emu = os.path.join(so, "plug", "emu_synth.py")
    plg = os.path.join(out, e["id"] + ".plg")
    res, ok = {}, True
    for s in (0, 1):
        def sumof(*extra):
            o = subprocess.run([py, "-I", emu, plg, harness, "--scenario", str(s)] + list(extra), stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True).stdout
            m = re.search(r"^output checksum (\S+)", o, re.M)
            return m.group(1) if m else None
        a, b = sumof("--base", "0x10000000", "--rw-base", "0x20027000"), sumof("--base", "0x1FFF6000")
        ok = ok and a is not None and a == b
        res["scenario%d" % s] = a
    o = subprocess.run([py, "-I", emu, plg, harness, "--scenario", "0", "--blocks", "1"], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True).stdout
    nums = re.search(r"^manifest numbers:.*\(equal to the extension\)", o, re.M) is not None
    return res, ok and nums


def build(a):
    root = os.path.abspath(a.root)
    tc = os.environ.get("TOOLCHAIN")
    if not tc:
        sys.exit("build_set.py: set TOOLCHAIN to the arm-none-eabi- prefix (there is no default: a standalone build never guesses a compiler)")
    t = table()
    flav = a.flavour or t["default_flavour"]
    if flav not in t["flavours"]:
        sys.exit("unknown flavour %s (%s)" % (flav, ", ".join(t["flavours"])))
    fl = flavour_flags(t, flav)
    ver = sdk_version()
    maj, mi, lvl = abi_of(os.path.join(root, "synth-open", "plug", "plug_abi.h"))
    if ver["abi"] != "%d.%d" % (maj, mi) or ver["host_level"] != str(lvl):
        sys.exit("sdk/VERSION (abi %s, host level %s) does not match plug_abi.h (%d.%d, level %d): bump the SDK version" % (
            ver["abi"], ver["host_level"], maj, mi, lvl))
    engines = [e for e in t["engines"] if not a.only or e["id"] in a.only.split(",")]
    out = os.path.abspath(a.out)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(os.path.join(out, "previews"))
    env = dict(os.environ, TOOLCHAIN=tc)
    py = a.py or sys.executable
    emu_ok = have_unicorn(py) and not a.no_emu
    with tempfile.TemporaryDirectory(prefix="engset") as tmp:
        sd = os.path.join(tmp, "stage")
        n = stage(root, sd)
        print("staged %d files (SDK %s, ABI %s); flavour %s: engines '%s', O3 engines '%s'" % (n, ver["sdk_version"], ver["abi"], flav,
                                                                                            fl["engines"], fl["o3"]))
        with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
            built = list(ex.map(lambda e: build_one(e, t, fl, tc, sd, out, tmp, env), engines))
        so = os.path.join(sd, "synth-open")
        harness = os.path.join(tmp, "harness.plg")
        if emu_ok:
            run([sys.executable, os.path.join(so, "plug", "build_plug.py"), "--id", "ff.test.synthharness", "--kind", "offline",
                 "--opt", "-O2 -fno-builtin", "--out", harness, "-I", ".", "-I", "ot", "-I", "../synth/include",
                 "plug/synth/synth_harness.cpp"], cwd=so, env=env)
        recs, fail = [], False
        for e, b in zip(engines, built):
            plg = os.path.join(out, e["id"] + ".plg")
            h, m = parse_plg(plg)
            ok = (b["got"] == 0 and b["abs32"] == h["rebase_count"] > 0 and h["crc_ok"] and h["magic"] == PLUG_MAGIC and
                  h["abi_major"] == maj and h["kind"] == KIND_SYNTH and h["cpu"] == CPU_CM4F and h["min_host"] <= lvl and
                  (m["stored"], m["patch_engine"], m["type_id"], m["magic"]) == (e["stored"], e["patch"], e["type"], SYN1) and
                  m["name"] == e["name"])
            emu = None
            if emu_ok:
                emu, eok = emu_checks(e, py, sd, out, harness)
                ok = ok and eok
            # logo sidecar and preview
            run([sys.executable, "-I", os.path.join(so, "tools", "make_plgi.py"), "--text", e["logo"], "-o",
                 os.path.join(out, e["id"] + ".plgi"), "--preview", os.path.join(out, "previews", e["id"] + ".png")])
            maj_mi = "%d.%d.%d" % (h["version"] >> 16, (h["version"] >> 8) & 255, h["version"] & 255)
            ok = ok and maj_mi == e["version"]
            fail = fail or not ok
            print("%-6s %s  %s v%s, image %d B, bss %d B, %d rebase words, GOT %d, abi %d.%d%s" % (
                e["id"], "ok  " if ok else "FAIL", m["name"], maj_mi, h["image_size"], h["bss_size"], h["rebase_count"], b["got"],
                h["abi_major"], h["abi_minor"], (", render " + "/".join(emu.values())) if emu else ", render check skipped"))
            recs.append({
                "file": e["id"] + ".plg", "logo": e["id"] + ".plgi", "name": m["name"], "version": maj_mi, "uid": "0x%08x" % h["uid"],
                "stored": m["stored"], "patch_engine": m["patch_engine"], "type_id": m["type_id"],
                "flags": engine_opt(e, fl) + (" " + e["extra"] if e["extra"] else ""),
                "abi": "%d.%d" % (h["abi_major"], h["abi_minor"]), "min_host": h["min_host"], "image_size": h["image_size"],
                "bss_size": h["bss_size"], "rw_offset": h["rw_offset"], "rebase_words": h["rebase_count"], "file_size": h["file_size"],
                "crc32": "0x%08x" % h["crc32"], "sha256": sha256(plg), "render_checksums": emu})
        run([sys.executable, "-I", os.path.join(so, "tools", "plgi_sheet.py"), "-o", os.path.join(out, "previews", "sheet.png")] +
            [os.path.join(out, "previews", e["id"] + ".png") for e in engines])
        gcc = run([tc + "gcc", "--version"]).splitlines()[0]
        man = {
            "set": "tracker-ff-synth-engines", "set_version": t["set_version"], "flavour": flav, "flavour_note": t["flavours"][flav]["note"],
            "flags": {"engines": fl["engines"], "o3": fl["o3"], "firmware_flags": t["fwflags"], "wrapper": t["wrapper_opt"]},
            "compiler": gcc,
            "sdk": {"version": ver["sdk_version"], "abi": ver["abi"], "host_level": int(ver["host_level"]),
                    "headers_sha256": digest(sd, ("headers.txt", "tools.txt")), "sources_sha256": digest(sd, ("sources.txt",))},
            "requires": "firmware plug host with PLUG_ABI_MAJOR %d and PLUG_HOST_LEVEL >= %d" % (maj, 1),
            "render_checks": "done" if emu_ok else "skipped (the Python has no unicorn or --no-emu)",
            "engines": recs}
        with open(os.path.join(out, "SET.json"), "w") as f:
            json.dump(man, f, indent=1)
            f.write("\n")
    write_sums(out)
    print("%s: %d engines, set %s, flavour %s%s" % (out, len(recs), t["set_version"], flav, "" if not fail else ", FAILED"))
    return 1 if fail else 0


def logo_test(a):
    """The device test files of the card logos, from the flavour of a finished set: ju6.plg with a logo trailer (the sidecar must
    win over it), a truncated fm4.plgi and a btype.plgi with a bad magic (both: text name). Never part of a shipped set."""
    root = os.path.abspath(a.root)
    tc = os.environ.get("TOOLCHAIN")
    if not tc:
        sys.exit("build_set.py: set TOOLCHAIN")
    t = table()
    d = os.path.abspath(a.set)
    flav = json.load(open(os.path.join(d, "SET.json")))["flavour"]
    fl = flavour_flags(t, flav)
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ, TOOLCHAIN=tc)
    with tempfile.TemporaryDirectory(prefix="engset") as tmp:
        sd = os.path.join(tmp, "stage")
        stage(root, sd)
        trailer = os.path.join(tmp, "ju6-trailer.plgi")
        run([sys.executable, "-I", os.path.join(sd, "synth-open", "tools", "make_plgi.py"), "--text", "JU6-T", "-o", trailer])
        e = next(x for x in t["engines"] if x["id"] == "ju6")
        build_one(e, t, fl, tc, sd, out, tmp, env, ["--logo", trailer])
        harness = os.path.join(out, "h.plg")
        run([sys.executable, os.path.join(sd, "synth-open", "plug", "build_plug.py"), "--id", "ff.test.synthharness", "--kind", "offline",
             "--opt", "-O2 -fno-builtin", "--out", harness, "-I", ".", "-I", "ot", "-I", "../synth/include",
             "plug/synth/synth_harness.cpp"], cwd=os.path.join(sd, "synth-open"), env=env)
    fm4 = open(os.path.join(d, "fm4.plgi"), "rb").read()
    bt = open(os.path.join(d, "btype.plgi"), "rb").read()
    open(os.path.join(out, "fm4.plgi"), "wb").write(fm4[:1000])
    open(os.path.join(out, "btype.plgi"), "wb").write(b"PLGX" + bt[4:])
    print("logo test files in " + out)
    return 0


def package(a):
    """OUT/../tracker-ff-engines-<set>-<flavour>.zip with the set under Plugins/Synth/ (unzip at the card root). Reproducible: sorted
    names, fixed timestamps. Refuses a set that does not verify."""
    import zipfile
    d = os.path.abspath(a.dir)
    if verify(argparse.Namespace(dir=d, host_abi=a.host_abi)):
        return 1
    man = json.load(open(os.path.join(d, "SET.json")))
    zp = os.path.join(os.path.dirname(d), "tracker-ff-engines-%s-%s.zip" % (man["set_version"], man["flavour"]))
    names = sorted(os.path.relpath(os.path.join(dp, f), d) for dp, _, fn in os.walk(d) for f in fn if not f.startswith("."))
    names = [n for n in names if not n.startswith("previews/")]   # the PNGs are for people, not for the card
    with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
        for n in names:
            zi = zipfile.ZipInfo("Plugins/Synth/" + n, (2026, 1, 1, 0, 0, 0))
            zi.compress_type, zi.external_attr = zipfile.ZIP_DEFLATED, 0o644 << 16
            z.writestr(zi, open(os.path.join(d, n), "rb").read())
    print("%s: %d files, sha256 %s" % (zp, len(names), sha256(zp)))
    return 0


def write_sums(out):
    files = []
    for dp, _, fn in os.walk(out):
        for f in fn:
            p = os.path.relpath(os.path.join(dp, f), out)
            if p != "SHA256SUMS" and not f.startswith("."):
                files.append(p)
    with open(os.path.join(out, "SHA256SUMS"), "w") as f:
        for p in sorted(files):
            f.write("%s  %s\n" % (sha256(os.path.join(out, p)), p))


def verify(a):
    d = os.path.abspath(a.dir)
    errs = []
    sums = os.path.join(d, "SHA256SUMS")
    listed = {}
    for l in open(sums):
        h, _, p = l.rstrip("\n").partition("  ")
        listed[p] = h
    for p, h in listed.items():
        fp = os.path.join(d, p)
        if not os.path.exists(fp):
            errs.append("SHA256SUMS lists a missing file: " + p)
        elif sha256(fp) != h:
            errs.append("hash differs: " + p)
    for dp, _, fn in os.walk(d):
        for f in fn:
            p = os.path.relpath(os.path.join(dp, f), d)
            if p != "SHA256SUMS" and p not in listed and not f.startswith("."):
                errs.append("file not in SHA256SUMS: " + p)
    man = json.load(open(os.path.join(d, "SET.json")))
    host = None
    if a.host_abi:
        host = abi_of(a.host_abi)
        if "%d.%d" % host[:2] != man["sdk"]["abi"] and str(host[0]) != man["sdk"]["abi"].split(".")[0]:
            errs.append("set built for ABI %s, host header is %d.%d" % (man["sdk"]["abi"], host[0], host[1]))
    for r in man["engines"]:
        h, m = parse_plg(os.path.join(d, r["file"]))
        if not h["crc_ok"] or h["magic"] != PLUG_MAGIC or h["kind"] != KIND_SYNTH or h["cpu"] != CPU_CM4F:
            errs.append(r["file"] + ": bad header or CRC")
        if "%d.%d" % (h["abi_major"], h["abi_minor"]) not in (r["abi"],) or str(h["abi_major"]) != man["sdk"]["abi"].split(".")[0]:
            errs.append(r["file"] + ": ABI differs from SET.json")
        if host and (h["abi_major"] != host[0] or h["min_host"] > host[2]):
            errs.append(r["file"] + ": the host refuses it (abi major %d vs %d, min_host %d vs level %d)" % (h["abi_major"], host[0],
                                                                                                           h["min_host"], host[2]))
        if (m["stored"], m["patch_engine"], m["type_id"], m["magic"]) != (r["stored"], r["patch_engine"], r["type_id"], SYN1):
            errs.append(r["file"] + ": manifest numbers differ from SET.json")
        if "%d.%d.%d" % (h["version"] >> 16, (h["version"] >> 8) & 255, h["version"] & 255) != r["version"]:
            errs.append(r["file"] + ": version differs from SET.json")
        if sha256(os.path.join(d, r["file"])) != r["sha256"]:
            errs.append(r["file"] + ": sha256 differs from SET.json")
    nums = [(r["stored"], r["patch_engine"], r["type_id"]) for r in man["engines"]]
    for k in range(3):
        col = [n[k] for n in nums]
        if len(set(col)) != len(col):
            errs.append("duplicate engine numbers in the set (column %d)" % k)
    if errs:
        print("verify FAILED: " + d)
        for e in errs:
            print("  " + e)
        return 1
    print("verify ok: %s (%d engines, set %s, flavour %s, sdk %s abi %s%s)" % (
        d, len(man["engines"]), man["set_version"], man["flavour"], man["sdk"]["version"], man["sdk"]["abi"],
        ", host header %d.%d level %d" % host if host else ""))
    return 0


def verify_external(a):
    """The artifacts of the private repositories against the public identity in engines.json: every private engine .plg is there, is a
    valid synth plugin of this host's ABI (header, CRC, kind) and carries exactly the numbers, name and version of its row; every private
    insert .plg is a master insert of this ABI. When the directory has the module manifests (<id>.json) and .wasm files they are checked
    too: kind, visibility, source repository, numbers, sha256 of the files. The dir may be the poly-engines and poly-effects release
    assets merged. Exit 1 on any difference."""
    t = table()
    host = abi_of(a.host_abi)
    errs = []
    for e in [x for x in t["all_engines"] if x.get("visibility") == "private"]:
        f = os.path.join(a.dir, e["id"] + ".plg")
        if not os.path.exists(f):
            errs.append("%s: %s.plg is missing (published by %s)" % (e["id"], e["id"], e["repo"]))
            continue
        h, m = parse_plg(f)
        if not h["crc_ok"] or h["magic"] != PLUG_MAGIC or h["kind"] != KIND_SYNTH or h["cpu"] != CPU_CM4F:
            errs.append(e["id"] + ": bad header or CRC")
        if h["abi_major"] != host[0] or h["min_host"] > host[2]:
            errs.append(e["id"] + ": the host refuses it (abi major %d vs %d, min_host %d vs level %d)" % (h["abi_major"], host[0], h["min_host"], host[2]))
        if (m["name"], m["stored"], m["patch_engine"], m["type_id"], m["magic"]) != (e["name"], e["stored"], e["patch"], e["type"], SYN1):
            errs.append("%s: manifest %s/%d/%d/%d differs from engines.json %s/%d/%d/%d" % (e["id"], m["name"], m["stored"], m["patch_engine"], m["type_id"],
                                                                                           e["name"], e["stored"], e["patch"], e["type"]))
        if "%d.%d.%d" % (h["version"] >> 16, (h["version"] >> 8) & 255, h["version"] & 255) != e["version"]:
            errs.append(e["id"] + ": version differs from engines.json")
        if not os.path.exists(os.path.join(a.dir, e["id"] + ".plgi")):
            errs.append(e["id"] + ": no .plgi card logo")
        errs += _module_manifest(a.dir, e["id"], "synth", e)
    for e in [x for x in t["inserts"] if x.get("visibility") == "private"]:
        f = os.path.join(a.dir, e["id"] + ".plg")
        if not os.path.exists(f):
            errs.append("%s: %s.plg is missing (published by %s)" % (e["id"], e["id"], e["repo"]))
            continue
        data = open(f, "rb").read()
        h = dict(zip(HDR_FIELDS, struct.unpack_from(HDR, data, 0)))
        body = data[64:h["header_size"] + h["image_size"] + 4 * h["rebase_count"]]
        if h["magic"] != PLUG_MAGIC or h["kind"] != 1 or (zlib.crc32(body) & 0xFFFFFFFF) != h["crc32"]:
            errs.append(e["id"] + ": not a master insert plugin with a valid CRC")
        if h["abi_major"] != host[0] or h["min_host"] > host[2]:
            errs.append(e["id"] + ": the host refuses it")
        errs += _module_manifest(a.dir, e["id"], "master", e)
    if errs:
        print("verify-external FAILED: " + a.dir)
        for x in errs:
            print("  " + x)
        return 1
    print("verify-external ok: %s (%s)" % (a.dir, ", ".join(x["id"] for x in t["all_engines"] + t["inserts"] if x.get("visibility") == "private")))
    return 0


def _module_manifest(d, ident, kind, row):
    mf = os.path.join(d, ident + ".json")
    if not os.path.exists(mf):
        return []
    man = json.load(open(mf))
    errs = []
    if man.get("format") != "ffmod" or man.get("kind") != kind or man.get("visibility") != "private" or man.get("source_repo") != row["repo"]:
        errs.append("%s.json: not a private %s module of %s" % (ident, kind, row["repo"]))
    if kind == "synth" and (man["synth"]["stored"], man["synth"]["patch_engine"], man["synth"]["type_id"]) != (row["stored"], row["patch"], row["type"]):
        errs.append(ident + ".json: numbers differ from engines.json")
    for k in ("wasm", "plg"):
        fl = man["files"].get(k)
        if fl and sha256(os.path.join(d, fl["name"])) != fl["sha256"]:
            errs.append("%s.json: %s sha256 differs" % (ident, fl["name"]))
    return errs


def shell(_a):
    """The table as the shell functions synth_engines.sh always had (synth_render_compare.sh and the old build_plugs.sh read them)."""
    t = table()
    fl = flavour_flags(t, t["default_flavour"])
    print('SYNTH_PLUG_OPT_ENGINES="${SYNTH_PLUG_OPT_ENGINES:-%s}"' % fl["engines"])
    print('SYNTH_PLUG_OPT_O3="${SYNTH_PLUG_OPT_O3:-%s}"' % fl["o3"])
    print('SYNTH_PLUGS="%s"' % " ".join(e["id"] for e in t["engines"]))
    def fn(name, rows):
        print("%s() {\n    case \"$1\" in" % name)
        for i, v in rows:
            print("        %s) echo %s ;;" % (i, v))
        print("        *) return 1 ;;\n    esac\n}")
    sub = {"engines": "$SYNTH_PLUG_OPT_ENGINES", "o3": "$SYNTH_PLUG_OPT_O3"}
    fn("synth_plug_info", [(e["id"], "\"%s %s %s %s\"" % (e["name"], e["def"], e["dsp"], (sub.get(e["opt"], e["opt"]) + " " + e["extra"]).strip()))
                          for e in t["engines"]])
    fn("synth_plug_numbers", [(e["id"], "%d,0x%X,0x%X" % (e["stored"], e["patch"], e["type"])) for e in t["engines"]])
    fn("synth_plug_logo", [(e["id"], e["logo"]) for e in t["engines"]])
    return 0


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    b = sp.add_parser("build")
    b.add_argument("--out", required=True)
    b.add_argument("--flavour")
    b.add_argument("--root", default=os.path.dirname(HERE), help="repository root (the SDK and engine files are read from here)")
    b.add_argument("--py", help="Python with unicorn for the render checks (default: the running one)")
    b.add_argument("--no-emu", action="store_true")
    b.add_argument("--only", help="comma separated engine ids")
    b.add_argument("--jobs", type=int, default=4)
    v = sp.add_parser("verify")
    v.add_argument("dir")
    v.add_argument("--host-abi", help="the firmware's plug_abi.h: refuse a set its host would refuse")
    sp.add_parser("shell")
    ve = sp.add_parser("verify-external")
    ve.add_argument("dir")
    ve.add_argument("--host-abi", required=True)
    pk = sp.add_parser("package")
    pk.add_argument("dir")
    pk.add_argument("--host-abi")
    lt = sp.add_parser("logo-test")
    lt.add_argument("--set", required=True, help="a finished set (its flavour is used)")
    lt.add_argument("--out", required=True)
    lt.add_argument("--root", default=os.path.dirname(HERE))
    a = ap.parse_args()
    return {"build": build, "verify": verify, "shell": shell, "verify-external": verify_external, "logo-test": logo_test, "package": package}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
