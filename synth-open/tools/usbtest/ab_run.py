#!/usr/bin/env python3
"""Unattended A/B test of two test console firmware flavours on the unit (or on the Mac build). Host tool, NOT firmware.

For every phase of the sequence (A B, or A B A B ... with --pairs K) the runner
  1. updates the firmware to the flavour's .ptf (usbtest.py update: put, device re-read, bootloader hand-off, wait, build id),
  2. swaps the plugin set on the card to the one that belongs to that firmware (put into /Plugins/Master and /Plugins/Synth, CRC checked
     on the device, files the flavour does not have are removed), resets the unit,
  3. VERIFIES: the unit reports the flavour's build id and every plugin file on the card has the CRC of the staged file, nothing else is
     in the two folders. A mismatch aborts the whole run before anything is measured,
  4. runs scenarios/suite.py (--suite first|all|none) and the CPU sweep (every engine of the Model column, built in and plugin, at the
     given voice counts, N repeats, a fresh boot per repeat), verifying again before and after,
  5. reads the crash markers (fault record, stale loading marker, safe mode) after each of these.
At the end it restores the flavour chosen with --restore and writes ab_report.md and ab_report.json (CPU per engine A against B with delta
and spread, scenarios that differ between the flavours, crash markers).

  ab_run.py --port auto --a build/release-1.2.0/auto-t --b build/release-1.2.0/auto-tf --out build/usbtest-ab
  ab_run.py --sim build/ui-shot/ui_shot --sd SDFOLDER --a-ptf A.ptf --a-plugins DIR --b-ptf B.ptf --b-plugins DIR2 --out DIR

A flavour folder (--a, --b) holds one PolyendTracker_*.ptf and the plugin files (*.plg, *.plgi) either flat or in Plugins/Master and
Plugins/Synth. Master or Synth is read from the kind byte in each .plg header. The two .ptf files must carry different build ids
(BUILD_TAG), otherwise the runner cannot tell which one runs. Exit status: 0 clean, 1 finished with findings (a failed scenario, a
scenario that differs, a crash marker), 2 usage or setup, 3 aborted (mismatch, update failed, unit lost).
"""
import argparse
import datetime
import glob
import json
import os
import shlex
import shutil
import subprocess
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SCN = os.path.join(HERE, 'scenarios')
sys.path.insert(0, HERE)
sys.path.insert(0, SCN)
import ab_report  # noqa: E402
import lib  # noqa: E402
import ptfcheck  # noqa: E402
import scn_engines  # noqa: E402
import usbtest  # noqa: E402
from usbtest import Disconnected, SerialTransport, SimTransport, Timeout, Tracker, UpdateFailed, UsbTestError  # noqa: E402

KIND_DIRS = {1: 'Master', 4: 'Synth'}   # PLUG_KIND_MASTER_INSERT, PLUG_KIND_SYNTH (plug_abi.h), byte 8 of the .plg header
T0 = time.monotonic()
EXIT_CLEAN, EXIT_FINDINGS, EXIT_SETUP, EXIT_ABORT = 0, 1, 2, 3


def say(msg):
    print('[%6.0f s] %s' % (time.monotonic() - T0, msg), flush=True)


class AbortRun(Exception):
    def __init__(self, reason, kind='abort', hand_off=False):
        super().__init__(reason)
        self.reason, self.kind, self.hand_off = reason, kind, hand_off


class SetupError(Exception):
    pass


# ------------------------------------------------------------------------------------------------ the two flavours
class PlugFile:
    def __init__(self, src, card):
        self.src, self.card = src, card
        with open(src, 'rb') as f:
            self.data = f.read()
        self.size, self.crc = len(self.data), zlib.crc32(self.data) & 0xFFFFFFFF
        self.name = os.path.basename(card)


class Flavour:
    def __init__(self, key, label, ptf, plugins):
        self.key, self.label, self.ptf, self.plugins = key, label, ptf, plugins
        with open(ptf, 'rb') as f:
            data = f.read()
        v = ptfcheck.validate(data)
        if not v.ok:
            raise SetupError('%s: not a flashable MK66 image: %s' % (ptf, v.error))
        self.ptf_name = os.path.basename(ptf)
        err = ptfcheck.name_error(self.ptf_name)
        if err != 'ok':
            raise SetupError('%s: the name is refused by the bootloader rules: %s (31 characters at most)' % (self.ptf_name, err))
        self.build = ptfcheck.find_build_id(data)
        if not self.build:
            raise SetupError('%s: no unique 7 digit build id string in the image (a pinned BUILD_TAG?)' % ptf)
        self.ptf_bytes, self.ptf_crc, self.top = len(data), zlib.crc32(data) & 0xFFFFFFFF, v.top
        self.files = {}    # card path -> PlugFile
        for p in plugins:
            self.files[p.card] = p

    def summary(self):
        return {'label': self.label, 'ptf': self.ptf, 'ptf_name': self.ptf_name, 'build': self.build, 'ptf_bytes': self.ptf_bytes,
                'ptf_crc': self.ptf_crc, 'plugin_files': len(self.files),
                'plugin_crcs': {k: '%08x' % v.crc for k, v in sorted(self.files.items())}}


