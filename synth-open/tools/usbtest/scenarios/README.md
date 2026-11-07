# usbtest scenarios: the scenario library and the suite runner

Host tools, NOT firmware. Phase 4 of the USB test plan (`ref/usb-test-mode-feasibility.md` section 5). A scenario drives the Tracker only through
console commands (`key`, `pad`, `enc`, `idle`, `texts`, `shotcrc`, `status`, `peek`, `hash`, `open`, `play`, `stop`, `get`, `put`, `rm`, `reboot`,
`markers`, `cpu`), so the same scenarios run against `ui_shot --serve` on the Mac and later, unchanged, against the unit over the CDC port.
The console itself and its protocol are in `docs/usb-test-console.md`, the runner is `../usbtest.py`.

    run_suite.sh                    build ui_shot, stage a card, run every scenario, report in build/usbtest-suite/last/suite.md and suite.json
    run_suite.sh selftest           also prove the suite itself (about 3 minutes, see "Proof")
    run_suite.sh -- --only 'inserts.*' --tag cpu --update-goldens ...     everything after -- goes to suite.py

    python3 -I suite.py --sim build/ui-shot/ui_shot --out DIR                         the Mac front end (card staged under DIR/sd)
    python3 -I suite.py --port auto --capture --device-writes --out DIR               the unit, first run (records goldens and baseline)
    python3 -I suite.py --port auto --device-writes --out DIR                         the unit, later runs (compare, diff against the baseline)
    python3 -I suite.py --list                                                         the scenarios with tags and flags

Exit status 0 when no scenario failed and the baseline diff has no regression.

## Scenarios

| Scenario | What it does | Mac | Device |
| :--- | :--- | :--- | :--- |
| `pages.walk` | every top row page and the two sub pages the repeat press reaches (Params 2/2, Performance Midi 9..16): texts and tile CRCs against the goldens. Starts and ends with a reboot | yes | yes, adds the SampleLoad page |
| `pages.roundtrip` | the File, Config, SamplePlay, SampleEdit and SampleRec pages twice with another page between: the second look equals the first | yes | yes |
| `input.master_cycle` | the Master button stops (1/n, Sample Track Mixer, Global Mixer, Inserts), n from the plugins on the card, the wrap, hold = peek and back, Shift + Saturation opens Inserts and Back leaves it | yes | yes |
| `input.shift_combos` | Shift + Insert (home), Shift held (the mute row), Shift + Play, Shift + encoder never slower than the encoder, Shift + Down selection | yes | yes, the Shift + power screenshot is a manual step |
| `input.hold_repeat` | one tap is one step, a held Down does not repeat in the first 250 ms and repeats later (steps per 1.5 s is a metric), the release stops it, a held Play is one start | yes | yes |
| `inserts.flows` | the Inserts page: Mixer column (three stages, Delete), Insert 1 and 2 load by pick, the parameter pages, unload by Delete, host.txt checked after each step | yes | yes |
| `plugins.load_unload` | five rounds of pick, replace and unload on both inserts, host.txt after every step, load wait time, FlexNVM command count | yes (nvm is 0) | yes |
| `plugins.sidecar_restore` | a project sidecar `/Workspace/plugins.txt` puts two plugins and Soft Knee in the inserts at the next boot. Writes the card, puts the original back | yes | `--device-writes` |
| `plugins.safe_marker` | a stale `/Plugins/.loading` makes the next boot safe: `markers` and host.txt say so, no insert loads on its own, a pick by hand does | yes | `--device-writes` |
| `plugins.safe_shift_boot` | Shift held while the unit boots gives safe mode | skipped | needs a hand: `--interactive` (the runner sends the reset while you hold Shift) |
| `plugins.synth_cache` | synth plugin library, `/Plugins/synth.txt` and `installed.txt` of the FlexNVM bank | skipped | yes |
| `engines.picker` | Synth 1 selected with the keys, every engine of the Model column loaded (a repeat press on F1 rolls to the next one), its page against the goldens, then the synth pages, the patch library and Edit | yes, 18 engines with the synth plugins staged | yes, the engines of the card |
| `engines.cpu` | per engine one voice and four voices: `cpu reset`, hold pads, 3 readings. Metrics `engine.NAME.v1.avg_us` and so on | numbers are fixed (see below) | the real numbers |
| `project.open_play_stop` | opens a project three times (same hash each time, load time), play, stop, open refused while playing, CPU while playing. Writes (an open fills `/Workspace`) | yes | `--device-writes --project Projects/Name` |

