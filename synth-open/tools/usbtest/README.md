# usbtest: runner and tests for the USB test console

Host tools, NOT firmware. The console itself is `core/source/UsbTest*.cpp`, the protocol and the gates are in `docs/usb-test-console.md`.

| File | What |
| :--- | :--- |
| `usbtest.py` | the runner: device (CDC port) or Mac build (`ui_shot --serve`), scripts, screenshot decode and compare, tile CRCs, CPU budgets, files, `update FILE.ptf` (firmware update) |
| `ptfcheck.py` | the .ptf validator and name rules as the device applies them, plus the build id lookup (used by `update`) |
| `ptfgen.py` | synthetic .ptf images and a corpus of broken ones for the tests |
| `ptf_verdict.cpp`, `usbtest_flash_test.sh` | the console's C++ validator and `ptfcheck.py` judge the same corpus (and real images) and must agree |
| `flash_e2e.py` | `update` against the Mac build with a simulated bootloader: success, no-op upgrade and every failure case |
| `ab_run.py` | unattended A/B of two console firmware flavours on the unit: update, swap the matching plugin set, verify, suite, CPU sweep, report (see "A/B runs" below) |
| `ab_report.py` | statistics and the markdown of `ab_run.py` (pure functions) |
| `ab_e2e.py` | `ab_run.py` against the Mac build with a simulated bootloader and two fake build ids: ABAB, differences, crash marker, plugin set mismatch abort, stuck update, damaged card |
| `scripts/*.ut` | smoke, files, project (open, play, stop), pages (tile CRCs per page) |
| `cpu_thresholds.json` | CPU budgets in microseconds per scenario (placeholders until a device baseline exists) |
| `usbtest_test.cpp` | host unit test of the console core against a fake host (ASan, UBSan) |
| `keys_inject_test.cpp` | host unit test of the input queue in the firmware's `Keys.cpp` |
| `e2e_extra.py` | gate, slow link, reboot, project open, serial code path through a pty |
| `run_usbtest_test.sh` | builds and runs the two unit tests |
| `run_usbtest_e2e.sh` | everything: unit tests, ui_shot build, runner against the Mac build, screenshot against the ui_shot render, firmware update cases |

A copy lives in `~/prj/poly/tools/usbtest/` (with `png_cmp.py`, from before `update`). The branch is the source of truth.

## A/B runs (`ab_run.py`)

Two console images that differ in one build flag (AUTO-T fused maths, AUTO-TF `-ffp-contract=off`), each with its own plugin set, compared on the unit without a hand on it:

    ab_run.py --port auto --a build/release-1.2.0/auto-t --b build/release-1.2.0/auto-tf --out build/usbtest-ab

A flavour folder holds one `PolyendTracker_*.ptf` and the `.plg` and `.plgi` files (Master or Synth is read from the header of each `.plg`). Per phase (`A B`, with `--pairs K` `A B A B ...`): update, plugin swap
(`put`, CRC checked by the device, files the flavour lacks are removed), reset, then verify: build id and the CRC of every plugin file on the card against the staged copy. A mismatch aborts the run before
anything is measured. Then `suite.py` (`--suite first|all|none`, default first pair only, `project.*` left out because it changes the card) and the CPU sweep (`--sweep-repeats N`, each repeat starts with a reset and
walks the whole Model column, built in and plugin engines, 1 and 4 voices by default, pads 0 to v-1, the default patch of the engine). The crash markers are read after the update, the suite and the sweep. The run ends by
restoring `--restore a|b|none`.

Output `ab_report.md` and `ab_report.json`: CPU per engine A against B (min / avg / max over repeats and phases, delta, spread, clear or within noise, pair deltas), scenarios that differ between the flavours,
crash markers, the phases. Exit 0 clean, 1 findings, 2 setup, 3 aborted. `--check-only` validates both flavours and prints the plan.
The two `.ptf` files need different build ids (`BUILD_TAG`): the console reports nothing else about the image. `ab_e2e.py` proves all of it on the Mac (`UISHOT_FAKE_CPU_BUILD`, `UISHOT_FAKE_CPU_JITTER`,
`UISHOT_FAKE_FAULT_BUILD` give the two simulated builds different CPU and a fault record). `/Firmware` keeps both `.ptf` files afterwards, see Recovery in `docs/usb-test-console.md` before using the card to recover.
