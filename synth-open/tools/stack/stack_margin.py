#!/usr/bin/env python3
"""Static stack margin of a firmware image (MK66, one stack for the main loop and every interrupt) and the release gate on it.

    TOOLCHAIN=.../arm-none-eabi- python3 stack_margin.py IMAGE_DIR [--su DIR ...] [--plugin-depths FILE] [--min-margin 512]
                                 [--ptf IMAGE.ptf --stamp FILE] [--json FILE] [--strict-recursion] [--census-print]

IMAGE_DIR holds PolyendTracker.axf and PolyendTracker.map of one build. Frames come from the disassembly (a prologue scan that agrees
with GCC's .su on every function both cover), --su adds the .su files.

Inputs next to this script (all tracked, each line has its reason):
    stack_resolve_fs.txt     indirect call rules of the firmware (virtual calls, ISR trampolines, the SD driver)
    stack_isr_levels.txt     every interrupt of the image with its NVIC level, read from the code, and the census that checks it
    stack_vla_bounds.txt     the dynamic frames (VLA) and their payload in bytes, added to the frame of the function
    stack_recursion.txt      recursion bounds (SdCard::delete_node)
    stack_plugin_depths.txt  deepest stack of each synth plugin entry (the plugin ELFs are not in this repository)

The formula (derivation and evidence in the release notes):

    room    = STACK_MARGIN + stack in use at the last _sbrk of Profiler::init
              Profiler::init mallocs until _sbrk refuses, so the heap line is SP(at that call) - STACK_MARGIN, below it up to
              _estack is the stack. A lower bound: the last grant may sit up to about 32 B under the line.
              STACK_MARGIN is read from mk20dx128.c (the MK66 value) and the makefiles are checked for a -DSTACK_MARGIN override.
    margin  = room - (main loop + audio out ISR chain + 108 + nested interrupts)
    108     = exception frame with FPU lazy stacking: 8 words + 18 FPU words = 104 B, plus 4 B for the 8 byte alignment.
    realistic: main loop without the SD timeout tail, nested = the handlers stronger than audio out (one per level)
    static   : main loop with the timeout tail, nested = one handler per level of every level (the weaker ones, SDHC, USB, I2C, sit
               below audio out and can be running when it fires)
    The nest is built twice, USB at level 7 (USB audio off) and at level 4 (USB audio on, usb_dev.c:1289). Every row is gated on the
    worse of the two.

One stack for everything: the image has no PSP (msr psp), FPCCR keeps its reset value (ASPEN and LSPEN set).
Exit status 1 when a row of the release set (every plugin not marked optional) is below --min-margin at the static bound, when the
interrupt census, the dynamic frame bounds or the STACK_MARGIN check fail, or when the walker finds no root.
"""
import argparse
import collections
import hashlib
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
WALK = os.path.join(HERE, "stack_depth.py")
EXC = 108
MIDI_TX = 150   # the MIDI transmit call under the clock timer: an estimate (E), the call goes through a pointer
MODES = ("usb_audio=off", "usb_audio=on")
# source census: what counts as a line that sets a priority, enables an interrupt or writes a vector
SRC_RX = re.compile(r"\b(NVIC_SET_PRIORITY|NVIC_ENABLE_IRQ|NVIC_SetPriority|NVIC_EnableIRQ|EnableIRQ|attachInterruptVector|attachInterrupt|"
                    r"setInterruptPriority|interruptPriority|SCB_SHPR\d)\b|_VectorsRam\s*\[[^\]]*\]\s*=(?!=)|\.priority\s*\(")
SRC_TOPS = ("platforms/mk66", "core", "synth-open", "util")
SRC_SKIP_DIRS = ("tools", "ui_shot", ".git")


def read_lines(path):
    """(text before the comment, comment) of every non empty line"""
    out = []
    for n, line in enumerate(open(path), 1):
        body, _, cm = line.partition("#")
        if body.strip():
            out.append((n, body.split(), cm.strip()))
    return out


