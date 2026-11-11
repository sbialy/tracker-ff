# Static stack margin

`stack_margin.py` computes the stack margin of one firmware image: stack room from the heap line, main loop depth, audio out
interrupt chain per synth plugin, every nested interrupt, the 108 B exception frame. The derivation, the checks behind each
term and the numbers per release are in `ref/stack-margin.md`. The tool exits 1 when a plugin of the release set is below
`--min-margin` at the static bound.

```
export TOOLCHAIN=$PWD/agt/12.3.rel1/bin/arm-none-eabi-
plug_depth.sh DIR_WITH_PLUGIN_ELFS > depths.txt          # the module repositories keep the ELFs (build_plug.py --keep)
python3 -I stack_margin.py build/<image dir> --plugin-depths depths.txt
```

Files: `stack_depth.py` (the call graph walker, frames from the disassembly or GCC `.su` files), `stack_resolve_fs.txt` (indirect
calls of the firmware: virtual calls, ISR trampolines, the SD driver), `stack_resolve_plug.txt` (plugin ELFs).

A rule that is easy to get wrong: `OpenInstrument::noteOnNow` calls `engine->noteOn` only. Without its rule (first line of
`stack_resolve_fs.txt`) the generic `OpenInstrument::` rule gives it every engine `render` and the main loop reads 3,200 B instead
of 2,544 B.
