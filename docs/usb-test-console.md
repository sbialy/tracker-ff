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

## Log levels

`core/source/log.h` has two levels. `LOGE()` / `LOGEF()` are the essential sites: errors, file manager errors, plugin load and failure, safe mode and its
reason, project open and loaded, SD card and USB storage mode, stream rebind and export open failures. They are live with `ENABLE_DEV_CONSOLE`.
`LOG()` / `LOGF()` are the verbose sites (dumps, tables, per step traces) and stay off unless `-DLOG_ALL` (or `ENABLE_LOG`, `DEBUG`) is given:

    CUSTOM_DEFINES="-DENABLE_DEV_CONSOLE -DREBOOT_ON_FAULT -DLOG_ALL"

Add a new site as `LOGE` only if a test run needs it, every site costs flash in the test image. Messages are short tags with numbers
(`plg1 Dirt ok`, `syn3 att 0`, `FM err op 7 step 2 e 5`, `open Projects/Song`, `loaded /Workspace 1`, `rebind 3 failed <path>`).
Measured on AUTO (12.3): the console is +13.9 KB (code 11.7 KB, mostly the commands and their reply strings), all verbose `LOG()` sites together only 0.9 KB,
`LOG_ALL` adds 0.5 KB over the essential level. `LOGEF` needs at least one argument, use `LOGE` for a plain text.

## Design rules

- Nothing is added to the audio interrupt. The console runs from `TrackerImpl::update` (main loop). `LOGE()` (essential log level) is switched on by the flag and goes
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
| `crash` | only in an image with `-DREBOOT_ON_FAULT`: `@ok crash pending`, 250 ms later an undefined instruction. The fault handler stores PC, LR, PSR and CFSR and resets, `markers` shows them after the reboot |
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
| `fwcheck` | `@ok fwinfo free=<bytes or unknown> limit=0xff000 namemax=31 filemax=<bytes>` |
| `fwcheck <name>` | read only: re-reads `/Firmware/<name>`, runs the .ptf validator. `@ok fwcheck size= crc= top= sp= reset= listed=<0/1>` or `@err fwcheck ptf <reason> record=<n>` |
| `fwpromote <tmp> <name> 0x<crc32>` | re-reads `/Firmware/<tmp>`, validates it, compares its CRC32 with the token, then renames it to `/Firmware/<name>` (an old file of that name goes first). `@ok fwpromote <name> size= crc=` |
| `flash <name> 0x<crc32>` | the hand-off to the bootloader, see "Firmware update". `@ok flash handoff <name> size= crc=` and about 250 ms later the unit resets |
| `abort` | cancels the running command |

