"""Project open, play and stop, load times and the load-hang probe."""
import time

from lib import scenario, UsbTestError


def pick_project(ctx):
    """('Projects', 'Demo') from --project, else the first folder under /Projects."""
    want = ctx.opts.get('project')
    if want:
        path, _, name = want.rpartition('/')
        return (path or 'Projects'), name
    try:
        dirs = [e['name'] for e in ctx.tr.ls('/Projects') if e['dir']]
    except UsbTestError:
        dirs = []
    return ('Projects', sorted(dirs)[0]) if dirs else (None, None)


@scenario('project.open_play_stop', tags=['project', 'cpu'], writes=True,
          summary='open a project three times, load time and project hash, play, stop, open refused while playing')
def project_open(ctx):
    ctx.reset_ui()
    path, name = pick_project(ctx)
    ctx.need(name, 'no project folder under /Projects (put one there or pass --project Projects/Name)')
    ctx.note('project %s/%s' % (path, name))
    hashes, times = [], []
    for i in range(3):
        t0 = time.monotonic()
        ctx.tr.open_project(path, name, force=True)
        ctx.settle(500)
        times.append(round(time.monotonic() - t0, 2))
        hashes.append(ctx.tr.hash_project())
        st = ctx.status()
        ctx.check(st.get('play') == '0' and st.get('name') == 'pattern', 'open %d: back on the pattern page, stopped (%s)' % (i + 1, st.get('name')))
        if i == 0:
            ctx.checkpoint('loaded', must=['Pattern'])
    ctx.check(len(set(hashes)) == 1, 'the same project loads to the same state every time (%s)' % ' '.join(hashes))
    ctx.metric('open_s.max', max(times), 's')
    ctx.metric('open_s.first', times[0], 's')
    ctx.note('project hash %s, open times %s s (wall clock: only meaningful on a device)' % (hashes[0], times))

    # play, stop: the project must not change by playing it
    ctx.tr.command('play song')
    ctx.tr.wait(300)
    ctx.check(ctx.status().get('play') == '1', 'play song starts')
    ctx.tr.command('cpu reset')
    ctx.tr.wait(2500)
    c = ctx.tr.cpu()
    if c:
        ctx.metric('project.play.avg_us', c['avg'], 'us')
        ctx.metric('project.play.max_us', c['max'], 'us', limit=2902)
    refused = ctx.tr.command('open %s %s' % (path, name), expect_err=True)[0]
    ctx.check(refused.startswith('@err'), 'open while playing is refused (%s)' % refused)
    ctx.tr.command('stop')
    ctx.tr.wait(300)
    ctx.check(ctx.status().get('play') == '0', 'stop stops')
    ctx.check(ctx.tr.hash_project() == hashes[0], 'playing and stopping did not change the project')
    ctx.tap('Play')
    ctx.tr.wait(400)
    ctx.check(ctx.status().get('play') == '1', 'the Play button starts')
    ctx.tap('Play')
    ctx.tr.wait(400)
    ctx.check(ctx.status().get('play') == '0', 'and stops')
    ctx.reset_ui()
