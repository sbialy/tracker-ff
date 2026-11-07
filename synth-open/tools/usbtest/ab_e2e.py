#!/usr/bin/env python3
"""End to end check of ab_run.py on the Mac, no device: ui_shot --serve with the simulated bootloader (a stub that records the call and ends the
process like a reset), two synthetic .ptf images with two fake build ids, a staged SD card and two plugin sets (the second lacks two synth engines).
Host tool, NOT firmware.

  python3 -I ab_e2e.py UI_SHOT_BINARY WORKDIR [PLG_DIR]

Cases: a full ABAB run with restore, a run whose flavours differ (a scenario that passes on A and fails on B through a CPU budget, a fault record on
B), the plugin set mismatch that must abort BEFORE anything is measured, an update that does not return, the setup refusals, and the verify step
against a corrupted, a missing and an extra plugin file.
"""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, 'scenarios'))
import ptfgen  # noqa: E402
import suite   # noqa: E402
import usbtest  # noqa: E402

A_ID, B_ID = 'a1b2c3d', 'e4f5a6b'
A_NAME, B_NAME = 'PolyendTracker_AUTOT_1.2.0.ptf', 'PolyendTracker_AUTOTF_1.2.0.ptf'
DROPPED = ('phz', 'pluck')      # B's plugin set lacks these two synth engines (plg and plgi)
passed = failed = 0


def check(cond, msg):
    global passed, failed
    if cond:
        passed += 1
    else:
        failed += 1
        print('  FAIL: %s' % msg)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def flavour_dirs(work, plg):
    """work/flavA and work/flavB: a .ptf and the plugin files each."""
    a, b = os.path.join(work, 'flavA'), os.path.join(work, 'flavB')
    for d in (a, b):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
    write(os.path.join(a, A_NAME), ptfgen.build(build_id=A_ID))
    write(os.path.join(b, B_NAME), ptfgen.build(build_id=B_ID))
    for f in sorted(os.listdir(plg)):
        p = os.path.join(plg, f)
        if os.path.isfile(p) and f.endswith(('.plg', '.plgi')):
            shutil.copy(p, a)
            if f.rsplit('.', 1)[0] not in DROPPED:
                shutil.copy(p, b)
    return a, b


def card_set(sd):
    """{relative path: bytes} of the plugin files on the card."""
    out = {}
    for d in ('Master', 'Synth'):
        root = os.path.join(sd, 'Plugins', d)
        if os.path.isdir(root):
            for f in sorted(os.listdir(root)):
                if f.endswith(('.plg', '.plgi')):
                    out[d + '/' + f] = open(os.path.join(root, f), 'rb').read()
    return out


def flavour_set(folder, ab):
    sys.path.insert(0, HERE)
    import ab_run
    return {p.card[len('/Plugins/'):]: p.data for p in ab_run.load_plugins(folder)}


class Run:
    def __init__(self, ui, work, name, plg):
        self.ui, self.name = ui, name
        self.dir = os.path.join(work, name)
        shutil.rmtree(self.dir, ignore_errors=True)
        os.makedirs(self.dir)
        self.out = os.path.join(self.dir, 'out')
        self.log = os.path.join(self.dir, 'bootloader.log')
        print('== %s' % name)
        stage = os.path.join(self.dir, 'stage')
        os.makedirs(stage)
        self.sd, _ = suite.stage_sd(stage, ui, plg, True, 'Demo')    # plugin set A on the card, the first power up wrote /Workspace
        self.flav_a, self.flav_b = flavour_dirs(self.dir, plg)

    def ab(self, *args, env=None):
        e = dict(os.environ, UISHOT_BOOTLOADER_LOG=self.log, UISHOT_FAKE_CPU_BUILD='%s=300,900 %s=270,800' % (A_ID, B_ID), UISHOT_FAKE_CPU_JITTER='12')
        e.update(env or {})
        cmd = ['python3', '-I', os.path.join(HERE, 'ab_run.py'), '--sim', self.ui, '--sd', self.sd, '--a', self.flav_a, '--b', self.flav_b,
               '--a-label', 'AUTO-T', '--b-label', 'AUTO-TF', '--out', self.out] + list(args)
        r = subprocess.run(cmd, capture_output=True, text=True, env=e, timeout=3000)
        self.stdout = r.stdout + r.stderr
        with open(os.path.join(self.dir, 'stdout.txt'), 'w') as f:
            f.write(self.stdout)
        return r.returncode

    def report(self):
        with open(os.path.join(self.out, 'ab_report.json')) as f:
            return json.load(f)

    def md(self):
        with open(os.path.join(self.out, 'ab_report.md')) as f:
            return f.read()

    def calls(self):
        return open(self.log).read().splitlines() if os.path.exists(self.log) else []

    def flash_state(self):
        p = os.path.join(self.sd, '.uishot_flash')
        return open(p).read() if os.path.exists(p) else None


