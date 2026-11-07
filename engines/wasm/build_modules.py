#!/usr/bin/env python3
"""The browser twin of the plugins: one .wasm module and one manifest per engine and per master insert (synth-open/plug/plug_wasm.h).

    python3 engines/wasm/build_modules.py --out DIR [--which public|private|all] [--only ju6,dirt] [--engines-plg DIR] [--inserts-plg DIR]
                                          [--emsdk DIR] [--wasm-abi-major N]
    TOOLCHAIN=.../arm-none-eabi- python3 engines/wasm/build_modules.py --verify-plg DIR      (insert table = synth-open/plug/build_plugs.sh)

The table is engines/engines.json: "engines" and "inserts", each row has a "visibility". public = the source is in this repository and
the artifacts are built and published from here. private = the source lives in a private repository (poly-engines, poly-effects) that
publishes the same files, here the row is the public identity (numbers, names) and the build is only done with --which all|private
(the sources are still in this tree until the deletion commit of ref/private-engines-effects-design.md section 13).

Engines are built in a scratch copy of the listed SDK files (like build_set.py), with the EDITOR HOST's compile flags (wasm.json
"engine_cxxflags" must equal `common` of poly-editor scripts/build-wasm.ts, or a module is not bit identical to the linked-in engine).
Inserts are built from the tree. Every module is checked (only the five module imports, plug_entry and plug_wasm_abi exported, no name
section, no DWARF), and the descriptor is read from the plugin's own sources (native arm64 build), so no number is typed twice.

DIR gets <id>.wasm, <id>.json per module, NOTICE.md, the licence texts the modules name, MODULES.json (the set) and SHA256SUMS.
No dates, paths or host names in any file: a rebuild is a diff. Never runs an x86_64 binary (the descriptor dump is arm64).
"""
import argparse
import json
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ENG = os.path.dirname(HERE)
ROOT = os.path.dirname(ENG)
sys.path.insert(0, HERE)
sys.path.insert(0, ENG)
import build_set  # noqa: E402  (stage, table, abi_of)
import wasm_common as wc  # noqa: E402

SO = os.path.join(ROOT, "synth-open")
FIELDS = ("max_voices", "volume_param", "work_bytes", "slot_fx", "params", "sections", "macros")


def check_table(t):
    """engines.json sanity: unique ids, a visibility on every row, a repo on exactly the private ones, the named repo exists."""
    ids = [r["id"] for r in t["engines"] + t["inserts"]]
    if len(ids) != len(set(ids)):
        sys.exit("engines.json: duplicate module ids")
    for r in t["engines"] + t["inserts"]:
        if r.get("visibility") not in ("public", "private"):
            sys.exit("engines.json: %s has no visibility public|private" % r["id"])
        if (r["visibility"] == "private") != ("repo" in r) or ("repo" in r and r["repo"] not in t["private_repos"]):
            sys.exit("engines.json: %s: a private module names its repo (private_repos), a public one has none" % r["id"])


def notice_section(name):
    """The section '## <name> ...' of synth-open/plug/fx/NOTICE.md up to the next '## ', as one text."""
    t = open(os.path.join(SO, "plug", "fx", "NOTICE.md")).read()
    m = re.search(r"^## %s\b.*?\n(.*?)(?=^## |\Z)" % re.escape(name), t, re.M | re.S)
    if not m:
        sys.exit("synth-open/plug/fx/NOTICE.md has no section '## %s'" % name)
    return " ".join(m.group(1).split()).replace("licenses/", "")


def native_dump(srcs, so, tmp, tag, inc, extra_for=None):
    """Descriptor JSON of a plugin from its own sources, arm64 clang. C stays C (plug_entry keeps its unmangled name)."""
    objs = []
    for src in list(srcs) + [os.path.join(HERE, "dump_desc.cpp")]:
        o = os.path.join(tmp, "%s_%s.native.o" % (tag, os.path.basename(src)))
        cxx = src.endswith(".cpp")
        fl = (["clang++", "-std=c++20", "-fno-exceptions", "-fno-rtti"] if cxx else ["clang", "-std=c11"]) + ["-arch", "arm64", "-O1", "-include",
              os.path.join(HERE, "native_compat.h")]
        fl += (extra_for or {}).get(src, []) + inc + ["-c", src, "-o", o]
        wc.run(fl, so, os.environ)
        objs.append(o)
    dump = os.path.join(tmp, tag + "_dump")
    wc.run(["clang++", "-arch", "arm64"] + objs + ["-o", dump], so, os.environ)
    return json.loads(wc.run([dump], so, os.environ))


