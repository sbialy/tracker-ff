"""Scenario library for the USB test console: the context a scenario runs in, golden storage, metrics. Host tool, NOT firmware.

A scenario is a function `fn(ctx)` registered with @scenario. It drives the Tracker only through console commands (key, pad, enc,
idle, texts, shotcrc, status, peek, hash, open, play, stop, get, put, rm, reboot, markers, cpu), so the same scenario runs against
`ui_shot --serve` on the Mac and against the device. What only a device can do is declared with ctx.device_only() (skipped on the
Mac and reported as such) and what needs a human with ctx.manual().

Rules the helpers enforce (lessons from the console work):
  - a screenshot (shotcrc or shot) rebuilds every control and restarts list animations: capture() settles before and after it
  - idle is not enough on pages that keep redrawing, settle() also waits a fixed time
  - texts are read BEFORE the tile capture of the same checkpoint
  - a key that was pressed is released when the scenario ends, whatever happens
"""
import json
import os
import re
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import usbtest  # noqa: E402
from usbtest import UsbTestError  # noqa: E402

BLOCK_US = 2902   # 128 samples at 44.1 kHz: the whole audio block budget (Profiler.h kMaxAudioUpdateTime)
SLOT_STATES = ['empty', 'pending', 'running', 'missing', 'failed', 'safe', 'tripped', 'bypassed']

REGISTRY = []   # (name, fn, meta)


def scenario(name, tags=(), writes=False, reboots=False, sim=True, summary=''):
    """Registers a scenario. writes: it writes files on the card (refused on a device without --device-writes).
    reboots: it resets the unit. sim=False: it cannot run against ui_shot (the reason goes in summary)."""
    def deco(fn):
        REGISTRY.append((name, fn, {'tags': list(tags), 'writes': writes, 'reboots': reboots, 'sim': sim,
                                    'summary': summary or (fn.__doc__ or '').strip().split('\n')[0]}))
        return fn
    return deco


class Skip(Exception):
    def __init__(self, reason, kind='precondition'):
        super().__init__(reason)
        self.reason, self.kind = reason, kind


class Abort(Exception):
    """A hard failure: the rest of the scenario makes no sense."""


# ------------------------------------------------------------------------------------------------ text normalisation
# Things on screen that change between runs and are not a regression: the song clock, the step position, CPU and heap readouts
VOLATILE = [
    (re.compile(r'\d+m:\d\ds'), '#m:#s'),
    (re.compile(r'P\d+\.\d+'), 'P#.#'),
    (re.compile(r'min: \d+%, max: \d+%'), 'min: #%, max: #%'),
    (re.compile(r'free heap: \d+ B'), 'free heap: # B'),
    (re.compile(r'(Inserts)( \d+(\.\d+)?%)+'), r'\1 #%'),
    (re.compile(r'\d+(\.\d+)?%'), '#%'),
]


def norm_text(t):
    t = t.strip()
    for rx, rep in VOLATILE:
        t = rx.sub(rep, t)
    return re.sub(r'\s+', ' ', t).strip()


