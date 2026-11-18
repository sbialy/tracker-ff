# Upgrading to tracker-ff 1.4.0

This guide is for the original Polyend Tracker (MK66) when you come from tracker-ff 1.3.0 or 1.3.1. If you come from 1.2.0 or 1.1.0, read "From 1.2.0 or 1.1.0" at the end first and then the sections for 1.3.x. A fresh install needs only the "Flash" section of the README of the repository.

The steps are always the same: back up, unzip the zip onto the card, flash, check. 1.4.0 has new files that the image needs, so use the zip and do not flash the `.ptf` alone (see "Flashing the image without the zip").

## Before you start

1. Put the SD card in your computer and copy the whole card to a folder on the computer. At least `Projects/`, `Patches/`, `Plugins/`, `Effects/` and `Workspace/` should be in the copy. A project saved by 1.4.0 opens on 1.3.x (see "Going back to 1.3.x"), but a copy costs nothing.
2. Download `PolyendTracker_FF_1.4.0.zip` from the release page of the tag `tracker-ff-v1.4.0`. Check it with the `SHA256SUMS` asset. The zip is the whole release. The firmware image is inside it as `Firmware/PolyendTracker_FF_1.4.0.ptf`.
3. Unzip it. The folders in it (`Firmware/`, `Plugins/`, `Effects/`) are laid out like the card, so you can unzip straight onto the card root and let the computer replace the files that are there. The documents are under `tracker-ff/`. `tracker-ff/SHA256SUMS` lists every file with its path from the card root. From the card root, `shasum -a 256 -c tracker-ff/SHA256SUMS` checks the copy.

## From 1.3.0 or 1.3.1

Projects, `.patch` files and the plugin format of 1.3.x work in 1.4.0 as they are. You need no conversion.

### Files

| File | What to do |
|---|---|
| `Firmware/PolyendTracker_FF_1.4.0.ptf` | New. The unzip puts it in `/Firmware/`. Leave the `.ptf` of your old version there as your way back (the firmware update menu lists both). |
| `Plugins/Ext/modit.plg` | **New and required for MOD and IT.** MOD import, IT import and MOD/IT export are no longer in the image. They load from this file on first use. The file belongs to the exact image of this release, so always take it from the same zip. Without it or with the file of another release the import and export keys show a short popup and do nothing. |
| `Effects/se_amplifier.ptfx`, `se_overdrive.ptfx`, `se_delay.ptfx`, `se_bitcrusher.ptfx`, `se_chorus.ptfx`, `se_flanger.ptfx`, `se_equalizer.ptfx`, `se_limiter.ptfx`, `se_compressor.ptfx` | **New and required for the nine Sample Editor effects.** Amplifier, Overdrive, Delay, Bitcrusher, Chorus, Flanger, Equalizer, Limiter and Compressor moved out of the image. Without a file the row reads `Missing: <name>` and the effect cannot be applied. They render what the old built in ones rendered. |
| `Plugins/Synth/alco.plg` and `alco.plgi` | New. ALCO, a synth plugin with two oscillators, a sub, 20 algorithms and a wave mode that reads single cycle waves from `/Waves`. |
| `Plugins/Synth/chip.plg`, `cstype.plg`, `ju6.plg` | Replace the old files. CHIP 1.1.1 and CS-Type 1.0.1 sound the same as before and use less CPU, CHIP also holds 16 voices for the A/B page. JU6 1.1.0 has a Para chord mode and a Strum and needs firmware 1.3.0 or newer. The old files keep working in 1.4.0, but the A/B page needs the new CHIP for 8+8 voices. |
| The other plugins and the `.ptfx` files you had | Unchanged. Copying them over is harmless. |

### Flash

1. Unzip the zip onto the card root as in "Before you start".
2. Put the card in the Tracker. Open **Config > Firmware > Firmware update**, pick `PolyendTracker_FF_1.4.0.ptf` and confirm.
3. Wait until the Tracker restarts by itself. Do not switch it off while it flashes.
4. Open **Config**. **Info > About** says `tracker-ff 1.4.0`.

### Flashing the image without the zip

The update works with the `.ptf` alone and the Tracker boots and opens your projects. What you are missing is the card files of the table above:

- The nine Sample Editor effects read `Missing: <name>` until `Effects/se_*.ptfx` are on the card.
- The MOD and IT import and export keys show a popup until `Plugins/Ext/modit.plg` of this release is on the card.
- A part on ALCO shows Missing until `alco.plg` and `alco.plgi` are in `/Plugins/Synth/`.

Copy the files, restart the Tracker and everything comes back. A Missing part keeps its settings and its patch file.

### What to check afterwards

- Open the Sample Editor of a sample and pick Amplifier. It applies the effect. If the row reads `Missing: Amplifier`, the `se_*.ptfx` files are not in `/Effects/`.
- Import a `.mod` or `.it` file. The first import takes a moment while `modit.plg` loads. After that it is quick until the next start.
- Open a project that has a part on a plugin engine. The first time an engine is needed it is copied into the data flash, you see `Loading <name>` and the part plays. The first install can cost a short audio gap in a busy project.
- Open the **File** screen and press F7. The Project Settings page opens (see "What is new in the project").

### New files the Tracker makes

- `project.mtff` next to `project.mt` in a project folder. It holds the settings the `.mt` has no room for (Strum volume, Random seed, Send ducking, the groove, scale snap and follow chord of each track, the Parts list, Macro depth and the 4 FX switch). It is written when you set one of them. It travels with the project through Save, Save As and Open. Do not edit it by hand.
- `pattern_NN.mtx` next to the pattern files, only in a project that has the 4 FX Slots switch on.
- `Workspace.prev` on the card. When you choose Don't save on opening or starting another project, the discarded session is kept there (one generation). Rename it to `Workspace` to restore it.
- `/Features/` holds the settings of the Features page. 1.4.0 adds Play stop lock, MIDI remote, USB drive and the USB audio Mode and FX in 8+3 rows (they are on Config > USB, the files are the same). Reset config clears the folder.

### What is new in the project

- **Project Settings page.** On the File screen, F7 opens it. It holds `4 FX Slots`, `Macro depth`, `Strum volume`, `Random seed`, `Reroll seed` and `Scale snap`. It replaces the F7 Strum ramp and F8 `4 FX not CE` buttons of the File screen and the rows `Random seed`, `Reroll seed` and `Scale snap` that were on the Config screen.
- **Strum volume.** A project from an earlier version has no setting and plays with the ramp off until you turn it on. A new project starts at 100.
- **Macro depth.** The default is Per patch, which loads and plays every patch as before.
- **4 FX Slots.** Off for every project of an earlier version. A project with the switch off writes the files it wrote before. Turning it on gives every pattern step two more FX slots. **A project that uses FX3 and FX4 is silent in those two slots on 1.3.x and on stock CE.** See "Going back to 1.3.x".
- **Parts.** Shift + F8 on the Song screen switches between the Patterns and the Parts view. A project with no Parts plays its song as before. The pattern files are not touched.
- **Snap FX.** A step FX (`N`) that sets the scale snap of its track from that step on. The values are Off, Nearest, Down, Up and Track. Track goes back to the Project Settings value.
- **Step FX that are new.** Chance FX (`K`), Tape (`W` for a Stop, `Z` for a Start), Morph (`o`) and Snap (`N`). The old firmware shows them as unknown FX or plays the note without them.

### What sounds or behaves differently

- **Delay and Reverb Mute.** On Master > Global Mixer page 3 the Mute keys of the Delay and the Reverb put the effect to sleep after the tail has rung out (up to 3 seconds). A muted effect costs almost nothing. With Mute off the audio is bit for bit as before.
- **Number printing.** The values with a decimal point (dB, seconds, tempo, plugin parameters) look the same and come from a smaller routine, which made the image about 4 KB shorter.
- **Master inserts.** Glue and Phaser fit the insert memory together now. Every pair of two different inserts of the release does.
- **Song grid.** The stock Song grid showed a black screen from 8 Parts. The Parts view draws its own table.

### USB

- **USB audio** has an Audio mode row in Config > USB (Stereo as before or 8+3 mono with 11 inputs on the computer). The USB entry is listed while USB audio or USB drive is on in Config > Features. The mode takes effect at the next start. While the computer records in 8+3 the master Delay and Reverb sleep to save CPU. Your Mute state is not changed.
- **USB drive** (Config > Features > USB drive, off by default, takes effect at the next start) adds a mass storage interface. With it on, Config > USB > Storage shows the SD card on the computer. Back up the card before the first use and eject the drive on the computer before you press Exit.
- With the USB drive off the unit shows up on the computer exactly as before.

