# ui_shot: screenshots of the original Tracker UI on the Mac

The firmware's own UI code (core/ and synth-open/ of this tree, the PLUG_HOST configuration) is built for the host with clang and
draws through a model of the FT812 instead of the chip. The result is a PNG of what the 800x480 screen shows. Host only: nothing in
this directory is part of a firmware image, nothing under core/ or synth-open/ outside it was changed.

    synth-open/tools/ui_shot/ui_shot.sh inserts            # Master Inserts page, Dirt + BBD (state "running")
    synth-open/tools/ui_shot/ui_shot.sh inserts safe       # the same project powered up with Shift: slots say "Safe: Dirt"
    synth-open/tools/ui_shot/ui_shot.sh engines            # synth engine picker with the card logos
    synth-open/tools/ui_shot/ui_shot.sh all                # every page below, checks each PNG opens
    synth-open/tools/ui_shot/ui_shot.sh list

PNGs go to `$OUT_DIR` (default `~/prj/poly/build/ui-shot/`), build products and the staged SD cards to `build/ui-shot/` of the
worktree. Pages and states: `pattern master master:inserts song config performance sample-play instrument inserts:empty
inserts:running inserts:bbd inserts:dirt2 inserts:safe inserts:safe-mixed engines engines:library synth-params synth-edit
synth-edit:filter synth-macros automation automation:lfo`. A page is a script for `ui_shot` (see `script_for` in ui_shot.sh and the
command list in ui_shot_main.cpp): button taps, encoder turns, waits, `shot`. Needs clang++, make, python3 and the plugin set in
`$PLG_DIR` (default the newest of `~/prj/poly/build/cpu-adopt`, `plugin-synth`) for the plugin pages.

## What is real and what is not

Real: the display code (`core/libs/NewDisplay`, every control), all pages (`core/interface`), the synth GUI, the plugin host and its
Inserts page (`synth-open/plug_*.cpp`), the fonts and bitmaps in the image, the project and file manager code, the plugin index, the
sidecar reader, the loader's header, CRC and rebase checks. They run on a Platform made of host stand-ins (`host_platform.cpp`:
virtual clock, an SD card that is a folder, no audio, no buttons hardware).

Not real:
- **FT812**: `eve_model.cpp` executes the 32 bit words the firmware's `API_*` layer sends (it implements `DisplayDriver`): RAM_G, the
  display list in RAM_DL, CMD_MEMCPY / CMD_APPEND of the stored control lists, CMD_SWAP renders. A software rasteriser draws it.
- **Master insert plugins** are Thumb code. Their own sources are compiled natively (`host_twins.cpp`, `host_dirt.cpp`, the Makefile),
  and the loader asks for the descriptor of the twin where it would jump into the file. They show real names, parameters, ranges and
  values, but no audio runs. The file is still read and CRC checked by the real loader.
- **Synth engine plugins** are listed with their card logos, but no engine installs (the host has no FlexNVM bank, `synthEnsure` is
  cut short): a part that picks one says `Missing: <name>`. The built-in engines run as built.
- Audio, USB, MIDI, battery, radio, touch, JPEG decode (CMD_LOADIMAGE data is dropped), the ROM fonts.

## Build and overlay

`make -C synth-open/tools/ui_shot` builds `build/ui-shot/ui_shot` (about 20 s on 8 cores, `OPT=-O0` for a debugger). The firmware
needs a few edits to compile for macOS arm64. They are not made in the tree: `gen_overlay.py` writes edited copies into
`build/ui-shot/gen` on every make (only changed files are rewritten) and the Makefile compiles those instead of the originals. Each
edit has its reason in that file, and an edit that no longer matches the source fails the build:

- ELF `section(...)` attributes dropped (config.h, cr_section_macros.h, plug_abi.h, a few sources), the CPSID/CPSIE interrupt macros
  made empty;
- `libs/Audio/utility/dspinst.h` and `Polyverb/util.h`: the Cortex-M DSP instructions get the C bodies Teensy keeps for the M0
  (the whole `libs/Audio` tree is copied, because `"utility/dspinst.h"` is found next to the includer before any `-I`);
- 64 bit host: pointer differences through `uintptr_t` (mtConfig, mtAudioEngine, FileManager recovery), `strchr` constness (textBox),
  `case SR->X` labels (fSampleRec), the `static_assert` on 32 bit struct sizes is switched off for the whole build
  (`-Dstatic_assert(...)=`: struct sizes here are the host's, not the firmware's 32 bit ones, nothing in the UI path depends on them);
- plug_host.cpp: the K66 core registers (cache control, cycle counter, MPU) point at a variable, and the plugin descriptor comes from
  the twin; plug_synth.cpp: no install. `plug_nvm.cpp` and `Profiler.cpp` are replaced by stubs (`host_stubs.cpp`).

