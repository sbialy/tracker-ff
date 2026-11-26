#!/usr/bin/env python3
"""Host test of check_static_init.py on canned nm and objdump text (no toolchain): a static constructor that stores to the SDRAM
through a literal pool address and through movw/movt is flagged in both objdump comment styles (";" binutils 2.3x of GCC 10.3, "@"
binutils 2.40 of 12.3.rel1, the style the MK66 gate missed), in the MK66 range and with --imx in the IMX range, and nothing else is."""
import contextlib
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_static_init as csi

NM = """00001000 T __preinit_array_start
00001000 T __preinit_array_end
00001000 T __init_array_start
00001008 T __init_array_end
"""
TABLE = """
img.axf:     file format elf32-littlearm

Contents of section .text:
 1000 01200000 11200000                    . ... ..
"""
DIS = """
img.axf:     file format elf32-littlearm

Disassembly of section .text:

00002000 <_GLOBAL__sub_I_lit>:
    2000:	4b01      	ldr	r3, [pc, #4]	{c} (2008 <_GLOBAL__sub_I_lit+0x8>)
    2002:	2200      	movs	r2, #0
    2004:	601a      	str	r2, [r3, #0]
    2006:	4770      	bx	lr
    2008:	{lit:08x} 	.word	0x{lit:08x}

00002010 <_GLOBAL__sub_I_movw>:
    2010:	f241 0300 	movw	r3, #{lo}	{c} 0x{lo:x}
    2014:	f2c8 0300 	movt	r3, #{hi}	{c} 0x{hi:x}
    2018:	2200      	movs	r2, #0
    201a:	601a      	str	r2, [r3, #0]
    201c:	4770      	bx	lr
"""


def check(comment, addr, imx):
    def run(tool, *args):
        if tool.endswith("nm"):
            return NM
        if "-s" in args:
            return TABLE
        return DIS.format(c=comment, lit=addr, lo=addr & 0xFFFF, hi=addr >> 16)

    csi.run = run
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = csi.main((["--imx"] if imx else []) + ["img.axf"])
    hits = out.getvalue().count("ERROR: static init touches the SDRAM")
    return rc, hits


bad = 0
for comment in (";", "@"):
    for addr, imx, want in ((0x70001000, False, 2), (0x80001000, False, 0), (0x80001000, True, 2), (0x83E037CC, True, 2),
                            (0x70001000, True, 0), (0x20001000, True, 0), (0x20001000, False, 0)):
        rc, hits = check(comment, addr, imx)
        if hits != want or rc != (1 if want else 0):
            bad += 1
            print("FAIL comment %r addr 0x%08X imx %s: %d hits rc %d, want %d" % (comment, addr, imx, hits, rc, want))
print("check_static_init_test: %s" % ("PASS" if not bad else "FAIL (%d)" % bad))
sys.exit(1 if bad else 0)