def load_levels(path):
    isr, off, src = [], [], {}
    for n, f, cm in read_lines(path):
        if f[0] == "isr":
            lv = {"": None}
            parts = f[2].split(",")
            lv = {"usb_audio=off": int(parts[0]), "usb_audio=on": int(parts[0])}
            for extra in parts[1:]:
                k, v = extra.split("=")
                if k != "usb_audio":
                    sys.exit("%s:%d: unknown level condition %s" % (path, n, k))
                lv["usb_audio=on"] = int(v)
            vec, ram = None, False
            for x in f[4:]:
                if x == "ram":
                    ram = True
                elif x.startswith("vec="):
                    vec = x[4:]
            if not vec and not ram:
                sys.exit("%s:%d: isr %s needs vec= or ram" % (path, n, f[1]))
            isr.append({"name": f[1], "level": lv, "rx": f[3], "vec": vec, "ram": ram, "why": cm})
        elif f[0] == "off":
            off.append({"name": f[1], "vec": f[2], "why": cm})
        elif f[0] == "src":
            src[f[2]] = int(f[1])
        else:
            sys.exit("%s:%d: unknown row kind %s" % (path, n, f[0]))
    return isr, off, src


def load_bounds(path):
    """(bytes, regex, comment, optional): an optional line is for a function that only some compilers link (library code)"""
    out = []
    for n, f, cm in read_lines(path):
        out.append((int(f[0]), f[1], cm, "optional" in f[2:]))
    return out


def load_recursion(path):
    out = []
    for n, f, cm in read_lines(path):
        if f[0] != "recur":
            sys.exit("%s:%d: unknown row kind %s" % (path, n, f[0]))
        kv = dict(x.split("=") for x in f[2:])
        out.append((f[1], int(kv["structural"]), int(kv["assumed"])))
    return out


def load_depths(path):
    plugs = []
    for line in open(path):
        p = line.split("#")[0].split()
        if len(p) >= 2 and p[1].isdigit():
            plugs.append((p[0], int(p[1]), len(p) > 2 and p[2] == "optional"))
    plugs.sort(key=lambda x: -x[1])
    return plugs


def stack_margin_from_source(root):
    """STACK_MARGIN of the MK66 branch of mk20dx128.c, and every makefile line that would override it with -DSTACK_MARGIN"""
    src = open(os.path.join(root, "platforms/mk66/3rdparty/teensy_core/mk20dx128.c"), errors="replace").read()
    m = re.search(r"defined\(__MK66FX1M0__\)\s*\n\s*#define\s+STACK_MARGIN\s+(\d+)", src)
    if not m:
        sys.exit("STACK_MARGIN of the MK66 branch not found in mk20dx128.c")
    over = []
    for mk in ("Makefile", "core/makefile", "platforms/mk66/makefile", "synth-open/engines.mk", "version.mk"):
        p = os.path.join(root, mk)
        if os.path.isfile(p):
            for n, line in enumerate(open(p, errors="replace"), 1):
                if re.search(r"-DSTACK_MARGIN|^\s*(export\s+)?STACK_MARGIN\s*[:?+]?=", line):
                    over.append("%s:%d: %s" % (mk, n, line.strip()))
    return int(m.group(1)), over


def source_census(root):
    res = collections.Counter()
    for top in SRC_TOPS:
        for dp, dn, fn in os.walk(os.path.join(root, top)):
            dn[:] = [d for d in dn if not d.startswith("build") and d not in SRC_SKIP_DIRS]
            for f in fn:
                if not f.endswith((".c", ".cpp", ".h", ".hpp", ".inc")):
                    continue
                p = os.path.join(dp, f)
                for line in open(p, errors="replace"):
                    s = line.strip()
                    if s.startswith(("#define", "//", "*", "/*")):
                        continue
                    if SRC_RX.search(line):
                        res[os.path.relpath(p, root)] += 1
    return res


def vector_table(img, toolchain):
    """slot -> list of symbol names at the handler address, from the vector table at address 0 of the axf"""
    axf = os.path.join(img, "PolyendTracker.axf")
    out = subprocess.run([toolchain + "objdump", "-s", "--start-address=0", "--stop-address=0x400", axf], capture_output=True, text=True, check=True).stdout
    words = []
    for line in out.splitlines():
        m = re.match(r"^\s*([0-9a-f]+) ((?:[0-9a-f]{8} ?){1,4})", line)
        if m:
            words += [int.from_bytes(bytes.fromhex(w), "little") for w in m.group(2).split()]
    nm = subprocess.run([toolchain + "nm", axf], capture_output=True, text=True, check=True).stdout
    sym = collections.defaultdict(list)
    for line in nm.splitlines():
        p = line.split(None, 2)
        if len(p) == 3 and p[1] in "tTwWiI":
            sym[int(p[0], 16)].append(p[2].strip())
    return {i: sym.get(w & ~1, []) for i, w in enumerate(words[:16 + 95])}


