# Changelog

All notable changes to the **Polyend Tracker Community Edition (CE)** firmware across **Original Tracker** (`MK66`), **Tracker+** and **Tracker Mini** (`IMX`) are documented in this file.

---

## [Unreleased] - 1.3.1

### Fixed

1. A part on an engine that is not on the card (for example CHIP without `chip.plg`) can be switched to an installed engine again. 1.3.0 refused every change of its Model list with the `<name> not installed` popup, so the part stayed on the missing engine until the plugin was copied or another project was opened. Now the Model list of such a part works like the list of any part: no row is highlighted, the first step puts the list on a row and the engine of that row is picked after the list has rested for 1 s. The part keeps its stored engine and its patch files until then, slot 0 is kept as `<P>-b.patch` when the engine changes, and picking the old engine again brings the patch back.
2. The Macros and Automation pages of a part whose engine is not installed stay closed. The Params key shows the `<name> not installed` popup and stays on the Synth page, and picking such a part while a Macros or Automation page is open shows the Synth page. Before, the pages opened and edits made there were lost.

## [1.3.0] - 2026-11-13 - tracker-ff 1.3.0 (original Tracker, MK66)

Version 1.3.0 of the tracker-ff firmware for the original Tracker. The Tracker+ and the Mini are not part of it.

### Added

1. **USB audio.** Built in. With USB audio on, the unit shows up as an audio input on a computer after the next start. With it off, the unit presents only the stock USB interfaces.
2. **Glue master insert.** `glue.plg` in `/Plugins/Master/`, a bus compressor with Auto makeup and a Character setting (Clean, Glue, Smooth, Punch or Colour). Its Makeup reads `Auto` at the bottom step. A plugin can name the end values of a numeric parameter now (`PLUG_PARAM_NAME_MIN` and `PLUG_PARAM_NAME_MAX`), older plugins and hosts are not affected. There are eight inserts now. Glue and Phaser do not fit the insert memory together, the second of the two shows `Full`.
3. **Paraphony.** A synth plugin can answer a chord query of the firmware (an optional extension of the plugin format, older plugins and hosts are not affected). ACD is the one engine of this release with a Para mode. It has three new parameters at the end of its Voice section: Chord Mode (Poly or Para), Chord and Strum. The mode is stored per patch. In Para a MIDI Chord step plays its notes as one voice with extra oscillators through the one filter and the envelopes, which costs less audio CPU than a voice per note. Patches from 1.2.x load with Poly and sound as before. Engines without a Para mode play the chord notes as separate voices. The patch Chord plays for notes from the pads, a MIDI keyboard and plain steps, and a MIDI Chord step FX wins for its own step (the same goes for Strum).
4. **Chord capture and chord names.** Play MIDI keys together with the cursor on a MIDI or synth step (any column), or hold pads with the cursor on a MIDI Chord cell, and the Tracker writes the chord: the lowest note becomes the step note and the chord goes into the FX value. A popup at the cursor shows the chord name and the held notes. A chord that is not an exact match gets the nearest one and a `~` in front of its name (`~Cmaj7`). A group with no near match leaves the step as it was. One Undo takes the chord back. The title bar and the popup show chord names with root and bass such as `Fmaj7/C`, the Fill list shows the name without a root such as `maj7 inv2`. MIDI Chord values above 29 (the inversions) play only the bass note on stock firmware, so a project with captured inversions sounds thinner there.
5. **Strum FX.** `u - Strum` in the FX slot next to a `0 - MIDI Chord` starts the chord notes one after another: 1 to 63 up (the lowest note first), 65 to 127 down, 2 ms between two notes per unit, 0 and 64 off. The notes start on audio blocks of about 2.9 ms, so `+10` is about 20 ms. A Para ACD patch strums inside its voice with its own Strum parameter.
6. **Record join.** Notes you play together while recording are written as one step with a MIDI Chord. With several tracks armed the chord joins onto the track under the cursor.
7. **Synth order.** Shift + F1 on the Model column of the Synth screen edits the engine list: Shift + Up and Down move an engine, Delete hides it, Insert shows it again, and the arrow keys wrap from the last engine to the first. The order is saved in `/Plugins/synth-order.txt`. Projects and `.patch` files still store the engine itself, so nothing in them changes. An engine that the open project plays is shown at the top even when hidden, and an engine without a line in the file is shown at the end.
8. **Macro depth Scale.** A new patch scales a macro amount to the room between the parameter value and its end, so an Amount of 1.00 reaches the maximum and -1.00 the minimum (the Amount is shown from -2.00 to 2.00, 1.00 is 100 %). Patches from 1.2.x keep the old rule. Firmware 1.2.x plays every macro with the old rule, so a 1.3.0 patch that uses a macro amount sounds different there.
9. **Features page.** Config > Features lists the options of the firmware that have a switch of their own. Each one has a State row and a page with its settings, and the settings are kept per feature in `/Features/` on the card. Reset config clears that folder. A feature that needs a restart says so and offers Restart now. The page lists USB audio and Chord names. Chord names switches the MIDI Chord cells of the pattern grid between short names such as `m7` and the interval digits.
10. **Note-on admission.** A synth note whose estimated cost would push the audio block over its time budget (87 % of the block) starts at the start of the next block, at most one block late and never dropped. A voice that a new note takes over in a full pool fades out in the block of the note, and a released voice that is near silence is freed at once.

