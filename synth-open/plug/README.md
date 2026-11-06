# Tracker FF plugin SDK (`.plg`, ABI 1.0)

Runtime plugins for tracker-ff on the original Polyend Tracker (MK66, Cortex-M4F). Phase 2 of the design
(`~/prj/poly/ref/plugin-architecture.md`) hosts **master inserts**: two slots after the tracks and sends mixer (and the
built-in Dirt of an `FF_DIRT=1` build, the PLUG_HOST default is `FF_DIRT=0`: `dirt.plg` replaces it), before the stock
limiters. Engines, send FX, track FX and offline effects use the same file format in later
phases.

Licence: the SDK files in this folder (`plug_abi.h`, `plug_rt.c`, `plug.ld`, `build_plug.py`, `emu_plug.py`,
`build_plugs.sh`, this README) are MIT (`LICENSE` here). A plugin may use any licence that allows shipping it as a separate
file. The firmware licence is unchanged. The plugins in `fx/` and `fx_dirt_plug.cpp` are ports of MIT and Apache-2.0
OpenTracker code, except Console and Tube, which are original. See `fx/NOTICE.md`.

## Files

| File | What |
|---|---|
| `plug_abi.h` | The ABI: file header, manifest, descriptor, parameters, host table, extension ids, `PLUG_HAS`, `PLUG_DESCRIPTOR`, `PLUG_ENTRY` |
| `plug_rt.c` | Linked into every plugin: `sinf cosf tanf tanhf expf logf powf exp2f` forward to the host table, `sqrtf` is one `vsqrt`, local `memcpy` and `memset` loops |
| `plug.ld` | Link at address 0: entry first, then code, the descriptor (`__plug_desc`), constants, then the writable segment |
| `build_plug.py` | Compile, link, check relocations, write header + manifest + image + rebase table |
| `emu_plug.py` | The host loader re-done in Python, the plugin's ARM code in Unicorn: checks, instructions per block, output checksum, wav |
| `build_plugs.sh` | Builds and checks the shipped plugins (dirt, tape, lofi, console, tube, bbd, phaser) |
| `fx_dirt_plug.cpp`, `fx/` | The shipped plugins |

## Shipped plugins

Seven master inserts. File sizes are the `.plg` bytes. Every image is text only (no writable data) plus 8 B of bss, far
below the 8,192 B limit, so each runs from either insert. The instance (state, the BBD line) is allocated by the host from the
SRAM heap and counts against neither limit. Emulated instructions are per stereo block of 128 frames (a lower bound, the
device measured 1.5 times that for Dirt). Console, Tube, BBD and Phaser ramp every setting over the block, so a change
never clicks.

| Plugin | File | Parameters (range, default) | File size, instance | Emulated insns per block |
|---|---|---|---|---|
| Dirt | `dirt.plg` | Type (Soft Hard Fold Asym, Soft), Drive 0-100 % (35), Bits 4-16 (16), Rate 1-32 (1), Tone 0-100 (100 = off), Mix 0-100 % (100) | 3,116 B, 680 B | 13.8 k |
| Tape | `tape.plg` | Drive 0-100 % (30), Wow 0-100 % (20), Flutter 0-100 % (10), Mix 0-100 % (100) | 1,692 B, 1,072 B | 16.3 k |
| Lo-Fi | `lofi.plg` | Bits, Rate, Ladder, Speaker, Mix | 2,668 B, 388 B | 18.4 k |
| Console | `console.plg` | Drive 0-100 % (25), Weight 0-100 % (30), Output -12 to +12 dB (0), Mix 0-100 % (100) | 1,508 B, 80 B | 8.8 k |
| Tube | `tube.plg` | Drive 0-100 % (25), Warmth 0-100 % (30), Tone 0-100 (80, 100 = off), Mix 0-100 % (100) | 1,496 B, 80 B | 9.7 k |
| BBD | `bbd.plg` | Mode (I II, I), Mix 0-100 % (100), Width 0-100 % (100), Tone 0-100 % (50) | 2,128 B, 568 B | 11.8 k |
| Phaser | `phaser.plg` | Mode (Classic 80s, Classic), Rate 0-100 (40), Depth 0-100 % (80), Feedback 0-100 % (30), Spread 0-100 % (25), Mix 0-100 % (50) | 4,792 B, 148 B | 7.8 k Classic, 11.9 k 80s, 14.0 k while a ramp runs |

