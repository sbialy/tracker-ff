# Upgrading to tracker-ff 1.3.0

This guide is for the original Polyend Tracker (MK66) when you come from tracker-ff 1.1.0 or 1.2.0. A fresh install needs only the "Flash" section of the README of the repository.

The steps are the same for both versions: back up, unzip the zip onto the card, flash, check. What differs is how many files are new to you. Read the section for your version and then the sections after it.

## Before you start

1. Put the SD card in your computer and copy the whole card to a folder on the computer. At least `Projects/`, `Patches/`, `Plugins/` and `Workspace/` should be in the copy. A project saved by 1.3.0 loads on 1.2.0 (see "Going back"), but a copy costs nothing.
2. Download `PolyendTracker_FF_1.3.0.zip` from the release page of the tag `tracker-ff-v1.3.0`. Check it with the `SHA256SUMS` asset. The zip is the whole release, the firmware image is inside it as `Firmware/PolyendTracker_FF_1.3.0.ptf`.
3. Unzip it. Unlike the 1.2.0 zip, it has no wrapper folder. The folders in it (`Firmware/`, `Plugins/`, `Effects/`) are laid out like the card, so you can unzip straight onto the card root and let the computer replace the files that are there. The 1.2.0 zip put `LICENSE`, `NOTICE.md`, `SHA256SUMS` and `licenses/` at the card root. This zip has them under `tracker-ff/`, so after the copy you can delete the old ones at the card root. The old `SHA256SUMS` there no longer matches the card. `tracker-ff/SHA256SUMS` lists every file with its path from the card root. From the card root, `shasum -a 256 -c tracker-ff/SHA256SUMS` checks the copy.

## From 1.2.0

1.2.0 and 1.3.0 use the same project format and the same plugin format. You need no conversion.

### Files

| File | What to do |
|---|---|
| `Firmware/PolyendTracker_FF_1.3.0.ptf` | New. The unzip puts it in `/Firmware/`. You can leave `PolyendTracker_FF_1.2.0.ptf` there as your way back (the firmware update menu lists both). |
| `Plugins/Synth/chip.plg` and `chip.plgi` | **New and required for CHIP.** CHIP is no longer built into the image, it is a plugin like the other twelve. Copy both files. Without them every part on CHIP shows `Missing: CHIP` and plays nothing. |
| `Plugins/Master/glue.plg` | New. The Glue bus compressor. It is the eighth master insert. |
| `Effects/*.ptfx` | Copy all fourteen over the old ones. They are rebuilt, and `transient_shaper.ptfx` has new envelope times. |
| The other 12 synth plugins and 7 master plugins | Unchanged from 1.2.0. Copying them over is harmless. That includes `pluck.plg`, which is PLK 1.1 as in 1.2.0. |

Plate Reverb was never in a public release. If a test build left a Plate Reverb `.ptfx` in your `/Effects/`, delete it. 1.3.0 does not have that effect.

### Flash

1. Unzip the zip onto the card root as in "Before you start".
2. Put the card in the Tracker. Open **Config > Firmware > Firmware update**, pick `PolyendTracker_FF_1.3.0.ptf` and confirm.
3. Wait until the Tracker restarts by itself. Do not switch it off while it flashes.
4. Open **Config**. There is a new **Features** entry, and **Info > About** says `tracker-ff 1.3.0`.

### What to check afterwards

- Open a project that has a CHIP part. The first time a plugin engine is needed it is copied into the data flash, you see `Loading CHIP` and the part plays. The first install can cost a short audio gap in a busy project.
- Open the **Inserts** page (Master button, then cycle). Glue is in the list.
- If a part shows `Missing: CHIP` after all, `chip.plg` or `chip.plgi` is not in `/Plugins/Synth/` on the card. Copy it and the part comes back with its settings. A Missing part keeps its settings, its patch file and its stored engine number, so nothing is lost. To use another engine on such a part, pick it in the Model list: the part changes to that engine after the list has rested for 1 s and its old patch is kept as `<P>-b.patch`, so copy the plugin first if you want to keep playing the old engine. Its Macros and Automation pages are closed while the engine is missing (1.3.1 and later, 1.3.0 let you open them but did not keep what you edited there).