def plugin_kind(path):
    with open(path, 'rb') as f:
        h = f.read(64)
    if len(h) < 64 or h[:4] != b'PLG1':
        raise SetupError('%s: not a plugin file (magic %r)' % (path, h[:4]))
    return h[8]


def load_plugins(folder):
    """The .plg and .plgi files of a folder (flat, or in Plugins/Master and Plugins/Synth), as card paths."""
    srcs = []
    for sub in ('', 'Plugins/Master', 'Plugins/Synth'):
        d = os.path.join(folder, sub)
        srcs += sorted(glob.glob(os.path.join(d, '*.plg')) + glob.glob(os.path.join(d, '*.plgi')))
    out, seen, kinds = [], {}, {}
    for s in srcs:
        base = os.path.basename(s)
        if base in seen:
            raise SetupError('%s: %s appears twice (%s)' % (folder, base, seen[base]))
        seen[base] = s
        if s.endswith('.plg'):
            kinds[base[:-4]] = KIND_DIRS.get(plugin_kind(s))
            if kinds[base[:-4]] is None:
                raise SetupError('%s: plugin kind %d is neither a master insert nor a synth engine' % (s, plugin_kind(s)))
    for s in srcs:
        base = os.path.basename(s)
        stem = base[:-4] if base.endswith('.plg') else base[:-5]
        if stem not in kinds:
            raise SetupError('%s: %s has no %s.plg next to it' % (folder, base, stem))
        out.append(PlugFile(s, '/Plugins/%s/%s' % (kinds[stem], base)))
    return out


def find_ptf(folder):
    found = sorted(glob.glob(os.path.join(folder, 'PolyendTracker_*.ptf')))
    if len(found) != 1:
        raise SetupError('%s: expected exactly one PolyendTracker_*.ptf, found %d' % (folder, len(found)))
    return found[0]


def make_flavour(key, folder, ptf, plugdir, label):
    if not (ptf or folder):
        raise SetupError('flavour %s: give --%s FOLDER or --%s-ptf FILE with --%s-plugins DIR' % (key, key.lower(), key.lower(), key.lower()))
    ptf = ptf or find_ptf(folder)
    plugdir = plugdir or folder
    if not plugdir or not os.path.isdir(plugdir):
        raise SetupError('flavour %s: no plugin folder (%s)' % (key, plugdir))
    files = load_plugins(plugdir)
    if not any(p.card.startswith('/Plugins/Master/') for p in files) or not any(p.card.startswith('/Plugins/Synth/') for p in files):
        raise SetupError('%s: the plugin set has no master insert or no synth engine plugin' % plugdir)
    return Flavour(key, label or os.path.basename(os.path.normpath(folder or plugdir)), ptf, files)


def plugin_diff(a, b):
    paths = sorted(set(a.files) | set(b.files))
    differ = [p for p in paths if p not in a.files or p not in b.files or a.files[p].crc != b.files[p].crc]
    return {'differ': differ, 'same': [p for p in paths if p not in differ]}


# ------------------------------------------------------------------------------------------------ the link
class Session:
    """One transport and tracker, closed while the suite subprocess owns the port or the SD folder."""

    def __init__(self, args):
        self.args = args
        self.t = None
        self.tr = None
        self.sim = bool(args.sim)

    def open(self):
        a = self.args
        if self.sim:
            self.t = SimTransport(os.path.abspath(a.sim), os.path.abspath(a.sd))
        else:
            port = a.port if a.port and a.port != 'auto' else SerialTransport.find_port(wait=10)
            self.t = SerialTransport(port)
        self.tr = Tracker(self.t, heartbeat=0 if self.sim else 1.0)
        self.t.drain(0.2)

    def release(self):
        if self.t:
            self.t.close()

    def reacquire(self, timeout=60.0):
        """Reconnect after release or a lost link and wait until the console answers."""
        self.t.reconnect(timeout)
        end = time.monotonic() + timeout
        while True:
            try:
                self.t.drain(0.2)
                return self.tr.ping(2.0)
            except UsbTestError:
                if time.monotonic() > end:
                    raise
                time.sleep(0.5)

    @property
    def target(self):
        return 'mac' if self.sim else 'device'

    @property
    def port(self):
        return getattr(self.t, 'port', None)


