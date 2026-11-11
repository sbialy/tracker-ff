#!/usr/bin/env python3
"""Static stack margin of a firmware image (MK66, one stack for the main loop and every interrupt).

    TOOLCHAIN=.../arm-none-eabi- python3 stack_margin.py IMAGE_DIR --plugin-depths FILE [--su DIR] [--optional flux,form]
                                                      [--insert-depth 664] [--stack-margin 8192] [--min-margin 256]

IMAGE_DIR holds PolyendTracker.axf and PolyendTracker.map of one build. FILE has one "name depth" line per synth plugin,
the output of plug_depth.sh on the plugin ELFs (the deepest entry of each plugin, in bytes). Frames come from the
disassembly (a prologue scan that agrees with GCC's .su on every function both cover), --su adds the .su files.

The formula (ref/stack-margin.md, derivation and evidence):

    room    = STACK_MARGIN + stack in use at the last _sbrk of Profiler::init
              Profiler::init mallocs until _sbrk refuses, so the heap line is SP(at that call) - STACK_MARGIN, below it up to
              _estack is the stack. A lower bound: the last grant may sit up to about 32 B under the line.
    margin  = room - (main loop + audio out ISR chain + 108 + nested interrupts)
    108     = exception frame with FPU lazy stacking: 8 words + 18 FPU words = 104 B, plus 4 B for the 8 byte alignment.
    realistic: main loop without the SD timeout tail, nested = SysTick, audio in and the MIDI level (they preempt audio out)
    static   : main loop with the timeout tail, nested = every handler of every level, one on top of the other
               (I2C, USB and SDHC are weaker than audio out and can be running when it fires)

One stack for everything: the image has no PSP (msr psp), FPCCR keeps its reset value (ASPEN and LSPEN set).
Exit status 1 when a row of the release set (every plugin not named in --optional) is below --min-margin at the static bound.
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WALK = os.path.join(HERE, "stack_depth.py")
RULES = os.path.join(HERE, "stack_resolve_fs.txt")
EXC = 108
MIDI_TX = 150   # the MIDI transmit call under the clock timer: an estimate (E), the call goes through a pointer
# (name, regex, NVIC level, lower = stronger) of every handler the image can run, see platforms/mk66/config/interruptsConfig.h
ISRS = [
    ("systick", r"^systick_isr$", 2),
    ("pendsv", r"^pendablesrvreq_isr$", 2),
    ("midi_uart", r"^uart4_status_isr$", 0),
    ("midi_clock", r"^pit0_isr$", 0),
    ("audio_in", r"^AudioInputI2S::isr\(\)$", 4),
    ("audio_out", None, 5),
    ("sdhc", r"^sdhc_isr$", 6),
    ("usb", r"^usb_isr$", 7),
    ("i2c0", r"^i2c0_isr$", 8), ("i2c1", r"^i2c1_isr$", 8), ("i2c2", r"^i2c2_isr$", 8), ("i2c3", r"^i2c3_isr$", 8),
    ("port_a", r"^port_A_isr$", 8), ("port_b", r"^port_B_isr$", 8), ("port_c", r"^port_C_isr$", 8),
    ("port_d", r"^port_D_isr$", 8), ("port_e", r"^port_E_isr$", 8),
    ("display_dma", r"^dataTransferComplite\(\)$", 8),
]


def walk(img, args, su):
    cmd = [sys.executable, WALK, os.path.join(img, "PolyendTracker.axf"), "--map", os.path.join(img, "PolyendTracker.map"),
           "--resolve", RULES]
    for s in su:
        cmd += ["--su", s]
    r = subprocess.run(cmd + args, capture_output=True, text=True)
    return r.stdout + r.stderr


def root_depth(out, which=None):
    d = [int(m.group(1)) for m in re.finditer(r"^root .* depth\s+(\d+) B", out, re.M)]
    if not d:
        sys.exit("walker found no root:\n" + out[-600:])
    return max(d) if which is None else d[which]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--plugin-depths", required=True)
    ap.add_argument("--su", action="append", default=[])
    ap.add_argument("--optional", default="flux,form", help="plugins that are not in the release set (reported, not gated)")
    ap.add_argument("--insert-depth", type=int, default=664)
    ap.add_argument("--stack-margin", type=int, default=8192, help="STACK_MARGIN of mk20dx128.c")
    ap.add_argument("--min-margin", type=int, default=256)
    a = ap.parse_args()
    if not os.environ.get("TOOLCHAIN"):
        sys.exit("TOOLCHAIN (the arm-none-eabi- prefix) is not set")
    w = lambda *x: walk(a.image, list(x), a.su)

    # boot: the stack in use at the last _sbrk of Profiler::init
    to = re.search(r"entered.*?: (\d+)", w("--root", r"^ResetHandler$", "--to", r"^Profiler::init\(\)$"))
    boot = int(to.group(1)) + root_depth(w("--root", r"^Profiler::init\(\)$"))
    room = a.stack_margin + boot

    main_static = root_depth(w("--root", r"^loop$"))
    main_real = root_depth(w("--root", r"^loop$", "--cut", r"^_printf_float$", "--cut", r"^restartMCU$"))
    own = {}
    for n, rx, _ in ISRS:
        if rx:
            own[n] = root_depth(w("--root", rx)) + EXC
    own["midi_clock"] += MIDI_TX
    above = max(own["systick"], own["pendsv"]) + own["audio_in"] + max(own["midi_uart"], own["midi_clock"])
    below_all = sum(max(own[n] for n in g) for g in (["sdhc"], ["usb"], ["i2c0", "i2c1", "i2c2", "i2c3", "port_a", "port_b", "port_c", "port_d", "port_e", "display_dma"]))
    nest_real, nest_static = above, above + below_all

    isr_rx = r"^AudioOutputI2S::isr\("
    slot = r"^plug::\(anonymous namespace\)::PlugSynth::(render|prepare|slotPost|setSendTap|setTempo|noteOn|noteOff|slotReset)\("
    builtin = root_depth(w("--root", isr_rx))
    plugs = []
    for line in open(a.plugin_depths):
        p = line.split()
        if len(p) == 2 and p[1].isdigit():
            plugs.append((p[0], int(p[1])))
    plugs.sort(key=lambda x: -x[1])
    opt = set(a.optional.split(",")) if a.optional else set()
    ins = root_depth(w("--extra", r"^AudioEffectPlug::update\(\)$=%d" % a.insert_depth, "--root", r"^AudioEffectPlug::update\(\)$"))

    print("room        %6d B   (STACK_MARGIN %d + stack in use at the last _sbrk %d)" % (room, a.stack_margin, boot))
    print("main loop   %6d B   realistic %d B (no SD timeout tail)" % (main_static, main_real))
    print("exception   %6d B   frame (FPU lazy stacking)" % EXC)
    print("nested      %6d B   realistic (SysTick + audio in + MIDI), %d B static (every level)" % (nest_real, nest_static))
    print("handlers    " + ", ".join("%s %d" % (n, own[n]) for n in own))
    print()
    print("%-10s %7s %7s %9s %9s" % ("audio ISR", "plugin", "chain", "realistic", "static"))
    rows = [("built-in", 0, builtin, False)]
    for n, d in plugs:
        chain = root_depth(w("--extra", slot + "=%d" % d, "--root", isr_rx))
        rows.append((n, d, chain, n in opt))
    bad = 0
    for n, d, chain, o in rows:
        mr = room - (main_real + chain + EXC + nest_real)
        ms = room - (main_static + chain + EXC + nest_static)
        flag = ""
        if o:
            flag = "  optional, not in the release set"
        elif ms < a.min_margin:
            flag = "  BELOW %d" % a.min_margin
            bad += 1
        print("%-10s %7s %7d %+9d %+9d%s" % (n, d or "-", chain, mr, ms, flag))
    print("inserts     (deepest %d B in AudioEffectPlug::update) chain %d B" % (a.insert_depth, ins))
    print("\nrelease set: %s" % ("OK, weakest static margin %+d B" % min(room - (main_static + c + EXC + nest_static) for n, d, c, o in rows if not o)
                                if not bad else "%d row(s) below %d B" % (bad, a.min_margin)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