- **Dirt**: the OpenTracker DIRT port, the same kernel as before. The Bits quantiser is mid-tread (half a step added with saturation, then the low bits cleared), so zero in is exactly zero out at every Bits setting and an input smaller than half a step (a line-in noise floor, a reverb tail) is silent. Checked by `dirt_zero_check.py`.
- **Console**: glue saturation. A rational tanh curve (small signal gain exactly 1 at every Drive, curve gain 0.05 to 3.5), with
  the lows pushed harder: Weight lifts everything below about 120 Hz by up to +12 dB before the curve and takes the same
  shelf out after it, so a signal too small to bend the curve passes unchanged. Drive 0 is transparent, Mix 0 is the dry
  signal exactly. Output trims the result (the curve lowers loud peaks, so a hot Drive wants a few dB back).
- **Tube**: a triode style stage. The signal rides on a bias point of the curve (Warmth), so one half-wave compresses sooner
  than the other and the added harmonics are mostly even (2nd and 4th, warm). The small signal gain is 1 and DC is blocked
  (6 Hz). Tone is a one pole high cut from 2 kHz (0) to 20 kHz (99), 100 is off. Measured at 1 kHz and -9 dBFS with the
  defaults: 2nd harmonic -29 dB, 3rd -46 dB.
- **BBD**: the classic two mode chorus of OpenTracker. Mode I is 0.5 Hz and 2.0 to 5.0 ms, Mode II 0.8 Hz and 1.5 to
  5.5 ms. Mix 100 is dry and wet equal as on the original unit. Width 0 puts both taps on both sides, 100 one tap per
  side. Tone is the BBD filter cutoff from 6 kHz (0) to 12 kHz (100). The line is 256 frames of int16 (512 B) in the
  instance, so no SDRAM is used.
- **Phaser**: Classic is 4 stages from 140 Hz to 2.4 kHz with feedback up to 60 %, 80s is 8 stages from 90 Hz to 4.2 kHz with
  feedback up to 80 %. Rate is 0.05 Hz (0) to 6.4 Hz (100) on a log scale, 40 is 0.38 Hz. Depth is how far up the sweep goes,
  Spread offsets the right channel's LFO by up to half a cycle. Mix 0 passes the bus untouched.

## File format

Little endian. All sizes in bytes.

```
offset 0     plug_file_header (64)       magic "PLG1", header_size, abi 1.0, kind, cpu, flags, uid, version, min_host,
                                         image_size, bss_size, rebase_count, entry_offset, ram_bytes, cpu_cycles,
                                         crc32, rw_offset, 8 reserved
offset 64    plug_manifest (96)          struct_size, name[16], author[32], licence[16], num_params, role, reserved
header_size  image (image_size)          [0, rw_offset) text: code, constants, the descriptor (may run from flash)
                                         [rw_offset, image_size) rw: initialised data (always RAM), then bss_size zeroed
             rebase table                rebase_count x u32: image offsets of the words that hold an image address
```