def link_and_check(e, objs, w, so, tmp, out, env):
    wasm = os.path.join(out, e["id"] + ".wasm")
    wc.run(["emcc"] + w["linkflags"].split() + objs + ["-o", wasm], so, env)
    data = open(wasm, "rb").read()
    imps, exps = wc.imports_exports(data)
    bad = [x for x in imps if (x[0], x[1]) not in wc.ALLOWED_IMPORTS]
    if bad:
        sys.exit("%s: imports outside the module ABI: %s" % (e["id"], bad))
    names = {n for n, _ in exps}
    if not {"plug_entry", "plug_wasm_abi"} <= names:
        sys.exit("%s: exports %s lack plug_entry or plug_wasm_abi" % (e["id"], sorted(names)))
    custom = [n for sid, n, _ in wc.wasm_sections(data) if sid == 0]
    if any(n == "name" or n.startswith(".debug") or n == "sourceMappingURL" for n in custom):
        sys.exit("%s: not stripped (custom sections %s)" % (e["id"], custom))
    return data, imps, custom, wasm


def build_engine(e, t, w, sd, tmp, out, env, emver, abi, wabi, claim, plg_dir):
    so = os.path.join(sd, "synth-open")
    rtflags = w["rtflags"].split()
    if claim is not None:
        rtflags += ["-DPLUG_RT_WASM_ABI=((%d<<16)|%d)" % (claim, wabi[1])]
    rt = os.path.join(tmp, e["id"] + "_rt.o")
    wc.run(["emcc"] + rtflags + ["-I", "plug", "-c", "plug/plug_rt_wasm.c", "-o", rt], so, env)
    objs = [rt]
    inc = ["-I", ".", "-I", "ot", "-I", "../synth/include", "-I", "plug"]
    for src in (e["wrapper"], e["def"], e["dsp"]):
        o = os.path.join(tmp, "%s_%s.o" % (e["id"], os.path.basename(src)))
        # the DSP source also gets the engine's own defines (engines.json "extra", e.g. -DPLUCK_WORK_POOL: the plugin takes its work
        # pool from host memory, as it does on the device)
        wc.run(["em++"] + w["engine_cxxflags"].split() + w["moduleflags"].split() + (e["extra"].split() if src == e["dsp"] else []) +
               ["-DSYNTH_ENGINE_%s" % e["name"]] + inc + ["-c", src, "-o", o], so, env)
        objs.append(o)
    data, imps, custom, wasm = link_and_check(e, objs, w, so, tmp, out, env)
    nf = {src: e["extra"].split() for src in (e["dsp"],)}
    desc = native_dump([e["wrapper"], e["def"], e["dsp"]], so, tmp, e["id"], ["-DSYNTH_ENGINE_%s" % e["name"]] + inc, nf)
    syn = desc.pop("synth")
    if (syn["stored"], syn["patch_engine"], syn["type_id"], syn["name"]) != (e["stored"], e["patch"], e["type"], e["name"]):
        sys.exit("%s: the extension's numbers differ from engines.json" % e["id"])
    module = re.search(r"ot-synths/(\w+)/", e["dsp"])
    notice = t["notices"][e["notice"]].replace("{module}", module.group(1) if module else e["id"]) if e.get("notice") else None
    man = {
        "format": "ffmod", "format_version": 1, "id": "org.opentracker.synth." + e["id"], "kind": "synth", "name": e["name"],
        "version": e["version"], "abi": "%d.%d" % abi[:2], "min_host": 1, "wasm_abi": "%d.%d" % (claim if claim is not None else wabi[0], wabi[1]),
        "visibility": e["visibility"], "source_repo": "poly" if e["visibility"] == "public" else e["repo"],
        "licence": e["licence"], "author": e.get("author", "tracker-ff"),
        "synth": {"stored": e["stored"], "patch_engine": e["patch"], "type_id": e["type"]},
        "build": {"emcc": emver, "cxxflags": w["engine_cxxflags"], "moduleflags": w["moduleflags"], "rtflags": w["rtflags"], "linkflags": w["linkflags"],
                  "extra": e["extra"], "editor_flags_from": w["editor_flags_from"]},
        "files": {"wasm": {"name": e["id"] + ".wasm", "size": len(data), "sha256": wc.sha(wasm)}},
        "imports": sorted("%s.%s" % (m, f) for m, f, _ in imps), "custom_sections": custom,
    }
    if notice:
        man["notice"] = notice
    man["synth"].update({k: syn[k] for k in FIELDS})
    man["descriptor"] = desc
    if plg_dir and os.path.exists(os.path.join(plg_dir, e["id"] + ".plg")):
        plg = os.path.join(plg_dir, e["id"] + ".plg")
        man["files"]["plg"] = {"name": e["id"] + ".plg", "size": os.path.getsize(plg), "sha256": wc.sha(plg)}
    return man, len(data), notice


