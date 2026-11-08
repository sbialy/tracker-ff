# Third-Party Notices & Attributions

tracker-ff (a fork of Polyend Tracker Community Edition, CE) incorporates or links against the following third-party open-source and binary components:

---

## 1. ARM CMSIS & CMSIS-DSP
* **Location**: `platforms/imx/CMSIS/`, `platforms/mk66/3rdparty/teensy_core/`
* **License**: Apache License 2.0 / BSD-3-Clause
* **Copyright**: Copyright (c) ARM Limited. All rights reserved.

## 2. NXP MCUXpresso SDK (Kinetis K66 & i.MX RT1050/RT1060)
* **Location**: `platforms/imx/drivers/`, `platforms/imx/device/`, `platforms/imx/usb/`, `platforms/imx/sdmmc/`, `platforms/mk66/3rdparty/SD/`, `platforms/mk66/3rdparty/I2C/`, `platforms/mk66/3rdparty/SDRAM/`
* **License**: BSD-3-Clause
* **Copyright**: Copyright (c) Freescale Semiconductor, Inc. / Copyright 2016-2022 NXP.

## 3. FatFs - Generic FAT Filesystem Module
* **Location**: `platforms/imx/fatfs/`, `platforms/mk66/3rdparty/SD/fatfs/`
* **License**: FatFs License (BSD-style)
* **Copyright**: Copyright (C) ChaN, 2019. All rights reserved.

## 4. Teensy Cores & Teensy Audio Library (PJRC)
* **Location**: `platforms/mk66/3rdparty/teensy_core/`, `platforms/mk66/3rdparty/Audio/`, `core/libs/Audio/`
* **License**: MIT / PJRC Permissive License
* **Copyright**: Copyright (c) 2014-2018 Paul Stoffregen, PJRC.COM, LLC.

## 5. Liberation Sans Fonts (UI Bitmap Font Source)
* **Location**: `core/libs/fonts/LiberationSans-*_L4.{h,cpp}` (4-bit `L4` antialiased glyph bitmaps)
* **License**: SIL Open Font License 1.1 (OFL-1.1)
* **Copyright**: Digitized data copyright (c) 2010 Google Corporation; Copyright (c) 2012 Red Hat, Inc.

## 6. Embedded Template Library (ETL)
* **Location**: `util/3rdparty/etl/`
* **License**: MIT License
* **Copyright**: Copyright (c) 2016 jwellbelove (John Wellbelove).

## 7. magic_enum
* **Location**: `util/3rdparty/magic_enum/`
* **License**: MIT License
* **Copyright**: Copyright (c) 2019 - 2022 Daniil Goncharov.