def census(img, toolchain, root, levels_path, isr, off, src, fails):
    """The image against the table: every non default flash vector has a row, every row exists. The sources against the src rows."""
    vt = vector_table(img, toolchain)
    default = {"unused_isr", "fault_isr"}
    rows = [(r["name"], re.compile(r["vec"]), "isr") for r in isr if r["vec"]] + [(r["name"], re.compile(r["vec"]), "off") for r in off]
    seen = set()
    nvec = 0
    for slot, names in sorted(vt.items()):
        if slot in (0, 1) or not names or default & set(names):
            continue
        nvec += 1
        hit = [n for n, rx, k in rows if any(rx.search(x) for x in names)]
        if not hit:
            fails.append("census: vector %d (IRQ %d) runs %s and has no row in stack_isr_levels.txt: find where it is enabled and at what level, then add an isr or off row" % (slot, slot - 16, "/".join(names[:3])))
        seen.update(hit)
    for n, rx, k in rows:
        if n not in seen:
            fails.append("census: row %s %s matches no flash vector of the image (stale row, or the handler is gone)" % (k, n))
    return nvec, vt


def check_rules(rules, img, toolchain, fails):
    """Every caller regex of the resolve rules must match a function of the image: a renamed or removed function would drop its rule without
    a sound (plug::start, 2026-11-15). A rule that may match nothing in the release image (CHIP built in) sits under a '# optional-rule' comment."""
    nm = subprocess.run([toolchain + "nm", "-C", "--defined-only", os.path.join(img, "PolyendTracker.axf")], capture_output=True, text=True, check=True).stdout
    names = [l.split(None, 2)[2] for l in nm.splitlines() if len(l.split(None, 2)) == 3]
    opt = False
    for n, line in enumerate(open(rules), 1):
        s = line.strip()
        if not s or s.startswith("#"):
            opt = "optional-rule" in s
            continue
        rx = re.compile(s.split("=>")[0].strip())
        if not opt and not any(rx.search(x) for x in names):
            fails.append("%s:%d: the caller regex %s matches no function of the image (renamed or removed: fix the rule, or mark it '# optional-rule' above it)" % (
                os.path.basename(rules), n, rx.pattern[:80]))


def walker(img, su, extra_args, rules):
    def w(*x):
        cmd = [sys.executable, WALK, os.path.join(img, "PolyendTracker.axf"), "--map", os.path.join(img, "PolyendTracker.map"),
               "--resolve", rules]
        for s in su:
            cmd += ["--su", s]
        r = subprocess.run(cmd + extra_args + list(x), capture_output=True, text=True)
        return r.stdout + r.stderr
    return w


def root_depth(out, which=None):
    d = [int(m.group(1)) for m in re.finditer(r"^root .* depth\s+(\d+) B", out, re.M)]
    if not d:
        sys.exit("FAIL: the walker found no root:\n" + out[-600:])
    return max(d) if which is None else d[which]


