# USB test console

A text console on the USB serial port of the original Tracker (MK66) that drives the real input path, reads the state of the
firmware, takes screenshots and moves files. It exists so that regression runs (pages, project load, CPU per scenario) can be
scripted from a Mac. The same console code also runs inside `ui_shot --serve` on the Mac, which is how the scripts are developed and
how the code is tested without a device. The Tracker+ and Tracker Mini have their own console (`platforms/imx`), this one is MK66 only.

## Gates

| Gate | Where | Effect when closed |
| :--- | :--- | :--- |
| Compile flag `ENABLE_DEV_CONSOLE` | `make original ... CUSTOM_DEFINES="-DENABLE_DEV_CONSOLE"` | Every hook compiles to nothing. The image is byte identical to one built without the change |
| `/.develop` on the card | `Tracker.cpp` (read once at boot, same marker as the Config screen CPU readout) | The console never reads or writes the port |
| DTR | a terminal must have the port open | No output is queued, held keys are released |

`CUSTOM_DEFINES` reaches `core/makefile` and `platforms/mk66/makefile`. `-DREBOOT_ON_FAULT` (test images only) makes the fault handler store
the stacked PC, LR, PSR and CFSR in RAM and reset the MCU, `markers` reports them after the reboot.
make does not track compiler flags: delete the core build dir of the SYNTH_TAG and `platforms/mk66/build-trim` (or `build`) when the flags change.

Example (AUTO test image, 12.3 toolchain):

    make original TOOLCHAIN=$PWD/agt/12.3.rel1/bin/arm-none-eabi- SYNTH_OPEN=1 TRIM=1 PLUG_HOST=1 PTF_TAG=AUTOT BUILD_TAG=$(git rev-parse --short=7 HEAD) \
      SYNTH_TAG=-ut SYNTH_ENGINES="ACD FAT VAP WTFM PERC CHIP" SYNTH_ENGINES_TAG=-EACD_FAT_VAP_WTFM_PERC_CHIP \
      CUSTOM_DEFINES="-DENABLE_DEV_CONSOLE -DREBOOT_ON_FAULT"

## Design rules

- Nothing is added to the audio interrupt. The console runs from `TrackerImpl::update` (main loop). `LOG()` is switched on by the flag and goes
  into a 4 KB ring that the main loop sends, so `usb_serial_write` (which can wait 70 ms for a host that does not read) is never called from an interrupt.
  Text from an interrupt is formatted by a small routine (no newlib printf, no stack), a line that finds the format buffer taken is dropped.
- Input is injected inside `Keys::update` (buttons and pads) and `EncoderImpl::update` (encoder), with the lines a real key event runs. Shift
  combinations (`isButtonPressed`) and the 333 ms hold repeat behave as with a finger. The power button is not injectable (sleep kills USB).
- At most 4 CDC packets are in flight (the pool of 30 is shared with MIDI). Commands are not read while the ring is nearly full.
- The screenshot freezes the display update from the snapshot to the end (it uses the controls RAM like Shift + power), then clears that RAM
  and refreshes the controls. Lists restart their animation, so wait a moment and run `idle` before comparing a second capture.
- No static constructors on SDRAM data. The scratch buffer is a plain NOINIT array.

## Protocol

Lines end with `\n` (`\r\n` is accepted), at most 127 characters, arguments are split on spaces and a double quoted part is one argument
(`open Projects "My Song"`). Every reply is a line that starts with `@`. Everything else is log text. A reply that follows an unfinished log line
starts on a new line. Binary parts have a header line `@row`, `@blk` and carry exactly the announced number of bytes followed by one `\n`.
While a command runs, only `ping help cpu mem status markers abort` are answered, others get `@err busy task=N`.

