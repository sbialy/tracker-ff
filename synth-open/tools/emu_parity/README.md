# emu_parity: measurements behind ref/emu-parity-plan.md

Mac tools only. Nothing here is compiled into the firmware image or into ui_shot (ui_shot compiles `tools/ui_shot/*.cpp` only).

- `ucbridge_proto.cpp`: a REAL release synth .plg run under libunicorn from native C++, as a ui_shot bridge would do it. The plug::load steps
  (header, CRC, logo trailer, rebase) put the text into a host array mapped at the FlexNVM bank address 0x10000000 with `uc_mem_map_ptr`
  (read and execute only), rw and bss in SRAM, the work arena in SDRAM. Driven by poly-engines `tools/render_harness.cpp`, so its output
  can be compared byte for byte with `emu_render.py`. Also prints the cost of one empty `uc_emu_start` round trip.
- `bench_synth.py`: wall time and instruction count of one 128 sample block, 1 and 4 voices, every synth .plg of the card (`SET=max`: every
  parameter at its max). The per-instruction cost hook of emu_render.py is dropped for the wall time.
- `bench_insert.py`: the same for the master inserts (defaults and every parameter at max).
- `ref_out.py`: the emu_render.py reference render for the bridge comparison.
- `libm_check.py`: the device's own host services (newlib sinf ... log2f, run as Thumb code from the release axf through the image's
  `plug::s_host` table) against the double rounded libm the Unicorn tools answer with and against the host libm a native twin calls.
- `worst.py`: the worst case table (8 voices extrapolated, 3 parts, 2 inserts) from the bench JSON.
- `run.sh OUTDIR`: all of the above (see its header for the variables).