# ------------------------------------------------------------------------------------------------ the card
def inventory(tr):
    """{card path: size} of every .plg and .plgi in /Plugins/Master and /Plugins/Synth."""
    out = {}
    for d in ('/Plugins/Master', '/Plugins/Synth'):
        try:
            entries = tr.ls(d)
        except UsbTestError:
            entries = []
        for e in entries:
            if not e['dir'] and e['name'].lower().endswith(('.plg', '.plgi')):
                out[d + '/' + e['name']] = e['size']
    return out


def swap_plugins(tr, fl, skip=False):
    """Makes the two plugin folders hold exactly the flavour's set. A file whose size and CRC already match stays, others are put (the
    device checks the CRC of what it received), files that are not in the set are removed. Returns a one line summary."""
    if skip:
        return 'SKIPPED (test hook)'
    inv = inventory(tr)
    want_l = {p.lower(): p for p in fl.files}
    inv_l = {p.lower(): p for p in inv}
    removed = put = kept = 0
    for p in sorted(inv):
        if p.lower() not in want_l:
            tr.rm(p)
            removed += 1
    sent = 0
    for card, pf in sorted(fl.files.items()):
        have = inv_l.get(card.lower())
        if have and inv[have] == pf.size:
            crc, _ = tr.hash_file(have)
            if int(crc, 16) == pf.crc:
                kept += 1
                continue
        if have and have != card:
            tr.rm(have)
        tr.put(pf.data, card)
        put += 1
        sent += pf.size
    return '%d put (%d B), %d kept, %d removed' % (put, sent, kept, removed)


def verify_flavour(tr, fl, stage):
    """The firmware reports the flavour's build id and the card holds exactly the flavour's plugin set. Returns the list of problems."""
    problems = []
    try:
        info = tr.ping(5.0)
    except UsbTestError as e:
        return ['%s: no console answer: %s' % (stage, e)]
    if info['build'] != fl.build:
        problems.append('the unit reports build %s, flavour %s (%s) is %s' % (info['build'], fl.key, fl.label, fl.build))
    inv = inventory(tr)
    inv_l = {p.lower(): p for p in inv}
    want_l = {p.lower(): p for p in fl.files}
    for low, card in sorted(want_l.items()):
        have = inv_l.get(low)
        pf = fl.files[card]
        if have is None:
            problems.append('%s is missing on the card' % card)
            continue
        crc, size = tr.hash_file(have)
        if size != pf.size or int(crc, 16) != pf.crc:
            problems.append('%s on the card is %d B crc %s, the staged file of flavour %s is %d B crc %08x' % (have, size, crc, fl.key, pf.size, pf.crc))
    for low, have in sorted(inv_l.items()):
        if low not in want_l:
            problems.append('%s is on the card and not in the plugin set of flavour %s' % (have, fl.key))
    return problems


def crash_markers(tr):
    """Lines describing a fault record, a stale loading marker or safe mode (empty: clean)."""
    m = tr.markers()
    out = []
    if m.get('fault') == '1':
        out.append('fault record pc=%s lr=%s psr=%s cfsr=%s srs=%s' % (m.get('pc'), m.get('lr'), m.get('psr'), m.get('cfsr'), m.get('srs')))
    if m.get('loading') == '1':
        out.append('stale /Plugins/.loading marker (a plugin load did not finish)')
    if m.get('safe') == '1':
        out.append('plugin host in safe mode: %s' % m.get('reason'))
    return out, m