## 8. FastCRC
* **Location**: `platforms/mk66/3rdparty/FastCRC/` (MK66 only, the i.MX tree has no copy)
* **License**: MIT License. Every source file carries the header "FastCRC library code is placed under the MIT license" with the full MIT text, and upstream (https://github.com/FrankBoesing/FastCRC, `LICENCE.md`) is MIT too. There is no GPL option and no GPL code in this tree (an earlier version of this file said "MIT / GPLv3 dual-licensed", which was wrong).
* **Copyright**: Copyright (c) 2014, 2015, 2016 Frank Bösing.

## 9. Polyend Precompiled Synth & DSP Libraries (`libPolySynth.a`, `libPolyDSP.a`)
* **Location**: `synth/lib/`
* **License**: Polyend Precompiled Synthesizer & DSP Library License (`synth/lib/LICENSE-PROPRIETARY.txt`)
* **Copyright**: Copyright (c) 2023-2026 Polyend Sp. z o.o. All rights reserved.

## 10. OpenTracker (open synth engines for the Original Tracker, `SYNTH_OPEN=1` build)
* **Location**: `synth-open/ot/` (unmodified copies, each file carries a provenance line, licence text in `licenses/LICENSE-OpenTracker-Apache-2.0.txt`). `synth-open/open_patchio.cpp` is a changed, trimmed version of `formats/patch_pb.cpp` (marked in its header).
* **Source**: https://github.com/sandlbn/polyend-tracker-re (commit 927b1516385e). Two files come from the 3.x line instead (branch `v3_fixes`, commit 789d55a4c5e8): `synth-open/ot/audio/synth_engine.h` (replaces the 2.x copy, the 3.x interface only adds `SynthHeld`, `cutoffOct` and the optional `slotPost()`) and `synth-open/ot/audio/bbd_chorus.h` (the chorus JU6 uses). Both carry a provenance line.
* **License**: Apache License 2.0 (the upstream project ships no NOTICE file)
* **Note**: the PERC drum engine (`synth-open/ot/audio/engines/perc.{h,cpp}`, same commit and licence, unmodified apart from the provenance line) is compiled only when `PERC` is in `SYNTH_ENGINES`. Its list icons are Polyend's (already in `core/libs/images`).
* **Note**: the three header-only DSP kits `synth-open/ot/audio/{chip_dsp,fm_dsp,va_dsp}.h` also come from the `v3_fixes` line (commit 789d55a4c5e8, Apache-2.0, provenance line at the top, otherwise unmodified). They are included by the OpenTracker 3.x module engines of item 11 and compile to nothing on their own.
* **Note**: `synth-open/ot/audio/dsp.h` also keeps its embedded notice for the PJRC Teensy Audio Library (MIT, Copyright (c) 2014-2017 Paul Stoffregen, PJRC.COM, LLC).

## 11. opentracker-synths (the OpenTracker 3.x module engines, shipped as plugins)
* **Location**: since 1.2.0 the source is in the public repository poly-engines-os (`src/synth-open/ot-synths/<module>/<module>.{cpp,h}`), which ships these notices with every plugin it builds. This repository keeps the engines' identity (`synth-open/engine_list.h`, `engines/engines.json`). Files: for the ten modules `ju6` (JU6), `fm4` (FM4), `dtype` (D-Type), `cstype` (CS-Type), `atype` (A-Type), `ttype` (T-Type drums), `eight` (8-Type), `hand` (Hand-Type), `btype` (B-Type) and `arc` (Arc-Type). Each file is unmodified apart from a two line provenance notice at the top. `synth-open/engine_<name>.cpp` carries the parameter ranges, labels, value formats, enum names and sections of the module's descriptor (`modules/<name>/src/<name>_desc.cpp`), marked in its header. The default macros and the edit curves in those files are this project's own choice. The module wrappers (`*_module.cpp`) and the descriptors themselves are not compiled.
* **Source**: https://github.com/opentracker-fw/opentracker-synths (commit eb150a5f741b, `modules/<name>`)
* **License**: MIT, Copyright (c) 2026 Marcin Spoczynski, text in `licenses/LICENSE-OpenTracker-synths-MIT.txt`. The repository states it contains no Polyend code.
* **Note**: none of them is built into the firmware image any more, they ship as `.plg` plugins. They need the 3.x `synth_engine.h` and `bbd_chorus.h` from item 10 and, for FM4, D-Type, CS-Type, A-Type, T-Type, 8-Type, Hand-Type, B-Type and Arc-Type, the DSP headers `chip_dsp.h`, `fm_dsp.h` and `va_dsp.h` from item 10 (Apache-2.0).
* **Note**: D-Type's OpenTracker engine id (0x82) is the id of this project's own PHZ engine, so D-Type is keyed 0xA2 in the picker and in `.patch` files, the same as the poly-editor stores it (the engine code itself is unchanged).

## 12. Polyend Chiptune (CHIP engine for the `SYNTH_OPEN=1` build, optional)
* **Location**: `core/synth/engines/ChiptuneSynthVoice.cpp` is Polyend's own open Chiptune voice from this repository (the repository licence applies, see LICENSE). The only change is a small block of hooks marked `FEATURE_SYNTH_OPEN` (parameter switch, bend, reset, create) that `synth-open/engine_chip.cpp` calls. Its parameter table follows the same file.
* **Note**: compiled only when `CHIP` is in `SYNTH_ENGINES`. Nothing here uses `libPolySynth.a` or `libPolyDSP.a`.

## 13. PHZ (clean-room phase distortion engine, source in the private repository poly-engines)
* **Location**: the source is not in this repository any more. It lives in the private repository `poly-engines` (`src/synth-open/phz/`). This repository keeps the engine's public identity (stored number, type id and name in `synth-open/engine_list.h` and `engines/engines.json`) and loads the built plugin from the SD card like any other engine plugin. The PHZ icons are Polyend's (already in `core/libs/images`).
* **Source**: written for this project from the public Polyend Synth manual (SYN2 p.76-80). Nothing of Polyend's closed code or binaries was read. The block rate voice skeleton and the filter stages are adapted from OpenTracker's VAP engine (item 10, Apache-2.0), the header of `phz.cpp` says so. No Mutable Instruments Plaits code is used. The published `phz.plg`, `phz.plgi` and `phz.wasm` ship with a notice that keeps this attribution and the Apache-2.0 text. The PHZ source and binaries are proprietary freeware (free use with the Polyend Tracker and tracker-ff, unmodified redistribution, all rights reserved), the terms are in `LICENSE.md` of the private repository `poly-engines`. Earlier tracker-ff releases (up to 1.1.0) contain the PHZ source in this repository under the MIT licence, that history and licence stay.
* **Note**: the engine is no longer built here, so `PHZ` is not accepted in `SYNTH_ENGINES`.

## 13a. PLK (clean-room Karplus-Strong plucked string engine, source in the private repository poly-engines)
* **Location**: the source is not in this repository. It lives in the private repository `poly-engines` (`src/synth-open/pluck/`). This repository keeps only the public identity of the engine (stored number 18, type id 0x8A, name).
* **Source**: written for this project from the published Karplus-Strong algorithm (Karplus and Strong, CMJ 1983) and its extensions (Jaffe and Smith, CMJ 1983). No third-party code was read or adapted. Only the `ISynthEngine` interface (item 10, Apache-2.0) is shared, and the headers of the PLK source keep that attribution. The published `pluck.plg`, `pluck.plgi` and `pluck.wasm` ship with our notice. The PLK source and binaries are proprietary freeware, the terms are in `LICENSE.md` of the private repository `poly-engines`.
* **Note**: the engine is no longer built here. It borrows the VAP list icons.

## 13b. Console and Tube (original master inserts, source in the private repository poly-effects)
* **Location**: the source is not in this repository. It lives in the private repository `poly-effects`. This repository keeps only the public identity of the two inserts (plugin ids `ff.console` and `ff.tube`).
* **Source**: written for this project, no third-party code. The published `console.plg`, `tube.plg` and their `.wasm` files are proprietary freeware (free use with the Polyend Tracker and tracker-ff, unmodified redistribution, all rights reserved), the terms are in `LICENSE.md` of the private repository `poly-effects`. The plugin runtime built into them stays MIT.

## 14. Mutable Instruments Polyverb sources (reverb code from Mutable Instruments Clouds and Rings)
* **Location**: `core/libs/Audio/Polyverb/{reverb,fx_engine,util,cosine_oscillator}.h`
* **License**: MIT License (the full text and "See http://creativecommons.org/licenses/MIT/" are in each file header)
* **Copyright**: Copyright 2014 Emilie Gillet (emilie.o.gillet@gmail.com). Adapted for Polyverb by Wojciech Jakóbczyk (Polyend), as stated in the headers.

## 15. Roboto Mono (UI bitmap font source)
* **Location**: `core/libs/fonts/RobotoMono-*_L4.{h,cpp}` (4-bit `L4` glyph bitmaps converted from `RobotoMono-*.ttf`, see the first line of each file)
* **License**: Apache License 2.0 (Roboto Mono by Christian Robertson, published by Google). The generated files carry no license header, the license is taken from the font's name and upstream, the text is `licenses/LICENSE-OpenTracker-Apache-2.0.txt` (same Apache-2.0 text).
* **Copyright**: Copyright (c) Google Inc. (Apache-2.0, Roboto Mono project authors).

## 16. util/3rdparty libraries (fmt, nanopb, tiny, constexpr-string)
* **Location**: `util/3rdparty/{fmt,nanopb,tiny,constexpr-string}/`, each with its own license file
* **License**: fmt: MIT (Copyright (c) 2012 - present, Victor Zverovich). nanopb: zlib license (Copyright (c) 2011 Petteri Aimonen). tiny (optional): Boost Software License 1.0. constexpr-string: MIT (Copyright (c) 2019 Marek Bogusovsky). The `boost`, `catch2`, `googletest` and `googlemock` copies there are not included by any firmware source (checked with git grep), so they are not in the image.

## 17. opentracker-fx (the DIRT master insert for the `SYNTH_OPEN=1` build, FF_DIRT=1)
* **Location**: `synth-open/ff_dirt.{h,cpp}` and `synth-open/ff_dirt_ctl.cpp`, with the host render and cycle bench in `synth-open/tools/dirt_render.*` and `synth-open/tools/dirt_cycles/`.
* **Source**: https://github.com/opentracker-fw/opentracker-fx (commit 6e5a7da, `modules/dirt/src/dirt.cpp`). A rewrite, not a copy: the shaper maths, the mid-rise quantiser, the sample and hold, the tone low-pass and the make-up gain follow the original, the audio path (int16 blocks), the fixed preset table and the block loop are this project's own. The headers carry the notice.
* **License**: MIT, Copyright (c) 2026 Marcin Spoczynski, text in `licenses/LICENSE-OpenTracker-fx-MIT.txt`. The repository states it contains no Polyend code.
* **Plugins** (`PLUG_HOST=1`, shipped as separate `.plg` files, not in the image): since 1.2.0 the master insert plugins Tape, Lo-Fi (its ladder weights are `kLadderErr` of OpenTracker's `chip_dsp.h`, Apache-2.0, item 10), BBD, Phaser and the Dirt wrapper (`fx_dirt_plug.cpp`, around the DIRT port above, whose three files the repository vendors) are built in the public repository poly-effects-os, which ships their notices (`src/synth-open/plug/fx/NOTICE.md` there) with every release file. The plugin SDK in `synth-open/plug/` is MIT (`synth-open/plug/LICENSE`).

## 18. OpenTracker mix effects as sample editor plugins (`editor-fx-sdk/effects/ot_*.c`, optional)
* **Location**: `editor-fx-sdk/effects/ot_tape.c`, `ot_lofi.c`, `ot_dirt.c`, `ot_dub_echo.c`, `ot_bbd_chorus.c` and `ot_reverb.c`, the shared header `editor-fx-sdk/ot_common.h` and the host test tools in `editor-fx-sdk/tools/`. They build to `.ptfx` files that are loaded from the SD card, nothing here is linked into the firmware.
* **Source (Tape, Lo-Fi, Dirt, Dub Echo)**: the TAPE, LOFI, DIRT and ECHO modules of https://github.com/opentracker-fw/opentracker-fx (commit 6e5a7da). **License**: MIT, Copyright (c) 2026 Marcin Spoczynski, text in `editor-fx-sdk/LICENSE-opentracker-fx-MIT`. The repository states it contains no Polyend code. Each port is rewritten in C and says so in its header (mono, no tempo sync, no block ramps, changes listed per file).
* **Source (BBD Chorus)**: `audio/bbd_chorus.h` of OpenTracker 3.x (https://github.com/sandlbn/polyend-tracker-re, branch `v3_fixes`, commit 789d55a4c5e8), the DSP of the BBDC module of opentracker-fx. **License**: Apache-2.0, text in `licenses/LICENSE-OpenTracker-Apache-2.0.txt`. The upstream file carries no copyright line of its own. The port is translated to C and marked as modified in its header (Apache-2.0 section 4b).
* **Source (Lo-Fi ladder weights)**: the six values of `kLadderErr` in OpenTracker's `chip_dsp.h` (same repository and commit as above, Apache-2.0). No other `chip_dsp.h` or `va_dsp.h` code is used, the filter coefficients come from the host's `tanf` and `powf`.
* **Source (Reverb)**: Polyverb, `core/libs/Audio/Polyverb/` of this repository (`reverb.h`, `fx_engine.h`, `cosine_oscillator.h`, `util.h`), itself Emilie Gillet's reverb from Mutable Instruments Clouds. **License**: MIT, Copyright 2014 Emilie Gillet, adapted for Polyverb by Wojciech Jakóbczyk (the adaptation falls under the repository licence, see LICENSE). The full MIT text of the Gillet header is kept in `ot_reverb.c`. The port is rewritten in C and marked as modified in its header.
* **Note**: the plugins have only been run in a host harness (`make ptfx-check`) and not on a device.

## 19. Add to Empty Slot (Sample Loader, community contribution)
* **Location**: `core/interface/sampleImporter/fSampleImport.cpp`, `gSampleImport.cpp` and `sampleImporter.h`.
* **Source**: https://github.com/JordanDuncan/tracker-ce (branch `add-to-empty-slot`, commit 3627767, upstream pull request 58 of polyend/tracker-ce). Cherry-picked with the original author kept (commit ea870ab). The only change is an explicit `<optional>` include in `sampleImporter.h`.
* **Author**: Jordan Duncan.
* **License**: MIT. Contributed to the Polyend Tracker Community Edition under the contribution terms of `CONTRIBUTING.md`, so the repository `LICENSE` applies (MIT with the Wholesale Commercial Clone Restriction for the complete application).

## 20. Per-track MIDI-in (Config > MIDI Track 1 to 8 in channel, community contribution)
* **Location**: `core/interface/configEditor/configMenu.cpp`, `configMenuDefs.h`, `core/source/mtConfig.cpp`, `mtMidi.cpp`, `mtSequencer.cpp`, `mtSequencer.h` and `mtStructs.h`.
* **Source**: https://github.com/scott-lepore/tracker-ce (branch `pr/per-track-midi-in`, commit 00d1b68, upstream pull request 51 of polyend/tracker-ce). Cherry-picked with the original author kept (commit 758505f), no changes to the code.
* **Author**: Scott Lepore.
* **License**: MIT. Contributed under the contribution terms of `CONTRIBUTING.md`, so the repository `LICENSE` applies (MIT with the Wholesale Commercial Clone Restriction for the complete application).

## 21. Plate Reverb (sample editor plugin, `editor-fx-sdk/effects/plate_reverb.c`, optional)
* **Location**: `editor-fx-sdk/effects/plate_reverb.c`. It builds to a `.ptfx` file that is loaded from the SD card, nothing here is linked into the firmware.
* **Source**: https://github.com/mranthropology/tracker-ce (branch `plate-ptfx`, commits 6145892 and 26abefd). Copied with the original author kept (commit bd5dd08). The file header says it is ported from the live master plate in mranthropology/Tracker-Firmware-Mod (`fxbank/fx_bank.c`). The algorithm is the plate reverb of Jon Dattorro, "Effect Design Part 1", Journal of the Audio Engineering Society, 1997.
* **Author**: MrAnthropology.
* **License**: MIT. The fork carries the same Polyend Tracker Community Edition license as this tree and adds no notice of its own. The `editor-fx-sdk/` folder is MIT under Part 1 of `LICENSE`.
* **Note**: the author reports a test on an original Tracker (MK66) with CE 1.0.0. In this project it has only been run in a host harness (`editor-fx-sdk/ptfx_emu_check.py`).

## 22. Transient Shaper (sample editor plugin, `editor-fx-sdk/effects/transient_shaper.c`, optional)
* **Location**: `editor-fx-sdk/effects/transient_shaper.c`. It builds to a `.ptfx` file that is loaded from the SD card, nothing here is linked into the firmware.
* **Source**: https://github.com/emanlapponi/tracker-ce (branch `feature/transient-shaper`, commits b170f11 to 95fbe30, upstream pull request 48 of polyend/tracker-ce). Squashed to the final file with the original author kept (commit 51bd65f).
* **Author**: Emanuele Lapponi.
* **License**: MIT. Contributed under the contribution terms of `CONTRIBUTING.md`. The `editor-fx-sdk/` folder is MIT under Part 1 of `LICENSE`.

## Release packages

The `.ptf` images of the `SYNTH_OPEN=1` builds are distributed together with `LICENSE`, this file and a `licenses/` folder holding the full text of every bundled third-party license (made by `synth-open/tools/package_release.sh`). Texts that live in the repository: `licenses/LICENSE-OpenTracker-Apache-2.0.txt`, `licenses/LICENSE-OpenTracker-synths-MIT.txt`, `licenses/LICENSE-OpenTracker-fx-MIT.txt`, `licenses/LICENSE-BSD-3-Clause-NXP.txt`, `licenses/LICENSE-LiberationFonts-OFL-1.1.txt`, `util/3rdparty/*/LICENSE*`. Texts that exist only as source headers (FastCRC, Polyverb, FatFs, PJRC Teensy core and audio library) are copied out of those headers by the packaging script, verbatim. The image links neither `libPolySynth.a` nor `libPolyDSP.a`, so `synth/lib/LICENSE-PROPRIETARY.txt` is not shipped. Factory patches and `.ptfx` plugins are not part of the package.
