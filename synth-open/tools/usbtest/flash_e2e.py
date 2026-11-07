#!/usr/bin/env python3
"""End to end check of the firmware update on the Mac, no device: usbtest.py update against ui_shot --serve, which runs the firmware's own
console (core/source/UsbTest*.cpp) with a simulated bootloader (PlatformImpl::runBootloader replaced by a stub that records the call, writes
/Firmware/_fwinfo and ends the process like a reset, the next start installs the named image and reports its build id). Host tool, NOT firmware.

  python3 -I flash_e2e.py UI_SHOT_BINARY WORKDIR

Every case states what must have happened on the "card" (a folder) and in the bootloader call log. The failure cases are the point: in each of
them the log must stay empty, nothing but a good, checked file may end up in /Firmware, and the temp file must be gone.
"""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ptfcheck  # noqa: E402
import ptfgen    # noqa: E402

GOOD = 'PolyendTracker_TEST_1.2.3.ptf'
passed = failed = 0


def check(cond, msg):
    global passed, failed
    if cond:
        passed += 1
    else:
        failed += 1
        print('  FAIL: %s' % msg)


class Case:
    def __init__(self, ui, work, name):
        self.ui, self.name = ui, name
        self.dir = os.path.join(work, name)
        shutil.rmtree(self.dir, ignore_errors=True)
        os.makedirs(self.dir)
        self.sd = os.path.join(self.dir, 'sd')
        self.log = os.path.join(self.dir, 'bootloader.log')
        self.out = ''
        print('== %s' % name)

    def file(self, name, data):
        p = os.path.join(self.dir, name)
        with open(p, 'wb') as f:
            f.write(data)
        return p

    def run(self, *args, env=None, expect_rc=None):
        """usbtest.py --sim ... ARGS. Returns (rc, output)."""
        e = dict(os.environ, UISHOT_BOOTLOADER_LOG=self.log)
        e.update(env or {})
        cmd = ['python3', '-I', os.path.join(HERE, 'usbtest.py'), '--sim', self.ui, '--sd', self.sd, '--out', self.dir] + list(args)
        r = subprocess.run(cmd, capture_output=True, text=True, env=e, timeout=600)
        self.out = r.stdout + r.stderr
        if expect_rc is not None:
            check(r.returncode == expect_rc, '%s: exit code %d, wanted %d\n%s' % (self.name, r.returncode, expect_rc, self.out))
        return r.returncode, self.out

    def calls(self):
        return open(self.log).read().splitlines() if os.path.exists(self.log) else []

    def fw(self, name=''):
        return os.path.join(self.sd, 'Firmware', name)

    def fw_files(self):
        d = self.fw()
        return sorted(f for f in os.listdir(d) if not f.startswith('_fwinfo')) if os.path.isdir(d) else []

    def has(self, text, why=None):
        check(text in self.out, '%sthe output lacks %r:\n%s' % ((why + ': ') if why else '', text, self.out))

    def console(self, lines, env=None):
        """Raw console lines through usbtest.py cmd, one process (one start of the sim) per line. Returns the terminal lines."""
        res = []
        for l in lines:
            self.run('cmd', l, env=env)
            res.append(self.out.strip().splitlines()[-1] if self.out.strip() else '')
        return res


