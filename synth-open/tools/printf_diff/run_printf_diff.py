# Driver of run_printf_diff.sh. Run with python -I.
#   --check-sites ROOT formats.txt : every float conversion spec in the firmware sources is in formats.txt
#   TC old.elf new.elf formats.txt OUT : run both images under Unicorn over the value sweep and diff
import os, random, re, struct, subprocess, sys

SPEC = re.compile(r'%[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*)?)?[lL]?[feEgGaA]')

def read_formats(path):
    rows = []
    for line in open(path, encoding="utf-8").read().split("\n"):
        if not line or line.startswith("#"): continue
        g, fmt, where = (line.split("\t") + [""])[:3]
        rows.append((g, fmt, where))
    return rows

def check_sites(root, fpath):
    known = {m for _, f, _ in read_formats(fpath) for m in SPEC.findall(f)}
    dirs = ["core", "platforms/mk66", "synth-open", "synth", "engines", "editor-fx-sdk"]
    skip = ("/3rdparty/", "/build", "/tests/", "/tools/", "/ref/", "data_waveforms.c")
    missing = []
    for d in dirs:
        for dp, _, fs in os.walk(os.path.join(root, d)):
            if any(s in dp + "/" for s in skip): continue
            for f in fs:
                if not f.endswith((".c", ".cpp", ".h", ".hpp")): continue
                p = os.path.join(dp, f)
                if any(s in p for s in skip): continue
                for n, line in enumerate(open(p, encoding="utf-8", errors="replace"), 1):
                    if line.lstrip().startswith("//"): continue
                    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', line):
                        for m in SPEC.findall(lit):
                            if m not in known: missing.append((os.path.relpath(p, root), n, m))
    if missing:
        for p, n, m in missing: print("printf_diff: float format %s at %s:%d is not in formats.txt (add it and rerun)" % (m, p, n))
        sys.exit(2)
    print("printf_diff: sites ok, %d distinct specs in formats.txt cover every float format in the sources" % len(known))

def f32(x):
    try: return struct.unpack("<f", struct.pack("<f", x))[0]
    except OverflowError: return float("inf") if x > 0 else float("-inf")

def bits(b): return struct.unpack("<d", struct.pack("<Q", b))[0]