# ------------------------------------------------------------------------------------------------ the suite
def run_suite(sess, fl, phase, args, outdir, goldens):
    """suite.py in a subprocess (it owns the port or the SD folder meanwhile). Returns (results list, one line)."""
    d = os.path.join(outdir, 'phase%02d_%s_suite' % (phase, fl.key))
    os.makedirs(d, exist_ok=True)
    cmd = [sys.executable, '-I', os.path.join(SCN, 'suite.py'), '--out', d, '--goldens', goldens, '--no-baseline']
    if sess.sim:
        cmd += ['--sim', os.path.abspath(args.sim), '--sd', os.path.abspath(args.sd)]
    else:
        cmd += ['--port', sess.port]
        if not args.no_device_writes:
            cmd += ['--device-writes']
    if args.suite_exclude:
        cmd += ['--exclude', args.suite_exclude]
    cmd += shlex.split(args.suite_args or '')
    sess.release()
    say('phase %d: suite.py (%s), log %s/suite.log' % (phase, ' '.join(shlex.split(args.suite_args or '')) or 'all scenarios', d))
    t0 = time.monotonic()
    try:
        with open(os.path.join(d, 'suite.log'), 'w') as log:
            rc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=args.suite_timeout).returncode
    except subprocess.TimeoutExpired:
        rc = 'timeout'
    finally:
        sess.reacquire()
    path = os.path.join(d, 'suite.json')
    if not os.path.exists(path):
        return [], 'suite did not produce a report (exit %s), see %s/suite.log' % (rc, d)
    with open(path) as f:
        doc = json.load(f)
    results = [{'name': r['name'], 'status': r['status'], 'error': r.get('error'),
                'failed': [s['text'] for s in r.get('steps', []) if s['kind'] == 'check' and not s['ok']][:6],
                'seconds': r.get('seconds')} for r in doc['results']]
    counts = {}
    for r in results:
        counts[r['status']] = counts.get(r['status'], 0) + 1
    return results, '%s in %.0f s (exit %s)' % (', '.join('%d %s' % (n, s) for s, n in sorted(counts.items())), time.monotonic() - t0, rc)


# ------------------------------------------------------------------------------------------------ the CPU sweep
def sweep_phase(sess, fl, phase, pair, args, outdir, crashes):
    """N repeats. Each starts with a reset (a known UI and engine state), rolls the Model column through every engine, and for each plays
    pads 0..v-1 (the fixed notes of the pad board, the engine's default patch) for every voice count and reads the audio CPU."""
    meas, missing, notes = [], [], []
    goldens = lib.Goldens(os.path.join(outdir, 'sweep_goldens'), record=False)
    only = set(x.strip().upper() for x in args.sweep_engines.split(',') if x.strip()) if args.sweep_engines else None
    voices = [int(v) for v in args.voices.split(',') if v.strip()]
    crashed_repeats = 0
    engines_seen = []
    for rep in range(1, args.sweep_repeats + 1):
        where = {'at': 'start of repeat %d' % rep}
        got, order = [], []
        try:
            ctx = lib.Ctx(sess.tr, sess.target, outdir, goldens, 'ab.sweep', {'budgets': {}, 'no_reboot': False})
            ctx.reboot()
            ctx.reset_ui()
            scn_engines.open_model_page(ctx)
            ctx.tap('Pattern')
            ctx.settle(500)
            where['at'] = 'idle'
            idle = ctx.cpu_measure('cpu.idle', hold_ms=1200)
            if idle:
                got.append({'engine': 'idle', 'voices': 0, 'avg_us': idle['avg'], 'max_us': idle['max']})
            ctx.tap('Params')
            ctx.settle(700)
            for name in scn_engines.cycle_engines(ctx):
                if name in order:
                    notes.append('phase %d repeat %d: the Model column showed %s a second time before it wrapped (the list re-orders while engines install), the walk stopped there' % (phase, rep, name))
                    break
                order.append(name)
                where['at'] = 'engine %s' % name
                if ctx.has_text('Missing'):
                    if 'Missing %s' % name not in missing:
                        missing.append('Missing %s' % name)
                    continue
                if only and name.upper() not in only:
                    continue
                ctx.tap('Pattern')
                ctx.settle(500)
                for v in voices:
                    where['at'] = 'engine %s, %d voice%s' % (name, v, '' if v == 1 else 's')
                    for p in range(v):
                        ctx.pad_press(p)
                    sess.tr.wait(400)
                    m = ctx.cpu_measure('x', budget=None)
                    for p in reversed(range(v)):
                        ctx.pad_release(p)
                    sess.tr.wait(900)
                    if m:
                        got.append({'engine': name, 'voices': v, 'avg_us': m['avg'], 'max_us': m['max']})
                ctx.tap('Params')
                ctx.settle(700)
            ctx.release_all()
            ctx.reset_ui()
        except (Disconnected, Timeout) as e:
            crashed_repeats += 1
            crashes.append({'phase': phase, 'flavour': fl.key, 'when': 'sweep repeat %d at %s' % (rep, where['at']), 'text': 'the link was lost: %s' % e})
            say('phase %d: LINK LOST in the sweep at %s: %s' % (phase, where['at'], e))
            try:
                sess.reacquire()
                lines, _ = crash_markers(sess.tr)
                for l in lines:
                    crashes.append({'phase': phase, 'flavour': fl.key, 'when': 'after the lost link (sweep repeat %d)' % rep, 'text': l})
            except (UsbTestError, OSError) as e2:
                raise AbortRun('the unit did not come back after the sweep lost it at %s: %s' % (where['at'], e2))
            if crashed_repeats >= 2:
                notes.append('phase %d: two repeats lost the link, the sweep of this phase was stopped' % phase)
                break
        except (UsbTestError, lib.Abort) as e:
            notes.append('phase %d repeat %d: %s at %s' % (phase, rep, e, where['at']))
            say('phase %d: sweep repeat %d failed at %s: %s' % (phase, rep, where['at'], e))
            try:
                sess.tr.t.drain(0.3)
            except Exception:  # noqa: BLE001
                pass
            continue
        for g in got:
            g.update(flavour=fl.key, phase=phase, pair=pair, repeat=rep)
        engines_seen.append(set(g['engine'] for g in got))
        meas += got
        say('phase %d: sweep repeat %d/%d done, %d readings' % (phase, rep, args.sweep_repeats, len(got)))
    if len(engines_seen) > 1:
        every = set().union(*engines_seen)
        for i, e in enumerate(engines_seen, 1):
            if e != every:
                notes.append('phase %d: sweep repeat %d did not measure %s' % (phase, i, ' '.join(sorted(every - e))))
    return meas, missing, notes