Items 3 to 6 and 8 (paraphony, chord capture and chord names, Strum, record join and macro depth) are explained in the user guide [`docs/chords_and_macros.md`](docs/chords_and_macros.md). Item 7 (Synth order) is explained in the user guide [`docs/synth_order.md`](docs/synth_order.md).

### Changed

1. **Sample input chain idles.** The USB input stream of the original sends no data, and a gate whose pre-FX mixer inputs are silenced builds no output block, so an unused input chain costs less CPU. The output is unchanged.
2. **CHIP is an installable engine.** CHIP is no longer built into the image. It is `chip.plg` in `/Plugins/Synth/` like the other plugin engines, and it frees about 15 KB of flash. The sound, the parameters and the numbers are unchanged, so old projects and `.patch` files open on it as before. A part on CHIP shows `Missing: CHIP` on a card without the plugin. When you update from 1.2.x, copy `chip.plg` to `/Plugins/Synth/` too.
3. **Patch FX.** `O - Patch` now applies the macros and the automation of the slot it switches to, together with its values. An LFO or envelope that sits on the same slot type in both patches keeps running, one that changes type starts new and an automation slot that is Off reads zero.
4. **Model list rest.** Every step onto another engine in the Model list now restarts the 1 s rest, built-in engines included. Before, only a step onto an engine that was not installed waited, so scrolling with the arrows, their key repeat or the encoder picked each built-in engine on the way.
5. **Transient Shaper.** The sample editor effect has new envelope times, and its Sustain gain is held back while a hit is rising.
6. **Add to Empty Slot.** With files selected in the Sample Loader, Shift with the add button adds the selection and does not jump to the first empty slot. After a jump the instrument list and the memory bar refresh.
7. **Short delay.** The delay runs only the lines of the mode that is on (mono or ping pong), which uses less audio CPU. The sound is unchanged while the mode stays. Switching the mode with echoes ringing now crossfades over about 90 ms and carries the echoes over, where it used to cut to unrelated echoes with a click.
8. **Master insert 2 runs from the shared SRAM slot.** Both master inserts run from one 8 KB internal SRAM slot, insert 1 from the bottom and insert 2 from the top, so insert 2 no longer writes to the data flash. A plugin that fits alone but not beside the other insert shows `Full:<name>` and loads when the other one leaves room. The synth plugin bank takes the two freed sectors and now has all 32 sectors of the data flash.
9. **Lower audio CPU beyond the delay.** The reverb loop runs at half rate with the same room size, damping and decay time, and its lines fit internal SRAM, so the 32 KB SDRAM buffer is gone. The limiter keeps its state in registers and divides only above the threshold. A sounding synth part skips the parameter refresh when nothing changed. The synth sends are skipped when both pre-FX send gains are 0. The sample voice loops keep their state in registers. The internal sequencer clock counts in integers. All of these give the same output bit for bit.

### Fixed

1. Installing a synth plugin into the data flash no longer makes the audio crackle. The audio output renders ahead into a deeper buffer while the flash erases, and the erase works in short slices.
2. The delay feedback and the reverb damping are set the same way at the start as a project load sets them. Before, they started from the raw project values and the first project load corrected them.
3. The master inserts of a project load now wait like the synth engines do: 1 s after the load finished, then one insert at a time, 250 ms apart. Before, both read the card in the first main loop pass after the load. A pick on the Inserts page still loads after 400 ms.
4. A part whose engine is not on the card (for example CHIP without `chip.plg`) keeps that engine. The encoder, the arrow keys and F1 on its Model list show the `not installed` popup and change nothing, and no row of the list is highlighted. Before, one encoder click turned the part into ACD, and the list highlighted ACD so that Delete in the edit mode hid ACD.

