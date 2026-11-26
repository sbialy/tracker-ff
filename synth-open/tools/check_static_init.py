#!/usr/bin/env python3
"""Refuse MK66 and IMX images whose static initialisation touches the SDRAM (EXTERNAL_RAM, 0x70000000-0x70FFFFFF).

The SDRAM only works after main() has run hardware init (platforms/mk66/source/hardware.cpp, externalRAM.init()).
ResetHandler (teensy_core/mk20dx128.c) runs __libc_init_array() before main(), so a static constructor that writes
a .noinit.$EXTERNAL_RAM global faults on the bus and fault_isr spins forever: the device sits on the bootloader's
"100" and never boots (ff-1.1.0 f563f27: gPatchScratch.data built by PatchData::PatchData() at static init).

Roots: every function in .preinit_array and .init_array (the _GLOBAL__sub_I_* functions and friends). From them it
follows direct calls and tail calls transitively. In each reached function it tracks the registers that hold an
SDRAM address (a literal pool load, movw/movt, mov of an immediate, then mov/add/sub copies) and flags:
  * a load or store with such a register as its base (it touches the SDRAM),
  * a call or tail call with such a register in r0-r3 that the callee reads before writing it (the callee may touch
    it, e.g. a constructor's this). Leftovers in an argument register the callee ignores are not flagged.
Only storing the address itself (a pointer to an SDRAM buffer kept in a RAM object) is fine and not flagged, so are
float constants with the same bits (vldr into an FP register). The scan is linear per function and calls through
pointers or vtables are not followed. Exit 1 with the call path for every hit.
IMX (Tracker+ and Mini, --imx): the same rule for the SEMC SDRAM 0x80000000-0x83FFFFFF. ResetISR
(platforms/imx/startup/startup_mimxrt1051.cpp) runs __libc_init_array before main(), main() sets up the SEMC
(BOARD_InitBootPeripherals), HardFault_Handler spins (semihost_hardfault.c): 2.0.0 beta 1 hung on the bootloader's "100 %"
(s_bank built by _GLOBAL__sub_I__ZN5SynthC2Ev).
Usage: check_static_init.py [--toolchain <prefix>] [--imx] <image.axf>
"""
import re
import subprocess
import sys

MK66_SDRAM = (0x70000000, 0x71000000)  # EXTERNAL_RAM of mk66fx1m0.ld
IMX_SDRAM = (0x80000000, 0x84000000)   # SEMC SDRAM of the RT1052 (platforms/imx/linker), set up in main() by BOARD_InitBootPeripherals
LO, HI = MK66_SDRAM

HDR = re.compile(r"^([0-9a-f]{8}) <(.+)>:$")
INS = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4} [0-9a-f]{4}|[0-9a-f]{8}|[0-9a-f]{4})\s+(\S+)\s*(.*)$")
TARGET = re.compile(r"^([0-9a-f]+) <")
# objdump comments: ";" up to binutils 2.3x (10.3), "@" from 2.40 (12.3.rel1). The ";"-only parse missed every literal pool and
# movw/movt address on 12.3 (the comment stuck to the immediate), so the MK66 gate ran blind (poly-imx-preinit-fix)
COMMENT = re.compile(r"[;@]")
LITREF = re.compile(r"[;@]\s*\(?([0-9a-f]+) <")
REGS = {f"r{i}" for i in range(13)} | {"sb", "sl", "fp", "ip", "sp", "lr", "pc"}
ALIAS = {"sb": "r9", "sl": "r10", "fp": "r11", "ip": "r12"}
ARGS = {"r0", "r1", "r2", "r3"}
CLOBBER = ARGS | {"r12", "lr"}
COND = r"(?:eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)?"
CALL_OPS = re.compile(rf"^(?:bl|blx)(?:{COND[3:-2]})?(?:\.[wn])?$")
BRANCH_OPS = re.compile(rf"^b{COND}(?:\.[wn])?$")
NODEST = re.compile(r"^(?:str|stm|push|vst|vpush|cmp|cmn|tst|teq|it|pld|pli|dmb|dsb|isb|nop|bkpt|svc|cps|msr|vcmp|vmsr)")
PROPAGATE = re.compile(r"^(?:mov|movs|mov\.w|add|adds|add\.w|addw|sub|subs|sub\.w|subw)" + COND + r"(?:\.[wn])?$")


def run(tool, *args):
    return subprocess.run([tool, *args], check=True, capture_output=True, text=True).stdout


def reg(tok):
    tok = tok.strip().strip("{}!").strip()
    if tok in REGS:
        return ALIAS.get(tok, tok)
    return None


def code(rest):
    """The operand text of one instruction without the objdump comment."""
    return COMMENT.split(rest, 1)[0]


def operands(rest):
    return [t.strip() for t in code(rest).split(",")]


