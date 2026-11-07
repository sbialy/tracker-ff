"""Synth engine picker, every engine loaded, audio CPU per engine at 1 and 4 voices."""
import re

from lib import scenario

ENGINE_RX = re.compile(r'Synth (\d) \(([^)]+)\)')


def engine_name(ctx, part=1):
    for t in ctx.raw_texts():
        m = ENGINE_RX.search(t)
        if m and int(m.group(1)) == part:
            return m.group(2)
    return None


def open_model_page(ctx, part=1):
    ctx.select_synth(part)
    ctx.tap('Params')
    ctx.settle(900)
    ctx.expect_page('instr')


def cycle_engines(ctx, part=1, limit=40):
    """Generator over the engines of the Model column. The press on F1 rolls to the next engine, so the body of the loop runs with
    that engine loaded. Ends when the first engine is back (the column wraps). Model rolls need time on a device: an engine
    that is only listed is installed after the list rests 500 ms."""
    first = engine_name(ctx, part)
    ctx.require(first is not None, 'the synth page shows the part\'s engine in its title')
    yield first
    for _ in range(limit):
        ctx.tap('F1')
        ctx.settle(1200 if not ctx.sim else 900)
        name = engine_name(ctx, part)
        if name == first or name is None:
            return
        yield name


@scenario('engines.picker', tags=['engines', 'goldens'], summary='engine picker: every engine of the Model column loaded, its page against the goldens')
def engines_picker(ctx):
    ctx.reset_ui()
    open_model_page(ctx)
    names = []
    start = engine_name(ctx)
    try:
        for name in cycle_engines(ctx):
            names.append(name)
            missing = ctx.has_text('Missing')
            if missing and ctx.sim:
                ctx.steps.append({'kind': 'skip', 'why': 'device-only', 'text': 'engine %s is a plugin engine (the Mac has no FlexNVM bank: Missing)' % name})
                ctx.skips.append('engine %s needs the FlexNVM bank' % name)
                continue
            ctx.check(not missing, 'engine %s loads (no Missing)' % name)
            ctx.check(ctx.tr.peek('lastUsedInstrument') == 64, 'the picker stays on Synth 1')
            ctx.checkpoint('engine.' + name, must=['Synth 1 (%s)' % name, 'Model', 'Polyphony'])
        ctx.note('engines in the Model column: ' + ' '.join(names))
        ctx.metric('engine_count', len(names), '')
        ctx.check(len(names) >= 2, 'at least two engines to pick from (%d)' % len(names))
        ctx.check(len(set(names)) == len(names), 'no engine listed twice')
        ctx.check(engine_name(ctx) == start, 'the column came back to the engine it started on (%s)' % engine_name(ctx))

        # the other synth pages of the part and the patch library
        ctx.tap('Params')
        ctx.settle(900)
        ctx.checkpoint('synth.page2', must=['Synth'])
        ctx.tap('Params')
        ctx.settle(900)
        ctx.checkpoint('synth.page3', must=['Synth'])
        ctx.tap('Params')
        ctx.settle(900)
        ctx.tap('F7')    # Load: the patch library
        ctx.settle(1200)
        ctx.checkpoint('library', must=['Preview', 'Cancel', 'Confirm'])
        ctx.tap('F7')    # Cancel
        ctx.settle(900)
        ctx.check(ctx.has_text('Load'), 'Cancel leaves the library')
        ctx.tap('F8')    # Edit
        ctx.settle(1500)
        ctx.checkpoint('synth.edit')
    finally:
        ctx.release_all()
        ctx.reset_ui()


@scenario('engines.cpu', tags=['engines', 'cpu'], summary='audio CPU per engine with 1 voice and 4 voices (device numbers, fixed on the Mac)')
def engines_cpu(ctx):
    ctx.reset_ui()
    budgets = ctx.opts.get('budgets', {})
    open_model_page(ctx)
    ctx.tap('Pattern')
    ctx.settle(500)
    ctx.cpu_measure('cpu.idle', hold_ms=1200, budget=budgets.get('cpu.idle'))
    ctx.tap('Params')
    ctx.settle(700)
    done = []
    try:
        for name in cycle_engines(ctx):
            if ctx.has_text('Missing'):
                if ctx.sim:
                    ctx.steps.append({'kind': 'skip', 'why': 'device-only', 'text': 'engine %s is a plugin engine (Missing on the Mac)' % name})
                    ctx.skips.append('engine %s needs the FlexNVM bank' % name)
                else:
                    ctx.check(False, 'engine %s shows Missing' % name)
                continue
            done.append(name)
            ctx.tap('Pattern')
            ctx.settle(500)
            ctx.check(ctx.tr.peek('lastUsedInstrument') == 64, 'pads play Synth 1')
            ctx.pad_press(0)
            ctx.tr.wait(400)
            key = 'engine.%s.v1' % name
            ctx.cpu_measure(key, budget=budgets.get(key))
            ctx.pad_release(0)
            ctx.tr.wait(900)
            for p in (0, 1, 2, 3):
                ctx.pad_press(p)
            ctx.tr.wait(400)
            key = 'engine.%s.v4' % name
            ctx.cpu_measure(key, budget=budgets.get(key))
            for p in (3, 2, 1, 0):
                ctx.pad_release(p)
            ctx.tr.wait(900)
            ctx.tap('Params')
            ctx.settle(700)
        ctx.note('measured engines: ' + ' '.join(done))
        ctx.check(len(done) >= 1, 'at least one engine measured')
    finally:
        ctx.release_all()
        ctx.reset_ui()