# ------------------------------------------------------------------------------------------------ one phase
def update_to(sess, fl, args, label):
    tr = sess.tr
    try:
        st = tr.status()
        if st.get('play') == '1':
            tr.command('stop')
            tr.wait(300)
        tr.idle()
    except UsbTestError:
        pass
    say('%s: firmware update to %s (%s, build %s)' % (label, fl.label, fl.ptf_name, fl.build))
    try:
        res = usbtest.update_firmware(tr, fl.ptf, name=fl.ptf_name, expect_build=fl.build, timeout=args.update_timeout, say=lambda m: say('    ' + m))
    except UpdateFailed as e:
        raise AbortRun('firmware update to %s failed at %s: %s' % (fl.label, e.stage, e.msg), kind='update', hand_off=e.hand_off)
    return 'build %s -> %s in %.0f s%s' % (res['before'], res['after'], res['seconds'], ' (no-op upgrade)' if res['same_build'] else '')


def bring_to(sess, fl, args, label, ph=None):
    """Firmware, plugin set, reset, verify. Raises AbortRun on a mismatch. Returns (update line, swap line), also stored in ph as they happen."""
    ph = ph if ph is not None else {}
    upd = ph['update'] = update_to(sess, fl, args, label)
    skip_swap = fl.key in (args.test_skip_swap or [])
    swap = ph['swap'] = swap_plugins(sess.tr, fl, skip=skip_swap)
    say('%s: plugin set of %s: %s' % (label, fl.label, swap))
    sess.tr.reboot(60.0)     # the plugin host indexes the folders at boot
    sess.t.drain(0.2)
    sess.tr.wait(4800)      # the plugin host's 4 s key window
    problems = verify_flavour(sess.tr, fl, label)
    if problems:
        raise AbortRun('plugin set or firmware does not match flavour %s (%s): %s' % (fl.key, fl.label, ' / '.join(problems[:6]) + (' ...' if len(problems) > 6 else '')),
                       kind='mismatch')
    say('%s: verified, build %s and %d plugin files match the staged set' % (label, fl.build, len(fl.files)))
    return upd, swap


def check_markers(sess, fl, phase, when, crashes):
    try:
        lines, _ = crash_markers(sess.tr)
    except UsbTestError as e:
        lines = ['markers could not be read: %s' % e]
    for l in lines:
        crashes.append({'phase': phase, 'flavour': fl.key, 'when': when, 'text': l})
        say('phase %d: CRASH MARKER (%s): %s' % (phase, when, l))
    return len(lines)


