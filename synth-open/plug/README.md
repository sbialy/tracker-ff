# Runtime plugin spike (branch plugin-spike, local only, never pushed)

Design and results: `~/prj/poly/ref/plugin-architecture.md`.

## Pieces

- `plug_abi.h` ABI 1: 64 byte file header, image linked at 0, rebase table of R_ARM_ABS32 words, `plug_entry()` returning a
  `plug_desc` (params, instance size, init / reset / set_param / process / query), `plug_host` service table.
- `build_plug.py` builds a `.plg` from C/C++ sources (refuses static constructors and any non PC-relative relocation other
  than R_ARM_ABS32). `plug_rt.c` forwards libm to the host table.
- `fx_dirt_plug.cpp` the shipped Dirt kernel (`../ff_dirt.cpp`) as a master insert plugin. The same file is built into the
  image by `../plug_dirt_inimage.cpp` (bench reference, byte identical kernel).
- `emu_plug.py` the loader re-done in Python, the plugin's ARM code in Unicorn: header, CRC, rebase, descriptor checks,
  instruction count per block, output checksum, optional wav.
- Firmware: `../plug_host.cpp` (loader, host table, boot hook), `../plug_insert.cpp` (`AudioEffectPlug`, the live insert
  after Dirt, watchdog bypass), `../plug_bench.cpp` (boot bench).

## Build

    python3 synth-open/plug/build_plug.py --id ff.dirt --kind master --opt "-O2 -ffp-contract=off -fno-builtin" \
        --src-opt "ff_dirt_ctl.cpp=-Os -fno-builtin" \
        --out build/plugin-spike/dirt.plg -I synth-open synth-open/plug/fx_dirt_plug.cpp synth-open/ff_dirt.cpp synth-open/ff_dirt_ctl.cpp
    /tmp/phzvenv/bin/python -I synth-open/plug/emu_plug.py build/plugin-spike/dirt.plg --base 0x1FFF0BD0
    make original SYNTH_OPEN=1 TRIM=1 PLUG_HOST=1 PLUG_BENCH=1 SYNTH_TAG=-pb      # bench image (4 default engines)
    # the bench's row F through the emulator: the firmware's own objects linked as a .plg
    O=core/build-MK66-ORIGINAL-SYNTHOPEN-pb-trim/obj/synth-open
    python3 synth-open/plug/build_plug.py --id ff.dirt --kind master --entry plug_entry_dirt --out /tmp/inimage.plg \
        $O/plug_dirt_inimage.o $O/ff_dirt.o $O/ff_dirt_ctl.o

`ff_dirt_ctl.cpp` is built at `-Os` like the firmware object (`core/makefile:88`): at `-Os` GCC contracts
`1 + 15 d^2` into a `vfma`, at `-O2 -ffp-contract=off` it does not, and Dirt Soft's `Coef::pre` then differs in the last bit
(0x3f722222 against 0x3f722223). That was the whole F against L checksum gap of the first device bench. `emu_plug.py` is
bit exact with the device since 1b (it computes the bench input with the same float32 roundings and fused operations).
    make original SYNTH_OPEN=1 TRIM=1 PLUG_HOST=1 SYNTH_TAG=-ph                   # host only, flash cost

## Device test (the user flashes, nothing here flashes)

1. Flash `build/plugin-spike/PolyendTracker_PLUGB_1.1.0.ptf`.
2. SD card: copy `dirt.plg` to `/plugbench.plg` (bench) and to `/plugins/master.plg` (live insert, Dirt Soft always on).
   Optional `/plugbench.nvm` (empty file) adds the FlexNVM row: the plugin is programmed into the last two 4 KB sectors of the
   data flash (only if they read blank) and erased again. Optional `/plugbench.alias` (empty file) adds the SDRAM code bus
   alias row: it may hang the boot, then delete the file.
3. Boot, play a song, listen (the master bus is driven by the loaded Dirt). Config screen CPU readout needs `/.develop`.
4. Bring back `/plugbench.txt` and `/plugins/master.txt`. Every row's two chk values must equal row F's and
   `emu_plug.py`'s checksum of the same `.plg`, the `inst` lines of every row must equal row F's.