`samplePlayback.editorInstrument` is seeded by `ui_shot_main.cpp`: initDisplayControls() reads it before start() sets it, harmless on
the Cortex-M (address 0 is flash) and a fault on the Mac. That is a latent firmware oddity, left alone.

## FT812 inventory (call sites outside the library, this tree)

Resolution is 800x480, not 480x272 (`doScreenShot` in `libs/NewDisplay/snapshots.cpp` writes an 800x480 32 bit BMP,
`Snapshots/Snapshot_NNN.bmp`, from CMD_SNAPSHOT2 ARGB8 of RAM_G; the screen is scanned out rotated 180 degrees by REG_ROTATE, the
model draws unrotated like the snapshot).

- Display list: VERTEX2F 510, COLOR 251, BEGIN 172 (RECTS 70, BITMAPS 37, LINES 31, LINE_STRIP 31, FTPOINTS 3), END, LINE_WIDTH 123,
  SAVE/RESTORE_CONTEXT 60, BLEND_FUNC 58 (SRC_ALPHA with ONE_MINUS_SRC_ALPHA, ZERO or ONE), BITMAP_HANDLE/SOURCE/LAYOUT/SIZE,
  CELL, SCISSOR_XY/SIZE 28, CLEAR_COLOR/CLEAR, VERTEX2II 11, DISPLAY, VERTEX_FORMAT(0), POINT_SIZE, BITMAP_TRANSFORM_A/E, COLOR_A.
- Coprocessor: CMD_TEXT 59, CMD_GRADIENT 26, CMD_NUMBER 8, CMD_APPEND 8, CMD_SETBITMAP 6, CMD_MEMCPY 6, CMD_LOADIDENTITY /
  TRANSLATE / ROTATE / SETMATRIX (the vertical track names, a glyph at a time), CMD_SETFONT2 (the fonts), CMD_SETBASE, CMD_LOADIMAGE 3
  (JPEG: image viewer, project cover), CMD_SNAPSHOT2, CMD_MEMZERO, CMD_GETPROPS, CMD_DLSTART, CMD_SWAP. No widgets (button, slider,
  gauge, toggle, ...).
- Fonts: custom, L4 bitmap fonts (`libs/fonts`, Roboto Mono and Liberation Sans) loaded into RAM_G at boot with CMD_SETFONT2. The only
  ROM font uses are font 29 in `simpleTextControl.cpp` (the "no SD card" screen) and font 28 in `mtTest.cpp` (the factory test).
- Bitmaps: L4 (fonts), L2 (engine logos, plugin card logos), L8 (Polyend logo), ARGB4 / RGB565 / PALETTED4444 (sample and cover images).
- The firmware draws a glyph with `CELL = character code` and `VERTEX2II`, so SETFONT2 must make the handle's source the address of
  glyph 0. The model does that when the display list runs.

## Why an own rasteriser

The command set above is small and every control goes through `DisplayDriver`, so the model is about 660 lines. Bridgetek's emulator
(ft8xxemu / BT8XXEMU) is distributed as a Windows library (AN_281 names ft8xxemu.dll and SDL2.dll). A macOS or Linux build and the licence
terms could not be confirmed from here, so it was not used. It would give the exact coprocessor and edge anti-aliasing, which is the
one thing the own model approximates. The ROM fonts (FT81x ROM, in the chip, not redistributable as such) are not in the host build.
CMD_TEXT / CMD_NUMBER on a font without CMD_SETFONT2 draws with custom font 1 and is counted (`dump` prints `romText`). `ref/ot3`
has a DejaVu based stand-in set (Apache 2.0 project, Bitstream Vera for the glyphs) that was not used: it is an approximation as well.

## Fidelity

There is no screenshot of the original firmware on this machine to compare against (searched `~/prj`): the only device images are
`ref/opentracker/screenshots`, OpenTracker's own firmware (another UI, another theme, 800x480). Against those:

- layout of the shared chrome matches to the pixel: the top bar title and clock position, the bottom bar's seven separators at
  x = 100, 200 ... 700 and its label text, the row of function labels, the Roboto Mono glyph shapes and advance widths (they come from
  the firmware's own font blocks);
- not equal: the colours (the default theme here, OpenTracker draws its grey one), line and edge softness (`png_cmp.py` on
  pattern.png shows 1 px separators there and a 2 px half coverage pair here: the model puts a line at an integer coordinate on a pixel
  boundary and covers two pixels at 50 %, which is how OpenTracker's own notes describe the chip, but it was not verified on a unit).

Titles that look cut ("Synth Automation 3/3 - Slo", "Safe mode: Shift at power o") are the firmware's: the title label is 270 px wide
(`core/interface/core/titleBar.cpp`) and clips its text.

Known differences to the chip: edge anti-aliasing (signed distance at the pixel centre instead of the 16 sample pattern), the
gradient (linear in sRGB values, the coprocessor's own ramp is not modelled), bilinear filtering and palettes are absent, ROM fonts,
and the host starts with an empty project and no theme or font files on the card.
