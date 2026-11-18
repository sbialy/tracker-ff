# Static stack margin

`stack_margin.py` computes the stack margin of one firmware image and is the release gate on it (`make stack-margin-release`).
Stack room from the heap line (STACK_MARGIN of `mk20dx128.c` + the stack in use at the last `_sbrk` of `Profiler::init`), main loop
depth, audio out interrupt chain per synth plugin, every nested interrupt in both USB modes, the 108 B exception frame. The derivation,
the checks behind each term and the numbers per release are in the release notes of the project. It exits 1 when a plugin of the
release set is below the floor (`--min-margin`, 512 B) at the static bound or when one of its input tables no longer matches the image.

```
make original-release TOOLCHAIN=$PWD/agt/12.3.rel1/bin/arm-none-eabi-
make stack-margin-release TOOLCHAIN=$PWD/agt/12.3.rel1/bin/arm-none-eabi-      # build/stack-margin.txt, .json and the stamp
```

`make stack-margin` is the same on the image of the last `make original` with the same variables (SYNTH_OPEN=1 only, the walker rules
and the tables are those of the synth-open image). Packages with plugins (`package_release.sh --modules` or `--card`) refuse an image without `<image>.ptf.stack-margin` saying PASS for the sha256 of that exact .ptf (`STACK_GATE=skip` for a layout test).
About 50 s on a Mac. CI runs it next to the stack ratchet (`.github/workflows/build.yml`).

Files:

| File | Content | What fails the gate |
|---|---|---|
| `stack_depth.py` | the call graph walker (frames from the disassembly or the GCC `.su` files), `--payload`, `--recur`, `--dyn-list` | |
| `stack_resolve_fs.txt` | indirect calls of the firmware: virtual calls, ISR trampolines, the SD driver | an edge missing here understates a root, see the noteOff rule below. The first matching caller rule wins (plug::start sits above the FileSystem rule for that reason). Every caller regex must match a function of the image or the gate fails, a rule that may match nothing sits under a `# optional-rule` comment |
| `stack_isr_levels.txt` | every interrupt, its NVIC level and where it is set (flash vectors `vec=`, run time attached `ram`, disabled `off`), plus the source census (`src` rows) | a flash vector without a row, a row without a vector, a changed count of interrupt setup lines in a source file |
| `stack_vla_bounds.txt` | payload bytes of every dynamic frame (VLA), with the size expression and why it is bounded | a dynamic frame without a line, a line without a dynamic frame |
| `stack_recursion.txt` | levels of the recursive functions (`SdCard::delete_node`): `structural` (what the code allows) and `assumed` (what the gate counts, a waiver when smaller) | `--strict-recursion` gates on structural |
| `stack_plugin_depths.txt` | deepest stack of each synth plugin entry (measured 2026-11-16 with plug_depth.sh, the plugin ELFs are in the module repositories) | a plugin entry deeper than the printed plugin budget |
| `stack_resolve_plug.txt`, `plug_depth.sh` | plugin ELFs (module repositories run it for their own depths) | |

Two rules that are easy to get wrong. `OpenInstrument::noteOnNow`, `noteOff` and `releaseAll` call the engine's `noteOn` or `noteOff`
only. Without their rules (top of `stack_resolve_fs.txt`) the generic `OpenInstrument::` rule gives them every engine method including
`render`, and the main loop reads 3,232 B instead of 2,544 B. And the USB interrupt is level 7, or level 4 with the USB audio feature on
(`usb_dev.c:1289`): the gate takes the worse of both modes for every row.

Adding an interrupt, a VLA or a recursion is a review decision that needs its row, its bound and its reason in the same commit. The
census prints what to add.
