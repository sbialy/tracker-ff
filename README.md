# tracker-ff

Unofficial community firmware for the **original Polyend Tracker** (the MK66 model, Cortex-M4F at 180 MHz). It is a fork of Polyend's open source firmware [polyend/tracker-ce](https://github.com/polyend/tracker-ce) (Tracker Community Edition) and adds synth engines to the original Tracker. Version 1.1.0. The name FF is the max value 0xFF.

tracker-ff is not affiliated with, endorsed by or supported by Polyend. It is free and non-commercial.

## What it adds

Upstream CE has synth engines only on the Tracker+ and the Mini, because Polyend's engine library is closed and built for their Cortex-M7 chip. tracker-ff replaces that layer on the original with an open one in [`synth-open/`](synth-open/README.md), so the original gets synth slots too. It never links Polyend's closed `libPolySynth.a` or `libPolyDSP.a`.

- **Synth engines, built in and as plugins.** The PLUGS image has six built in: ACD, FAT, VAP, WTFM, PERC and CHIP. The other twelve are synth plugins (`.plg` in `/Plugins/Synth/` on the card): JU6, PHZ, FM4, D-Type, CS-Type, A-Type, T-Type, 8-Type, Hand-Type, B-Type, Arc-Type and PLK. A plugin engine is installed into the data flash when a part picks it, and a part whose engine is not there shows Missing and keeps its settings. Most engines come from [OpenTracker](https://github.com/sandlbn/polyend-tracker-re) and [opentracker-synths](https://github.com/opentracker-fw/opentracker-synths) through a shim that hosts their engine interface. CHIP is Polyend's own open Chiptune voice. PHZ is a phase distortion engine and PLK a Karplus-Strong engine, both written for this project. The 1.1.0 image (FF) has 16 engines built in instead.
- **Polyphony and a governor.** Up to 4 voices per synth instrument, a shared voice pool (default 4, at most 8) and OpenTracker's governor, which drops voices when the audio block load gets too high.
- **`.patch` files and pitch bend.** Patches load and save from `Patches/<ENGINE>/` on the SD card, including Polyend's factory patches. Pitch bend works from MIDI and from the Micro-tune FX.
- **Patch slots and the Patch FX.** Every synth part keeps a bank of 16 patches in the project. The `O - Patch` step FX switches the part to one of them at a step, so a pattern can change the sound of an instrument. Slot 0 is the stock `<N>.patch`, so stock firmware opens the song with that sound.
- **Lower idle audio CPU.** Synth parts with nothing sounding are skipped in the audio graph, idle voices send no data, the reverb and short delay sleep when silent, mixers skip empty inputs and one reverb buffer lives in internal SRAM. The details and the bench that checks the output stays bit identical are in `synth-open/README.md`.
- **Dirt master insert.** OpenTracker's DIRT (drive, fold, bitcrush) runs live on the master bus before the limiter. Pick `Dirt Soft`, `Dirt Hard`, `Dirt Fold`, `Dirt Asym` or `Dirt Crush` in the Saturation entry of the master screen. It costs 1.6 to 2.6 points of the audio block (emulated) and nothing in silence, see `synth-open/README.md`.
- **TRIM flash budget.** `TRIM=1` links with newlib-nano and builds the UI, the platform code and some cold code at `-Os`. It is what made 16 built-in engines fit the original's flash in 1.1.0 (that image ends at 0xFBB90, 1,031,056 bytes of 1,044,480 allowed). The PLUGS image also uses it and ends near 0xE7E5C with six built-in engines. The last 4 KB sector stays free.
- **Fixes.** Three out of bounds writes to `label[8]` arrays in the UI are fixed in all builds. Slot names of the Patch FX bank survive a slot change and a project reload. Changing a part's engine and back keeps the slot 0 patch and its name, and the names of slots 1 to 15. A knob edit made while a song with the Patch FX plays is kept, and a save writes one consistent patch. A patch file or project copy that fails on the card is reported and the old files stay (the edits are not dropped, no saved slot is deleted). Patch files are read and written through an SDRAM scratch buffer and the bank lives in SDRAM, so loading a project after saving a slot no longer hangs or reports "error loading slot". A post-build check (`synth-open/tools/check_static_init.py`) refuses an image whose static constructors touch the SDRAM, which hangs the boot.

Not available: Polyend's DIRT and Shaping engines (they exist only in the closed library). A project that uses an engine missing from the build gets that part reset to defaults on load.

## Hardware

The original Polyend Tracker (MK66) only. The Tracker+ and the Mini are not built or tested in this fork. Their code is still in the tree from upstream. For those devices use [upstream](https://github.com/polyend/tracker-ce).

## Flash

1. Copy `PolyendTracker_FF_1.1.0.ptf` to the `/Firmware/` folder on the microSD card.
2. On the Tracker open **Config > Firmware > Firmware update**, pick the file and confirm.

Do not rename the file. The original's bootloader lists only names that start with `PolyendTrac` and are at most 31 characters long. Back up your `Projects/` folder first, because upstream CE saves patterns in a newer format that older firmware cannot read. If a build does not boot, the emergency recovery steps in [upstream's README](docs/upstream-README.md#4-flashing-on-hardware--emergency-recovery) apply.

## Build

You need `make`, `python3` and the GNU Arm Embedded Toolchain 10.3-2021.10. Put it in `./agt/10.3/` or pass `TOOLCHAIN=/path/to/arm-none-eabi-`. Build only the original, `make all` also builds the Tracker+ and the Mini.

```bash
# the 16 engine release image, build/PolyendTracker_FF_1.1.0.ptf
make original SYNTH_OPEN=1 TRIM=1 \
  SYNTH_ENGINES="ACD FAT VAP WTFM PERC CHIP JU6 PHZ FM4 DTYPE CSTYPE ATYPE TTYPE 8TYPE BTYPE ARCTYPE"

# folder and zip with the .ptf, LICENSE, NOTICE.md and every third party license text, in build/release/
make package-release SYNTH_OPEN=1 TRIM=1
```

`SYNTH_ENGINES` picks any subset of the engines. Without it the build has only ACD FAT VAP WTFM. `make synth-engines` prints the flash cost of each engine. The build stops if the image would pass the flash cap, and `build/BUILD.txt` lists the engines and sizes. Plain `make original` builds the stock upstream firmware without the synth layer. Add `BUILD_TAG=<hash>` to set the build id shown on the Config screen. `make clean` at the top level removes everything in `build/`, so keep copies of the images you want.

Upstream's build notes (toolchain downloads, version helpers, `.ptfx` plugins) are in [`docs/upstream-README.md`](docs/upstream-README.md). The technical description of the synth layer and the build is [`synth-open/README.md`](synth-open/README.md).

## License and credits

- [`LICENSE`](LICENSE): MIT with upstream's Wholesale Commercial Clone Restriction. You can use, share and modify the firmware for free. You may not sell builds or clones of it, and you may not use Polyend's marks in a commercial product. Ship `LICENSE` and `NOTICE.md` with any build you pass on.
- [`NOTICE.md`](NOTICE.md) and [`licenses/`](licenses/): third party components and their license texts, including OpenTracker (Apache-2.0), opentracker-synths (MIT) and the Mutable Instruments reverb (MIT).
- tracker-ff is a fork of [polyend/tracker-ce](https://github.com/polyend/tracker-ce) and builds on the work of Polyend, OpenTracker and opentracker-synths. "Polyend" and "Polyend Tracker" are Polyend's names. They appear here only to say which device this runs on and because the original's bootloader needs the file name prefix.
- The project is free and non-commercial.
