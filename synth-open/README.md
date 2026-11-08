# tracker-ff 1.2.0 - synth-open, the open PolySynth shim for the original Tracker (MK66)

tracker-ff is a fork of Polyend's Tracker CE firmware (FF as in 0xFF). `synth-open/` is the directory and the make flag (`SYNTH_OPEN=1`) of its synth layer. The repository README is `../README.md`. This file is the technical description of the shim and of the build.

The shim is built only with `make original SYNTH_OPEN=1`. It replaces the closed `libPolySynth.a` and `libPolyDSP.a` (Cortex-M7 only, the licence forbids reverse engineering) on the original Tracker. The closed libraries are never linked here and nothing in this directory was derived from them. Everything is written from the open headers in `synth/include/PolySynth` and from OpenTracker (Apache-2.0).

What 1.2.0 has:

- Synth engines chosen at build time (see "Engine picker") plus synth plugins at run time. The PLUGS configuration has six built in (ACD, FAT, VAP, WTFM, PERC, CHIP) and a library of twelve plugins in `/Plugins/Synth/` (JU6, PHZ, FM4, D-Type, CS-Type, A-Type, T-Type, 8-Type, Hand-Type, B-Type, Arc-Type, PLK). ACD, FAT, VAP and WTFM are the Polyend engines as OpenTracker re-implements them. PERC is OpenTracker's drum engine, CHIP is Polyend's own open Chiptune voice, PHZ (phase distortion) and PLK (Karplus-Strong) are our own, the rest are OpenTracker 3.x modules. Other built-in sets still build, for example the 16 engine 1.1.0 image. MIDI registers by name only and Shaping has no engine.
- Up to 4 voices per instrument, a shared voice pool and OpenTracker's governor.
- `.patch` files that load and save from the SD card, a bank of 16 patch slots per synth part with the Patch step FX (see "Patch files"), and pitch bend.
- Master insert plugins (`/Plugins/Master/`, two slots on the master bus, seven inserts with the release, Dirt among them), see `plug/README.md`.
- A flash trim (`TRIM=1`). It made the 16 built-in engines of 1.1.0 fit the original's flash, the PLUGS configuration uses it with six built-in engines and the rest as plugins.
- A set of audio CPU optimisations that remove work the stock firmware does for silence.

## Card plugin engines (PLUG_HOST builds)

The PLUG_HOST image (`make original SYNTH_OPEN=1 TRIM=1 PLUG_HOST=1 SYNTH_ENGINES="ACD FAT VAP WTFM PERC CHIP"`, PTF tag PLUGS)
builds in only the six stock-named engines. Every custom engine is a card plugin in `/Plugins/Synth/` (`plug/README.md`, Synth
engine plugins): `ju6 fm4 btype phz dtype cstype atype ttype eight hand arc pluck` (`.plg` plus a `.plgi` logo each). They
keep their stored numbers and patch ids (JU6 7, PHZ 8, FM4 9, D-Type 10, CS-Type 11, A-Type 12, T-Type 13, 8-Type 14,
Hand-Type 15, B-Type 16, Arc-Type 17, PLK 18), so old projects, patches and slot files open on the plugin. A part whose
plugin is not installed shows `Missing: <name>` (`open_cold.cpp`, any stored number 7 to 255, the name from `/Plugins/names.txt` or `#<number>`). Every plugin rendered bit identical to
the built-in engine (the render compare, both scenarios, run before the engine sources moved to poly-engines-os). Moving the seven that were still built in (PHZ D-Type
CS-Type A-Type T-Type 8-Type Arc-Type) frees 88,884 B: the image ends at 0xE535C instead of 0xFAEB8 (same tree, flag flipped).

All twelve are in the model list from boot, installed or not. The FlexNVM bank (30 sectors of 4 KB) is a cache of the card:
an engine is installed when a part needs it (model list after a 500 ms rest, project load) and the least recently used engines
that no part plays make room (`plug/README.md`, Library and cache). A Tracker has 3 synth parts (`kSynthInstrumentsCount` in
`mtStructs.h`), each with one engine, and Patch FX slots keep the part's engine, so at most 3 engines play at once, 4 while one
part changes engine. Budgets (`tools/plug_bank_test.cpp`, sizes of the `-O2` set, 12.3.rel1): the twelve need 44 whole sectors, 7 to 8 stay resident, the 3
biggest plus the one being picked need 18 (73,728 of 122,880 B). Engine rw and bss: 2,976 B of the 3,072 B SRAM pool for all twelve (the worst 3 at once
need 1,928 B, the pool stays at all twelve so it never forces a flash write). PLK's 8,772 B delay lines live in a 16 KB SDRAM
work arena (`plug_work.h`, a block per attached engine, given back when another engine needs the room and no part plays it, so the
limit is the engines in use at once: worst 3 of 1.2.0 + formflux 15,000 B, all four hungry ones 15,512 B), no engine takes internal heap.
RAM_G logos: a cache of 32 list and 2 editor images (`plug_logo.h`), every listed plugin shows its logo. CPU: FlexNVM runs code at flash speed (device bench run 3), the plugin executes the same
instructions as the built-in engine (worst block equal in `emu_synth.py`, PLK +11 instructions for its pool pointer).

The patch editor gives every section page an icon from one table (`open_section_icon.h`, SYNTH_OPEN builds only, the stock
chain stays in the stock build), checked over all 131 sections of the 18 engines by `tools/run_section_icon_test.sh`.

## Source files

