# tracker-ff

Unofficial community firmware for the **original Polyend Tracker** (the MK66 model, Cortex-M4F at 180 MHz), the **Tracker+** and the **Tracker Mini**. It is a fork of Polyend's open source firmware [polyend/tracker-ce](https://github.com/polyend/tracker-ce) (Tracker Community Edition) and adds synth engines to the original Tracker. Version 2.0.0. The name FF is the max value 0xFF.

tracker-ff is not affiliated with, endorsed by or supported by Polyend. It is free and non-commercial.

## What it adds

Upstream CE has synth engines only on the Tracker+ and the Mini, because Polyend's engine library is closed and built for their Cortex-M7 chip. tracker-ff replaces that layer on the original with an open one in [`synth-open/`](synth-open/README.md), so the original gets synth slots too. It never links Polyend's closed `libPolySynth.a` or `libPolyDSP.a`.

- **Synth engines, built in and as plugins.** The image has five built in: ACD, FAT, VAP, WTFM and PERC. The other fourteen are synth plugins (`.plg` with a `.plgi` logo in `/Plugins/Synth/` on the card): CHIP, JU6, PHZ, FM4, D-Type, CS-Type, A-Type, T-Type, 8-Type, Hand-Type, B-Type, Arc-Type, PLK and ALCO. A plugin engine is installed into the data flash when a part picks it or a project needs it and a part whose engine is not on the card shows Missing and keeps its settings. The library takes as many synth plugins as the card holds (up to 256 files, file names up to 23 characters). Most engines come from [OpenTracker](https://github.com/sandlbn/polyend-tracker-re) and [opentracker-synths](https://github.com/opentracker-fw/opentracker-synths) through a shim that hosts their engine interface. CHIP is Polyend's own open Chiptune voice, a plugin since 1.3.0 (same project and `.patch` numbers as before). PHZ is a phase distortion engine, PLK a Karplus-Strong engine and ALCO a two oscillator algorithm synth with 20 algorithms, a chord mode and a wave mode that reads single cycle waves from `/Waves` on the card, all written for this project. `alco.plg` is new in 1.4.0. The release zip carries a curated `/Waves` folder of 143 [AKWF-FREE](https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE) waves (CC0 1.0, Kristoffer Ekstrand) for it.
- **Polyphony and a governor.** Up to 4 voices per synth instrument (up to 8 on the Synth play page), a shared pool of 4 voices (the Polyphony settings of the three parts add up to at most 8) and OpenTracker's governor, which drops voices when the audio block load gets too high. A new note that would push the block over its time budget starts at the start of the next block instead, at most one block late and never dropped and a voice that a new note takes over fades out in the same block. Config shows the overrun count next to the CPU maximum and every Play resets both.
- **Synth order.** Shift + F1 on the Model column of the Synth screen opens an edit mode for the engine list: Shift + Up and Down move an engine, Delete hides it and Insert shows it again. The order is kept in `/Plugins/synth-order.txt` on the card. Projects and `.patch` files are not affected and a hidden engine that the open project plays still shows at the top. Stepping through the list waits 1 s on the engine before it is picked, so scrolling does not install or load every engine on the way. The user guide is [`docs/synth_order.md`](docs/synth_order.md).
- **`.patch` files and pitch bend.** Patches load and save from `Patches/<ENGINE>/` on the SD card, including Polyend's factory patches. Pitch bend works from MIDI and from the Micro-tune FX.
- **Patch slots and the Patch FX.** Every synth part keeps a bank of 16 patches in the project. The `O - Patch` step FX switches the part to one of them at a step, so a pattern can change the sound of an instrument. Slot 0 is the stock `<N>.patch`, so stock firmware opens the song with that sound.
- **Lower idle audio CPU.** Synth parts with nothing sounding are skipped in the audio graph, idle voices send no data, the reverb and short delay sleep when silent, mixers skip empty inputs and one reverb buffer lives in internal SRAM. The details and the bench that checks the output stays bit identical are in `synth-open/README.md`.
- **Master insert plugins.** Two insert slots process the master bus after the track mixers and before the limiter. Eight inserts come with the release (`.plg` in `/Plugins/Master/`): Dirt, Tape, Lo-Fi, Console, Tube, BBD, Phaser and Glue. The Master button cycles Master, Track Mixer, Global Mixer and the Inserts page, where you pick the plugin of each slot and set its parameters. The settings are saved with the project. The mixer stage in front of the inserts is Hard Clip, Soft Clip or Soft Knee. The picker lists every insert on the card, up to 272.
- **Features page.** Config > Features lists the options that need a restart or a switch of their own, each with a State row and its own page and keeps the settings in `/Features/` on the card. **USB audio** is built in: switched on, the unit shows up as an audio input on a computer after the next start, switched off it presents only the stock USB interfaces. Its Audio mode, Stereo (the master mix) or 8+3 mono (tracks 1 to 8 dry before pan, synth parts 1 to 3, 11 inputs), is in Config > USB (listed while USB audio or USB drive is on) and takes effect at the next start. **Chord names** switches the MIDI Chord cells of the pattern grid between short names such as `m7` and the interval digits. **Reset config** clears `/Features/`.
- **Songs, patterns and the project (1.4.0).** Parts build a song from source patterns, with a Part view on the Song screen (Shift + F8), play and edit while a Part plays and Performance Insert. The Project Settings page (File screen, F7) holds `4 FX Slots` (two more FX slots on every step, not CE compatible), `Macro depth`, `Strum volume`, `Random seed` and `Scale snap`. The Snap FX sets the scale snap of a track from a step on. The user guide is [`docs/scale_snap.md`](docs/scale_snap.md). The settings that the `.mt` has no room for live in `project.mtff` next to it. New step FX are Chance FX, Tape (stop and start in one FX) and Morph. The full list is in [`CHANGELOG.md`](CHANGELOG.md).
- **Synth play page and USB (1.4.0).** Shift + Perform plays one synth part or an A/B pair with a Morph, from a keyboard on USB or DIN and REC resamples the take into a sample slot. USB audio has an 8+3 mono mode (tracks 1 to 8 and synth parts 1 to 3 as 11 inputs). Config > Features > USB drive shows the SD card on the computer, off until you switch it on.
- **MOD and IT and the effects, on the card (1.4.0).** MOD import, IT import and MOD/IT export live in `Plugins/Ext/extras.plg` (called `modit.plg` in 1.4.0, on the original Tracker it also carries the custom theme loader and the factory self test) and the nine Sample Editor effects (Amplifier, Overdrive, Delay, Bitcrusher, Chorus, Flanger, Equalizer, Limiter, Compressor) in `Effects/se_*.ptfx`. Both come in the release zip.
- **Chords and macros.** Macro depth Scale, paraphony on ACD, chord capture with chord names, 77 chords in the MIDI Chord FX, record join and the Strum FX. The user guide is [`docs/chords_and_macros.md`](docs/chords_and_macros.md).
- **Capture (2.0.0).** Every note you play is kept. Button 3 of the pattern editor writes what you played into the track you are on, with one Undo. It is on by default on all three trackers, Config > Features > Capture switches it off. The original Tracker needs `Plugins/Ext/extras.plg` from the zip for it, the Tracker+ and the Tracker Mini need no card file.
- **Stereo (2.0.0, new on the original Tracker).** Stereo track pairs in the Project Settings, a stereo Line in, Radio and USB audio recording, the stereo Sample Editor and stereo delay and reverb sends. See the CHANGELOG.
- **MIDI Program Change in (2.0.0).** Config > MIDI > Program in picks the bank slot of Synth 1 to 3 (or of the side of the Synth play page) with a Program Change over USB or the MIDI In jack.
- **Tuning (2.0.0).** The Project Settings page has a Tuning group: notes per period, period size, base key and frequency and Scala files (`.scl`) from `/Tunings` on the card. It tunes sample tracks on all three trackers and synth parts on the original Tracker. The zip carries five example scales.
- **Tables (2.0.0).** The instrument editor of a sample instrument and of a synth part has a Table page of 16 rows with a transpose, a volume and one FX for each row. A note or the `TBL` step FX starts a table.
- **Reverse, Oscillator and Repitch (2.0.0).** Three new play modes for sample instruments and a `Repitch` row that fits the sample to a set number of steps at any tempo.
- **New note action (2.0.0, original Tracker).** A synth part decides what happens to the note a track still holds when the next one starts: Release, Cut, Fade or Continue.
- **MPE (2.0.0).** MPE out sends the MIDI instruments through one MPE lower zone with a member channel for every note. The Synth play page takes MPE in from a controller such as the ROLI Seaboard RISE 2, with a pitch bend, a pressure and a slide for each note.

- **Synth Automation.** Each patch has four automation slots. A slot is an LFO or an envelope and drives one engine parameter or one macro. The Params button cycles Synth Parameters, Synth Macros and Synth Automation. The automation is saved in the `.patch` file. A slot can also drive the pan, gain, reverb send or delay send of its synth part.
- **Sample editor effects.** Fourteen `.ptfx` effects for `/Effects/`, among them Transient Shaper, Tape Saturation, Lo-Fi Chip, Dirt Drive, BBD Chorus, Dub Echo and Reverb. The nine effects of the Sample Editor itself are `se_*.ptfx` files in the same folder since 1.4.0.
- **Workflow.** Add to Empty Slot in the Sample Loader, per-track MIDI-in for tracks 1 to 8, RAM or streaming mode per sample and a project load that ends in the pattern editor.
- **Fixes.** Three out of bounds writes to `label[8]` arrays in the UI are fixed in all builds. Slot names of the Patch FX bank survive a slot change and a project reload and a knob edit made while a song with the Patch FX plays is kept. A patch file, plugin settings file or project copy that fails on the card is reported and the old files stay. An unreadable folder on the card no longer breaks the file lists, a corrupt `.it` module cannot overflow the stack and the main loop and the display use less stack.

Not available: Polyend's DIRT and Shaping engines (they exist only in the closed library). A project part that uses an engine which is not on the card, such as CHIP without `chip.plg`, shows Missing and keeps its settings and its patch file and copying the plugin to the card brings the part back. Only a DIRT part (number 5) or an engine number no engine can have is reset to the defaults of its part on load.

## Recommended polyphony

Each synth part plays up to 4 voices, set with Polyphony on the Synth page (on the Synth play page a part takes up to 8). The Polyphony settings of the three parts add up to at most 8. Outside the Synth play page all parts draw from one shared pool of 4 voices, so at most 4 notes sound at the same time and a new note takes over a sounding one when the pool is full. PERC and T-Type are drum engines with one voice.

The table gives the number of voices that leaves room for the rest of a project when one synth part plays with the reverb and delay switched on. The rest means a second synth part and the sample tracks. Leave some headroom on top of it.

| Engine | Suggested voices |
|---|---:|
| ACD | 4 |
| FAT | 4 |
| PLK | 4 |
| Hand-Type | 4 |
| B-Type | 4 |
| 8-Type | 4 |
| VAP | 4 |
| WTFM | 4 |
| JU6 | 4 |
| FM4 | 4 |
| A-Type | 4 |
| PHZ | 4 |
| Arc-Type | 4 |
| D-Type | 4 |
| CS-Type | 4 |
| CHIP | 2 |
| PERC | 1 |
| T-Type | 1 |

CHIP is the heaviest engine and the only multi-voice engine that stays below 4.

## Hardware

The original Polyend Tracker (MK66), the Tracker+ and the Tracker Mini (IMX). All three are built from the same source and share the project features of the CHANGELOG, among them Tuning, Tables, MPE, the nine new chords and MIDI Program Change in. This release adds on the original Tracker the stereo track pairs, USB audio 8+3, a USB drive mode of its own and the new note action. The Tracker+ and the Tracker Mini have stereo recording and editing, USB audio and the USB drive from Polyend's firmware. The contents of `extras.plg` (MOD and IT, custom themes, the factory self test and Capture) are linked into their image, so they need no card file for them. The synth parts of the Tracker+ and the Tracker Mini keep the stock engines. Their image also reads the plugin synth engines from `/Plugins/Synth` on the card and loads one when a part picks it (the `chip.plg` file is not used there, the stock CHIP engine plays).

## Flash

1. Download `PolyendTracker_FF_2.0.0.zip` from the [release page](https://github.com/sbialy/tracker-ff/releases/tag/tracker-ff-v2.0.0) and unzip the whole zip onto the root of the microSD card. It is laid out like the card: `Firmware/` holds the image, `Plugins/Synth/`, `Plugins/Master/` and `Plugins/Ext/` the plugins and the MOD and IT module and `Effects/` the sample editor effects.
2. On the Tracker open **Config > Firmware > Firmware update**, pick `PolyendTracker_FF_2.0.0.ptf` and confirm.

On the Tracker+ and the Tracker Mini take the `TrackerPlus` or `TrackerMini` image from the release page and flash it the same way. Copy the `Tunings/` folder of the zip to the card for the example scales. Their image also reads the master insert plugins from `Plugins/Master` and the synth engines from `Plugins/Synth` (not `chip.plg`), so those folders of the zip work there too. The other card files of the zip, among them `extras.plg`, are made for the original Tracker.

Coming from 1.3.x, 1.2.0 or 1.1.0? Read [`UPGRADING.md`](UPGRADING.md) first. The zip carries new files that the image needs (`Plugins/Ext/extras.plg`, called `modit.plg` in 1.4.0, and `Effects/se_*.ptfx`) and, from 1.2.x, CHIP is a plugin that has to be on the card.

Do not rename the `.ptf`. The original's bootloader lists only names that start with `PolyendTrac` and are at most 31 characters long. Back up your `Projects/` folder first, because upstream CE saves patterns in a newer format that older firmware cannot read. If a build does not boot, the emergency recovery steps in [upstream's README](docs/upstream-README.md#4-flashing-on-hardware--emergency-recovery) apply.

## Build

Step by step instructions for Linux and macOS, with the expected size and checksum of a 1.4.0 build, are in [`docs/BUILDING.md`](docs/BUILDING.md).

You need `git`, `make`, `python3` and the Arm GNU Toolchain 12.3.rel1 (gcc 12.3.1), the release compiler. Download the `arm-none-eabi` archive for your machine from [Arm's toolchain downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads), version 12.3.rel1: `arm-gnu-toolchain-12.3.rel1-x86_64-arm-none-eabi.tar.xz` on a Linux PC, `...-aarch64-...` on a 64 bit Arm Linux, `...-darwin-arm64-...` on an Apple Silicon Mac, `...-darwin-x86_64-...` on an Intel Mac. Unpack it into `./agt/12.3.rel1/` (`make original` and `make original-release` look there, a `./agt/10.3/` next to it is picked first, remove it or pass `TOOLCHAIN=`. `make plus`, `make mini` and `make all` never look there, they need GCC 10.3) or pass `TOOLCHAIN=/path/to/arm-gnu-toolchain-12.3.rel1-.../bin/arm-none-eabi-`. **The 1.4.0 tag and older do not look in `./agt/12.3.rel1/`, pass `TOOLCHAIN=` there.** Use a fresh clone, `make` does not notice a compiler change.

The GCC 10.3 of upstream's notes, `docker/Dockerfile` and CI is for stock CE builds and gate checks, and the `arm-none-eabi-gcc` of a package manager is some other version. Such an image links and fits in flash but was never run on a unit and can hang at the boot logo, so `make original-release` stops on any compiler other than gcc 12.3.1 (`ANY_GCC=1` overrides, the image is then untested). With the Apple Silicon Mac archive a release tag gives the published `.ptf` byte for byte (check it against the `SHA256SUMS` of the release). With the Linux archives the image is not byte identical and its size varies by a few bytes between machines, because Arm's prebuilt C library carries the path it was built in and the makefiles list the sources in the order your file system returns them (sizes in `docs/BUILDING.md`). Copy `build/extras.plg` of your own build to `Plugins/Ext/` on the card (`modit.plg` in 1.4.0), the one in the release zip belongs to the published image only. Build only the original, `make all` also builds the Tracker+ and the Mini.

```bash
# the release image, build/PolyendTracker_FF_2.0.0.ptf (without RELEASE=1: PolyendTracker_FF_<commit>.ptf)
make original-release
```

`make original-release` is `make original` with the five built in engines, the plugin host, `RELEASE=1`, `USB_AUDIO=1`, `USB_MSC=1` and `CUSTOM_DEFINES="-DFEATURE_EXT_HOOKS -DFEATURE_NO_SKIPBACK"` (the Features page, USB audio and the USB drive, Skip-back left out of 1.4.0), engine DSP at `-O2`/`-O3` with `-ffp-contract=off`. The variables are `RELEASE_ARGS` in the top `Makefile`. CI builds the same flags for its quality gates.

The plugins are not built from this tree. They come from the module repositories pinned in `engines/external.lock.json`, see [`engines/README.md`](engines/README.md). `synth-open/tools/package_release.sh` puts the image, the plugins, the licenses and a checksum list into a folder and a zip.

`SYNTH_ENGINES` picks any subset of the engines. Without it the build has only ACD FAT VAP WTFM. `make synth-engines` prints the flash cost of each engine. The build stops if the image would pass the flash cap and `build/BUILD.txt` lists the engines and sizes. Plain `make original` builds the stock upstream firmware without the synth layer. Add `BUILD_TAG=<hash>` to set the build id shown on the Config screen. `make clean` at the top level removes everything in `build/`, so keep copies of the images you want.

Upstream's build notes (toolchain downloads, version helpers, `.ptfx` plugins) are in [`docs/upstream-README.md`](docs/upstream-README.md). The technical description of the synth layer and the build is [`synth-open/README.md`](synth-open/README.md).

## License and credits

- [`LICENSE`](LICENSE): MIT with upstream's Wholesale Commercial Clone Restriction. You can use, share and modify the firmware for free. You may not sell builds or clones of it and you may not use Polyend's marks in a commercial product. Ship `LICENSE` and `NOTICE.md` with any build you pass on.
- [`NOTICE.md`](NOTICE.md) and [`licenses/`](licenses/): third party components and their license texts, including OpenTracker (Apache-2.0), opentracker-synths (MIT) and the Mutable Instruments reverb (MIT).
- tracker-ff is a fork of [polyend/tracker-ce](https://github.com/polyend/tracker-ce) and builds on the work of Polyend, OpenTracker and opentracker-synths. "Polyend" and "Polyend Tracker" are Polyend's names. They appear here only to say which device this runs on and because the original's bootloader needs the file name prefix. The engine names ACD, FAT, VAP, WTFM, PERC and PHZ are the names Polyend gives its own synth engines and Polyend marks some of them as trademarks. They are used here only to say which engine a slot is modelled on. The engines in this firmware are not Polyend's code. They come from OpenTracker or were written for this project, they sound different and PHZ patches are not interchangeable with Polyend's. All names belong to their owners.
- Contributors: see [`CONTRIBUTORS.md`](CONTRIBUTORS.md).
- The project is free and non-commercial.
