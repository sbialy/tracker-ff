#!/usr/bin/env python3
"""USB test runner for the original Polyend Tracker (and the same firmware code on the Mac). Host tool, NOT firmware.

Talks the line protocol of docs/usb-test-console.md over

  - the device's CDC serial port (auto-detects /dev/cu.usbmodem*, or USBTEST_PORT, or --port PATH), or
  - the Mac build of the firmware's UI (--sim ui_shot --sd DIR), the same console code without a device.

  python3 -I usbtest.py --sim build/ui-shot/ui_shot --sd /tmp/sd scripts/smoke.ut
  usbtest.py --port auto scripts/smoke.ut --cpu-thresholds cpu_thresholds.json
  usbtest.py --port auto cmd ping            # one raw command, prints the reply
  usbtest.py --port auto update FW.ptf       # firmware update: put, verify, hand off to the bootloader, wait, confirm the build id

Script format (one command per line, '#' starts a comment, words are split like a shell, quote names with spaces):

  ping | wait MS | tap NAME [N] | press NAME | release NAME | pad N [tap|press|release] | enc DELTA | idle
  status | texts | markers | peek NAME | hash-project | hash-file PATH | cpu-reset | echo TEXT | raw LINE...
  expect page NAME | expect text SUBSTRING | expect no-text SUBSTRING | expect status KEY=VALUE | expect peek NAME VALUE
  shot FILE.png | shotcrc NAME | shotcrc-save NAME FILE.json | shotcrc-expect NAME FILE.json [MAX_TILES]
  compare A.png B.png [MAX_PERCENT] | cpu-check NAME [SAMPLES]
  open PATH NAME [force] | play song|pattern [POS] | stop | ls PATH | get PATH FILE | put FILE PATH | rm PATH | reboot

Exit status 0 when every command and every expect passed.
"""
import argparse
import glob
import json
import os
import re
import select
import shlex
import struct
import subprocess
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'ui_shot'))
sys.path.insert(0, HERE)   # a copy of png_cmp.py next to the runner works as well
try:
    import ptfcheck  # host copy of the device's .ptf checks, needed by the update flow
except Exception:  # noqa: BLE001
    ptfcheck = None
try:
    import png_cmp  # layout tool next to ui_shot, only its PNG reader is used
except Exception:  # noqa: BLE001
    png_cmp = None

SHOT_W, SHOT_H = 800, 480
TILE_W, TILE_H = 100, 80

TERMINAL = re.compile(r'^@(ok|err|idle|tracker-dev|cpu|mem|status|markers|peek|hash|end|advanced|linked|rated)( |$)')


class UsbTestError(Exception):
    pass


class Disconnected(UsbTestError):
    def __init__(self, msg='disconnected', reboot=False):
        super().__init__(msg)
        self.reboot = reboot


class Timeout(UsbTestError):
    pass


# ------------------------------------------------------------------------------------------------ codec (mirror of core/source/UsbTestCore.cpp)
def crc32(data, crc=0):
    return zlib.crc32(data, crc) & 0xFFFFFFFF


def rle_decode(body, prev, count=SHOT_W):
    """rle1: PackBits on 32 bit little endian words of (pixel XOR the pixel above). prev is the row above (bytes) or None."""
    words = []
    n, i = len(body), 0
    while i < n and len(words) < count:
        c = body[i]
        i += 1
        if c < 0x80:
            k = c + 1
            if i + 4 * k > n:
                raise UsbTestError('rle: truncated literal')
            words.extend(struct.unpack_from('<%dI' % k, body, i))
            i += 4 * k
        else:
            k = (c & 0x7F) + 2
            if i + 4 > n:
                raise UsbTestError('rle: truncated run')
            words.extend([struct.unpack_from('<I', body, i)[0]] * k)
            i += 4
    if len(words) != count or i != n:
        raise UsbTestError('rle: row has %d words, %d bytes unused' % (len(words), n - i))
    delta = struct.pack('<%dI' % count, *words)
    if prev is None:
        return delta
    a = int.from_bytes(delta, 'little') ^ int.from_bytes(prev, 'little')
    return a.to_bytes(len(delta), 'little')