| Command | Reply |
| :--- | :--- |
| `ping` | `@tracker-dev original <build id> proto=1` |
| `help` | `@ok <command names>` |
| `cpu` / `cpu reset` | `@cpu min=<us> max=<us> avg=<us> last=<us> tmax=<us> block=2902` / `@ok cpu reset`. `@cpu none` for 25 ms after a reset |
| `mem` | `@mem free=<bytes of heap>` |
| `status` | `@status page=<n> name=<page> play=<0/1> fm=<file manager status> dirty=<0/1> idle=<0/1> up=<s> drop=<bytes lost>` |
| `key <name or id> press\|release\|tap [n]` | `@ok key <id> <action>`. Names: F1..F8 Params Performance File Config Master SampleLoad SamplePlay SampleEdit SampleRec Song Note Instr Vol Fx Pattern Play Copy Insert Up Delete Rec Shift Left Down Right. A tap holds 70 ms and waits 70 ms, n up to 10 |
| `pad <0..47> press\|release\|tap [n]` | `@ok pad <n> <action>` |
| `enc <delta>` | `@ok enc <delta>`. Delta (-200..200) goes to `mtInterface.potChange` with the next encoder reading |
| `idle` | `@idle` once the injected input is handled, the file manager is idle and the display queue is empty (or 600 ms passed without input). `@err idle timeout` after 15 s |
| `reboot` | `@ok reboot pending`, the MCU resets 250 ms later and USB re-enumerates |
| `shotcrc` | six lines `@shotcrc <row> <8 hex crc32>` (tiles of 100x80 px, B G R bytes) then `@ok shotcrc` |
| `shot` | `@shot 800 480 rle1`, 480 times `@row <y> <n>` + n bytes + `\n`, then `@ok shot crc=<crc32 of all B G R bytes> bytes=<n> rows=480`. `shot force` and `shotcrc force` work while playing |
| `texts` | `@t <control> <x> <y> <w> <h> <text>` for every shown control with a text, then `@ok texts <n>` |
| `open <path> <name> [force]` | `@ok open` when the project is loaded, `@err open fm=<status>`. Refuses while playing, loading or with unsaved changes unless `force` |
| `play song\|pattern [pos]`, `stop` | `@ok play song`, `@ok stop` |
| `peek [name]` | `@peek names: ...` / `@peek <name> <value>` (tempo is x100) |
| `hash project` / `hash file <path>` | `@hash project <crc32> <size>` (over `mtProject.values`) / `@hash file <crc32> <size>` |
| `markers` | `@markers srs=<RCM_SRS1 RCM_SRS0> fault=<0/1> pc= lr= psr= cfsr= safe=<0/1> loading=<0/1> reason=<text>` |
| `ls <dir>` | `@d <size> <name>` / `@f <size> <name>` lines, then `@ok ls <n>` |
| `get <path>` | `@get <size>`, blocks `@blk <n>` + n bytes + `\n` (512 at most), then `@end <size> <crc32>` |
| `put <path> <size> <crc32>` | `@ready`, then exactly `<size>` raw bytes, then `@ok put <size>` or `@err put crc <crc32>` (the file is removed). Refused while playing or loading. A stall of 3 s aborts and removes the file |
| `rm <path>` | `@ok rm`. Refused while playing |
| `abort` | cancels the running command |

CRC32 is the zlib one (poly 0xEDB88320). Paths may not contain `..`, `\`, `:` or control characters. Errors are `@err <reason>`.
`flash` (the upstream command that hands a .ptf to the bootloader) is not implemented, see "Not done".

### Screenshot codec `rle1`

A pixel is the FT812 ARGB8 word (bytes B G R A). Row y is coded as 32 bit little endian words of `pixel XOR the pixel above` (row 0 against
black). PackBits: a control byte below 0x80 is followed by control+1 literal words (1 to 128), a control byte of 0x80 or more by one word
that repeats (control & 0x7F) + 2 times (2 to 129). A row equal to the one above is `@row y 0` with no bytes. The decoder in
`usbtest.py` (`rle_decode`) and `rleDecode` in `UsbTestCore.cpp` are the references.

## Mac side

`synth-open/tools/usbtest/usbtest.py` (python3, stdlib only, pyserial is used when installed) talks to the device (`--port auto` finds
`/dev/cu.usbmodem*`, `USBTEST_PORT` overrides) or to the Mac build (`--sim build/ui-shot/ui_shot --sd DIR`). Scripts are in `scripts/*.ut`, the
format is in the docstring of `usbtest.py`. Screenshots are decoded to PNG and compared with `compare A.png B.png [percent]`, tile CRC
files with `shotcrc-expect`, CPU with `cpu-check NAME` against `cpu_thresholds.json` (median of 3 readings, microseconds).
A console that stops answering while the runner waits (a ping each second) is reported as a hang. `reboot` reconnects and rescans the port.

`ui_shot --serve SD_DIR [--no-develop] [--tx-rate BYTES]` runs the console and the real UI on stdin and stdout. Time is virtual, `wait`
in a script advances it. What is shared with the firmware: the console (`UsbTestCore.cpp`), its binding to the UI (`UsbTest.cpp`), `Keys.cpp`,
the display code and the snapshot calls. What is a stand-in: the serial port, the clock, the FT812 (`eve_model.cpp` now answers
`CMD_SNAPSHOT2`), `Profiler` (the `cpu` numbers are fixed, `UISHOT_FAKE_CPU=avg,max`), the encoder tick.

    synth-open/tools/usbtest/run_usbtest_test.sh     host unit tests
    synth-open/tools/usbtest/run_usbtest_e2e.sh      unit tests + runner against the Mac build + extra checks

## Not done

- `flash <name>`: it remounts the card, writes `/Firmware/_fwinfo` and resets into the bootloader. It cannot be exercised without a device,
  a wrong name or an interrupted copy leaves the unit in the bootloader and it is the one command that can make a test run strand the
  hardware. Use `put` and flash from Config > Firmware.
- `render` and `wavstat` (phase 5), the watchdog build, a Config menu switch for the console, SysEx transport.
- `texts` lists controls that carry a text. List contents (file browsers, instrument lists) and the grid editors are not in it.
