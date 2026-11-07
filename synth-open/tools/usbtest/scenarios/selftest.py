#!/usr/bin/env python3
"""Proves the suite itself against ui_shot --serve (host tool, NOT firmware):

  1. a first run records the goldens, a second run passes against them
  2. a golden with one wrong tile CRC fails and names the checkpoint and the tile
  3. a golden with a wrong text fails
  4. a missing golden fails with --no-record
  5. a baseline round trip: the same run has no regression, a CPU that got 2x slower and a scenario that got worse are regressions
  6. the committed Mac goldens pass

  python3 -I selftest.py UI_SHOT_BINARY WORK_DIR
"""
import json
import os
import shutil
import subprocess
import sys
import threading
import tty

HERE = os.path.dirname(os.path.abspath(__file__))
SUITE = os.path.join(HERE, 'suite.py')
UI, WORK = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
failed = []


def check(name, cond, info=''):
    print('%-5s %s %s' % ('ok' if cond else 'FAIL', name, info), flush=True)
    if not cond:
        failed.append(name)


def suite(name, *args, env=None):
    out = os.path.join(WORK, name)
    e = dict(os.environ)
    e.update(env or {})
    p = subprocess.run([sys.executable, '-I', SUITE, '--sim', UI, '--out', out] + list(args), env=e, capture_output=True, text=True)
    with open(out + '.log', 'w') as f:
        f.write(p.stdout + p.stderr)
    return p.returncode, p.stdout, out


def load(path):
    with open(path) as f:
        return json.load(f)


shutil.rmtree(WORK, ignore_errors=True)
os.makedirs(WORK)
G = os.path.join(WORK, 'goldens')

print('== 1. first run records, second run compares')
rc, out, o1 = suite('run1', '--goldens', G, '--no-baseline')
rep = load(o1 + '/suite.json')
recorded = [r['name'] for r in rep['results'] if r['status'] == 'RECORDED']
check('first run passes and records', rc == 0 and len(recorded) >= 6, '(%d scenarios recorded)' % len(recorded))
check('goldens written', len([f for f in os.listdir(G) if not f.startswith('_')]) == len(recorded) and os.path.exists(os.path.join(G, '_stage.json')))
rc, out, o2 = suite('run2', '--goldens', G, '--no-baseline')
rep = load(o2 + '/suite.json')
check('second run passes with nothing recorded', rc == 0 and not any(r['status'] == 'RECORDED' for r in rep['results']))
compared = sum(1 for r in rep['results'] for c in r['checkpoints'].values() if c['status'] == 'pass')
check('checkpoints were compared', compared >= 30, '(%d)' % compared)

print('== 2. a wrong tile CRC in a golden fails')
G2 = os.path.join(WORK, 'goldens_tile')
shutil.copytree(G, G2)
p = os.path.join(G2, 'pages.walk.json')
d = load(p)
d['checkpoints']['page.song']['tiles'][19] ^= 0x1
with open(p, 'w') as f:
    json.dump(d, f)
rc, out, o3 = suite('broken_tile', '--goldens', G2, '--no-baseline', '--only', 'pages.walk')
rep = load(o3 + '/suite.json')
cp = rep['results'][0]['checkpoints'].get('page.song', {})
check('broken tile golden fails the run', rc == 1 and rep['results'][0]['status'] == 'FAIL')
check('the failure names the checkpoint and the tile', cp.get('status') == 'fail' and cp.get('bad_tiles') == ['r2c3'], '(%s)' % cp.get('bad_tiles'))
check('a screenshot of the failing screen is kept', bool(cp.get('png')) and os.path.exists(os.path.join(o3, cp['png'])))
check('the other checkpoints still pass', sum(1 for c in rep['results'][0]['checkpoints'].values() if c['status'] == 'pass') >= 10)
md = open(o3 + '/suite.md').read()
check('the markdown report lists it', 'page.song' in md and 'r2c3' in md and '## Failures' in md)

print('== 3. a wrong text in a golden fails')
G3 = os.path.join(WORK, 'goldens_text')
shutil.copytree(G, G3)
p = os.path.join(G3, 'input.master_cycle.json')
d = load(p)
d['checkpoints']['stop2']['texts'][1] = 'Not a Mixer'
with open(p, 'w') as f:
    json.dump(d, f)
rc, out, o4 = suite('broken_text', '--goldens', G3, '--no-baseline', '--only', 'input.master_cycle')
rep = load(o4 + '/suite.json')
cp = rep['results'][0]['checkpoints'].get('stop2', {})
check('broken text golden fails the run', rc == 1 and cp.get('status') == 'fail' and any('Not a Mixer' in t for t in cp.get('text_diff', [])), '(%s)' % cp.get('text_diff'))

print('== 4. no golden with --no-record fails')
G4 = os.path.join(WORK, 'goldens_missing')
shutil.copytree(G, G4)
os.remove(os.path.join(G4, 'pages.walk.json'))
rc, out, o5 = suite('missing', '--goldens', G4, '--no-baseline', '--no-record', '--only', 'pages.walk')
check('a missing golden fails when recording is off', rc == 1)
rc, out, o5 = suite('missing_rec', '--goldens', G4, '--no-baseline', '--only', 'pages.walk')
check('and is recorded when it is on', rc == 0 and os.path.exists(os.path.join(G4, 'pages.walk.json')))