### Known issues

1. A patch saved by 1.3.0 with a macro amount in Scale mode plays with the old Add rule on firmware 1.2.x, so that macro sounds different there.
2. Macro depth mode is stored per patch and the unit has no row to switch it yet. New patches are Scale, patches without the mode stored are Add.
3. Glue and Phaser do not fit the insert memory together. The second of the two shows `Full` and passes the audio.
4. A part whose engine is missing still opens its Macros and Automation pages, and edits made there are not kept.
5. Firmware 1.2.0 keeps at most 768 bytes of patch data it does not know. A patch saved by 1.3.0 that carries more than 765 bytes of such data fails to load on 1.2.0. Normal patches carry 8 to 52 bytes.
6. The first install of a plugin engine can still cost a short audio gap in a busy project. The gap is much shorter than in 1.2.0.
7. Shift + Power screenshots can freeze the Tracker (known since 1.2.0). Hold the power button to switch it off and on again.

Updating from 1.1.0 or 1.2.0: see [`UPGRADING.md`](UPGRADING.md). `chip.plg` has to be on the card for projects that use CHIP.

---

## [1.2.0] - tracker-ff 1.2.0 (original Tracker, MK66)

Version 1.2.0 of the tracker-ff firmware for the original Tracker. The Tracker+ and the Mini are not part of it.

### Added

1. **Master insert plugins.** Two insert slots on the master bus, a new Inserts page after the Global Mixer and seven inserts as `.plg` files in `/Plugins/Master/`: Dirt, Tape, Lo-Fi, Console, Tube, BBD and Phaser. The settings are saved with the project in `plugins.txt`. The page lists up to 272 inserts, and a project finds its inserts by id and not by position.
2. **Mixer stage and Soft Knee.** The mixer stage in front of the inserts is Hard Clip, Soft Clip or Soft Knee. Soft Knee is also an entry of the Saturation list.
3. **Synth engines as plugins.** ACD, FAT, VAP, WTFM, PERC and CHIP are built in. JU6, PHZ, FM4, D-Type, CS-Type, A-Type, T-Type, 8-Type, Hand-Type, B-Type, Arc-Type and PLK are `.plg` files in `/Plugins/Synth/`, up to 256 files with names of up to 23 characters. A part shows `Missing: <NAME>` and keeps its settings when its engine is not on the card.
4. **Synth Automation.** Four LFO or envelope slots per patch, each on one engine parameter or macro, saved in the `.patch` file. The Params button cycles Synth Parameters, Synth Macros and Synth Automation.
5. **Patch FX.** A bank of 16 patch slots per synth part and the `O - Patch` step FX that switches between them.
6. **Sample editor effects.** Transient Shaper, Tape Saturation, Lo-Fi Chip, Dirt Drive, BBD Chorus, Dub Echo and Reverb join the existing effects, fourteen `.ptfx` files in all.
7. **Workflow.** Add to Empty Slot in the Sample Loader, per-track MIDI-in (Config > MIDI, tracks 1 to 8), RAM or streaming mode per sample, Synth FX performance control from MIDI CC, and a project load that ends in the pattern editor.

### Changed

1. **Lower audio CPU.** The synth engine code is built with `-O2` (FM4 and B-Type `-O3`) and without fused multiply and add steps, which are faster on the Tracker's processor. The sound of a note can differ from earlier builds in its exact waveform.
2. **Plugins load safely.** A marker on the card keeps a crashing plugin from loading at the next start, and holding Shift in the first 4 seconds after power on starts without plugins.
3. **Dirt is a plugin.** The five built-in Dirt entries of the Saturation list are replaced by `dirt.plg`. A project that used one opens with Dirt in insert 1 at the same values.

### Fixed