A scenario that cannot run on the Mac says so with `ctx.device_only(...)`, the step is listed under "Not run here" in the report and the rest of the scenario goes on.
A step that needs a person is `ctx.manual(...)`: it never blocks an unattended run, with `--interactive` it asks.

## Goldens: record, compare, mask

Every checkpoint (`ctx.checkpoint(key)`) settles, reads the texts (normalised: the song clock, the pattern position, CPU and heap readouts and percentages become `#`),
reads the 48 tile CRCs and settles again. Then it is compared with `goldens/<mac|device>/<scenario>.json`:

- texts equal and at most `tol` of the 48 tiles (default 0) different, tiles listed in the golden's `mask` and texts in `volatile` excepted
- **first run records.** A checkpoint without a golden is recorded, then the scenario runs a second time over what it just recorded and everything
  that differs by itself is masked (the clock area, a readout, an animation phase). The report calls this status RECORDED and the log shows the masked tiles
- `--update-goldens` records everything again, `--no-record` makes a missing golden a failure (use it once the goldens are reviewed)
- a failing checkpoint keeps a PNG of the screen in `DIR/png/` and names the differing tiles as `r<row>c<col>` (100 x 80 px tiles, 6 rows of 8)
- goldens of a scenario are written only when the scenario passed

The Mac goldens are committed (`goldens/mac`). They depend on the staged plugin files (the list, the card logos, the engine pages): `goldens/mac/_stage.json` keeps their
digest and the run warns when another plugin set was staged (default source `~/prj/poly/build/cpu-adopt`, `--plg-dir` to change it, `--no-synth-plugins` leaves the 12 synth plugins out so only the 6 built-in engines are in the list).
The device goldens are recorded on the unit from a known good build (`goldens/device`, not committed until you reviewed them).
`golden_diff.py goldens/mac goldens/device` compares the two sets: texts have to be equal, differing tiles measure how far the Mac render is from the FT812
(the open fidelity question of ui_shot).

## Report and baseline

`DIR/suite.md` and `DIR/suite.json`: result, target and firmware build id, per scenario status, checks, checkpoints and seconds, failures with the failing checks, tiles and
texts, everything not run here with the reason, every metric next to its baseline value, and the regression diff.
Statuses: PASS, RECORDED (goldens were recorded), FAIL (a check failed), ERROR (the console stopped answering or a protocol error: the runner reconnects and goes on), SKIP (device only, manual,
a card without what the scenario needs, or it writes the card and `--device-writes` is missing), NOT RUN (the unit was lost earlier).

`baselines/<target>.json` is the saved baseline (`--save-baseline` writes it from a passing full run, `--capture` writes it when there is none, `--baseline FILE` picks another).
Regressions (exit status 1): a scenario that got worse (PASS to FAIL, SKIP to ERROR ...) or vanished, a CPU metric (`*_us`) more than `--cpu-tol` (10 percent and at least 50 us) above the baseline,
a timing metric more than `--time-tol` (25 percent and at least 0.5 s, device runs only, the Mac clock means nothing). Everything else that differs (new scenarios, a screen digest that changed because a golden was
updated, a counter) is listed under "Other changes".
Hard CPU limits: on a device any `max_us` above the block budget of 2902 us fails the scenario, `--budgets FILE` (`{"engine.CHIP.v4": {"max_us": N, "avg_us": N}}`) adds budgets per metric.
A first set of budgets is simply 15 percent over the first device baseline.

## What the scenarios rely on (found while writing them)

