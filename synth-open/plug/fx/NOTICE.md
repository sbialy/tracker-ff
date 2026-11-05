# Notices for the shipped master insert plugins

Ship each `.plg` with this file and the licence texts it names (`licenses/` at the top of the repository).

## tape.plg (`fx/ot_tape_plug.c`)

- Port of the TAPE module of opentracker-fx, https://github.com/opentracker-fw/opentracker-fx, `modules/tape/src/tape.cpp`,
  commit 6e5a7da.
- Copyright (c) 2026 Marcin Spoczynski. MIT licence, text in `licenses/LICENSE-OpenTracker-fx-MIT.txt`.
- Modified: rewritten in C, first as an offline .ptfx effect (branch ptfx-ot), then as a stereo float live insert on the
  tracker-ff plugin ABI. The source file header lists the changes.

## lofi.plg (`fx/ot_lofi_plug.c`)

- Port of the LOFI module of opentracker-fx, https://github.com/opentracker-fw/opentracker-fx, `modules/lofi/src/lofi.cpp`,
  commit 6e5a7da. Copyright (c) 2026 Marcin Spoczynski. MIT licence, text in `licenses/LICENSE-OpenTracker-fx-MIT.txt`.
- The ladder error weights are the table `kLadderErr` of OpenTracker's `chip_dsp.h`,
  https://github.com/sandlbn/polyend-tracker-re (branch v3_fixes, commit 789d55a4c5e8). Apache License 2.0, text in
  `licenses/LICENSE-OpenTracker-Apache-2.0.txt`. The upstream project ships no NOTICE file.
- Modified: rewritten in C, first as an offline .ptfx effect (branch ptfx-ot), then as a stereo float live insert. The
  filter coefficients use the host's tanf and powf instead of the va_dsp.h approximations. The source file header lists the
  changes.

## dirt.plg (`fx_dirt_plug.cpp` with `../ff_dirt.cpp`, `../ff_dirt_ctl.cpp`)

- The firmware's built-in Dirt (a port of the DIRT module of opentracker-fx, `modules/dirt/src/dirt.cpp`, commit 6e5a7da,
  Copyright (c) 2026 Marcin Spoczynski, MIT, `licenses/LICENSE-OpenTracker-fx-MIT.txt`) behind the plugin wrapper.

## The SDK

`plug_abi.h`, `plug_rt.c` and `plug.ld` are linked into every plugin: MIT, `synth-open/plug/LICENSE`.
