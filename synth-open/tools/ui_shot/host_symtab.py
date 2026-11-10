#!/usr/bin/env python3 -I
"""host_symtab.py EXE HOST_OBJ_SYMS > host_symtab.txt   (host only, run by the Makefile after the link)

One line per function symbol of the linked ui_shot, sorted by address: "ADDR H|F NAME". H = the symbol comes from one of the host's own objects
(host_*.cpp, the console's Mac side, the Keys stand-in: names listed in HOST_OBJ_SYMS), F = firmware code (core/, synth-open/, devtools core).
host_heapmodel.cpp reads it to tell who called operator new: dladdr cannot see local (anonymous namespace) symbols, nm can.
"""
import subprocess
import sys

host = set(l.strip() for l in open(sys.argv[2]) if l.strip())
out = subprocess.run(['nm', '-n', '-U', sys.argv[1]], capture_output=True, text=True, check=True).stdout
for ln in out.splitlines():
    p = ln.split(None, 2)
    if len(p) != 3 or p[1] not in ('T', 't'):
        continue
    name = p[2][1:] if p[2].startswith('_') else p[2]
    print('%s %s %s' % (p[0], 'H' if name in host else 'F', name))
