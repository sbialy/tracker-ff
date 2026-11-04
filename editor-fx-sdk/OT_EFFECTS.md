# OpenTracker mix effects as Tracker plugins

Six offline effects for the original Tracker (MK66), built as `.ptfx` plugins. Copy the files to `/Effects` on the SD card, they then show up in the sample editor effect list. Every plugin is mono, 16 bit, 44.1 kHz, uses no libc or libm and fits the 32 KB code plus bss limit.

**Nothing here has been run on a device.** The plugins build with `ptfx_builder.py` and were rendered through a host harness (`make ptfx-check`) with a fake host table backed by the real libm. The harness runs the same C source, but compiled for the host and not for the Cortex-M4.

| File | Name on screen | Code | bss | Scratch |
|---|---|---|---|---|
| `ot_tape.ptfx` | Tape Saturation | 1496 | 548 | 0 |
| `ot_lofi.ptfx` | Lo-Fi Chip | 2766 | 340 | 0 |
| `ot_reverb.ptfx` | Reverb | 3664 | 64 | 65536 |
| `ot_dirt.ptfx` | Dirt Drive | 2756 | 40 | 0 |
| `ot_bbd_chorus.ptfx` | BBD Chorus | 1659 | 552 | 0 |
| `ot_dub_echo.ptfx` | Dub Echo | 2492 | 60 | 132300 |

Sizes are in bytes.

## Parameters

**Tape Saturation** - drive into a soft clip, head loss high cut, wow and flutter as a modulated delay of up to 2.3 ms, then dry / wet.

| Knob | Range | Default |
|---|---|---|
| Drive | 0 to 100 % | 30 |
| Wow | 0 to 100 % | 20 |
| Flutter | 0 to 100 % | 10 |
| Mix | 0 to 100 % | 100 |

**Lo-Fi Chip** - a chip DAC (bit depth with optional ladder error), sample and hold, a small speaker shape and dry / wet.

| Knob | Range | Default |
|---|---|---|
| Bits | 4 to 16 (16 is no quantiser) | 8 |
| Rate | Off, 32 kHz, 22 kHz, 16 kHz, 11 kHz, 8 kHz, 6 kHz, 4 kHz | 11 kHz |
| Ladder | 0 to 100 % | 0 |
| Speaker | Off, TV, Handheld, Monitor | TV |
| Mix | 0 to 100 % | 100 |

TV and Handheld are not level neutral. A loud square can peak above full scale before the output clamp.

**Reverb** - the Tracker firmware's own Polyverb (Clouds style, Griesinger topology) as an insert. Adds up to 5 s of tail to the sample.

| Knob | Range | Default |
|---|---|---|
| Size | 0 to 100 % (reverb time, 100 is 0.95) | 70 |
| Damp | 0 to 100 % (high cut inside the loop) | 50 |
| Predly | 0 to 100 % of 5000 samples (113 ms) | 0 |
| Diffus | 0 to 85 % | 62 |
| Mix | 0 to 100 % | 35 |

**Dirt Drive** - pre-gain into a shaper, bit quantiser, sample and hold, tone low-pass and dry / wet.

| Knob | Range | Default |
|---|---|---|
| Type | Soft, Hard, Fold, Asym | Soft |
| Drive | 0 to 100 % | 30 |
| Bits | 4 to 16 (16 is no quantiser) | 16 |
| Rate | 1 to 32 frames per hold (1 is no hold) | 1 |
| Tone | 0 to 100 % (200 Hz to 20 kHz, 100 is off) | 100 |
| Mix | 0 to 100 % | 100 |

**BBD Chorus** - the classic two mode bucket brigade chorus. Mono, so the two taps are summed and there is no width knob.

| Knob | Range | Default |
|---|---|---|
| Mode | Chorus I, Chorus II | Chorus I |
| Tone | 0 to 100 % (BBD filter 6 to 12 kHz) | 50 |
| Mix | 0 to 100 % | 100 |

**Dub Echo** - a mono echo with a filtered, soft clipped feedback loop and a duck. Time is in milliseconds, there is no tempo sync. Adds a tail of up to 5 s.

| Knob | Range | Default |
|---|---|---|
| Time | 20 to 1500 ms | 350 |
| Fdbk | 0 to 100 % (98 % at the top) | 45 |
| LowCut | 0 to 100 % (20 to 500 Hz in the loop) | 40 |
| Tone | 0 to 100 % (500 Hz to 9 kHz in the loop, 100 is open) | 50 |
| Duck | 0 to 100 % | 50 |
| Mix | 0 to 100 % | 35 |

The tail is capped at 5 s, so with a long Time and high Fdbk the repeats that are still audible at the cap are stopped there. With Fdbk at the top, Tone open, Duck off and Mix at 100 the dry signal plus the repeats can reach full scale and clip.

## Differences to the originals

- The originals are stereo master bus effects with an On switch, block-rate ramps and (for Echo) a tempo sync. The ports are mono and offline, so those parts are gone and parameters are applied as constants. Details are in the header of each source file.
- Everything is evaluated per sample, so the plugins have no 128 frame block assumption and give the same result for any block size.
- Reverb input gain is 0.2 instead of the firmware's 0.1 (a send level), Size stops at 0.95 and Damp at 0.95.
- Echo runs its line at the full sample rate, the original keeps it at half rate, so its repeats are brighter at the same Tone.
- The plain Chorus module was not ported, stock Chorus covers it.

## Build and check

```
make ptfx          # builds every effects/*.c into editor-fx-sdk/build/Effects
make ptfx-check    # builds the ot_*.c effects, renders them on the host, checks the result
```

`make ptfx-check` writes a synthetic test sample (`_input.wav`) and for each plugin five renders (`default`, `min`, `max`, `mixA`, `mixB`) to `build/ptfx-wav`. Checks per render: no NaN or inf inside the plugin, scratch guards untouched, not silent, no full scale stuck output, tails do not grow and lose at least 6 dB. Per plugin: the same output for a second init, for in place processing and for 37 sample blocks against 8192 sample blocks. `PTFX_PARAMS=70,50,0,62,100 build/ptfx-host/ot_reverb build/ptfx-wav` renders one custom parameter set to `<plugin>_custom.wav`.

## Licences

Tape, Lo-Fi, Dirt and Dub Echo are MIT (opentracker-fx, Copyright (c) 2026 Marcin Spoczynski, text in `LICENSE-opentracker-fx-MIT`). BBD Chorus derives from Apache-2.0 OpenTracker code (`synth-open/LICENSE-OpenTracker-Apache-2.0`). Reverb is Polyverb with Emilie Gillet's MIT header kept. See section 14 of `NOTICE.md`.
