# tracker-ff

Unofficial community firmware for the **original Polyend Tracker** (the MK66 model, Cortex-M4F at 180 MHz). It is a fork of Polyend's open source firmware [polyend/tracker-ce](https://github.com/polyend/tracker-ce) (Tracker Community Edition) and adds synth engines to the original Tracker. Version 1.2.0. The name FF is the max value 0xFF.

tracker-ff is not affiliated with, endorsed by or supported by Polyend. It is free and non-commercial.

## What it adds

Upstream CE has synth engines only on the Tracker+ and the Mini, because Polyend's engine library is closed and built for their Cortex-M7 chip. tracker-ff replaces that layer on the original with an open one in [`synth-open/`](synth-open/README.md), so the original gets synth slots too. It never links Polyend's closed `libPolySynth.a` or `libPolyDSP.a`.

- **Synth engines, built in and as plugins.** The image has six built in: ACD, FAT, VAP, WTFM, PERC and CHIP. The other twelve are synth plugins (`.plg` with a `.plgi` logo in `/Plugins/Synth/` on the card): JU6, PHZ, FM4, D-Type, CS-Type, A-Type, T-Type, 8-Type, Hand-Type, B-Type, Arc-Type and PLK. A plugin engine is installed into the data flash when a part picks it or a project needs it, and a part whose engine is not on the card shows Missing and keeps its settings. The library takes as many synth plugins as the card holds (up to 256 files, file names up to 23 characters). Most engines come from [OpenTracker](https://github.com/sandlbn/polyend-tracker-re) and [opentracker-synths](https://github.com/opentracker-fw/opentracker-synths) through a shim that hosts their engine interface. CHIP is Polyend's own open Chiptune voice. PHZ is a phase distortion engine and PLK a Karplus-Strong engine, both written for this project.
- **Polyphony and a governor.** Up to 4 voices per synth instrument, a shared voice pool (default 4, at most 8) and OpenTracker's governor, which drops voices when the audio block load gets too high.
- **`.patch` files and pitch bend.** Patches load and save from `Patches/<ENGINE>/` on the SD card, including Polyend's factory patches. Pitch bend works from MIDI and from the Micro-tune FX.
- **Patch slots and the Patch FX.** Every synth part keeps a bank of 16 patches in the project. The `O - Patch` step FX switches the part to one of them at a step, so a pattern can change the sound of an instrument. Slot 0 is the stock `<N>.patch`, so stock firmware opens the song with that sound.
- **Lower idle audio CPU.** Synth parts with nothing sounding are skipped in the audio graph, idle voices send no data, the reverb and short delay sleep when silent, mixers skip empty inputs and one reverb buffer lives in internal SRAM. The details and the bench that checks the output stays bit identical are in `synth-open/README.md`.
- **Master insert plugins.** Two insert slots process the master bus after the track mixers and before the limiter. Seven inserts come with the release (`.plg` in `/Plugins/Master/`): Dirt, Tape, Lo-Fi, Console, Tube, BBD and Phaser. The Master button cycles Master, Track Mixer, Global Mixer and the Inserts page, where you pick the plugin of each slot and set its parameters. The settings are saved with the project. The mixer stage in front of the inserts is Hard Clip, Soft Clip or Soft Knee. The picker lists every insert on the card, up to 272.
- **Synth Automation.** Each patch has four automation slots. A slot is an LFO or an envelope and drives one engine parameter or one macro. The Params button cycles Synth Parameters, Synth Macros and Synth Automation. The automation is saved in the `.patch` file.
- **Sample editor effects.** Fourteen `.ptfx` effects for `/Effects/`, among them Transient Shaper, Tape Saturation, Lo-Fi Chip, Dirt Drive, BBD Chorus, Dub Echo and Reverb.
- **Workflow.** Add to Empty Slot in the Sample Loader, per-track MIDI-in for tracks 1 to 8, RAM or streaming mode per sample, and a project load that ends in the pattern editor.
- **TRIM flash budget.** `TRIM=1` links with newlib-nano and builds the UI, the platform code and some cold code at `-Os`. With six engines built in and the rest on the card the image leaves about 40 KB of the original's 1,044,480 bytes of flash free. The last 4 KB sector stays free.
- **Fixes.** Three out of bounds writes to `label[8]` arrays in the UI are fixed in all builds. Slot names of the Patch FX bank survive a slot change and a project reload, and a knob edit made while a song with the Patch FX plays is kept. A patch file, plugin settings file or project copy that fails on the card is reported and the old files stay. An unreadable folder on the card no longer breaks the file lists, a corrupt `.it` module cannot overflow the stack and the main loop and the display use less stack. A post-build check (`synth-open/tools/check_static_init.py`) refuses an image whose static constructors touch the SDRAM, which hangs the boot.

Not available: Polyend's DIRT and Shaping engines (they exist only in the closed library). A project that uses an engine this firmware does not know gets that part reset to defaults on load.

## Hardware

The original Polyend Tracker (MK66) only. The Tracker+ and the Mini are not built or tested in this fork. Their code is still in the tree from upstream. For those devices use [upstream](https://github.com/polyend/tracker-ce).

## Flash

1. Copy `PolyendTracker_FF_1.2.0.ptf` to the `/Firmware/` folder on the microSD card. Keep exactly one `.ptf` there.
2. Copy `Plugins/Synth/` to `/Plugins/Synth/` and `Plugins/Master/` to `/Plugins/Master/` on the card. Optional: copy the `.ptfx` files to `/Effects/`. The release zip holds all of it.
3. On the Tracker open **Config > Firmware > Firmware update**, pick the file and confirm.

Do not rename the file. The original's bootloader lists only names that start with `PolyendTrac` and are at most 31 characters long. Back up your `Projects/` folder first, because upstream CE saves patterns in a newer format that older firmware cannot read. If a build does not boot, the emergency recovery steps in [upstream's README](docs/upstream-README.md#4-flashing-on-hardware--emergency-recovery) apply.

## Build

You need `make`, `python3` and the Arm GNU Toolchain 12.3.rel1 (the release compiler, in `./agt/12.3.rel1/` or pass `TOOLCHAIN=/path/to/arm-none-eabi-`). Build only the original, `make all` also builds the Tracker+ and the Mini.

```bash
# the release image, build/PolyendTracker_FF_1.2.0.ptf
make original SYNTH_OPEN=1 TRIM=1 PLUG_HOST=1 SYNTH_ENGINES="ACD FAT VAP WTFM PERC CHIP" \
  SYNTH_OPT_ENGINES="-O2 -ffp-contract=off" SYNTH_OPT_FAT="-O2 -ffp-contract=off" \
  SYNTH_OPT_FM4="-O3 -ffp-contract=off" SYNTH_OPT_BTYPE="-O3 -ffp-contract=off"
```

The plugins are not built from this tree. They come from the module repositories pinned in `engines/external.lock.json`, see [`engines/README.md`](engines/README.md). `synth-open/tools/package_release.sh` puts the image, the plugins, the licenses and a checksum list into a folder and a zip.

`SYNTH_ENGINES` picks any subset of the engines. Without it the build has only ACD FAT VAP WTFM. `make synth-engines` prints the flash cost of each engine. The build stops if the image would pass the flash cap, and `build/BUILD.txt` lists the engines and sizes. Plain `make original` builds the stock upstream firmware without the synth layer. Add `BUILD_TAG=<hash>` to set the build id shown on the Config screen. `make clean` at the top level removes everything in `build/`, so keep copies of the images you want.

Upstream's build notes (toolchain downloads, version helpers, `.ptfx` plugins) are in [`docs/upstream-README.md`](docs/upstream-README.md). The technical description of the synth layer and the build is [`synth-open/README.md`](synth-open/README.md).

## License and credits

- [`LICENSE`](LICENSE): MIT with upstream's Wholesale Commercial Clone Restriction. You can use, share and modify the firmware for free. You may not sell builds or clones of it, and you may not use Polyend's marks in a commercial product. Ship `LICENSE` and `NOTICE.md` with any build you pass on.
- [`NOTICE.md`](NOTICE.md) and [`licenses/`](licenses/): third party components and their license texts, including OpenTracker (Apache-2.0), opentracker-synths (MIT) and the Mutable Instruments reverb (MIT).
- tracker-ff is a fork of [polyend/tracker-ce](https://github.com/polyend/tracker-ce) and builds on the work of Polyend, OpenTracker and opentracker-synths. "Polyend" and "Polyend Tracker" are Polyend's names. They appear here only to say which device this runs on and because the original's bootloader needs the file name prefix.
- The project is free and non-commercial.