def main(ui, work, plg):
    os.makedirs(work, exist_ok=True)
    if plg is None:
        plg = suite.find_plg_dir(None)
    if plg is None:
        print('no plugin folder (PLG_DIR or build/cpu-adopt of the main checkout)')
        return 2

    # ------------------------------------------------------------------------------------------------ setup refusals (nothing is touched)
    r = Run(ui, work, 'setup-refusals', plg)
    write(os.path.join(r.flav_b, B_NAME), ptfgen.build(build_id=A_ID))
    rc = r.ab('--check-only')
    check(rc == 2 and 'both .ptf files carry build id %s' % A_ID in r.stdout, 'same build id: rc %d\n%s' % (rc, r.stdout))
    write(os.path.join(r.flav_b, B_NAME), ptfgen.build(build_id=B_ID))
    shutil.rmtree(r.flav_b)
    shutil.copytree(r.flav_a, r.flav_b)
    os.rename(os.path.join(r.flav_b, A_NAME), os.path.join(r.flav_b, B_NAME))
    write(os.path.join(r.flav_b, B_NAME), ptfgen.build(build_id=B_ID))
    rc = r.ab('--check-only')
    check(rc == 2 and 'byte identical' in r.stdout, 'identical plugin sets: rc %d\n%s' % (rc, r.stdout))
    rc = r.ab('--check-only', '--allow-same-plugins')
    check(rc == 0 and '0 files differ' in r.stdout, 'identical plugin sets allowed: rc %d\n%s' % (rc, r.stdout))
    r.flav_a, r.flav_b = flavour_dirs(r.dir, plg)
    write(os.path.join(r.flav_b, 'PolyendTracker_NOTAPTF_1.2.0.ptf'), b'nothing')
    os.remove(os.path.join(r.flav_b, B_NAME))
    rc = r.ab('--check-only')
    check(rc == 2 and 'not a flashable MK66 image' in r.stdout, 'bad ptf: rc %d\n%s' % (rc, r.stdout))
    check(r.calls() == [] and r.flash_state() is None, 'something was touched by a refused setup')
    r.flav_a, r.flav_b = flavour_dirs(r.dir, plg)
    rc = r.ab('--check-only')
    check(rc == 0 and ('build %s' % A_ID) in r.stdout and ('build %s' % B_ID) in r.stdout and 'files differ' in r.stdout, 'check-only: rc %d\n%s' % (rc, r.stdout))
    check(len(A_NAME) <= 31 and len(B_NAME) <= 31 and A_NAME != B_NAME, 'the two .ptf names are distinct and within 31 characters')

    # ------------------------------------------------------------------------------------------------ ABAB, restore A
    r = Run(ui, work, 'abab', plg)
    rc = r.ab('--pairs', '2', '--sweep-repeats', '2', '--voices', '1,4', '--suite', 'first', '--suite-args', '--only pages.walk,plugins.*,engines.cpu', '--restore', 'a')
    check(rc == 0, 'ABAB run: exit %d\n%s' % (rc, r.stdout[-3000:]))
    rep = r.report()
    check(rep['result'] == 'CLEAN' and not rep.get('abort'), 'result %s %s' % (rep['result'], rep.get('abort')))
    check(r.calls() == ['runBootloader ' + n for n in (A_NAME, B_NAME, A_NAME, B_NAME, A_NAME)], 'bootloader calls %r (4 phases and the restore)' % r.calls())
    check(r.flash_state() == A_ID, 'the unit ends on flavour A (%s), flash says %s' % (A_ID, r.flash_state()))
    check(card_set(r.sd) == flavour_set(r.flav_a, 'a'), 'the card ends with exactly the plugin set of A')
    check([p['flavour'] for p in rep['phases']] == ['A', 'B', 'A', 'B'], 'phases %r' % [p['flavour'] for p in rep['phases']])
    check(all(p['verified'].startswith('yes') for p in rep['phases']), 'every phase verified before measuring')
    check('Restored flavour A' in (rep['restore'] or ''), 'restore: %s' % rep['restore'])
    engines = [c['engine'] for c in rep['cpu']]
    check({'ACD', 'FAT', 'VAP', 'WTFM', 'PERC', 'CHIP'} <= set(engines), 'engines in the CPU table: %s' % engines)
    rows = [c for c in rep['cpu'] if c['engine'] == 'ACD']
    check(len(rows) == 2 and all(c['A']['n'] == 4 and c['B']['n'] == 4 for c in rows), 'ACD: 2 repeats x 2 phases per flavour at 1 and 4 voices: %r' % rows)
    check(all(c['verdict'] == 'B cheaper' and c['delta_us'] < 0 for c in rows), 'B is clearly cheaper than A for ACD: %r' % [(c['delta_us'], c['verdict']) for c in rows])
    check(all(len(c['pair_deltas']) == 2 for c in rows), 'pair deltas for 2 pairs')
    check(all(c['A']['spread'] > 0 for c in rows), 'the repeats have a spread (the jitter): %r' % [c['A'] for c in rows])
    check(len(rep['suites']) == 2 and all(s['phase'] < 2 for s in rep['suites']), 'the suite ran in the first pair only')
    check(all(x['verdict'] == 'same' for x in rep['scenarios']), 'no scenario differs: %r' % [x for x in rep['scenarios'] if x['verdict'] != 'same'])
    check(not rep['crashes'], 'no crash markers: %r' % rep['crashes'])
    md = r.md()
    for want in ('# A/B run: AUTO-T against AUTO-TF', '## CPU per engine', '| ACD | 4 |', '## Scenarios', '## Crash markers', 'Restored flavour A'):
        check(want in md, 'the markdown lacks %r' % want)

    # ------------------------------------------------------------------------------------------------ a scenario that differs, a crash marker
    r = Run(ui, work, 'findings', plg)
    budgets = os.path.join(r.dir, 'budgets.json')
    with open(budgets, 'w') as f:
        budget = {'max_us': 1500, 'avg_us': 700}
        json.dump({k: budget for k in ['cpu.idle'] + ['engine.%s.v%d' % (e, v) for e in ('ACD', 'FAT', 'VAP', 'WTFM', 'PERC', 'CHIP') for v in (1, 4)]}, f)
    rc = r.ab('--sweep-repeats', '1', '--suite', 'all', '--suite-args', '--only engines.cpu --budgets %s' % budgets, '--restore', 'b',
              env={'UISHOT_FAKE_CPU_BUILD': '%s=300,900 %s=270,2000' % (A_ID, B_ID), 'UISHOT_FAKE_FAULT_BUILD': B_ID})
    check(rc == 1, 'findings run: exit %d\n%s' % (rc, r.stdout[-2500:]))
    rep = r.report()
    row = [x for x in rep['scenarios'] if x['name'] == 'engines.cpu']
    check(len(row) == 1 and row[0]['verdict'] == 'DIFFERENT' and row[0]['A'] == ['PASS'] and row[0]['B'] == ['FAIL'], 'engines.cpu passes on A, fails on B: %r' % row)
    check(rep['crashes'] and all(c['flavour'] == 'B' for c in rep['crashes']) and 'fault record' in rep['crashes'][0]['text'],
          'crash markers on B only: %r' % rep['crashes'])
    check(rep['result'].startswith('FINDINGS'), rep['result'])
    check('**DIFFERENT**' in r.md() and 'fault record pc=0001a2b4' in r.md(), 'the markdown names the difference and the fault record')
    check(r.flash_state() == B_ID and 'verified again' in (rep['restore'] or ''), 'restore b after the last phase B only verifies: %s' % rep['restore'])
    check(len(r.calls()) == 2, 'two updates, no restore flash: %r' % r.calls())

    # ------------------------------------------------------------------------------------------------ plugin set mismatch: abort before measuring
    r = Run(ui, work, 'mismatch', plg)
    rc = r.ab('--sweep-repeats', '2', '--suite', 'all', '--suite-args', '--only pages.walk', '--restore', 'a', '--test-skip-swap', 'B')
    check(rc == 3, 'mismatch run: exit %d (3 = aborted)\n%s' % (rc, r.stdout[-2500:]))
    rep = r.report()
    check(rep['result'] == 'ABORTED' and rep['abort'] and rep['abort']['kind'] == 'mismatch', 'aborted as a mismatch: %r' % rep.get('abort'))
    check('phz' in rep['abort']['reason'] or 'pluck' in rep['abort']['reason'], 'the reason names the extra plugin files: %s' % rep['abort']['reason'])
    check(rep['phases'][1]['flavour'] == 'B' and rep['phases'][1]['verified'].startswith('NO'), 'phase B is marked not verified: %r' % rep['phases'][1])
    check(all(m['flavour'] == 'A' for m in rep['measurements']) and rep['measurements'], 'measurements exist for A only: %r' % {m['flavour'] for m in rep['measurements']})
    check(all(s['flavour'] == 'A' for s in rep['suites']), 'no suite result for B')
    check(not os.path.exists(os.path.join(r.out, 'phase01_B_suite')), 'the suite never started for B')
    check(rep['phases'][1].get('suite') in (None, '-') and rep['phases'][1].get('sweep') in (None, '-'), 'phase B has no suite and no sweep entry: %r' % rep['phases'][1])
    check('ABORTED' in r.md(), 'the markdown says ABORTED')
    check(r.calls() == ['runBootloader ' + n for n in (A_NAME, B_NAME, A_NAME)], 'A, B, then the restore of A: %r' % r.calls())
    check(r.flash_state() == A_ID and card_set(r.sd) == flavour_set(r.flav_a, 'a'), 'restored: the unit runs A and the card holds the set of A')
    check('Restored flavour A' in (rep['restore'] or ''), 'restore after the abort: %s' % rep['restore'])

    # ------------------------------------------------------------------------------------------------ the update does not come back
    r = Run(ui, work, 'stuck', plg)
    rc = r.ab('--update-timeout', '5', '--sweep-repeats', '1', env={'UISHOT_BOOTLOADER_STUCK': '1'})
    rep = r.report()
    check(rc == 3 and rep['abort'] and rep['abort']['kind'] == 'update' and 'Recovery:' in (rep['restore'] or ''), 'stuck bootloader: rc %d %r' % (rc, rep.get('abort')))
    check(not rep['measurements'] and r.calls() == ['runBootloader ' + A_NAME], 'nothing measured, one hand-off: %r' % r.calls())

    # ------------------------------------------------------------------------------------------------ verify against a damaged card
    r = Run(ui, work, 'verify-card', plg)
    import ab_run
    fl = ab_run.make_flavour('A', r.flav_a, None, None, 'AUTO-T')
    sim = usbtest.SimTransport(ui, r.sd)
    try:
        tr = usbtest.Tracker(sim, heartbeat=0)
        tr.ping()
        fl.build = 'uishot'     # the unit that has not been updated yet reports this build
        check(ab_run.verify_flavour(tr, fl, 't') == [], 'the staged card verifies: %r' % ab_run.verify_flavour(tr, fl, 't'))
        victim = os.path.join(r.sd, 'Plugins', 'Synth', 'arc.plg')
        data = bytearray(open(victim, 'rb').read())
        data[100] ^= 0x55
        open(victim, 'wb').write(bytes(data))
        p = ab_run.verify_flavour(tr, fl, 't')
        check(len(p) == 1 and 'arc.plg' in p[0] and 'crc' in p[0], 'a flipped byte is found: %r' % p)
        open(victim, 'wb').write(bytes(data[:100]) + bytes([data[100] ^ 0x55]) + bytes(data[101:]))
        os.remove(os.path.join(r.sd, 'Plugins', 'Master', 'dirt.plg'))
        write(os.path.join(r.sd, 'Plugins', 'Synth', 'foreign.plg'), b'x' * 64)
        p = ab_run.verify_flavour(tr, fl, 't')
        check(len(p) == 2 and any('dirt.plg is missing' in x for x in p) and any('foreign.plg' in x and 'not in the plugin set' in x for x in p), 'missing and extra file: %r' % p)
        fl.build = 'ffffff0'
        p = ab_run.verify_flavour(tr, fl, 't')
        check(any('reports build uishot' in x for x in p), 'the wrong build id is found: %r' % p)
    finally:
        sim.close()

    print('ab_e2e: %d checks, %d failed' % (passed + failed, failed))
    return 1 if failed else 0


if __name__ == '__main__':
    if len(sys.argv) not in (3, 4):
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None))
