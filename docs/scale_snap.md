# Scale snap in tracker-ff 1.4.0

Scale snap keeps the notes of a track in the key of your project. A note that is not in the scale moves to a scale note at the moment it plays. The notes you wrote in the pattern stay as they are.

- [What it does](#1-what-it-does)
- [Set the scale and the root note](#2-set-the-scale-and-the-root-note)
- [Turn it on for a track](#3-turn-it-on-for-a-track)
- [The four modes](#4-the-four-modes)
- [Snap FX: change the mode inside a pattern](#5-snap-fx-change-the-mode-inside-a-pattern)
- [What is not snapped](#6-what-is-not-snapped)
- [Tips](#7-tips)
- [Going back to 1.3.x](#8-going-back-to-13x)

## 1. What it does

Every track has a snap mode: Off, Nearest, Down or Up. When a track plays a note that is not in the project scale, the Tracker plays the scale note the mode picks instead. A note that is already in the scale plays as written.

- It works on synth tracks, sample tracks and MIDI tracks alike. On a sample track the sample is played at the pitch of the scale note, so it is repitched by the same amount.
- It changes only what you hear and what goes out of the MIDI port. The pattern keeps the notes you wrote. Switch the mode to Off and the original notes play again.
- The default is Off on every track. A project that never uses it plays as before.

## 2. Set the scale and the root note

Scale snap uses the scale and the root note of the project. These are the same two settings that lay out the pads.

1. Press **Config** and open the **Project Settings** group.
2. Set **Notes scale**, for example `Major`.
3. Set **Root note**, for example `C`.

With the scale on `Chromatic` every note is in the scale, so snap changes nothing. Pick a real scale first.

## 3. Turn it on for a track

1. Press **File** and then **F7** (the button reads Project Settings). This is the Project Settings page of the project. It is not the Config group of the same name.
2. Go to the **Scale snap** row. Its value column says `Off` or the number of tracks that snap, for example `2 tracks`.
3. Press **F8** (Enter). The list shows Track 1 to Track 16.
4. Go to the track, press **F8** and pick Off, Nearest, Down or Up with Up and Down or the encoder. Press **F8** again to set it.
5. Press **F7** or **Left** to go back, once for the list and once more for the page.

Save the project. The snap modes are stored with it, in the `project.mtff` file next to the project.

## 4. The four modes

The example is a C major project and a pattern that plays a C# (it is not in C major). The two scale notes next to it are C below and D above.

| Mode | Which note it plays | The C# plays as |
|---|---|---|
| Off | The note as written | C# |
| Nearest | The closest scale note. A note exactly halfway goes down | C |
| Down | The scale note at or below the note | C |
| Up | The scale note at or above the note | D |

Two details:

- In C major every note that is not in the scale sits exactly halfway between two scale notes, so Nearest and Down give the same result there. They differ in a scale with wider gaps. In C major pentatonic (`Penta Maj`: C D E G A) an F# is one semitone below G and two above E. Nearest plays G, Down plays E and Up plays G.
- A note at the very edge of the keyboard range can have no scale note on its side. Down then takes the next one above and Up the next one below.

## 5. Snap FX: change the mode inside a pattern

The Snap FX (`N`) sets the snap mode of its track from its step on. Use it when a track should follow the scale in one part of a pattern and play free in another.

To add it, press **Rec**, put the cursor on a step with a note, hold the FX1 button (Vol) or the FX2 button (Fx) and turn the encoder until the list shows `N - Snap`. It sits after Tape in the FX list. Let go of the button and turn the encoder to pick the value. The value is the mode and a new Snap FX starts on Nearest. The note of the step that holds the FX is snapped too.

| Value | Shown as | What the track does from this step |
|---|---|---|
| Off | `off` | Plays the notes as written, whatever the project setting says |
| Nearest | `nea` | Snaps to the nearest scale note |
| Down | `dwn` | Snaps down |
| Up | `up` | Snaps up |
| Track | `trk` | Goes back to the setting of the track on the Project Settings page |

When to use each value:

- **Off** for a bar of chromatic runs or a blue note on a track that is snapped the rest of the time.
- **Nearest** to turn snap on for a part of a pattern when the track is Off in the project.
- **Down** or **Up** to push a part of a pattern one way, for example a lead line that should only bend upward into scale notes.
- **Track** to end a change and return to the project setting from that step on. This is the way back when the track is set to Down in the project and one bar was Off.

How long it holds:

- The Snap FX holds until the next Snap FX on the same track or until the pattern ends (and each time the pattern loops). Then the track goes back to its project setting.
- It holds per track. A Snap FX on track 3 does not touch track 4.
- Stop and Play, a pattern change in the song and a Part change clear it too.
- If you start playing from the cursor in the middle of a pattern, the track already uses the last Snap FX above the cursor. A step sounds the same as it does when you play from the top.
- A Chance FX in the partner slot of the Snap FX can skip it. When the chance says no, the Snap FX does nothing on that pass.
- Playing a pattern backwards or in random order does not look back. The Snap FX holds only from the step the playhead reaches it.

## 6. What is not snapped

- **Pads you play live.** Notes you play on the pads are heard as played. If you record them the pattern gets the notes as you played them, and they snap when the pattern plays.
- **MIDI in.** Notes from a MIDI keyboard that play a track are heard as played and written as played.

The pattern grid always shows what you wrote, not the note that was snapped.

## 7. Tips

- **Leave drum sample tracks Off.** A snapped note repitches the sample, so a kick or a snare that sits on an out-of-scale note changes its pitch.
- **Basslines: try Down or Up as well as Nearest.** Nearest picks whichever scale note is closer, so one wrong note can move down and the next one up, and the line changes shape. Down or Up moves every wrong note the same way and keeps the contour of the line. Listen to all three, the right one depends on the scale.
- **Pick the scale first.** If snap seems to do nothing, check that Notes scale is not `Chromatic` and that the track is not Off.
- **Hear it before you commit.** Switch a track between Off and Nearest while the pattern plays and compare. The pattern is not changed, so you lose nothing.
- **Change the root note** to move the whole song to another key. Every snapped track follows.

## 8. Going back to 1.3.x

Firmware 1.3.x does not read `project.mtff`, so on 1.3.x scale snap does nothing and every note plays as written. The file stays on the card. Open the project on 1.4.0 again and the modes apply again.

The Snap FX does nothing on 1.3.x either. A step that holds it shows as an unknown FX and plays its note without the FX. Save As on 1.3.x makes a copy without `project.mtff`, so make a copy of a project that uses scale snap on 1.4.0.
