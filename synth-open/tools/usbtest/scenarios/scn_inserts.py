"""Master Inserts page flows: Mixer column, Insert 1 and 2 load, parameter pages. Plugins come from /Plugins/Master on the card.

Two facts about the page that the helpers here work around:
  - a press on the button of the column that is already selected is not a selection: on a plugin column it shows the next four
    parameters, on the Mixer column it steps the list. select() therefore selects a neutral column (F4) first
  - /Plugins/host.txt is written on a load, on a rescan (the Inserts page does one on entry) and on a project save, not on an
    unload or a mixer change: refresh_host() leaves the page and enters it again before reading
"""
from lib import scenario


def on_inserts(ctx):
    """The Inserts page title is 'Inserts ..', in safe mode it names the plugin that crashed ('Safe mode: BBD crashed')."""
    return ctx.title().startswith(('Inserts', 'Safe mode'))


def open_inserts(ctx):
    """Shift + Saturation on Master 1/n: the Inserts page whether or not it is a stop of the Master cycle."""
    ctx.goto_master()
    ctx.tap('F6')
    ctx.combo('Shift', 'F6', 700)
    ctx.require(on_inserts(ctx), 'the Inserts page is open (%s)' % ctx.title())


def leave_inserts(ctx):
    ctx.tap('F8')    # the Back column
    ctx.settle(500)


def refresh_host(ctx):
    leave_inserts(ctx)
    open_inserts(ctx)
    return ctx.host_txt()


def select(ctx, col):
    """Selects a column (F1 Mixer, F2 Insert 1, F3 Insert 2) so that the next press is a first press."""
    ctx.tap('F4')
    ctx.tap(col)


def pick(ctx, col, downs=1):
    select(ctx, col)
    ctx.tap('Down', downs)


def delete(ctx, col):
    select(ctx, col)
    ctx.tap('Delete')


def clear_inserts(ctx):
    """Both inserts empty and the mixer on Hard Clip, so a scenario starts and ends the same."""
    delete(ctx, 'F3')
    delete(ctx, 'F2')
    delete(ctx, 'F1')
    ctx.tr.wait(900)
    return refresh_host(ctx)


def wait_slot(ctx, slot, name=None, timeout_ms=6000):
    """Polls /Plugins/host.txt until the slot (0 or 1) runs (a load writes it itself, no refresh needed)."""
    waited = 0
    while True:
        h = ctx.host_txt()
        s = h['slots'][slot] if len(h['slots']) > slot else {'name': '', 'state_name': 'empty'}
        if s['state_name'] in ('running', 'bypassed') and (name is None or s['name'] == name):
            return h
        if waited >= timeout_ms:
            return h
        ctx.tr.wait(300)
        waited += 300


@scenario('inserts.flows', tags=['inserts', 'plugins', 'goldens'],
          summary='Inserts page: Mixer column, Insert 1 and 2 load and unload, parameter pages')
def inserts_flows(ctx):
    ctx.reset_ui()
    h = ctx.host_txt()
    ctx.need(len(h['plugins']) >= 2, 'fewer than two plugins in /Plugins/Master (%d)' % len(h['plugins']))
    names = [p['name'] for p in h['plugins']]
    ctx.note('plugins on the card: ' + ', '.join(names))
    try:
        open_inserts(ctx)
        h = clear_inserts(ctx)
        ctx.check(h['mixer'] == 'Hard Clip' and all(s['state_name'] == 'empty' for s in h['slots']), 'start: Hard Clip, both inserts empty')
        ctx.checkpoint('empty', must=['Inserts', 'Insert 1', 'Insert 2'])

        # Mixer column: Down steps Hard Clip > Soft Clip > Soft Knee, Delete is Hard Clip
        select(ctx, 'F1')
        ctx.checkpoint('mixer.1', must=['Mixer'])
        stages = []
        for i in (2, 3):
            ctx.tap('Down')
            ctx.settle(500)
            ctx.checkpoint('mixer.%d' % i)
            stages.append(refresh_host(ctx)['mixer'])
            select(ctx, 'F1')
        ctx.check(stages == ['Soft Clip', 'Soft Knee'], 'Down steps the mixer stage: %s' % stages)
        digests = [ctx.checkpoints['mixer.%d' % i].get('digest') for i in (1, 2, 3)]
        ctx.check(len(set(digests)) == 3, 'the three stages look different on screen')
        ctx.tap('Delete')
        ctx.check(refresh_host(ctx)['mixer'] == 'Hard Clip', 'Delete returns the mixer to Hard Clip')

        # Insert 1: Down from None picks the first plugin of the list
        pick(ctx, 'F2')
        h = wait_slot(ctx, 0)
        s1 = h['slots'][0]
        ctx.check(s1['state_name'] in ('running', 'bypassed'), 'Insert 1 loaded (%s %s)' % (s1['name'], s1['state_name']))
        ctx.check(s1['name'] in names, 'Insert 1 is one of the card plugins (%s)' % s1['name'])
        ctx.settle(700)
        ctx.checkpoint('insert1.loaded', must=['Insert 1'])
        # the parameter pages of the focused insert: a further press on the insert's button shows the next four
        ctx.tap('F2')
        ctx.settle(500)
        ctx.checkpoint('insert1.params2')
        ctx.tap('F2')
        ctx.settle(500)
        ctx.checkpoint('insert1.params3')

        # Insert 2
        pick(ctx, 'F3', 2)
        h = wait_slot(ctx, 1)
        s2 = h['slots'][1]
        ctx.check(s2['state_name'] in ('running', 'bypassed'), 'Insert 2 loaded (%s %s)' % (s2['name'], s2['state_name']))
        ctx.check(s2['name'] != '' and s2['name'] != s1['name'], 'Insert 2 holds another plugin than Insert 1')
        ctx.settle(700)
        ctx.checkpoint('insert2.loaded', must=['Insert 2'])

        # unload: Delete empties the selected insert
        ctx.tap('Delete')
        ctx.tr.wait(900)
        h = refresh_host(ctx)
        ctx.check(h['slots'][1]['state_name'] == 'empty', 'Delete empties Insert 2 (%s)' % h['slots'][1]['state_name'])
        ctx.check(h['slots'][0]['state_name'] in ('running', 'bypassed'), 'Insert 1 keeps running')
        ctx.checkpoint('insert2.empty')
    finally:
        try:
            if on_inserts(ctx):
                clear_inserts(ctx)
                leave_inserts(ctx)
        finally:
            ctx.reset_ui()