def nest_of(isr, own, mode, audio_level):
    """one handler per level, a level cannot preempt itself. Stronger than audio out: above it (always counted). Weaker: below it (static)."""
    above, below = collections.defaultdict(int), collections.defaultdict(int)
    for r in isr:
        if r["name"] == "audio_out":
            continue
        lv = r["level"][mode]
        tgt = above if lv < audio_level else below
        tgt[lv] = max(tgt[lv], own[r["name"]])
    return sum(above.values()), sum(below.values()), dict(above), dict(below)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--plugin-depths", default=os.path.join(HERE, "stack_plugin_depths.txt"))
    ap.add_argument("--isr-levels", default=os.path.join(HERE, "stack_isr_levels.txt"))
    ap.add_argument("--vla-bounds", default=os.path.join(HERE, "stack_vla_bounds.txt"))
    ap.add_argument("--recursion", default=os.path.join(HERE, "stack_recursion.txt"))
    ap.add_argument("--resolve", default=os.path.join(HERE, "stack_resolve_fs.txt"), help="indirect call rules (tests of a rule)")
    ap.add_argument("--root", default=ROOT, help="the source tree of the image (census, STACK_MARGIN)")
    ap.add_argument("--su", action="append", default=[])
    ap.add_argument("--optional", default="", help="more plugins that are not in the release set (reported, not gated)")
    ap.add_argument("--insert-depth", type=int, default=664)
    ap.add_argument("--stack-margin", type=int, help="STACK_MARGIN, default: read from mk20dx128.c")
    ap.add_argument("--min-margin", type=int, default=512)
    ap.add_argument("--strict-recursion", action="store_true", help="gate on the structural recursion depth instead of the assumed one")
    ap.add_argument("--census-print", action="store_true", help="print the src rows of the current sources and exit")
    ap.add_argument("--json", help="write the numbers to FILE")
    ap.add_argument("--ptf", help="the image the axf was linked into (its sha256 goes into the stamp)")
    ap.add_argument("--stamp", help="write the gate result (PASS or FAIL, sha256 of --ptf) to FILE, package_release.sh reads it")
    ap.add_argument("--no-census", action="store_true", help="skip the interrupt census (tests that run on a synthetic tree)")
    a = ap.parse_args()
    if not os.environ.get("TOOLCHAIN"):
        sys.exit("TOOLCHAIN (the arm-none-eabi- prefix) is not set")
    tc = os.environ["TOOLCHAIN"]
    if a.census_print:
        for p, c in sorted(source_census(a.root).items()):
            print("src %d %s" % (c, p))
        return
    if a.stamp and os.path.exists(a.stamp):
        os.remove(a.stamp)   # a stale PASS must never outlive a run that did not finish
    fails = []

    isr, off, src = load_levels(a.isr_levels)
    bounds = load_bounds(a.vla_bounds)
    recs = load_recursion(a.recursion)
    plugs = load_depths(a.plugin_depths)
    opt = set(a.optional.split(",")) if a.optional else set()

    # STACK_MARGIN of the source and the check that nothing overrides it
    sm_src, over = stack_margin_from_source(a.root)
    stack_margin = a.stack_margin or sm_src
    if over:
        fails.append("STACK_MARGIN is mentioned in a makefile (the room would not be the source value): " + " | ".join(over))
    if a.stack_margin and a.stack_margin != sm_src:
        fails.append("--stack-margin %d differs from mk20dx128.c (%d)" % (a.stack_margin, sm_src))

    # interrupt census: the image against the table, the sources against the src rows
    nvec = 0
    if not a.no_census:
        nvec, _ = census(a.image, tc, a.root, a.isr_levels, isr, off, src, fails)
        now = source_census(a.root)
        for p in sorted(set(now) | set(src)):
            if now.get(p, 0) != src.get(p, 0):
                fails.append("census: %s has %d interrupt setup lines, stack_isr_levels.txt says %d: read the change, fix the isr/off rows, then update the src row (--census-print)" % (p, now.get(p, 0), src.get(p, 0)))

    base = []
    for nbytes, rx, _, _ in bounds:
        base += ["--payload", "%s=%d" % (rx, nbytes)]
    assumed = base[:]
    structural = base[:]
    for rx, st, asm in recs:
        assumed += ["--recur", "%s=%d" % (rx, asm)]
        structural += ["--recur", "%s=%d" % (rx, st)]
    gate_args = structural if a.strict_recursion else assumed
    check_rules(a.resolve, a.image, tc, fails)
    w = walker(a.image, a.su, gate_args, a.resolve)
    ws = walker(a.image, a.su, structural, a.resolve)

    # dynamic frames: every one needs a bound, every bound a dynamic frame
    dyn = w("--dyn-list")
    nodyn = [l for l in dyn.splitlines() if l.startswith("dynamic")]
    for l in nodyn:
        if l.rstrip().endswith("NO-PAYLOAD"):
            fails.append("dynamic frame without a bound in stack_vla_bounds.txt: " + l.split("B ", 1)[-1].strip())
    for l in dyn.splitlines():
        if l.startswith("payload-unused") and not any(o and "%s=%d" % (rx, nb) == l.split(None, 1)[1] for nb, rx, _, o in bounds):
            fails.append("stack_vla_bounds.txt line matches no dynamic frame of the image (stale): " + l.split(None, 1)[1])
    if not nodyn and not dyn.strip():
        fails.append("the walker listed no dynamic frames (bounds file not applied?)")

    # boot: the stack in use at the last _sbrk of Profiler::init
    to = re.search(r"entered.*?: (\d+)", w("--root", r"^ResetHandler$", "--to", r"^Profiler::init\(\)$"))
    if not to:
        fails.append("the walker could not place Profiler::init under ResetHandler")
        to_n = 0
    else:
        to_n = int(to.group(1))
    boot = to_n + root_depth(w("--root", r"^Profiler::init\(\)$"))
    room = stack_margin + boot

    main_static = root_depth(w("--root", r"^loop$"))
    main_real = root_depth(w("--root", r"^loop$", "--cut", r"^_printf_float$", "--cut", r"^restartMCU$"))
    main_struct = root_depth(ws("--root", r"^loop$")) if gate_args is not structural else main_static
    own = {}
    for r in isr:
        if r["name"] != "audio_out":
            own[r["name"]] = root_depth(w("--root", r["rx"])) + EXC
    own["midi_clock"] += MIDI_TX
    audio_level = [r for r in isr if r["name"] == "audio_out"][0]["level"]["usb_audio=off"]
    nests = {}
    for m in MODES:
        ab, bl, ab_d, bl_d = nest_of(isr, own, m, audio_level)
        nests[m] = {"real": ab, "static": ab + bl, "above": ab_d, "below": bl_d}
    nest_real = max(n["real"] for n in nests.values())
    nest_static = max(n["static"] for n in nests.values())

    isr_rx = [r for r in isr if r["name"] == "audio_out"][0]["rx"]
    slot = r"^plug::\(anonymous namespace\)::PlugSynth::(render|prepare|slotPost|setSendTap|setTempo|noteOn|noteOff|slotReset)\("
    builtin = root_depth(w("--root", isr_rx))
    cache = {}

    def chain_of(d, wk):
        k = (id(wk), d)
        if k not in cache:
            cache[k] = root_depth(wk("--extra", slot + "=%d" % d, "--root", isr_rx))
        return cache[k]

    ins = root_depth(w("--extra", r"^AudioEffectPlug::update\(\)$=%d" % a.insert_depth, "--root", r"^AudioEffectPlug::update\(\)$"))

    # report
    print("room        %6d B   (STACK_MARGIN %d from mk20dx128.c + stack in use at the last _sbrk %d)" % (room, stack_margin, boot))
    print("floor       %6d B   (the weakest static margin of the release set must stay above it)" % a.min_margin)
    print()
    print("%-22s %5s %9s   %s" % ("entry point", "level", "worst B", "note"))
    print("%-22s %5s %9d   %s" % ("main loop (static)", "-", main_static, "with the SD timeout tail, recursion at the counted depth"))
    print("%-22s %5s %9d   %s" % ("main loop (realistic)", "-", main_real, "no SD timeout tail"))
    if main_struct != main_static:
        print("%-22s %5s %9d   %s" % ("main loop (structural)", "-", main_struct, "recursion at the most the code allows, see the open finding below"))
    for r in isr:
        lv = r["level"]
        lvs = str(lv["usb_audio=off"]) if lv["usb_audio=off"] == lv["usb_audio=on"] else "%d/%d" % (lv["usb_audio=off"], lv["usb_audio=on"])
        if r["name"] == "audio_out":
            print("%-22s %5s %9s   %s" % ("audio out ISR", lvs, "chain", "per plugin, below"))
        else:
            print("%-22s %5s %9d   %s" % (r["name"], lvs, own[r["name"]], "own depth + %d B exception frame" % EXC + (" + %d B MIDI send call (E)" % MIDI_TX if r["name"] == "midi_clock" else "")))
    print()
    for m in MODES:
        n = nests[m]
        print("nest %-14s realistic %5d B (stronger than audio out: %s)   static %5d B (+ weaker: %s)" % (
            m, n["real"], ", ".join("L%d %d" % kv for kv in sorted(n["above"].items())), n["static"], ", ".join("L%d %d" % kv for kv in sorted(n["below"].items()))))
    print("nest used   %5d B realistic, %d B static (the worse of both modes)" % (nest_real, nest_static))
    print()

    def margins(chain, mr, ms, nest_r, nest_s):
        return room - (mr + chain + EXC + nest_r), room - (ms + chain + EXC + nest_s)

    rows = [("built-in", 0, builtin, False)]
    for n, d, o in plugs:
        rows.append((n, d, chain_of(d, w), o or n in opt))
    print("%-10s %7s %7s %9s %9s   %s" % ("audio ISR", "plugin", "chain", "realistic", "static", "(worse of both USB modes)"))
    bad = 0
    worst = None
    out_rows = []
    for n, d, chain, o in rows:
        mr = min(margins(chain, main_real, main_static, nests[m]["real"], nests[m]["static"])[0] for m in MODES)
        ms = min(margins(chain, main_real, main_static, nests[m]["real"], nests[m]["static"])[1] for m in MODES)
        flag = ""
        if o:
            flag = "  optional, not in the release set"
        elif ms < a.min_margin:
            flag = "  BELOW %d" % a.min_margin
            bad += 1
        else:
            if worst is None or ms < worst[0]:
                worst = (ms, n)
        out_rows.append({"name": n, "plugin": d, "chain": chain, "realistic": mr, "static": ms, "optional": o})
        print("%-10s %7s %7d %+9d %+9d%s" % (n, d or "-", chain, mr, ms, flag))
    print("inserts     (deepest %d B in AudioEffectPlug::update) chain %d B" % (a.insert_depth, ins))
    deep = max((d for n, d, c, o in rows if not o), default=0)
    deep_chain = max((c for n, d, c, o in rows if not o), default=builtin)
    prefix = chain_of(deep, w) - deep if deep else 0
    budget = room - main_static - EXC - nest_static - a.min_margin - prefix
    print("plugin budget  %d B for a plugin entry at the floor (deepest of the release set: %d B)" % (budget, deep))
    print("plugin depths  %s (E, %s)" % (os.path.relpath(a.plugin_depths, a.root) if a.plugin_depths.startswith(a.root) else a.plugin_depths,
                                          "measured with plug_depth.sh from the module ELFs, see the header of the file"))

    # recursion: the gate counts the assumed depth, the structural worst case is printed every time
    open_findings = []
    for rx, st, asm in recs:
        if st != asm:
            ms_struct = min(room - (main_struct + c + EXC + nest_static) for n, d, c, o in rows if not o)
            open_findings.append("OPEN FINDING recursion %s: the code allows %d levels, the gate counts %d (WAIVER, stack_recursion.txt). At %d levels the main loop is %d B and the weakest static margin %+d B (%s)" % (
                rx, st, asm, st, main_struct, ms_struct, "gate" if a.strict_recursion else "not gated, --strict-recursion gates on it"))
    for f in open_findings:
        print(f)

    out_loop = w("--root", r"^loop$")
    for m in re.finditer(r"^recursion bound (.*)$", out_loop, re.M):
        print("recursion   " + m.group(0))
    cyc = re.findall(r"recursion cut at: (.*)", out_loop)
    if cyc:
        bounded = [re.compile(rx) for rx, _, _ in recs]
        rest = [c for c in cyc[0].split("; ") if not any(b.search(c) for b in bounded)]
        print("recursion cut once, not bounded here (small cycles and walker artefacts, the design report 9.6 lists them): %s" % "; ".join(rest)[:400])

    if bad:
        fails.append("%d row(s) of the release set below %d B at the static bound" % (bad, a.min_margin))
    weakest = min(room - (main_static + c + EXC + nest_static) for n, d, c, o in rows if not o) if rows else 0
    print("\ncensus      %s" % ("skipped" if a.no_census else "%d interrupt vectors in the image, %d isr rows, %d off rows, %d source files" % (nvec, len(isr), len(off), len(src))))
    print("dynamic     %d dynamic frames in the image, payload bytes in the table: %d B" % (len(nodyn), sum(b[0] for b in bounds)))
    if fails:
        print("\nRESULT FAIL")
        for f in fails:
            print("  FAIL: " + f)
    else:
        print("\nrelease set: OK, weakest static margin %+d B at the floor %d B" % (min(weakest, worst[0] if worst else weakest), a.min_margin))
        print("RESULT PASS")

    result = {"result": "FAIL" if fails else "PASS", "room": room, "stack_margin": stack_margin, "boot": boot, "floor": a.min_margin,
              "main_static": main_static, "main_real": main_real, "main_structural": main_struct, "exception": EXC,
              "nest": nests, "handlers": own, "rows": out_rows, "inserts_chain": ins, "plugin_budget": budget,
              "weakest_static": weakest, "failures": fails, "open_findings": open_findings}
    if a.json:
        json.dump(result, open(a.json, "w"), indent=1, sort_keys=True)
    if a.stamp:
        sha = hashlib.sha256(open(a.ptf, "rb").read()).hexdigest() if a.ptf and os.path.isfile(a.ptf) else ""
        axf = hashlib.sha256(open(os.path.join(a.image, "PolyendTracker.axf"), "rb").read()).hexdigest()
        with open(a.stamp, "w") as f:
            f.write("%s ptf_sha256=%s axf_sha256=%s weakest_static=%d floor=%d\n" % (result["result"], sha, axf, weakest, a.min_margin))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
