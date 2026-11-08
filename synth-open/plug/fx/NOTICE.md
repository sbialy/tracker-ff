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

## bbd.plg (`fx/ot_bbd_plug.c`)

- Port of the BBDC master insert of opentracker-fx, https://github.com/opentracker-fw/opentracker-fx, `modules/bbd/src/bbdc.cpp`,
  commit 23d58b2 (the commit that added the module is 6e5a7da). Copyright (c) 2026 Marcin Spoczynski. MIT licence, text in
  `licenses/LICENSE-OpenTracker-fx-MIT.txt`.
- The chorus DSP is `audio/bbd_chorus.h` of OpenTracker 3.x, https://github.com/sandlbn/polyend-tracker-re (branch v3_fixes,
  commit 789d55a4c5e8), Apache License 2.0, text in `licenses/LICENSE-OpenTracker-Apache-2.0.txt`, copy in
  `synth-open/ot/audio/bbd_chorus.h`. The upstream project ships no NOTICE file.
- Modified: rewritten in C as a stereo float live insert, the delay line inside the plugin instance, the Tone coefficient built
  in `set_param`, the mode a parameter. The source file header lists the changes. Checked against the upstream header: the
  output is within 1 LSB of `bbd::process` on int16 input (Mode I and II, Mix, Width and Tone moved).

## phaser.plg (`fx/ot_phaser_plug.c`)

- Port of the PHSR module of opentracker-fx, https://github.com/opentracker-fw/opentracker-fx, `modules/phaser/src/phaser.cpp`,
  commit 7899366 (tree 23d58b2). Copyright (c) 2026 Marcin Spoczynski. MIT licence, text in
  `licenses/LICENSE-OpenTracker-fx-MIT.txt`.
- The Pade tangent and the polynomial 2^x are `tanPade` and `fastExp2` of `audio/va_dsp.h` of OpenTracker 3.x,
  https://github.com/sandlbn/polyend-tracker-re (branch v3_fixes, commit 789d55a4c5e8), Apache License 2.0, text in
  `licenses/LICENSE-OpenTracker-Apache-2.0.txt`, copy in `synth-open/ot/audio/va_dsp.h`.
- Modified: rewritten in C as a stereo master insert (upstream is a synth slot insert), the settings are plugin parameters,
  plain multiply and add instead of fused multiply add, Mix 0 passes the bus untouched. The source file header lists the
  changes. Checked against the upstream source: the output is within 1 LSB of `PhaserEffect::process` on int16 input in
  Classic and 80s, with Rate, Depth, Feedback, Spread and Mix moved.

## dirt.plg (`fx_dirt_plug.cpp` with `../ff_dirt.cpp`, `../ff_dirt_ctl.cpp`)

- The firmware's built-in Dirt (a port of the DIRT module of opentracker-fx, `modules/dirt/src/dirt.cpp`, commit 6e5a7da,
  Copyright (c) 2026 Marcin Spoczynski, MIT, `licenses/LICENSE-OpenTracker-fx-MIT.txt`) behind the plugin wrapper.

## The SDK

`plug_abi.h`, `plug_rt.c` and `plug.ld` are linked into every plugin: MIT, `synth-open/plug/LICENSE`.