def run_phase(sess, flavours, key, idx, pair, args, outdir, goldens, rep, do_suite):
    fl = flavours[key]
    label = 'phase %d (%s %s)' % (idx, key, fl.label)
    ph = {'index': idx, 'flavour': key, 'pair': pair}
    rep['phases'].append(ph)
    try:
        bring_to(sess, fl, args, label, ph)
        ph['verified'] = 'yes, before measuring'
        ph['markers'] = '%d after boot' % check_markers(sess, fl, idx, 'after the update and reset', rep['crashes'])
        write_reports(rep, outdir)
        if do_suite:
            results, line = run_suite(sess, fl, idx, args, outdir, goldens)
            ph['suite'] = line
            rep['suites'].append({'flavour': key, 'phase': idx, 'results': results})
            problems = verify_flavour(sess.tr, fl, label + ' after the suite')
            if problems:
                raise AbortRun('after the suite the unit or card no longer matches flavour %s: %s' % (key, ' / '.join(problems[:4])), kind='mismatch')
            n = check_markers(sess, fl, idx, 'after the suite', rep['crashes'])
            ph['markers'] += ', %d after the suite' % n
            write_reports(rep, outdir)
        else:
            ph['suite'] = 'not run'
        if args.sweep_repeats > 0:
            meas, missing, notes = sweep_phase(sess, fl, idx, pair, args, outdir, rep['crashes'])
            rep['measurements'] += meas
            for m in missing:
                if m not in rep['cpu_missing']:
                    rep['cpu_missing'].append(m)
            rep['notes'] += notes
            ph['sweep'] = '%d readings, %d repeat%s' % (len(meas), args.sweep_repeats, '' if args.sweep_repeats == 1 else 's')
            problems = verify_flavour(sess.tr, fl, label + ' after the sweep')
            if problems:
                raise AbortRun('after the sweep the unit or card no longer matches flavour %s: %s' % (key, ' / '.join(problems[:4])), kind='mismatch')
            n = check_markers(sess, fl, idx, 'after the sweep', rep['crashes'])
            ph['markers'] += ', %d after the sweep' % n
        else:
            ph['sweep'] = 'not run'
    except AbortRun as e:
        if e.kind == 'update':
            ph.setdefault('update', 'FAILED')
        ph.setdefault('verified', 'NO (mismatch, see ABORTED)' if e.kind == 'mismatch' else 'no (%s)' % e.kind)
        raise
    finally:
        write_reports(rep, outdir)


# ------------------------------------------------------------------------------------------------ reports
def finish_report(rep):
    ka, kb = rep['keys']
    rep['cpu'] = ab_report.cpu_rows(rep['measurements'], rep['keys'])
    rep['totals'] = [t for t in (ab_report.totals(rep['cpu'], v, rep['keys']) for v in sorted({r['voices'] for r in rep['cpu'] if r['voices']})) if t]
    rep['scenarios'] = ab_report.scenario_rows(rep['suites'], rep['keys'])
    bad = [r for s in rep['suites'] for r in s['results'] if r['status'] in ab_report.FAIL_LIKE]
    findings = []
    if rep.get('abort'):
        rep['result'] = 'ABORTED'
        return
    if bad:
        findings.append('%d failed scenario run%s' % (len(bad), '' if len(bad) == 1 else 's'))
    d = [r for r in rep['scenarios'] if r['verdict'] in ('DIFFERENT', 'flaky')]
    if d:
        findings.append('%d scenario%s differ%s between the flavours' % (len(d), '' if len(d) == 1 else 's', 's' if len(d) == 1 else ''))
    if rep['crashes']:
        findings.append('%d crash marker%s' % (len(rep['crashes']), '' if len(rep['crashes']) == 1 else 's'))
    rep['result'] = 'CLEAN' if not findings else 'FINDINGS: ' + ', '.join(findings)


def write_reports(rep, outdir):
    finish_report(rep)
    rep['meta']['seconds'] = round(time.monotonic() - T0)
    with open(os.path.join(outdir, 'ab_report.json'), 'w') as f:
        json.dump(rep, f, indent=1)
    with open(os.path.join(outdir, 'ab_report.md'), 'w') as f:
        f.write(ab_report.render(rep))