def tile_name(i):
    return 'r%dc%d' % (i // 8, i % 8)


def digest(texts, tiles):
    """A short hash of one checkpoint, for the baseline diff."""
    return '%08x' % (zlib.crc32(json.dumps([texts, tiles]).encode()) & 0xFFFFFFFF)


# ------------------------------------------------------------------------------------------------ goldens
class Goldens:
    """goldens/<target>/<scenario>.json: {"checkpoints": {key: {"texts": [...], "tiles": [48 ints], "mask": [tile numbers], "volatile": [text indexes]}}}.
    A checkpoint without a golden is recorded (unless record is off), --update re-records."""

    def __init__(self, root, record=True, update=False):
        self.root, self.record, self.update = root, record, update
        self.cache, self.dirty = {}, set()
        self.stabilise = False   # second pass over fresh goldens: what differs from the first pass is volatile, not a failure
        self.fresh = set()       # (scenario, key) recorded in this run

    def _path(self, scn):
        return os.path.join(self.root, scn + '.json')

    def _load(self, scn):
        if scn not in self.cache:
            p = self._path(scn)
            if os.path.exists(p):
                with open(p) as f:
                    self.cache[scn] = json.load(f)
            else:
                self.cache[scn] = {'version': 1, 'checkpoints': {}}
        return self.cache[scn]

    def get(self, scn, key):
        return self._load(scn)['checkpoints'].get(key)

    def put(self, scn, key, entry):
        self._load(scn)['checkpoints'][key] = entry
        self.dirty.add(scn)
        self.fresh.add((scn, key))

    def meta(self, scn, **kv):
        self._load(scn).update(kv)

    def discard(self, scn):
        self.cache.pop(scn, None)
        self.dirty.discard(scn)
        self.fresh = {f for f in self.fresh if f[0] != scn}

    def flush(self, scn):
        if scn in self.dirty:
            os.makedirs(self.root, exist_ok=True)
            with open(self._path(scn), 'w') as f:
                json.dump(self.cache[scn], f, indent=1, sort_keys=True)
                f.write('\n')
            self.dirty.discard(scn)


# ------------------------------------------------------------------------------------------------ context
class Ctx:
    def __init__(self, tr, target, outdir, goldens, name, opts):
        self.tr, self.target, self.outdir, self.goldens, self.name, self.opts = tr, target, outdir, goldens, name, opts
        self.steps = []         # everything that happened, for the report
        self.checkpoints = {}   # key -> {status, digest, ...}
        self.metrics = {}       # name -> {value, unit}
        self.soft_failures = 0
        self.held = []
        self.pads = []
        self.t0 = time.monotonic()
        self.skips = []

    # ---- bookkeeping
    @property
    def sim(self):
        return self.target == 'mac'

    def note(self, msg):
        self.steps.append({'kind': 'note', 'text': msg})

    def check(self, cond, msg):
        """A soft assertion: recorded, the scenario goes on and ends FAIL."""
        self.steps.append({'kind': 'check', 'ok': bool(cond), 'text': msg})
        if not cond:
            self.soft_failures += 1
        return bool(cond)

    def require(self, cond, msg):
        """A hard assertion: the scenario stops."""
        if not self.check(cond, msg):
            raise Abort(msg)

    def metric(self, name, value, unit='', limit=None, gate_on='device'):
        """Records a number. limit: a hard ceiling that fails the scenario (only on the target named in gate_on, 'any' for both)."""
        self.metrics[name] = {'value': value, 'unit': unit}
        if limit is not None and gate_on in ('any', 'device' if not self.sim else 'mac'):
            self.check(value <= limit, '%s = %s %s, limit %s' % (name, value, unit, limit))

    def device_only(self, what):
        """True when the step cannot run here (the Mac front end): the step is listed as skipped, the caller returns or goes on."""
        if self.sim:
            self.steps.append({'kind': 'skip', 'why': 'device-only', 'text': what})
            self.skips.append(what)
            return True
        return False

    def manual(self, what):
        """A step that needs a human at the unit (hold Shift while it boots, a power cycle ...). Never blocks an unattended run."""
        if self.opts.get('interactive') and not self.sim:
            print('\n  MANUAL STEP: %s\n  press Enter when done, or type s to skip: ' % what, end='', flush=True)
            if sys.stdin.readline().strip().lower() != 's':
                self.steps.append({'kind': 'note', 'text': 'manual step done: ' + what})
                return False
        self.steps.append({'kind': 'skip', 'why': 'manual', 'text': what})
        self.skips.append('manual: ' + what)
        return True

    def need(self, cond, reason):
        if not cond:
            raise Skip(reason)

    # ---- input and timing
    def settle(self, ms=800):
        self.tr.idle()
        self.tr.wait(ms)
        self.tr.idle()

    def tap(self, name, n=1, settle=0):
        self.tr.key(name, 'tap', n)
        if settle:
            self.settle(settle)

    def press(self, name):
        self.tr.key(name, 'press')
        self.held.append(name)

    def release(self, name):
        self.tr.key(name, 'release')
        if name in self.held:
            self.held.remove(name)

    def pad_press(self, n):
        self.tr.pad(n, 'press')
        self.pads.append(n)

    def pad_release(self, n):
        self.tr.pad(n, 'release')
        if n in self.pads:
            self.pads.remove(n)

    def release_all(self):
        for k in list(self.held):
            try:
                self.release(k)
            except UsbTestError:
                self.held.remove(k)
        for n in list(self.pads):
            try:
                self.pad_release(n)
            except UsbTestError:
                self.pads.remove(n)

    def tap_until(self, name, text, limit=5, settle=500):
        """Taps a page button until `text` is on screen: a page that has sub pages remembers the one it was left on."""
        if self.has_text(text):
            return True    # already there: the page's own button would move to its next sub page (or toggle a view)
        for _ in range(limit):
            self.tr.key(name, 'tap', 1)
            self.settle(settle)
            if self.has_text(text):
                return True
        return self.check(False, 'could not reach "%s" with %s' % (text, name))

    def combo(self, mod, key, settle=300):
        """A modifier combination: the modifier is held while the key is tapped (the way a hand does it)."""
        self.press(mod)
        try:
            self.tr.key(key, 'tap', 1)
        finally:
            self.release(mod)
        if settle:
            self.settle(settle)

    def enc(self, delta, settle=150):
        self.tr.enc(delta)
        self.tr.idle()   # the encoder is read every 100 ms: let the value arrive before anything is released
        self.tr.wait(settle)
        self.tr.idle()

    # ---- state
    def status(self):
        return self.tr.status()

    def page(self):
        return self.tr.status().get('name', '?')

    def texts(self):
        return [norm_text(t['text']) for t in self.tr.texts() if norm_text(t['text'])]

    def raw_texts(self):
        return [t['text'].strip() for t in self.tr.texts() if t['text'].strip()]

    def title(self):
        """The page title: the first text that is not the song clock."""
        for t in self.raw_texts():
            if not re.match(r'\d+m:\d\ds', t):
                return t
        return ''

    def has_text(self, sub):
        return any(sub in t for t in self.raw_texts())

    def expect_page(self, name):
        got = self.page()
        return self.check(got == name, 'page is %s, expected %s' % (got, name))

    def expect_text(self, sub):
        return self.check(self.has_text(sub), 'text "%s" on screen' % sub)

    def step_position(self):
        """The pattern step the cursor is on (the P1.016 of the clock control), None when no such text."""
        for t in self.raw_texts():
            m = re.search(r'P(\d+)\.(\d+)', t)
            if m:
                return int(m.group(2))
        return None

    # ---- known state
    def reset_ui(self):
        """Back to a state every scenario can start from: stopped, nothing held, Pattern page, instrument 1, cursor on step 1."""
        self.tr.t.drain(0.05)
        if self.tr.status().get('play') == '1':
            self.tr.command('stop')
            self.tr.wait(200)
        for _ in range(4):
            if self.page() == 'pattern':
                break
            self.tr.key('Pattern')
            self.settle(300)
        self.tr.key('Instr', 'press')    # Instr held and a pad tapped: that pad's instrument
        self.tr.pad(0, 'tap')
        self.tr.key('Instr', 'release')
        self.combo('Shift', 'Insert', 200)   # Shift + Insert = Home
        self.settle(500)

    def goto_master(self, first=True):
        """The Master page, on its first stop (Master 1/n) whatever the button cycle was left on."""
        for _ in range(6):
            if self.page() == 'master' and self.title().startswith('Master 1/'):
                return True
            self.tr.key('Master')
            self.settle(400)
        return self.check(False, 'could not reach Master 1/n')

    def select_synth(self, part):
        """Synth part 1..3 through the keys: hold Instr until the instrument list shows, turn the encoder to the first entry
        (the synth parts are the first three), then up to the part."""
        self.tr.key('Pattern')
        self.settle(300)
        self.press('Instr')
        try:
            self.tr.wait(800)
            self.enc(-100, 200)
            if part > 1:
                self.enc(part - 1, 200)
        finally:
            self.release('Instr')
        self.settle(300)
        want = 63 + part
        got = self.tr.peek('lastUsedInstrument')
        self.require(got == want, 'synth part %d selected (lastUsedInstrument %d, expected %d)' % (part, got, want))

    # ---- files
    def get_text(self, path):
        return self.tr.get(path).decode('latin-1')

    def exists(self, path):
        d, _, n = path.rpartition('/')
        try:
            return any(e['name'] == n for e in self.tr.ls(d or '/'))
        except UsbTestError:
            return False

    def host_txt(self):
        """/Plugins/host.txt parsed: {safe, reason, inserts_on, mixer, plugins: [names], slots: [{name, uid, state}]}"""
        txt = self.get_text('/Plugins/host.txt').replace('\r\n', '\n')
        out = {'plugins': [], 'slots': [], 'mixer': '', 'safe': 0, 'reason': '', 'inserts_on': False, 'nvm': 0, 'raw': txt}
        for line in txt.split('\n'):
            m = re.match(r'plugin host 1, index (\d+), inserts (\w+), safe (\d) ?(.*?), nvm commands (\d+)', line)
            if m:
                out.update(count=int(m.group(1)), inserts_on=(m.group(2) == 'on'), safe=int(m.group(3)), reason=m.group(4), nvm=int(m.group(5)))
            m = re.match(r'mixer (.*)', line)
            if m:
                out['mixer'] = m.group(1).split(',')[0].strip()
            m = re.match(r'plugin (\S.*?) uid ([0-9a-f]{8}) v (\d+) file (\S+)', line)
            if m:
                out['plugins'].append({'name': m.group(1), 'uid': m.group(2), 'ver': int(m.group(3)), 'file': m.group(4)})
            m = re.match(r'insert (\d): (.*?) uid ([0-9a-f]{8}) state (\d+) err (-?\d+).* cycles (\d+) max (\d+)', line)
            if m:
                st = int(m.group(4))
                out['slots'].append({'name': '' if m.group(2) == '-' else m.group(2), 'uid': m.group(3), 'state': st,
                                     'state_name': SLOT_STATES[st] if st < len(SLOT_STATES) else str(st), 'err': int(m.group(5)),
                                     'cycles': int(m.group(6)), 'max_cycles': int(m.group(7))})
        return out

    def fresh_boot(self, why):
        """A reset for scenarios whose screens depend on history: after the Performance page the pattern editor comes back
        without its cell text (also in the plain ui_shot front end), a Master page remembers its selected column. --no-reboot skips it."""
        if self.opts.get('no_reboot'):
            self.note('no reboot (%s)' % why)
            self.reset_ui()
            return
        self.note('reboot: ' + why)
        self.reboot()
        self.reset_ui()

    def markers_clean(self):
        m = self.tr.markers()
        return m.get('loading') == '0' and m.get('fault') == '0'

    def reboot(self, window_ms=4800):
        """Resets the unit, waits for the console and for the plugin host's 4 s key window to close (nothing loads inside it)."""
        self.tr.reboot()
        self.tr.t.drain(0.2)
        self.tr.wait(window_ms)
        self.settle(500)

    # ---- checkpoints
    def capture(self):
        """Settle, read the texts, read the 48 tile CRCs, settle again (the capture rebuilt the controls)."""
        self.settle()
        texts = self.texts()
        tiles = self.tr.shotcrc()
        self.settle()
        return texts, tiles

    def _save_png(self, key, tag):
        try:
            d = os.path.join(self.outdir, 'png')
            os.makedirs(d, exist_ok=True)
            p = os.path.join(d, '%s.%s.%s.png' % (self.name, key, tag))
            self.tr.shot(p)
            self.settle()
            return os.path.relpath(p, self.outdir)
        except UsbTestError:
            return None

    def checkpoint(self, key, must=(), tol=0, texts=True, tiles=True):
        """The current screen against its golden: normalised texts equal and at most `tol` of the 48 tiles different (the golden's
        own mask of unstable tiles aside). `must`: substrings that have to be on the screen whatever the golden says.
        No golden yet: the checkpoint is recorded (twice, to find what changes by itself)."""
        txt, til = self.capture()
        for m in must:
            self.check(any(m in t for t in txt), '%s: "%s" on screen' % (key, m))
        g = self.goldens.get(self.name, key)
        rec = {'key': key}
        if g is None or (self.goldens.update and (self.name, key) not in self.goldens.fresh and not self.goldens.stabilise):
            if not self.goldens.record and g is None:
                self.check(False, '%s: no golden and recording is off' % key)
                rec.update(status='nogolden')
            else:
                self.goldens.put(self.name, key, {'texts': txt, 'tiles': til, 'mask': [], 'volatile': []})
                rec.update(status='recorded', mask=[], volatile=[], digest=digest(txt, til))
                self._save_png(key, 'recorded')
        elif self.goldens.stabilise and (self.name, key) in self.goldens.fresh:
            # second pass over a golden recorded a moment ago: whatever differs by itself (clock, readouts, animation phase) is masked
            mask, vol = set(g['mask']), set(g['volatile'])
            for i in range(48):
                if til[i] != g['tiles'][i]:
                    mask.add(i)
            if len(txt) == len(g['texts']):
                for i in range(len(txt)):
                    if txt[i] != g['texts'][i]:
                        vol.add(i)
            else:
                g['texts_count_unstable'] = True
            g['mask'], g['volatile'] = sorted(mask), sorted(vol)
            self.goldens.dirty.add(self.name)
            rec.update(status='recorded', mask=g['mask'], volatile=g['volatile'],
                       digest=digest(g['texts'], [t for i, t in enumerate(g['tiles']) if i not in mask]))
            if mask:
                rec['note'] = 'unstable tiles masked: %s' % ' '.join(tile_name(i) for i in sorted(mask))
        else:
            mask, vol = set(g.get('mask', [])), set(g.get('volatile', []))
            bad = [i for i in range(48) if i not in mask and til[i] != g['tiles'][i]]
            tdiff = []
            if texts:
                if len(txt) != len(g['texts']) and not g.get('texts_count_unstable'):
                    tdiff.append('%d texts, golden has %d' % (len(txt), len(g['texts'])))
                for i in range(min(len(txt), len(g['texts']))):
                    if i not in vol and txt[i] != g['texts'][i]:
                        tdiff.append('#%d "%s" instead of "%s"' % (i, txt[i], g['texts'][i]))
            ok_tiles = (not tiles) or len(bad) <= tol
            ok_texts = (not texts) or not tdiff
            rec.update(status='pass' if (ok_tiles and ok_texts) else 'fail', bad_tiles=[tile_name(i) for i in bad], text_diff=tdiff[:6],
                       digest=digest(g['texts'] if ok_texts else txt, [t for i, t in enumerate(til) if i not in mask]))
            if ok_tiles and ok_texts:
                self.check(True, '%s: matches the golden' % key)
            else:
                rec['png'] = self._save_png(key, 'fail')
                why = []
                if not ok_tiles:
                    why.append('%d tiles differ (%s)' % (len(bad), ' '.join(tile_name(i) for i in bad[:8])))
                if not ok_texts:
                    why.append('texts differ: ' + ' / '.join(tdiff[:3]))
                self.check(False, '%s: %s' % (key, ', '.join(why)))
        self.checkpoints[key] = rec
        return rec['status']

    # ---- CPU
    def cpu_measure(self, name, hold_ms=2400, samples=3, gap_ms=600, budget=None):
        """cpu reset, run for hold_ms, then `samples` readings gap_ms apart. avg = median of the averages, max = the largest max seen
        (cumulative since the reset). Metrics name.avg_us and name.max_us. The block budget (2902 us) is a hard ceiling on a device,
        `budget` ({max_us, avg_us}) from the budget file is checked on both."""
        self.tr.command('cpu reset')
        self.tr.wait(hold_ms)
        avgs, maxs = [], []
        for i in range(samples):
            if i:
                self.tr.wait(gap_ms)
            c = self.tr.cpu()
            if c is None:
                self.tr.wait(200)
                c = self.tr.cpu()
            if c is None:
                self.check(False, '%s: no audio block measured' % name)
                return None
            avgs.append(c['avg'])
            maxs.append(c['max'])
        avg, mx = sorted(avgs)[len(avgs) // 2], max(maxs)
        self.metric(name + '.avg_us', avg, 'us')
        self.metric(name + '.max_us', mx, 'us', limit=BLOCK_US)
        if budget:
            self.check(mx <= budget['max_us'] and avg <= budget['avg_us'],
                       '%s: max %d us (budget %d), avg %d us (budget %d)' % (name, mx, budget['max_us'], avg, budget['avg_us']))
        return {'avg': avg, 'max': mx}
