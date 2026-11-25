#!/usr/bin/env python3
"""imx_sram_cliff.py: read an IMX linker map, print SRAM_DTC and SRAM_ITC free and check the .bss alignment cliff.

Why: qh_buffer (platforms/imx/usb/device/source/ehci/usb_device_ehci.c:71-74) is aligned(2048), so the output section .bss starts on a
0x800 boundary. When .data + .dmabuffers end a few bytes past a boundary, the next bytes of .data move .bss up by 2,048 B and the
SRAM_DTC free figure drops by that much for 16 B of code (Mini at cb4df997: end 0x6810, 2,032 B of padding). Two warnings:
  - the end of .data + .dmabuffers is closer than --margin (default 256 B) below the next boundary (about to step)
  - the end is less than --margin past a boundary (the step was just taken, a few bytes less .data win 2,048 B back)

usage: imx_sram_cliff.py [--strict] [--align 0x800] [--margin 256] [--name LABEL] imx.map [more.map ...]
exit:  0 ok (or warnings without --strict), 1 warning in --strict mode, 2 map not readable or sections missing
"""
import argparse
import re
import sys

SEC_FULL = re.compile(r'^([.A-Za-z_][\w.$]*)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)')
SEC_NAME = re.compile(r'^([.A-Za-z_][\w.$]*)\s*$')
SEC_CONT = re.compile(r'^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)')
REGION = re.compile(r'^(SRAM_\w+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)')


def parse(path):
    regions = {}
    secs = []  # (name, addr, size) output sections in map order
    in_map = False
    in_cfg = False
    pending = None
    with open(path, errors='replace') as f:
        for line in f:
            if line.startswith('Memory Configuration'):
                in_cfg = True
                continue
            if line.startswith('Linker script and memory map'):
                in_map, in_cfg = True, False
                continue
            if in_cfg:
                m = REGION.match(line)
                if m:
                    regions[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
                continue
            if not in_map:
                continue
            if pending:
                m = SEC_CONT.match(line)
                if m:
                    secs.append((pending, int(m.group(1), 16), int(m.group(2), 16)))
                pending = None
                if m:
                    continue
            m = SEC_FULL.match(line)
            if m:
                secs.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16)))
                continue
            m = SEC_NAME.match(line)
            if m:
                pending = m.group(1)
    return regions, secs


def check(path, label, align, margin):
    regions, secs = parse(path)
    by = {}
    for n, a, s in secs:
        by.setdefault(n, (a, s))
    for need in ('.data', '.bss', '.heap', '.stack'):
        if need not in by:
            print('imx_sram_cliff: %s: section %s not found in %s' % (label, need, path), file=sys.stderr)
            return 2
    dtc = regions.get('SRAM_DTC')
    itc = regions.get('SRAM_ITC')
    if not dtc or not itc:
        print('imx_sram_cliff: %s: no SRAM_DTC / SRAM_ITC in the memory configuration of %s' % (label, path), file=sys.stderr)
        return 2
    d_lo, d_len = dtc
    i_lo, i_len = itc
    bss_a = by['.bss'][0]
    # end of everything placed in the DTC before .bss: .data, then the .dmabuffers orphan (and .uninit_RESERVED)
    pre = [(a + s) for n, a, s in secs if d_lo <= a < bss_a and s > 0 and n != '.bss']
    data_end = max(pre) if pre else by['.data'][0] + by['.data'][1]
    head = (-data_end) % align          # bytes of .data/.dmabuffers growth before .bss moves up by one step
    pad = bss_a - data_end              # padding in front of .bss now
    heap_end = by['.heap'][0] + by['.heap'][1]
    dtc_free = by['.stack'][0] - heap_end
    itc_end = max((a + s) for n, a, s in secs if i_lo <= a < i_lo + i_len and s > 0)
    itc_free = i_lo + i_len - itc_end
    print('%s: .data+.dmabuffers end 0x%x, .bss starts 0x%x (pad %d B), next 0x%x boundary in %d B' % (label, data_end, bss_a, pad, align, head))
    print('%s: SRAM_DTC free %d B (heap end 0x%x to stack base 0x%x), SRAM_ITC free %d B of %d B' %
          (label, dtc_free, heap_end, by['.stack'][0], itc_free, i_len))
    over = data_end % align             # bytes past the last boundary (0 = exactly on it)
    rc = 0
    if head < margin:
        print('WARN  %s: .data+.dmabuffers end is %d B before the 0x%x boundary (< %d B): %d B more .data moves .bss up by %d B, '
              'SRAM_DTC free would drop to %d B' % (label, head, align, margin, head + 1, align, dtc_free - align))
        rc = 1
    if 0 < over < margin or (over == 0 and pad == 0 and data_end > d_lo):
        print('WARN  %s: .data+.dmabuffers end is %d B past a 0x%x boundary (< %d B): .bss sits %d B up because of it, %d B less .data or '
              '.dmabuffers gives %d B back' % (label, over, align, margin, pad, over, align if over else 0))
        rc = 1
    if rc == 0:
        print('ok    %s: cliff margin %d B, pad %d B' % (label, head, pad))
    return rc


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--strict', action='store_true', help='exit 1 on a warning')
    ap.add_argument('--align', type=lambda s: int(s, 0), default=0x800)
    ap.add_argument('--margin', type=int, default=256)
    ap.add_argument('--name', default=None, help='label (default: the map path)')
    ap.add_argument('maps', nargs='+')
    a = ap.parse_args()
    worst = 0
    for m in a.maps:
        try:
            rc = check(m, a.name or m, a.align, a.margin)
        except OSError as e:
            print('imx_sram_cliff: %s' % e, file=sys.stderr)
            rc = 2
        if rc == 2:
            worst = 2
        elif rc == 1 and worst == 0:
            worst = 1
    if worst == 1 and not a.strict:
        print('imx_sram_cliff: warnings only (use --strict to fail)')
        return 0
    return worst


if __name__ == '__main__':
    sys.exit(main())