def imm(tok):
    tok = tok.strip()
    if not tok.startswith("#"):
        return None
    try:
        return int(tok[1:], 0)
    except ValueError:
        return None


def rw(op, rest):
    """(reads, writes) of the core registers of one instruction, approximate."""
    text = code(rest)
    ops = operands(rest)
    regs = [reg(t) for t in re.split(r"[,\[\]{}\s!]+", text) if reg(t)]
    if NODEST.match(op):
        return regs, []
    if op.startswith(("ldm", "pop")):
        lst = text.split("{", 1)[1] if "{" in text else ""
        dests = [reg(t) for t in re.split(r"[,}\s]+", lst) if reg(t)]
        return [r for r in regs if r not in dests], dests
    if op.startswith("ldrd") or op.startswith(("umull", "smull", "umlal", "smlal")) or (op.startswith("vmov") and len(ops) > 2 and reg(ops[0]) and reg(ops[1])):
        return regs[2:] + (regs[:2] if op.startswith(("umlal", "smlal")) else []), regs[:2]
    return regs[1:], regs[:1]


def entry_live(lines):
    """The argument registers a function may read before it writes them. Walks every path through the function's own
    branches. A call, a table branch or a branch out of the function makes every argument register not yet written
    count as read, a return ends the path, and writes under an IT block do not count (conservative)."""
    index = {int(a, 16): i for i, (a, _o, _r) in enumerate(lines)}
    live = set()
    todo, seen = [(0, frozenset())], set()
    while todo and not ARGS <= live:
        i, written = todo.pop()
        it_left = 0
        while i < len(lines):
            if (i, written) in seen:
                break
            seen.add((i, written))
            _addr, op, rest = lines[i]
            if op.startswith("it"):
                it_left = len(op) - 1
                i += 1
                continue
            cond = it_left > 0
            it_left = max(0, it_left - 1)
            text = code(rest)
            if (op.startswith("bx") and "lr" in text) or ("pc" in text and (op.startswith(("pop", "ldm")) or (op.startswith("ldr") and text.strip().startswith("pc")))):
                if not cond:
                    break
                i += 1
                continue
            if CALL_OPS.match(op) or op.startswith(("tbb", "tbh", "bx", "blx")) or (op.startswith(("mov", "add")) and text.strip().startswith("pc")):
                live |= ARGS - written
                break
            reads, dests = rw(op, rest)
            live |= {r for r in reads if r in ARGS and r not in written}
            if BRANCH_OPS.match(op) or op.startswith(("cbz", "cbnz")):
                t = re.search(r"([0-9a-f]+) <", text)
                tgt = index.get(int(t.group(1), 16)) if t else None
                if tgt is None:
                    live |= ARGS - written
                    break
                conditional = op.startswith(("cbz", "cbnz")) or cond or BRANCH_OPS.match(op).group(0).split(".")[0] not in ("b",)
                if conditional:
                    todo.append((i + 1, written))
                i = tgt
                continue
            if not cond:
                written = written | {d for d in dests if d in ARGS}
            i += 1
        else:
            live |= ARGS - written
    return live


def analyse(lines, words, func_starts, live_of):
    """One function: (callees, hits). hits are 'addr: reason' strings."""
    callees, hits = set(), []
    taint = {}  # reg -> SDRAM address it holds
    low = {}  # reg -> movw low half
    for addr, op, rest in lines:
        base_op = op.split(".")[0]
        ops = operands(rest)
        if CALL_OPS.match(op) or (BRANCH_OPS.match(op) and TARGET.match(rest) and int(TARGET.match(rest).group(1), 16) in func_starts):
            t = TARGET.match(rest)
            if t:
                callees.add(int(t.group(1), 16))
            live = live_of(int(t.group(1), 16)) if t else ARGS
            args = sorted(r for r in taint if r in ARGS and r in live)
            if args:
                hits.append(f"{addr}: {op} {code(rest).strip()} with SDRAM address "
                            + ", ".join(f"{r}={taint[r]:#010x}" for r in args) + " in an argument register")
            for r in CLOBBER:
                taint.pop(r, None)
            continue
        if "[" in rest:
            inside = code(rest).split("[", 1)[1]
            b = reg(inside.split(",")[0].split("]")[0])
            if b in taint:
                hits.append(f"{addr}: {op} {code(rest).strip()} accesses SDRAM {taint[b]:#010x}")
        elif re.match(r"^(?:ldm|stm|vldm|vstm)", op) and ops:
            b = reg(ops[0])
            if b in taint:
                hits.append(f"{addr}: {op} {code(rest).strip()} accesses SDRAM {taint[b]:#010x}")
        if NODEST.match(op) or BRANCH_OPS.match(op):
            continue
        dest = reg(ops[0]) if ops else None
        if dest is None:
            continue
        if base_op.startswith("ldm") or op.startswith("pop") or op.startswith("ldrd"):
            for t in code(rest).replace("{", ",").replace("}", ",").split(","):
                r = reg(t)
                if r and not (base_op.startswith("ldm") and t.strip().endswith("!")):
                    taint.pop(r, None)
            continue
        if base_op.startswith("ldr") and "[pc" in rest:
            m = LITREF.search(rest)
            v = words.get(int(m.group(1), 16)) if m else None
            if v is not None and LO <= v < HI:
                taint[dest] = v
            else:
                taint.pop(dest, None)
            continue
        if base_op == "movw":
            v = imm(ops[1]) if len(ops) > 1 else None
            low[dest] = v or 0
            taint.pop(dest, None)
            continue
        if base_op == "movt":
            v = imm(ops[1]) if len(ops) > 1 else None
            if v is not None and LO <= (v << 16) < HI:
                taint[dest] = (v << 16) | low.get(dest, 0)
            else:
                taint.pop(dest, None)
            continue
        if PROPAGATE.match(op):
            srcs = [reg(t) for t in ops[1:]]
            src = next((s for s in srcs if s in taint), None)
            if src is not None:
                taint[dest] = taint[src]
                continue
            if base_op.startswith("mov") and len(ops) > 1:
                v = imm(ops[1])
                if v is not None and LO <= v < HI:
                    taint[dest] = v
                    continue
        taint.pop(dest, None)
    return callees, hits