def main(ui, work):
    os.makedirs(work, exist_ok=True)
    good = ptfgen.build(build_id='abc1234')
    crc = ptfcheck.validate(good).crc

    # ------------------------------------------------------------------------------------------------ success
    c = Case(ui, work, 'update-pass')
    f = c.file(GOOD, good)
    c.run('update', f, expect_rc=0)
    c.has('update PASS')
    c.has('build uishot -> abc1234')
    check(c.calls() == ['runBootloader ' + GOOD], 'bootloader log %r' % c.calls())
    check(c.fw_files() == [GOOD], 'files in /Firmware: %r' % c.fw_files())
    check(open(c.fw(GOOD), 'rb').read() == good, 'the file on the card is not the file that was sent')
    check(not os.path.exists(c.fw('_usbtest.tmp')), 'temp file left behind')
    check(open(os.path.join(c.sd, '.uishot_flash')).read() == 'abc1234', 'the simulated flash does not hold the new build')
    for stage in range(1, 8):
        c.has('[%d/7]' % stage)

    # no-op upgrade: the same image again over the build that is running, the file of that name is replaced
    c.run('update', f, expect_rc=0)
    c.has('update PASS')
    c.has('build abc1234 -> abc1234')
    c.has('no-op upgrade')
    check(c.calls() == ['runBootloader ' + GOOD] * 2, 'bootloader log %r' % c.calls())
    check(open(c.fw(GOOD), 'rb').read() == good, 'file changed by the no-op upgrade')

    # a different build on top of it
    f2 = c.file(GOOD, ptfgen.build(build_id='0123abc'))
    c.run('update', f2, expect_rc=0)
    c.has('build abc1234 -> 0123abc')

    # ------------------------------------------------------------------------------------------------ stage only
    c = Case(ui, work, 'stage-only')
    f = c.file(GOOD, good)
    c.run('--stage-only', 'update', f, expect_rc=0)
    c.has('staged only')
    c.has('flash %s 0x%08x' % (GOOD, crc))
    check(c.calls() == [], 'the bootloader was called')
    check(c.fw_files() == [GOOD], 'files in /Firmware: %r' % c.fw_files())

    # ------------------------------------------------------------------------------------------------ the device refuses by itself
    # (raw console lines: they bypass every check of the runner, the device has to hold the line alone)
    c = Case(ui, work, 'device-refuses')
    f = c.file(GOOD, good)
    c.run('--stage-only', 'update', f, expect_rc=0)
    tok = '0x%08x' % crc
    r = c.console(['flash ' + GOOD,                                   # no confirm token
                   'flash ' + GOOD + ' ' + tok.replace('0x', ''),     # token without 0x
                   'flash ' + GOOD + ' 0x00000001',                   # wrong token
                   'flash ' + GOOD + ' 0x1234',                       # short token
                   'flash PolyendTracker_123456789_1.0.ptf ' + tok,    # 32 characters
                   'flash polyendTracker_CE_1.0.0.ptf ' + tok,
                   'flash ../PolyendTracker_CE_1.0.0.ptf ' + tok,
                   'flash PolyendTracker_NOFILE_1.0.ptf ' + tok])
    wants = ['needs the confirm token', 'needs the confirm token', 'crc', 'needs the confirm token', 'name-too-long', 'name-no-prefix',
             'name-bad-char', 'no such file']
    for line, want in zip(r, wants):
        check(line.startswith('@err') and want in line, 'device answered %r, wanted an error with %r' % (line, want))
    check(c.calls() == [], 'the bootloader was called by a refused flash: %r' % c.calls())
    # the right token at last: the device would hand off (the sim exits with 77 and records it)
    c.run('cmd', 'flash %s %s' % (GOOD, tok))
    check(c.calls() == ['runBootloader ' + GOOD], 'the confirmed flash did not reach the bootloader: %r' % c.calls())
    check(open(c.fw('_fwinfo')).read() == GOOD, '/Firmware/_fwinfo does not name the file')

    # ------------------------------------------------------------------------------------------------ bad CRC
    c = Case(ui, work, 'bad-crc')
    f = c.file(GOOD, good)
    c.run('--stage-only', 'update', f, expect_rc=0)
    # (a) the file on the card changes after it was checked (a trailing blank line keeps it a valid image but moves the CRC)
    with open(c.fw(GOOD), 'ab') as fh:
        fh.write(b'\r\n')
    r = c.console(['flash %s %s' % (GOOD, tok)])
    check(r[0].startswith('@err flash crc ') and 'the token says %08x' % crc in r[0], 'flash with a stale token: %r' % r)
    check(c.calls() == [], 'bootloader called with a CRC mismatch')
    # (b) put with a wrong CRC: the device removes what it received
    sys.path.insert(0, HERE)
    import usbtest
    t = usbtest.SimTransport(ui, c.sd)
    try:
        tr = usbtest.Tracker(t, heartbeat=0)
        tr.send('put /Firmware/_usbtest.tmp %d 0x00000001' % len(good))
        tr._ready_or_err()
        t.write(good)
        term, _, _ = tr._read_reply(60)
        check(term.startswith('@err put crc'), 'put with a wrong crc -> %r' % term)
        check(not os.path.exists(c.fw('_usbtest.tmp')), 'the temp file survived a CRC failure')
        # (c) fwpromote with a token that does not match
        tr.put(good, '/Firmware/_usbtest.tmp')
        term, _, _ = tr.command('fwpromote _usbtest.tmp PolyendTracker_NEW_1.0.0.ptf 0x00000002', 60, expect_err=True)
        check(term.startswith('@err fwpromote crc'), 'fwpromote with a wrong token -> %r' % term)
        check(os.path.exists(c.fw('_usbtest.tmp')) and not os.path.exists(c.fw('PolyendTracker_NEW_1.0.0.ptf')),
              'fwpromote moved a file whose CRC does not match')
        # (d) the runner side: it compares what the device read back with its own CRC (a lying device cannot pass)
    finally:
        t.close()
    check(c.calls() == [], 'bootloader called in the CRC cases')

    # ------------------------------------------------------------------------------------------------ wrong platform and broken files
    c = Case(ui, work, 'wrong-platform')
    wrong = {'sp': ptfgen.build(sp=0x20280000), 'reset': ptfgen.build(reset=0x6000_2001), 'config': ptfgen.build(config=bytes([0xFF] * 16)),
             'beyond': ptfgen.build(size=0xFF000 + 0x100), 'notHex': bytes(range(256)) * 40}
    for kind, data in wrong.items():
        f = c.file('PolyendTracker_BAD%s_1.0.0.ptf' % kind[:3].upper(), data)
        rc, out = c.run('update', f)
        check(rc == 1 and 'update FAIL at file' in out and 'Nothing was handed to the bootloader' in out, '%s: %s' % (kind, out))
    check(c.calls() == [], 'the bootloader was called for a bad image')
    check(c.fw_files() == [], 'something reached /Firmware: %r' % c.fw_files())
    # the device alone, with a bad file the runner never looked at, and the right token
    for kind, data, err in (('sp', wrong['sp'], 'bad-sp'), ('reset', wrong['reset'], 'bad-reset'), ('config', wrong['config'], 'flash-config'),
                            ('beyond', wrong['beyond'], 'beyond-limit'), ('notHex', wrong['notHex'], 'not-intel-hex')):
        name = 'PolyendTracker_RAW%s_1.0.ptf' % kind[:3].upper()
        os.makedirs(c.fw(), exist_ok=True)
        with open(c.fw(name), 'wb') as fh:
            fh.write(data)
        r = c.console(['flash %s 0x%08x' % (name, ptfcheck.validate(data).crc), 'fwcheck ' + name])
        check(r[0].startswith('@err flash ptf %s' % err), '%s: %r' % (kind, r[0]))
        check(r[1].startswith('@err fwcheck ptf %s' % err), '%s: %r' % (kind, r[1]))
    check(c.calls() == [], 'the bootloader was called for a bad file: %r' % c.calls())

    # ------------------------------------------------------------------------------------------------ name too long
    c = Case(ui, work, 'name-too-long')
    f = c.file(GOOD, good)
    rc, out = c.run('--ptf-name', 'PolyendTracker_123456789_1.0.ptf', 'update', f)
    check(rc == 1 and 'update FAIL at name' in out and 'name-too-long' in out, out)
    rc, out = c.run('--ptf-name', 'PolyendTracker_12345678_1.0.ptf', 'update', f)   # exactly 31: fine
    check(rc == 0 and 'update PASS' in out, out)
    c.run('--stage-only', '--ptf-name', 'PolyendTracker_X_1.0.0.ptf', 'update', f, expect_rc=0)
    r = c.console(['fwpromote _usbtest.tmp PolyendTracker_123456789_1.0.ptf 0x00000001'])
    check('name-too-long' in r[0], r)
    check(len(c.calls()) == 1, 'bootloader log %r' % c.calls())   # only the 31 character run

    # ------------------------------------------------------------------------------------------------ interrupted put
    c = Case(ui, work, 'interrupted-put')
    f = c.file(GOOD, good)
    rc, out = c.run('--test-interrupt-put', '30000', 'update', f)
    check(rc == 1 and 'update FAIL at put' in out and '@err put timeout' in out, out)
    check(c.calls() == [], 'bootloader called after an interrupted copy')
    check(c.fw_files() == [], 'a partial file is left on the card: %r' % c.fw_files())
    c.has('Nothing was handed to the bootloader')

    # ------------------------------------------------------------------------------------------------ no confirm token
    c = Case(ui, work, 'no-token')
    f = c.file(GOOD, good)
    c.run('--stage-only', 'update', f, expect_rc=0)
    r = c.console(['flash ' + GOOD, 'flash', 'flash ' + GOOD + ' ' + GOOD, 'flash ' + GOOD + ' ' + tok + ' now'])
    check(all(x.startswith('@err flash') for x in r), r)
    check(c.calls() == [], 'bootloader called without a token')

    # ------------------------------------------------------------------------------------------------ playing
    c = Case(ui, work, 'playing')
    f = c.file(GOOD, good)
    c.run('--stage-only', 'update', f, expect_rc=0)
    script = c.file('play.ut', b'play song\nwait 300\nraw flash %s %s\nraw fwcheck %s\nraw fwpromote _usbtest.tmp PolyendTracker_P_1.0.0.ptf 0x00000001\n' % (
        GOOD.encode(), tok.encode(), GOOD.encode()))
    c.run('--report', os.path.join(c.dir, 'r.json'), script)
    rep = open(os.path.join(c.dir, 'r.json')).read()
    check(rep.count('refused while playing, rendering or loading') >= 3, 'playing was not refused: %s' % rep[:600])
    check(c.calls() == [], 'bootloader called while playing')
    # the runner stops before it sends a byte when the unit reports play=1
    import usbtest
    t = usbtest.SimTransport(ui, c.sd)
    try:
        tr = usbtest.Tracker(t, heartbeat=0)
        tr.command('play song')
        t.wait_ms(300)
        failed_stage = None
        try:
            usbtest.update_firmware(tr, f, say=lambda *_: None)
        except usbtest.UpdateFailed as e:
            failed_stage = (e.stage, e.hand_off, e.msg)
        check(failed_stage is not None and failed_stage[0] == 'device' and not failed_stage[1] and 'playing' in failed_stage[2],
              'update while playing -> %r' % (failed_stage,))
        check(c.fw_files() == [GOOD], 'the runner wrote while the unit was playing: %r' % c.fw_files())
    finally:
        t.close()
    check(c.calls() == [], 'bootloader called while playing')

    # ------------------------------------------------------------------------------------------------ space
    c = Case(ui, work, 'no-space')
    f = c.file(GOOD, good)
    rc, out = c.run('update', f, env={'UISHOT_FREE_BYTES': '100000'})
    check(rc == 1 and 'update FAIL at space' in out, out)
    check(c.fw_files() == [] and c.calls() == [], 'something was written without space')

    # ------------------------------------------------------------------------------------------------ build id
    c = Case(ui, work, 'build-id')
    f = c.file(GOOD, good)
    rc, out = c.run('--expect-build', 'deadbee', 'update', f)
    check(rc == 1 and 'update FAIL at build-id' in out and 'reports build abc1234' in out and 'The bootloader was started' in out, out)
    pinned = ptfgen.build(build_id=None)
    f = c.file('PolyendTracker_PIN_1.0.0.ptf', pinned)
    rc, out = c.run('update', f)
    check(rc == 1 and 'update FAIL at build-id' in out and 'pinned BUILD_TAG' in out and 'Nothing was handed' in out, out)
    rc, out = c.run('--no-build-check', 'update', f)
    check(rc == 0 and 'update PASS' in out, out)
    rc, out = c.run('--expect-build', 'abc1234', 'update', c.file(GOOD, good))
    check(rc == 0 and 'update PASS' in out, out)

    # ------------------------------------------------------------------------------------------------ the unit never comes back
    c = Case(ui, work, 'stuck-in-bootloader')
    f = c.file(GOOD, good)
    rc, out = c.run('--update-timeout', '5', 'update', f, env={'UISHOT_BOOTLOADER_STUCK': '1'})
    check(rc == 1 and 'update FAIL at re-enumerate' in out and 'did not come back' in out, out)
    c.has('Recovery:')
    c.has('/Firmware')
    check(c.calls() == ['runBootloader ' + GOOD], c.calls())

    # ------------------------------------------------------------------------------------------------ the hand-off itself fails
    c = Case(ui, work, 'handoff-fails')
    f = c.file(GOOD, good)
    rc, out = c.run('update', f, env={'UISHOT_BOOTLOADER_FAIL': '1'})
    check(rc == 1 and 'update FAIL at ' in out and 'hand-off failed' in out, out)

    print('flash_e2e: %d checks, %d failed' % (passed + failed, failed))
    return 1 if failed else 0


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
