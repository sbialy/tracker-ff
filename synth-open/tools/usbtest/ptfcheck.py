#!/usr/bin/env python3
"""Host side checks of a .ptf firmware image for the original Tracker (MK66). Host tool, NOT firmware.

A port of PtfValidator and firmwareNameError in core/source/UsbTestFlash.cpp (the device runs the same rules on the file it holds
before it hands anything to the bootloader), plus the build id lookup the update flow needs. synth-open/tools/check_ptf.py is the
build time check, this module gives the runner the verdict the device will give so a bad file is stopped before a single byte is sent.
usbtest_flash_test.sh runs both validators over the same corpus and compares the verdicts.

  python3 -I ptfcheck.py FILE.ptf [--name NAME]      prints the verdict, exit 0 when the file and the name pass
"""
import re
import sys
import zlib

FLASH_LIMIT = 0xFF000
MIN_FILE_BYTES = 4096
MAX_FILE_BYTES = 6 << 20
NAME_MAX = 31
NAME_MIN = 15
SPARE_BYTES = 1 << 20
SRAM_LOW, SRAM_HIGH = 0x1FFF0000, 0x20030000
STOCK_CONFIG = bytes([0xFF] * 12 + [0xDE, 0xF9, 0xFF, 0xFF])

# the names the device prints (ptfErrorName, nameErrorName)
OK = 'ok'
EMPTY, NOT_HEX, BAD_CHAR, BAD_LENGTH, BAD_CHECKSUM = 'empty', 'not-intel-hex', 'bad-char', 'bad-length', 'bad-checksum'
RECORD_TYPE, TRUNCATED, NO_EOF, AFTER_EOF = 'record-type', 'truncated', 'no-eof', 'after-eof'
BEYOND_LIMIT, FLASH_CONFIG, BAD_SP, BAD_RESET, START_ADDRESS = 'beyond-limit', 'flash-config', 'bad-sp', 'bad-reset', 'start-address'

_HEX = {c: int(chr(c), 16) for c in b'0123456789abcdefABCDEF'}
_SPACE = b' \t\r\n'


class PtfResult:
    def __init__(self):
        self.error = OK
        self.record = 0
        self.top = 0
        self.sp = 0xFFFFFFFF
        self.reset = 0xFFFFFFFF
        self.crc = 0
        self.size = 0

    @property
    def ok(self):
        return self.error == OK

    def __repr__(self):
        return 'PtfResult(%s record=%d top=0x%X sp=0x%08X reset=0x%08X size=%d crc=%08x)' % (
            self.error, self.record, self.top, self.sp, self.reset, self.size, self.crc)