## If it does not work

- **A part shows Missing.** The plugin file is not in `/Plugins/Synth/`. Copy it, together with its `.plgi`. File names are up to 23 characters. The plugin library is read at boot only, so a plugin you copy to the card while the Tracker is on shows as Missing until the next restart. A Missing part keeps its settings, its patch file and its stored engine number, so nothing is lost. To use another engine on such a part, pick it in the Model list. The part changes to that engine after the list has rested for 1 s and its old patch is kept as `<P>-b.patch`.
- **The nine Sample Editor effects read Missing.** `Effects/se_*.ptfx` are not on the card.
- **MOD and IT import or export shows a popup.** `Plugins/Ext/modit.plg` is not on the card or it belongs to another release. Copy the one from the zip of this release.
- **The Tracker does not list the new file in Firmware update.** The bootloader lists only names that start with `PolyendTrac` and are at most 31 characters long. Do not rename the file.
- **The Tracker does not start.** Put a known good `.ptf` in `/Firmware` with no other `.ptf` there, insert the card, power on and press the recessed reset pinhole on the bottom with a paperclip. The bootloader flashes the file. The README of the repository has the emergency recovery steps too.
- **A plugin crashed the Tracker.** The next start loads no plugin by itself and the Inserts page names the one that crashed. Holding Shift in the first 4 seconds after power on starts without plugins.

## Going back to 1.3.x

1. Back up the card first, at least `Projects/` and `Workspace/`. Some 1.4.0 settings do not survive every save on 1.3.x (see below). The copy lets you go back to them.
2. Use the `PolyendTracker_FF_1.3.x.ptf` you kept in `/Firmware/`. If it is gone, the release page of the tag `tracker-ff-v1.3.1` (or `tracker-ff-v1.3.0`) has it.
3. For the pinhole recovery only one `.ptf` may be in `/Firmware`. For the menu it does not matter.
4. Open **Config > Firmware > Firmware update**, pick the 1.3.x file and confirm.
5. Take `Plugins/Synth/alco.plg` and `alco.plgi` off the card. ALCO is made for 1.4.0 and was not tested on 1.3.x. Take `Effects/se_*.ptfx` off the card too, because 1.3.x has those nine effects built in and may list the files as extra effects. The rest can stay. 1.3.x does not use `Plugins/Ext/modit.plg`, `Workspace.prev` or the Features rows that are new.

What to expect on 1.3.x:

- Projects saved by 1.4.0 open.
- **project.mtff is not read.** The Strum volume, Random seed, Send ducking, groove, scale snap, follow chord, Macro depth and the Parts list are not applied there. The file stays on the card. If you change and save the project on 1.3.x and open it on 1.4.0 again, all of these settings apply again, the per track ones and the Parts list included. The Parts come back by themselves. A Part keeps naming the same pattern numbers, so a pattern you edited on 1.3.x plays as you edited it. If a pattern a Part plays is gone, that track of the Part is left empty and the rest of the Part plays.
- **Save As on 1.3.x drops the 1.4.0 settings.** The copy it makes has no `project.mtff` and no `pattern_NN.mtx` files, so it opens on 1.4.0 without the Parts, the 4 FX switch and FX3/FX4, the Strum volume, seed, ducking, groove, scale snap, follow chord and Macro depth. Use Save on 1.3.x or make the copy on 1.4.0.
- **FX3 and FX4.** A project that uses them is silent in those two slots on 1.3.x. The content sits in the `pattern_NN.mtx` files. A Save As on 1.3.x does not take it along, so do not use Save As on 1.3.x for a project whose FX3 and FX4 steps you want to keep.
- **Parts.** 1.3.x plays the song playlist and keeps the Part lines. The Parts view is not there.
- **Glue and Phaser together.** 1.3.x has less memory for the two master inserts. With Glue in one insert and Phaser in the other the second one does not run on 1.3.x (the Inserts page says NoRoom) and the sound passes it dry. Every other pair of the shipped inserts fits.
- **New step FX.** Chance FX, Tape, Morph and Snap show as unknown FX on 1.3.x or play the note without the FX. The Chance values `1/8` to `8/8` and `P01` to `P16` do not exist there.
- **Patches.** A patch saved by 1.4.0 loads on 1.3.x.
- MOD and IT import, IT export and the nine Sample Editor effects are built into 1.3.x again.