- **Synth part by keys.** `instr N` exists only in the ui_shot script mode. On the unit: hold Instr for 800 ms (the instrument popup shows), `enc -100` goes to the first entry (Synth 1) and `enc +1` per further part, release Instr. The encoder is read every 100 ms, so the scenario runs `idle` after each `enc` before it releases the key (an `enc` and an immediate release loses the turn).
- **Engines.** Synth page, F1 (Model) repeat press rolls to the next engine, the title reads `Synth 1 (NAME)`. The column wraps, the scenario stops when the first engine is back. A plugin engine is installed after the list rests 500 ms, the scenario waits 1.2 s per press on a device.
- **Voices.** One voice is pad 0 held, four voices are pads 0 to 3 held, on the Pattern page with the synth part selected. The Polyphony column of the part has to allow 4 (list values are not in `texts`, the scenario cannot read it).
- **Inserts page.** A press on the button of the column that is already selected is not a selection: on a plugin column it shows the next four parameters, on Mixer it steps the list. The helpers select the neutral column F4 first.
  `/Plugins/host.txt` is written on a load, on a rescan (entering the page does one) and on a project save, not on an unload or a mixer change: the scenarios leave the page and enter it again before reading. Slot states: 0 empty, 1 pending, 2 running, 3 missing, 4 failed, 5 safe, 6 tripped, 7 bypassed.
  In safe mode the page title reads `Safe mode: NAME crashed` instead of `Inserts` (it is still the last stop of the Master cycle).
- **History.** A Master page remembers its selected column and its sub page, Params and Performance remember their sub page, and after the Performance page the pattern editor comes back without its cell text (in the plain ui_shot front end too, not checked on a device).
  Scenarios whose screens depend on this start with `ctx.fresh_boot()` (a reset, skipped with `--no-reboot`) and `tap_until` never presses a page button when the page is already shown.
- **The lesson.** A screenshot (`shotcrc`, `shot`) rebuilds every control and restarts list animations. `ctx.capture()` settles (idle, a fixed wait, idle) before and after it, and idle alone is not enough on pages that keep redrawing.

## Mac front end limits

- `ui_shot --serve` has no audio: `cpu` returns fixed numbers (`UISHOT_FAKE_CPU=avg,max`), so the CPU metrics of the Mac baseline are the same for every engine. The flow is exercised, the numbers only mean something on the unit.
- The SampleLoad page crashes the host (SIGSEGV in `HFileSystem::createFilesList`, a null list entry), the page walk skips it on the Mac. Followup in `ui_shot`, no firmware involved as far as seen.
- Shift held while the unit boots (safe mode by key) cannot be simulated: the serve front end starts its clock after the 4 s key window. The stale-marker route (`plugins.safe_marker`) is the same host path and runs on both.
- Wall clock timings (`open_s.*`) are not compared on the Mac.

## Writing a scenario

    from lib import scenario

    @scenario('name.thing', tags=['x'], writes=False, reboots=False, sim=True)
    def thing(ctx):
        ctx.fresh_boot('why')                       # or ctx.reset_ui(): stopped, Pattern page, instrument 1, cursor on step 1
        ctx.tap('Master')                           # ctx.press / release / combo('Shift', 'F6') / enc(n) / pad_press(n)
        ctx.settle()                                # idle, wait, idle
        ctx.check(cond, 'text')                     # soft: recorded, the scenario goes on and ends FAIL
        ctx.require(cond, 'text')                   # hard: stops the scenario
        ctx.checkpoint('key', must=['Master 1/'])   # texts + tiles against the golden
        ctx.metric('name', value, 'unit')           # shows in the report and the baseline diff
        ctx.cpu_measure('cpu.thing')                # cpu reset, run, 3 readings, metrics .avg_us and .max_us
        if ctx.device_only('what'): return          # skipped on the Mac and listed

Put the file in this folder as `scn_*.py`, it is picked up by name. Keys are released when a scenario ends, whatever happened. Scenarios that write files on the card
declare `writes=True` and put back what they changed (see `Sidecar` in `scn_plugins.py`).

## Proof

`run_suite.sh selftest` (`selftest.py`): a first run records, a second passes against it, a golden with one wrong tile CRC fails and names the checkpoint and the tile (`r2c3`) and keeps the screenshot,
a wrong text fails, a missing golden fails with `--no-record`, the baseline diff is clean for the same run and flags a CPU 2x slower, a CPU budget and a scenario that went from PASS to FAIL, the committed goldens pass,
and the suite runs over a serial port (ui_shot behind a pty, the device code path: `--port auto`, skip rules, device-only steps).