def validate(data, limit=FLASH_LIMIT):
    """Character by character like the device: the first error wins, the final checks run in the same order."""
    r = PtfResult()
    r.size = len(data)
    r.crc = zlib.crc32(data) & 0xFFFFFFFF
    state, digit, pos, count, rtype, total = 0, 0, 0, 0, 0, 0   # state 0 wait colon, 1 high nibble, 2 low nibble, 3 done
    addr, upper, record, low, top = 0, 0, 0, 0xFFFFFFFF, 0
    eof = any_data = False
    segment = [0, 0]
    vectors = bytearray([0xFF] * 8)
    config = bytearray([0xFF] * 16)
    error = OK

    for c in data:
        space = c in _SPACE
        if state == 0:
            if c == 0x3A:
                state, pos, total, record = 1, 0, 0, record + 1
            elif not space:
                error = NOT_HEX if record == 0 else (BAD_LENGTH if c in _HEX else BAD_CHAR)
        elif state in (1, 2):
            v = _HEX.get(c)
            if v is None:
                error = BAD_LENGTH if (space or c == 0x3A) else BAD_CHAR
            elif state == 1:
                digit, state = v, 2
            else:
                state = 1
                b = (digit << 4) | v
                total = (total + b) & 0xFF
                if pos == 0:
                    count = b
                elif pos == 1:
                    addr = b << 8
                elif pos == 2:
                    addr |= b
                elif pos == 3:
                    rtype = b
                    if b > 2:
                        error = RECORD_TYPE
                    elif (b == 1 and count != 0) or (b == 2 and count != 2):
                        error = BAD_LENGTH
                elif pos < 4 + count:
                    index = pos - 4
                    if rtype == 2:
                        segment[index] = b
                        if index == 1:
                            upper = ((segment[0] << 8) | segment[1]) << 4
                    elif rtype == 0:
                        address = upper + addr + index
                        if address >= limit:
                            error = BEYOND_LIMIT
                        else:
                            any_data = True
                            low = min(low, address)
                            top = max(top, address + 1)
                            if address < 8:
                                vectors[address] = b
                            elif 0x400 <= address < 0x410:
                                config[address - 0x400] = b
                else:
                    if total != 0:
                        error = BAD_CHECKSUM
                    elif rtype == 1:
                        eof, state = True, 3
                    else:
                        state = 0
                    pos = -1   # so that pos + 1 below is 0 again; the next record starts at its colon anyway
                pos += 1
        elif not space:   # state 3, after the end of file record
            error = AFTER_EOF
        if error != OK:
            break

    r.record = record
    r.top = top
    r.sp = int.from_bytes(vectors[0:4], 'little')
    r.reset = int.from_bytes(vectors[4:8], 'little')
    if error == OK:
        if state in (1, 2):
            error = TRUNCATED
        elif record == 0:
            error = EMPTY
        elif not eof:
            error = NO_EOF
        elif not any_data:
            error = EMPTY
        elif low != 0:
            error = START_ADDRESS   # before the vectors and the flash configuration: a file that starts elsewhere has neither
        elif bytes(config) != STOCK_CONFIG:
            error = FLASH_CONFIG
        elif not (SRAM_LOW < r.sp <= SRAM_HIGH):
            error = BAD_SP
        elif not (r.reset & 1) or (r.reset & ~1) < 0x40 or (r.reset & ~1) >= top:
            error = BAD_RESET
    r.error = error
    return r


def name_error(name, listed=True):
    """The device's firmwareNameError: OK or one of name-too-short, name-too-long, name-not-ptf, name-no-prefix, name-bad-char."""
    n = len(name)
    if n == 0:
        return 'name-too-short'
    if n > NAME_MAX:
        return 'name-too-long'
    if not re.fullmatch(r'[0-9A-Za-z._-]+', name):
        return 'name-bad-char'
    if not listed:
        return OK
    if n < NAME_MIN:
        return 'name-too-short'
    if not name.endswith('.ptf'):
        return 'name-not-ptf'
    if not name.startswith('PolyendTrac'):
        return 'name-no-prefix'
    return OK


def decode_image(data):
    """Intel HEX text to the bytes of the image (record types 00 and 02, gaps are 0xFF)."""
    mem = bytearray()
    upper = 0
    for line in data.splitlines():
        line = line.strip()
        if len(line) < 11 or line[:1] != b':':
            continue
        raw = bytes.fromhex(line[1:].decode('ascii'))
        count, addr, rtype = raw[0], (raw[1] << 8) | raw[2], raw[3]
        if rtype == 2:
            upper = ((raw[4] << 8) | raw[5]) << 4
        elif rtype == 0:
            end = upper + addr + count
            if end > len(mem):
                mem.extend(b'\xff' * (end - len(mem)))
            mem[upper + addr:end] = raw[4:4 + count]
    return bytes(mem)


def find_build_id(data):
    """The build id the image reports with `ping`: BUILD_TAG, a short git hash, sits in the image as a NUL delimited string of seven
    lower case hex digits. Returns it when exactly one such string exists, otherwise None (a pinned tag like `stk` cannot be found
    this way, pass the id to the runner)."""
    image = decode_image(data)
    found = {m.group(1) for m in re.finditer(rb'(?<![\x20-\x7e])([0-9a-f]{7})\x00', image)}
    return found.pop().decode() if len(found) == 1 else None


def main(argv):
    args = argv[1:]
    name = None
    if '--name' in args:
        i = args.index('--name')
        name = args[i + 1]
        del args[i:i + 2]
    if len(args) != 1:
        print(__doc__)
        return 2
    with open(args[0], 'rb') as f:
        data = f.read()
    r = validate(data)
    print(r)
    bad = not r.ok
    if name is not None:
        e = name_error(name)
        print('name %s: %s' % (name, e))
        bad = bad or e != OK
    print('build id: %s' % find_build_id(data))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
