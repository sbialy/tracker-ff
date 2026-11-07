#!/usr/bin/env python3
"""Synthetic Intel HEX .ptf images for the firmware update tests. Host tool, NOT firmware.

build() makes a small image that passes check_ptf.py and ptfcheck.py (vector table, stock flash configuration, a build id string, 64 KB
segment records like the stock images). corpus() returns variants with the verdict both validators must give. The files are not
bootable, they only have to look right to the validators and to the simulated bootloader of ui_shot.

  python3 -I ptfgen.py OUTDIR      writes the corpus as files plus verdicts.txt (name verdict), used by usbtest_flash_test.sh
"""
import os
import random
import sys

STOCK_CONFIG = bytes([0xFF] * 12 + [0xDE, 0xF9, 0xFF, 0xFF])


def record(rtype, addr, data=b''):
    body = bytes([len(data), (addr >> 8) & 0xFF, addr & 0xFF, rtype]) + bytes(data)
    return ':%s%02X' % (body.hex().upper(), (-sum(body)) & 0xFF)


def image_bytes(size=0x12000, build_id='abc1234', sp=0x20030000, reset=0x1D1, config=STOCK_CONFIG, seed=1):
    rnd = random.Random(seed)
    mem = bytearray(rnd.randrange(256) for _ in range(size))
    mem[0:4] = sp.to_bytes(4, 'little')
    mem[4:8] = reset.to_bytes(4, 'little')
    mem[0x400:0x410] = config
    if build_id:
        mem[0x2000:0x2000 + len(build_id) + 2] = b'\x00' + build_id.encode() + b'\x00'
    return mem


def hex_lines(mem, start=0):
    lines, seg = [], -1
    for base in range(0, len(mem), 16):
        chunk = bytes(mem[base:base + 16])
        address = start + base
        if address >> 16 != seg and address >= 0x10000:
            seg = address >> 16
            lines.append(record(2, 0, ((seg << 12) & 0xFFFF).to_bytes(2, 'big')))
        elif seg < 0:
            seg = 0
        lines.append(record(0, address & 0xFFFF, chunk))
    return lines


def build(eol='\r\n', **kw):
    """A good image by default. Keywords go to image_bytes (size, build_id, sp, reset, config)."""
    lines = hex_lines(image_bytes(**kw)) + [record(1, 0)]
    return (eol.join(lines) + eol).encode()


def corpus():
    """(name, bytes, verdict) with the verdict of ptfcheck.validate / PtfValidator::finish."""
    good = build()
    lines = good.decode().split('\r\n')[:-1]
    out = [('good', good, 'ok'), ('good-lf', build(eol='\n'), 'ok'),
           ('good-trailing-blank', good + b'\r\n\r\n', 'ok'),
           ('good-other-id', build(build_id='0123abc'), 'ok')]

    def variant(name, mutate, verdict):
        ls = list(lines)
        mutate(ls)
        out.append((name, ('\r\n'.join(ls) + '\r\n').encode(), verdict))

    def flip(ls):
        s = ls[100]
        ls[100] = s[:12] + ('0' if s[12] != '0' else '1') + s[13:]
    variant('bad-checksum', flip, 'bad-checksum')
    variant('no-eof', lambda ls: ls.pop(), 'no-eof')
    variant('bad-char', lambda ls: ls.__setitem__(50, ls[50][:9] + 'G' + ls[50][10:]), 'bad-char')
    variant('record-type-04', lambda ls: ls.insert(60, record(4, 0, b'\x00\x01')), 'record-type')
    variant('record-type-05', lambda ls: ls.insert(60, record(5, 0, b'\x00\x00\x00\x00')), 'record-type')
    variant('short-record', lambda ls: ls.__setitem__(70, ls[70][:-4] + ls[70][-2:]), 'bad-length')
    variant('long-record', lambda ls: ls.__setitem__(70, ls[70] + '00'), 'bad-length')
    variant('after-eof', lambda ls: ls.append('hello'), 'after-eof')
    variant('after-eof-record', lambda ls: ls.append(record(0, 0, b'\x00')), 'after-eof')
    variant('no-colon-first', lambda ls: ls.__setitem__(0, ls[0][1:]), 'not-intel-hex')
    variant('not-between-records', lambda ls: ls.insert(30, 'garbage'), 'bad-char')
    out.append(('truncated-mid-record', good[:len(good) // 2 + 7], 'truncated'))
    out.append(('empty-file', b'', 'empty'))
    out.append(('only-whitespace', b'\r\n  \r\n', 'empty'))
    out.append(('binary-garbage', bytes(random.Random(5).randrange(256) for _ in range(6000)), 'not-intel-hex'))
    out.append(('eof-only', (record(1, 0) + '\r\n').encode(), 'empty'))
    out.append(('beyond-limit', build(size=0xFF000 + 0x100), 'beyond-limit'))
    out.append(('at-limit-ok', build(size=0xFF000), 'ok'))
    out.append(('flash-config', build(config=bytes([0xFF] * 12 + [0xFE, 0xF9, 0xFF, 0xFF])), 'flash-config'))
    out.append(('flash-config-unlocked', build(config=bytes([0xFF] * 16)), 'flash-config'))
    # another platform: the Tracker+ and Mini are i.MX RT (flash config differs, stack and flash addresses are elsewhere)
    out.append(('wrong-platform-sp', build(sp=0x20280000), 'bad-sp'))
    out.append(('wrong-platform-sp-low', build(sp=0x00001000), 'bad-sp'))
    out.append(('wrong-platform-reset', build(reset=0x6000_2001), 'bad-reset'))
    out.append(('reset-not-thumb', build(reset=0x1D0), 'bad-reset'))
    out.append(('reset-in-vectors', build(reset=0x21), 'bad-reset'))
    shifted = hex_lines(image_bytes(), start=0x1000) + [record(1, 0)]
    out.append(('starts-at-0x1000', ('\r\n'.join(shifted) + '\r\n').encode(), 'start-address'))
    return out


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    os.makedirs(argv[1], exist_ok=True)
    with open(os.path.join(argv[1], 'verdicts.txt'), 'w') as v:
        for name, data, verdict in corpus():
            with open(os.path.join(argv[1], name + '.ptf'), 'wb') as f:
                f.write(data)
            v.write('%s %s\n' % (name, verdict))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
