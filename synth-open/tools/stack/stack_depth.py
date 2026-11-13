#!/usr/bin/env python3
"""cpu-opt: worst case stack depth of call chains in an ARM Thumb ELF (firmware axf or plugin elf).

    stack_depth.py ELF [--su DIR ...] [--resolve FILE] [--root SYM_REGEX ...] [--path] [--check-su]

Frames: GCC's .su file of the function when the demangled name matches exactly one .su entry (--su dirs, searched
recursively), else the prologue scan of the disassembly (push, vpush, stmdb sp!, sub sp #imm; a sub sp by a register is
reported as dynamic). Calls: bl/blx <sym> add the callee depth to the caller frame, a b/b.w to another function's start is a
tail call (caller frame already popped, callee depth alone). Indirect calls (blx rN, bx rN other than lr) take the targets
of --resolve (lines "caller_regex => target_regex", demangled names, first match wins per caller), unresolved ones are
listed. Recursion is reported and cut (the cycle counted once).
"""
import argparse
import os
import re
import subprocess
import sys

OBJDUMP = os.environ.get("TOOLCHAIN", os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../agt/10.3/bin/arm-none-eabi-")) + "objdump"
CXXFILT = os.environ.get("TOOLCHAIN", os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../agt/10.3/bin/arm-none-eabi-")) + "c++filt"

FN_RE = re.compile(r"^([0-9a-f]+) <(.+)>:$")
INS_RE = re.compile(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$")


def reglist_count(s):
    m = re.search(r"\{([^}]*)\}", s)
    if not m:
        return 0, False, 0
    n = 0
    pc = False
    width = 4
    for part in m.group(1).split(","):
        part = part.strip()
        if "-" in part:
            a, b = part.split("-")
            ka = int(re.sub(r"\D", "", a))
            kb = int(re.sub(r"\D", "", b))
            n += kb - ka + 1
            if a.strip().startswith("d"):
                width = 8
        elif part:
            n += 1
            if part == "pc":
                pc = True
            if part.startswith("d"):
                width = 8
    return n, pc, width


def parse(elf):
    out = subprocess.run([OBJDUMP, "-d", "--no-show-raw-insn", elf], capture_output=True, text=True, check=True).stdout
    funcs = {}          # name -> dict(addr, ins=[(addr, mnem, ops)])
    order = []
    cur = None
    for line in out.splitlines():
        m = FN_RE.match(line)
        if m:
            cur = m.group(2)
            if cur.startswith("$") or cur.startswith("."):
                continue
            funcs[cur] = {"addr": int(m.group(1), 16), "ins": []}
            order.append(cur)
            continue
        m = INS_RE.match(line)
        if m and cur in funcs:
            funcs[cur]["ins"].append((int(m.group(1), 16), m.group(2), m.group(3)))
    return funcs


def frame_of(ins):
    """Running SP decrease, max over the body. Epilogue pops that return are not subtracted (code after them still runs
    with the frame in place)."""
    cur = mx = 0
    dynamic = False
    for _, mn, ops in ins:
        base = mn.split(".")[0]
        if base == "push" or (base == "stmdb" and ops.startswith("sp!")):
            n, _, _ = reglist_count(ops)
            cur += 4 * n
        elif base == "vpush":
            n, _, w = reglist_count(ops)
            cur += w * n
        elif base in ("sub", "subw") and re.match(r"sp,\s*(sp,\s*)?#", ops):
            cur += int(re.search(r"#(\d+)", ops).group(1))
        elif base in ("sub", "subw") and re.match(r"sp,\s*(sp,\s*)?r", ops):
            dynamic = True
        elif base in ("add", "addw") and re.match(r"sp,\s*(sp,\s*)?#", ops):
            cur -= int(re.search(r"#(\d+)", ops).group(1))
        elif base == "vpop":
            n, _, w = reglist_count(ops)
            cur -= w * n
        elif base == "pop" or (base == "ldmia" and ops.startswith("sp!")):
            n, pc, _ = reglist_count(ops)
            if not pc:
                cur -= 4 * n
        mx = max(mx, cur)
    return mx, dynamic


def calls_of(name, f, by_addr):
    direct, tail, indirect = set(), set(), []
    lo = f["addr"]
    hi = f["ins"][-1][0] if f["ins"] else lo
    for a, mn, ops in f["ins"]:
        base = mn.split(".")[0]
        m = re.search(r"([0-9a-f]+) <([^>+]+)(\+0x[0-9a-f]+)?>", ops)
        if base in ("bl", "blx") and m and not ops.startswith("r"):
            tgt = int(m.group(1), 16)
            if tgt in by_addr:
                direct.add(by_addr[tgt])
        elif base == "blx" and re.match(r"r\d+|ip|lr", ops):
            indirect.append(a)
        elif base == "bx" and not ops.startswith("lr"):
            indirect.append(a)
        elif base == "b" and m:
            tgt = int(m.group(1), 16)
            if not (lo <= tgt <= hi) and tgt in by_addr and by_addr[tgt] != name:
                tail.add(by_addr[tgt])
    return direct, tail, indirect


def addr_taken(elf, by_addr, with_vtables=False):
    """Thumb function addresses (bit 0 set) stored as words anywhere in the loaded sections: the possible targets of an
    indirect call (function pointer tables, vtables, literal pools)."""
    import struct
    d = open(elf, "rb").read()
    shoff, = struct.unpack_from("<I", d, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", d, 0x2E)
    # words inside a vtable are virtual methods: those calls are resolved by explicit rules, not as pointer tables
    nm = subprocess.run([OBJDUMP.replace("objdump", "nm"), "-S", "--defined-only", elf], capture_output=True, text=True).stdout
    vt = []
    for line in nm.splitlines():
        f = line.split()
        if len(f) == 4 and f[3].startswith("_ZTV"):
            vt.append((int(f[0], 16), int(f[0], 16) + int(f[1], 16)))
    taken = set()
    for k in range(shnum):
        _, typ, flags, addr, off, size = struct.unpack_from("<IIIIII", d, shoff + k * shentsize)
        if typ != 1 or not (flags & 2):
            continue
        blob = d[off:off + size]
        for i in range(0, len(blob) - 3, 2):
            w, = struct.unpack_from("<I", blob, i)
            if w & 1 and (w & ~1) in by_addr and (with_vtables or not any(lo <= addr + i < hi for lo, hi in vt)):
                taken.add(by_addr[w & ~1])
    return taken



def vtable_slots(elf, by_addr, only=None):
    """slot offset (bytes from the vptr) -> set of functions stored there in any vtable (Itanium ABI: symbol + 8 is the vptr)."""
    import struct
    d = open(elf, "rb").read()
    shoff, = struct.unpack_from("<I", d, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", d, 0x2E)
    secs = []
    for k in range(shnum):
        _, typ, flags, addr, off, size = struct.unpack_from("<IIIIII", d, shoff + k * shentsize)
        if typ == 1 and (flags & 2):
            secs.append((addr, off, size))
    def word(a):
        for sa, so, ss in secs:
            if sa <= a < sa + ss - 3:
                return struct.unpack_from("<I", d, so + a - sa)[0]
        return None
    nm = subprocess.run([OBJDUMP.replace("objdump", "nm"), "-S", "--defined-only", elf], capture_output=True, text=True).stdout
    slots = {}
    for line in nm.splitlines():
        f = line.split()
        if len(f) == 4 and f[3].startswith("_ZTV") and (only is None or f[3] == only):
            base, size = int(f[0], 16), int(f[1], 16)
            for off in range(0, size - 8, 4):
                w = word(base + 8 + off)
                if w is not None and w & 1 and (w & ~1) in by_addr:
                    slots.setdefault(off, set()).add(by_addr[w & ~1])
    return slots


def classify_sites(f):
    """For each indirect call site of function f: ('virt', slot offset) when the target register came from
    ldr rA,[rB] ; ldr rC,[rA,#off] ; blx rC, else ('ptr', None)."""
    ins = f["ins"]
    out = []
    for i, (a, mn, ops) in enumerate(ins):
        base = mn.split(".")[0]
        reg = None
        if base == "blx" and re.match(r"r\d+|ip|lr", ops):
            reg = ops.strip()
        elif base == "bx" and not ops.startswith("lr"):
            reg = ops.strip()
        if reg is None:
            continue
        kind = ("ptr", None)
        for j in range(i - 1, max(i - 12, -1), -1):
            m = re.match(r"ldr(?:\.w)?\s+%s,\s*\[(\w+)(?:,\s*#(\d+))?\]" % reg, "%s %s" % (ins[j][1], ins[j][2]))
            if m:
                src, off = m.group(1), int(m.group(2) or 0)
                for k in range(j - 1, max(j - 12, -1), -1):
                    m2 = re.match(r"ldr(?:\.w)?\s+%s,\s*\[(\w+)(?:,\s*#0)?\]$" % src, "%s %s" % (ins[k][1], ins[k][2]))
                    if m2:
                        kind = ("virt", off)
                        break
                break
        out.append((a, kind))
    return out


def load_map(path, by_addr):
    """addr -> input object of a GNU ld map (-ffunction-sections: one .text.<fn> input section per function)."""
    obj = {}
    if not path:
        return obj
    pend = None
    for line in open(path, errors="replace"):
        m = re.match(r"^ (\.text\S*)\s*$", line)
        if m:
            pend = m.group(1)
            continue
        m = re.match(r"^ (\.text\S*)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)", line)
        if m and (m.group(1) or pend):
            a = int(m.group(2), 16)
            sz = int(m.group(3), 16)
            for fa in range(a, a + sz, 2):
                if fa in by_addr:
                    obj[by_addr[fa]] = m.group(4)
        pend = None
    return obj


def demangle_all(names):
    r = subprocess.run([CXXFILT], input="\n".join(names), capture_output=True, text=True, check=True).stdout.splitlines()
    return dict(zip(names, r))


def load_su(dirs):
    su = {}
    for d in dirs:
        for root, _, files in os.walk(d):
            for fn in files:
                if not fn.endswith(".su"):
                    continue
                for line in open(os.path.join(root, fn), errors="replace"):
                    parts = line.rstrip("\n").split("\t")
                    if len(parts) < 3:
                        continue
                    loc = parts[0]
                    # file:line:col:name, the name may hold ':' (namespaces), so cut the first three fields
                    bits = loc.split(":", 3)
                    if len(bits) < 4:
                        continue
                    nm = bits[3]
                    nm = re.sub(r"^virtual ", "", nm)
                    # drop the return type: the qualified name starts at the last top level space before the '('
                    p = nm.find("(")
                    head = nm[:p] if p >= 0 else nm
                    depth = 0
                    cut = -1
                    for i, ch in enumerate(head):
                        if ch == "<":
                            depth += 1
                        elif ch == ">":
                            depth -= 1
                        elif ch == " " and depth == 0:
                            cut = i
                    key = nm[cut + 1:] if cut >= 0 else nm
                    key = key.replace("{anonymous}", "(anonymous namespace)").replace(" ", "")
                    su.setdefault(key, set()).add((int(parts[1]), parts[2]))
    return su


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--su", action="append", default=[])
    ap.add_argument("--resolve")
    ap.add_argument("--root", action="append", default=[])
    ap.add_argument("--path", action="store_true")
    ap.add_argument("--to", help="with one --root: deepest stack in use (callers' frames) when TO_REGEX is entered")
    ap.add_argument("--check-su", action="store_true")
    ap.add_argument("--gap", help="ISR_ROOT_REGEX: with --root (main roots) bound the unresolved indirect calls: deepest stack at an unresolved site and deepest indirect-only target")
    ap.add_argument("--cut", action="append", default=[], help="drop call edges into functions matching REGEX (a path that cannot be taken, for example float printf)")
    ap.add_argument("--sa", action="append", default=[], help="with --gap: print the deepest prefix before functions matching REGEX")
    ap.add_argument("--frame", action="append", default=[], help="REGEX=BYTES: replace the frame of matching functions (what-if for a patch)")
    ap.add_argument("--cha", action="store_true", help="indirect calls without a rule: virtual calls (vptr load) go to every vtable entry at that slot")
    ap.add_argument("--sites", action="store_true", help="list the indirect call sites left unresolved below the roots, with their kind")
    ap.add_argument("--map", help="link map: rule target &OBJ = address-taken functions of the caller's object file")
    ap.add_argument("--extra", action="append", default=[], help="SYM_REGEX=BYTES: add BYTES to the depth of matching leaves (plugin code)")
    a = ap.parse_args()

    funcs = parse(a.elf)
    by_addr = {f["addr"]: n for n, f in funcs.items()}
    dem = demangle_all(list(funcs))
    su = load_su(a.su)

    frames = {}
    src = {}
    dyn = {}
    mism = []
    for n, f in funcs.items():
        fr, d = frame_of(f["ins"])
        key = re.sub(r"\s+", "", dem[n])
        hit = None if "[clone" in key else su.get(key)
        if hit and len(hit) == 1:
            (sv, q), = hit
            if a.check_su and sv != fr:
                mism.append((dem[n], sv, fr))
            frames[n] = sv
            src[n] = "su"
            dyn[n] = d or q.startswith("dynamic")
        else:
            frames[n] = fr
            src[n] = "dis"
            dyn[n] = d
    if a.check_su:
        matched = sum(1 for n in funcs if src[n] == "su")
        print("su matched %d of %d functions, disassembly frame differs for %d" % (matched, len(funcs), len(mism)))
        for d, sv, fr in sorted(mism, key=lambda t: -abs(t[1] - t[2]))[:25]:
            print("  su %5d  dis %5d  %s" % (sv, fr, d))

    for fo in a.frame:
        rr, bb = fo.rsplit('=', 1)
        for n in funcs:
            if re.search(rr, dem[n]):
                frames[n] = int(bb)
    resolve = []
    if a.resolve:
        for line in open(a.resolve):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            c, t = [x.strip() for x in line.split("=>")]
            resolve.append((re.compile(c), re.compile(t)))
    extra = []
    for e in a.extra:
        r, b = e.rsplit("=", 1)
        extra.append((re.compile(r), int(b)))

    edges = {}
    unresolved = {}
    sitekind = {}
    nvirt = {}
    vslots = vtable_slots(a.elf, by_addr) if a.cha else {}
    fsslots = vtable_slots(a.elf, by_addr, '_ZTV14FileSystemImpl')
    fsleft = {}
    taken = addr_taken(a.elf, by_addr)
    taken_vt = addr_taken(a.elf, by_addr, True)
    objmap = load_map(a.map, by_addr)
    for n, f in funcs.items():
        d, t, ind = calls_of(n, f, by_addr)
        targets = set()
        if ind:
            rule = None
            for c, tg in resolve:
                if c.search(dem[n]):
                    rule = tg
                    break
            if rule is None and a.cha:
                vt = []
                left = []
                for ad, (kd, off) in classify_sites(f):
                    if kd == "virt" and off in vslots:
                        vt.append(vslots[off])
                    else:
                        left.append((ad, kd))
                for tset in vt:
                    targets |= tset
                if left:
                    unresolved[n] = [x[0] for x in left]
                    sitekind[n] = left
                nvirt[n] = len(vt)
            elif rule is None:
                unresolved[n] = ind
            elif rule.pattern == "&FSVT":
                for ad, (kd, off) in classify_sites(f):
                    if kd == "virt" and off in fsslots:
                        targets |= fsslots[off]
                    else:
                        fsleft.setdefault(n, []).append(ad)
                if n in fsleft:
                    unresolved[n] = fsleft[n]
            elif rule.pattern == "&OBJ":
                o = objmap.get(n)
                targets = {m for m in taken if o and objmap.get(m) == o}
                if not o:
                    unresolved[n] = ind
            elif rule.pattern == "&ALL":
                targets = set(taken)
            elif rule.pattern.startswith("&VT:"):
                rx = re.compile(rule.pattern[4:])
                targets = {m for m in taken_vt if rx.search(dem[m])}
            elif rule.pattern.startswith("&RE:"):
                rx = re.compile(rule.pattern[4:])
                targets = {m for m in taken if rx.search(dem[m])}
            elif rule.pattern != "NONE":
                targets = {m for m in funcs if rule.search(dem[m])}
        edges[n] = (d | targets, t)

    if a.cut:
        cx = [re.compile(c) for c in a.cut]
        for n in list(edges):
            c_, t_ = edges[n]
            edges[n] = ({x for x in c_ if not any(r.search(dem[x]) for r in cx)}, {x for x in t_ if not any(r.search(dem[x]) for r in cx)})
    memo = {}
    cycles = set()
    sys.setrecursionlimit(100000)

    def depth(n, stack):
        if n in memo:
            return memo[n]
        if n in stack:
            cycles.add(dem[n])
            return (0, [])
        stack.add(n)
        best, bpath = 0, []
        calls, tails = edges.get(n, (set(), set()))
        for c in calls:
            dd, p = depth(c, stack)
            if dd > best:
                best, bpath = dd, p
        own = frames[n] + best
        path = [n] + bpath
        for c in tails:
            dd, p = depth(c, stack)
            if dd > own:
                own, path = dd, [n + " (tail)"] + p
        for r, b in extra:
            if r.search(dem[n]):
                own += b
        stack.discard(n)
        memo[n] = (own, path)
        return memo[n]

    if a.gap:
        isr_rx = re.compile(a.gap)
        def reach_from(rx):
            seen = set()
            todo = [n for n in funcs if rx.search(dem[n])]
            while todo:
                x = todo.pop()
                if x in seen:
                    continue
                seen.add(x)
                c, t = edges.get(x, (set(), set()))
                todo.extend(c | t)
            return seen
        mroots = [n for n in funcs if any(re.search(r, dem[n]) for r in a.root)]
        rm = reach_from(re.compile("|".join(a.root)))
        ri = reach_from(isr_rx)
        # longest prefix with back edges (recursion) cut: DFS order, then relax in reverse postorder
        order, state = [], {}
        for r0 in mroots:
            stk = [(r0, iter(sorted(edges.get(r0, (set(), set()))[0] | edges.get(r0, (set(), set()))[1])))]
            if r0 in state:
                continue
            state[r0] = 1
            while stk:
                n, itr = stk[-1]
                adv = False
                for c in itr:
                    if c not in state:
                        state[c] = 1
                        cc, tt = edges.get(c, (set(), set()))
                        stk.append((c, iter(sorted(cc | tt))))
                        adv = True
                        break
                if not adv:
                    state[n] = 2
                    order.append(n)
                    stk.pop()
        pos = {n: i for i, n in enumerate(reversed(order))}
        sa = {n: 0 for n in mroots}
        for n in reversed(order):
            calls, tails = edges.get(n, (set(), set()))
            for c, v in [(c, sa.get(n, 0) + frames[n]) for c in calls] + [(c, sa.get(n, 0)) for c in tails]:
                if pos.get(c, -1) > pos[n] and v > sa.get(c, -1):
                    sa[c] = v
        sites = sorted(((sa[n] + frames[n], n) for n in rm if n in unresolved and n in sa), reverse=True)
        print("main roots reach %d functions, %d have unresolved indirect calls" % (len(rm), sum(1 for n in rm if n in unresolved)))
        print("deepest stack AT an unresolved indirect call (frames of the callers, including the caller's own):")
        for v, n in sites[:12]:
            print("   %6d  %s" % (v, dem[n][:110]))
        for rx_ in a.sa:
            for n in sorted((n for n in funcs if re.search(rx_, dem[n]) and n in sa), key=lambda n: -sa[n])[:6]:
                print("   prefix %6d  frame %5d  depth %5d  %s" % (sa[n], frames[n], depth(n, set())[0], dem[n][:100]))
        cand = [n for n in (taken | taken_vt) if n not in ri and n in funcs]
        dd = sorted(((depth(n, set())[0], n) for n in cand), reverse=True)
        print("indirect-only targets (address-taken, not reached by the audio ISR): %d" % len(cand))
        for v, n in dd[:25]:
            print("   %6d  %s%s" % (v, dem[n][:110], "  [in main reach]" if n in rm else ""))
        return
    if a.to:
        tx = re.compile(a.to)
        tmemo = {}

        def reach(n, stack):
            # max bytes of the frames below the target's entry, None if n never reaches it
            if n in tmemo:
                return tmemo[n]
            if n in stack:
                return None
            stack.add(n)
            best = None
            bp = []
            calls, tails = edges.get(n, (set(), set()))
            for c in calls | tails:
                if tx.search(dem[c]):
                    v, p = (0, [c])
                else:
                    r_ = reach(c, stack)
                    if r_ is None:
                        continue
                    v, p = r_
                v2 = v + (0 if c in tails else frames[n])
                if best is None or v2 > best[0]:
                    best = (v2, [n] + p)
            stack.discard(n)
            tmemo[n] = best
            return best
        for n in [m for m in funcs if re.search(a.root[0], dem[m])][:1]:
            r_ = reach(n, set())
            print("deepest stack when %s is entered from %s: %s" % (a.to, dem[n], r_[0] if r_ else "not reached"))
            if r_ and a.path:
                for q in r_[1]:
                    print("    %5d %s" % (frames.get(q, 0), dem[q][:110]))
        return
    for r in a.root:
        rx = re.compile(r)
        roots = [n for n in funcs if rx.search(dem[n])]
        if not roots:
            print("root %s: no match" % r)
            continue
        for n in sorted(roots, key=lambda n: -depth(n, set())[0])[:3]:
            dd, path = depth(n, set())
            print("root %-50s depth %6d B" % (dem[n][:50], dd))
            if a.path:
                run = 0
                for p in path:
                    q = p.replace(" (tail)", "")
                    run += 0 if p.endswith("(tail)") else frames.get(q, 0)
                    flags = (" dynamic" if dyn.get(q) else "") + (" UNRESOLVED-indirect" if q in unresolved else "")
                    print("    %5d %5d %-3s %s%s%s" % (frames.get(q, 0), run, src.get(q, ""), dem.get(q, q)[:110],
                                                   " (tail)" if p.endswith("(tail)") else "", flags))
        reach = set()
        todo = list(roots)
        while todo:
            x = todo.pop()
            if x in reach:
                continue
            reach.add(x)
            c, t = edges.get(x, (set(), set()))
            todo.extend(c | t)
        ur = [x for x in reach if x in unresolved]
        if ur:
            print("  unresolved indirect calls reachable from %s: %d functions" % (r, len(ur)))
            for x in sorted(ur, key=lambda x: dem[x])[:int(os.environ.get("UNRES_MAX","80"))]:
                print("    %s  (%s)" % (dem[x][:120], ", ".join("%x" % v for v in unresolved[x][:4])))
        dy = [x for x in reach if dyn.get(x)]
        if dy:
            print("  dynamic frames reachable: " + "; ".join(dem[x][:60] for x in dy))
    if cycles:
        print("recursion cut at: " + "; ".join(sorted(cycles)[:20]))


if __name__ == "__main__":
    main()