def build_insert(e, w, tmp, out, env, emver, abi, wabi, claim, plg_dir):
    so = SO
    rtflags = w["rtflags"].split()
    if claim is not None:
        rtflags += ["-DPLUG_RT_WASM_ABI=((%d<<16)|%d)" % (claim, wabi[1])]
    rt = os.path.join(tmp, e["id"] + "_rt.o")
    wc.run(["emcc"] + rtflags + ["-I", "plug", "-c", "plug/plug_rt_wasm.c", "-o", rt], so, env)
    objs = [rt]
    inc = sum((["-I", i] for i in e["includes"]), []) + ["-I", "plug"]
    for src in e["sources"]:
        o = os.path.join(tmp, "%s_%s.o" % (e["id"], os.path.basename(src)))
        cxx = src.endswith(".cpp")
        wc.run(["em++" if cxx else "emcc"] + (w["insert_cxxflags"] if cxx else w["insert_cflags"]).split() + (w["moduleflags"] if cxx else w["moduleflags_c"]).split()
               + inc + ["-c", src, "-o", o], so, env)
        objs.append(o)
    data, imps, custom, wasm = link_and_check(e, objs, w, so, tmp, out, env)
    desc = native_dump(e["sources"], so, tmp, e["id"], inc)
    notice = notice_section(e["notice_section"]) if e.get("notice_section") else None
    man = {
        "format": "ffmod", "format_version": 1, "id": e["plug_id"], "kind": "master", "name": desc["name"], "version": e["version"],
        "abi": "%d.%d" % abi[:2], "min_host": 1, "wasm_abi": "%d.%d" % (claim if claim is not None else wabi[0], wabi[1]),
        "visibility": e["visibility"], "source_repo": "poly" if e["visibility"] == "public" else e["repo"],
        "licence": e["licence"], "author": desc["author"],
        "params": desc["params"], "instance_size": desc["instance_size"],
        "build": {"emcc": emver, "cxxflags": w["insert_cxxflags"], "cflags": w["insert_cflags"], "moduleflags": w["moduleflags"],
                  "rtflags": w["rtflags"], "linkflags": w["linkflags"]},
        "files": {"wasm": {"name": e["id"] + ".wasm", "size": len(data), "sha256": wc.sha(wasm)}},
        "imports": sorted("%s.%s" % (m, f) for m, f, _ in imps), "custom_sections": custom,
    }
    if notice:
        man["notice"] = notice
    if plg_dir and os.path.exists(os.path.join(plg_dir, e["id"] + ".plg")):
        plg = os.path.join(plg_dir, e["id"] + ".plg")
        man["files"]["plg"] = {"name": e["id"] + ".plg", "size": os.path.getsize(plg), "sha256": wc.sha(plg)}
    return man, len(data), notice