- `crc32` is CRC-32 (IEEE, zlib's) of every byte after the first 64: manifest, image and table.
- The image is linked at address 0. The loader adds the run address of the segment a word points into (text or rw) to every
  word the table names. Only `R_ARM_ABS32` is relocated. PC-relative branches and loads are fine inside one segment. No GOT,
  no PIC, no ELF on the device.
- The manifest is readable without running code: the host lists plugins from it. `build_plug.py` copies name, author and
  parameter count from the descriptor marked `PLUG_DESCRIPTOR`.
- `uid` is FNV-1a 32 of a reverse DNS id (`--id org.example.fx.chorus`). Projects store the uid, never a file name: keep the
  id when you ship a new version.
- `version` is `major << 16 | minor << 8 | patch`. Two files with the same uid on the card: the higher version is used.
- `kind`: 1 master insert (the only kind phase 2 hosts). `cpu`: 1 Cortex-M4F. `min_host`: 1.
- `flags` bit 0 `PLUG_F_S16` (int16 I/O for bit identical ports of the stock FX) is reserved: the phase 2 host refuses it.
- `role` (manifest): 0 insert. 1 reverb and 2 delay are reserved for the override slots of phase 5b.

## Compatibility rules

- `abi_major` must equal the host's (1). `abi_minor` and the host level only grow.
- Every struct that crosses the boundary starts with `struct_size`. Read a field only when it lies inside the other side's
  size: `PLUG_HAS(host, plug_host, query)`. New fields go at the end, nothing is reordered or removed.
- Optional features are queries: `desc->query(PLUG_EXT_*)` and `host->query(...)` return NULL or a struct with its own
  `struct_size`. `PLUG_EXT_TAIL` (`tail_frames`, `quiet`, `skip`) is declared for phase 5b, the phase 2 host does not ask.
- A host skips a kind, cpu or role it does not know, and refuses a plugin whose `min_host` is above its level, before any
  code runs.

## Writing a master insert

```c
#include "plug_abi.h"
extern const plug_host* plug_rt_host;            // plug_rt.c, set in plug_entry

typedef struct { float gain; } Inst;             // ALL state lives in the instance, never in globals

static const plug_param kParams[1] = {
    {"Gain", "%", 0.0f, 200.0f, 100.0f, 1.0f, 0, 0, 0},   // name unit min max default step enum_count flags enum_names
};
static void set_param(void* p, uint32_t i, float v) { if (i == 0) ((Inst*)p)->gain = v * 0.01f; }
static void reset(void* p) { (void)p; }
static int32_t init(void* p, const plug_host* h) { (void)h; set_param(p, 0, kParams[0].def); return 0; }
static void process(void* p, float* const* ch, uint32_t channels, uint32_t frames)
{
    const Inst* s = (const Inst*)p;
    for (uint32_t c = 0; c < channels; c++)
        for (uint32_t i = 0; i < frames; i++) ch[c][i] *= s->gain;
}
PLUG_DESCRIPTOR static const plug_desc kDesc = {
    sizeof(plug_desc), PLUG_KIND_MASTER_INSERT, "Gain", "me", 1, kParams, sizeof(Inst),
    init, reset, set_param, process, 0,
};
PLUG_ENTRY const plug_desc* plug_entry(const plug_host* host, uint32_t abi)
{
    if ((abi >> 16) != PLUG_ABI_MAJOR || !host) return 0;
    plug_rt_host = host;
    return &kDesc;
}
```

Contract:

- `process` gets 2 channels of 128 floats, full scale +-1.0, in place. It runs in the audio interrupt: no allocation, no
  file access, no waiting. It is not called while the master bus is silent. The host converts to int16 after the second
  slot with a clamp, so a plugin may overshoot without wrapping.
- `init` runs on the main thread before the first block, with the instance zeroed (`instance_size` bytes, 8 aligned, at
  most 16 KB). Leave every parameter at its default: the host then calls `set_param` for each value the project changes.
  `host->alloc(bytes, 0)` gives SRAM heap, `alloc(bytes, 1)` gives SDRAM (32 KB per slot), both only inside `init`, freed
  with the instance.
- `set_param` runs in the audio interrupt at a block start, only the latest value of each parameter is delivered. Values
  are floats in the parameter's units, enums are indices `0 .. enum_count - 1`. The host rounds to `step` and clamps.
- At most 32 parameters. Names of 11 characters or less show in full on the Inserts page. Values are stored in the project
  by name, so renaming a parameter loses its stored value and reordering is harmless.
- CPU: a slot that takes more than 60,000 cycles (11.5 % of a 522,240 cycle block) three blocks in a row is bypassed until
  it is loaded again. `emu_plug.py` gives instructions per block, a lower bound: the device measured 1.5 times the emulated
  count for Dirt (design section 11). Add the host's conversion pair (6,496 cycles measured, shared by the two slots).
- Size: insert 1 holds image + bss up to 8,192 B in SRAM_L. Insert 2 holds text up to 8,192 B in the FlexNVM data flash,
  its rw and bss go to a fixed 1 KB SRAM buffer (more is refused for insert 2), so its text is the
  same bytes on every load and a reload costs no flash command.
- No static constructors, no exceptions, no RTTI, no per instance state in globals. Constant tables of pointers are fine
  (they are rebased).

## Building

```
TOOLCHAIN=/path/to/agt/10.3/bin/arm-none-eabi- python3 build_plug.py --id org.example.fx.gain --kind master \
    --version 1.0.0 --opt "-O2 -ffp-contract=off" --out gain.plg gain.c
/tmp/phzvenv/bin/python -I emu_plug.py gain.plg --base 0x1FFF0BD0 --param 0=150 --wav gain.wav
```

`--param-at BLOCK:INDEX=VALUE` sets a parameter just before that block, to test ramps and crossfades (a plugin that treats the first block differently from the later ones only shows it there).

- `dirt.plg` is a worked example of a plugin that rebuilds coefficients in `set_param` (powf and tanf through the host table, once per change, never in `process`) and fades from the old setting to the new one over the next block: it keeps the pre-change coefficients and state, runs the block twice and crossfades. Mix is a wet ramp. Its parameters are those of the OpenTracker DIRT: Type (Soft Hard Fold Asym), Drive 0-100 %, Bits 4-16, Rate 1-32, Tone 0-100 (200 Hz to 20 kHz, 100 = off), Mix 0-100 %.
- Use the pinned GNU Arm 10.3-2021.10 compiler. `-ffp-contract=off` is recommended for new plugins: a fused multiply add
  costs 3 cycles on the M4F against 2 for multiply then add.
- **Ports of firmware DSP:** two builds of one source are bit identical only with the same FP contraction. Build each object
  with the flags the firmware object uses (`--src-opt NAME=FLAGS`). Dirt's `ff_dirt_ctl.cpp` is `-Os` in the image, which
  fuses `1 + 15 d^2` into a `vfma`. Built at `-O2 -ffp-contract=off` its `Coef::pre` differs by one ulp and the output hash
  changes (design section 11, phase 1b).
- `build_plug.py` refuses static constructors, GOT access, MOVW/MOVT absolute pairs and PC-relative references between the
  text and the rw segment, and prints image, rw, bss and rebase counts.
- `build_plugs.sh OUT [FW_OBJ]` builds `dirt.plg`, `tape.plg`, `lofi.plg`, `console.plg`, `tube.plg`, `bbd.plg` and `phaser.plg` and
  checks them: no `R_ARM_GOT*`, the rebase table equals the `R_ARM_ABS32` count, the same output checksum at the SRAM_L,
  FlexNVM plus heap and SDRAM homes, with the default settings and again with every parameter moved and one more change in
  block 5 (the checksum must differ from the default one), Dirt
  `3b1f3dc5` (the device's in-image Dirt hash), also from the firmware's own `ff_dirt.o` and `ff_dirt_ctl.o` when FW_OBJ is
  given (a PLUG_HOST build still compiles them with `FF_DIRT=0`, the linker drops them from the image), and Dirt entering
  Asym from Soft, Hard and Fold leaves no DC step (`dirt_asym_check.py`, mean over whole periods under 0.01 of full scale).
  The Dirt wrapper follows Asym's DC block while another type plays (as opentracker-fx `cbedea9`): without it the mean
  after a switch to Asym reached 0.156 at Drive 100 and drained over the 10 Hz block, a thump.

## On the device (phase 2 host, `make original SYNTH_OPEN=1 TRIM=1 PLUG_HOST=1`)

- Copy `.plg` files to `/Plugins/Master/` on the SD card. The host reads only their headers at boot and when the Inserts
  page opens.
- Master screen, Saturation list: Hard Clip, Soft Clip, Saturate, Overdrive, Drive+, Soft Knee (new: bit exact below
  -3 dBFS, a smooth curve into full scale above, `synth-open/sat_softknee.h`), then one `Inserts` entry (when a plugin is
  on the card or in the project). The five built-in Dirt entries are gone from this build (`FF_DIRT=0`): a project saved
  with one opens as Inserts with insert 1 = Dirt at that entry's values (Dirt Crush: Drive 15, Bits 8, Rate 4, Tone 70)
  when `dirt.plg` is on the card, else as Hard Clip, and `/Plugins/host.txt` line 3 says which. Inserts plays both insert
  slots on top of the mixer stage picked on the Inserts page (Hard Clip unless changed), and the Saturation value names what
  is loaded in 9 characters: `Inserts` (both empty), `Ins: Tape`, `Ins:Lo-Fi`, `Tape+Dirt`, `Lo-F+Tape`. A built-in
  entry bypasses both inserts and keeps them (loaded, values kept, saved in the sidecar), so Inserts again brings both
  back. Plugins are picked on the Inserts page only.
- Shift + Saturation opens the Master Inserts page: column 1 `Mixer` is the stage of the tracks and sends mixers before
  the inserts (Hard Clip, Soft Clip or Soft Knee, Delete: Hard Clip), columns 2 and 3 pick the plugin of insert 1 and
  insert 2 (encoder or Up and Down, Delete empties), columns 4 to 7 are the parameters of the focused insert (`*` marks
  it), four at a time, each further press on the insert's button shows the next four. The last column goes back. The
  plugin columns (`None` and every plugin on the card), the Mixer column and every parameter with `enum_count > 0` show
  all their entries in the stock list control with the
  current one highlighted, numeric parameters show their value and a bar. The value line of an insert column is at most 9 characters:
  the plugin name, `None`, `Loading`, `Bypassed`, or a tag and as much of the name as fits (`Safe:BBD`, `Miss:Lo-Fi`,
  `Fail:Dirt`, `CPU:Phase`). Picking a plugin while a built-in Saturation
  entry is on switches the Saturation to Inserts. With `/.develop` on the card the title shows the last block's cycles of
  each insert in percent.
- Insert 1 runs from the 8 KB SRAM_L slot. Insert 2 runs its text from the FlexNVM data flash at 0x1001E000 (two 4 KB
  sectors, erased and programmed when a different plugin is loaded there, no flash command when the bytes are already
  there) and its data from a fixed 1 KB SRAM buffer.
- The project stores the inserts in `plugins.txt` next to the project file (`tffplug 1`, `format 2`, `mixer <n>` when the
  stage is not Hard Clip (1 Soft Clip, 10 Soft Knee), `slot <n> <uid> <version> <name>`, `p <param>=<value>`). The binary
  project only holds the reserved saturationType `0x80` for the Inserts entry, which stock firmware loads as Hard Clip
  whatever the mixer line says, and the built-in Soft Knee as `10`, which stock firmware and FF 1.1.0 load as Hard Clip. A
  file without the `mixer` line (every older host) runs Inserts on Hard Clip. A file without the `format` line (hosts
  before the Inserts entry) still loads: with Hard Clip in the project its inserts come back on. A project without plugins
  and with a Hard Clip stage has no such file and stays what stock firmware writes. A plugin that is missing from the card shows as `Miss:<name>`, the audio passes and its values are
  kept and saved back.
- Safety: before a plugin starts, the host writes `/Plugins/.loading` with its name and deletes it after 3 s of running.
  If the Tracker died in between, the next boot loads no plugin on its own and the Inserts page title says which one
  crashed. Holding Shift during the first 4 s after power on does the same. Picking a plugin by hand still works in both.

## Synth engine plugins (phase 3, ABI 1.1)

A `PLUG_KIND_SYNTH` plugin answers `query(PLUG_EXT_SYNTH)` with a `plug_ext_synth` (plug_abi.h): the engine's numbers
(`stored` for projects, `patch_engine` for `.patch` field 1, `type_id`), its name (on screen and the `Patches/<name>/`
folder), parameter, section and macro tables in the shim's `ParamDef` / `SectionDef` / `MacroDef` layout, and the
`ISynthEngine` calls as C functions. An engine that used to be built in keeps its numbers (JU6 7 / 0x81, FM4 9 / 0x80), so
old projects and patches find it. `synth_plug.h` turns an `open::EngineDef` and its `ISynthEngine` into a plugin in three
lines (`synth/ju6_plug.cpp`). Large working RAM comes from the host (`work_bytes`, `set_work`), not from bss.

Files go to `/Plugins/Synth/`. At boot the host installs them into the FlexNVM bank (`synth-open/plug_bank.h`, index
`/Plugins/installed.txt`, status `/Plugins/synth.txt`) and adds them to the engine list before the synth starts. Build the
engine sources with the firmware's own flags (`build_plugs.sh`, the fused vfma default of `-Os`), and prove bit identity with
`synth_render_compare.sh` (the firmware's objects behind the same wrapper, rendered by `emu_synth.py`).
