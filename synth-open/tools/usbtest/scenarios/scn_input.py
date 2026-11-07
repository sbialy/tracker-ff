"""Master button cycle, Shift combinations and hold repeat."""
from lib import scenario


def plugins_present(ctx):
    try:
        return any(e['name'].lower().endswith('.plg') for e in ctx.tr.ls('/Plugins/Master'))
    except Exception:  # noqa: BLE001
        return False


@scenario('input.master_cycle', tags=['input', 'master', 'goldens'], reboots=True,
          summary='Master button stops incl. Inserts, hold = peek, Shift + Saturation')
def master_cycle(ctx):
    ctx.fresh_boot('the Master pages remember their selected column')
    ctx.goto_master()
    with_inserts = plugins_present(ctx)
    n = 4 if with_inserts else 3
    ctx.check(ctx.title() == 'Master 1/%d' % n, 'first stop reads Master 1/%d (%s)' % (n, ctx.title()))
    stops = [ctx.title()]
    ctx.checkpoint('stop1', must=['Master 1/'])
    for i in range(2, n + 1):
        ctx.tap('Master')
        ctx.settle(500)
        stops.append(ctx.title())
        ctx.checkpoint('stop%d' % i)
    ctx.note('stops: ' + ' > '.join(stops))
    if with_inserts:
        ctx.check(stops[-1].startswith('Inserts'), 'the last stop is the Inserts page (%s)' % stops[-1])
    ctx.check(all(s.split(' ')[-1].endswith('/%d' % n) for s in stops[:3]), 'stops count n/%d' % n)
    ctx.tap('Master')    # one more wraps to the first stop
    ctx.settle(500)
    ctx.check(ctx.title() == stops[0], 'the next press wraps to the first stop (%s)' % ctx.title())

    # hold = peek: Master held from another page shows the Master page, releasing it returns
    ctx.tap('Pattern')
    ctx.settle(500)
    ctx.press('Master')
    ctx.tr.wait(700)
    ctx.tr.idle()
    ctx.expect_page('master')
    ctx.release('Master')
    ctx.settle(600)
    ctx.expect_page('pattern')

    # Shift + Saturation opens the Inserts page directly (also with no plugin on the card)
    ctx.tap('Master')
    ctx.settle(500)
    ctx.goto_master()
    ctx.tap('F6')    # selects the Saturation column
    ctx.combo('Shift', 'F6', 700)
    ctx.check(ctx.title().startswith('Inserts'), 'Shift + Saturation opens Inserts (%s)' % ctx.title())
    ctx.tap('F8')    # Back
    ctx.settle(500)
    ctx.check(ctx.title().startswith('Master 1/'), 'Back returns to Master 1/n (%s)' % ctx.title())
    ctx.reset_ui()


@scenario('input.shift_combos', tags=['input', 'goldens'], summary='Shift + Insert, Shift + Play, Shift held, Shift + encoder')
def shift_combos(ctx):
    ctx.reset_ui()
    ctx.tap('Down', 3)
    ctx.settle(300)
    p = ctx.step_position()
    ctx.check(p == 4, 'three Down taps move the cursor to step 4 (%s)' % p)
    ctx.combo('Shift', 'Insert')    # Home
    p = ctx.step_position()
    ctx.check(p == 1, 'Shift + Insert goes home to step 1 (%s)' % p)
    ctx.checkpoint('pattern.plain', must=['Pattern 1'])

    # Shift held changes the under screen row (mute buttons): the tiles differ from the plain look, the texts may not
    plain_txt, plain_til = ctx.capture()
    ctx.press('Shift')
    ctx.tr.wait(400)
    ctx.tr.idle()
    shift_txt, shift_til = ctx.capture()
    shift_state = ctx.tr.status().get('name')
    ctx.release('Shift')
    ctx.settle(500)
    ctx.check(shift_state == 'pattern', 'Shift held keeps the pattern page')
    ctx.check(shift_til != plain_til, 'Shift held changes what the screen shows (mute row)')

    # Shift + Play starts the song, Play stops it
    ctx.combo('Shift', 'Play', 500)
    ctx.check(ctx.tr.status().get('play') == '1', 'Shift + Play starts playback')
    ctx.tap('Play')
    ctx.tr.wait(300)
    ctx.check(ctx.tr.status().get('play') == '0', 'Play stops it')
    ctx.combo('Shift', 'Insert')

    # Shift + encoder: never slower than the encoder alone
    start = ctx.step_position()
    ctx.enc(1)
    plain = ctx.step_position() - start
    ctx.combo('Shift', 'Insert')
    start = ctx.step_position()
    ctx.press('Shift')
    try:
        ctx.enc(1)
    finally:
        ctx.release('Shift')
    boosted = ctx.step_position() - start
    ctx.metric('shift_encoder_step', boosted, 'steps')
    ctx.check(plain == 1, 'the encoder alone moves one step (%d)' % plain)
    ctx.check(boosted >= plain, 'Shift + encoder moves at least as far (%d against %d)' % (boosted, plain))
    ctx.combo('Shift', 'Insert')

    # Shift + Down extends the selection (the pattern shows it), moving without Shift clears it
    ctx.press('Shift')
    try:
        ctx.tr.key('Down', 'tap', 3)
    finally:
        ctx.release('Shift')
    ctx.settle(400)
    ctx.checkpoint('selection.down3')
    ctx.tap('Up', 3)
    ctx.combo('Shift', 'Insert')
    ctx.reset_ui()
    if ctx.device_only('Shift + power button takes a screenshot (the power key cannot be injected)'):
        pass


@scenario('input.hold_repeat', tags=['input'], summary='held keys auto repeat after 333 ms, taps do not')
def hold_repeat(ctx):
    ctx.reset_ui()
    ctx.combo('Shift', 'Insert')
    base = ctx.step_position()
    ctx.tap('Down')
    ctx.settle(200)
    tap_delta = ctx.step_position() - base
    ctx.check(tap_delta == 1, 'one tap moves one step (%d)' % tap_delta)

    ctx.combo('Shift', 'Insert')
    base = ctx.step_position()
    ctx.press('Down')
    try:
        ctx.tr.wait(250)     # inside the 333 ms before the first repeat: still one step
        ctx.tr.idle()
        early = ctx.step_position() - base
        ctx.tr.wait(1250)
        ctx.tr.idle()
    finally:
        ctx.release('Down')
    ctx.settle(300)
    held = ctx.step_position() - base
    ctx.metric('hold_steps_1500ms', held, 'steps')
    ctx.check(early <= 2, 'no repeat inside the first 333 ms (%d steps after 250 ms)' % early)
    ctx.check(held >= 5, 'a 1.5 s hold repeats (%d steps)' % held)
    # releasing stops it
    after = ctx.step_position()
    ctx.tr.wait(600)
    ctx.tr.idle()
    ctx.check(ctx.step_position() == after, 'no repeat after the release')

    # keys that must not repeat into an action: Shift and Play held do not start anything twice
    ctx.combo('Shift', 'Insert')
    ctx.press('Play')
    try:
        ctx.tr.wait(900)
        ctx.tr.idle()
    finally:
        ctx.release('Play')
    ctx.tr.wait(300)
    ctx.check(ctx.tr.status().get('play') == '1', 'a held Play is one start, not a start and a stop')
    ctx.tap('Play')
    ctx.tr.wait(300)
    ctx.check(ctx.tr.status().get('play') == '0', 'Play stops')
    ctx.reset_ui()