def verify_plg(t, d):
    """Rebuild every insert .plg from the table's flags and compare it byte for byte with build_plugs.sh's output in d."""
    tc = os.environ.get("TOOLCHAIN")
    if not tc:
        sys.exit("set TOOLCHAIN to the arm-none-eabi- prefix")
    env = dict(os.environ, TOOLCHAIN=tc)
    fail = 0
    with tempfile.TemporaryDirectory(prefix="insplg") as tmp:
        for e in t["inserts"]:
            ref = os.path.join(d, e["id"] + ".plg")
            if not os.path.exists(ref):
                print("%-8s skipped (no %s)" % (e["id"], ref))
                continue
            plg = os.path.join(tmp, e["id"] + ".plg")
            cmd = [sys.executable, "plug/build_plug.py", "--id", e["plug_id"], "--kind", "master", "--version", e["version"], "--opt", e["plg_opt"]]
            for k, v in e.get("plg_src_opt", {}).items():
                cmd += ["--src-opt", "%s=%s" % (k, v)]
            for i in e["includes"]:
                cmd += ["-I", i]
            cmd += ["--out", plg] + e["sources"]
            wc.run(cmd, SO, env)
            same = open(plg, "rb").read() == open(ref, "rb").read()
            fail += not same
            print("%-8s %s  table-built .plg %s the build_plugs.sh one" % (e["id"], "ok  " if same else "FAIL", "equals" if same else "DIFFERS from"))
    return 1 if fail else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--which", default="public", choices=("public", "private", "all"))
    ap.add_argument("--only")
    ap.add_argument("--engines-plg", help="the finished engine .plg set (build_set.py): its sha256 goes into the manifests")
    ap.add_argument("--inserts-plg", help="the build_plugs.sh output directory: the same for the inserts")
    ap.add_argument("--emsdk", default=os.environ.get("EMSDK") or next((p for p in (os.path.join(ROOT, "agt", "emsdk"), os.path.expanduser("~/prj/poly/agt/emsdk")) if os.path.isdir(p)), "emsdk"),
                    help="emsdk 6.0.13 (default: $EMSDK, <repo>/agt/emsdk, ~/prj/poly/agt/emsdk)")
    ap.add_argument("--wasm-abi-major", type=int, help="claim another module ABI major (refusal test only)")
    ap.add_argument("--verify-plg", metavar="DIR")
    a = ap.parse_args()
    t = build_set.table()
    check_table(t)
    if a.verify_plg:
        return verify_plg(t, a.verify_plg)
    if not a.out:
        ap.error("--out is required")
    w = json.load(open(os.path.join(HERE, "wasm.json")))
    env, up = wc.emsdk_env(a.emsdk)
    emver = wc.run([os.path.join(up, "emcc"), "--version"], ROOT, env).splitlines()[0]
    if w["emcc"] not in emver:
        sys.exit("wasm.json pins emcc %s, found: %s" % (w["emcc"], emver))
    maj, mi, _ = build_set.abi_of(os.path.join(SO, "plug", "plug_abi.h"))
    wtext = open(os.path.join(SO, "plug", "plug_wasm.h")).read()
    wabi = (int(wtext.split("#define PLUG_WASM_ABI_MAJOR")[1].split()[0]), int(wtext.split("#define PLUG_WASM_ABI_MINOR")[1].split()[0]))
    out = os.path.abspath(a.out)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    want = lambda r: (a.which == "all" or r["visibility"] == a.which) and (not a.only or r["id"] in a.only.split(","))
    engines = [e for e in t["engines"] if want(e)]
    inserts = [e for e in t["inserts"] if want(e)]
    recs, notices, licences = [], [], set()
    with tempfile.TemporaryDirectory(prefix="modwasm") as tmp:
        sd = os.path.join(tmp, "stage")
        build_set.stage(ROOT, sd)
        for e in engines:
            man, size, notice = build_engine(e, t, w, sd, tmp, out, env, emver, (maj, mi), wabi, a.wasm_abi_major, a.engines_plg)
            recs.append(man)
            notices.append((e["id"], notice))
            licences.update(e["licence_files"])
            print("%-7s engine %-7s %6d B  %s" % (e["id"], e["visibility"], size, ",".join(man["custom_sections"]) or "none"))
        for e in inserts:
            man, size, notice = build_insert(e, w, tmp, out, env, emver, (maj, mi), wabi, a.wasm_abi_major, a.inserts_plg)
            recs.append(man)
            notices.append((e["id"], notice))
            licences.update(e["licence_files"])
            print("%-7s insert %-7s %6d B  %s" % (e["id"], e["visibility"], size, ",".join(man["custom_sections"]) or "none"))
    for m in recs:
        with open(os.path.join(out, m["files"]["wasm"]["name"].replace(".wasm", ".json")), "w") as f:
            json.dump(m, f, indent=1)
            f.write("\n")
    for lf in sorted(licences):
        shutil.copy(os.path.join(ROOT, lf), os.path.join(out, os.path.basename(lf)))
    with open(os.path.join(out, "NOTICE.md"), "w") as f:
        f.write("# Notices for the browser modules in this directory\n\nShip every module with this file and the licence texts it names.\n\n")
        for i, n in notices:
            f.write("## %s\n\n%s\n\n" % (i, n or "Own code of this project, MIT licence (synth-open/plug/LICENSE)."))
    ext = [{"id": r["id"], "kind": "synth" if "stored" in r else "master", "name": r.get("name", r["id"]), "repo": r["repo"]} for r in t["engines"] + t["inserts"]
           if r["visibility"] == "private" and a.which == "public"]
    with open(os.path.join(out, "MODULES.json"), "w") as f:
        json.dump({"set": "tracker-ff-modules", "set_version": t["set_version"], "abi": "%d.%d" % (maj, mi), "wasm_abi": "%d.%d" % wabi, "emcc": emver,
                   "which": a.which,
                   "modules": [{"id": m["files"]["wasm"]["name"][:-5], "plug_id": m["id"], "kind": m["kind"], "name": m["name"], "version": m["version"],
                                "visibility": m["visibility"], "licence": m["licence"], "wasm": m["files"]["wasm"]} for m in recs],
                   "external_private": ext}, f, indent=1)
        f.write("\n")
    with open(os.path.join(out, "SHA256SUMS"), "w") as f:
        for p in sorted(os.listdir(out)):
            if p != "SHA256SUMS":
                f.write("%s  %s\n" % (wc.sha(os.path.join(out, p)), p))
    print("%s: %d modules (%d engines, %d inserts), %s" % (out, len(recs), len(engines), len(inserts), a.which))
    return 0


if __name__ == "__main__":
    sys.exit(main())