### New files the Tracker makes

- `/Features/` holds the settings of the Features page. The page lists USB audio and Chord names. Reset config clears the folder.
- `/Plugins/synth-order.txt` is written the first time you reorder or hide engines in the Model list (Shift + F1). 1.2.0 ignores the file.

### What sounds or behaves differently

- **Patch FX.** `O - Patch` now also applies the macros and the automation of the slot it switches to. In 1.2.0 it kept those of the slot that was open on the Synth screen. A song that relied on that can sound different.
- **Macro depth.** New patches scale a macro amount to the room between the parameter and its end (Scale). A patch from 1.2.0 has no mode stored and keeps the old rule (Add), so it sounds as before. The mode is stored per patch and the unit has no row to switch it yet. A patch saved by 1.3.0 in Scale mode plays with the Add rule on 1.2.x, so a macro amount in it sounds different there.
- **Model list.** Every step onto another engine waits 1 s before the engine is loaded, built-in engines included. Scrolling through the list no longer picks each engine on the way.
- **Short delay.** Same sound while the mode stays. Switching between mono and ping pong crossfades now.
- **Transient Shaper.** New envelope times.
- **Master insert 2** runs from internal memory. If two inserts do not fit together the second shows `Full:<name>` and passes the audio. Glue and Phaser are such a pair.

### Paraphony and chord capture

These are in the image, so there is nothing to copy.

- **ACD** has two new parameters at the end of its Voice section: Chord Mode (Poly or Para) and Chord. A patch from 1.2.0 loads with Poly and sounds as before. Para plays a MIDI Chord step as one voice, so the sound of a chord differs from Poly: one filter and one set of envelopes for all its notes.
- **Other engines** keep playing a chord as separate voices. A plugin gets a Chord Mode only when it answers the new chord query. No other engine of this release does.
- **Chord capture.** With the cursor on a MIDI Chord cell, hold pads or play MIDI keys together and the Tracker writes the chord into the step. A `~` before the name in the popup marks a near match.
- **Inversions.** MIDI Chord values above 29 (the inversions) play in full on tracker-ff since 1.2.0, but only as the bass note on stock firmware. Chord capture writes them, so a project that uses them sounds thinner when you open it on stock.

## From 1.1.0

Do everything for 1.2.0 above, and the following. 1.2.0 moved most engines out of the image and added more, so the card needs more files than it did.

### Engines are plugins now

1.1.0 had all its engines built in. 1.3.0 has five built in (ACD, FAT, VAP, WTFM and PERC). The other thirteen are plugins in `/Plugins/Synth/`, each a `.plg` with a `.plgi` logo: CHIP, JU6, PHZ, FM4, D-Type, CS-Type, A-Type, T-Type, 8-Type, Hand-Type, B-Type, Arc-Type and PLK. Copy the whole `Plugins/Synth/` folder of the zip. Hand-Type and PLK are new, 1.1.0 did not have them.

Engine numbers, project files and `.patch` files are the same as in 1.1.0, so your projects and patches find their engines again as soon as the plugin is on the card. A part whose engine is not on the card shows `Missing: <NAME>` and keeps its settings.

### Dirt moved to the master inserts

The five built-in Dirt entries of the Saturation list are gone. Dirt is `dirt.plg` in `/Plugins/Master/` now, with Type, Drive, Bits, Rate, Tone and Mix. A project saved with one of the old entries opens with Dirt in insert 1 at the same values, as long as `dirt.plg` is on the card. Copy the whole `Plugins/Master/` folder of the zip. It holds Dirt, Tape, Lo-Fi, Console, Tube, BBD, Phaser and Glue.