def write_png(path, width, height, rgb_rows):
    raw = b''.join(b'\x00' + r for r in rgb_rows)

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)

    ihdr = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def bgra_row_to_rgb(row):
    rgb = bytearray(len(row) // 4 * 3)
    rgb[0::3] = row[2::4]
    rgb[1::3] = row[1::4]
    rgb[2::3] = row[0::4]
    return bytes(rgb)


def tile_crcs_of_rows(rows_bgra):
    """48 tile CRCs (6 x 8, row major) over B G R of the pixels, what shotcrc reports."""
    out = []
    for ty in range(SHOT_H // TILE_H):
        crcs = [0] * (SHOT_W // TILE_W)
        for y in range(ty * TILE_H, (ty + 1) * TILE_H):
            row = rows_bgra[y]
            for tx in range(SHOT_W // TILE_W):
                seg = row[tx * TILE_W * 4:(tx + 1) * TILE_W * 4]
                bgr = bytearray(TILE_W * 3)
                bgr[0::3] = seg[0::4]
                bgr[1::3] = seg[1::4]
                bgr[2::3] = seg[2::4]
                crcs[tx] = crc32(bytes(bgr), crcs[tx])
        out.extend(crcs)
    return out


def png_pixels(path):
    if png_cmp is None:
        raise UsbTestError('png_cmp.py not found next to ui_shot')
    w, h, bpp, rows = png_cmp.read_png(path)
    return w, h, bpp, rows


def png_diff(path_a, path_b):
    """(differing pixels, percent, bounding box or None) of two PNGs of the same size."""
    wa, ha, ba, ra = png_pixels(path_a)
    wb, hb, bb, rb = png_pixels(path_b)
    if (wa, ha) != (wb, hb):
        raise UsbTestError('size differs: %dx%d against %dx%d' % (wa, ha, wb, hb))
    diff, x0, y0, x1, y1 = 0, wa, ha, -1, -1
    for y in range(ha):
        a, b = ra[y], rb[y]
        if ba == bb and a == b:
            continue
        for x in range(wa):
            if bytes(a[x * ba:x * ba + 3]) != bytes(b[x * bb:x * bb + 3]):
                diff += 1
                x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
    return diff, 100.0 * diff / (wa * ha), ((x0, y0, x1, y1) if diff else None)


# ------------------------------------------------------------------------------------------------ transports
class Transport:
    """Byte stream with line reads. Subclasses implement _read_some, write, close, wait_ms and reconnect."""

    def __init__(self):
        self.buf = bytearray()

    def _read_some(self, timeout):
        raise NotImplementedError

    def write(self, data):
        raise NotImplementedError

    def _fill(self, deadline):
        left = deadline - time.monotonic()
        if left <= 0:
            raise Timeout('timeout waiting for the device')
        chunk = self._read_some(min(left, 0.5))
        if chunk:
            self.buf += chunk

    def read_line(self, timeout=5.0):
        deadline = time.monotonic() + timeout
        while True:
            i = self.buf.find(b'\n')
            if i >= 0:
                line = bytes(self.buf[:i])
                del self.buf[:i + 1]
                return line.rstrip(b'\r').decode('latin-1')
            self._fill(deadline)

    def read_exact(self, n, timeout=5.0):
        deadline = time.monotonic() + timeout
        while len(self.buf) < n:
            self._fill(deadline)
        data = bytes(self.buf[:n])
        del self.buf[:n]
        return data

    def wait_ms(self, ms, keep=False):
        time.sleep(ms / 1000.0)

    def close(self):
        pass

    def reconnect(self, timeout=40.0):
        raise UsbTestError('reconnect not supported')

    def node_present(self):
        return True

    def drain(self, quiet=0.1):
        """Throw away whatever is queued (log text from before the command)."""
        self.buf.clear()
        end = time.monotonic() + quiet
        while time.monotonic() < end:
            try:
                chunk = self._read_some(0.02)
            except Disconnected:
                return
            if chunk:
                end = time.monotonic() + quiet


class SimTransport(Transport):
    """ui_shot --serve: the firmware's console and UI on the Mac. Time is virtual, wait_ms advances it."""

    def __init__(self, binary, sd_dir, extra=()):
        super().__init__()
        self.binary, self.sd_dir, self.extra = binary, sd_dir, list(extra)
        self.proc = None
        self.start()

    def start(self):
        self.buf.clear()
        self.proc = subprocess.Popen([self.binary, '--serve', self.sd_dir] + self.extra, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)
        deadline = time.monotonic() + 120
        while True:
            line = self.read_line(max(1.0, deadline - time.monotonic()))
            if line.startswith('ui_shot: serve ready'):
                return

    def _read_some(self, timeout):
        fd = self.proc.stdout.fileno()
        r, _, _ = select.select([fd], [], [], timeout)
        if not r:
            return b''
        data = os.read(fd, 65536)
        if not data:
            code = self.proc.wait()
            raise Disconnected('ui_shot exited with %s' % code, reboot=(code == 77))
        return data

    def write(self, data):
        try:
            self.proc.stdin.write(data)
        except BrokenPipeError:
            raise Disconnected('ui_shot is gone')

    def wait_ms(self, ms, keep=False):
        """keep=True hands the lines that the device sent while time advanced to the next read (an interrupted put answers in there)."""
        self.write(('_advance %d\n' % ms).encode())
        deadline = time.monotonic() + 120
        kept = []
        while True:
            line = self.read_line(max(1.0, deadline - time.monotonic()))
            if line.startswith('@advanced'):
                if kept:
                    self.buf[0:0] = ''.join(l + '\n' for l in kept).encode('latin-1')
                return
            if keep and line.startswith('@'):
                kept.append(line)

    def side(self, line):
        """Host only line (_link 0|1, _rate N), returns when acknowledged."""
        self.write((line + '\n').encode())
        while True:
            if self.read_line(30).startswith(('@linked', '@rated', '@rawlimited')):
                return

    def close(self):
        if self.proc and self.proc.poll() is None:
            try:
                self.proc.stdin.close()
            except Exception:  # noqa: BLE001
                pass
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()

    def reconnect(self, timeout=40.0):
        """Restart the Mac build. A simulated bootloader that does not return (UISHOT_BOOTLOADER_STUCK) exits at once, keep trying until `timeout`."""
        self.close()
        end = time.monotonic() + timeout
        while True:
            try:
                self.start()
                return
            except Disconnected:
                if time.monotonic() > end:
                    raise UsbTestError('the device did not come back within %d s' % timeout)
                time.sleep(0.2)


class SerialTransport(Transport):
    """The device's CDC port. pyserial when installed, plain termios otherwise."""

    def __init__(self, port):
        super().__init__()
        self.port = port
        self.ser = None
        self.fd = None
        self.open()

    @staticmethod
    def find_port(wait=0.0):
        end = time.monotonic() + wait
        while True:
            env = os.environ.get('USBTEST_PORT')
            ports = [env] if env else sorted(glob.glob('/dev/cu.usbmodem*'))
            if len(ports) == 1:
                return ports[0]
            if len(ports) > 1:
                raise UsbTestError('several serial ports, pick one with --port or USBTEST_PORT: %s' % ', '.join(ports))
            if time.monotonic() >= end:
                raise UsbTestError('no /dev/cu.usbmodem* port: is the Tracker plugged in, awake and running the test image?')
            time.sleep(0.25)

    def open(self):
        self.buf.clear()
        try:
            import serial  # type: ignore
            self.ser = serial.Serial(self.port, 115200, timeout=0.05, dsrdtr=False)
            self.ser.dtr = True   # the console only talks to a terminal that has the port open
            self.ser.rts = True
            return
        except ImportError:
            self.ser = None
        import termios
        self.fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = 0                      # iflag
        attrs[1] = 0                      # oflag
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[3] = 0                      # lflag: raw
        attrs[4] = attrs[5] = termios.B115200
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)

    def _read_some(self, timeout):
        try:
            if self.ser is not None:
                self.ser.timeout = timeout
                return self.ser.read(4096)
            r, _, _ = select.select([self.fd], [], [], timeout)
            if not r:
                return b''
            data = os.read(self.fd, 65536)
            if not data:
                raise Disconnected('serial port closed')
            return data
        except OSError as e:
            raise Disconnected('serial port error: %s' % e)

    def node_present(self):
        return os.path.exists(self.port)

    def write(self, data):
        try:
            if self.ser is not None:
                self.ser.write(data)
                self.ser.flush()
                return
            view = memoryview(data)
            while view:
                try:
                    n = os.write(self.fd, view)
                    view = view[n:]
                except BlockingIOError:
                    select.select([], [self.fd], [], 1.0)
        except OSError as e:
            raise Disconnected('serial port error: %s' % e)

    def close(self):
        try:
            if self.ser is not None:
                self.ser.close()
            elif self.fd is not None:
                os.close(self.fd)
        except Exception:  # noqa: BLE001
            pass
        self.ser, self.fd = None, None

    def reconnect(self, timeout=40.0):
        """After reboot, a flash or power cycling the node can change: rescan until a port answers."""
        self.close()
        deadline = time.monotonic() + timeout
        time.sleep(1.0)
        while True:
            try:
                self.port = self.find_port(wait=max(0.5, deadline - time.monotonic()))
                self.open()
                return
            except (UsbTestError, OSError):
                if time.monotonic() > deadline:
                    raise UsbTestError('the device did not come back within %d s' % timeout)
                time.sleep(0.5)


# ------------------------------------------------------------------------------------------------ client
class Tracker:
    def __init__(self, transport, heartbeat=1.0):
        self.t = transport
        self.heartbeat = heartbeat
        self.log = []          # non-@ lines seen (device log text)
        self.last_status = {}
        self.tiles = {}

    # ---- low level
    def send(self, line):
        self.t.write(line.encode() + b'\n')

    def _read_reply(self, timeout, on_line=None):
        """Reads until the terminal line of a command. Returns (terminal line, intermediate lines, blobs)."""
        inter, blobs = [], []
        deadline = time.monotonic() + timeout
        while True:
            line = self.t.read_line(max(0.2, deadline - time.monotonic()))
            if not line:
                continue
            if line[0] != '@':
                self.log.append(line)
                continue
            if line.startswith('@row '):
                _, y, n = line.split()
                body = self.t.read_exact(int(n) + 1, timeout)
                blobs.append((int(y), body[:-1]))
                continue
            if line.startswith('@blk '):
                n = int(line.split()[1])
                body = self.t.read_exact(n + 1, timeout)
                blobs.append((None, body[:-1]))
                continue
            if TERMINAL.match(line):
                return line, inter, blobs
            inter.append(line)
            if on_line:
                on_line(line)

    def command(self, line, timeout=10.0, expect_err=False):
        self.send(line)
        term, inter, blobs = self._read_reply(timeout)
        if term.startswith('@err') and not expect_err:
            raise UsbTestError('%s -> %s' % (line, term))
        return term, inter, blobs

    # ---- commands
    def ping(self, timeout=3.0):
        term, _, _ = self.command('ping', timeout)
        m = re.match(r'@tracker-dev (\S+) (\S+) proto=(\d+)', term)
        if not m:
            raise UsbTestError('unexpected ping reply: ' + term)
        return {'product': m.group(1), 'build': m.group(2), 'proto': int(m.group(3))}

    def status(self):
        term, _, _ = self.command('status')
        st = dict(kv.split('=', 1) for kv in term.split()[1:] if '=' in kv)
        self.last_status = st
        return st

    def key(self, name, action='tap', n=1):
        self.command('key %s %s %d' % (shlex.quote(str(name)), action, n))

    def pad(self, n, action='tap', count=1):
        self.command('pad %s %s %d' % (n, action, count))

    def enc(self, delta):
        self.command('enc %d' % delta)

    def idle(self, timeout=30.0):
        self.command('idle', timeout)

    def cpu(self):
        term, _, _ = self.command('cpu')
        if term.startswith('@cpu none'):
            return None
        return {k: int(v) for k, v in (kv.split('=') for kv in term.split()[1:])}

    def texts(self):
        _, inter, _ = self.command('texts', 20)
        out = []
        for l in inter:
            m = re.match(r'@t (\d+) (-?\d+) (-?\d+) (\d+) (\d+) ?(.*)', l)
            if m:
                out.append({'id': int(m.group(1)), 'x': int(m.group(2)), 'y': int(m.group(3)), 'w': int(m.group(4)),
                            'h': int(m.group(5)), 'text': m.group(6)})
        return out

    def shotcrc(self):
        _, inter, _ = self.command('shotcrc', 60)
        tiles = [None] * 48
        for l in inter:
            m = re.match(r'@shotcrc (\d) ((?:[0-9a-f]{8} ?){8})$', l)
            if m:
                row = int(m.group(1))
                for i, h in enumerate(m.group(2).split()):
                    tiles[row * 8 + i] = int(h, 16)
        if None in tiles:
            raise UsbTestError('shotcrc: incomplete reply')
        return tiles

    def shot(self, path=None, force=False):
        """Full screenshot. Returns the rows as BGRA bytes, writes a PNG when path is given."""
        term, _, blobs = self.command('shot force' if force else 'shot', 120)
        rows = [None] * SHOT_H
        prev = None
        for y, body in blobs:
            if len(body) == 0:
                row = prev if prev is not None else bytes(SHOT_W * 4)
            else:
                row = rle_decode(body, prev)
            rows[y] = row
            prev = row
        if any(r is None for r in rows):
            raise UsbTestError('shot: %d rows missing' % sum(1 for r in rows if r is None))
        m = re.search(r'crc=([0-9a-f]{8})', term)
        want = int(m.group(1), 16) if m else None
        crc = 0
        for r in rows:
            crc = crc32(self._bgr(r), crc)
        if want is not None and crc != want:
            raise UsbTestError('shot: image crc %08x, the device says %08x' % (crc, want))
        if path:
            write_png(path, SHOT_W, SHOT_H, [bgra_row_to_rgb(r) for r in rows])
        self.rows = rows
        return rows

    @staticmethod
    def _bgr(row):
        bgr = bytearray(len(row) // 4 * 3)
        bgr[0::3] = row[0::4]
        bgr[1::3] = row[1::4]
        bgr[2::3] = row[2::4]
        return bytes(bgr)

    def ls(self, path='/'):
        _, inter, _ = self.command('ls %s' % shlex.quote(path), 30)
        out = []
        for l in inter:
            m = re.match(r'@([df]) (\d+) (.*)', l)
            if m:
                out.append({'dir': m.group(1) == 'd', 'size': int(m.group(2)), 'name': m.group(3)})
        return out

    def get(self, path, dest=None):
        term, inter, blobs = self.command('get %s' % shlex.quote(path), 300)
        data = b''.join(b for _, b in blobs)
        m = re.match(r'@end (\d+) ([0-9a-f]{8})', term)
        if not m or int(m.group(1)) != len(data) or int(m.group(2), 16) != crc32(data):
            raise UsbTestError('get %s: size or crc mismatch (%s, %d bytes)' % (path, term, len(data)))
        if dest:
            with open(dest, 'wb') as f:
                f.write(data)
        return data

    def put(self, data, path, stop_after=None):
        """stop_after=N is a test aid: send only N bytes, wait for the device to give up (3 s) and return its reply instead of raising."""
        if stop_after is not None and hasattr(self.t, 'side'):
            self.t.side('_rawlimit %d' % stop_after)   # the Mac build stops reading the payload there, the device just gets nothing more
        self.send('put %s %d 0x%08x' % (shlex.quote(path), len(data), crc32(data)))
        line = self._ready_or_err()
        if len(data) == 0:
            return
        if stop_after is not None:
            for i in range(0, stop_after, 1024):
                self.t.write(data[i:min(i + 1024, stop_after)])
            self.t.wait_ms(4000, keep=True)
            term, _, _ = self._read_reply(10)
            return term
        for i in range(0, len(data), 1024):
            self.t.write(data[i:i + 1024])
        term, _, _ = self._read_reply(120)
        if not term.startswith('@ok put'):
            raise UsbTestError('put %s -> %s' % (path, term))

    def _ready_or_err(self):
        deadline = time.monotonic() + 10
        while True:
            line = self.t.read_line(max(0.2, deadline - time.monotonic()))
            if line.startswith('@ready'):
                return line
            if line.startswith('@err'):
                raise UsbTestError('put refused: ' + line)
            if line and line[0] != '@':
                self.log.append(line)

    def rm(self, path):
        self.command('rm %s' % shlex.quote(path))

    def open_project(self, path, name, force=False, timeout=180.0):
        self.command('open %s %s%s' % (shlex.quote(path), shlex.quote(name), ' force' if force else ''), timeout)

    def markers(self):
        term, _, _ = self.command('markers')
        return dict(kv.split('=', 1) for kv in term.split()[1:] if '=' in kv)

    def peek(self, name):
        term, _, _ = self.command('peek %s' % name)
        return int(term.split()[-1])

    def hash_file(self, path):
        term, _, _ = self.command('hash file %s' % shlex.quote(path), 300)
        _, _, crc, size = term.split()
        return crc, int(size)

    def hash_project(self):
        term, _, _ = self.command('hash project')
        return term.split()[2]

    # ---- firmware update commands (core/source/UsbTestFlash.cpp)
    def fw_info(self):
        term, _, _ = self.command('fwcheck')
        return dict(kv.split('=', 1) for kv in term.split() if '=' in kv)

    def fw_check(self, name):
        term, _, _ = self.command('fwcheck %s' % shlex.quote(name), 120)
        return dict(kv.split('=', 1) for kv in term.split() if '=' in kv)

    def fw_promote(self, tmp, name, crc):
        term, _, _ = self.command('fwpromote %s %s 0x%08x' % (shlex.quote(tmp), shlex.quote(name), crc), 120)
        return dict(kv.split('=', 1) for kv in term.split() if '=' in kv)

    def fw_flash(self, name, crc):
        """The confirmed hand-off. Returns the ok line, the device resets about 250 ms later."""
        term, _, _ = self.command('flash %s 0x%08x' % (shlex.quote(name), crc), 120)
        return term

    def wait_reset(self, timeout):
        """After a confirmed hand-off: True once the link dropped (the unit reset), False if it never did. An @err line from the
        device (the hand-off failed after the ok line) raises UsbTestError."""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            try:
                line = self.t.read_line(0.3)
            except Timeout:
                line = ''
            except Disconnected:
                return True
            if line.startswith('@err'):
                raise UsbTestError(line)
            if not self.t.node_present():
                return True
        return False

    def reboot(self, timeout=40.0):
        self.command('reboot')
        try:   # the reset drops the link, the node may change
            end = time.monotonic() + 5
            while time.monotonic() < end:
                self.t._read_some(0.2)
        except Disconnected:
            pass
        self.t.reconnect(timeout)
        end = time.monotonic() + timeout
        while True:
            try:
                self.t.drain(0.2)
                return self.ping(2.0)
            except UsbTestError:
                if time.monotonic() > end:
                    raise
                time.sleep(0.5)

    def wait(self, ms):
        """Sleeps (device) or advances virtual time (sim). On a device it pings once a second: a silent console means a hang."""
        if not isinstance(self.t, SerialTransport) or self.heartbeat <= 0:
            self.t.wait_ms(ms)
            return
        end = time.monotonic() + ms / 1000.0
        while True:
            left = end - time.monotonic()
            if left <= 0:
                return
            time.sleep(min(left, self.heartbeat))
            if end - time.monotonic() > 0.05:
                try:
                    self.ping(2.0)
                except UsbTestError as e:
                    raise UsbTestError('no heartbeat while waiting (hang or fault?): %s' % e)


# ------------------------------------------------------------------------------------------------ firmware update
UPDATE_TMP = '_usbtest.tmp'     # in /Firmware: not a .ptf, so Config > Firmware never lists it
RECOVERY = ('Recovery: power off, take the SD card out, leave exactly one known good .ptf in /Firmware (move the staged image and any other .ptf away), '
            'put the card back and use Config > Firmware, or press the recessed Reset / Emergency Bootloader pinhole on the bottom of the unit. '
            'The bootloader is never written by the console, a failed update does not damage it (docs/usb-test-console.md, Recovery).')


class UpdateFailed(Exception):
    def __init__(self, stage, msg, hand_off=False):
        super().__init__('%s: %s' % (stage, msg))
        self.stage, self.msg, self.hand_off = stage, msg, hand_off


def update_firmware(tr, path, name=None, expect_build=None, check_build=True, timeout=240.0, stage_only=False,
                    interrupt_put=None, say=print):
    """Firmware update over the console. Stages: check the file here, check the device, put to a temp name, the device re-reads it and
    validates it (fwpromote renames it into /Firmware), the confirmed hand-off (flash NAME 0xCRC), wait for the unit to leave and come
    back, compare the build id with the one in the .ptf. Returns a dict, raises UpdateFailed. The bootloader is only reached by the
    last device command, after everything before it passed."""
    if ptfcheck is None:
        raise UpdateFailed('setup', 'ptfcheck.py not found next to usbtest.py')
    t0 = time.monotonic()
    with open(path, 'rb') as f:
        data = f.read()
    name = name or os.path.basename(path)
    crc = crc32(data)

    def step(n, text):
        say('[%d/7] %6.1f s  %s' % (n, time.monotonic() - t0, text))

    # 1. the file and the name, here
    verdict = ptfcheck.validate(data)
    if not verdict.ok:
        raise UpdateFailed('file', '%s is not a flashable MK66 image: %s (record %d)' % (path, verdict.error, verdict.record))
    if not (ptfcheck.MIN_FILE_BYTES <= len(data) <= ptfcheck.MAX_FILE_BYTES):
        raise UpdateFailed('file', 'size %d outside %d..%d' % (len(data), ptfcheck.MIN_FILE_BYTES, ptfcheck.MAX_FILE_BYTES))
    err = ptfcheck.name_error(name)
    if err != 'ok':
        raise UpdateFailed('name', '%s: %s (31 characters at most, PolyendTrac... .ptf, letters digits . _ -)' % (name, err))
    expected = expect_build or (ptfcheck.find_build_id(data) if check_build else None)
    if check_build and not expected:
        raise UpdateFailed('build-id', 'the image has no unique 7 digit build id string (a pinned BUILD_TAG), pass --expect-build ID '
                                       'or --no-build-check')
    step(1, '%s: %d B, crc %08x, image top 0x%X, name %s ok, expected build %s' % (os.path.basename(path), len(data), crc, verdict.top, name,
                                                                                    expected or 'not compared'))

    # 2. the device
    try:
        before = tr.ping(3.0)
        st = tr.status()
    except UsbTestError as e:
        raise UpdateFailed('device', 'no console answer: %s' % e)
    if before['proto'] != 1:
        raise UpdateFailed('device', 'console protocol %s, this runner speaks 1' % before['proto'])
    if st.get('play') != '0' or st.get('fm') != '0':
        raise UpdateFailed('device', 'the unit is playing or busy (play=%s fm=%s), stop it first' % (st.get('play'), st.get('fm')))
    up_before, seen_before = int(st.get('up', '0')), time.monotonic()
    info = tr.fw_info()
    free = info.get('free', 'unknown')
    if free != 'unknown' and int(free) < len(data) + ptfcheck.SPARE_BYTES:
        raise UpdateFailed('space', 'the card has %s B free, %d B needed (file plus %d B spare)' % (free, len(data) + ptfcheck.SPARE_BYTES,
                                                                                                   ptfcheck.SPARE_BYTES))
    step(2, 'device %s build %s up %d s, free space %s' % (before['product'], before['build'], up_before, free))

    # 3. put to the temp name
    tmp_path = '/Firmware/' + UPDATE_TMP
    try:
        reply = tr.put(data, tmp_path, stop_after=interrupt_put)
    except UsbTestError as e:
        raise UpdateFailed('put', str(e))
    if interrupt_put is not None:
        raise UpdateFailed('put', 'interrupted after %d bytes on purpose, the device said: %s' % (interrupt_put, reply))
    step(3, 'put %s, %d B' % (UPDATE_TMP, len(data)))

    # 4. the device re-reads the temp file, validates it and renames it into /Firmware
    try:
        promoted = tr.fw_promote(UPDATE_TMP, name, crc)
    except UsbTestError as e:
        try:
            tr.command('rm %s' % tmp_path, expect_err=True)
        except UsbTestError:
            pass
        raise UpdateFailed('verify', str(e))
    if promoted.get('size') != str(len(data)) or promoted.get('crc') != '%08x' % crc:
        raise UpdateFailed('verify', 'the device read back %s, the file is %d B crc %08x' % (promoted, len(data), crc))
    step(4, 'device re-read /Firmware/%s: crc %s size %s, validated and renamed' % (name, promoted['crc'], promoted['size']))
    if stage_only:
        say('staged only: nothing was handed to the bootloader. To flash it: flash %s 0x%08x' % (name, crc))
        return {'staged': True, 'name': name, 'crc': crc, 'seconds': time.monotonic() - t0}

    # 5. the hand-off
    try:
        ok = tr.fw_flash(name, crc)
    except UsbTestError as e:
        raise UpdateFailed('hand-off', str(e))
    t_handoff = time.monotonic()
    step(5, 'hand-off accepted (%s)' % ok)

    # 6. the unit leaves, the bootloader flashes it, the console comes back
    try:
        gone = tr.wait_reset(30.0)
    except UsbTestError as e:
        raise UpdateFailed('hand-off', 'the device took the confirmation and then reported: %s (nothing was handed over)' % e)
    if not gone:
        raise UpdateFailed('reset', 'the unit never dropped off the bus within 30 s of the hand-off', hand_off=True)
    step(6, 'link dropped %.1f s after the hand-off, waiting for the console (up to %d s)' % (time.monotonic() - t_handoff, timeout))
    try:
        tr.t.reconnect(timeout)
    except UsbTestError as e:
        raise UpdateFailed('re-enumerate', str(e), hand_off=True)
    after, end = None, t_handoff + timeout
    while after is None:
        try:
            tr.t.drain(0.2)
            after = tr.ping(2.0)
        except UsbTestError as e:
            if time.monotonic() > end:
                raise UpdateFailed('re-enumerate', 'no console answer after the reset: %s' % e, hand_off=True)
            time.sleep(0.5)
    back = time.monotonic()

    # 7. is it the new build, and did it really restart
    try:
        up_after = int(tr.status().get('up', '0'))
    except UsbTestError as e:
        raise UpdateFailed('status', str(e), hand_off=True)
    # The uptime counter started over. Only meaningful on the real unit, in the Mac build the clock is virtual (there the exit of the process,
    # which wait_reset saw, is the proof of the restart).
    restarted = up_after + 1 < up_before + (back - seen_before) or not isinstance(tr.t, SerialTransport)
    if not restarted:
        raise UpdateFailed('restart', 'the uptime did not restart (%d s before, %d s after)' % (up_before, up_after), hand_off=True)
    if check_build and after['build'] != expected:
        raise UpdateFailed('build-id', 'the unit reports build %s, the image has %s' % (after['build'], expected), hand_off=True)
    total = time.monotonic() - t0
    step(7, 'console back %.1f s after the hand-off, build %s, uptime %d s' % (back - t_handoff, after['build'], up_after))
    return {'staged': False, 'name': name, 'crc': crc, 'before': before['build'], 'after': after['build'], 'expected': expected,
            'seconds': total, 'bootloader_seconds': back - t_handoff, 'same_build': before['build'] == after['build']}


# ------------------------------------------------------------------------------------------------ scripts
class Failure(Exception):
    pass


class ScriptRunner:
    def __init__(self, tracker, outdir, cpu_thresholds=None, verbose=True):
        self.tr = tracker
        self.outdir = outdir
        self.cpu_thresholds = cpu_thresholds or {}
        self.verbose = verbose
        self.results = []
        self.failures = 0

    def say(self, msg):
        if self.verbose:
            print(msg, flush=True)

    def path(self, p):
        return p if os.path.isabs(p) else os.path.join(self.outdir, p)

    def run_file(self, script):
        self.say('== %s' % script)
        with open(script) as f:
            lines = f.read().splitlines()
        for no, raw in enumerate(lines, 1):
            line = raw.split('#', 1)[0].strip()
            if not line:
                continue
            words = shlex.split(line)
            t0 = time.monotonic()
            try:
                out = self.dispatch(words)
                status = 'ok'
            except Failure as e:
                out, status = str(e), 'FAIL'
                self.failures += 1
            except UsbTestError as e:
                out, status = str(e), 'ERROR'
                self.failures += 1
            self.results.append({'script': os.path.basename(script), 'line': no, 'cmd': line, 'status': status,
                                 'info': out if out is None or isinstance(out, (str, int, float, dict, list)) else str(out),
                                 'seconds': round(time.monotonic() - t0, 3)})
            self.say('%-5s %s:%d  %s%s' % (status, os.path.basename(script), no, line, ('   -> ' + str(out)) if out else ''))
            if status == 'ERROR':
                raise UsbTestError('script stopped at %s:%d' % (script, no))
        return self.failures == 0

    def expect(self, cond, msg):
        if not cond:
            raise Failure(msg)

    def dispatch(self, w):
        tr, c, a = self.tr, w[0], w[1:]
        if c == 'ping':
            return tr.ping()
        if c == 'wait':
            tr.wait(int(a[0]))
        elif c == 'tap':
            tr.key(a[0], 'tap', int(a[1]) if len(a) > 1 else 1)
        elif c in ('press', 'release'):
            tr.key(a[0], c)
        elif c == 'pad':
            tr.pad(a[0], a[1] if len(a) > 1 else 'tap', int(a[2]) if len(a) > 2 else 1)
        elif c == 'enc':
            tr.enc(int(a[0]))
        elif c == 'idle':
            tr.idle()
        elif c == 'status':
            return tr.status()
        elif c == 'texts':
            t = tr.texts()
            return ' | '.join(x['text'] for x in t)
        elif c == 'markers':
            return tr.markers()
        elif c == 'peek':
            return tr.peek(a[0])
        elif c == 'hash-project':
            return tr.hash_project()
        elif c == 'hash-file':
            crc, size = tr.hash_file(a[0])
            return '%s %d' % (crc, size)
        elif c == 'cpu-reset':
            tr.command('cpu reset')
        elif c == 'echo':
            return ' '.join(a)
        elif c == 'raw':
            return tr.command(' '.join(a), expect_err=True)[0]
        elif c == 'expect':
            return self.do_expect(a)
        elif c == 'shot':
            tr.shot(self.path(a[0]))
            return a[0]
        elif c == 'shotcrc':
            tr.tiles[a[0]] = tr.shotcrc()
        elif c == 'shotcrc-save':
            with open(self.path(a[1]), 'w') as f:
                json.dump(tr.tiles[a[0]], f)
        elif c == 'shotcrc-expect':
            with open(self.path(a[1])) as f:
                want = json.load(f)
            got = tr.tiles[a[0]]
            bad = [i for i in range(48) if got[i] != want[i]]
            limit = int(a[2]) if len(a) > 2 else 0
            self.expect(len(bad) <= limit, '%d tiles differ (allowed %d): %s' % (len(bad), limit, bad))
            return '%d tiles differ' % len(bad)
        elif c == 'compare':
            diff, pct, box = png_diff(self.path(a[0]), self.path(a[1]))
            limit = float(a[2]) if len(a) > 2 else 0.0
            self.expect(pct <= limit, '%d pixels (%.3f %%) differ, box %s, allowed %.3f %%' % (diff, pct, box, limit))
            return '%d pixels differ' % diff
        elif c == 'cpu-check':
            return self.cpu_check(a[0], int(a[1]) if len(a) > 1 else 3)
        elif c == 'open':
            tr.open_project(a[0], a[1], force=len(a) > 2 and a[2] == 'force')
        elif c == 'play':
            tr.command('play ' + ' '.join(a))
        elif c == 'stop':
            tr.command('stop')
        elif c == 'ls':
            return ', '.join(('%s/' if e['dir'] else '%s') % e['name'] for e in tr.ls(a[0]))
        elif c == 'get':
            return len(tr.get(a[0], self.path(a[1])))
        elif c == 'put':
            with open(self.path(a[0]), 'rb') as f:
                data = f.read()
            tr.put(data, a[1])
            return len(data)
        elif c == 'rm':
            tr.rm(a[0])
        elif c == 'reboot':
            return tr.reboot()
        else:
            raise UsbTestError('unknown script command: %s' % c)
        return None

    def do_expect(self, a):
        tr = self.tr
        what = a[0]
        if what == 'page':
            st = tr.status()
            self.expect(st.get('name') == a[1], 'page is %s, expected %s' % (st.get('name'), a[1]))
        elif what == 'status':
            st = tr.status()
            k, v = a[1].split('=', 1)
            self.expect(st.get(k) == v, '%s is %s, expected %s' % (k, st.get(k), v))
        elif what in ('text', 'no-text'):
            texts = [x['text'] for x in tr.texts()]
            found = any(a[1] in t for t in texts)
            self.expect(found if what == 'text' else not found, '"%s" %s on screen; texts: %s' %
                        (a[1], 'not found' if what == 'text' else 'found', texts[:20]))
        elif what == 'peek':
            v = tr.peek(a[1])
            self.expect(v == int(a[2]), '%s is %d, expected %s' % (a[1], v, a[2]))
        else:
            raise UsbTestError('unknown expect: %s' % what)
        return None

    def cpu_check(self, name, samples):
        """Median of SAMPLES readings of max and avg (microseconds), one second of device time apart, against the budget file."""
        budget = self.cpu_thresholds.get(name)
        if budget is None:
            raise UsbTestError('no cpu budget named %s' % name)
        maxes, avgs = [], []
        for i in range(samples):
            if i:
                self.tr.wait(1000)
            c = self.tr.cpu()
            if c is None:
                raise Failure('no audio blocks measured yet (cpu reset was just done?)')
            maxes.append(c['max'])
            avgs.append(c['avg'])
        mx, av = sorted(maxes)[len(maxes) // 2], sorted(avgs)[len(avgs) // 2]
        self.expect(mx <= budget['max_us'] and av <= budget['avg_us'],
                    'max %d us (budget %d), avg %d us (budget %d)' % (mx, budget['max_us'], av, budget['avg_us']))
        return 'max %d us, avg %d us' % (mx, av)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', help='serial port, or "auto" (default when no --sim)')
    ap.add_argument('--sim', help='path of the ui_shot binary: run against the Mac build')
    ap.add_argument('--sd', default=None, help='SD card folder of the sim (default: a temporary one)')
    ap.add_argument('--sim-arg', action='append', default=[], help='extra argument for ui_shot --serve (repeatable)')
    ap.add_argument('--out', default='.', help='folder for screenshots and downloads')
    ap.add_argument('--cpu-thresholds', help='JSON: {"name": {"max_us": N, "avg_us": N}}')
    ap.add_argument('--report', help='write the results as JSON')
    ap.add_argument('--no-heartbeat', action='store_true')
    ap.add_argument('--ptf-name', help='update: name in /Firmware (default: the file name, 31 characters at most)')
    ap.add_argument('--expect-build', help='update: build id the unit must report afterwards (default: the 7 digit id inside the .ptf)')
    ap.add_argument('--no-build-check', action='store_true', help='update: do not compare the build id (the restart is still checked)')
    ap.add_argument('--update-timeout', type=float, default=240.0, help='update: seconds to wait for the console after the hand-off')
    ap.add_argument('--stage-only', action='store_true', help='update: put, verify and rename, but do not hand off to the bootloader')
    ap.add_argument('--test-interrupt-put', type=int, help='update: test aid, stop sending the file after N bytes')
    ap.add_argument('items', nargs='+', help='script files, or: cmd LINE..., or: update FILE.ptf')
    args = ap.parse_args(argv)

    os.makedirs(args.out, exist_ok=True)
    if args.sim:
        sd = args.sd or os.path.join(os.environ.get('TMPDIR', '/tmp'), 'usbtest_sd_%d' % os.getpid())
        transport = SimTransport(args.sim, sd, args.sim_arg)
    else:
        port = args.port if args.port and args.port != 'auto' else SerialTransport.find_port()
        transport = SerialTransport(port)
    tracker = Tracker(transport, heartbeat=0 if args.no_heartbeat else 1.0)
    thresholds = {}
    if args.cpu_thresholds:
        with open(args.cpu_thresholds) as f:
            thresholds = json.load(f)
    runner = ScriptRunner(tracker, args.out, thresholds)
    ok = True
    try:
        transport.drain(0.2)
        if args.items[0] == 'update':
            if len(args.items) != 2:
                raise UsbTestError('usage: usbtest.py ... update FILE.ptf')
            try:
                res = update_firmware(tracker, args.items[1], name=args.ptf_name, expect_build=args.expect_build,
                                      check_build=not args.no_build_check, timeout=args.update_timeout, stage_only=args.stage_only,
                                      interrupt_put=args.test_interrupt_put)
                if not res['staged']:
                    note = ' (the same build as before: a no-op upgrade, the restart is what proves the flash)' if res['same_build'] else ''
                    print('update PASS in %.1f s (bootloader and re-enumeration %.1f s): build %s -> %s%s' % (
                        res['seconds'], res['bootloader_seconds'], res['before'], res['after'], note))
            except UpdateFailed as e:
                ok = False
                print('update FAIL at %s: %s' % (e.stage, e.msg), file=sys.stderr)
                if e.hand_off:
                    print('The bootloader was started. ' + RECOVERY, file=sys.stderr)
                else:
                    print('Nothing was handed to the bootloader, the unit runs as before.', file=sys.stderr)
        elif args.items[0] == 'cmd':
            term, inter, _ = tracker.command(' '.join(args.items[1:]), 60, expect_err=True)
            for l in inter:
                print(l)
            print(term)
            ok = not term.startswith('@err')
        else:
            for script in args.items:
                ok = runner.run_file(script) and ok
    except UsbTestError as e:
        print('ERROR: %s' % e, file=sys.stderr)
        ok = False
    finally:
        if args.report:
            with open(args.report, 'w') as f:
                json.dump({'ok': ok, 'results': runner.results, 'device_log': tracker.log[-200:]}, f, indent=1)
        transport.close()
    n = len(runner.results)
    if n:
        print('%d commands, %d failed' % (n, runner.failures))
    return 0 if ok and runner.failures == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
