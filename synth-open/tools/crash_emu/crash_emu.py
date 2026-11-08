#!/usr/bin/env python3
"""Targeted Mac checks of the crash-guards changed paths, against a sanitizer ui_shot built with SAN=address,undefined STACKMODEL=1
(host_fault.cpp, host_poke.cpp and the UISHOT_COVER counter of host_stackmodel.cpp are in that build). Host tool, NOT firmware.

  python3 -I crash_emu.py --ui UI_SHOT --out DIR --label guards|base [--only a1,c1] [--plg-dir DIR]

Needs the poly-devtools usbtest tools (DT, default ~/prj/poly-devtools-wt-newlist) for the console transport and the scenario context.
Each scenario starts its own ui_shot on a private copy of one staged SD card (fixtures: 150 wavs, a 150 s wav, 130 project folders,
a Games folder with long .nes names), with the sanitizer's stderr kept, and reports checks plus the function coverage of the changed code.
A finding that is not in the known stock list (host-side ALLOW) is a failure.
"""
import argparse
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import wave

DT = os.environ.get('DT', os.path.expanduser('~/prj/poly-devtools-wt-newlist')) + '/tools/usbtest'
sys.path[:0] = [DT, DT + '/scenarios']
import lib  # noqa: E402
import suite  # noqa: E402
from usbtest import Disconnected, SimTransport, Tracker, UsbTestError  # noqa: E402

# stock findings that are in the baseline too (ref/mac-emulator-plan.md section 4, fPatternEdit.cpp:4628, mtWavetableEnvelopes.cpp:206, mtAudioEngine.cpp:1655)
ALLOW = ['mtWavetableEnvelopes.cpp:206', 'fPatternEdit.cpp:4628', 'functPads', 'mtAudioEngine.cpp:1655']
COVER = ['stopOperationWithError', 'WorkspaceProjectLoad::', 'ProjectOpen::', 'RecordingSave::', 'cFileManager::browseGamesLocation', 'cFileManager::listOnlyWav',
         'cFileManager::browseProjectsLocation', 'cSampleImporter::showImportStartPointValue', 'cSampleImporter::showImportEndPointValue', 'cSampleImporter::showImportPreviewValue',
         'changeSynthReverbSend', 'changeSynthDelaySend', 'setDefaultSynthReverb', 'setDefaultSynthDelay', 'instrumentIdxToSynthIdx']
GAMES = ['a.nes', 'B.NES', 'x' * 49 + '.nes', 'y' * 60 + '.NeS', 'z' * 200 + '.nes', 'dotted.name.nes.nes', 'noext', 'pre.nesx.nes']


