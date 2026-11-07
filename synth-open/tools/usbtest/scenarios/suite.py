#!/usr/bin/env python3
"""Scenario suite for the USB test console: runs the scenario library, writes a pass/fail report (markdown + JSON) and diffs it
against a saved baseline. Host tool, NOT firmware.

  Mac (ui_shot --serve, the SD card is staged for you):
    python3 -I suite.py --sim build/ui-shot/ui_shot --out build/usbtest-suite
  Device (first run records the goldens and the baseline):
    python3 -I suite.py --port auto --capture --device-writes --out build/usbtest-device
  Later device runs compare against what the first run recorded:
    python3 -I suite.py --port auto --device-writes --out build/usbtest-device2

  --list shows the scenarios, --only 'inserts.*,pages.walk' / --tag cpu pick some.
  --save-baseline FILE writes this run as the baseline, --baseline FILE (default: baselines/<target>.json when it exists) is the diff base.
Exit status 0: no scenario failed, no regression against the baseline.
"""
import argparse
import fnmatch
import glob
import hashlib
import importlib
import json
import os
import shutil
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))
import lib  # noqa: E402
import usbtest  # noqa: E402
from lib import Abort, Ctx, Goldens, Skip  # noqa: E402
from usbtest import Disconnected, SerialTransport, SimTransport, Timeout, Tracker, UsbTestError  # noqa: E402

MASTER_PLUGINS = 'dirt bbd tape lofi console tube phaser'.split()
STATUS_ORDER = {'PASS': 0, 'RECORDED': 0, 'SKIP': 1, 'NOT RUN': 2, 'FAIL': 3, 'ERROR': 4}


# ------------------------------------------------------------------------------------------------ staging (Mac)
def find_plg_dir(arg):
    home = os.path.expanduser('~')
    for d in [arg, os.environ.get('PLG_DIR'), home + '/prj/poly/build/cpu-adopt', home + '/prj/poly/build/plugin-synth']:
        if d and os.path.isfile(os.path.join(d, 'dirt.plg')):
            return d
    return None


def stage_sd(out, ui, plg_dir, with_synth, project):
    """A fresh SD card folder for ui_shot: Master plugins, optionally the synth plugins, and a project folder (a copy of the
    Workspace the first power up writes) for the project scenarios."""
    sd = os.path.join(out, 'sd')
    shutil.rmtree(sd, ignore_errors=True)
    staged = {}
    os.makedirs(sd + '/Plugins/Master')
    os.makedirs(sd + '/Plugins/Synth')
    if plg_dir:
        for n in MASTER_PLUGINS:
            if os.path.exists('%s/%s.plg' % (plg_dir, n)):
                shutil.copy('%s/%s.plg' % (plg_dir, n), sd + '/Plugins/Master/')
                staged['Master/%s.plg' % n] = file_sha('%s/%s.plg' % (plg_dir, n))
        if with_synth:
            for f in sorted(glob.glob(plg_dir + '/*.plg') + glob.glob(plg_dir + '/*.plgi')):
                if os.path.basename(f)[:-4] not in MASTER_PLUGINS and os.path.basename(f)[:-5] not in MASTER_PLUGINS:
                    shutil.copy(f, sd + '/Plugins/Synth/')
                    staged['Synth/' + os.path.basename(f)] = file_sha(f)
    sim = SimTransport(ui, sd)   # the first power up writes /Workspace
    try:
        Tracker(sim, heartbeat=0).ping()
    finally:
        sim.close()
    if project and os.path.isdir(sd + '/Workspace'):
        dest = os.path.join(sd, 'Projects', project)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copytree(sd + '/Workspace', dest)
    return sd, staged