def sweep(seed=12345):
    """(kind, value). kind 'f' = a float promoted to double, as every firmware call site passes, 'd' = any double."""
    rng = random.Random(seed)
    v = []
    add = lambda k, x: v.append((k, x))
    for x in (0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 1.5, 2.5, -2.5, 0.25, 0.125, 0.375, 0.0625, 100.0, 1e-5, 1e-9, 1e9, 1e10, 4294967296.0, 18446744073709551616.0):
        add("f", f32(x)); add("d", x)
    for x in (float("inf"), float("-inf"), float("nan"), -float("nan")): add("f", x); add("d", x)
    add("d", bits(0x7ff8000000000001)); add("d", bits(0xfff0000000000001))   # payload NaN, signalling NaN
    for x in (1.17549435e-38, 1.4e-45, 3.4028235e38, -3.4028235e38, 5e-324, 2.2250738585072014e-308, 1.7976931348623157e308): add("f", f32(x)); add("d", x)
    # rounding edges: k.5, 0.005, 0.015, 0.025 ... at 1 to 3 decimals, as float and as double, and both neighbours
    for dec in (0, 1, 2, 3, 4):
        s = 10 ** dec
        for n in list(range(-300, 301)) + [9999, 99999, 999999, 12345678, -99999]:
            t = (n + 0.5) / s
            for k, x in (("f", f32(t)), ("d", t)): add(k, x)
    for x in (0.005, 0.015, 0.025, 0.035, 0.045, 0.055, 0.995, 0.9949999, 0.9950001, 9.995, 99.995, 0.0049999, 0.0050001, 0.0005, 0.0015, 0.9995, 2.675, 1.005, 8.345, 0.045, 1.0e-3, 2.5e-3, 12.5, 0.1, 0.2, 0.3, 0.7):
        for y in (x, -x): add("f", f32(y)); add("d", y)
    # integers and tenths, hundredths, thousandths, eighths, sixteenths (what sliders and dB steps produce)
    for n in range(-2500, 2501):
        for d in (1, 10, 100, 1000, 8, 16, 64, 127, 128, 3, 7):
            add("f", f32(n / d))
    for n in range(-12000, 4001): add("f", f32(n / 100.0))      # dB range
    for n in range(0, 100001, 7): add("f", f32(n / 1000.0))      # seconds, grain position
    for n in range(0, 100001, 3): add("f", f32(n / 10.0)); add("f", f32(n * 0.01))
    for n in range(20, 300): add("f", f32(n * 1.0)); add("f", f32(n * 0.5)); add("f", f32(n / 1.7))   # tempo
    # next and previous float of the ties
    for dec in (1, 2, 3):
        for n in range(-200, 201):
            t = f32((n + 0.5) / 10 ** dec)
            b = struct.unpack("<I", struct.pack("<f", t))[0]
            for nb in (b - 1, b + 1):
                try: add("f", struct.unpack("<f", struct.pack("<I", nb & 0xffffffff))[0])
                except struct.error: pass
    # random float bit patterns over exponents -40 .. 63, and random doubles
    for _ in range(9000):
        e = rng.randint(-40, 63); m = rng.getrandbits(23)
        x = f32(((1 << 23) | m) * 2.0 ** (e - 23))
        add("f", x if rng.random() < .5 else -x)
    for _ in range(3000):
        e = rng.randint(-60, 70); m = rng.getrandbits(52)
        x = ((1 << 52) | m) * 2.0 ** (e - 52)
        add("d", x if rng.random() < .5 else -x)
    for _ in range(2000):   # human numbers, a few digits
        x = round(rng.uniform(-200, 5000), rng.randint(0, 5)); add("f", f32(x)); add("d", x)
    for _ in range(1500):   # large floats
        x = f32(rng.uniform(1, 10) * 10.0 ** rng.randint(5, 38)); add("f", x if rng.random() < .5 else -x)
    seen, out = set(), []
    for k, x in v:
        key = (k, struct.pack("<d", x))
        if key not in seen: seen.add(key); out.append((k, x))
    return out