# ------------------------------------------------------------------------------------------------ main
def build_parser():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    c = ap.add_argument_group('connection (exactly one)')
    c.add_argument('--port', help='serial port, or "auto" (/dev/cu.usbmodem*)')
    c.add_argument('--sim', help='ui_shot binary: run against the Mac build')
    c.add_argument('--sd', help='sim: the SD card folder (the plugin folders and /Firmware live there)')
    for k in 'ab':
        g = ap.add_argument_group('flavour %s' % k.upper())
        g.add_argument('--' + k, metavar='FOLDER', help='folder with one PolyendTracker_*.ptf and the plugin files')
        g.add_argument('--%s-ptf' % k, metavar='FILE')
        g.add_argument('--%s-plugins' % k, metavar='DIR', help='plugin folder (default: the flavour folder)')
        g.add_argument('--%s-label' % k, metavar='TEXT', help='name in the report (default: the folder name)')
    r = ap.add_argument_group('what to run')
    r.add_argument('--pairs', type=int, default=1, help='K: run the pair K times alternating, A B A B ... (default 1)')
    r.add_argument('--order', choices=('AB', 'BA'), default='AB', help='which flavour goes first (default AB)')
    r.add_argument('--suite', choices=('first', 'all', 'none'), default='first', help='run scenarios/suite.py in the first pair only, in every phase, or never (default first)')
    r.add_argument('--suite-args', default='', help='extra arguments for suite.py, one string, e.g. "--project Projects/Demo --only pages.walk"')
    r.add_argument('--suite-exclude', default='project.*', help='scenarios left out of the suite (default project.*: they open a project, and the card then boots into it, '
                   'so the next phase would compare other screens against the same goldens). Empty string: run everything')
    r.add_argument('--suite-timeout', type=int, default=7200)
    r.add_argument('--no-device-writes', action='store_true', help='do not pass --device-writes to suite.py on a device')
    r.add_argument('--goldens', help='goldens folder of the suite (default OUT/goldens, the first run records and the other flavour compares. On the Mac the repository goldens are copied)')
    r.add_argument('--sweep-repeats', type=int, default=3, help='N repeats of the CPU sweep per phase, 0 = no sweep (default 3)')
    r.add_argument('--voices', default='1,4', help='voice counts of the sweep (default 1,4)')
    r.add_argument('--sweep-engines', help='only these engines, e.g. ACD,CHIP (default: every one of the Model column)')
    r.add_argument('--restore', choices=('a', 'b', 'none'), default='a', help='flavour left on the unit at the end (default a)')
    r.add_argument('--update-timeout', type=float, default=240.0)
    r.add_argument('--out', help='output folder (default ab-out-DATE in the current folder)')
    r.add_argument('--check-only', action='store_true', help='validate the two flavours, print the plan, touch nothing')
    r.add_argument('--allow-same-build', action='store_true', help='allow equal build ids (the runner then cannot tell the flavours apart by the console)')
    r.add_argument('--allow-same-plugins', action='store_true', help='allow two identical plugin sets')
    t = ap.add_argument_group('test hooks (ab_e2e.py)')
    t.add_argument('--test-skip-swap', action='append', default=[], metavar='A|B', help='do not swap the plugin set for this flavour: the verify step must abort')
    return ap