1. The main loop, the display and the 8-Type and T-Type engines use less stack in the audio interrupt, a deep plugin together with an insert and a card write can no longer run past the end of the stack.
2. A failed card write reports an error and keeps the old patch, plugin settings and project files.
3. An unreadable folder no longer breaks the file lists, a corrupt `.it` module cannot overflow the stack on import and an out of range audio connection index is rejected.
4. The playhead stays inside very short backward, ping-pong and granular loops, wavetable and granular position modulation clamps at 0, and song blocks smaller than 32 steps copy and paste correctly.
5. The CHIP engine no longer overloads the audio interrupt with many voices.
6. Changing the engine of a synth part, loading a project, saving a recording, listing a folder and opening the value list of a synth parameter no longer write through a null pointer when the heap is short. Large synth blocks come from the external memory and a failed allocation ends the action with an error.
7. The synth patch editor refuses a parameter or macro index outside the patch.
8. Power off always switches the Tracker off. Before, it could hang at the end with the screen on and every key dead. The wait for a running card operation is now bounded, a short heap no longer faults while the shutdown bar is created and a software deadline switches the Tracker off if the sequence stalls.

---

## [1.0.0] - Initial Community Edition Release

> **⚠️ Important :: Project Compatibility Notice:**
> To support dedicated per-step **Micro-move (`m`)** and **Volume (`V`)** columns alongside **`Fx1`** and **`Fx2`**, pattern files (`.mtp`) have been updated to a new format (**Version 6**).
> 
> - **Loading old projects works automatically**: Existing projects created on official/older firmwares load seamlessly and are upgraded in memory.
>   
> - **Saving updates the project to Version 6 (not backward-compatible with older firmware)**: Once you save a project on Community Edition firmware, its saved `.mtp` pattern files cannot be read by older firmwares. **Please make a backup of your `Projects/` folder on your SD card or use `Save As` to save under a new project name, if you ever plan to roll back to an older firmware.**

### Added

1. **Open Synth SDK & New `CHIP` + `DIRT` Synthesizer Engines (Tracker+ & Tracker Mini)**
   * Added a new Open Synth SDK allowing the community to add new synthesizer engines. `CHIP` is the first implementation of this open architecture.
   * Added **`DIRT`** (wave-shaping/distortion synth).
   * See [`docs/custom_synth_sdk.md`](docs/custom_synth_sdk.md) to learn more about developing your own engines.

2. **Modular Sample Editor Effect & Generator Plugins (All 3 Devices)**
   * Added hot-swappable DSP plugins loaded from `/Effects/*.ptfx` on the SD card. 
   * Supports up to **12 parameters** (across 3 pages) and up to **128 installed plugins**.
   * Added the complete `.ptfx` SDK, `ptfx_builder.py`, and 6 reference effects/generators (`Add Silence`, `Analog Kick Gen`, `Chiptune Voice Gen`, `Multi-Mode Filter`, `Stereo Delay`, `Stereo to Mono`) in [`editor-fx-sdk/`](editor-fx-sdk/).
   * See [`docs/modular_effects_system.md`](docs/modular_effects_system.md) to learn more about developing your own plugins.

3. **Wavetable Envelopes (`WT Env`), Wavetable LFOs (`WT LFO`) & Step FX (`w` / `y`) (All 3 Devices)**
   * Added `WT Env` and `WT LFO` modulation types across all 7 instrument parameters (`Volume`, `Panning`, `Cutoff`, `Wavetable Position`, `Granular Position`, `Granular Length`, `Finetune`) with **128 Factory (`F 001`–`F 128`)** and **128 User (`U 001`–`U 128`)** morphable contours and dual-axis `Warp X/Y` shaping.
   * Load your own wavetables to overwrite the 128 User shapes, by uploading them to `/Envelopes/*.wav`
   * Added `w` (WT Volume Env) and `y` (WT Filter Env) pattern step FX for per-step envelope duration/warp/ADSR shaping.
   * See [`docs/wavetable_envelopes.md`](docs/wavetable_envelopes.md).

