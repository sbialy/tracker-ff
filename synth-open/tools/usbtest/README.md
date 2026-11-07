# usbtest: runner and tests for the USB test console

Host tools, NOT firmware. The console itself is `core/source/UsbTest*.cpp`, the protocol and the gates are in `docs/usb-test-console.md`.

| File | What |
| :--- | :--- |
| `usbtest.py` | the runner: device (CDC port) or Mac build (`ui_shot --serve`), scripts, screenshot decode and compare, tile CRCs, CPU budgets, files |
| `scripts/*.ut` | smoke, files, project (open, play, stop), pages (tile CRCs per page) |
| `cpu_thresholds.json` | CPU budgets in microseconds per scenario (placeholders until a device baseline exists) |
| `usbtest_test.cpp` | host unit test of the console core against a fake host (ASan, UBSan) |
| `keys_inject_test.cpp` | host unit test of the input queue in the firmware's `Keys.cpp` |
| `e2e_extra.py` | gate, slow link, reboot, project open, serial code path through a pty |
| `run_usbtest_test.sh` | builds and runs the two unit tests |
| `run_usbtest_e2e.sh` | everything: unit tests, ui_shot build, runner against the Mac build, screenshot against the ui_shot render |

A copy lives in `~/prj/poly/tools/usbtest/` (with `png_cmp.py`). The branch is the source of truth.