def main():
    from unicorn import UC_ARCH_ARM, UC_MODE_THUMB, Uc, UcError
    from unicorn.arm_const import (UC_CPU_ARM_CORTEX_A15, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0,
                                   UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_SP)
    tc, old, new, fpath, outdir = sys.argv[1:6]
    rows = read_formats(fpath)
    values = sweep()
    BASE, SIZE, TRAP = 0x20000000, 0x800000, 0x20000000 + 0x7F0000
    CASES, OUTS = BASE + 0x300000, BASE + 0x500000
    OUTSZ = 4 + 348
    CH = 512

    def load(elf):
        subprocess.run([tc + "objcopy", "-O", "binary", elf, elf + ".bin"], check=True)
        syms = {}
        for line in subprocess.run([tc + "nm", elf], check=True, capture_output=True, text=True).stdout.splitlines():
            p = line.split()
            if len(p) == 3: syms[p[2]] = int(p[0], 16)
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB); uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
        uc.mem_map(BASE, SIZE); uc.mem_write(BASE, open(elf + ".bin", "rb").read()); uc.mem_write(TRAP, b"\xfe\xe7")
        uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20)); uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        return uc, syms["run"]

    def run_cases(uc, run, cases, fmt_addr):
        # cases: (value, fmt index, size). Returns [(ret, text)].
        res = []
        for lo in range(0, len(cases), CH):
            chunk = cases[lo:lo + CH]
            uc.mem_write(CASES, b"".join(struct.pack("<dII", v, fmt_addr[fi], sz) for v, fi, sz in chunk))
            uc.reg_write(UC_ARM_REG_SP, BASE + SIZE - 0x10000); uc.reg_write(UC_ARM_REG_LR, TRAP | 1)
            uc.reg_write(UC_ARM_REG_R0, CASES); uc.reg_write(UC_ARM_REG_R1, len(chunk)); uc.reg_write(UC_ARM_REG_R2, OUTS)
            try: uc.emu_start(run | 1, TRAP, timeout=300 * 1000000)
            except UcError as e:
                print("printf_diff: emulation stopped at pc=0x%x (%s) in a chunk starting at case %d" % (uc.reg_read(UC_ARM_REG_PC), e, lo)); sys.exit(2)
            raw = bytes(uc.mem_read(OUTS, OUTSZ * len(chunk)))
            for i in range(len(chunk)):
                r, = struct.unpack_from("<i", raw, i * OUTSZ)
                s = raw[i * OUTSZ + 4:(i + 1) * OUTSZ]
                res.append((r, s.split(b"\0")[0] if b"\0" in s else s))
        return res

    # format strings go to the top of the image RAM
    FMT = BASE + 0x2F0000
    fmt_addr, blob = [], b""
    for _, f, _ in rows:
        fmt_addr.append(FMT + len(blob)); blob += f.encode() + b"\0"
    ulo, uhi = float(os.environ.get("PF_FLOAT_LIMIT", "1e9")), None

    # Full sweep once per distinct conversion spec, a fixed sample (SAMPLE values) for the other formats with the same spec (they only differ
    # in the text around it) and for the buffer sizes 0 (sprintf), 12 and 8 (truncation), size 64 is the plain case.
    SAMPLE = int(os.environ.get("PF_SAMPLE", "1500"))
    rng = random.Random(7)
    sample = rng.sample(values, SAMPLE)
    specs_done = set()
    cases, meta = [], []
    for fi, (g, f, _) in enumerate(rows):
        spec = tuple(SPEC.findall(f))
        full = spec not in specs_done
        specs_done.add(spec)
        for kind, x in (values if full else sample):
            cases.append((x, fi, 64)); meta.append((fi, kind, 64))
        for kind, x in sample:
            for sz in ((0, 12, 8) if abs(x) < 1e20 or x != x else (12, 8)):
                cases.append((x, fi, sz)); meta.append((fi, kind, sz))
    # Plugin parameter display: plug_ui.cpp prints a number with "%.0f%s" when step >= 1, else "%.2f%s" (v is a float). The params come from
    # the module json files (PLUG_PARAMS_DIR, e.g. the Dirt / Tape / Glue .json of the release modules), every step value and 400 values between.
    plug_cases, plug_meta = [], []
    pdir = os.environ.get("PLUG_PARAMS_DIR", "")
    plug_fmt = {r[1]: i for i, r in enumerate(rows) if r[1] in ("%.0f%s", "%.2f%s")}
    if pdir:
        import json
        for fn in sorted(os.listdir(pdir)):
            if not fn.endswith(".json"): continue
            try: params = json.load(open(os.path.join(pdir, fn))).get("params", [])
            except ValueError: continue
            for p in params:
                if "enum" in p: continue   # named values print the name, no float conversion
                lo, hi, st = f32(p["min"]), f32(p["max"]), f32(p["step"])
                vals = set()
                k = 0
                while True:
                    x = f32(lo + k * st)
                    if x > hi: break
                    vals.add(x); k += 1
                    if k > 100000: break
                vals.add(hi)
                for j in range(401): vals.add(f32(lo + (hi - lo) * j / 400.0))
                fi = plug_fmt["%.0f%s" if st >= 1.0 else "%.2f%s"]
                for x in sorted(vals):
                    plug_cases.append((x, fi, 64)); plug_meta.append((fn[:-5], p["name"]))
        print("printf_diff: plugin params: %d values of %d params from %s" % (len(plug_cases), len({m for m in plug_meta}), pdir))
    results = {}
    for name, elf in (("old", old), ("new", new)):
        uc, run = load(elf)
        uc.mem_write(FMT, blob)
        results[name] = run_cases(uc, run, cases, fmt_addr)
        if plug_cases: results[name + "_plug"] = run_cases(uc, run, plug_cases, fmt_addr)
    # sanity of the reference: the old image against Python's own (correctly rounded) formatting on finite values
    refbad, refex = 0, []
    diffs = {}
    minabs = {}
    for (x, fi, sz), (kind_fi, kind, _), o, n in zip(cases, meta, results["old"], results["new"]):
        g, f = rows[fi][0], rows[fi][1]
        if o != n:
            lim = "out" if (x == x and abs(x) != float("inf") and abs(x) >= ulo) else "in"   # NaN and inf are always in
            diffs.setdefault((g, f, kind, lim), []).append((x, sz, o, n))
            if g in ("ui", "log") and x == x: minabs[kind] = min(minabs.get(kind, float("inf")), abs(x))
        if sz == 64 and x == x and abs(x) != float("inf") and g in ("ui", "log") and "%s" not in f:
            try:
                want = (f % x).encode()[:63]   # the 64 byte buffer
                if o[1] != want:
                    refbad += 1
                    if len(refex) < 12: refex.append((f, x, want, o[1]))
            except Exception: pass
    total = sum(len(d) for d in diffs.values())
    plug_bad = [(m, c[0], o, n) for m, c, o, n in zip(plug_meta, plug_cases, results.get("old_plug", []), results.get("new_plug", [])) if o != n]
    if plug_cases:
        for m, x, o, n in plug_bad[:20]: print("   plugin param %s / %s: v=%r old=%r new=%r" % (m[0], m[1], x, o, n))
        print("printf_diff: plugin params: %d of %d displayed values differ" % (len(plug_bad), len(plug_cases)))
    for e in refex: print("   reference example: %r %r python=%r old=%r" % e)
    print("printf_diff: reference check, old image vs Python '%%' formatting: %d differing cases (informational, finite values, ui and log formats)" % refbad)
    # the pass rule: ui and log formats, float arguments or doubles below the limit
    fail = {k: d for k, d in diffs.items() if k[0] in ("ui", "log") and k[3] == "in"}
    if plug_bad: fail[("plugin", "params", "f", "in")] = plug_bad
    other = {k: d for k, d in diffs.items() if k not in fail}
    with open(os.path.join(outdir, "printf_diff.txt"), "w") as fh:
        for k, d in sorted(diffs.items()):
            fh.write("%s\n" % (k,))
            for x, sz, o, n in d[:200]: fh.write("   %r size=%d old=%r new=%r\n" % (x, sz, o, n))
    for label, group in (("FAIL (shipped formats, |v| < %g)" % ulo, fail), ("not a failure (|v| >= %g, extra shapes)" % ulo, other)):
        print("printf_diff: %s: %d differing cases in %d groups" % (label, sum(len(d) for d in group.values()), len(group)))
        for k, d in sorted(group.items())[:40]:
            x, sz, o, n = d[0]
            print("   %-8s %-12r %-2s %-3s %6d cases, first: v=%r size=%d old=%r new=%r" % (k[0], k[1], k[2], k[3], len(d), x, sz, o, n))
    print("printf_diff: smallest |v| with any diff on the shipped formats (NaN and inf never differ): float args %s, double args %s" % (
        minabs.get("f", "none"), minabs.get("d", "none")))
    print("printf_diff: %d of %d cases differ in total, details in %s" % (total, len(cases), os.path.join(outdir, "printf_diff.txt")))
    print("printf_diff: RESULT %s" % ("PASS, 0 diffs on the shipped formats" if not fail else "FAIL"))
    sys.exit(1 if fail else 0)

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--check-sites": check_sites(sys.argv[2], sys.argv[3])
    else: main()