Going back to 1.2.0 or 1.1.0 works the same way with the file of that tag. A patch saved by 1.3.0 or later that carries more than 765 bytes of data the old firmware does not know fails to load on 1.2.0 (normal patches carry 8 to 52 bytes). The 1.3.0 notes of this guide still hold: a CHIP part plays on the built in CHIP of 1.2.0 and the Glue insert depends on `glue.plg`. 1.1.0 does not have the master inserts, so the inserts of a project do not play there. JU6 1.1.0 does not install on 1.2.x firmware, the JU6 slot reads Missing there.

## From 1.2.0 or 1.1.0

Do the steps above and the following. 1.3.0 moved CHIP out of the image and added more, so the card needs more files than it did.

- **CHIP is a plugin.** `Plugins/Synth/chip.plg` and `chip.plgi` are required. Without them every part on CHIP shows `Missing: CHIP` and plays nothing. Copy the whole `Plugins/Synth/` folder of the zip.
- **Engines are plugins.** 1.1.0 had all its engines built in. 1.4.0 has five built in (ACD, FAT, VAP, WTFM and PERC). The other fourteen are plugins in `/Plugins/Synth/`, each a `.plg` with a `.plgi` logo: CHIP, JU6, PHZ, FM4, D-Type, CS-Type, A-Type, T-Type, 8-Type, Hand-Type, B-Type, Arc-Type, PLK and ALCO. Hand-Type and PLK are new since 1.1.0. Engine numbers, project files and `.patch` files are the same as in 1.1.0, so your projects and patches find their engines as soon as the plugin is on the card.
- **Dirt moved to the master inserts.** The five built in Dirt entries of the Saturation list are gone. Dirt is `dirt.plg` in `/Plugins/Master/` now. A project saved with one of the old entries opens with Dirt in insert 1 at the same values, as long as `dirt.plg` is on the card. Copy the whole `Plugins/Master/` folder of the zip. It holds Dirt, Tape, Lo-Fi, Console, Tube, BBD, Phaser and Glue. Two insert slots sit after the Global Mixer on the **Inserts** page. The Master button cycles Master, Track Mixer, Global Mixer and Inserts.
- **Effects.** Copy all `Effects/*.ptfx` over the old ones. Plate Reverb was never in a public release. If a test build left a Plate Reverb `.ptfx` in your `/Effects/`, delete it.
- **Patch FX.** `O - Patch` also applies the macros and the automation of the slot it switches to. In 1.2.0 it kept those of the slot that was open on the Synth screen. A song that relied on that can sound different.
- **Macro depth.** New patches scale a macro amount to the room between the parameter and its end (Scale). A patch from 1.2.0 has no mode stored and keeps the old rule (Add), so it sounds as before.
- **Model list.** Every step onto another engine waits 1 s before the engine is loaded, built in engines included. Scrolling through the list no longer picks each engine on the way.
- **Features page.** Config > Features is new. The synth order file `/Plugins/synth-order.txt` is written the first time you reorder or hide engines in the Model list (Shift + F1).
- **Paraphony and chord capture.** ACD and JU6 have a Chord Mode (Poly or Para). With the cursor on a MIDI Chord cell, hold pads or play MIDI keys together and the Tracker writes the chord into the step. A `~` before the name in the popup marks a near match.
- **Samples and workflow.** Samples load into RAM by default and the Sample Loader has a Mode button for RAM or streaming per sample. A project load ends in the pattern editor. Config > MIDI has eight per track MIDI-in items that start Off.

Your files from 1.1.0 or 1.2.0, the projects, `.patch` files and `Patches/` folders, open as they were. Back them up first anyway.

## Checksums

The release page lists the SHA-256 of the zip in `SHA256SUMS`. The zip also holds `tracker-ff/SHA256SUMS` for every file in it, the image included. Check the zip before you unzip it and run `shasum -a 256 -c tracker-ff/SHA256SUMS` from the card root afterwards, because a bad copy is the most common reason for a failed update.