print('== 5. baseline and regression diff')
B = os.path.join(WORK, 'baseline.json')
rc, out, o6 = suite('base', '--goldens', G, '--save-baseline', B)
check('baseline saved from a passing run', rc == 0 and os.path.exists(B))
rc, out, o7 = suite('same', '--goldens', G, '--baseline', B)
rep = load(o7 + '/suite.json')
check('the same run has no regression', rc == 0 and rep['diff'] and not rep['diff']['regressions'] and not rep['diff']['changes'], '(%s)' % (rep['diff'] or {}).get('regressions'))
rc, out, o8 = suite('slow', '--goldens', G, '--baseline', B, '--only', 'engines.cpu', env={'UISHOT_FAKE_CPU': '600,1800'})
rep = load(o8 + '/suite.json')
reg = rep['diff']['regressions'] if rep['diff'] else []
check('a CPU that got 2x slower is a regression', rc == 1 and any('engine.CHIP.v4.avg_us' in r for r in reg), '(%d regressions)' % len(reg))
rc, out, o9 = suite('noise', '--goldens', G, '--baseline', B, '--only', 'engines.cpu', env={'UISHOT_FAKE_CPU': '310,930'})
rep = load(o9 + '/suite.json')
check('3 percent more CPU is inside the tolerance', rc == 0 and not rep['diff']['regressions'])
BUD = os.path.join(WORK, 'budgets.json')
with open(BUD, 'w') as f:
    json.dump({'engine.CHIP.v4': {'max_us': 500, 'avg_us': 200}}, f)
rc, out, o10 = suite('budget', '--goldens', G, '--no-baseline', '--only', 'engines.cpu', '--budgets', BUD)
rep = load(o10 + '/suite.json')
bad = [s['text'] for s in rep['results'][0]['steps'] if s['kind'] == 'check' and not s['ok']]
check('a CPU budget from the budget file fails a scenario', rc == 1 and any('engine.CHIP.v4' in t and 'budget' in t for t in bad), '(%s)' % bad)
# a scenario that got worse: the baseline says PASS, the golden of the next run is broken
rc, out, o11 = suite('worse', '--goldens', G2, '--baseline', B, '--only', 'pages.walk')
rep = load(o11 + '/suite.json')
check('PASS -> FAIL is a regression', rc == 1 and any('pages.walk: PASS -> FAIL' in r for r in rep['diff']['regressions']), '(%s)' % rep['diff']['regressions'])

print('== 6. the committed Mac goldens and baseline')
rc, out, o12 = suite('committed')
rep = load(o12 + '/suite.json')
check('the committed goldens pass', rc == 0 and not any(r['status'] in ('FAIL', 'ERROR') for r in rep['results']))
check('against the committed baseline', rep['diff'] is not None and not rep['diff']['regressions'], '(%s)' % (rep['diff'] or {}).get('regressions'))

print('== 7. the device code path: the suite over a serial port (ui_shot behind a pty)')
# The pty carries no clock: time in ui_shot only moves with commands, so only scenarios that wait on screen state can run this way.
master, slave = os.openpty()
tty.setraw(slave)   # no echo of the sim's output back into its input
port = os.ttyname(slave)
sd7 = os.path.join(WORK, 'sd_pty')
shutil.copytree(os.path.join(o12, 'sd'), sd7)
proc = subprocess.Popen([UI, '--serve', sd7], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)


def pump_out():
    while True:
        d = os.read(proc.stdout.fileno(), 65536)
        if not d:
            return
        os.write(master, d)


def pump_in():
    while True:
        try:
            d = os.read(master, 65536)
        except OSError:
            return
        if not d:
            return
        proc.stdin.write(d)


threading.Thread(target=pump_out, daemon=True).start()
threading.Thread(target=pump_in, daemon=True).start()
G7 = os.path.join(WORK, 'goldens_device')
out7 = os.path.join(WORK, 'device_path')
env7 = dict(os.environ)
env7['USBTEST_PORT'] = port
p7 = subprocess.run([sys.executable, '-I', SUITE, '--port', 'auto', '--out', out7, '--goldens', G7, '--no-baseline', '--only', 'pages.roundtrip,project.*,plugins.synth_cache,plugins.sidecar_restore'],
                    env=env7, capture_output=True, text=True, timeout=600)
with open(out7 + '.log', 'w') as f:
    f.write(p7.stdout + p7.stderr)
proc.stdin.close()
try:
    proc.wait(timeout=10)
except subprocess.TimeoutExpired:
    proc.kill()
rep = load(out7 + '/suite.json') if os.path.exists(out7 + '/suite.json') else {'results': [], 'meta': {}}
by = {r['name']: r for r in rep['results']}
check('device path: the suite runs over the port', p7.returncode == 0 and rep['meta'].get('target') == 'device', '(rc %s)' % p7.returncode)
check('device path: a scenario passes over the port', by.get('pages.roundtrip', {}).get('status') == 'PASS', '(%s)' % by.get('pages.roundtrip', {}).get('status'))
check('device path: scenarios that write the card are skipped without --device-writes',
      by.get('project.open_play_stop', {}).get('status') == 'SKIP' and by.get('plugins.sidecar_restore', {}).get('status') == 'SKIP')
sc = by.get('plugins.synth_cache', {})
check('device path: device-only steps run over the port (not skipped as on the Mac)', sc.get('status') == 'PASS' and sc.get('checks', 0) >= 1, '(%s, %s checks)' % (sc.get('status'), sc.get('checks')))

print()
print('SELFTEST %s' % ('FAILED: ' + ', '.join(failed) if failed else 'OK'))
sys.exit(1 if failed else 0)