| File | Content |
|---|---|
| `open_synth.cpp` | Render path, built at `-O2`: `Synth`, `Instrument` (voice allocation and steal, legato reuse in mono, MIDI note on/off/CC from `MidiAdapter`), `OpenPatch::effective` (macros with the OpenTracker additive law) |
| `open_patch.h` | `OpenPatch` (values, macros, Macro 7 velocity to Volume), shared by the two files |
| `open_patchdata.h` | Plain `PatchData` model of a `.patch` file and the codec interface, the automation slot (`ModSlot`) |
| `open_mod.h` | Synth automation run time, header only and pure (no PolySynth include): LFO shapes, the envelope, the tempo rates and time law of the sample page, destination eligibility, the wave name match, picker order and identity resolution (see "Synth automation") |
| `open_patchio.cpp` | `.patch` codec (protobuf wire reader and writer), `-Os`. Trimmed from OpenTracker's `formats/patch_pb.cpp` (Apache-2.0, a changed file, the header says so) |
| `open_cold.cpp` | Cold code, built at `-Os` (`SYNTH_OPT_COLD`): value text, edit curves, registry factories and metadata, the `OpenPatch` GUI API and `PatchStore` (file backed, see "Patch files") |
| `open_load.h` | Plain C++ interface the core calls into: `reportBlockCycles` (block load, from `Profiler::stopMeasuringAudio`), `setOffline` (exporter), `setVoiceBudget`, `partIdle` and read-outs |
| `open_fileops.h` | The file replace of every save (`replaceFileOps`) and the recovery of `<file>.bak` (`recoverBackupOps`), header only, plain C++ over a small file operations type. `FileSystemAdapter` runs it over the SD card (the card's `FR_NO_FILE` is the only proof of absence), the host test over a FatFs-like in-memory card that fails a rename or loses power at every step |
| `open_bankcopy.h` | The bank copy of project save and open (`copyPartBank`, header only, plain C++): every patch, backup and names file replaces the destination only when copied completely. Project open (into the Workspace) afterwards removes the Workspace slot files the project does not have, project save (into Projects) removes nothing. `Synth::copyWorkspacePatches` runs it over the SD card, the host test over an in-memory one |
| `open_engine.h` | `EngineDef`: parameter table, editor sections, default macros and the hosted `ISynthEngine`. A new engine is one `EngineDef` plus its engine source |
| `engines.mk` `engine_list.h` | The engine picker: names, sources and default set (`engines.mk`, read by the top Makefile and `core/makefile`) and one X-macro entry per engine (`engine_list.h`: registry factory, `Synth::kSynthTypes`, editor name and icon, `.patch` engine id, stored project number) |
| `engine_<name>.cpp` | One `EngineDef` per engine (parameter table, sections, default macros, value text). `engine_acd.cpp` also holds the enum name tables shared by all engines |
| `engine_chip.cpp` | CHIP: also the `ISynthEngine` adapter that hosts `core/synth/engines/ChiptuneSynthVoice.cpp` |
| `ot/` | Copies from OpenTracker (commit 927b1516385e, Apache-2.0): `audio/engines/{acd,fat,vap,wtfm,perc}.{h,cpp}`, `audio/dsp.h`, `audio/synth_engine.h`, `audio/audio_tables.h`, `audio/{governor,cpu_meter}.{h,cpp}`, `model/synth_patch.h`, `fastrun.h`, plus the 3.x headers the module engines need. Unmodified apart from a provenance line at the top (Apache section 4b) |
| (module repositories) | The plugin engines and inserts are not in this repository: poly-engines-os and poly-effects-os (public), poly-engines and poly-effects (private). `engines/README.md` |
| `ff_dirt.h` `ff_dirt.cpp` `ff_dirt_ctl.cpp` | The live Dirt master bus insert (FF_DIRT, see "Dirt master insert"): per block DSP at `-O2 -ffp-contract=off`, the preset table, coefficients and the `AudioEffectDirt` audio object at `-Os` |
| `stub_fmt.cpp` | `fmt::v9::vformat` replacement so the 37 KB `fmt/format.cc` is not linked |
| `tools/host_smoke_test.cpp` | Host (Mac) smoke test of the shim, not firmware. It plays notes on every engine and checks finite, non-silent output, patch round trips, the id tables, the voice pool, the governor, the idle skip and the synth automation (codec, math, hook, gates, Patch FX, identity, backup). Build line in the file header |
| `tools/run_smoke.sh` | `run_smoke.sh "ACD VAP WTFM CHIP"` builds and runs the smoke test for an engine set (same names as `SYNTH_ENGINES`) |
| `tools/run_tests.sh` `run_host_tests.sh` | `run_tests.sh [WORKTREE [OUTDIR]]` runs every host test of the tree (smoke variants, plugin SDK examples, plugin host unit tests against the pinned modules) and ends with `RESULT: GREEN` or `RESULT: RED`. Needs `TOOLCHAIN` (Arm GNU 12.3 prefix) and a `PY` with `unicorn`. `run_host_tests.sh` skips the SDK examples and takes the plugins from `$PLUGDIR` |
| `tools/validate_synth.py` | `python -I tools/validate_synth.py DIR` runs the firmware's own `checkSynth` (cut out of `plug_synth.cpp`, built with the agt toolchain) in Unicorn on every synth `.plg` in DIR. A reject shows as Missing on the device. All 12 must PASS |
| `tools/engine_budget.sh` `.py` | Flash cost per engine from the link map, the image size against the cap, `BUILD.txt` |
| `tools/phz_cycles/` | Emulated cycle bench for the engines (`run_bench.sh [OPT] [ENGINE]`) |
| `tools/dirt_cycles/` `tools/dirt_render.sh` | Emulated cycle bench of the Dirt insert and its host render (WAV files and self checks) |
| `tools/idle_cycles/` `tools/audio_opt/` | Emulated benches for the audio graph (see "Idle synth parts" and "Audio CPU optimisations") |
| `tools/package_release.sh` `tools/check_ptf.py` | Release packaging and the `.ptf` sanity check that runs on every SYNTH_OPEN build |
| `tools/check_static_init.py` | Post-build check of the `.axf` that refuses an image whose static constructors touch the SDRAM (the SDRAM works only after `main()` has initialised it, so such an image hangs at boot) |

## What the shim does

- `Instrument` hosts the OpenTracker `ISynthEngine` directly (OpenTracker's `SynthBank` is not linked). The voice cap is 4 per instrument (`Metadata::maxVoices`, see "Four voices per instrument") and there is one global pool shared by the three synth slots, default 4 and at most 8 (`open::setVoiceBudget`, no UI yet). Voice steal: a free voice, else the quietest released voice, else the oldest. With the pool full a new note takes the best victim over all slots (one in another slot fades out over its last block, no click). When the own cap is reached the steal happens inside the slot. With 1 voice a new note reuses the held voice, so the legato and overlap glide of ACD works.
- MIDI: note on and off, velocity 0 is off, CC 102 to 107 are macros M1 to M6, CC 123 and 120 release all, Stop releases all. Pitch bend is implemented (see "Pitch bend"). Aftertouch and the held-note stack of a mono voice are not.
- Parameters reach the engine as OpenTracker `SynthParams` (start and end ramp per 128 frame block), rebuilt from the patch every block while a voice sounds, so knob turns are heard live.
- Gain: `Volume * (1 + k * (velocity - 1))` with k the amount of the Velocity macro on Volume (the OpenTracker law). Output is mono (`outputChannels = kMono`), the engine's L and R are averaged into the left buffer.
- Edit curves (the knob law, our own because the stock ones are undocumented): cutoff and LFO rate geometric, envelope times cubic, glide time quadratic, the rest linear. The OpenTracker 3.x engines use linear curves except Hz and LFO Hz exponential and seconds cubic, because their descriptors carry no curve, so fine adjustments of a long time or a wide integer range are coarse on some knobs. Stepped values such as semitones, ratios and on/off are continuous on the knob and rounded in the text. The VAP and WTFM LFO Sync tempo divisions show as Hz. Default macros are the shim's own choice.
- Not implemented: per-drum aux sends (`setSendTap` is never called, see PERC and T-Type) and aftertouch. The delay and reverb sends of a whole synth part do work (see "Synth delay and reverb sends on MK66").
- The core is built with `FEATURE_SYNTH_OPEN`. Everything the shim changes in shared code sits behind that flag, `FW_TRIM` or SYNTH_OPEN make logic, so plain `make original` stays the stock firmware (see "Stock build").

## Voice pool and governor

`ot/audio/governor.{h,cpp}` and `cpu_meter.{h,cpp}` are OpenTracker's, unmodified (provenance line only). Control law as in `governor.h`: cut on overrun (load L >= 1000 permille), risk (L >= 980, or a second block >= 950 within 1 s) and average (A >= 800, deep 880), 250 ms hold-off after a cut, restore one voice after 2 s with A < 650 and every block < 900. Only synth voices are cut, sample voices never are. A governor steal fades the voice out over its last block (`SynthVoice::fading`).

Block load source: tracker-ce already times every live audio block with the DWT cycle counter (`Profiler::startMeasuringAudio` and `stopMeasuringAudio` around `TrackerImpl::updateAudioBuffers`: midi, sequencer, engine update and the audio library update, so sample voices, FX and synths together, like OpenTracker's CpuMeter). Under `FEATURE_SYNTH_OPEN` `stopMeasuringAudio` also passes the raw cycle delta to `open::reportBlockCycles`, which feeds `CpuMeter::addBlock` (permille of 522,240 cycles = 128 frames at 44117.6 Hz and 180 MHz, same as OpenTracker) and `Governor::evaluate`. Both run in the audio interrupt at the end of the block, so a cut acts on the next block. The exporter calls `open::setOffline` (governor off, cap back to the budget) and transport Stop resets the cap. Project load (`Synth::updateToSettings(..., true)`) also resets it: `open::resetOnProjectLoad` cuts every voice and restarts governor and meter. Not done: a budget setting in the UI and the governor's telemetry on screen.

The coloured line at the top of the screen is not the governor. `TitleBar::refresh` (`core/interface/core/titleBar.cpp`, a 170 x 1 px bar) shows it, only in builds with `GUI_HANDLE_SYNTH` (which `FEATURE_SYNTH_OPEN` turns on, the stock MK66 build has none). Every 300 ms it takes the largest audio block time of the period as a percentage of 2,902 us (128 frames) and draws yellow from 85 %, orange from 93 % and red from 99 % (`kPerformance1/2/3Treshold`, `titleBar.h`), hidden again after three quiet periods. It is the load the governor sees, so red means a block at the limit, an audible gap or one about to be. It was calibrated for the 600 MHz Tracker+, so on this CPU an average near 77 % keeps it yellow. The Config screen in dev mode shows min, max, avg and last of that load.

## Four voices per instrument

The Voices parameter of a synth slot goes up to 4: `kMaxVoicesPerInstrument` in `open_engine.h`, and every poly engine's `EngineDef::maxVoices` points at it. PERC and T-Type stay at 1 voice (the kit lives in the voice, `maxVoices = 1`, and the editor, `pickVoice` and the patch metadata cap them). The project field `voiceCount` is validated against 0 to 8 (`kSynthMaxVoices`, the sum over the 3 slots, so 4 + 4 + 0 is the most the Voices page offers) and the `.patch` format has no voice count, so projects saved with 1 or 2 voices load as before.

The shared pool (default 4, max 8) and the governor stay the global limit. One slot can take the whole default pool, and three slots asking for 12 still sound 4.

Memory is all SRAM (the libc heap and `.bss`, no big per voice buffer, the voice state is the 128 byte `SynthVoice::state`): `Voice` is 168 B, `OpenInstrument` is 1824 B (3 heap objects) and CHIP keeps one `ChiptuneSynthVoice` (276 B) per voice record, 12 objects in all (about 2.8 KB of heap with CHIP, 1.0 KB without).

## Engines

### Engine picker

```
make original SYNTH_OPEN=1                                     # default set ACD FAT VAP WTFM
make original SYNTH_OPEN=1 SYNTH_ENGINES="ACD VAP WTFM CHIP"   # any subset of the 18 names below, the 1.1.0 release set is ACD to ARCTYPE without HANDTYPE
make synth-engines [SYNTH_ENGINES="..."]                       # flash cost of every engine and what fits on top of the selection
```

The 18 names are ACD FAT VAP WTFM PERC CHIP JU6 PHZ FM4 DTYPE CSTYPE ATYPE TTYPE 8TYPE HANDTYPE BTYPE ARCTYPE PLK (PHZ and PLK are our own engines, their source is in the private repository `poly-engines`, this repository keeps their numbers and names, PLK is stored 18, ids 0x8A). They are case-insensitive, commas work too and the order does not matter (the model list on screen always runs in the order above). An unknown name or an empty list stops with the list of valid names. Unlisted engines are not compiled and not linked: their `engine_<name>.cpp`, OpenTracker sources, `ChiptuneSynthVoice.cpp`, list icons, registry entry and `kSynthTypes` entry are all out. Every engine set other than the default builds in its own directory (`core/build-MK66-ORIGINAL-SYNTHOPEN-E<names>`) because the engine list is a `-D` flag that the dependency files do not see. `make clean` removes them all.

- One place per engine: a line in `engines.mk` (name, sources), one entry in `engine_list.h`, its `EngineDef` and sources.
- Size guards: `FLASH_IMG_MAX` is 1,044,480 B (0xFF000, the last 4 KB sector stays free) and is checked three times: the post-build `size` check of the platform makefile and the linker ASSERT (both on text+data), and again in the top Makefile from the linked `.axf`, so a second `make` of an oversize set fails as well. An oversize set leaves no `.ptf` in `build/`. `SYNTH_FLASH_IMG_MAX=n` lowers the cap for one build. The real flash image is text + data. The FLASH line of ld leaves out the `.data` load image and is about 6.5 KB too optimistic.
- The `.ptf` name is `PolyendTracker_<TAG>_<A.B.C>.ptf`, with TAG `FF` for TRIM builds (`PolyendTracker_FF_1.1.0.ptf`, 27 chars) and `SO` for plain SYNTH_OPEN builds. The firmware list (`platforms/mk66/3rdparty/SD/SD.cpp`) shows only names that start with `PolyendTrac`, are lowercase `.ptf` and are at least 15 chars long. The bootloader reads at most 31 chars. The engine list is in `build/BUILD.txt`, written next to the `.ptf` after a successful build (engines, image size, free bytes, bytes per engine from the link map).
- `make synth-engines` links all engines once with the cap lifted for that one link, sums the input sections that survive `--gc-sections` per engine archive member and prints the cost per engine, the base without engines and the estimate for the selection. The estimate is within about 1 KB of a real link, the real size is always the one `make original` prints.
- Projects store an engine as its canonical number, not as the position in the list on screen, so a project keeps its engines across builds with other engine sets: ACD 0, FAT 1, VAP 2, WTFM 3, PERC 4, DIRT 5, CHIP 6, JU6 7, PHZ 8, FM4 9, DTYPE 10, CSTYPE 11, ATYPE 12, TTYPE 13, 8TYPE 14, HANDTYPE 15, BTYPE 16, ARCTYPE 17. PERC is 4 and CHIP is 6 as in the stock Tracker+ and Mini CE list. A part whose engine the build lacks (DIRT, an engine left out such as HANDTYPE in the release image) is reset to that part's defaults on load (`Synth::validateSynthPartSettings`, called from `fProjEdit.cpp` on the loaded project), with the first engine of the build when the default engine is not in it. The reset is made in the loaded project, so saving writes the default part over the original engine choice (the part's `Workspace/<N>.patch` file is not touched). Open such a project in a build that has the engine, or do not save it.
- `.patch` engine numbers differ from the stored numbers on purpose, see "Patch files".

### PERC

PERC is the Tracker+ and Mini drum synth: one slot holds a whole kit (kick, tom, snare, hats, cymbal, percussion) and up to five drums ring at once. The engine is OpenTracker's `ot/audio/engines/perc.{h,cpp}` (Apache-2.0, unmodified copies). `engine_perc.cpp` has its 84 parameters in 8 sections (Kick, Toms, Snare, Hihat, Cymbal, Percussion, Mixer, FX) and 6 default macros.

- Numbers: stored 4, `InstrumentTypeId::kDrumSynth` 6, `.patch` engine 6. Stored 6 is CHIP, which is not a clash because stored numbers live in project files and `.patch` numbers in `.patch` files.
- Icons: `perc_mini_nomark` and `perc_med` (1,660 B) are compiled in when PERC or T-Type is picked (T-Type wears them, see "Engine icons").
- Gaps: no Volume parameter (`volumeParam = -1`, Macro 7 and the framework gain carry the note velocity and the engine applies it per hit) and 1 voice (a slot is one kit). The per-drum Delay and Reverb sends are ignored because `percSetSendTap` is never called. Wiring them is not small: the firmware only takes aux sends from a stereo, `rendersAuxSends` patch (`synth_PolySynth.cpp` asserts `hasSends == isStereo`), so the shim would have to stop averaging L and R to mono for PERC, report `kStereo` plus `rendersAuxSends`, hand `setSendTap` the two aux buffers and the firmware stereo path has not run on this hardware.
- The drum overlap, the per-hit velocity, the unmapped notes (61 63 68 70 play nothing) and the 6 drums at once are covered by the host smoke test. A single hit peaks at 0.5 to 0.9 and all six drums together reach about 1.4 (the firmware soft clips the slot).
- Flash: 23.7 KB linked (22.1 KB code plus the 1.66 KB list icons).

### CHIP

`core/synth/engines/ChiptuneSynthVoice.cpp` stays Polyend's file. It gets a small `FEATURE_SYNTH_OPEN` block (a parameter switch with the same effect as the handlers in `createMetadata`, bend, reset, create) so the shim needs neither `Patch::Metadata`, `fmt` formatters nor the SDK registry path. `createChiptuneSynth()` is unused in this build and the linker drops it. `engine_chip.cpp` has the 25 parameters in the file's order, the 5 sections and the 6 default macros of `createMetadata`, and an `ISynthEngine` that keeps one voice object per shim voice record (at most 3 slots x 4 voices), pushes the parameters into it every block (Volume is applied by the shim as for the other engines) and feeds the shim's pitch bend. Objects are created once, outside the audio path.

CPU: the first device run of CHIP on a pre-release build showed the red line at once. The voice called `powf`, `sinf`, `fmodf`, `roundf` and (for the drum model) `expf` per sample. Newlib's float libm costs about 380 cycles per `powf` on the M4F, so one voice took 229,082 cycles per 128 frame block at `-Os` (43.9 % of the 522,240 cycle budget). The fix, all under `FEATURE_SYNTH_OPEN` so the Tracker+ and Mini builds keep `std::`: exp2 and sine as polynomials (relative error 5e-9 and 7e-9, exp2 exact at integers), `round` and `fmod(x, 1)` as exact integer code, the pitch and the filter coefficient recomputed only when their input changes, and the vibrato sine skipped at depth 0. A held note with static pitch renders bit identical to the old voice, with moving pitch the difference is a few samples where the 6 bit quantiser flips.

Upstream tracker-ce 23db9c3 and 9d209ca (merged at 998b06d) rewrote the voice the same way for every platform: per block constants, `sdk::fastPow2` and `sdk::fastSine` (in `SynthVoiceSDK.h`, now also behind `sdk::noteToFreq` and `sdk::detuneMult`), a recurrence for the drum decay and the filter coefficient cached on the envelope level. Our polynomials were dropped for theirs. On the M4F their `__builtin_floorf` and `__builtin_roundf`, and the voice's `std::round`, are still newlib calls (no `VRINT`), so the merged voice took 72.0 % (default) and 88.3 % (worst) at 4 voices against 57.9 % and 82.0 % before. Under `FEATURE_SYNTH_OPEN` the voice now uses the same two polynomials with integer `floor` and `round` (`roundOf` gives the same bits as `roundf` for every float below 2^31, checked over all 2.65 billion of them), forced inline, and computes the pitch only when the note moves: 57.1 % and 75.0 %, bit identical to the SDK version in all 13 bench scenarios (24 blocks, CRC of the output). Against the pre-merge voice the pitch differs by at most 0.0004 cent (2e-7) and the renders by flips of the quantisers, the polyBLEP edges and the LFSR clock (rms -50 to -68 dB for the tonal models, -35 dB Noise, -20 dB Chip Drum, levels within 0.003 dB). With every parameter at its maximum the two stay equal for about 60 blocks, then drift apart in phase (resonance at the top, level +0.3 dB). The drum decay constant 0.99971659 is e^(-1 / (0.08 x 44100)), 44100 Hz instead of the 44117.6 Hz the voice runs at, restarted from `fastPow2` every block, so the error is at most 1.5e-5 of the decay.

Emulated cost per voice per block, `-Os` / `-O2`: default patch 14.4 / 12.7 %, held in sustain 12.1 / 10.6 %, worst model (Super Square) 18.6 / 15.6 %. CHIP follows `SYNTH_OPT_ENGINES`, which is `-O2` since the engine DSP moved off `-Os` (see "Engine DSP at -O2"): four voices of the worst model take 62.3 % instead of 75.0 %, so CHIP is still the most expensive built-in engine. Further savings would mean control rate updates of the filter coefficient, which changes the sound. The figures are emulated (no flash wait states), not read from a device.

### PHZ

PHZ is the Polyend Synth's dual phase distortion engine (Casio CZ style), which exists only in Polyend's closed code. Ours is written from the public manual (SYN2 p.76 to 80) and nothing else. No Polyend binary or library was opened. Its voice skeleton follows OpenTracker's VAP engine (Apache-2.0).

Its source (and the source of PLK, the Karplus-Strong engine) moved to the private repository `poly-engines`. This repository keeps the public identity of both (stored numbers 8 and 18, type ids, names in `engine_list.h` and `engines/engines.json`) and loads the built `phz.plg` and `pluck.plg` from the card like every other engine plugin. They are no longer built here: `SYNTH_ENGINES` does not accept `PHZ` or `PLK`, and `engines/check_modules.py verify-external DIR` checks the files of the private repository against the numbers kept here. The CPU figures below stay as measured. The tracker-ff releases up to 1.1.0 contain the PHZ source.

### JU6 and the other OpenTracker 3.x engines

Their source is in the public repository poly-engines-os since 1.2.0 (`src/synth-open/ot-synths/`, `engine_<module>.cpp`, `plug/synth/<module>_plug.cpp`), the text below describes it. `ot-synths/<module>/<module>.{cpp,h}` are opentracker-synths files (`eb150a5f741b`, MIT) unmodified apart from a provenance line. They need the 3.x `audio/synth_engine.h` (adds `slotPost`, `SynthHeld`, `cutoffOct`), `audio/bbd_chorus.h` and the DSP headers `ot/audio/{chip,fm,va}_dsp.h` from OpenTracker `v3_fixes` (Apache-2.0, provenance lines added). Each engine has one `engine_<module>.cpp` with the `EngineDef` written from the module's descriptor (ranges, defaults, labels, value formats, enum names, sections, six default macros and curves by value format).

JU6 has a slot effect (chorus). With an engine that sets `EngineDef::slotFx` the instrument renders its voices into a private bus, runs `slotPost(slot, ...)` on it, adds the result to the mix and keeps rendering while the effect reports a tail. That path is compiled only when JU6 is picked (`OPEN_SLOT_FX`, about 0.4 KB). `SynthHeld` is never filled (only A-Type reads it) and `cutoffOct` stays 0 (the Performance filter offset, JU6 does not read it).

| Engine (name in picker) | stored | id / `.patch` | params | sections | voices | linked (B) |
|---|---:|---|---:|---:|---|---:|
| JU6 | 7 | 0x81 | 23 | 6 | 4 | 6,108 |
| FM4 | 9 | 0x80 | 43 | 6 | 4 | 7,291 |
| D-Type (DTYPE) | 10 | 0xA2 | 35 | 9 | 4 | 9,869 |
| CS-Type (CSTYPE) | 11 | 0x83 | 67 | 9 | 4 | 10,507 |
| A-Type (ATYPE) | 12 | 0x84 | 46 | 8 | 4 | 9,989 |
| T-Type drums (TTYPE) | 13 | 0x85 | 73 | 9 | 1 (kit) | 14,001 |
| 8-Type (8TYPE) | 14 | 0x86 | 56 | 7 | 4 | 15,474 |
| Hand-Type (HANDTYPE) | 15 | 0x87 | 52 | 8 | 4 | 9,140 |
| B-Type (BTYPE) | 16 | 0x88 | 27 | 5 | 4 | 9,740 |
| Arc-Type (ARCTYPE) | 17 | 0x89 | 62 | 8 | 4 | 13,939 |

The on-screen names are the picker names (`DTYPE`, `HANDTYPE` and so on), not OpenTracker's "D-Type". The folder `Patches/<NAME>/` and the model list use the same string. The model list scrolls (7 rows) and the widest names (HANDTYPE, ARCTYPE) were not looked at on a device. The engines keep their state in the slot scratch (384 B, `static_assert`ed in each engine) and the voice state, so the only static RAM is T-Type's send tap pointers (48 B), JU6's chorus lines (1,536 B) and about 10 B of `.data` per engine. They compile at `SYNTH_OPT_ENGINES` (`-O2`), FM4 and B-Type at `-O3` (`SYNTH_OPT_FM4`, `SYNTH_OPT_BTYPE`).

What the host smoke test checks for every engine: the Init patch sounds (finite, non silent), a sweep of every parameter to its far end changes the output for a share of them (at least 3 and a fifth of the parameters, the ones that do not change it at the Init patch are things like a muted operator or a section behind a switch), velocity 32 and 127 both sound and the level follows velocity, a second note makes 2 voices (1 for T-Type) and a fifth note steals, a patch save and load round trip and ids that are unique and equal to the poly-editor's table. Known gaps, not features invented to hide them:

- **A-Type**: the Keyboard "Duo" mode (VCO 1 follows the lowest and VCO 2 the highest held note on a Poly 1 slot) and the Poly 1 note priority (Priority: Last, Low or High) read `SynthParams::held`, which this shim never fills (the held-note registry of OpenTracker's `SynthBank` is not ported). Both parameters show and save, but Duo plays like Normal and the priority is always Last.
- **T-Type**: a drum kit like PERC, one voice per instrument, polyphony does not apply. Note map (`drumForNote`): 60 kick, 61 rim, 62 snare, 63 and 71 clap, 64 to 67 hat (closed to open), 68 cowbell, 69 crash, 70 ride, 48 to 59 the three toms (4 semitones each, the notes between detune the tom), below 48 the low tom, 72 and up wrap onto 60 to 71. Velocity is the Accent parameter's share. The 22 Sends parameters (per drum delay and reverb send) do nothing, as for PERC. The kit cost governor (`kitCost`) is on.
- **D-Type id**: its OpenTracker id 0x82 is PHZ's, so D-Type is keyed 0xA2 (type id and `.patch` engine id), the same as the poly-editor stores it, so a D-Type patch saved by the editor loads on the device.
- None of the OpenTracker 3.x engines has been heard on a device.

### CPU at four voices

Emulated with `tools/phz_cycles/run_bench.sh <FLAGS> <ENGINE>` (522,240 cycles per block, no flash wait states, the shim's own mixing and the rest of the tracker's block not included). The bench renders the voices as the shim does (4 voices playing the chord 57 61 64 68, `slotPost` for JU6). Kit engines (PERC, T-Type) are one voice that gets 5 hits every 8 blocks. "Worst" is the most expensive of: default patch, every parameter at its max, at its middle (voices still alive) and, for CHIP, each model. The bench's cost table reads about 5 % high against the device (Dirt block 21,786 emulated, 20,685 on the device). Percent of a block, `-Os` is the flags up to 1.1.0, `-O2` today's (`-O3` for FM4 and B-Type):

| engine | default patch, `-Os` | default patch, `-O2` | worst, `-Os` | worst, `-O2` | note |
|---|---:|---:|---:|---:|---|
| ACD | 12.8 | 11.6 | 24.9 | 20.5 | |
| FAT | 13.7 | 13.7 | 22.7 | 22.7 | always `-O2` |
| VAP | 13.2 | 12.8 | 18.7 | 18.0 | |
| WTFM | 16.1 | 13.4 | 52.0 | 31.6 | |
| PERC | 13.8 | 9.1 | 13.8 | 9.1 | 1 voice, 5 hits |
| CHIP | 57.1 | 48.8 | 75.0 | 62.3 | upstream voice (998b06d) with the MK66 fast path |
| JU6 | 21.1 | 18.6 | 24.0 | 21.4 | with chorus |
| PHZ | 17.4 | 17.4 | 23.5 | 23.5 | always `-O2 -ffp-contract=off`, worst model x 4 from the dedicated bench (5.87 %) |
| FM4 | 32.5 | 16.6 | 32.6 | 16.8 | `-O3` |
| D-Type | 27.0 | 25.9 | 42.8 | 34.9 | |
| CS-Type | 46.8 | 30.5 | 74.9 | 46.3 | |
| A-Type | 27.3 | 22.5 | 37.0 | 27.3 | |
| T-Type | 10.9 | 9.2 | 11.2 | 9.2 | 1 voice, kit |
| 8-Type | 24.3 | 14.8 | 61.4 | 42.5 | |
| Hand-Type | 12.2 | 9.3 | 12.7 | 9.3 | |
| B-Type | 14.1 | 8.8 | 21.8 | 16.2 | `-O3` |
| Arc-Type | 33.2 | 21.0 | 41.7 | 27.7 | |
| PLK | 5.6 | 5.6 | 6.3 | 6.3 | always `-O2 -ffp-contract=off` |

The bench's `memset` is a byte loop like the plugin runtime's was. The plugins now store words (`plug/plug_rt.c`), which takes another 1.2 to 2.3 % off CS-Type, B-Type and 8-Type and 0.7 % off PLK (worst block instructions in `emu_synth.py`, the same harness on both sides). The built-in engines always had the firmware's word-wise `fastmem.S`.

Synth automation (bench scenarios 30 to 32, `bench_engine.cpp`): 30 ramps every parameter a slot may drive (triangle over its range, Volume kept) with `serial` and `moving` set every block, an upper bound no 4 slots reach. 31 ramps the first 4 of the picker order (wave group first), the realistic 4 slot case. 32 is the modulator alone (4 slots stepped and added, the engine sees the default values): 554 cycles per part and block (0.11 points) for every engine (469 for FM4 and B-Type at `-O3`). Percent of a block at 4 voices with the engine DSP at `-O2` (`-O3` for FM4 and B-Type), delta against the default patch:

| engine | default | 31: 4 params moving | 30: every param moving |
|---|---:|---:|---:|
| ACD | 11.6 | 11.9 (+0.3) | 16.7 (+5.0) |
| FAT | 13.7 | 19.1 (+5.4) | 20.4 (+6.8) |
| VAP | 12.8 | 19.0 (+6.2) | 19.0 (+6.2) |
| WTFM | 13.4 | 15.4 (+2.0) | 24.9 (+11.5) |
| PERC | 9.1 | 9.2 (+0.2) | 7.3 (-1.8) |
| CHIP | 48.8 | 48.8 (+0.1) | 58.7 (+9.9) |
| JU6 | 18.6 | 18.8 (+0.2) | 18.8 (+0.3) |
| PHZ (4 voice generic bench) | 17.4 | 17.7 (+0.3) | 18.0 (+0.7) |
| FM4 | 16.6 | 17.1 (+0.5) | 17.4 (+0.8) |
| D-Type | 25.9 | 26.3 (+0.3) | 28.7 (+2.8) |
| CS-Type | 30.5 | 31.4 (+0.9) | 45.4 (+14.9) |
| A-Type | 22.5 | 22.8 (+0.4) | 25.8 (+3.3) |
| T-Type | 9.2 | 9.3 (+0.2) | 8.8 (-0.3) |
| 8-Type | 14.8 | 15.4 (+0.6) | 19.7 (+4.9) |
| Hand-Type | 9.3 | 14.1 (+4.8) | 14.5 (+5.2) |
| B-Type | 8.8 | 13.0 (+4.2) | silent, not valid |
| Arc-Type | 21.0 | 21.4 (+0.4) | silent, not valid |
| PLK | 5.6 | 7.4 (+1.9) | 7.4 (+1.9) |

The modulator is cheap, the cost is the engine's recompute when its values move: most engines refresh once per part and block (under 1 point), FAT, VAP, WTFM, B-Type, Hand-Type and PLK also work per voice while `moving` is set on their wave and pitch params (+1.9 to +6.2 points). Every engine stays far below its own worst case, CHIP (48.8) and CS-Type (31.4) with 4 moving params are the highest. PERC and T-Type are kits: with every param moving the hits are cheaper than the default patch. Emulated, not measured on a device.

Read against the governor (cuts at a block >= 98 to 100 %, or an average >= 80 %): with the default pool of 4, one slot at 4 voices or three slots sharing 4 sound at most 4 voices, so the worst total is the 4 voice figure of the dearest engine. CHIP worst (Super Square, 62 %) is the dearest, then CS-Type (46 %) and 8-Type (42 %), the rest fit with room. Three slots each asking for 4 voices is 12 voices on paper, 3 x the figures above, which only the pool (4) keeps from happening. Raising the pool budget to 8 would overload the same engines. Not measured on a device.

## Engine icons

The list and editor icons are lettered name logos (80x29 mini and 108x40 medium pills that spell ACD, FAT, VAP, WTFM, PERC, CHIP and PHZ), not pictures. There is no artwork for the OpenTracker engines, so each one wears the logo of the Polyend engine closest in character (`engine_list.h`, MINI_ICON and ICON). Only the stored ids and `.patch` numbers matter for files, the icon is display only. All seven logos are linked in the 16 engine image already, so this mapping costs no flash. DIRT (shaping, no open engine) and its Shaper glyph stay out.

| Engine | Icon | Why |
|---|---|---|
| ACD, FAT, VAP, WTFM, PERC, CHIP, PHZ | own | |
| JU6 | VAP | Juno-style poly virtual analog with chorus |
| FM4 | WTFM | four-operator FM |
| D-Type | ACD | mono three-VCO bass and lead voice, the monophonic analog logo |
| CS-Type | FAT | two-layer big polysynth, the fat analog logo |
| A-Type | ACD | duophonic mono-style lead, the monophonic analog logo |
| T-Type | PERC | analog drum machine, the percussion logo |
| 8-Type | CHIP | three-voice 8-bit computer chip |
| Hand-Type | CHIP | four-channel handheld console chip |
| B-Type | CHIP | polynomial counter 8-bit chip |
| Arc-Type | WTFM | four-operator FM console chip, the FM character wins over the chip label |

A logo is built in when any engine that wears it is picked: `FW_ICON_PERC` (PERC, T-Type) and `FW_ICON_CHIP` (CHIP, 8-Type, Hand-Type, B-Type) in `displayStructs.h`. When an icon is gated by `FW_TRIM` the test is `(!FW_TRIM || SYNTH_ENGINE_X)`, so plain builds stay byte identical.

## Patch files

`open_patchio.cpp` reads and writes the stock `.patch` format (a protobuf `Patch` message, as the Tracker Mini and OpenTracker use it). `PatchStore` (in `open_cold.cpp`) puts it on the SD card through the `FileSystem` the firmware registers:

- Library: `Patches/<ENGINE>/<name>.patch`, one folder per engine (`ACD FAT VAP WTFM PERC CHIP JU6 PHZ FM4 DTYPE CSTYPE ATYPE TTYPE 8TYPE HANDTYPE BTYPE ARCTYPE`), created on first save. The patch browser lists the names of the current engine sorted, without `.patch`.
- Workspace: `Workspace/<N>.patch` for the three slots. The firmware already writes edited patches there (`saveDirtyWorkspacePatches`), copies them into the project folder on project save and back on project open (`copyWorkspacePatches`), so a project reload keeps the edits.
- Patch slots: a synth part has 16 patches in the project. Slot 0 is `<N>.patch` as above, so stock firmware and Tracker+ open the song with that sound. Slot n from 1 to 15 is `<N>-<n>.patch` (`0-1.patch` is the second patch of synth part 0), in `Workspace/` and in the project folder, copied together with slot 0 on project save and open (project open removes the Workspace slot files that the project does not have, project save never removes a file in Projects). Stock firmware never reads the extra files. The slot selector is the seventh place of the Synth screen (Right from Delay send, encoder or Up and Down change the value). It opens the slot, saves the open slot first when it was edited and starts an empty slot as a copy of the open one without writing a file until the slot is edited or loaded from the library. The library Load and Save screens work on the open slot. The Patch step FX (`O - Patch`, value 0 to 15) copies the values of slot n over the part's live patch at the step. Macros stay. A slot with no file or with a file of another engine is ignored. The song's FX does not mark the patch edited, and entering the Synth screen or the patch editor puts the open slot's own values back. The FX is not offered by the Random FX picker. A knob edit made while the FX plays another slot goes to the open slot's edited values (the part plays them from then until the next FX step) and the editor shows the edited value of a knob while FX values play. A save copies the open slot out in one short audio-free window (the FX values go back first) and writes the file from that copy, so it is one patch whatever the FX does meanwhile, and the bank follows what was written. Every patch, names and backup file is written to `<file>.tmp` first and replaces the old file only when complete: `replaceFileOps` (`open_fileops.h`) moves the old one aside as `<file>.bak`, renames the new one into place, deletes the `.bak` and moves it back if the rename fails. FatFs cannot rename over a file, so this is not atomic against power loss: between the two renames the canonical file is absent and the old version is `<file>.bak`. Every reader (slot files, backup, names) therefore moves `<file>.bak` back to `<file>` when the card confirms the canonical file is absent, before it takes the file for missing. This is not complete end to end: slot 0 is loaded through a loader that only answers loaded or not, so when the move back fails on a slot 0 that exists only as `.bak` the part goes on to the library patch or Init, and the save of that replaces the file and deletes the `.bak`. The library browser lists only `*.patch`, so a library patch left only as `.bak` is not listed (loading it by name recovers it). Both are limits, the old file is never deleted by the replace itself. A leftover `<file>.tmp` is never promoted, a write that was not acknowledged may be half done, the next write of that file replaces it. A save that fails (card full or an error), or one that fails on a part that was clean (an accepted library patch), is reported (`Error saving patch`), the old file stays and the part stays edited: the slot selector and an engine change do not go on until it is saved. A project save or open copies the bank files the same way (`open_bankcopy.h`, a source file is also taken from its `.bak`), and a project open removes a Workspace slot file and its `.bak` only after every copy worked and only when the card said the project has no such file (FatFs `FR_NO_FILE`, a failed lookup is not proof). A project save only copies the Workspace files that exist, it never deletes or renames away a slot file or `.bak` in Projects, and it ends with an error if a file could not be copied or the card could not say whether a Workspace file exists. A project open whose bank copy fails tries it once more and then loads the requested project with the patch files the Workspace has, as stock does: no error is shown and the song, patterns and samples are kept (a project open never ends in a new empty project because of a patch file, only a Workspace that cannot be opened does), and the audio engine restarts only at the end of that load. A project save also ends with an error when the names file cannot be written. A card read error is not taken for the end of a file: the slot is not loaded and the names file is not rewritten from empty names, and a slot file that is there but does not open is an error, not an empty slot. When the names file could not be read at project load, every save reads it again first (`patchNamesPersist`): the names on the card are kept and the names edited since are merged over them. While it still cannot be read the file is left untouched and the save does not fail for it (stock has no names file), the next save tries again.
- Limits: a slot cleared in the editor reappears after the saved project is opened again, because a project save never deletes a slot file in Projects. If the card fails during a project open, a slot whose file was not copied into the Workspace can show as empty, and saving an edit there overwrites the project's original file of that slot (slot 0 of stock firmware behaves the same way).
- Slot names: the `.patch` files carry no name, so the name a slot shows on the Synth screen is kept in `<N>.names` next to the slot files, in `Workspace/` and in the project folder (copied like a slot file, ignored by stock firmware). The file is ASCII text: the first line is `tffnames 1`, then one `<slot>=<name>` line for every named slot from 1 to 15 (a name is 1 to 31 printable bytes, slot 0 is not listed because the project stores its name). It is written when a slot gets another name (library Load, Save as, Init, opening a slot) and on project save, and read on project load. A part that never named a slot has no such file. A slot the file does not name shows `Unnamed`, and a missing or damaged file reads as no names (every line is checked, a file is read up to 1,024 bytes). An engine change sends the part back to slot 0 and keeps the names of slots 1 to 15 (their files stay, so do their names). A line `b=<name>` holds the name of the slot 0 backup (see Engine change). The codec is `open_slotnames.h`.
- FatFs file pools: tracker-ce 5b6b0f2 moved the MK66 `FIL` pool (24 x 560 B = 13,440 B, each with its 512 B sector buffer) and the `SdFile` pool (6,240 B) from the SDRAM into the internal RAM, no reason given beyond "filesystem optimization" (internal RAM is faster for the FatFs window and the partial sector copies). SYNTH_OPEN builds put them back (`SDRAM_FILE_POOLS=1`, `-DFW_SDRAM_FILE_POOLS`, the top Makefile sets it), the stock build keeps upstream's placement. Safe for the SDIO path: the pools sat in the SDRAM from the first tracker-ce commit up to 5b6b0f2, the SDHC ADMA2 already writes every whole sector read straight into SDRAM buffers (sample loads), only the MK66 code cache is on (`LMEM_PCCCR`, the system cache that would cover 0x70000000 is off, so there is no coherency step), the ADMA table stays in internal RAM, `FIL` and `SdFile` are 4 byte aligned and both pools are reset at allocation (`memset`, `SdFile{}`) so `.noinit` needs no constructor (`check_static_init.py`: 0 SDRAM accesses). Internal RAM left for heap and stack in the PLUGS image: 71,400 B at bbfd0fc, 66,376 B after the merge (upstream eb369af adds 4,672 B to `cPatternEditor::trackerPattern`), 46,696 B with upstream's placement.
- Memory: the load, save and slot paths keep their multi-KB buffers in the SDRAM (`.noinit.$EXTERNAL_RAM`), not on the heap, so a short heap cannot fail a slot select or leave the Patch FX with an empty bank. File and decode scratch for every `.patch` read and write 9.8 KB (`PatchScratch`, one lease at a time, main thread), Patch FX bank rows 17,136 B (3 parts x (16 slots + the shadow row) x 84 floats, 84 being `kMaxParams`, the PERC count), the `OpenPatch` pool 16,000 B (5 objects of about 3,200 B with the automation slots, a full pool falls back to the heap). What stays on the heap: the `std::vector` of parameter metadata inside each `OpenPatch` (52 B per parameter, 1.2 to 4.4 KB) and the 1 KB slot names buffer.
- Engine change: the part starts on slot 0 with the Init patch of the new engine (as stock does). The slot files 1 to 15 of the old engine stay in the project and are ignored, a slot that holds a file of another engine cannot be opened, until the part is switched back to that engine. Slot 0 of the old engine is kept once: before the Init is written the part's `<N>.patch` (the file, also when slot 2 or another slot is open, after saving that slot if it was edited) is copied to `<N>-b.patch` (and its name to the `b=` line of `<N>.names`), in `Workspace/` and in the project folder, copied like a slot file (it is not a slot, the selector and the Patch FX never see it). The copy is skipped when slot 0 is an untouched Init (its bytes are those of a new patch of the engine), so A to B to C to A still brings A's patch back. After the change the backup is loaded as slot 0 instead of Init when it is a file of the new engine (the engine field of the file decides, as for every patch file), with its name, and the popup says `Previous patch restored`. There is one backup per part: slot 0 edited on B and then left for A replaces it by B's patch and A gets an Init. The backup is never deleted, the next change of an edited or loaded slot 0 overwrites it. If the card fails while slot 0 is read or its backup written, the engine does not change (`Error saving patch`, `Backup::kFailed`), a slot 0 file that is missing, damaged or of another engine has nothing worth keeping and the change goes on. The untouched Init check builds the Init data from the engine definition (`initPatchData`), not from a second patch, so it needs no heap.
- The file holds the engine id, all parameters (stored values, same indices as the editor), eight macros (name, bipolar, up to six targets with parameter and amount, M7 Velocity and M8 Pressure included) and the constant `Extra` block. Names, the OpenTracker extension field and unknown fields are kept as raw bytes and written back (up to 768 B), so loading and saving does not drop them.
- A load needs the same engine as the patch it goes into and a file of at most 8 KB. A parameter that is missing, not finite or out of range takes its default. A file cut inside a field is refused and the patch is left as it was. The apply runs with audio interrupts off. A patch of an engine the build lacks is refused. Saving goes through a temp file and a rename, see Patch slots for what that does and does not guarantee.
- Engine numbers in field 1: Polyend's `InstrumentTypeId` for the Polyend engines (PERC is 6 `kDrumSynth`, CHIP is 8, so the PERC and CHIP patches of Tracker+ and Mini CE are the right format and order) or OpenTracker's engine id (JU6 is 0x81, the same as OpenTracker 3.x `Patches/JU6/`). PHZ is 0x82, our own number, so Polyend's Synth PHZ patches (id unknown) are refused. FM4 is 0x80, CS-Type to Arc-Type are 0x83 to 0x89 and D-Type is 0xA2 (see above). The stored numbers 9 to 17 are the poly-editor's as well. There are two numbers per engine, the stored one (project files, `engine_list.h` column STORED: PERC 4, CHIP 6) and the `.patch` one (PERC 6, CHIP 8). They differ on purpose and each lives in its own file type. The smoke test pins the table and checks that the three numbers (stored, type id, `.patch` engine) are unique and map back for every picked engine.
- Verified on the host: save then load gives equal parameters and macros for every engine, load then save gives the same bytes. With OpenTracker's own `patch_pb.cpp` as oracle (not in the repository, `-DOT_ORACLE`, see the test header) its Init patches load here and save back byte for byte and OpenTracker re-encodes our files identically. Polyend Mini factory `.patch` files load and play on a device (confirmed on an original Tracker with a pre-release build).

## Synth automation

The third Synth page (Params cycles Synth Parameters 1/3, Synth Macros 2/3, Synth Automation 3/3, the mini menu keeps its two entries and counts Automation as the second). Four slots per patch, each Off, an LFO or an envelope on one engine parameter or one user macro. The page is the sample automation page with its wide destination column split in two: column 0 the slot (`1 Cutoff`, `2 Off`), column 1 the destination, columns 2 to 7 as the sample page (Type, then Attack Decay Sustain Release Amount for an envelope, Shape Speed Amount for an LFO with columns 6 and 7 covered). The title shows the open patch slot and its name (`Synth Automation 3/3 - Slot 2 Bass`): the automation belongs to that slot's file. Places above Type are locked while the slot is Off, an LFO stops at its Amount, a second press on a list rolls it over, Delete puts the selected value back to its default, pads and the preview button play notes. Every edit runs with audio interrupts off and marks the patch edited (`Synth::setModSlot`), never the project structure. A part whose engine is missing (plugin not attached) shows its slots and edits nothing, so its files are never rewritten. The page uses fixed buffers only (no `new`, the MK66 `operator new` returns null on a short heap). Code: `core/interface/instrumentEditor/SynthAutomation.cpp` (`GUI_SYNTH_AUTO`: SYNTH_OPEN only, the IMX synth GUI is unchanged).

- Destinations: every parameter of the engine except enums, on/off switches and unused slots (`min == max`), plus Macro 1 to 6. Stepped parameters (semitones, ratios, PERC Body Wave) are allowed and step audibly, Volume is allowed (tremolo). Order in the list: the wave and position like parameters first (a word of the name starts with Shape, Wave, Pos, PW, Timbre, Morph, Table, Duty, XMod or YMod, case-insensitive, `isWaveParam`), then Macro 1 to 6 by their names, then every other parameter in engine order. The bottom label shows the section of the parameter. Wave groups of the built in engines (pinned in the smoke test): ACD PW, PW LFO, PW Env; FAT Timbre; VAP Shape 1, Shape 2, PW 1, PW 2; WTFM Shape 1, Shape 2; CHIP Duty / Shape, PWM Rate, PWM Depth; PERC 12 (Body Wave and Timbre of each drum, plus false positives such as Decay Shape and Waveshaper Level, which only move up the list). The name match needs no plugin ABI or manifest change and covers every `.plg` as it is.
- Identity: a slot stores the parameter's index, its name (up to 20 characters) and which occurrence of that name it is (`nth`: WTFM and PHZ have three Attack params). On load, index plus name plus occurrence must match, else the nth parameter of that name is taken (a plugin update reordered its table: the slot follows the param), else the slot is unresolved: it shows `? <name>`, drives nothing and is written back as it was read, so the old plugin version brings it back. Picking another destination clears it. A placeholder engine (plugin not attached) resolves nothing and keeps every slot as read. A macro slot is the macro number.
- LFO: Rev Saw, Saw, Triangle (starts at 0 rising), Square, Random (sample and hold, a new value at every wrap), the sample page's 29 tempo synced speeds (128 steps to 1/64 step, a step is a 16th, period `15000 / bpm * rate` ms, 120 BPM when there is no tempo), bipolar.
- Envelope: Attack linear 0 to 1, Decay linear to Sustain, Release linear to 0 from wherever it starts and always in the Release time. Times 0 to 10 s with the sample page's 190 step law (10 ms steps to 1 s, 100 ms steps to 10 s), a 0 ms stage is one block. One ADSR per Envelope slot, at part level: one gate for the part, never per voice (per voice envelopes would mean per voice parameters in every engine). The attack restarts on every note from the current level (no click, in a tracker line the previous voice is usually still held). The release starts when the last held voice of the part is released, Stop and All Notes Off release it too. After silence (no voice sounding) the envelope starts from 0 and an LFO from phase 0, a note while voices sound leaves the LFO running.
- Amount: -100 to +100 % of the destination's range (the file is sint32, a wider range later is a UI change). A negative envelope Amount inverts it. A macro destination adds `amount x out` to the macro's normalised value, clamped to the knob's range, so 100 % on a unipolar macro sweeps it across its whole range. A macro slot and a macro target on the same param add up with the param's value and are clamped once.
- Render path: config in the `OpenPatch` next to the macros, run time state in the instrument (internal RAM, about 100 B per part). `modAdvance` runs once per rendered block before `refreshParams` with the block's tempo, `OpenPatch::effective(out, modOut)` adds the slots like macros. The patch's values, `getParamValue`, `getValuesRaw` and the editor's final value stay unmodulated. A modulated destination changes `params.end` every block, so `serial` bumps and `moving` is set: the engines' per block recompute is the real CPU cost (below). With every slot Off nothing is stepped and `effective` is as before. An idle part does not render, so its automation freezes, offline WAV export renders it as played.
- Patch FX and slots: the automation belongs to the open slot's file, not to a Patch FX row. An `O - Patch` step swaps only the values, the open slot's automation keeps running over the FX values. Opening another slot brings that slot file's automation and restarts the modulators. Library Load and Save carry the block with the sound (a library patch without it turns automation Off), the slot 0 backup of an engine change keeps it (raw bytes), a new engine starts with automation Off.
- File: `.patch` field 64 (tag `82 04`), after Extra: `{1 version = 1, 2 repeated Slot{1 type (1 LFO, 2 Envelope), 2 kind (0 param, 1 macro), 3 index, 4 name, 5 nth, 6 amount (sint32 zigzag), 7 shape, 8 speed, 9 attack ms, 10 decay ms, 11 sustain %, 12 release ms}}`, varints, zero omitted, an Off slot is an empty Slot, trailing Off slots are not written. Left out when every slot is Off: a patch without automation saves byte identical to before (the untouched Init check of the backup still holds). A 4 slot example is 111 B, the worst case (20 character names, every field at its top) 202 B (`kModBlockMax` 256). A version above 1 is kept raw and written back (a later image's block, replaced if this image's automation is set), a malformed block is dropped and the sound still loads, values out of range go back to the defaults. Older images (1.1.0, PLUGS 41513e12) keep field 64 as an unknown raw field and write it back, a worst block fits their 768 B raw budget. Stock files use fields 1 to 9, OpenTracker 2 to 5, 10, 11 and 16: 64 collides with none.
- RAM: `OpenPatch` grows by the 4 slots (160 B, the SDRAM pool is 16,000 B instead of 15,200), `PatchData` by the same (SDRAM scratch). The page's list buffers (832 B) are one plain data block in the SDRAM (`.noinit`, no constructor), so internal RAM (heap and stack) pays 32 B. `check_static_init.py`: 0 SDRAM accesses.
- Flash: +9,600 B on the PLUGS engine set (ACD FAT VAP WTFM PERC CHIP, TRIM, PLUG_HOST, engine DSP at `-O2`: 964,668 to 974,268 B, the AUTO image ends 0xEDDBC, 58,608 B under the device-proven 0xFC2AC). From the symbol sizes: the page about 3.9 KB, the codec 1.2 KB, import and resolution 0.6 KB, the render hook at -O2 (step, effective, note and gate events) about 2.4 KB, the rest glue. The plan's estimate was 5.2 to 7.0 KB.
- CPU: see the automation rows under "CPU at four voices".
- Not in this version: graph view, 8 slots (the file format already allows them), per voice envelopes, free running or song locked LFO phase, per Patch FX slot automation, unipolar LFO, delay and hold stages, gain, pan and sends as destinations, the moving value on the destination's bar.

## Pitch bend

`MidiEvent::kPitchBendChange` (from a MIDI keyboard through the virtual port and from the tracker's Micro-tune FX, which `mtSequencer` sends as a full +-8192 bend to synth instruments) sets the instrument's bend, applied to every voice of the slot through `SynthVoice::bend` (ramped over the block from `bendStart`). The range is +-1 semitone at full deflection, our own choice, so the Micro-tune FX reads in cents (-99 to +99). The range of the closed firmware is not known. Transport Stop resets the bend to 0.

## Dirt master insert

OpenTracker's DIRT (drive and bitcrush, `opentracker-fx` `modules/dirt`) runs live on the master bus. `FF_DIRT=1` is the default of a SYNTH_OPEN build (`FF_DIRT=0` leaves it out, build that with its own `SYNTH_TAG`, a define is not a make dependency). It is code in `synth-open/` plus a few lines behind `FEATURE_FF_DIRT` in the core, plain `make original` is untouched.

**Placement.** OpenTracker 3.x runs its master inserts after the master sum (dry tracks, delay and reverb returns, synth parts, saturation applied) and before the limiter (`audio/inserts.h`). Here the same point: `AudioEffectDirt` (stereo, 2 inputs and 2 outputs) sits between the tracks and sends mixers and the two limiters (`audioEngine::connectMixerToOutput`, `AudioOutput`). It is created before the limiters, so the audio objects (updated in creation order) keep the master bus in one block. The limiter, the final mixer, the stream output and the master export tap all come after it, so the master export stem carries the effect (the per track and mixer stems are taken before it).

**Controls.** The master screen's Saturation entry (button 5 on the Master page, saved in the project like before) has five more entries after Drive+: `Dirt Soft`, `Dirt Hard`, `Dirt Fold`, `Dirt Asym` and `Dirt Crush`. Picking one runs the mixers at hard clip and the insert with that preset. The stock entries run the insert off. No new parameter UI: the presets are a fixed table (`ffdirt::kPresets`):

| Entry | Shaper | Drive | Bits | Rate | Tone |
|---|---|---|---|---|---|
| Dirt Soft | rational tanh | 35 | 16 | 1 | off |
| Dirt Hard | clip at +-1 | 35 | 16 | 1 | off |
| Dirt Fold | triangle fold | 35 | 16 | 1 | off |
| Dirt Asym | biased tanh, 10 Hz DC block | 35 | 16 | 1 | off |
| Dirt Crush | rational tanh | 15 | 8 | 4 | 70 (about 5 kHz) |

Mix is 100 % for all of them. Drive, Bits, Rate and Tone are the original's parameters and units (pre-gain 1 + 15 d^2 with d = Drive / 100, make-up gain 1 / (1 + 0.5 d)), so another preset is one line in `kPresets`. A project that uses a Dirt entry and is opened on a build without it falls back to Hard Clip (the existing range check in `fProjEdit.cpp`). The PLUG_HOST build (`PLUG_HOST=1` defaults to `FF_DIRT=0`) drops the five entries and opens such a project as Inserts with insert 1 = `dirt.plg` at the entry's values, or Hard Clip without the plugin (`plug/README.md`, On the device).

**Differences from the original.** Int16 blocks instead of float buses. The parameters are constant per preset, so the block ramps of Drive and Mix are gone, and a preset change fades the old one out and the new one in over one block each (through the dry signal) instead of the original's crossfade. A silent bus (NULL blocks) is left alone with the state cleared, like the original, which does not call its process for a silent bus: the effect costs nothing in silence. The quantiser is mid-tread (half a step added with saturation, then the low bits cleared, so one of the 2^Bits levels is 0): a zero or near zero input that is not a NULL block (an unmuted line-in, a reverb tail) gives zero out instead of a constant floor. The original's mid-rise quantiser never outputs zero and gave a floor of -49 dBFS at 8 bits. The int16 stage clips at full scale where the original's float bus would pass the overshoot to the limiter (Asym can go to about 1.03 of full scale on its negative side). The tone corner and the DC block coefficients are computed when a preset is picked (`powf` and `tanf` once), the block loop calls no libm function.

**Cost.** Emulated Cortex-M4F (`tools/dirt_cycles/run_bench.sh`, real `AudioEffectDirt` and `AudioStream`, cost table of `tools/phz_cycles`, **no flash wait states, not the device**), loud stereo input, cycles per 128 frame block over the same graph without the insert (2,625 cycles of stand-in source and sink):

| Entry | cycles | points of a 522,240 cycle block |
|---|---:|---:|
| insert off (pass through) | +239 | +0.05 |
| Dirt Soft, Dirt Asym | +13,598 | +2.60 |
| Dirt Fold | +11,769 | +2.25 |
| Dirt Crush | +8,734 | +1.67 |
| Dirt Hard | +8,341 | +1.60 |
| a silent bus | about 0 | about 0 |

Against the last device readings (max 76 %, avg 50 % on the test song, see Summary under Audio CPU optimisations) that is at most 2.6 points on top of the peak. Flash: the insert costs 2,200 B in the 16 engine TRIM image (`ffdirt::process` 1,148, `makeCoef` 372, `update` 348, presets and names about 120, the rest list text and glue). The whole release image ends at 0xFBB90 (1,031,056 B, 13,424 B under 0xFF000).

**Checks.** `tools/dirt_render.sh` renders a synthetic loop dry and through each entry into `build/dirt-wav/` and checks that wet 0 passes the input bit for bit, that a full scale square followed by silence leaves a finite state and that silence in gives exactly zero out, for every entry and for every Bits setting 4 to 16. `tools/dirt_cycles/run_bench.sh check` runs the firmware build of the per block code in the emulator and compares its output block with the host code: 0 LSB difference for Fold, Asym and Crush (the quantiser's `qadd` and mask against the host's 64 bit add), 1 LSB on 1 of 256 samples for Soft and Hard (neither has a quantiser, the same 1 LSB is there before the quantiser change). Not done: a device run, and the audio graph bench (`tools/audio_opt`) does not contain the insert.

## Flash trim (TRIM=1)

`TRIM=1` is an opt-in build switch for the "no feature loss" trims. It frees about 157 KB of the original's 1 MB flash, which is what lets the 16 release engines link. The plain build is untouched, and `TRIM=1` without `SYNTH_OPEN=1` is an error (stock `make original` has no trims, by design).

```
make original SYNTH_OPEN=1 TRIM=1 \
  SYNTH_ENGINES="ACD FAT VAP WTFM PERC CHIP JU6 PHZ FM4 DTYPE CSTYPE ATYPE TTYPE 8TYPE BTYPE ARCTYPE"
make synth-engines TRIM=1 [SYNTH_ENGINES="..."]   # budget table of the trim image
```

What it does:

1. The platform link line gets `-specs=nano.specs -Xlinker -u -Xlinker _printf_float` (`platforms/mk66/makefile`). The compile flags already said nano, the stock link line did not, so the image carried full newlib (printf 6.9 KB, scanf 6.0 KB, strtod/dtoa 13 KB) and the full libstdc++ (cp-demangle 28 KB through the verbose terminate handler). `%f` stays (about 50 UI call sites), scanf float is not needed.
2. `InstrumentTypeRegistry.h` uses `std::map` instead of `std::unordered_map` (`POLYSYNTH_REGISTRY_MAP`, only lookups by id).
3. Float instead of double math at 6 call sites: `cosf` and `tanf` in `mtEffectEQ.cpp`, `expf` in `mtEffectOverdrive.cpp`, `value == floorf(value)` in `fSongEdit.cpp` and `powf` in `control_sgtl5000.cpp` (MK66 copy). This drops pow, exp, fmod, remainder and the trig kernels from libm.
4. The PERC icons are out of the bitmap table unless PERC or T-Type is picked (`FW_ICON_PERC`), and the other lettered icons follow the same rule.
5. `-Os` (appended after `-O2`, per-path pattern rules `TRIM_OS_PATHS` in `core/makefile`) for `interface FileManager libs/NewDisplay effector Exporter synth source` and for the whole MK66 platform (teensy core, FatFs, SDMMC, USB, I2S). A list of `AudioEngine` files that run once at start or once per event, not per block, is also `-Os` (`TRIM_OS_FILES`: AudioInput, MixerAndFxs, AudioVoice, AudioOutput, ExportQueues, mtAudioPerformanceMode, mtAudioNoteOnHandle and mtAudioSequencer). The sequencer entry is the one that paid for the merge of upstream's relicense and file hardening. Only the note on handler of that file runs in the sequencer interrupt, a few hundred cycles per note at worst. Not touched: the per-block audio code in `AudioEngine`, `libs/Audio` and the engine DSP, which keeps its own flags (`SYNTH_OPT_ENGINES` `-O2`, FM4 and B-Type `-O3`, PHZ and PLK `-O2 -ffp-contract=off`, an explicit per-object `EXTRA_OPT` wins over the TRIM pattern rules).
6. `platforms/mk66/source/fastmem.S`, a word-wise `memcpy` and `memset`, see "Idle audio load".

Source level trims sit behind `-DFW_TRIM`. TRIM uses its own build directories (`core/build-MK66-ORIGINAL-SYNTHOPEN[-E<names>]-trim` and `platforms/mk66/build-trim`) so trim and plain objects never mix. Not in TRIM: LTO, font recompression, a printf replacement and any feature removal. The three size guards and `make synth-engines` all work with TRIM=1.

The cold shim code is split for the same reason: `open_synth.cpp` keeps the render path (instrument, voice pool, `OpenPatch::effective`) at `-O2`, and everything else (`open_cold.cpp`, `open_patchio.cpp`, `stub_fmt.cpp`) is built at `-Os` (`SYNTH_OPT_COLD`). The knobs `SYNTH_OPT_ENGINES`, `SYNTH_OPT_GUI`, `SYNTH_OPT_SHIM`, `SYNTH_OPT_COLD`, `SYNTH_OPT_FAT`, `SYNTH_OPT_CHIP`, `SYNTH_OPT_FM4`, `SYNTH_OPT_BTYPE`, `SYNTH_OPT_PHZ` and `SYNTH_OPT_PLK` set the `-O` level of each file group. A stray `<iostream>` anywhere pulls about 150 KB of locale code on MK66, so after a change `nm` the `.axf` for `std::locale`.

### Flash budget of the 1.1.0 image

Sizes are text+data from `arm-none-eabi-size` against the cap of 1,044,480 B, toolchain 10.3.

| Build | text+data | free | highest .ptf address |
|---|---:|---:|---|
| `SYNTH_OPEN=1 TRIM=1`, the 16 release engines (Dirt insert on, the default) | 1,031,056 | 13,424 | 0xFBB90 |

The highest image end known to boot on a device is 0xFC2AC (1,032,876 B, an early pre-release image), so the release image ends 1,820 B below it. Without TRIM the 16 engine set does not link. The flash cost of each engine in the 16 engine TRIM link (`make synth-engines TRIM=1`, within about 1 KB of the real marginal cost) is ACD 9,374, FAT 12,952, VAP 15,514, WTFM 14,793, PERC 23,655, CHIP 10,685, JU6 6,108, PHZ 15,154, FM4 7,291, DTYPE 9,868, CSTYPE 10,507, ATYPE 9,989, TTYPE 14,025, 8TYPE 15,474, BTYPE 9,740 and ARCTYPE 13,939 B, with a base of about 824,800 B without engines. HANDTYPE costs about 9,140 B when it is picked. Re-run the target for exact figures, they move with every merge.

RAM of the 16 engine TRIM image (`-print-memory-usage`): about 139.3 KB of 256 KB (`.data` 5.7 KB, `.bss` 110.2 KB, `.dmabuffers` 20.7 KB) and about 15.6 MB of the 16 MB SDRAM. Static RAM of the engine objects: JU6 1,536 B (chorus lines), T-Type 48 B and PERC 48 B, the other engines 0 B of `.bss` and 8 to 12 B of `.data`. The remaining flash levers, none of them needed now: a printf smaller than nano's float one (about 7.5 KB) and font recompression.

Known risks of TRIM that a device run has to keep an eye on: nano malloc and nano stdio replace newlib's (`Profiler.cpp` and the Arduino `String` class use malloc and `%f` goes through the nano float printf), and `-Os` on `source/` and on the platform (ISRs, SD read speed) and the `powf`, `cosf` and `tanf` coefficients (tiny numeric drift in the EQ and the SGTL5000 constants) have not been listened to separately.

### Idle audio load

Observed on an original Tracker with pre-release builds of this design (`/.develop` readout, same project, sample tracks only): stock CE read min 53 % and max about 80 %, a first TRIM image read min 68 % and max about 110 %. `-Os` was not the cause, because stock CE built with exactly TRIM's `-Os` read the same as stock.

Cause: newlib-nano's `memcpy` and `memset` are byte loops. The stock link has no `-specs=nano.specs` and gets newlib's word-wise ones (memcpy 308 B, memset 164 B), while TRIM links nano (memcpy 28 B: `ldrb`, `strb`, `cmp`, `bne`). Emulated on a Cortex-M4 with the cost model of `tools/phz_cycles` (Unicorn, no flash wait states):

| call | nano | stock newlib | fastmem.S |
| --- | --- | --- | --- |
| memcpy 256 B (one audio block) | 2,063 cycles | 295 | 282 |
| memset 512 B (a muted int32 mixer channel) | 4,618 | 335 | 352 |

The audio interrupt calls them every block (the whole block is 522,240 cycles): `AudioStream::receiveWritable` (256 B copy of every shared block), `MonoToStereo::update` (every active voice), the bypass copy of `AudioFilterStateVariable::update`, `AudioOutputStream::update` (three 256 B copies or clears), `applyGain` and `applyGainAndCopy` (clear of a muted channel) and the envelope clears. Three 256 B copies per playing voice cost about 5.3 k cycles more than stock, so 8 playing tracks are about 8 points of a block before the mixer and output stages.

Fix: `platforms/mk66/source/fastmem.S`, word-wise `memcpy` and `memset` (ldm and stm, 4 words per iteration, byte loop for a misaligned pointer, +98 B), linked only with `TRIM=1` (`TRIM_FASTMEM=0` leaves it out for comparison builds). Checked in Unicorn against both libcs: 12,000 random cases per implementation (size 0 to 600, all source and destination offsets 0 to 7, fill values with high bits set, guard bytes before and after, r4 and r5 preserved). `memmove`, `memcmp` and `strlen` stay nano's, they are not on the audio path. The teensy_core `memcpy-armv7m.S` and `memset.S` are guarded by `__OPTIMIZE_SIZE__` and the platform `.S` rule passes no `-O`, so they assemble to empty objects in every build.

Ruled out in code: the `FW_TRIM` float math (it runs at init, and `expf` instead of `exp` is cheaper), libm (nano.specs does not change it), data placement (no hot object moved between flash, SRAM and SDRAM) and code placement (no audio code runs from RAM in either build, `.audio_ram_functions` has no effect on MK66).

### Idle synth parts

Observed on a pre-release SYNTH_OPEN build without TRIM and with ACD only (`/.develop` readout, same project, no synth notes): stock CE min about 43 % and max 80 %, the synth build min 52 %, avg 77 % and max 100 %, with the top bar yellow all the time.

Cause: `PolySynth::update` (`core/libs/Audio/synth_PolySynth.cpp`) only skipped a part whose `getVoiceCount()` is 0. A part that is allowed a voice but has none sounding (Voices 1 is the default: `kDefaultSettings` in `core/synth/Synth.cpp`, `Synth::init` and a new project) went the whole way every block: 2 block allocations, `renderAudioBuffer` (clears of 4 x 128 floats, mono mix down), `saturateSoftClip` twice at the default -9 dB gain (a `powf`, a clamp, a cube and a `VDIV` per sample), the copy and the float to int16 conversions, then 4 transmits of blocks of zeros. Those blocks made six panning amplifiers, six RMS analysers and the six extra mixer inputs (the `AudioMixer<11>` that stock has as `<8>`) do real work.

Emulated cost of one `update_all` (`tools/idle_cycles/run_bench.sh`, real AudioStream, amplifier, RMS, mixer and applyGain code, `PolySynth::update` as a literal copy with the Synth stubbed, Unicorn Cortex-M4F with the cost table of `tools/phz_cycles`, **no flash wait states, not the device**, 522,240 cycles per block):

| 3 synth parts | cycles | % of a block |
| --- | --- | --- |
| no synth objects in the graph | 8,270 | 1.58 |
| Voices 0 (stock `continue`), or Voices 1 idle with the fix | 9,064 to 9,138 | 1.74 to 1.75 |
| Voices 1, nothing sounding, 0 dB part gain, before the fix | 57,898 | 11.09 |
| Voices 1, nothing sounding, -9 dB part gain (default), before the fix | 82,390 | 15.78 |

So an idle part with a voice allowed cost 9.5 to 14.2 points of a block for the three parts. What stays with the fix is about 800 cycles (0.15 points): 13 active objects that find no input and `AudioMixer<11>` instead of `<8>`.

Fix: `open::partIdle(part)` (`open_load.h`, `open_synth.cpp`) answers with the test `OpenInstrument::render` already used for its early return (no engine or patch, Voices 0, no voice on, no slot effect tail). `PolySynth::update` skips such a part with `continue` and transmits nothing, and a missing block is silence for the amplifiers, analysers and mixers. Offline render keeps the old path (`AudioStream::renderMode`), so exported stems are unchanged. `updateSynthParams` skips a part with no dirty parameter flag with one `none()`. The host smoke test covers it (idle with a voice allowed, not idle on note on, held and in the release, idle again after the tail, Voices 0).

Other things looked at and ruled out as the floor: `Profiler::stopMeasuringAudio` calls `reportBlockCycles` after the readout value is taken, so `CpuMeter::addBlock` and `Governor::evaluate` (a 64 bit divide and a walk of at most 12 voice records, a few hundred cycles) are real ISR time but not in min, max or avg. Data placement against stock showed no hot object moving between SRAM and SDRAM.

## Audio CPU optimisations

These remove work the stock firmware does for silence. The basis is a DWT profile of stock CE on a test song on an original Tracker: avg 66 %, max 86 %, stopped 56 % of a 522,240 cycle block. Always-on cost measured on the device: `Reverb(polyverb)` 94.8 k cycles (18.1 %, constant, even stopped), `Delay(short)` 28 k, both limiters 22 k, mixers 52 k and idle voice chains about 7 points. All of it is behind `FEATURE_SYNTH_OPEN` (or `FW_REVERB_SRAM`), plain `make original` is the stock image. Five changes follow in the order they build on each other.

Evidence tool: `synth-open/tools/audio_opt/run_graph.sh`. It compiles the real library objects of the graph (AudioStream, BitDepth, Filter, Amplifier, MonoToStereo, RMS, all mixer sizes, Limiter, Mux, ShortDelay, Polyverb, OutputStream and RecordQueue as the export taps) from any source tree (working tree or `git archive` snapshot) with the firmware compiler and flags, wires them like `core/AudioEngine` does and runs a scripted scenario in a Unicorn Cortex-M4F. Only the voice source is a stand-in (its idle branch is the one of `AudioPlayMemory::envelopeUpdate`). Everything that leaves the graph is compared byte for byte between two builds: the output stream buffers, a hash of every block drained from the export queues, and the RMS and limiter meters. Scenario 0 is a 3000 block "song" (random notes on 8 voices, random gain, pan, filter, overdrive, bit depth, send, mixer, delay, reverb, sidechain and export tap changes, long idle stretches), scenario 1 repeats bursts and long silences with short tails, and scenarios 2 to 7 cover idle, playing with and without sends, filters settling, loud bursts with sidechain switching and sounding synth sends. `mutate.sh` builds a deliberately broken variant of the tree so the bench can be shown to catch it. Usage is in the header of `run_graph.sh` (`build`, `run`, `cmp`, `cycles` and `git`). The cycle numbers are emulated Thumb-2 instruction cycles (cost table of `tools/phz_cycles`) with **no flash or SDRAM wait states**, so they are estimates and not device measurements.

### Reverb in SRAM

`AudioEffectPolyverb` has two 16384 x int16 line buffers (32 KB each): E (`internalRamReverbBuff`, `__NOINIT(EXTERNAL_RAM)` on the MK66 although it is named internal and is `.noinit` SRAM on the Tracker+) and E2 (`externalRamReverbBuff`, `.reverb_memory`). Both sit in the 90 MHz SDRAM with no data cache. Per output sample the reverb makes 12 reads and 11 writes (E: 8 + 8, E2: 4 + 3), 2944 accesses per block. `REVERB_SRAM=<n>` (`core/makefile`, `-DFW_REVERB_SRAM`, default 1 for SYNTH_OPEN builds):

| REVERB_SRAM | placement | accesses off the SDRAM per block | RAM left for heap + stack (16 engines) |
| --- | --- | --- | --- |
| 0 | E and E2 in SDRAM (stock) | 0 | 122,808 B |
| 1 (default) | E in SRAM (`.noinit`), E2 in SDRAM | 2048 of 2944 (70 %) | 90,040 B |
| 2 | E and E2 in SRAM | 2944 (100 %) | 57,272 B |

Why 1 and not 2: the MK66 has 256 KB of SRAM and no TCM, `_sbrk` stops 8 KB below the stack and a device profile showed `arena=92636` (the whole malloc space, `Profiler::init` claims all of it) with 15.7 KB in use on the stock image. Moving both buffers leaves about 57 KB for heap and stack, 8 KB of that is the `_sbrk` margin and the project explorer lists (256 entries x up to 255 chars), the sample import, the exporter and the synth patch editor allocate on demand. One buffer leaves 90 KB. Smaller buffers are not an option: the live window of E is 15,591 of 16,384 entries and of E2 11,037, a modulus other than the power of two costs a `udiv` per access and saves little, and the output stays bit exact only with an unchanged index sequence. The move is bit identical by construction (same arithmetic, same size, only the address changes) and was checked with scenarios 0 and 1 (3000 blocks each) for SDRAM placement against `REVERB_SRAM=1` and `=2`. A define is not a make dependency, so build a `REVERB_SRAM` variant with its own `SYNTH_TAG` or wipe the object dir.

Estimated saving: the bench gives the reverb 45,149 emulated instruction cycles per block without wait states and the device measured 94,779. The difference, 49.6 k cycles over 2944 SDRAM accesses, is 16.9 cycles per access (an upper bound because it blames the whole difference on SDRAM, a lower bound assumes 12). E in SRAM removes 2048 accesses, which is **about 24 k to 35 k cycles per block (4.6 to 6.6 points) on every block, playing or stopped**.

### Idle voices send nothing

`AUDIO_NULL_IS_SILENCE` (`AudioStream.h`, defined with `FEATURE_SYNTH_OPEN`). `AudioPlayMemory::envelopeUpdate` with an idle envelope used to allocate, memset and transmit a block of zeros, so bit depth, filter, amplifier, mono to stereo, the meter, six mixers and the 22 export taps all processed 8 voices of silence. Now it transmits nothing. The rule for every consumer is "exactly what a block of zeros would have done, state included":

| object | NULL in | why it is not just `return` |
|---|---|---|
| AudioBitDepth, AudioMux, AudioOutputStream, AudioDelay (zeroblock) | as before | no state, or already NULL safe |
| AudioAnalyzeRMS | as before (`count++`) | a zero block adds 0 to the sum and 1 to the count, the same |
| AudioAmplifier, MonoToStereo, AudioMixer | no output, and `previous = target` per gain | `applyGain*` ends the gain ramp on any block, so a gain changed while the voice was idle must not ramp on the next note |
| AudioFilterStateVariable | bypass: `inputprev = outputprev = 0`. Enabled: skip when all states are 0 (and move `lastupdate_fmult/damp` like `update_fixed` does) or when one sample of silence leaves the state unchanged with a zero output (`isSettledOnSilence`, the fixed point arithmetic has a dead band). Otherwise run a real block of zeros | the filter keeps ringing after a note and the zero block is what decays it |
| AudioEffectLimiter | skip only when the peak detector is 0, the gain is exactly 1.0f, the look-ahead delay is empty and there is no sidechain block. It then sets the meter (`averageGain` 1.0). Otherwise run a block of zeros | its release and 5 sample delay still have to play out |
| AudioRecordQueue (export taps) | enabled: queue a block of zeros | the exporter counts blocks and a missing one ends a render (`handleStem`, `mtAudioExport.cpp`) |
| AudioEffectPolyverb | allocates a block of zeros for each missing input and runs as before | stock returned on a missing left input without reading the right one |

Bit identity against the stock graph with zero-block idle voices: scenarios 0 to 5 (3000 blocks, all the changes in the script above, export taps on and off) give identical output stream, export queue hashes and meters. The bench is sensitive to every rule above: mutants of the amplifier, mono to stereo, both mixer paths, the record queue, the polyverb and four filter rules each make a scenario fail.

Flash: the five object changes cost 808 B. Paid back with `-Os` for the graph construction and event glue objects (`TRIM_OS_FILES`, see "Flash trim"): -3,480 B.

Emulated cost per block (**estimated**): all 8 voices idle 161,420 to 112,163 cycles (-9.4 points), 3 voices playing with fx sends -5.0 points, 3 playing with zero sends -3.6 points, idle after a burst with 8 filters on -14.2 points (the filters are skipped as soon as they settle), silence after the fx tails with the export taps on -17.9 points, 4 voices bursting with fx -3.8 points. From the device profile (IDLE section, stopped, wait states included): the idle chain objects measured 42.6 k and the four mixer kinds 54.8 k, of which about 38 k (Tracks, PreFx) become 1 k, so **about 70 to 80 k, 13 to 15 points when stopped**. The saving is per idle voice (about 5 k each) plus the share of the mixers, so it is smaller when more tracks play.

### Reverb and short delay sleep when silent

All of it is behind `AUDIO_NULL_IS_SILENCE`. A skipped block transmits nothing (NULL), so the tracks and sends mixer, both limiters and the final mixer see NULL as well and fall asleep with them.

**Short delay** (`effect_shortDelay.cpp`, bit exact by construction): skip when the input is NULL or all zero, the delay time is at least 1 sample, the values written to the three lines (right, left, mono) were all zero for at least `timeInSamples` samples in a row (the whole delay window is zero, counted per block, `quietSamplesCount`) and the six filter states are zero. What `process()` would then do is write 128 zeros per line, move the head and count 128 silent samples, and the skip does exactly that (`silentSamplesCount += 128`, so `isSilent()` for the exporter stays right). The loop with the quiet bookkeeping runs only for silent input (`processBlock<true>`).

**Reverb** (`reverb.h`, `effect_polyverb.cpp`): skip when both inputs are NULL or all zero and the reverb is quiet. Quiet means that for 16,384 samples in a row (one buffer length, the input path with predelay is at most 6,210 samples and the longest line 15,591) the input was silent, both long feedback lines (`del1`, `del2`) were written with values that compress to 0 (below 1 LSB of 16 bit) and the three low pass states were below 1 LSB. That is 128 blocks, 0.37 s after the tail has gone. **Threshold: 1 LSB of 16 bit (-90.3 dBFS).** The skipped block keeps the state: the write pointers and the LFO are advanced as `Process()` would advance them (`FxEngine::Skip`), the line buffers are not touched and the low pass states are set to 0. A reverb `Reset()` is quiet at once and a block with sound clears the count.

Where the reverb is not bit identical to the version without the skip (this is the whole list): at the moment the skip starts it drops what it would still have output below 1 LSB (the low pass states and what is left in the dap lines, at most 1 to 2 LSB for a few hundred samples at -90 dBFS), and the activity meter `last_update_delta_` is 0 while skipped (nothing reads it). The buffers stay as they were, so the first note after a long pause meets exactly the state a stock reverb has (zero lines, the same write position and LFO phase, no reset, no fill, no click). In the bench 7 scenarios x 3000 blocks are byte identical, so the residue never flipped a sample. A mutant with 1,024 instead of 16,384 samples differs by up to 4 LSB.

**Limiter** (`effect_limiter.cpp`, bit exact): a limiter that gets NULL still has to run its release, its 5 sample look-ahead delay and the sidechain. It allocates a block of zeros and processes it until one such block leaves its whole state as it found it (gain, peak detector, both delay lines: `relaxed`). From then on silent blocks are skipped, with the same meter value, until a block with sound, a new attack or release time or a sidechain switch. `relaxed` is derived from what the real code did, not from a formula, because the float gain does not always come back to exactly 1.0.

Mutants the bench catches: reverb quiet threshold too small, LFO and pointers not advanced (differences up to 3234 LSB, a different chorus phase), skip without the silent input check, delay quiet length too short, delay lines not zeroed, delay `silentSamplesCount` not advanced, limiter relaxed without the unchanged gain, limiter without the meter and a sidechain switch not clearing `relaxed`.

**What to listen for**: very long reverb tails with the reverb size at the top (the tail must ring out to the end, not cut off or pump), a very quiet reverb tail (a note with a tiny reverb send, then silence), the first note after a long pause (same reverb as before) and a delay with feedback and a long time (the last echoes must still ring out, ping pong too and changing the delay time to a longer time while it is quiet must not bring back old echoes). Switching the reverb off in the project (send 0) with notes still playing is the case where the reverb now stops costing CPU.

Flash: +1,208 B. Emulated cost per block (**estimated**): everything idle 112,163 to 13,411 cycles (-18.9 points), silence after the fx tails with the export taps on -18.4 points, idle after a burst with the filters on -18.9 points, **3 voices playing with zero fx sends once the first block's transient has died away -12.7 points**. With fx in use nothing is skipped and the bookkeeping costs +1.0 k cycles (+0.2 points). What is left in silence (14 k) is the object walk (`AudioStream::update_all` 5.7 k), the output stream clears and the export taps. From the device profile, stopped (wait states included): reverb about 60 k after the SRAM move, delay 28 k, limiters 22 k, tracks and sends mixer 10.7 k and the final mixer 6.3 k go to about 1 k each, which is **about 120 to 125 k, 23 to 24 points when everything is silent**. The reverb 60 k plus delay 28 k (17 points) also go away whenever the sends are zero or between tails.

### Mixers skip what adds nothing

`AudioMixerBase::mix` (`mixer.cpp`, one function for all mixer sizes, the template only holds the gain tables, -24 B of flash against the old copies of the loop). Same result as the old loop for every input, bit identical:

- A NULL input is silence (its gain ramp ends) and an input whose gain is 0 (target and previous) adds nothing: its block is released, never read.
- Nothing left: no output block (NULL, every consumer treats it as silence).
- One input left and hard clip (the default saturation type): unity gain forwards the input block itself (no copy, no mix, no clip, because `saturate16` of an `int16` is the identity), another gain runs `applyGain` in place.
- Several inputs (or another saturation type): the first input with a gain starts the sum (no 512 B clear for a muted first channel, no 256 B copy of a shared block), the rest is `applyGainThenAdd`, then `saturate` as before.

The final mixer has the unity stream gain (`mtAudioEngine.cpp`) and one live input, so it forwards the limiter output untouched. The tracks and sends mixer does the same whenever the delay and reverb are asleep. Evidence: scenarios 0 to 6 byte identical, with scenario 0 also switching the project saturation type of the tracks and tracks + sends mixers (saturate, overdrive, soft clip, back to hard clip) and using a 2.5 gain. Mutants caught: pass-through that ignores a ramp from another gain, a single input path for every saturation type, "gain 0" meaning target only, no copy of a shared block before `applyGain` and no new block for a shared first input.

Emulated cost per block (**estimated**): 3 voices playing with sends -1.4 points, 3 playing with zero sends -2.7 points, 4 voices bursting with fx -0.7 points. From the device profile: `Mix.Final` 6.3 k becomes about 0.7 k on every block, `Mix.TrackSends` 10.7 k about 1 k when delay and reverb sleep, and the tracks and pre-fx mixers lose the clear or copy of the first channel (about 0.6 k each), **about 6 k always (1.1 points), up to 16 k (3 points) with the fx asleep**.

### Synth delay and reverb sends on MK66

`MixerAndFxs<voices>::kFirstSynthSendInput` is the one source of the send input of a synth part: `2 + voices` on MK66 (tracks 0 to 7, external inputs 8 and 9, synth parts 10 to 12) and `2 + 2 * voices` on IMX (value 18, unchanged). Under `FEATURE_SYNTH_OPEN` on MK66 `kSendsInputsCount` is `kFirstSynthSendInput + kSynthParts` = 13 (stock 10, IMX 21, both unchanged). `connectSynthToMixer`, `setSynthDelaySend` and `setSynthReverbSend` use the constant and a `static_assert` keeps every send input inside the mixers. `AudioMixer<13>` is a new instance (`mixer.cpp`, `kMaxMixerInputs` 12 to 13, 4 B of stack).

This fixes a dead feature: the code used inputs 18 to 20 (the Tracker+ layout) of the delay and reverb pre-FX mixers, which have 10 inputs on MK66. `AudioConnection::connect` returned before it linked anything (`dest_index > dst->num_inputs`) and `AudioMixer::gain` ignored the channel, so nothing was written out of bounds and the synth sends were silently dead.

Proof that every `connect()` succeeds: the bench builds the six synth send connections with the firmware's constants and reads `AudioConnection::isConnected` after each one (6 of 6 accepted in all scenarios, and 0 of 6 with the old index as a negative control). The previous and the new graph are byte identical in scenarios 0 to 6 (the stand-in synth sends transmit nothing there), and scenario 7 has the three sends sounding. The cost is the 6 extra empty inputs of the two send mixers, 6 NULL `receiveReadOnly` calls and ramp updates, **+252 emulated cycles per block (0.05 points)**. With a synth part sounding and its send above zero the fx wake up, which is the feature and not the mixer: three synth sends at 0.4 and 0.3 with no track voice cost 22.9 points, the same as a track send of that level.

### Engine DSP at -O2 and small fixes

From `ref/cpu-optimisation-plan.md` (items 1 and 3 to 6 and 10), all output bit identical or below -119 dBFS, all SYNTH_OPEN or plugin only (the stock image is unchanged):

- Engine DSP at `-O2` (`SYNTH_OPT_ENGINES`), FM4 and B-Type at `-O3` (`SYNTH_OPT_FM4`, `SYNTH_OPT_BTYPE`), the synth plugins with the same flags (`engines/engines.json`). At `-Os` GCC 10.3 kept the per-sample helpers (`svfStep`, `limit`, `fastSine`, `fastPow2`) as calls with the filter state in memory. The table above has the gains. Output: 12 of 18 engines bit identical, the others differ by at most VAP -180.6, CHIP -162.6, CS-Type -119.2, A-Type -125.6, 8-Type -132.5 and Hand-Type -138.5 dBFS (where GCC places a fused multiply add, bench scenarios and plugin render compare). The PLUGS image grows by 14.9 KB, the twelve plugins by 15.5 KB.
- Lo-Fi and Tape keep their coefficients and filter state in locals for the block (a float store to the output may alias any float of the instance, so GCC reloaded them after every sample): Lo-Fi 26.6k to 23.0k emulated cycles per block, Tape 18.4k to 17.9k, bit identical. Dirt, Console, Tube, BBD and Phaser already did.
- `saturateSoftClip` (`SynthVoiceSDK.h`, twice per sounding part per block) multiplies by `1 / 6.72` instead of dividing, 3,328 cycles per part and block. 1.3 % of the results move 1 ulp (at most -144.5 dBFS). Stock keeps the divide.
- The plugin runtime's `memset` and `memcpy` store words when aligned (see the CPU table).

`-ffp-contract=off` on the engines (plan item 2) takes another 0.7 to 10.4 points (PLUGF test image: `SYNTH_TAG=-fpc SYNTH_OPT_ENGINES="-O2 -ffp-contract=off" SYNTH_OPT_FAT=... SYNTH_OPT_FM4="-O3 -ffp-contract=off" SYNTH_OPT_BTYPE=...`, plugins with `SYNTH_PLUG_OPT_ENGINES` and `SYNTH_PLUG_OPT_O3`). It changes the output and its cost model is not device calibrated. The 1.2.0 release ships it on every engine and plugin (see "Release 1.2.0 lines" below).

### Summary

The estimated saving per block, in points of 522,240 cycles (device profile based, estimates and not device readings): reverb buffer in SRAM 4.6 to 6.6 on every block, idle voices send nothing about 13 to 15 stopped and about 1 per idle voice while playing, reverb and delay sleep about 23 to 24 when everything is silent and 12 to 17 whenever the sends are zero, mixers about 1 always and up to 3 with the fx asleep. Stopped, that is 56 % down to roughly 10 to 17 %. Playing the test song (avg 66 %, max 86 %) the first, second and fourth items are what shows because the song has fx sends in use, roughly 8 to 11 points off the peak blocks.

Measured on an original Tracker with pre-release builds: with the first four items, Audio CPU on the Config screen (dev mode) on the same song read min 9, max 76 and avg 50 against stock CE at 43 / 80. With all five, a 4 voice VAP part played at 52 % peak. The reverb and delay tails and the synth sends were checked by ear and worked.

Not done: profiling instrumentation of the audio graph (a DWT cycle counter build) costs about 7 KB on the stock image and does not fit next to 16 engines. A profiling build with fewer engines (`SYNTH_ENGINES="ACD FAT VAP WTFM"` has about 60 KB free) would measure the same graph.

## Stock build

Plain `make original` is the upstream firmware and the shim never touches it. It differs from clean upstream by 64 B, the out of bounds fixes in the UI: three unconditional `label[8] = { -1 }` fixes (`configEditor.h`, `sampleEditor.h`, `samplePlayback.h`, constructors wrote `label[8]` and `bar[8]` one element past arrays of 8). Everything else is behind `FEATURE_SYNTH_OPEN`, `FW_TRIM` or SYNTH_OPEN make logic.

| Stock build (`BUILD_TAG=7fd6c2c`, `git archive`, `make original`) | image bytes | ends | sha256 |
|---|---|---|---|
| clean `origin/main` 7fd6c2c | 902,032 | 0xDC390 | ee2289b510e9ffdaa8aafc49709297b6cfed2fe28cda546cabccbfeed1f2bc3f |
| tracker-ff (merged) | 902,096 | 0xDC3D0 | e449201f74038da7dffff602abec9310160a4bcaf7e7540417ec3e64049db072 |
| clean `origin/main` plus our three `label[8]` fixes | 902,096 | 0xDC3D0 | e449201f...db072 (identical to tracker-ff) |

The image embeds the git short hash (`BUILD_ID`), so the sha256 changes with every commit. To regression-check the stock image, rebuild with `BUILD_TAG` set to the baseline commit. Run `make clean` first, because the MK66 platform objects are shared between stock and SYNTH_OPEN builds and a stock build right after a SYNTH_OPEN one gives a different image.

## Build

You need `make`, `python3` and the GNU Arm Embedded Toolchain 10.3-2021.10 (in `./agt/10.3/` or `TOOLCHAIN=/path/to/arm-none-eabi-`).

```
# the 16 engine release image, build/PolyendTracker_FF_1.1.0.ptf
make original SYNTH_OPEN=1 TRIM=1 \
  SYNTH_ENGINES="ACD FAT VAP WTFM PERC CHIP JU6 PHZ FM4 DTYPE CSTYPE ATYPE TTYPE 8TYPE BTYPE ARCTYPE"

# folder and zip with the .ptf, LICENSE, NOTICE.md and every third party license text, in build/release/
make package-release SYNTH_OPEN=1 TRIM=1
```

### Release 1.2.0 lines (Arm GNU 12.3.rel1)

The 1.2.0 release compiler is Arm GNU Toolchain 12.3.rel1 (`TOOLCHAIN=/path/to/agt/12.3.rel1/bin/arm-none-eabi-`). `SYNTH_OPEN_VER` in `version.mk` is 1.2.0, so the images are `PolyendTracker_<TAG>_1.2.0.ptf` (AUTOF 30 chars, AUTO 29, the bootloader reads at most 31). BUILD_TAG has no dirty marker, so commit first. `make` does not track compiler flags: delete `core/build-*` and `platforms/mk66/build-trim` between variants.

The release image is **AUTOF**: six built in engines (ACD FAT VAP WTFM PERC CHIP), the plugin host, the other twelve engines as card plugins, engine DSP at `-O2` (FM4 and B-Type `-O3`) with `-ffp-contract=off` on every engine and every plugin, WTFM included. The flag stops GCC from fusing a multiply and an add into a `vfma` (3 cycles on the M4F against 2 for a multiply followed by an add). It goes on the OpenTracker modules, FAT, CHIP, FM4 and B-Type (PHZ, PLK and Dirt already have it), the plugins take it from `SYNTH_PLUG_OPT_ENGINES` and `SYNTH_PLUG_OPT_O3`, and the render compare passes the same values to the firmware's `make -n`:

```
E=-EACD_FAT_VAP_WTFM_PERC_CHIP
# the release image, build/PolyendTracker_AUTOF_1.2.0.ptf
make original SYNTH_OPEN=1 TRIM=1 PLUG_HOST=1 PTF_TAG=AUTOF SYNTH_TAG=-fpc SYNTH_ENGINES="ACD FAT VAP WTFM PERC CHIP" SYNTH_ENGINES_TAG=$E \
  SYNTH_OPT_ENGINES="-O2 -ffp-contract=off" SYNTH_OPT_FAT="-O2 -ffp-contract=off" \
  SYNTH_OPT_FM4="-O3 -ffp-contract=off" SYNTH_OPT_BTYPE="-O3 -ffp-contract=off" BUILD_TAG=$(git rev-parse --short HEAD) TOOLCHAIN=...
# every plugin of the release: the four module repositories at the tags pinned in engines/external.lock.json, rebuilt from local
# checkouts (or python3 -I engines/external.py fetch OUT/modules for the published files), checked against the lock, checkSynth, logos
TOOLCHAIN=... PY=/tmp/phzvenv/bin/python engines/rebuild_pinned.sh OUT autof
```

Against the plain AUTO build the default patch differs by at most -103 dBFS on every engine except A-Type (-80 dBFS), and the all-parameters-max scenarios of Arc-Type, WTFM and Hand-Type differ far more (Arc-Type -7 dBFS, WTFM -44, Hand-Type -69), measured on the 12.3.rel1 bench dumps. The user listened to both and chose AUTOF for all engines. Its CPU saving is an emulated figure, not device calibrated.

The release package (zip) is made from the image and the pinned modules (`Plugins/Synth/` with the twelve `.plg` and `.plgi`, `Plugins/Master/` with the seven inserts, `Effects/` with the `.ptfx` files, `licenses/plugins/` with the notices of the module repositories): `synth-open/tools/package_release.sh build/PolyendTracker_AUTOF_1.2.0.ptf OUT --zip --modules OUT/modules --effects PTFX_DIR` (`--card CARD_DIR` takes a ready card layout instead).

The plain **AUTO** build stays buildable as an unpublished fallback: the same two lines without the `-ffp-contract=off` flags (engine DSP `-O2`, FM4 and B-Type `-O3`, the OpenTracker modules fused as the compiler defaults). It is not part of the release package.

```
make original SYNTH_OPEN=1 TRIM=1 PLUG_HOST=1 PTF_TAG=AUTO SYNTH_TAG=-pa SYNTH_ENGINES="ACD FAT VAP WTFM PERC CHIP" SYNTH_ENGINES_TAG=$E \
  BUILD_TAG=$(git rev-parse --short HEAD) TOOLCHAIN=...
TOOLCHAIN=... PY=/tmp/phzvenv/bin/python engines/rebuild_pinned.sh OUT fused
```

- `package-release` only packages. The default engine set is the 4 engine one, so build with the explicit 16 engine list first. `synth-open/tools/package_release.sh build/<image>.ptf` does the same packaging for an existing image.
- A `-D` define change (for example in `version.mk`) does not rebuild the objects that depend on it. After changing a version or tag, delete the objects that depend on `mtStructs.h` and grep the `.ptf` for the old strings.
- `make clean` at the top level removes everything in `build/`, so keep copies of the images you want.
- Host checks that need no device: `synth-open/tools/run_smoke.sh "<engines>"` (smoke test), `make synth-engines` (flash budget), `tools/phz_cycles/run_bench.sh` (engine cycles), `tools/idle_cycles/run_bench.sh` and `tools/audio_opt/run_graph.sh` (audio graph), `tools/dirt_cycles/run_bench.sh` and `tools/dirt_render.sh` (Dirt insert). The emulated benches need a Python with `unicorn` and `capstone`.

## Scope and licences

- Unofficial, noncommercial community work. Not affiliated with, endorsed by or supported by Polyend.
- Original Tracker (MK66) only. The device numbers above marked as measured come from an earlier pre-release build run on one original Tracker.
- This build links neither `libPolySynth.a` nor `libPolyDSP.a`. Nothing here is derived from them.
- Licences: the repository `LICENSE` (MIT with the Wholesale Commercial Clone Restriction, since upstream's 9cc7aae) covers the tracker-ce code and this shim. It is free to share with `LICENSE` and `NOTICE.md`, and not to sell. `ot/` is OpenTracker under Apache-2.0 (`licenses/LICENSE-OpenTracker-Apache-2.0.txt`), the opentracker-synths engines (MIT, `licenses/LICENSE-OpenTracker-synths-MIT.txt`) are plugins built in poly-engines-os and everything is listed in `NOTICE.md`. "Polyend" and "Polyend Tracker" are Polyend's names, used here only to say which device this runs on and because the original's bootloader wants the file name prefix. The release package from `make package-release` has every license text: the `.ptf`, `LICENSE`, `NOTICE.md` and `licenses/`. No factory patches and no `.ptfx` are shipped.