def build_card(out, plg_dir):
    sd, _ = suite.stage_sd(out, ui_path, plg_dir, True, 'Demo')
    os.makedirs(sd + '/Samples/big', exist_ok=True)
    for i in range(150):
        with wave.open(sd + '/Samples/big/w%03d.wav' % i, 'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(44100); w.writeframes(b'\x00\x10' * 441)
    with wave.open(sd + '/Samples/long150.wav', 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(44100)
        blk = b''.join(struct.pack('<h', int(8000 * math.sin(i * 0.05))) for i in range(44100))
        for _ in range(150):
            w.writeframes(blk)
    for name, secs in (('zlong100.wav', 100), ('zlong1000.wav', 1000)):
        with wave.open(sd + '/Samples/' + name, 'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(44100)
            blk = b''.join(struct.pack('<h', int(8000 * math.sin(i * 0.05))) for i in range(44100))
            for _ in range(secs):
                w.writeframes(blk)
    for i in range(130):
        os.makedirs(sd + '/Projects/proj%03d' % i, exist_ok=True)
    return sd


class Sim:
    """One ui_shot on its own copy of the card. env: extra environment for the process."""

    def __init__(self, name, env=None, prep=None):
        self.name = name
        self.sd = os.path.join(OUT, 'sd_' + name)
        shutil.rmtree(self.sd, ignore_errors=True)
        shutil.copytree(CARD, self.sd, symlinks=True)
        if prep:
            prep(self.sd)
        ACTIVE.append(self)
        self.stderr = os.path.join(OUT, name + '.stderr')
        self.cover = os.path.join(OUT, name + '.cover')
        self.poke = os.path.join(OUT, name + '.poke')
        for p in (self.stderr, self.cover, self.poke, self.poke + '.out'):
            if os.path.exists(p):
                os.remove(p)
        e = {'ASAN_OPTIONS': 'halt_on_error=0:detect_leaks=0', 'USBTEST_SIM_STDERR': self.stderr, 'UISHOT_COVER': ','.join(COVER),
             'UISHOT_COVER_REPORT': self.cover, 'UISHOT_POKE': self.poke}
        e.update(env or {})
        self.old = {k: os.environ.get(k) for k in e}
        os.environ.update(e)
        self.transport = SimTransport(UI, self.sd)
        self.tr = Tracker(self.transport, heartbeat=0)
        self.tr.ping()
        self.ctx = lib.Ctx(self.tr, 'mac', OUT, lib.Goldens(OUT + '/g', record=False), name, {})
        self.alive = True

    def poke_cmd(self, *lines, wait_ms=600):
        with open(self.poke, 'w') as f:
            f.write('\n'.join(lines) + '\n')
        for _ in range(40):
            self.tr.wait(100)
            if not os.path.exists(self.poke):
                break
        self.tr.wait(wait_ms)

    def poke_out(self):
        p = self.poke + '.out'
        return open(p).read().splitlines() if os.path.exists(p) else []

    def close(self):
        if self in ACTIVE:
            ACTIVE.remove(self)
        try:
            self.transport.close()
        except Exception:  # noqa: BLE001
            pass
        for k, v in self.old.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v

    def findings(self):
        """(new, known) sanitizer SUMMARY lines of this run."""
        new, known = [], []
        if os.path.exists(self.stderr):
            for l in open(self.stderr, errors='replace'):
                if l.startswith('SUMMARY') or 'signal 11' in l or 'signal 6' in l:
                    (known if any(a in l for a in ALLOW) else new).append(l.strip()[:200])
        return new, known

    def coverage(self):
        d = {}
        if os.path.exists(self.cover):
            for l in open(self.cover):
                n, _, name = l.rstrip('\n').partition('\t')
                if name:
                    d[name] = int(n)
        return d


RESULTS = []
ACTIVE = []


def cleanup():
    """Whatever a scenario left running: stop it and put the environment back (a crashed scenario must not leave UISHOT_FAIL set)."""
    while ACTIVE:
        ACTIVE.pop().close()


def scenario(code, title):
    def deco(fn):
        fn.code, fn.title = code, title
        RESULTS.append(fn)
        return fn
    return deco


class Rec:
    def __init__(self, fn):
        self.code, self.title = fn.code, fn.title
        self.checks, self.notes, self.cover, self.new, self.known, self.crashed = [], [], {}, [], [], False
        self.residual = False

    def check(self, cond, text):
        self.checks.append((bool(cond), text))
        return bool(cond)


def finish(rec, sim, crashed=False):
    sim.close()
    time.sleep(0.3)
    rec.cover = sim.coverage()
    rec.new, rec.known = sim.findings()
    rec.crashed = rec.crashed or crashed
    rec.check(not rec.new, 'no sanitizer finding or crash beyond the known stock ones (%d new, %d known)' % (len(rec.new), len(rec.known)))


def covered(rec, *subs):
    for s in subs:
        n = sum(v for k, v in rec.cover.items() if s in k)
        rec.check(n > 0, 'changed path reached: %s (%d entries)' % (s, n))


def shotcrc(ctx):
    ctx.settle(500)
    c = ctx.tr.shotcrc()
    ctx.settle(300)
    return '%08x' % (__import__('zlib').crc32(json.dumps(c).encode()) & 0xffffffff)


def to_sampleload_folder(ctx, downs):
    ctx.tap('SampleLoad', 1, 1500)
    ctx.tap('Down', downs, 500)
    ctx.tap('F1', 1, 1500)
    ctx.settle(1500)


# ------------------------------------------------------------------------------------------------------------ scenarios
@scenario('a1', 'open a normal project (twice, same state)')
def a1(rec):
    s = Sim('a1')
    try:
        for i in range(2):
            s.tr.open_project('Projects', 'Demo', force=True)
            s.ctx.settle(500)
        h1 = s.tr.hash_project()
        s.tr.open_project('Projects', 'Demo', force=True)
        s.ctx.settle(500)
        rec.check(s.tr.hash_project() == h1, 'the same project loads to the same state (%s)' % h1)
        rec.check(s.ctx.page() == 'pattern', 'back on the pattern page')
    except (Disconnected, UsbTestError) as e:
        rec.check(False, 'console lost: %s' % e)
    finish(rec, s)
    covered(rec, 'ProjectOpen::')


@scenario('a2', 'cold start loads the Workspace project (boot path)')
def a2(rec):
    s = Sim('a2')
    try:
        rec.check(s.ctx.page() == 'pattern', 'boots to the pattern page')
        rec.check(s.tr.hash_project() != '', 'project state readable')
    except (Disconnected, UsbTestError) as e:
        rec.check(False, 'console lost: %s' % e)
    finish(rec, s)
    n = sum(v for k, v in rec.cover.items() if 'WorkspaceProjectLoad::loadProjectFileFromWorkspace' in k)
    rec.notes.append('WorkspaceProjectLoad::loadProjectFileFromWorkspace entries on the cold start: %d' % n)


@scenario('a3', 'workspace load with the project image allocation failing (size of strProjectFile)')
def a3(rec):
    s0 = Sim('a3size')
    s0.poke_cmd('sizes')
    out = s0.poke_out()
    s0.close()
    m = re.search(r'sizeof\(strProjectFile\)=(\d+)', ' '.join(out))
    size = int(m.group(1)) if m else 0
    rec.check(size > 0, 'sizeof(strProjectFile) = %d' % size)
    arm = os.path.join(OUT, 'a3.arm')
    open(arm, 'w').write('1')
    s = Sim('a3', {'UISHOT_FAIL': 'size=%d,count=0' % size, 'UISHOT_FAIL_ARM': arm, 'UISHOT_FAIL_REPORT': os.path.join(OUT, 'a3.failrep')})
    if os.path.exists(os.path.join(OUT, 'a3.failrep')):
        os.remove(os.path.join(OUT, 'a3.failrep'))
    crashed = False
    try:
        # the cold start is already over, the failing allocation is armed for every open from here on
        s.tr.open_project('Projects', 'Demo', force=True)
        s.ctx.settle(800)
        rec.check(s.ctx.page() == 'pattern', 'the project opened with allocations of that size failing')
    except (Disconnected, UsbTestError) as e:
        crashed = True
        rec.check(False, 'console lost during the open: %s' % e)
    fr = os.path.join(OUT, 'a3.failrep')
    n = len(open(fr).read().splitlines()) if os.path.exists(fr) else 0
    rec.notes.append('allocations of %d B failed on purpose: %d' % (size, n))
    finish(rec, s, crashed)
    rec.notes.append('(the nothrow branch of loadProjectFileFromWorkspace is compiled only without FEATURE_SYNTH_OPEN: not in this build)')


@scenario('b1', 'browse a folder with 150 wavs and the 130 project folders to the end of the list')
def b1(rec):
    s = Sim('b1')
    ctx = s.ctx
    try:
        to_sampleload_folder(ctx, 3)       # /Samples
        ctx.tap('Down', 1, 500)             # /big
        ctx.tap('F1', 1, 1500)
        ctx.settle(2000)
        top = shotcrc(ctx)
        for _ in range(16):
            ctx.tap('Down', 10, 100)
        ctx.settle(1500)
        end = shotcrc(ctx)
        rec.check(top != end, 'the list scrolled (tile CRC %s -> %s)' % (top, end))
        ctx.tr.shot(os.path.join(OUT, 'b1_end_%s.png' % LABEL))
        rec.notes.append('end of the 150 entry folder: tile CRC %s' % end)
        # project list
        ctx.tap('File', 1, 1500)
        ctx.settle(1000)
        rec.notes.append('File page title: %s' % ctx.title())
        ctx.tr.shot(os.path.join(OUT, 'b1_file_%s.png' % LABEL))
    except (Disconnected, UsbTestError) as e:
        rec.check(False, 'console lost: %s' % e)
    finish(rec, s)
    covered(rec, 'cFileManager::listOnlyWav')


def importer_labels(rec, code, downs, secs):
    """Open the file `downs` rows below the folder top in the importer, drag the start point to the end, read the three label buffers."""
    s = Sim(code)
    ctx = s.ctx
    last = '%.3fs' % secs
    try:
        to_sampleload_folder(ctx, 3)
        ctx.tap('Down', downs, 500)
        ctx.settle(1000)
        ctx.tap('F5', 1, 1500)
        ctx.settle(3000 + int(secs * 3))
        ctx.tap('F2', 1, 500)
        for _ in range(60):
            ctx.enc(200, 60)
        ctx.settle(800)
        ctx.tr.shot(os.path.join(OUT, '%s_start_%s.png' % (code, LABEL)))
        s.poke_cmd('importlabels')
        out1 = '\n'.join(s.poke_out())
        s.poke_cmd('importplayend')
        s.poke_cmd('importlabels')
        out2 = '\n'.join(s.poke_out()[len(out1.splitlines()) + 1:])
        rec.notes.append(out1)
        rec.notes.append('after importplayend:\n' + out2)
        m = re.search(r'sizeof\(start\)=(\d+) sizeof\(end\)=(\d+) sizeof\(play\)=(\d+)', out1)
        if m:
            rec.notes.append('buffer sizes start/end/play = %s/%s/%s' % m.groups())
            need = len(last) + 1
            for nm, v in zip(('start', 'end', 'play'), m.groups()):
                rec.check(int(v) >= need, '%s label buffer holds "%s" and the NUL (%s B, needs %d)' % (nm, last, v, need))
        for tag, out in (('drag', out1), ('play end', out2)):
            st = re.search(r'start: ((?:[0-9a-f]{2} )+)', out)
            en = re.search(r'end:   ((?:[0-9a-f]{2} )+)', out)
            pl = re.search(r'play:  ((?:[0-9a-f]{2} )+)', out)
            if st and en and pl:
                txt = [bytes.fromhex(x.group(1)) for x in (st, en, pl)]
                for nm, b in zip(('start', 'end', 'play'), txt):
                    if nm == 'play' and tag == 'drag':
                        continue
                    t = b.split(b'\0')[0].decode(errors='replace')
                    rec.check(b'\0' in b, '%s: %s label buffer is NUL terminated inside its %d bytes' % (tag, nm, len(b)))
                    if nm == 'start':
                        ok = re.fullmatch(r'\d+\.\d{3}s', t) is not None and secs - 0.5 <= float(t[:-1]) <= secs
                        rec.check(ok, '%s: start label "%s" is a complete value near the end of the %d s sample' % (tag, t, secs))
                    else:
                        rec.check(t == last, '%s: %s label reads "%s" (%s), not truncated' % (tag, nm, last, t))
            else:
                rec.check(False, '%s: label dump parsed' % tag)
    except (Disconnected, UsbTestError) as e:
        rec.check(False, 'console lost: %s' % e)
    finish(rec, s)
    covered(rec, 'cSampleImporter::showImportStartPointValue', 'cSampleImporter::showImportEndPointValue', 'cSampleImporter::showImportPreviewValue', 'cSampleImporter::showImportPreviewValue')


@scenario('c1', 'sample importer with a 150 s sample: start point at the end, labels read byte by byte')
def c1(rec):
    importer_labels(rec, 'c1', 2, 150)             # long150.wav (after /.. and /big)


@scenario('c2', 'sample importer with a 100 s sample (the label needs exactly 8 characters plus the NUL)')
def c2(rec):
    importer_labels(rec, 'c2', 3, 100)             # zlong100.wav


@scenario('c3', 'sample importer with a 1000 s sample (the label needs 9 characters plus the NUL)')
def c3(rec):
    importer_labels(rec, 'c3', 4, 1000)            # zlong1000.wav


@scenario('d1', 'Games folder with .nes names of 5, 53, 64, 204 characters (browseGames via the poke hook, no UI page calls it)')
def d1(rec):
    def games(sd):
        os.makedirs(sd + '/Games', exist_ok=True)
        for n in GAMES:
            open(sd + '/Games/' + n, 'wb').write(b'NES\x1a')

    s = Sim('d1', prep=games)
    try:
        s.poke_cmd('browsegames')
        s.tr.wait(2000)
        s.poke_cmd('dumpgames')
        out = s.poke_out()
        rec.notes += out
        names = [re.search(r"'(.*)'", l).group(1) for l in out if l.startswith('game[')]
        rec.check(len(names) >= 6, 'the .nes files are listed (%d entries, the host list stand-in does not apply the .nes filter of SD.cpp)' % len(names))
        rec.check(all('.nes' not in n.lower() for n in names), 'every ".nes" is cut off')
        rec.check(any(len(n) == 200 for n in names), 'the 204 character name survived as 200 characters')
    except (Disconnected, UsbTestError) as e:
        rec.check(False, 'console lost: %s' % e)
    finish(rec, s)
    covered(rec, 'cFileManager::browseGamesLocation')


def wav_info(path):
    with wave.open(path, 'rb') as w:
        return w.getnchannels(), w.getnframes(), w.readframes(w.getnframes())


@scenario('e1', 'stereo recording save, then import from the recorder into instrument 5')
def e1(rec):
    s = Sim('e1')
    try:
        s.poke_cmd('savestereo take_st 4096 4', wait_ms=3000)
        s.tr.wait(3000)
        s.poke_cmd('status')
        rec.notes += s.poke_out()
        p = s.sd + '/Recorded/take_st.wav'
        rec.check(os.path.exists(p), 'Recorded/take_st.wav written')
        if os.path.exists(p):
            ch, n, data = wav_info(p)
            rec.check(ch == 2 and n == 4096, 'a 2 channel wav of 4096 frames (%d ch, %d frames)' % (ch, n))
            exp = b''.join(struct.pack('<hh', i & 0x7fff, -(i & 0x7fff)) for i in range(4096))
            rec.check(data == exp, 'the interleaved samples are L ramp, R negative ramp')
        s.poke_cmd('savemono take_mono 2048', wait_ms=3000)
        pm = s.sd + '/Recorded/take_mono.wav'
        rec.check(os.path.exists(pm) and wav_info(pm)[0] == 1, 'a mono save takes the plain path (1 channel wav)')
    except (Disconnected, UsbTestError) as e:
        rec.check(False, 'console lost: %s' % e)
    finish(rec, s)
    covered(rec, 'RecordingSave::updateSave', 'RecordingSave::finishSave')


@scenario('e2', 'stereo recording save with the 2048 byte interleave buffer failing')
def e2(rec):
    arm = os.path.join(OUT, 'e2.arm')
    if os.path.exists(arm):
        os.remove(arm)
    rep = os.path.join(OUT, 'e2.failrep')
    if os.path.exists(rep):
        os.remove(rep)
    s = Sim('e2', {'UISHOT_FAIL': 'size=2048,count=0', 'UISHOT_FAIL_ARM': arm, 'UISHOT_FAIL_REPORT': rep})
    crashed = False
    try:
        open(arm, 'w').write('1')
        with open(s.poke, 'w') as f:
            f.write('savestereo take_fail 4096\n')
        seen = []
        for _ in range(30):
            s.tr.wait(60)
            if not os.path.exists(s.poke):
                s.poke_cmd('status', wait_ms=0)
                line = s.poke_out()[-1] if s.poke_out() else ''
                m = re.search(r'fm status (\d+)', line)
                if m and (not seen or seen[-1] != int(m.group(1))):
                    seen.append(int(m.group(1)))
        os.remove(arm)
        out = s.poke_out()
        rec.notes += out[:2]
        n = len(open(rep).read().splitlines()) if os.path.exists(rep) else 0
        rec.check(n > 0, 'the allocation failed on purpose (%d times)' % n)
        rec.notes.append('file manager status polled after the save started: %s (the interface clears an error status within a loop pass, so it is read through the coverage counter below)' % seen)
        wp = s.sd + '/Recorded/take_fail.wav'
        rec.check(os.path.exists(wp) and os.path.getsize(wp) <= 44, 'the incomplete wav was closed with no sample data written (%s B)' % (os.path.getsize(wp) if os.path.exists(wp) else 'missing'))
        s.ctx.tap('Pattern', 1, 500)
        rec.check(s.ctx.page() == 'pattern', 'the UI still answers after the failed save')
    except (Disconnected, UsbTestError) as e:
        crashed = True
        rec.check(False, 'console lost: %s' % e)
    finish(rec, s, crashed)
    covered(rec, 'stopOperationWithError')


def goto_synth_params(ctx):
    ctx.select_synth(1)
    ctx.tap('Params', 1, 500)
    ctx.settle(500)


@scenario('f1', 'synth editor reverb and delay send: change and default (synth part 1)')
def f1(rec):
    s = Sim('f1')
    ctx = s.ctx
    try:
        goto_synth_params(ctx)
        rec.check(ctx.title().startswith('Synth Parameters'), 'on the synth parameters page')
        h0 = s.tr.hash_project()
        ctx.tap('Right', 4, 300)               # Reverb send
        a = shotcrc(ctx)
        ctx.tap('Up', 5, 300)
        b = shotcrc(ctx)
        ctx.tap('Delete', 1, 400)
        c = shotcrc(ctx)
        ctx.tap('Right', 1, 300)               # Delay send
        ctx.tap('Up', 5, 300)
        d = shotcrc(ctx)
        ctx.tap('Delete', 1, 400)
        e = shotcrc(ctx)
        rec.check(a != b, 'Reverb send moves with Up (%s -> %s)' % (a, b))
        rec.check(b != c, 'Delete resets the Reverb send to the default (%s -> %s)' % (b, c))
        rec.check(d != e, 'Delete resets the Delay send to the default (%s -> %s)' % (d, e))
        rec.notes.append('project hash before %s after %s' % (h0, s.tr.hash_project()))
    except (Disconnected, UsbTestError) as e:
        rec.check(False, 'console lost: %s' % e)
    finish(rec, s)
    covered(rec, 'changeSynthReverbSend', 'changeSynthDelaySend', 'setDefaultSynthReverb', 'setDefaultSynthDelay')


@scenario('f2', 'synth editor keys while the last used instrument is a sample instrument (the -1 -> 255 index)')
def f2(rec):
    s = Sim('f2')
    ctx = s.ctx
    crashed = False
    try:
        goto_synth_params(ctx)
        h0 = s.tr.hash_project()
        ctx.tap('Right', 4, 300)
        s.poke_cmd('lastinstr 0')              # instrument 1, a sample instrument
        for col in (0, 1):
            ctx.tap('Up', 3, 200)
            ctx.tap('Down', 4, 200)
            ctx.tap('Delete', 1, 300)
            ctx.tap('Right', 1, 300)
        s.poke_cmd('lastinstr 64')
        ctx.settle(500)
        rec.notes += s.poke_out()
        rec.check(s.tr.hash_project() == h0, 'the project is unchanged by the keys on a non-synth instrument (%s)' % h0)
    except (Disconnected, UsbTestError) as e:
        crashed = True
        rec.check(False, 'console lost: %s' % e)
    finish(rec, s, crashed)
    covered(rec, 'instrumentIdxToSynthIdx')


def main():
    global UI, OUT, LABEL, CARD, ui_path
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--ui', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--label', default='run')
    ap.add_argument('--only', default='')
    ap.add_argument('--plg-dir')
    a = ap.parse_args()
    UI = ui_path = os.path.abspath(a.ui)
    OUT = os.path.abspath(a.out)
    LABEL = a.label
    os.makedirs(OUT, exist_ok=True)
    plg = suite.find_plg_dir(a.plg_dir)
    CARD = build_card(OUT + '/card', plg)
    only = [x for x in a.only.split(',') if x]
    out = []
    for fn in RESULTS:
        if only and fn.code not in only:
            continue
        rec = Rec(fn)
        t0 = time.monotonic()
        try:
            fn(rec)
        except Exception as e:  # noqa: BLE001
            rec.check(False, 'scenario error: %r' % e)
        cleanup()
        ok = all(c for c, _ in rec.checks)
        print('%-3s %-5s %s (%.0f s)' % (rec.code, 'PASS' if ok else 'FAIL', rec.title, time.monotonic() - t0), flush=True)
        for c, t in rec.checks:
            if not c:
                print('       FAILED: %s' % t)
        for n in rec.new[:3]:
            print('       finding: %s' % n)
        out.append({'code': rec.code, 'title': rec.title, 'ok': ok, 'checks': rec.checks, 'notes': rec.notes, 'new': rec.new, 'known': rec.known,
                    'residual': rec.residual, 'cover': {k: v for k, v in rec.cover.items() if v}})
    with open(os.path.join(OUT, 'crash_emu_%s.json' % LABEL), 'w') as f:
        json.dump(out, f, indent=1)
    return 0 if all(r['ok'] for r in out) else 1


if __name__ == '__main__':
    sys.exit(main())
