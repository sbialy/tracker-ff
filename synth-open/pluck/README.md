# PLK (Pluck)

A plucked string engine for the open PolySynth shim on the original Tracker (MK66, Cortex-M4F 180 MHz). Pick `PLK` in `SYNTH_ENGINES`.

## Clean room

`pluck.cpp` is our own implementation of the textbook Karplus-Strong plucked string algorithm with the extensions of Jaffe and Smith:

- K. Karplus and A. Strong, "Digital Synthesis of Plucked-String and Drum Timbres", Computer Music Journal 7(2), 1983
- D. A. Jaffe and J. O. Smith, "Extensions of the Karplus-Strong Plucked-String Algorithm", Computer Music Journal 7(2), 1983

A delay line is filled once with a burst and then recirculates through a loop filter. The extensions used here are a first order allpass for fractional tuning, a one pole lowpass loop filter with a decay compensating gain, a comb on the burst for the pick position and an allpass cascade for dispersion. No third-party code was read, copied or adapted. Nothing of Polyend's closed `libPolySynth.a` was used. The parameter set, ranges and defaults are this shim's own choice because no Polyend engine of this kind exists. The only shared parts are the `ISynthEngine` interface (`ot/audio/synth_engine.h`, OpenTracker, Apache-2.0) and the shim's `EngineDef` metadata.

## Ids