CRC32 is the zlib one (poly 0xEDB88320). Paths may not contain `..`, `\`, `:` or control characters. Errors are `@err <reason>`.
`flash` takes the same path as Config > Firmware, see "Firmware update". The help line lists `fwcheck fwpromote flash` as well.

### Screenshot codec `rle1`

A pixel is the FT812 ARGB8 word (bytes B G R A). Row y is coded as 32 bit little endian words of `pixel XOR the pixel above` (row 0 against
black). PackBits: a control byte below 0x80 is followed by control+1 literal words (1 to 128), a control byte of 0x80 or more by one word
that repeats (control & 0x7F) + 2 times (2 to 129). A row equal to the one above is `@row y 0` with no bytes. The decoder in
`usbtest.py` (`rle_decode`) and `rleDecode` in `UsbTestCore.cpp` are the references.

## Firmware update

`usbtest.py update FILE.ptf` replaces the firmware over the console, no card swap. It exists for repeated test cycles (flash an image, run the
scripts, flash the next). It is the one thing the console does that can leave the unit in the bootloader, so every step has a check and the
hand-off needs an explicit confirm token.

    python3 -I usbtest.py --port auto update PolyendTracker_AUTOT_1.1.0.ptf
    options (before the word update): --expect-build ID  --no-build-check  --ptf-name NAME  --update-timeout S  --stage-only

| Step | Where | What is checked |
| :--- | :--- | :--- |
| 1 | Mac, `ptfcheck.py` | The file is an Intel HEX image the MK66 can take (rules below), the name is legal, the expected build id is known |
| 2 | device | `ping`, `status` (not playing, not loading), `fwcheck` (free space at least the file plus 1 MiB, or unknown) |
| 3 | device | `put /Firmware/_usbtest.tmp` with size and CRC32. The name is not a .ptf, so Config > Firmware never lists it. A copy that stalls for 3 s or ends with a wrong CRC is deleted by the device |
| 4 | device | `fwpromote`: reads the temp file back from the card, validates it, compares the CRC32 with the token, renames it into `/Firmware/<name>` |
| 5 | device | `flash <name> 0x<crc32>`: reads the final file again, validates it, compares the token. Only then `@ok flash handoff`, 250 ms later `runBootloader(name)` (`PlatformImpl.cpp:97`, what Config > Firmware calls: writes `/Firmware/_fwinfo`, triggers the bootloader). The bootloader is not touched in any other way |
| 6 | Mac | Waits for the link to drop (no drop in 30 s is a failure), then for the CDC node and a `ping` answer (240 s by default) |
| 7 | Mac | `build` of the new `ping` must equal the build id inside the .ptf and the uptime must have restarted. Prints PASS or FAIL and the time taken |

What the device refuses (each case leaves the bootloader alone, `usbtest_test.cpp` and `flash_e2e.py` run all of them):

- no token, a token that is not `0x` plus eight hex digits, a token that differs from the CRC32 of the file now on the card (a copy that changed after it was checked)
- while playing, rendering (`Sequencer::isRenderMode`) or while the file manager is busy, also if playback starts in the 250 ms before the hand-off (the hand-off is cancelled), also on `abort` or when the USB link drops in that window
- a name that is longer than 31 characters (the bootloader reads it into `char[32]`), shorter than 15, does not start with `PolyendTrac`, does not end in `.ptf` or has a character outside `A-Za-z0-9._-` (the same filter as Config > Firmware, `SD.cpp:590`)
- a file under 4 KiB or over 6 MiB, or one the validator rejects

The validator (`core/source/UsbTestFlash.cpp`, mirrored for the Mac in `ptfcheck.py`, `usbtest_flash_test.sh` makes both judge the same corpus and the real images) is the
build time `check_ptf.py` as a streaming parser: Intel HEX record structure and checksums, only record types 00, 01 and 02 (stock images use nothing else, type 04 means the image crossed
1 MiB), an end of file record and nothing after it, every data byte below 0xFF000, the image starts at address 0, the flash configuration field at 0x400 equals the stock one (`FF` x 12, `DE F9 FF FF`:
a wrong value can lock the MCU), the initial stack pointer is in the MK66 SRAM (0x1FFF0000 to 0x20030000), the reset vector is a Thumb address inside the image. A Tracker+ or Mini image fails here
(the stack pointer, the start address or the flash configuration are not the MK66 ones).

Limits: the console cannot know what the bootloader itself accepts (closed source), the checks are those of the build tools. The free space query is FatFs `f_getfree`, on a card whose FSINFO is stale it scans the FAT once
(seconds, main loop), and a result of 0 is reported as `unknown`. The build id is found in the .ptf as the single NUL delimited seven digit hex string (`BUILD_TAG` of a normal build). A pinned tag such as `stk` or
`ut00001` cannot be found, give `--expect-build ID` (or `--no-build-check`, the restart is still checked).

`--stage-only` stops after step 4 and prints the confirm command, so the file can be inspected and flashed by hand (`usbtest.py cmd flash NAME 0xCRC`).

On the Mac the same flow runs against `ui_shot --serve` with a simulated bootloader: `runBootloader` is replaced by a stub that writes `/Firmware/_fwinfo` like the real one, appends the call to
`$UISHOT_BOOTLOADER_LOG` and ends the process with exit code 77 (a reset). The next start installs the named image and reports its build id. `UISHOT_BOOTLOADER_STUCK=1` never comes back,
`UISHOT_BOOTLOADER_FAIL=1` lets the hand-off return, `UISHOT_FREE_BYTES=N` sets the free space. `flash_e2e.py` drives all of it.

### Recovery

A hand-off that does not end in a running console (the build id is wrong, the node never returns, the bootloader loops from 0 to 100 %, the new image hangs) is recovered through the card. The bootloader is a separate
chip that no console command writes to (`docs/upstream-README.md`, "Emergency Recovery"), a failed update does not damage it:

1. Power the unit off (hold the power button) and take out the SD card, or use a card reader.
2. In `/Firmware` keep exactly one `.ptf`: a known good image for the unit (a release image or the one that ran before). Move everything else out, the staged image and any other `.ptf` too, the emergency pick
   takes one of the files it finds. `_usbtest.tmp` and `_fwinfo` can stay.
3. Put the card in, power the unit on. If it shows the UI use Config > Firmware > select the file (the same hand-off the console makes).
4. If it does not boot far enough for that: press the recessed Reset / Emergency Bootloader pinhole button on the bottom of the unit with a paperclip, the bootloader flashes the `.ptf` from `/Firmware`.
5. Remove `/.develop` and flash a release image to get rid of the console.

Do not run an update on a nearly empty battery and do not unplug USB between step 5 of the flow and the console coming back (flashing a full image takes a minute or more).

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
    synth-open/tools/usbtest/run_usbtest_e2e.sh      unit tests + runner against the Mac build + extra checks + firmware update cases
    synth-open/tools/usbtest/usbtest_flash_test.sh   the console's .ptf validator against ptfcheck.py on a generated corpus and real images
    synth-open/tools/usbtest/flash_e2e.py            usbtest.py update against the Mac build with a simulated bootloader, success and failure cases

## Not done

- `render` and `wavstat` (phase 5), the watchdog build, a Config menu switch for the console, SysEx transport.
- `texts` lists controls that carry a text. List contents (file browsers, instrument lists) and the grid editors are not in it.