def file_sha(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()[:12]


def check_stage(gdir, staged, record):
    """The Mac goldens depend on the plugin files on the staged card (list, logos, engine pages). The set they were recorded with is kept
    in goldens/mac/_stage.json. Returns a warning text when this run staged another set."""
    path = os.path.join(gdir, '_stage.json')
    digest = hashlib.sha256(json.dumps(staged, sort_keys=True).encode()).hexdigest()[:12]
    if os.path.exists(path):
        with open(path) as f:
            old = json.load(f)
        if old.get('digest') != digest:
            diff = sorted(k for k in set(old.get('files', {})) | set(staged) if old.get('files', {}).get(k) != staged.get(k))
            return 'the staged plugin files differ from the set the goldens were recorded with (%d files: %s): expect screen differences, re-record with --update-goldens' % (len(diff), ', '.join(diff[:6]))
        return None
    if record:
        os.makedirs(gdir, exist_ok=True)
        with open(path, 'w') as f:
            json.dump({'digest': digest, 'files': staged}, f, indent=1, sort_keys=True)
            f.write('\n')
    return None


# ------------------------------------------------------------------------------------------------ running
def load_scenarios():
    for f in sorted(glob.glob(os.path.join(HERE, 'scn_*.py'))):
        importlib.import_module(os.path.basename(f)[:-3])
    return lib.REGISTRY


def pick(registry, only, tags):
    out = []
    for name, fn, meta in registry:
        if only and not any(fnmatch.fnmatch(name, p) for p in only):
            continue
        if tags and not set(tags) & set(meta['tags']):
            continue
        out.append((name, fn, meta))
    return out


def recover(tr, say):
    """After a lost link: reconnect and ping. False when the unit does not come back."""
    try:
        tr.t.reconnect()
        tr.t.drain(0.2)
        tr.ping(5.0)
        say('      reconnected')
        return True
    except (UsbTestError, OSError) as e:
        say('      no reconnect: %s' % e)
        return False


def attempt(name, fn, tr, target, outdir, goldens, opts):
    """One execution of a scenario. Returns (ctx, status, error, skip kind, link lost)."""
    ctx = Ctx(tr, target, outdir, goldens, name, opts)
    status, err, kind, lost = 'PASS', None, None, False
    try:
        fn(ctx)
        if ctx.soft_failures:
            status = 'FAIL'
        elif any(c['status'] == 'recorded' for c in ctx.checkpoints.values()):
            status = 'RECORDED'
    except Skip as s:
        status, err, kind = 'SKIP', s.reason, s.kind
    except Abort as a:
        status, err = 'FAIL', 'stopped: %s' % a
    except (Disconnected, Timeout) as e:
        status, err, lost = 'ERROR', '%s: %s' % (type(e).__name__, e), True
    except UsbTestError as e:
        status, err = 'ERROR', str(e)
    except Exception as e:  # noqa: BLE001
        status, err = 'ERROR', '%s: %s\n%s' % (type(e).__name__, e, traceback.format_exc(limit=6))
    if status == 'PASS' and ctx.skips and not ctx.checkpoints and not any(s['kind'] == 'check' for s in ctx.steps):
        status, kind = 'SKIP', 'device-only'
    try:
        ctx.release_all()
    except UsbTestError:
        lost = True
    return ctx, status, err, kind, lost


def run_one(name, fn, meta, tr, target, outdir, goldens, opts, say):
    t0 = time.monotonic()
    skip_reason = None
    if not meta['sim'] and target == 'mac':
        skip_reason = ('device-only', meta['summary'])
    elif meta['writes'] and target == 'device' and not opts.get('device_writes'):
        skip_reason = ('writes', 'writes files on the card, run with --device-writes')
    if skip_reason:
        ctx = Ctx(tr, target, outdir, goldens, name, opts)
        status, err, kind, lost = 'SKIP', skip_reason[1], skip_reason[0], False
    else:
        ctx, status, err, kind, lost = attempt(name, fn, tr, target, outdir, goldens, opts)
        fresh = [k for k in goldens.fresh if k[0] == name]
        if status == 'RECORDED' and fresh and not lost:
            # the goldens were just recorded: run it once more over them, what differs by itself (clock, readouts) becomes masked
            say('   recorded, second pass to find what changes by itself')
            goldens.stabilise = True
            try:
                ctx2, status2, err2, kind2, lost = attempt(name, fn, tr, target, outdir, goldens, opts)
            finally:
                goldens.stabilise = False
            ctx.steps.append({'kind': 'note', 'text': 'second pass (stabilise): %s, %d checks, %d failed' % (status2, len([s for s in ctx2.steps if s['kind'] == 'check']), ctx2.soft_failures)})
            if status2 in ('RECORDED', 'PASS') and not ctx2.soft_failures:
                ctx = ctx2   # its digests are the ones a later run compares with
                ctx.steps.insert(0, {'kind': 'note', 'text': 'goldens recorded in the first pass, volatile parts masked in the second'})
            else:
                status, err = 'ERROR' if status2 == 'ERROR' else 'FAIL', err2 or 'the scenario did not repeat cleanly: %s' % status2
                ctx = ctx2
        if status in ('PASS', 'RECORDED'):
            goldens.flush(name)
        else:
            if name in goldens.dirty:
                ctx.steps.append({'kind': 'note', 'text': 'goldens of this scenario were NOT written (the scenario did not pass)'})
            goldens.discard(name)
    res = {
        'name': name, 'status': status, 'summary': meta['summary'], 'tags': meta['tags'], 'seconds': round(time.monotonic() - t0, 1),
        'error': err, 'skip_kind': kind, 'skips': ctx.skips, 'metrics': ctx.metrics, 'checkpoints': ctx.checkpoints,
        'checks': sum(1 for s in ctx.steps if s['kind'] == 'check'), 'checks_failed': ctx.soft_failures,
        'steps': ctx.steps,
    }
    return res, lost


# ------------------------------------------------------------------------------------------------ baseline
def baseline_of(results, target, build):
    return {
        'version': 1, 'target': target, 'build': build, 'date': time.strftime('%Y-%m-%d %H:%M:%S'),
        'scenarios': {r['name']: {
            'status': r['status'],
            'digests': {k: c.get('digest') for k, c in r['checkpoints'].items() if c.get('digest')},
            'metrics': {k: m['value'] for k, m in r['metrics'].items()},
            'units': {k: m['unit'] for k, m in r['metrics'].items()},
        } for r in results if r['status'] != 'NOT RUN'},
    }


def diff_baseline(base, cur, target, cpu_tol, time_tol):
    """Regressions: a scenario that got worse, a checkpoint that went from pass to anything else, a CPU or timing metric over its
    tolerance. Changes: everything else that differs (new and removed scenarios, checkpoint digests, other metrics)."""
    out = {'regressions': [], 'improvements': [], 'changes': [], 'base': {'build': base.get('build'), 'date': base.get('date'), 'target': base.get('target')}}
    bs, cs = base.get('scenarios', {}), cur.get('scenarios', {})
    for name in sorted(set(bs) | set(cs)):
        b, c = bs.get(name), cs.get(name)
        if b is None:
            out['changes'].append('%s: new scenario (%s)' % (name, c['status']))
            continue
        if c is None:
            out['regressions'].append('%s: in the baseline but not run' % name)
            continue
        if STATUS_ORDER[c['status']] > STATUS_ORDER[b['status']]:
            out['regressions'].append('%s: %s -> %s' % (name, b['status'], c['status']))
        elif STATUS_ORDER[c['status']] < STATUS_ORDER[b['status']]:
            out['improvements'].append('%s: %s -> %s' % (name, b['status'], c['status']))
        for k, d in c['digests'].items():
            if k in b['digests'] and b['digests'][k] != d:
                out['changes'].append('%s / %s: screen digest %s -> %s (a golden was updated or the screen changed)' % (name, k, b['digests'][k], d))
        for k, v in c['metrics'].items():
            u = c['units'].get(k, '')
            if k not in b['metrics']:
                out['changes'].append('%s / %s: new metric %s %s' % (name, k, v, u))
                continue
            bv = b['metrics'][k]
            if v == bv:
                continue
            if k.endswith('_us'):
                if v > bv * (1 + cpu_tol) and v - bv > 50:
                    out['regressions'].append('%s / %s: %s -> %s us (+%.1f %%, tolerance %d %%)' % (name, k, bv, v, 100.0 * (v - bv) / max(bv, 1), cpu_tol * 100))
                elif v < bv * (1 - cpu_tol) and bv - v > 50:
                    out['improvements'].append('%s / %s: %s -> %s us' % (name, k, bv, v))
            elif u in ('s', 'ms'):
                if target == 'mac':
                    continue   # wall clock of the Mac process: no information
                floor = 0.5 if u == 's' else 500
                if v > bv * (1 + time_tol) and v - bv > floor:
                    out['regressions'].append('%s / %s: %s -> %s %s (+%.0f %%)' % (name, k, bv, v, u, 100.0 * (v - bv) / max(bv, 1e-9)))
            else:
                out['changes'].append('%s / %s: %s -> %s %s' % (name, k, bv, v, u))
        for k in b['metrics']:
            if k not in c['metrics'] and c['status'] not in ('SKIP', 'ERROR'):
                out['changes'].append('%s / %s: metric no longer produced' % (name, k))
    return out


# ------------------------------------------------------------------------------------------------ reports
def esc(s):
    return str(s).replace('|', '\\|').replace('\n', ' ')


def write_reports(path_base, meta, results, diff, base_metrics):
    ok = all(r['status'] in ('PASS', 'RECORDED', 'SKIP') for r in results) and not (diff and diff['regressions'])
    doc = {'ok': ok, 'meta': meta, 'results': results, 'diff': diff}
    with open(path_base + '.json', 'w') as f:
        json.dump(doc, f, indent=1)
    counts = {}
    for r in results:
        counts[r['status']] = counts.get(r['status'], 0) + 1
    L = ['# USB test suite report', '',
         '- result: **%s**' % ('PASS' if ok else 'FAIL'),
         '- target: %s, firmware %s, %s' % (meta['target'], meta.get('build', '?'), meta['date']),
         '- duration: %s s, goldens: `%s`' % (meta['seconds'], meta['goldens']),
         '- baseline: %s' % (('`%s` (%s, build %s)' % (meta['baseline'], diff['base']['date'], diff['base']['build'])) if diff else 'none'),
         '- plugins on the card: %s' % (meta.get('plugins') or 'unknown'),
         ('- **WARNING**: %s' % meta['warning']) if meta.get('warning') else '- plugin files: as recorded',
         '- ' + ', '.join('%d %s' % (n, s) for s, n in sorted(counts.items(), key=lambda kv: STATUS_ORDER[kv[0]])), '',
         '## Scenarios', '', '| Scenario | Status | Checks | Checkpoints | Seconds | Note |', '| :--- | :--- | :--- | :--- | ---: | :--- |']
    for r in results:
        cp = r['checkpoints']
        cps = '%d ok, %d recorded, %d failed' % (sum(1 for c in cp.values() if c['status'] == 'pass'), sum(1 for c in cp.values() if c['status'] == 'recorded'),
                                                 sum(1 for c in cp.values() if c['status'] not in ('pass', 'recorded'))) if cp else '-'
        note = r['error'] or (' / '.join(r['skips'][:2]) if r['skips'] else '')
        L.append('| %s | %s | %d/%d | %s | %s | %s |' % (r['name'], r['status'], r['checks'] - r['checks_failed'], r['checks'], cps, r['seconds'], esc(note)[:160]))
    fails = [r for r in results if r['status'] in ('FAIL', 'ERROR')]
    if fails:
        L += ['', '## Failures', '']
        for r in fails:
            L += ['### %s (%s)' % (r['name'], r['status']), '']
            if r['error']:
                L.append('- error: ' + esc(r['error'])[:400])
            for s in r['steps']:
                if s['kind'] == 'check' and not s['ok']:
                    L.append('- FAILED: ' + esc(s['text']))
            for k, c in r['checkpoints'].items():
                if c['status'] == 'fail':
                    L.append('- checkpoint `%s`: tiles %s, texts %s%s' % (k, ' '.join(c.get('bad_tiles') or []) or '-', esc(' / '.join(c.get('text_diff') or [])) or '-',
                                                                         (', image `%s`' % c['png']) if c.get('png') else ''))
            L.append('')
    skips = [r for r in results if r['skips'] or r['status'] == 'SKIP']
    if skips:
        L += ['', '## Not run here (device only, manual or missing precondition)', '']
        for r in skips:
            if r['status'] == 'SKIP':
                L.append('- **%s**: %s (%s)' % (r['name'], esc(r['error'] or ''), r['skip_kind']))
            for s in r['skips']:
                L.append('- %s: %s' % (r['name'], esc(s)))
    L += ['', '## Metrics', '', '| Scenario | Metric | Value | Baseline |', '| :--- | :--- | ---: | ---: |']
    for r in results:
        for k, m in r['metrics'].items():
            L.append('| %s | %s | %s %s | %s |' % (r['name'], k, m['value'], m['unit'], base_metrics.get(r['name'], {}).get(k, '-')))
    if diff:
        L += ['', '## Regression diff against the baseline', '']
        for title, key in (('Regressions', 'regressions'), ('Improvements', 'improvements'), ('Other changes', 'changes')):
            L.append('**%s: %d**' % (title, len(diff[key])))
            L += ['- ' + esc(x) for x in diff[key][:60]]
            L.append('')
    with open(path_base + '.md', 'w') as f:
        f.write('\n'.join(L) + '\n')
    return ok


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--sim', help='path of the ui_shot binary: run against the Mac build (the SD card is staged under --out)')
    ap.add_argument('--port', help='serial port or "auto": run against the device')
    ap.add_argument('--sd', help='sim: use this SD folder as it is (no staging)')
    ap.add_argument('--plg-dir', help='sim: folder with the .plg files (default: build/cpu-adopt of the main checkout)')
    ap.add_argument('--no-synth-plugins', action='store_true', help='sim: do not stage the synth engine plugins (only the 6 built-in engines are then in the Model column)')
    ap.add_argument('--out', default='suite-out')
    ap.add_argument('--name', default='suite', help='basename of the report files')
    ap.add_argument('--only', help='comma separated scenario names or globs')
    ap.add_argument('--tag', action='append', default=[])
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--goldens', help='folder for the goldens (default: goldens/<mac|device> next to this file)')
    ap.add_argument('--no-record', action='store_true', help='a checkpoint without a golden fails instead of being recorded')
    ap.add_argument('--update-goldens', action='store_true', help='record every checkpoint again, overwriting the goldens')
    ap.add_argument('--capture', action='store_true', help='golden capture mode: record missing goldens and, when there is none, the baseline')
    ap.add_argument('--baseline', help='baseline file to diff against (default baselines/<target>.json if it exists)')
    ap.add_argument('--no-baseline', action='store_true')
    ap.add_argument('--save-baseline', nargs='?', const='auto', help='write this run as a baseline (default file baselines/<target>.json)')
    ap.add_argument('--cpu-tol', type=float, default=0.10, help='CPU regression tolerance (fraction, default 0.10)')
    ap.add_argument('--time-tol', type=float, default=0.25, help='timing regression tolerance (device only)')
    ap.add_argument('--budgets', help='JSON {"engine.CHIP.v4": {"max_us": N, "avg_us": N}}, hard CPU budgets per metric prefix')
    ap.add_argument('--project', help='project folder for the project scenarios, e.g. Projects/Demo')
    ap.add_argument('--device-writes', action='store_true', help='allow scenarios that write files on the card of a device')
    ap.add_argument('--no-reboot', action='store_true', help='do not reset the unit between scenarios that want a clean state (screens may then depend on history)')
    ap.add_argument('--interactive', action='store_true', help='device: stop for the steps that need a hand')
    args = ap.parse_args(argv)

    registry = load_scenarios()
    chosen = pick(registry, [p for p in (args.only or '').split(',') if p], args.tag)
    if args.list:
        for name, fn, meta in chosen:
            print('%-26s %-22s %s%s%s' % (name, ','.join(meta['tags']), meta['summary'], ' [writes]' if meta['writes'] else '', ' [device only]' if not meta['sim'] else ''))
        return 0
    if bool(args.sim) == bool(args.port):
        ap.error('give exactly one of --sim and --port')
    target = 'mac' if args.sim else 'device'
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    say = lambda m: print(m, flush=True)  # noqa: E731

    stage_warning = None
    gdir = os.path.abspath(args.goldens) if args.goldens else os.path.join(HERE, 'goldens', 'mac' if args.sim else 'device')
    if args.sim:
        ui = os.path.abspath(args.sim)
        if args.sd:
            sd = os.path.abspath(args.sd)
        else:
            plg = find_plg_dir(args.plg_dir)
            if plg is None:
                ap.error('no .plg files found: pass --plg-dir DIR (a folder with dirt.plg bbd.plg ...)')
            say('staging the SD card from %s' % plg)
            sd, staged = stage_sd(out, ui, plg, not args.no_synth_plugins, 'Demo')
            if args.update_goldens and os.path.exists(os.path.join(gdir, '_stage.json')):
                os.remove(os.path.join(gdir, '_stage.json'))
            stage_warning = check_stage(gdir, staged, not args.no_record)
            if stage_warning:
                say('WARNING: ' + stage_warning)
        transport = SimTransport(ui, sd)
    else:
        port = args.port if args.port != 'auto' else SerialTransport.find_port()
        transport = SerialTransport(port)
    tr = Tracker(transport, heartbeat=1.0 if args.port else 0)
    transport.drain(0.2)
    try:
        info = tr.ping(5.0)
    except UsbTestError as e:
        print('no answer from the console: %s\nIs the unit running a test image (ENABLE_DEV_CONSOLE), is /.develop on the card, is a terminal allowed to hold the port (DTR)?' % e, file=sys.stderr)
        return 2
    build = '%s %s proto=%d' % (info['product'], info['build'], info['proto'])
    say('connected: %s' % build)

    goldens = Goldens(gdir, record=not args.no_record, update=args.update_goldens)
    opts = {'project': args.project, 'device_writes': args.device_writes, 'interactive': args.interactive, 'no_reboot': args.no_reboot, 'budgets': {}}
    if args.budgets:
        with open(args.budgets) as f:
            opts['budgets'] = {k: v for k, v in json.load(f).items() if not k.startswith('_')}

    t0 = time.monotonic()
    results, alive = [], True
    plugins = ''
    try:
        h = lib.Ctx(tr, target, out, goldens, 'preflight', opts)
        try:
            plugins = ', '.join(p['name'] for p in h.host_txt()['plugins'])
        except UsbTestError:
            plugins = ''
    except UsbTestError:
        pass
    for name, fn, meta in chosen:
        if not alive:
            results.append({'name': name, 'status': 'NOT RUN', 'summary': meta['summary'], 'tags': meta['tags'], 'seconds': 0, 'error': 'the unit was lost earlier',
                            'skip_kind': None, 'skips': [], 'metrics': {}, 'checkpoints': {}, 'checks': 0, 'checks_failed': 0, 'steps': []})
            continue
        say('== %s' % name)
        res, lost = run_one(name, fn, meta, tr, target, out, goldens, opts, say)
        results.append(res)
        say('   %s  (%d checks, %d failed, %.1f s)%s' % (res['status'], res['checks'], res['checks_failed'], res['seconds'], ('  ' + str(res['error']).split('\n')[0][:150]) if res['error'] else ''))
        for s in res['steps']:
            if s['kind'] == 'check' and not s['ok']:
                say('      FAILED: %s' % s['text'])
        if lost:
            alive = recover(tr, say)
    transport.close()

    meta = {'target': target, 'build': build, 'date': time.strftime('%Y-%m-%d %H:%M:%S'), 'seconds': round(time.monotonic() - t0, 1), 'goldens': gdir,
            'baseline': None, 'plugins': plugins, 'args': sys.argv[1:], 'warning': stage_warning}
    cur = baseline_of(results, target, build)
    bpath = args.baseline or os.path.join(HERE, 'baselines', target + '.json')
    diff, base_metrics = None, {}
    if not args.no_baseline and os.path.exists(bpath):
        with open(bpath) as f:
            base = json.load(f)
        if len(chosen) != len(registry):   # a partial run: only compare what was run
            names = {r['name'] for r in results}
            base['scenarios'] = {k: v for k, v in base['scenarios'].items() if k in names}
        diff = diff_baseline(base, cur, target, args.cpu_tol, args.time_tol)
        meta['baseline'] = bpath
        base_metrics = {k: v['metrics'] for k, v in base['scenarios'].items()}
    ok = write_reports(os.path.join(out, args.name), meta, results, diff, base_metrics)
    if args.save_baseline or (args.capture and not os.path.exists(bpath)):
        target_path = bpath if args.save_baseline in (None, 'auto') else args.save_baseline
        os.makedirs(os.path.dirname(target_path), exist_ok=True)
        if len(chosen) != len(registry) and os.path.exists(target_path):
            say('partial run: the baseline file is not overwritten (run the whole suite to save one)')
        elif not ok:
            say('the run did not pass: the baseline is not saved')
        else:
            with open(target_path, 'w') as f:
                json.dump(cur, f, indent=1, sort_keys=True)
                f.write('\n')
            say('baseline saved: %s' % target_path)
    counts = {}
    for r in results:
        counts[r['status']] = counts.get(r['status'], 0) + 1
    say('')
    say('%s: %s' % ('PASS' if ok else 'FAIL', ', '.join('%d %s' % (n, s) for s, n in counts.items())))
    if diff:
        say('baseline diff: %d regressions, %d improvements, %d other changes' % (len(diff['regressions']), len(diff['improvements']), len(diff['changes'])))
        for x in diff['regressions']:
            say('   REGRESSION ' + x)
    say('report: %s.md and %s.json' % (os.path.join(out, args.name), os.path.join(out, args.name)))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
