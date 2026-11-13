# Chords and macros in tracker-ff 1.3.0

This guide covers five features of tracker-ff 1.3.0 for the original Tracker. They all work together around the `0 - MIDI Chord` step FX:

- [Macro depth](#1-macro-depth-add-and-scale) decides how far a macro moves a synth parameter.
- [Paraphony](#2-paraphony-poly-and-para) lets one synth voice play a whole chord.
- [Chord names](#3-chord-names) shows chords by name and lists the 68 chords the firmware knows.
- [Chord capture](#4-chord-capture) writes a chord into a step from the pads or a MIDI keyboard.
- [Record join](#5-record-join) turns notes you play together while recording into one chord step.
- [Strum](#6-strum-fx) starts the notes of a chord one after another.

[MIDI in and out](#7-midi-in-and-out) and the [limits](#8-limits-and-known-issues) come last.

## 1. Macro depth: Add and Scale

A macro is a knob (a value from 0 to 100 %) that moves up to five synth parameters at once. You set the macros on the Macros page of the Synth screen. The Params button cycles Synth Parameters, Synth Macros and Synth Automation. Each target of a macro has an Amount. The Amount runs from -2.00 to 2.00 in steps of 0.01 and the Tracker shows it as a decimal number, so 1.00 means 100 %. The macros are moved by the `a - CC A` to `f - CC F` step FX (Macro 1 to 6) and by automation slots that sit on a macro.

How far the macro pushes a parameter depends on the depth mode of the patch.

| Mode | What the macro adds to the parameter |
|---|---|
| Add | Amount x macro position x the whole range of the parameter. The result is cut off at the ends of the range. |
| Scale | Amount x macro position x the room that is left. With a positive push the room is the distance from the parameter value up to its maximum. With a negative push it is the distance down to its minimum. |

So in Scale mode an Amount of +1.00 with the macro at 100 % reaches the maximum of the parameter and -1.00 reaches the minimum, wherever the parameter was set. The macro can never overshoot. In Add mode the same Amount pushes by the full range, so a parameter that sits high hits the top early and a macro with a large Amount cuts off.

### Worked example

A parameter runs from 0 to 100 and is set to 40. One macro points at it.

| Macro position | Amount | Add gives | Scale gives |
|---:|---:|---:|---:|
| 100 % | +1.00 | 100 (40 + 100 is cut off at 100) | 100 |
| 50 % | +1.00 | 90 | 70 |
| 100 % | +0.50 | 90 | 70 |
| 50 % | -1.00 | 0 (40 - 50 is cut off at 0) | 20 |
| 100 % | -0.50 | 0 (40 - 50 is cut off at 0) | 20 |

In Scale mode the macro at 50 % always lands half way between the parameter value and the end it moves toward. A bipolar macro works the same way with its centre as the rest position. Moving it below the centre reverses the direction of the push.

When several macros point at one parameter their pushes are added together and the sum is cut off at the ends of the parameter. Automation slots that sit directly on a parameter always work like Add. The depth mode only changes macro targets.

### Which patches get which mode

| Patch | Mode |
|---|---|
| A new patch made on 1.3.0 | Scale |
| A patch file that does not store a mode, which is every `.patch` file made by 1.2.x or earlier | Add |
| A patch saved by 1.3.0 in Scale | Scale |
| An Add patch that you load and save again on 1.3.0 | Add, it keeps its mode |

The mode belongs to the patch. Each slot of the Patch FX bank keeps its own mode, so the `O - Patch` FX switches to the macros of the new slot in that slot's mode.

### On the unit and on 1.2.x

The unit has no row to switch the mode yet. A patch keeps the mode it was created or loaded with. Firmware 1.2.x knows only Add. A patch saved by 1.3.0 in Scale loads there and its macros work as Add, so a macro with a large Amount moves its parameter further than it did on 1.3.0.

## 2. Paraphony: Poly and Para

A `0 - MIDI Chord` step plays several notes. What happens next depends on the Synth engine of the instrument.

| | Poly | Para |
|---|---|---|
| Voices | One voice for each chord note | One voice for the whole chord |
| Sound | Every note is a full voice with its own filter and envelopes | The chord notes are extra oscillators of one voice. They share the filter, the envelopes and the LFO |
| Audio CPU | More | Less |
| Note off | Every chord note gets its own note off | One note off ends the whole chord |

In Poly a chord of four notes uses four voices. The synth parts draw from a shared pool of 4 voices by default, so one four note chord can take the whole pool and a new note takes over a sounding one when the pool is full. In Para the same chord uses a single voice, so even a part with Polyphony set to 1 plays complete chords.

### Which engines

ACD is the only engine of this release with a Para mode. Every other engine plays the chord notes as separate voices, as before. The synth plugin interface has an optional chord query, so a plugin engine can offer Para too.

### The parameters

ACD has three new parameters at the end of its Voice section on the Synth Parameters page. They come after Glide Mode, Glide Time and Volume.

| Parameter | Range | Default | What it does |
|---|---|---|---|
| Chord Mode | Poly or Para | Poly | Switches the part between the two ways of playing a chord. It is saved with the patch. |
| Chord | 0 to 67, shown as a chord name | 0 (`note`) | The chord that Para plays for every note that does not come from a MIDI Chord step. |
| Strum | -63 to 63 | 0 | In Para, the gap between the chord notes in units of 2 ms. Positive starts the base note first, negative starts the top note first, 0 plays them together. |

The Chord list uses the names of the [chord table](#the-68-chords). Patches from 1.2.x load with Chord Mode on Poly and sound as before. While Chord Mode is Poly, the Chord and Strum parameters do nothing.

### How a MIDI Chord step plays

| Instrument | What the step sends |
|---|---|
| Synth part, engine in Poly | One note on for each chord note, to the synth part. Each takes a voice |
| Synth part, ACD in Para | One note on for the step note. The chord of the step is used for that note only, in place of the Chord parameter of the patch. The step chord has at most four notes, so the voice has the base note plus up to three extra oscillators |
| MIDI instrument | One note on for each chord note on the MIDI channel of the instrument |
| Sample instrument | No chord. Samples ignore the MIDI Chord FX |

A note that is played from the pads or a MIDI keyboard on a Para part uses the Chord of the patch. With Chord on `maj`, every key sounds a major chord from one voice. A MIDI Chord step replaces that chord for its own note.

### Strum and Para

A Strum FX next to the MIDI Chord of a step reaches a Para part as the Strum parameter of that note. The oscillators start one after another inside the voice. When the Strum FX is off (0 or 64) or missing, the Strum parameter of the patch is used. A Strum FX with a value does not change the patch. See [Strum FX](#6-strum-fx).

## 3. Chord names

The firmware knows 68 chords. The table is the MIDI Chord FX table. The FX value is the row number from 0 to 67. Each row is a set of notes above the lowest note of the step. The lowest note of the step is always the bass. Values 30 to 67 are inversions. Polyend's own firmware plays only the step note for values above 29, so a project with those values sounds thinner there.

Config > Features > Chord names (State On by default) switches the MIDI Chord cells of the pattern grid between two looks. On shows a short name of three characters such as `m7` and Off shows the interval digits of the stock firmware. It changes the display only. The project and the playback stay the same. The value 0 shows `---` in both looks.

The name of a chord comes in four looks.

| Where | Shown as | Example |
|---|---|---|
| Pattern grid | Three character short name | `^7` |
| Title bar with the cursor on a MIDI Chord cell | `MIDI Chord:` and the chord with its root and bass, worked out from the step note | `MIDI Chord: Fmaj7/C` |
| Capture popup | The same name, with `~` in front when the match is approximate | `~Cmaj7` |
| Fill popup and the Chord parameter of ACD | The name of the table row without a root | `maj7 inv2` |

A name with a slash says which note is in the bass. `Fmaj7/C` is F major seventh with C as the lowest note. A root position chord has no slash. The major triad is the root alone, so it is `C` and in second inversion `F/C`. When the step has no note the title bar shows the name of the row without a root.

### The 68 chords

All intervals are semitones above the lowest note. The examples have C as the lowest note. In the first table the lowest note is the root.

| Value | Grid name | Grid digits | Name | Example with C lowest | Notes above the lowest (semitones) |
|---:|---|---|---|---|---|
| 0 | `---` | `---` | note | C | none |
| 1 | `Su2` | `27` | sus2 | Csus2 | 2 7 |
| 2 | `Su+` | `28` | sus2#5 | Csus2#5 | 2 8 |
| 3 | `dim` | `36` | dim | Cdim | 3 6 |
| 4 | `min` | `37` | m | Cm | 3 7 |
| 5 | `maj` | `47` | maj | C | 4 7 |
| 6 | `aug` | `48` | aug | Caug | 4 8 |
| 7 | `su4` | `57` | sus4 | Csus4 | 5 7 |
| 8 | `s42` | `5A` | sus4 inv2 | Fsus4/C | 5 10 |
| 9 | `po1` | `5C` | 5 inv1 | F5/C | 5 12 |
| 10 | `su#` | `67` | sus#4 | Csus#4 | 6 7 |
| 11 | `pow` | `7C` | 5 | C5 | 7 12 |
| 12 | `po9` | `7E` | 5add9 | C5add9 | 7 14 |
| 13 | `6S2` | `279` | 6sus2 | C6sus2 | 2 7 9 |
| 14 | `7S2` | `27A` | 7sus2 | C7sus2 | 2 7 10 |
| 15 | `MS2` | `27B` | M7sus2 | CM7sus2 | 2 7 11 |
| 16 | `di7` | `369` | dim7 | Cdim7 | 3 6 9 |
| 17 | `m7b` | `36A` | m7b5 | Cm7b5 | 3 6 10 |
| 18 | `^71` | `378` | maj7 inv1 | G#maj7/C | 3 7 8 |
| 19 | `mi6` | `379` | m6 | Cm6 | 3 7 9 |
| 20 | `m7` | `37A` | m7 | Cm7 | 3 7 10 |
| 21 | `mM7` | `37B` | mM7 | CmM7 | 3 7 11 |
| 22 | `ma6` | `479` | 6 | C6 | 4 7 9 |
| 23 | `do7` | `47A` | 7 | C7 | 4 7 10 |
| 24 | `^7` | `47B` | maj7 | Cmaj7 | 4 7 11 |
| 25 | `mM1` | `489` | mM7 inv1 | AmM7/C | 4 8 9 |
| 26 | `a7` | `48A` | 7#5 | C7#5 | 4 8 10 |
| 27 | `aM7` | `48B` | M7#5 | CM7#5 | 4 8 11 |
| 28 | `7s4` | `57A` | 7sus4 | C7sus4 | 5 7 10 |
| 29 | `Ms4` | `57B` | M7sus4 | CM7sus4 | 5 7 11 |

Inversions. The name says which chord note is lowest. The example shows the chord that results with C in the bass.

| Value | Grid name | Grid digits | Name | Example with C lowest | Notes above the lowest (semitones) |
|---:|---|---|---|---|---|
| 30 | `ma1` | `38` | maj inv1 | G#/C | 3 8 |
| 31 | `ma2` | `59` | maj inv2 | F/C | 5 9 |
| 32 | `mi1` | `49` | m inv1 | Am/C | 4 9 |
| 33 | `mi2` | `58` | m inv2 | Fm/C | 5 8 |
| 34 | `^72` | `459` | maj7 inv2 | Fmaj7/C | 4 5 9 |
| 35 | `^73` | `158` | maj7 inv3 | C#maj7/C | 1 5 8 |
| 36 | `m72` | `358` | m7 inv2 | Fm7/C | 3 5 8 |
| 37 | `m73` | `259` | m7 inv3 | Dm7/C | 2 5 9 |
| 38 | `do1` | `368` | 7 inv1 | G#7/C | 3 6 8 |
| 39 | `do2` | `359` | 7 inv2 | F7/C | 3 5 9 |
| 40 | `do3` | `269` | 7 inv3 | D7/C | 2 6 9 |
| 41 | `di1` | `39` | dim inv1 | Adim/C | 3 9 |
| 42 | `di2` | `69` | dim inv2 | F#dim/C | 6 9 |
| 43 | `S+1` | `6A` | sus2#5 inv1 | A#sus2#5/C | 6 10 |
| 44 | `S+2` | `46` | sus2#5 inv2 | Esus2#5/C | 4 6 |
| 45 | `s#1` | `16` | sus#4 inv1 | F#sus#4/C | 1 6 |
| 46 | `s#2` | `5B` | sus#4 inv2 | Fsus#4/C | 5 11 |
| 47 | `mb2` | `469` | m7b5 inv2 | F#m7b5/C | 4 6 9 |
| 48 | `mb3` | `258` | m7b5 inv3 | Dm7b5/C | 2 5 8 |
| 49 | `mM2` | `458` | mM7 inv2 | FmM7/C | 4 5 8 |
| 50 | `mM3` | `148` | mM7 inv3 | C#mM7/C | 1 4 8 |
| 51 | `a71` | `468` | 7#5 inv1 | G#7#5/C | 4 6 8 |
| 52 | `a72` | `248` | 7#5 inv2 | E7#5/C | 2 4 8 |
| 53 | `a73` | `26A` | 7#5 inv3 | D7#5/C | 2 6 10 |
| 54 | `aM1` | `478` | M7#5 inv1 | G#M7#5/C | 4 7 8 |
| 55 | `aM2` | `348` | M7#5 inv2 | EM7#5/C | 3 4 8 |
| 56 | `aM3` | `159` | M7#5 inv3 | C#M7#5/C | 1 5 9 |
| 57 | `257` | `257` | 7sus4 inv1 | G7sus4/C | 2 5 7 |
| 58 | `35A` | `35A` | 7sus4 inv2 | F7sus4/C | 3 5 10 |
| 59 | `58A` | `58A` | 7sus2 inv1 | A#7sus2/C | 5 8 10 |
| 60 | `357` | `357` | 7sus2 inv2 | F7sus2/C | 3 5 7 |
| 61 | `249` | `249` | 7sus2 inv3 | D7sus2/C | 2 4 9 |
| 62 | `59A` | `59A` | M7sus2 inv1 | A#M7sus2/C | 5 9 10 |
| 63 | `457` | `457` | M7sus2 inv2 | FM7sus2/C | 4 5 7 |
| 64 | `138` | `138` | M7sus2 inv3 | C#M7sus2/C | 1 3 8 |
| 65 | `267` | `267` | M7sus4 inv1 | GM7sus4/C | 2 6 7 |
| 66 | `45A` | `45A` | M7sus4 inv2 | FM7sus4/C | 4 5 10 |
| 67 | `168` | `168` | M7sus4 inv3 | C#M7sus4/C | 1 6 8 |

Digits in the Grid digits column are the semitone numbers written with `A` for 10, `B` for 11, `C` for 12 and `E` for 14. A chord can span up to 14 semitones. The widest rows are `5` (7 and 12) and `5add9` (7 and 14).

### How the nearest chord is found

The Tracker takes the lowest note you play as the bass and measures every other note from it.

1. If the shape matches a row exactly, that row is used. A voicing that spreads over more than 14 semitones can not match exactly.
2. Otherwise all notes are folded into one octave above the bass and the closest row is taken. The Tracker may add one missing note and may leave out notes you played. Each change has a price and the match has to cost 4 or less. A missing fifth costs 1 and any other added note costs 3. A left out 9th, 11th or 13th costs 3 and any other left out note costs 4. A tie goes to the lower row, so a root position chord wins over its inversions. A row with exactly the same note names wins its tie.
3. A match that is not exact gets a `~` in the popup, as in `~Cmaj7`. Notes with the same names in another octave count as exact, so C E G and a high C E G both give `C`. The Tracker writes the closest row, which is a close voicing.
4. If no row is cheap enough, there is no chord and the step is left as it was.

| You play | Result |
|---|---|
| C E G | `C` |
| C G | `C5` (the power chord, row 11) |
| C E | `~C` (the fifth is added) |
| C E B | `~Cmaj7` (the fifth is added) |
| C E G B D | `~Cmaj7` (the D is left out) |
| E G C | `C/E` (row 30, first inversion with E as the bass) |
| C and the C an octave up | `C` (one note, no chord) |
| C C# D | No chord |

Only the inversions in the table exist. A chord with a bass that is not one of its notes has no row, so the Tracker writes the closest one or none.

## 4. Chord capture

Chord capture writes a chord into a step so that you do not have to look up the FX value. The lowest note you hold becomes the note of the step and the chord goes into the MIDI Chord FX. Two ways work.

Both ways need these things:

- Edit mode is on (Rec, the red cursor) and the pattern is not being recorded live.
- The cursor is on one cell. A selection of several cells does not capture.
- The Pattern screen is open and no step popup is showing.
- The step belongs to a MIDI instrument or a Synth instrument. On an empty step the last used instrument is used. Sample instruments keep the normal behaviour.

### With the pads

1. Press Fx to move the cursor to an FX column. Put the cursor on a cell that holds `0 - MIDI Chord`, or on an empty FX cell when MIDI Chord was the last FX you used.
2. Hold the pads of the chord. Press them one after another or together.
3. The Tracker reads the held pads again after every new pad. The step note is the lowest held pad. The chord goes into the FX slot. If the step already has a MIDI Chord, that slot is used. If not, the slot under the cursor is used when it is empty. Otherwise the first empty slot is used. When the sequencer is not playing, each pad press plays the step as it is now.
4. When you let go of the last pad the cursor moves to the next step.

There is no time window for the pads. The chord stays open while at least one pad is held. A group takes up to 8 pads. With the Fx button held the pads pick an FX type as before. In the volume and micro timing view they set those values as before.

One pad is a single note. It sets the step note and takes the MIDI Chord FX away if the step had one.

### With a MIDI keyboard

Play the chord with the cursor on the step. The first key writes the note into the cursor step as usual. Every key that comes while a key of that group is still held joins the chord. The cursor can be in any column. After the last key is released the cursor moves to the next step. A group takes up to 8 keys. Keys come from the Notes in input and channel of Config > MIDI, see [MIDI in and out](#7-midi-in-and-out). There is no time window here either.

### What you see

When two or more notes are held, a popup at the cursor shows the chord name on the first line and the held notes from the bass up on the second line, for example `Cmaj7` and `C E G B`. A name that starts with `~` is an approximate match.

| Popup | Meaning |
|---|---|
| No chord, step unchanged | The notes match no row. The step goes back to how it was before you started |
| Both FX slots in use, chord not written | Both FX slots hold other FX. The Tracker never overwrites them |

One Undo takes the whole chord back.

## 5. Record join

While you record live, notes that you play together become one step with a MIDI Chord. Without it only the first note of a chord was written and the other notes were dropped.

### What it needs

- Live recording is on (hold Rec and press Play) and the pattern is playing.
- One track is armed, or several are armed and the cursor is on one of the armed tracks. See below.
- The instrument that you record with is a MIDI instrument or a Synth instrument.
- The first note lands on a step that has no note and no FX.

### How the notes join

The first note is written to its step as always. A later note joins that step when all of these hold:

- The first note of the group is still held.
- It starts within 100 ms of the previous note of the group.
- It starts within 300 ms of the first note.
- The group has room left. It takes up to 8 notes.
- The notes so far make a chord of the [table](#the-68-chords).

The 100 ms window slides with every note that joins. The lowest note becomes the note of the step. The chord goes into the MIDI Chord slot, or into the first empty slot. The joined note sounds while it is held. The note off of the chord is written once, after the last held note of the group is released.

A note that does not join is recorded the normal way. It gets its own step when the playhead has moved on to an empty step. Otherwise it is not recorded. A note that would not make a chord with the notes already in the group is left out and the group stays open, so the next note can still join. Notes played during the count in of the pre-roll metronome do not join.

### Quantise and velocity

Join does not change how the first note is placed. The Recording options of Config > General decide that.

| Recording options | First note lands on |
|---|---|
| Only notes or Velocity | The nearest step. In the second half of a step it goes to the next step |
| Microtiming or Microtiming + Velocity | The current step, with a micro timing offset |

The joined notes always stay in the step of the first note. With a Velocity option the step takes the velocity of the first note only. The other notes of the chord use that velocity.

### Several armed tracks

All 16 tracks start armed. Shift + Rec shows the arming buttons and F1 to F8 switch the tracks of the page on or off.

| Armed tracks | What happens |
|---|---|
| One | Notes record on that track and join |
| Several, the track under the cursor is armed | The cursor track records alone, as if it were the only armed track. Notes that do not join are dropped. They never go to another armed track |
| Several, the track under the cursor is not armed | No join. The notes spread over the armed tracks as before |

With several tracks armed, recording instruments with the Instr pads never joins. A chord played on a track whose instrument is a sample records only its first note.

## 6. Strum FX

`u - Strum` starts the notes of a MIDI Chord one after another, like a strummed guitar. Put it in one FX slot of the step and `0 - MIDI Chord` in the other one. The order of the two slots does not matter. Without a MIDI Chord in the other slot, the step plays its note alone.

| FX value | Shown as | Meaning |
|---:|---|---|
| 1 to 63 | `u +1` to `u+63` | Up. The lowest note starts first |
| 0 and 64 | `uoff` | Off. All notes start together |
| 65 to 127 | `u -1` to `u-63` | Down. The highest note starts first. Value 65 is -1 and 127 is -63 |

When you add the FX it starts at 8 (`u +8`, 16 ms between two notes). `u-10` is the value 74.

### Timing

A unit is 2 ms between two successive chord notes, so `u+10` is 20 ms. The notes start on the audio block grid of about 2.9 ms. A gap is therefore rounded to whole blocks and `u+10` comes out at about 20 ms. The error does not add up over the chord, because the start time of each note is worked out from the first note.

The largest gap is 126 ms (`u+63` or `u-63`). The biggest chord has four notes, so the notes of a chord are spread over at most about 380 ms. If the next step arrives before a strummed note has started, that note never starts. Stop drops the pending notes. Up to 12 strummed notes can wait at once across all tracks. When all 12 places are used a note starts at once.

### Where it works

| Instrument | Strum |
|---|---|
| MIDI instrument | Yes. The chord notes go out on MIDI delayed one after another |
| Synth part, engine in Poly | Yes. The notes reach the synth part one after another |
| Synth part, ACD in Para | Yes. The FX value sets the Strum parameter for that note and the oscillators of the voice start one after another |
| Sample instrument | No |

Para ACD has its own Strum parameter with the same unit, range and direction (-63 to 63). The parameter holds the strum of the patch, so every chord of the part is strummed. The Strum FX sets it per step. The FX wins when it has a value. When the FX is off (0 or 64) or missing, the parameter of the patch is used. The Strum parameter of ACD does nothing in Poly.

Random Fx never picks Strum.

## 7. MIDI in and out

### MIDI in

Chord capture and record join read the keys that arrive on the Notes in input.

| Setting in Config > MIDI | Values |
|---|---|
| Notes in | Off, USB, MIDI In jack, USB + MIDI In jack |
| Notes in channel | ALL, CH1 to CH16 |

- The USB input and the MIDI In jack work the same way for capture and join.
- The Middle C setting shifts the incoming note numbers the same way it does for playing a note.
- A channel that you gave to a track with Track 1 to 8 in channel plays that track directly. Those keys are not captured and not recorded into the pattern.
- Capture and join work in the pattern editor. In the sample editor, sample playback and the recorder the keys keep their own jobs.
- The Tracker reads incoming USB MIDI at every audio block, so chord notes sent by a keyboard or a computer arrive in time. A sender that spaces the notes of one chord more than 100 ms apart gives a group that does not join while recording.

### MIDI out

- A MIDI instrument chord step sends one note on for each chord note, on the MIDI channel of the instrument, to the output chosen in Config > MIDI > MIDI out (Off, USB, MIDI Out jack or USB + MIDI Out jack). The Middle C setting shifts the note numbers on the way out. Chord notes above 127 are left out.
- At the end of the step every chord note gets its own note off.
- A strummed chord sends the note ons one after another, so a chord with `u+10` is four note ons about 20 ms apart. A note that has not started when the step ends is never sent. Stop sends the note offs.
- A Synth part plays inside the Tracker. Its chord notes go to the synth and not to MIDI out.
- A chord has at most four notes. Capture and join take up to 8 held notes in a group.

## 8. Limits and known issues

- A patch saved by 1.3.0 with a macro in Scale mode plays with the Add rule on firmware 1.2.x, so that macro sounds different there.
- The depth mode of a patch is stored in the patch and the unit has no row to switch it yet. New patches are Scale and patches without a stored mode are Add.
- A part whose engine is missing from the card still opens its Macros and Automation pages and the edits made there are not kept.
- Para is available on ACD only. Other engines play chords as separate voices.
- Chord values above 29 (the inversions) play only the bass note on Polyend's own firmware.
- Capture and join write only to steps of MIDI and Synth instruments.
- A group of held notes takes up to 8 notes and a chord has at most 4 notes. Extra notes are ignored or left out of the match.
- A step with both FX slots taken by other FX does not take a chord.
- Strum waits for at most 12 notes at once. Strummed notes that have not started when their step ends are never sent.