| What | Value |
| --- | --- |
| Name in `SYNTH_ENGINES`, on screen and `Patches/PLK/` folder | `PLK` |
| Stored number in project files (`STORED`, the poly-editor's engine number) | 18 |
| `polysynth::InstrumentTypeId` | 0x8A (138), ours, after OpenTracker's Arc-Type 0x89 |
| Engine number in field 1 of a `.patch` file | 0x8A |
| List and editor icon | the VAP logo (no new artwork) |

## Parameters

| # | Name | Range, default | Meaning |
| --- | --- | --- | --- |
| 0 | Exciter | Noise, Click, Saw, Mallet, Noise | Shape of the burst the string is struck with. Every burst has zero mean and the same RMS (peak limited) |
| 1 | Brightness | 0 to 100 %, 60 % | Colour of the burst and cutoff of the loop lowpass |
| 2 | Damping | 0 to 100 %, 35 % | Time for the fundamental to fall by 60 dB: 0 % is 15 s, 100 % is 0.1 s, 35 % is 2.6 s. The same on every key |
| 3 | Pick Pos | 0 to 100 %, 35 % | Comb on the burst. 0 % is at the bridge (no comb), 100 % is the middle of the string (no even harmonics) |
| 4 | Stretch | 0 to 100 %, 0 % | Dispersion: the partials drift sharp, the sound turns bell like and metallic |
| 5 | Release | 0.02 to 2 s, 0.15 s | Note off damps the string (time to -60 dB, duller) instead of cutting it |
| 6 | Tune | -24 to 24 st, 0 | |
| 7 | Fine | -100 to 100 c, 0 | |
| 8 | Volume | 0 to 200 %, 100 % | Applied by the framework, never by the engine |

Velocity (the Volume FX of a step, 0 to 1) brightens and lengthens the note: Brightness drops by up to 0.4 and the Damping time shrinks to 55 % at velocity 0. Velocity 1 is the plain patch. There is no filter section and no envelope other than the string itself and Release. Pan and the sends belong to the slot. Mono output, L = R.

Six default macros: Brightness, Damping, Stretch, Pick Pos (bipolar), Release and Fine (bipolar).

## How a voice works

```
burst (once, into the empty line) -> delay line -> tuning allpass -> dispersion allpass x2 -> loop lowpass -> loop gain -> rounding -> delay line
```

- **Line.** One `int16` ring of 544 samples (1,088 bytes) per voice, from a shared pool of 8 (the shim never sounds more than 8 voices). The pool is plain `.bss` data (8,772 bytes of SRAM), the voice state itself is 68 bytes of the 128 the framework gives. A line belongs to a voice record (keyed by its address, like CHIP). A new note takes a free line, else the oldest one, whose voice then ends in its next block.
- **Low notes.** 544 samples hold a period down to E2 (82.4 Hz). Notes below E2 run the loop at half the sample rate (the line then covers twice the time, the output is linearly interpolated), which brings E1 into the same line and makes them cheaper. The mode is picked at note on.
- **Tuning.** The total loop delay is the sample rate divided by f0. The phase delays of the loop lowpass and the dispersion allpasses at f0 are computed in closed form, the rest is an integer line length plus a first order allpass of 0.5 to 1.5 samples whose coefficient is solved at f0 (two correction steps, the allpass delay is not flat). Measured error is below 1 cent from E1 to E7 (0.62 cent worst, the measurement itself wobbles by a few tenths of a cent with a Noise burst).
- **Even damping.** The loop gain is set so the fundamental loses exactly `exp(-6.9078 / (f0 * T60))` per pass whatever the lowpass takes. The lowpass cutoff follows `sqrt(f0)`, so the partials decay about equally fast in seconds on every key. The lowpass may take 60 % of the loss budget at most, the pole is capped so a high note with a long Damping does not need a loop gain above 1. The consequence is that Brightness has less reach on high notes with a long Damping.
- **Rounding.** The loop writes `int16`. Plain rounding would leave a noise floor of constant samples on a long decay and truncation would add its own damping, so the rounding error is fed back (first order noise shaping). A voice ends when the mean level of the block stays under 3 LSB for longer than one period.
- **Note off.** The loop is retuned with the Release time and a cutoff of 40 %. The pitch does not move.
- **Pitch bend.** The step FX bend, Tune and Fine retune the loop at block rate (the line length steps by one sample at most, the allpass takes the fraction). Nothing runs per sample except the loop.
- **No libm.** The few transcendentals needed per note (2^x, exp, sin, cos, atan) are polynomials in `pluck.cpp`, the square root is `vsqrt`. Nothing per sample is more than adds, multiplies and one rounding trick.

## Build

`make original SYNTH_OPEN=1 SYNTH_ENGINES="... PLK"`. `core/makefile` builds `pluck.cpp` at `-O2 -ffp-contract=off` (`SYNTH_OPT_PLK`), as PHZ: a fused multiply add costs 3 cycles on the M4F, a multiply plus an add 2. The per note code (`retune`, `excite`, the polynomials, `noteOn`) is `noinline` and `optimize("Os")`, inlined copies of the polynomials cost 3.5 KB of flash. Linked size of PLK: 5,736 bytes (the marginal cost in a real image is 5,800 bytes). It also takes 8,772 bytes of RAM, which the heap loses.

## Host tools

`synth-open/tools/pluck_render.sh [outdir]` renders the WAV files (default `build/pluck-wav`, list in `pluck_render.cpp`) and then runs the checks. `pluck_render --check` runs only the checks (95 s). The checks:

- Pitch within 5 cents of the equal tempered target for every note from E1 to E7 at 13 parameter sets (default, Brightness 0 and 100 %, each Exciter, Pick Pos 0 and 100 %, Stretch 30 and 100 %, Damping 0 and 60 %, Tune and Fine). Measured by a Hann windowed DFT peak search over +-80 cents in 1 cent steps with parabolic refinement. Worst case 0.62 cent.
- Damping time of the fundamental on every fourth key from E1 to C7 at Damping 35 %: 2.52 to 2.61 s against 2.60 s expected.
- Loudness of the four Exciters within 4 dB, no clipping, finite mono output.
- A held note with a short Damping frees its voice, and a note off ends the string.

`host_smoke_test.cpp` (via `run_smoke.sh`) covers PLK like every other engine: metadata, sound, velocity, voice cap, `.patch` round trip and the stored number pin.

## CPU

Emulated, `tools/phz_cycles/run_bench.sh fw PLK` (Unicorn, firmware flags, instruction count with a cycle cost table, block = 522,240 cycles). **The device reads higher than this number**, as for every other engine (SDRAM waits and interrupts are not modelled, the delay lines are in SRAM so PLK is hit less than most). Four voices as the shim renders them (chord A3 C#4 E4 G#4):

| Scenario | 4 voices | Per voice |
| --- | --- | --- |
| Default patch | 29,090 cycles (5.57 %) | 7,270 cycles = **1.39 %** of a block |
| Worst steady state: Stretch 100 %, Damping 0, Brightness 100 % (dispersion on) | 32,674 (6.26 %) | 8,170 = **1.56 %** |
| Same chord 28 semitones down (half rate loop) | 25,177 (4.82 %) | 6,290 = 1.20 % |
| Every parameter at its minimum | 22,946 (4.39 %) | 5,740 = 1.10 % |
| Note on for all 4 voices in one block, over the default | +115,669 | about 29,000 per note on, once (includes the emulator's byte wise `memset` of the line, the real one is faster) |

An idle voice costs nothing: the voice ends by itself (`render` returns false), then the shim's `partIdle` path sees silence.

## Flash and RAM

Linked 5,736 bytes. In the PLUGK test set (`ACD FAT VAP WTFM PERC CHIP PHZ DTYPE CSTYPE ATYPE TTYPE 8TYPE BTYPE PLK`) the image grows from 1,016,712 to 1,022,512 bytes (+5,800) and ends at 0xF9A30, which is 10,364 bytes under 0xFC2AC. With ARCTYPE added the set ends at 0xFCDCC (1,035,724 bytes), 2,848 bytes past 0xFC2AC, so ARCTYPE is left out of this test set (the limit was not raised). RAM: +8,772 bytes of `.bss`.

## Moving it to a plugin (phase 3)

The DSP is self-contained: no libm, no dependency on the shim beyond the `ISynthEngine` types, constants are `constexpr` and the only tables are two small arrays of floats (`kTrim` and the Click shape) with no pointers. What a `.plg` port has to change: the `Pool` becomes memory the host hands to the plugin (8,772 bytes), `gPluck` and its vtable become the plugin's entry points, and the `__builtin_memcpy` bit casts and the `vsqrt` inline assembly stay as they are.

## Limits

- Lowest note E1 (41.2 Hz), highest tuned note about C8. A bend below the line length of a note flattens.
- A line taken over by a ninth voice would silence the oldest. The shim cannot sound a ninth, so this only happens with leaked voice records after a project load.
- Not flashed and not device tested. All numbers above are host and emulator numbers.
