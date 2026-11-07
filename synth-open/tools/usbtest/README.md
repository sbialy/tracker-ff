# usbtest: runner and tests for the USB test console

Host tools, NOT firmware. The console itself is `core/source/UsbTest*.cpp`, the protocol and the gates are in `docs/usb-test-console.md`.

| File | What |
| :--- | :--- |
| `usbtest.py` | the runner: device (CDC port) or Mac build (`ui_shot --serve`), scripts, screenshot decode and compare, tile CRCs, CPU budgets, files, `update FILE.ptf` (firmware update) |
| `ptfcheck.py` | the .ptf validator and name rules as the device applies them, plus the build id lookup (used by `update`) |
| `ptfgen.py` | synthetic .ptf images and a corpus of broken ones for the tests |
| `ptf_verdict.cpp`, `usbtest_flash_test.sh` | the console's C++ validator and `ptfcheck.py` judge the same corpus (and real images) and must agree |
| `flash_e2e.py` | `update` against the Mac build with a simulated bootloader: success, no-op upgrade and every failure case |
| `scripts/*.ut` | smoke, files, project (open, play, stop), pages (tile CRCs per page) |
| `cpu_thresholds.json` | CPU budgets in microseconds per scenario (placeholders until a device baseline exists) |
| `usbtest_test.cpp` | host unit test of the console core against a fake host (ASan, UBSan) |
| `keys_inject_test.cpp` | host unit test of the input queue in the firmware's `Keys.cpp` |
| `e2e_extra.py` | gate, slow link, reboot, project open, serial code path through a pty |
| `run_usbtest_test.sh` | builds and runs the two unit tests |
| `run_usbtest_e2e.sh` | everything: unit tests, ui_shot build, runner against the Mac build, screenshot against the ui_shot render, firmware update cases |

A copy lives in `~/prj/poly/tools/usbtest/` (with `png_cmp.py`, from before `update`). The branch is the source of truth.
