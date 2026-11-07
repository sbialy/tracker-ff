"""Plugin host: load, replace, unload, the project sidecar, the crash marker and safe mode, the synth engine cache."""
from lib import scenario, SLOT_STATES
from scn_inserts import open_inserts, leave_inserts, clear_inserts, wait_slot, on_inserts, pick, delete, refresh_host

LOADED = ('running', 'bypassed')


def sidecar_text(plugins, mixer=10):
    """A plugins.txt (format 2) that puts the first two plugins of the card in the two inserts."""
    lines = ['tffplug 1', 'format 2', 'mixer %d' % mixer]
    for i, p in enumerate(plugins[:2], 1):
        lines.append('slot %d %s %08x %s' % (i, p['uid'], p['ver'], p['name']))
    return '\n'.join(lines) + '\n'


class Sidecar:
    """Saves /Workspace/plugins.txt, puts another one, gives the original back (or removes ours) when done."""

    def __init__(self, ctx):
        self.ctx = ctx
        self.orig = ctx.tr.get('/Workspace/plugins.txt') if ctx.exists('/Workspace/plugins.txt') else None

    def put(self, text):
        self.ctx.tr.put(text.encode(), '/Workspace/plugins.txt')

    def restore(self):
        c = self.ctx
        if self.orig is not None:
            c.tr.put(self.orig, '/Workspace/plugins.txt')
        elif c.exists('/Workspace/plugins.txt'):
            c.tr.rm('/Workspace/plugins.txt')


@scenario('plugins.load_unload', tags=['plugins'], summary='pick, replace and unload both inserts five times, host.txt after every step')
def load_unload(ctx):
    ctx.reset_ui()
    h = ctx.host_txt()
    ctx.need(len(h['plugins']) >= 2, 'fewer than two plugins in /Plugins/Master (%d)' % len(h['plugins']))
    names = [p['name'] for p in h['plugins']]
    open_inserts(ctx)
    try:
        clear_inserts(ctx)
        nvm0 = ctx.host_txt()['nvm']
        slow = 0
        for rnd in range(5):
            pick(ctx, 'F2', 1 + rnd % len(names))
            waited = 0
            while waited < 6000:
                h = ctx.host_txt()
                if h['slots'][0]['state_name'] in LOADED:
                    break
                ctx.tr.wait(200)
                waited += 200
            ctx.check(h['slots'][0]['state_name'] in LOADED, 'round %d: Insert 1 loads (%s)' % (rnd + 1, h['slots'][0]['state_name']))
            slow = max(slow, waited)
            pick(ctx, 'F3', 1 + (rnd + 1) % len(names))
            h = wait_slot(ctx, 1)
            ctx.check(h['slots'][1]['state_name'] in LOADED, 'round %d: Insert 2 loads (%s)' % (rnd + 1, h['slots'][1]['state_name']))
            ctx.check(h['safe'] == 0, 'round %d: no safe mode' % (rnd + 1))
            # replace Insert 1 by the next plugin without unloading first
            pick(ctx, 'F2')
            h = wait_slot(ctx, 0)
            ctx.check(h['slots'][0]['state_name'] in LOADED, 'round %d: Insert 1 replaced (%s)' % (rnd + 1, h['slots'][0]['name']))
            delete(ctx, 'F3')
            delete(ctx, 'F2')
            ctx.tr.wait(900)
            h = refresh_host(ctx)
            ctx.check(all(s['state_name'] == 'empty' for s in h['slots']), 'round %d: both inserts empty (%s)' % (rnd + 1, [s['state_name'] for s in h['slots']]))
        ctx.metric('load_wait_ms.max', slow, 'ms')
        nvm = ctx.host_txt()['nvm'] - nvm0
        ctx.metric('nvm_commands', nvm, '')
        ctx.device_only('FlexNVM command count for Insert 2 (the Mac has no flash, nvm is always 0): %d' % nvm)
        ctx.check(ctx.markers_clean(), 'no crash marker left behind')
    finally:
        try:
            if on_inserts(ctx):
                clear_inserts(ctx)
                leave_inserts(ctx)
        finally:
            ctx.reset_ui()


@scenario('plugins.sidecar_restore', tags=['plugins', 'goldens'], writes=True, reboots=True,
          summary='a project sidecar (plugins.txt) loads both inserts on the next boot')
def sidecar_restore(ctx):
    ctx.reset_ui()
    h = ctx.host_txt()
    ctx.need(len(h['plugins']) >= 2, 'fewer than two plugins in /Plugins/Master (%d)' % len(h['plugins']))
    side = Sidecar(ctx)
    try:
        side.put(sidecar_text(h['plugins']))
        ctx.reboot()
        h2 = ctx.host_txt()
        want = [p['name'] for p in h['plugins'][:2]]
        ctx.check([s['name'] for s in h2['slots']] == want, 'both inserts come back from the sidecar (%s, expected %s)' % ([s['name'] for s in h2['slots']], want))
        ctx.check(all(s['state_name'] in LOADED for s in h2['slots']), 'both run or wait for Inserts (%s)' % [s['state_name'] for s in h2['slots']])
        ctx.check(h2['mixer'] == 'Soft Knee', 'the mixer line restores Soft Knee (%s)' % h2['mixer'])
        ctx.check(h2['safe'] == 0, 'a normal boot is not safe mode')
        open_inserts(ctx)
        ctx.checkpoint('restored', must=['Inserts'])
        leave_inserts(ctx)
    finally:
        side.restore()
        ctx.reboot()
        ctx.reset_ui()
        h3 = ctx.host_txt()
        ctx.check(all(s['state_name'] == 'empty' for s in h3['slots']) or side.orig is not None, 'the card is back to its own sidecar')