def main(argv=None):
    args = build_parser().parse_args(argv)
    if bool(args.sim) == bool(args.port):
        print('give exactly one of --sim and --port', file=sys.stderr)
        return EXIT_SETUP
    if args.sim and not args.sd:
        print('--sim needs --sd FOLDER', file=sys.stderr)
        return EXIT_SETUP
    if args.pairs < 1 or args.sweep_repeats < 0:
        print('--pairs must be at least 1', file=sys.stderr)
        return EXIT_SETUP
    try:
        fa = make_flavour('A', args.a, args.a_ptf, args.a_plugins, args.a_label)
        fb = make_flavour('B', args.b, args.b_ptf, args.b_plugins, args.b_label)
        if fa.build == fb.build and not args.allow_same_build:
            raise SetupError('both .ptf files carry build id %s: after an update the console cannot tell which flavour runs. Build them with different BUILD_TAG '
                             'values (7 hex digits) or pass --allow-same-build' % fa.build)
        if fa.ptf_name == fb.ptf_name:
            raise SetupError('both .ptf files are called %s: they would overwrite each other in /Firmware' % fa.ptf_name)
        diff = plugin_diff(fa, fb)
        if not diff['differ'] and not args.allow_same_plugins:
            raise SetupError('the two plugin sets are byte identical: this A/B would test the firmware only. Pass --allow-same-plugins if that is the point')
    except SetupError as e:
        print('setup error: %s' % e, file=sys.stderr)
        return EXIT_SETUP
    flavours = {'A': fa, 'B': fb}
    seq = list(args.order) * args.pairs
    do_suite = {i: (args.suite == 'all' or (args.suite == 'first' and i < 2)) for i in range(len(seq))}
    if args.suite == 'none':
        do_suite = {i: False for i in range(len(seq))}
    out = os.path.abspath(args.out or 'ab-out-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    os.makedirs(out, exist_ok=True)
    print('A: %-24s %s  build %s  %d B  %d plugin files' % (fa.label, fa.ptf_name, fa.build, fa.ptf_bytes, len(fa.files)))
    print('B: %-24s %s  build %s  %d B  %d plugin files' % (fb.label, fb.ptf_name, fb.build, fb.ptf_bytes, len(fb.files)))
    print('plugin sets: %d files differ, %d identical' % (len(diff['differ']), len(diff['same'])))
    print('sequence: %s, suite in phases %s, sweep %d repeats x voices %s, restore %s, output %s' % (
        ' '.join(seq), [i for i in range(len(seq)) if do_suite[i]], args.sweep_repeats, args.voices, args.restore, out))
    if args.check_only:
        return EXIT_CLEAN

    goldens = os.path.abspath(args.goldens or os.path.join(out, 'goldens'))
    if args.sim and not args.goldens:
        src = os.path.join(SCN, 'goldens', 'mac')
        if os.path.isdir(src) and not os.path.isdir(goldens):
            shutil.copytree(src, goldens)
    rep = {'keys': ['A', 'B'], 'flavours': {k: f.summary() for k, f in flavours.items()}, 'plugin_diff': diff, 'phases': [], 'measurements': [],
           'suites': [], 'crashes': [], 'cpu_missing': [], 'notes': [], 'abort': None, 'restore': None,
           'meta': {'target': 'mac (ui_shot --serve)' if args.sim else 'device', 'started': datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'), 'seconds': 0,
                    'sequence': seq, 'pairs': args.pairs, 'sweep_repeats': args.sweep_repeats, 'suite': args.suite,
                    'command': ' '.join(shlex.quote(a) for a in sys.argv)}}
    sess = Session(args)
    last_ok = None
    code = EXIT_CLEAN
    try:
        try:
            sess.open()
            info = sess.tr.ping(5.0)
        except (UsbTestError, OSError) as e:
            print('no answer from the console: %s\nIs the unit running a test image (ENABLE_DEV_CONSOLE), is /.develop on the card?' % e, file=sys.stderr)
            return EXIT_SETUP
        started_on = [k for k, f in flavours.items() if f.build == info['build']]
        say('connected: %s %s, %s' % (info['product'], info['build'], ('looks like flavour ' + started_on[0]) if started_on else 'neither flavour'))
        for i, key in enumerate(seq):
            run_phase(sess, flavours, key, i, i // 2, args, out, goldens, rep, do_suite[i])
            last_ok = key
    except AbortRun as e:
        say('ABORT: %s' % e.reason)
        rep['abort'] = {'reason': e.reason, 'kind': e.kind, 'phase': len(rep['phases']) - 1}
        code = EXIT_ABORT
        if e.hand_off:
            rep['restore'] = 'NOT attempted: the bootloader was started and the unit did not come back as expected. ' + usbtest.RECOVERY
    except (Disconnected, Timeout, UsbTestError) as e:
        say('ABORT: the unit was lost: %s' % e)
        rep['abort'] = {'reason': 'the unit was lost: %s' % e, 'kind': 'lost', 'phase': len(rep['phases']) - 1}
        code = EXIT_ABORT
    except KeyboardInterrupt:
        rep['abort'] = {'reason': 'interrupted', 'kind': 'interrupted', 'phase': len(rep['phases']) - 1}
        code = EXIT_ABORT
    # restore
    if args.restore != 'none' and rep['restore'] is None:
        target = args.restore.upper()
        try:
            if sess.t is None:
                raise UsbTestError('never connected')
            if last_ok == target and not rep['abort']:
                problems = verify_flavour(sess.tr, flavours[target], 'restore')
                if problems:
                    raise AbortRun(' / '.join(problems[:4]), kind='mismatch')
                rep['restore'] = 'Flavour %s (%s) was the last phase, verified again: build %s and %d plugin files match.' % (target, flavours[target].label,
                                                                                                                         flavours[target].build, len(flavours[target].files))
            else:
                if rep['abort']:
                    sess.reacquire(60.0)
                upd, swap = bring_to(sess, flavours[target], args, 'restore')
                rep['restore'] = 'Restored flavour %s (%s): update %s, plugins %s, verified.' % (target, flavours[target].label, upd, swap)
            say(rep['restore'])
        except (AbortRun, UpdateFailed, UsbTestError, Disconnected, Timeout, OSError) as e:
            msg = e.reason if isinstance(e, AbortRun) else str(e)
            rep['restore'] = 'RESTORE FAILED: %s. %s' % (msg, usbtest.RECOVERY)
            say(rep['restore'])
            code = EXIT_ABORT
    elif rep['restore'] is None:
        rep['restore'] = 'Not restored (--restore none): the unit runs the last phase.'
    if sess.t is not None:
        sess.release()
    write_reports(rep, out)
    say('result: %s' % rep['result'])
    say('report: %s and %s' % (os.path.join(out, 'ab_report.md'), os.path.join(out, 'ab_report.json')))
    if code == EXIT_CLEAN and rep['result'] != 'CLEAN':
        code = EXIT_FINDINGS
    return code


if __name__ == '__main__':
    sys.exit(main())
