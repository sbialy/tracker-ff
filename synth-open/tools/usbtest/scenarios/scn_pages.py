"""Page walk: every top row page and its sub pages, texts and tile CRCs against the goldens."""
from lib import scenario

# (key, button, page name in `status`, a title that has to be on screen)
PAGES = [
    ('pattern', 'Pattern', 'pattern', 'Pattern 1'),
    ('file', 'File', 'file', 'File'),
    ('config', 'Config', 'config', 'Config'),
    ('sample-play', 'SamplePlay', 'play', 'Sample Playback'),
    ('sample-edit', 'SampleEdit', 'sample-edit', 'Sample Editor 1/2'),
    ('sample-rec', 'SampleRec', 'rec', 'Sample Recorder'),
    ('song', 'Song', 'song', 'Song'),
    ('performance', 'Performance', 'performance', 'Track 1'),
    ('params', 'Params', 'instr', 'Instrument Parameters 1/2'),
]

# a repeat press of the page's own button shows the next sub page
SUBPAGES = [
    ('params-2', 'Params', 'instr', 'Instrument Automation 2/2'),
    ('performance-2', 'Performance', 'performance', 'Midi 9'),
]


@scenario('pages.walk', tags=['pages', 'goldens'], reboots=True, summary='every top row page: texts and tile CRCs against the goldens')
def pages_walk(ctx):
    ctx.fresh_boot('the first look of every page must not depend on what ran before')
    for key, button, page, title in PAGES:
        ctx.tap_until(button, title)
        ctx.expect_page(page)
        ctx.checkpoint('page.' + key, must=[title])
    for key, button, page, title in SUBPAGES:
        ctx.tap('Pattern')
        ctx.settle(400)
        ctx.tap_until(button, title)
        ctx.expect_page(page)
        ctx.checkpoint('page.' + key, must=[title])
    ctx.tap('Pattern')
    ctx.settle(300)
    ctx.tap_until('Params', 'Instrument Parameters 1/2')    # leave the Params and Performance pages on their first sub page
    ctx.tap_until('Performance', 'Track 1')
    ctx.tap('SampleLoad')    # the sample loader lists the card's folders and samples
    ctx.settle(1200)
    ctx.expect_page('importer')
    # the card list on the left (3 tiles) shows the folders the earlier scenarios created, so a run of this scenario alone differs there
    ctx.checkpoint('page.sample-load', must=['Sample Loader'], tol=3)
    ctx.fresh_boot('start the next scenario from the power up screens, not from the sample loader')


@scenario('pages.roundtrip', tags=['pages'], summary='leave a page and come back: the first look equals the second')
def pages_roundtrip(ctx):
    """Each page twice with another page in between, the screen has to be the same both times (state leaks show here)."""
    ctx.reset_ui()
    seen = {}
    for rounds in (1, 2):
        for key, button, page, title in PAGES[1:6]:
            ctx.tap(button)
            ctx.settle(600)
            txt, til = ctx.capture()
            if key in seen:
                ctx.check(seen[key][0] == txt and seen[key][1] == til, '%s: second visit equals the first' % key)
            else:
                seen[key] = (txt, til)
            ctx.tap('Pattern')
            ctx.settle(400)
    ctx.reset_ui()