@scenario('plugins.safe_marker', tags=['plugins', 'safe-mode', 'goldens'], writes=True, reboots=True,
          summary='a stale /Plugins/.loading marker: next boot loads no plugin on its own, a manual pick still works')
def safe_marker(ctx):
    ctx.reset_ui()
    h = ctx.host_txt()
    ctx.need(len(h['plugins']) >= 2, 'fewer than two plugins in /Plugins/Master (%d)' % len(h['plugins']))
    victim = h['plugins'][0]['name']
    side = Sidecar(ctx)
    try:
        side.put(sidecar_text(h['plugins']))
        ctx.tr.put(victim.encode(), '/Plugins/.loading')
        m = ctx.tr.markers()
        ctx.check(m['loading'] == '1', 'markers see the stale marker before the reboot')
        ctx.reboot()
        m = ctx.tr.markers()
        h2 = ctx.host_txt()
        ctx.check(m['safe'] == '1', 'safe mode after a boot with a marker (markers safe=%s)' % m['safe'])
        ctx.check(m['loading'] == '0', 'the host removed the marker')
        ctx.check(victim.replace(' ', '_') in m['reason'] or victim in h2['reason'], 'the reason names the plugin (%s)' % h2['reason'])
        ctx.check(h2['safe'] == 1, 'host.txt says safe')
        ctx.check(all(s['state_name'] in ('safe', 'empty') for s in h2['slots']), 'no insert loaded on its own (%s)' % [s['state_name'] for s in h2['slots']])
        open_inserts(ctx)
        ctx.checkpoint('safe.inserts', must=['crashed'])
        # a plugin picked by hand loads even in safe mode
        pick(ctx, 'F2')
        h3 = wait_slot(ctx, 0)
        ctx.check(h3['slots'][0]['state_name'] in LOADED, 'a manual pick loads in safe mode (%s)' % h3['slots'][0]['state_name'])
        ctx.check(h3['safe'] == 1, 'and the session stays safe')
        ctx.settle(700)
        ctx.checkpoint('safe.manual_pick')
        leave_inserts(ctx)
    finally:
        if ctx.exists('/Plugins/.loading'):
            ctx.tr.rm('/Plugins/.loading')
        side.restore()
        ctx.reboot()
        ctx.reset_ui()
        m = ctx.tr.markers()
        ctx.check(m['safe'] == '0', 'the next normal boot is not safe (markers safe=%s)' % m['safe'])


@scenario('plugins.safe_shift_boot', tags=['plugins', 'safe-mode'], reboots=True, sim=False,
          summary='Shift held while the unit boots: safe mode (needs a hand on the unit)')
def safe_shift_boot(ctx):
    """The power button and a key held through a reset cannot be injected: a person holds Shift, the runner sends the reset."""
    ctx.reset_ui()
    if ctx.device_only('Shift at power on (ui_shot --serve starts its clock after the 4 s key window)'):
        return
    if ctx.manual('hold Shift on the unit, then press Enter here: the runner resets the unit. Keep holding Shift for 4 s after the screen comes up'):
        return
    ctx.tr.reboot()
    ctx.tr.wait(5000)
    ctx.settle(500)
    m = ctx.tr.markers()
    ctx.check(m['safe'] == '1', 'Shift at power on gives safe mode (markers safe=%s)' % m['safe'])
    h = ctx.host_txt()
    ctx.check('Shift' in h['reason'], 'the reason says Shift (%s)' % h['reason'])
    ctx.reboot()
    ctx.check(ctx.tr.markers()['safe'] == '0', 'the next boot without Shift is normal')


@scenario('plugins.synth_cache', tags=['plugins', 'engines'], sim=False,
          summary='synth engine plugins: library files, installed index and status of the FlexNVM bank (device)')
def synth_cache(ctx):
    ctx.reset_ui()
    lib = [e['name'] for e in ctx.tr.ls('/Plugins/Synth')] if ctx.exists('/Plugins/Synth') else []
    plg = [n for n in lib if n.endswith('.plg')]
    ctx.metric('synth_library_files', len(plg), '')
    if ctx.device_only('synth plugin install into the FlexNVM bank, /Plugins/synth.txt and installed.txt (the Mac has no flash)'):
        return
    ctx.need(plg, 'no synth plugins on the card')
    ctx.check(ctx.exists('/Plugins/synth.txt'), '/Plugins/synth.txt exists')
    if ctx.exists('/Plugins/synth.txt'):
        txt = ctx.get_text('/Plugins/synth.txt')
        ctx.note('synth.txt: ' + ' // '.join(txt.replace('\r', '').split('\n')[:8]))
        ctx.check('fail' not in txt.lower() or 'fail 0' in txt.lower(), 'synth.txt reports no failure')
    if ctx.exists('/Plugins/installed.txt'):
        idx = [l for l in ctx.get_text('/Plugins/installed.txt').replace('\r', '').split('\n') if l.strip()]
        ctx.metric('installed_engines', len(idx), '')
        ctx.check(len(idx) <= 16, 'at most 16 engines in the bank index (%d)' % len(idx))