Two insert slots sit after the Global Mixer on a new **Inserts** page. The Master button cycles Master, Track Mixer, Global Mixer and Inserts.

### More that is new since 1.1.0

- Synth Automation: the Params button cycles Synth Parameters, Synth Macros and Synth Automation.
- Config > MIDI has eight per-track MIDI-in items, Track 1 to Track 8. They start Off.
- Samples now load into RAM by default. The Sample Loader has a Mode button for RAM or streaming per sample.
- A project load ends in the pattern editor.
- Fourteen sample editor effects in `/Effects/`.
- The Features page, the Glue insert and the reordering of the Model list are new in 1.3.0.

### Your files from 1.1.0

Projects, `.patch` files and `Patches/` folders open as they were. Back them up first anyway, as in "Before you start".

## If it does not work

- **A part shows Missing.** The plugin file is not in `/Plugins/Synth/`. Copy it, together with its `.plgi`. File names are up to 23 characters. The plugin library is read at boot only, so a plugin you copy to the card while the Tracker is on shows as Missing until the next restart.
- **The Tracker does not list the new file in Firmware update.** The bootloader lists only names that start with `PolyendTrac` and are at most 31 characters long. Do not rename the file.
- **The Tracker does not start.** Put a known good `.ptf` in `/Firmware` with no other `.ptf` there, insert the card, power on and press the recessed reset pinhole on the bottom with a paperclip. The bootloader flashes the file. The README of the repository has the emergency recovery steps too.
- **A plugin crashed the Tracker.** The next start loads no plugin by itself and the Inserts page names the one that crashed. Holding Shift in the first 4 seconds after power on starts without plugins.

## Going back to 1.2.0

1. Use the `PolyendTracker_FF_1.2.0.ptf` you kept in `/Firmware/`. If it is gone, the release page of the tag `tracker-ff-v1.2.0` has it as a file of its own.
2. For the pinhole recovery only one `.ptf` may be in `/Firmware`. For the menu it does not matter.
3. Open **Config > Firmware > Firmware update**, pick the 1.2.0 file and confirm.
4. You can leave the 1.3.0 plugin files on the card. 1.2.0 lists the plugins it knows and ignores `chip.plg` because CHIP is built into 1.2.0. It also ignores `/Plugins/synth-order.txt` and `/Features/`.

What to expect on 1.2.0:

- Projects saved by 1.3.0 open. A CHIP part plays on the built-in CHIP.
- A patch saved by 1.3.0 with a macro amount in Scale mode plays with the old Add rule, so that macro sounds different.
- 1.2.0 has no Glue of its own, so what a Glue slot does there depends on `glue.plg`. Without the file in `/Plugins/Master/` the slot shows Missing and the project plays without Glue, but the project keeps the Glue values and a save on 1.2.0 writes them back, so 1.3.0 finds Glue as you left it. With the file on the card 1.2.0 lists it like any other insert. The plugin format is the same, so Glue should run there, with Makeup showing a number where 1.3.0 shows `Auto`. The first case was checked in the simulator, the second by reading the code and comparing the plugin format, not on a unit.
- `O - Patch` keeps the macros and the automation of the open slot, as 1.2.0 always did.
- 1.2.0 has no Para mode and no chord capture. A MIDI Chord step plays its notes as separate voices there. Chords you captured on 1.3.0 still play, because 1.2.0 knows all 68 chord values.

Going back to 1.1.0 works the same way with the file of the tag `tracker-ff-v1.1.0`. 1.1.0 does not have the master inserts, so the inserts of a project do not play there.

## Checksums

The release page lists the SHA-256 of the zip in `SHA256SUMS`. The zip also holds `tracker-ff/SHA256SUMS` for every file in it, the image included. Check the zip before you unzip it and run `shasum -a 256 -c tracker-ff/SHA256SUMS` from the card root afterwards, because a bad copy is the most common reason for a failed update.