def main(argv):
    global LO, HI
    LO, HI = MK66_SDRAM
    prefix = "arm-none-eabi-"
    while argv and argv[0].startswith("--"):
        if argv[0] == "--toolchain" and len(argv) >= 2:
            prefix, argv = argv[1], argv[2:]
        elif argv[0] == "--imx":
            (LO, HI), argv = IMX_SDRAM, argv[1:]
        else:
            sys.exit(__doc__)
    if len(argv) != 1:
        sys.exit(__doc__)
    axf = argv[0]

    syms = {}
    for line in run(prefix + "nm", axf).splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    for s in ("__init_array_start", "__init_array_end", "__preinit_array_start", "__preinit_array_end"):
        if s not in syms:
            sys.exit(f"ERROR: {axf}: no {s}")

    # The pointer tables live inside .text (mk66fx1m0.ld): read them as words
    start, end = syms["__preinit_array_start"], syms["__init_array_end"]
    roots = []
    if end > start:
        dump = run(prefix + "objdump", "-s", "-j", ".text", f"--start-address={start:#x}", f"--stop-address={end:#x}", axf)
        data = b""
        for line in dump.splitlines():
            m = re.match(r"^ ([0-9a-f]{4,8}) ((?:[0-9a-f]{2,8} ){1,4})", line)
            if m:
                data += bytes.fromhex(m.group(2).replace(" ", ""))
        data = data[: end - start]
        roots = [int.from_bytes(data[i : i + 4], "little") & ~1 for i in range(0, len(data) - 3, 4)]

    # Whole disassembly once: functions with their instructions, and every literal pool word by address
    funcs, words, cur = {}, {}, None
    for line in run(prefix + "objdump", "-d", axf).splitlines():
        m = HDR.match(line)
        if m:
            cur = [m.group(2), []]
            funcs[int(m.group(1), 16)] = cur
            continue
        m = INS.match(line)
        if not m or cur is None:
            continue
        addr, op, rest = m.group(1), m.group(2), m.group(3)
        if op == ".word":
            try:
                words[int(addr, 16)] = int(rest.split()[0], 16)
            except ValueError:
                pass
        else:
            cur[1].append((addr, op, rest))

    starts = set(funcs)
    live_cache = {}

    def live_of(a):
        if a not in funcs:
            return ARGS
        if a not in live_cache:
            live_cache[a] = entry_live(funcs[a][1])
        return live_cache[a]

    bad, seen = 0, set()
    stack = [(r, (r,)) for r in roots if r in funcs]
    for r in roots:
        if r not in funcs:
            print(f"WARN: init entry {r:#x} is not a function start, not checked")
    while stack:
        a, path = stack.pop()
        if a in seen:
            continue
        seen.add(a)
        callees, hits = analyse(funcs[a][1], words, starts, live_of)
        for h in hits:
            bad += 1
            print(f"ERROR: static init touches the SDRAM before it is initialised: {h}\n  via "
                  + " -> ".join(funcs[p][0] for p in path))
        for c in callees:
            if c in funcs and c not in seen:
                stack.append((c, path + (c,)))

    print(f"static init: {len(roots)} roots, {len(seen)} functions reached, {bad} SDRAM accesses")
    if bad:
        print("Fix: keep .noinit.$EXTERNAL_RAM globals trivially constructible (raw bytes, placement new after hardware init)")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