4. **Custom Color Themes & Font Packages (All 3 Devices)**
   * You can now create your own themes and load them from `/Themes/*.json` on your SD Card (up to 29 custom themes alongside the 3 built-in themes) and custom antialiased font packages from `/Fonts/*.ptfont` (up to 30 custom fonts) selectable in `Config > General > Theme / Font`.
   * Create your own with the [Web Theme & Font Editor](https://polyend.github.io/tracker-theme-editor/) ([`polyend/tracker-theme-editor`](https://github.com/polyend/tracker-theme-editor)). For more information see: [`docs/custom_themes_and_fonts.md`](docs/custom_themes_and_fonts.md).

5. **Performance Mode Enhancement (All 3 Devices)**
   * Added page toggling between Sample Tracks and MIDI/Synth Tracks in Performance Mode. Just keep pressing `[Perform]` to toggle between the views.
   * Oh, for OG Tracker users, there's a small surprise that goes along well with this change.

6. **Hybrid SD Card Sample Streaming (All 3 Devices)**
   * Long samples  (`256 KB` and larger) now keep only their initial attack in sample memory while streaming the rest of the audio directly from the SD card in the background. This lets you load a massive amount of stems, loops, and recordings into a single project while using only a fraction of your sample RAM.
     
   * **What Streams on Each Device**:
     * **Tracker+ & Tracker Mini**: Streams **`One-Shot`**, **`Forward Loop`**, **`Backward Loop`**, **`Ping-Pong Loop`**, **`Slice`**, and **`Beat Slice`** instruments (`Wavetable` and `Granular` instruments remain fully in RAM).
       
     * **Original Tracker**: Streams **`One-Shot`**, **`Forward Loop`**, **`Backward Loop`**, and **`Ping-Pong Loop`** instruments, while keeping **`Slice`** and **`Beat Slice`** instruments (alongside `Wavetable` and `Granular`) fully in RAM to keep performance solid across multiple tracks.
       
   * **Works Transparently with the Sample Editor**: You can still edit streamed samples in the Sample Editor or switch play modes at any time. The Tracker automatically loads the full sample into RAM while you edit it and returns to streaming once your edits are applied. This excludes very long sample however, due to RAM restrictions. If you plan to stream multi-minutes samples, please prepare them outside of the device.
   * See [`docs/developer_guide.md`](docs/developer_guide.md#3-copy-on-write-workspaceinstruments-incremental-project-saver--hybrid-sd-streaming) for technical details.

7. **`Slice` & `Beat Slice` Instruments can now `Play Thru` (All 3 Devices)**
   * Added a per-instrument **`Play Thru`** (`Off` / `On`) setting on the **`More`** page of the **Sample Playback** screen for both **`Slice`** and **`Beat Slice`** modes.
   * When **`Play Thru`** is **`On`**, triggering any slice starts at that slice marker and continues playing through subsequent slices until the note is released (`Note Off` / `CUT` / `FAD`) or the end of the sample is reached.

8. **Dedicated Micro-move (`m`) & Volume (`V`) Step View (All 3 Devices)**
   * Each pattern step now stores dedicated **Micro-move (`m`)** (`0..100`) and **Volume / Velocity (`V`)** (`0..100`) values in addition to its two custom step FX slots (**`Fx1`** and **`Fx2`**), freeing both FX slots for creative use.
   * Pressing **`[Note]`** in Pattern View while the cursor is already on the **`Note`** column toggles the two effect columns between the standard **`[Fx1 | Fx2]`** view and the new **`[m | V]`** view.
   * Unquantized live recording (**Record Micro-move**) and velocity recording (**Record Velocity**) now write directly into the dedicated **`m`** and **`V`** step fields instead of occupying **`Fx1`** and **`Fx2`**.
   * Full support in **`[m | V]`** view for Jog-wheel / `±` editing, Pad entry, `Delete`, `Copy` / `Paste`, `Invert`, `Undo` / `Redo`, and the **`Fill`** tool (`Constant`, `From-To`, `Random`, `Euclidean`).

9. **Granular Length Automation & Step FX (`z`) (All 3 Devices)**
   * Added **`Granular Length`** as a modulatable parameter in **Instrument Automation**, allowing you to animate grain size over time using an Envelope, LFO and the new Wavetable Envelope or LFOs.
   * Added the **`z`** step FX (`0`–`100%`) to control grain length on a per-step basis (`0–50%` gives fine `1.0–100.0 ms` control for tight micro-grains, while `50–100%` sweeps `100.0–1000.0 ms` for lush clouds).
     
10. **More Chance Conditions (All 3 Devices)**
   * Spiced up the **`Chance` (`C`)** step FX beyond percentage probability (`C  0`–`C100`) with conditional and cycle-based modes inspired by the Polyend Play:
     * **Group with Last (`C GL`)**: Plays only if the previous conditional step evaluated on the same track played.
     * **Not Last (`C !L`)**: Plays only if the previous conditional step evaluated on the same track was skipped.
     * **Play N / Skip M (`C1:1`–`C6:6`)**: Plays the step for `N` consecutive pattern playthroughs, then skips it for `M` playthroughs (`N, M` in `1..6`).
     * **Skip N / Play M (`CS11`–`CS66`)**: Skips the step for `N` consecutive pattern playthroughs, then plays it for `M` playthroughs (`N, M` in `1..6`).
     
11. **Reactivated Multi-Folder SD Card Image Viewer (All 3 Devices)**
   * Reactivated and expanded the JPEG image viewer under `Config > Info` with three user folders (`Reference`, `Notes`, and `Custom` in `/Images/<Folder>/1.jpg` … `48.jpg`), supporting Left/Right, Jog-wheel, and direct Pad `1–48` selection.
   * See [`docs/image_viewer.md`](docs/image_viewer.md).

### Changed & Fixed

 * **Live Recording Improvements (All 3 Devices)**:
	* You can now play much tigther notes on single-armed tracks. As in: If you play a second note before releasing the first, it won't get lost.
	* Audible first note after preroll, and configurable first-note recording timing threshold (if you tend to play early).
	  
* **Exact `44100 Hz` Offline Audio Export (All 3 Devices)**:
	* Fixed sample rate drift during offline master and stem export so rendered `.wav` files match real-time `44100 Hz` playback pitch and timing.
	* Also removed excessive silence at end of renders.
	  
* **Removed clicks on rendered samples and editor effects (All 3 devices)**
	* No more clicks at the end of your samples.
	  
* **MIDI Overhaul (All 3 devices)**
	* Fixed USB MIDI enumeration for Windows 11 (March 2026 update).
	* Fixed long-term tempo drift and overhauled how MIDI Clock is sent out. Timing is now ~15× tighter and stays locked over hours of playback.
	* Fixed an issue where the Tracker could start slightly late (by one clock tick) when pressing Play on an external master clock or DAW. Step 1 now fires dead-on the downbeat.
	* The Tracker now responds to MIDI `Continue` and `Song Position Pointer` messages.
	* Improved how the Tracker follows incoming MIDI Clock when your computer or master device sends slightly uneven clock bursts, and fixed the `Clock Sync Delay` setting so fine-tuning your setup's latency works predictably.
	* Fixed low-level USB and TRS MIDI buffer issues that could occasionally drop clocks, notes, or CC automation during busy patterns.
	
* **Faster Project Loading & Saving (All 3 Devices)**:
	* Opening a project no longer duplicates all of its audio samples into a temporary folder on the SD card before loading them into memory. Instead, samples are loaded directly from your project folder, making even large, sample-heavy projects open in a fraction of the time while drastically reducing SD card wear.
	* Saving changes to your current project now only writes out samples whose audio was actually edited or newly added, while pattern edits and instrument parameter tweaks save almost instantly. (`Save As` still saves a complete, standalone copy of your project and all its samples).
	  
* **Major Internal Memory, SDRAM & Audio Engine Optimization (All 3 Devices)**:
	* Reclaimed internal SRAM and SDRAM across all three devices to power Hybrid SD Card Sample Streaming while keeping 100% of the original sample RAM pool intact (`11.25 MB` on Original Tracker / `60 MB` on Tracker+ & Tracker Mini), leaving generous internal RAM headroom (`~128 KB` free on Original Tracker, `~22.5 KB` `SRAM_ITC` + `~21.5 KB` `SRAM_DTC` + `~9.3 KB` `SRAM_OC` on Tracker+ & Tracker Mini) and up to `~29 MB` (`IMX`) / `~5.3 MB` (`MK66`) of reclaimable SDRAM for future community ideas.
	* Optimized real-time audio rendering for cleaner playback on heavy multi-track projects.
	  
* **Open-Source Build Consolidation (All 3 Devices)**:
	* Unified `core/`, `platforms/mk66/`, `platforms/imx/`, `synth/`, `util/`, and `editor-fx-sdk/` into a single monorepo built via a top-level [`Makefile`](Makefile) and [`version.mk`](version.mk).
	* Removed the proprietary NES emulator and repurposed its `EXTERNAL_RAM` region (`externalRamScratchBuff`, `768 KB`) as the shared scratch pool for `.ptfx` plugins, offline audio export, SD `.wav` preview, and modal file browsers. (Sorry gamers).
