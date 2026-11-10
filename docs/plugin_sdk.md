# Tracker FF Runtime Plugin SDK (`.plg`) - Developer Guide

The **Tracker FF runtime plugin SDK** ([`synth-open/plug/`](../synth-open/plug/)) lets developers write **synth engines** and **Master insert effects** as separate `.plg` files that the **tracker-ff** firmware loads from the SD card while it runs. It targets only the **Original Polyend Tracker** (`NXP Kinetis MK66`, ARM Cortex-M4F @ 180 MHz). Tracker+ and Tracker Mini are not supported by the `.plg` host.

Nothing here needs the firmware to be rebuilt. A plugin is compiled and linked on its own with the pinned GNU Arm toolchain (a synth plugin includes headers from this repository), copied to the card and picked in the UI.

---

## Table of Contents

1. [What a Plugin Is and How It Differs from Upstream](#1-what-a-plugin-is-and-how-it-differs-from-upstream)
2. [Where Plugins Live on the SD Card](#2-where-plugins-live-on-the-sd-card)
3. [Toolchain and Building](#3-toolchain-and-building)
4. [The `.plg` Binary Format](#4-the-plg-binary-format)
5. [Rules Every Plugin Follows](#5-rules-every-plugin-follows)
6. [Master Insert API](#6-master-insert-api)
7. [Synth Engine API](#7-synth-engine-api)
8. [Worked Example: `tilt.plg`, a Gain and Tilt Insert](#8-worked-example-tiltplg-a-gain-and-tilt-insert)
9. [Worked Example: `sawf.plg`, a One Oscillator Saw Synth](#9-worked-example-sawfplg-a-one-oscillator-saw-synth)
10. [Memory and CPU Budgets](#10-memory-and-cpu-budgets)
11. [Engine Logos (`.plgi`)](#11-engine-logos-plgi)
12. [Sidecar and Status Files](#12-sidecar-and-status-files)
13. [Validation, Safe Mode and the Crash Marker](#13-validation-safe-mode-and-the-crash-marker)
14. [Testing on the Mac](#14-testing-on-the-mac)
15. [Pitfalls from Real Bugs](#15-pitfalls-from-real-bugs)
16. [Licence Notes for Plugin Authors](#16-licence-notes-for-plugin-authors)
17. [The Browser Twin (`.wasm`)](#17-the-browser-twin-wasm)

---

## 1. What a Plugin Is and How It Differs from Upstream

A `.plg` file is a small position independent ARM Thumb-2 module with a 64 byte header, a 96 byte manifest, a code and data image and a table of addresses to fix up when it is loaded. The firmware reads the header and manifest without running any code, copies the image to its run address, applies the fix-ups and calls one exported function, `plug_entry`.

Two kinds of plugin are loaded:

| Kind | `PLUG_KIND_*` | What it does | Where it runs | Folder |
| :--- | :--- | :--- | :--- | :--- |
| **Master insert** | `PLUG_KIND_MASTER_INSERT` (1) | Processes the stereo master bus in place, after the tracks and sends mixer and before the stock limiters | Insert 1: 8 KB SRAM_L slot. Insert 2: code in FlexNVM at `0x1001E000` | `/Plugins/Master/` |
| **Synth engine** | `PLUG_KIND_SYNTH` (4) | Adds a model to the synth model list. The shim's voice pool, macros, patch slots and governor treat it like a built-in engine | Code in the FlexNVM bank, data in an SRAM pool | `/Plugins/Synth/` |

`plug_abi.h` also declares `PLUG_KIND_SEND_FX`, `PLUG_KIND_TRACK_FX` and `PLUG_KIND_OFFLINE`. The current host does not load them.

A plugin needs a **PLUG_HOST** firmware image (`make original SYNTH_OPEN=1 TRIM=1 PLUG_HOST=1`). Other builds, including the stock build, ignore the `/Plugins/` folder.

### How this differs from the upstream SDKs

This repository also carries Polyend's upstream SDKs. They solve different problems, so it helps to know which one you are reading about.

| | Upstream Custom Synth SDK ([`custom_synth_sdk.md`](custom_synth_sdk.md)) | Upstream `.ptfx` ([`modular_effects_system.md`](modular_effects_system.md)) | tracker-ff `.plg` (this guide) |
| :--- | :--- | :--- | :--- |
| **Target** | Tracker+ and Tracker Mini (Cortex-M7) | All three devices | Original Tracker (MK66) only |
| **Delivery** | C++20 source compiled **into** the firmware. You edit `registry.cpp` and `config.h`, rebuild and flash a `.ptf` | Binary loaded from `/Effects/` | Binary loaded from `/Plugins/Master/` or `/Plugins/Synth/`. No firmware rebuild |
| **What it is** | Real time synth voices (`CustomSynthVoice`, `SynthVoiceSDK.h`) | Offline Sample Editor effects and generators on `int16` samples | Real time Master inserts on a float bus, and real time synth engines |
| **Audio format** | `float` blocks of 128 samples | `int16` sample buffers, processed when you press Preview or Apply | `float` blocks of 128 frames, full scale +-1.0 |
| **Header** | none (it is C++ in the image) | 472 byte `PtfxHeader` | 64 byte `plug_file_header` plus 96 byte `plug_manifest` |
| **Relocation** | linker | position independent, flat copy | linked at address 0, `R_ARM_ABS32` rebase table |
| **Engine interface** | `InstrumentVoice` and `CustomSynthVoice` | none | OpenTracker's `ISynthEngine` plus an `EngineDef` table, exported as `plug_ext_synth` |

Facts that follow from this:

- The MK66 `SYNTH_OPEN` build does not link `libPolySynth.a` or `libPolyDSP.a`. The synth layer on the original Tracker is the open shim in [`synth-open/`](../synth-open/README.md), which hosts OpenTracker's `ISynthEngine` interface. An engine written against `SynthVoiceSDK.h` for Tracker+ is a different interface and does not load as a `.plg`. This guide does not cover porting.
- A `.plg` engine and a `.ptfx` effect are unrelated. `.ptfx` files are not affected by anything in this guide.
- The `.plg` file format is meant to be shared by other kinds in later phases (the header says so), which is why `kind` and `cpu` are open enums that a host skips when it does not know them.

---

## 2. Where Plugins Live on the SD Card

```text
SD Card Root/
├── Plugins/
│   ├── Master/                 insert plugins you add
│   │   ├── dirt.plg
│   │   └── tilt.plg
│   ├── Synth/                  engine plugins you add, each with an optional logo
│   │   ├── sawf.plg
│   │   └── sawf.plgi
│   ├── installed.txt           written by the host: index of the FlexNVM engine bank
│   ├── synth.txt               written by the host: what happened to each engine
│   ├── host.txt                written by the host: what each insert slot holds
│   └── .loading                crash marker, exists only while a plugin starts
├── Patches/<ENGINE>/*.patch    one folder per engine name, created on first save
└── Projects/<project>/plugins.txt   insert settings of a project
```

- A plugin file is `*.plg` in any case. The name must be shorter than 40 characters. macOS `._` twin files are ignored (`isPlugName` in [`plug_index.h`](../synth-open/plug_index.h)).
- **A synth engine file name is at most 23 characters, `.plg` included** (19 before the extension, `eightvoice_ext_v2.plg` is fine). The flash bank keeps the name in 24 bytes (`kBankFileLen` in [`plug_bank.h`](../synth-open/plug_bank.h)). A longer name is listed and shows its logo, but its install fails (the bank looks for a cut name, error `kBankNoFile`) and the part shows `Missing: <name>`. Insert files in `/Plugins/Master` have only the 39 character limit above.
- A scan lists at most 272 names (`kScanMax`). `/Plugins/Master` keeps every insert a scan lists (`kIndexMax` = 272: projects name an insert by its `uid`, so no stored number bounds the count), `/Plugins/Synth` at most 256 engine files (`kLibMax`), of which at most 249 are listed: a listed engine needs a stored number of its own (7 to 255, one byte in a project). A file the host rejects still takes one of the 272 places.
- Two files with the same `uid` in one folder: the higher `version` wins.
- The host reads only the header and manifest of each insert file at boot and when the Inserts page opens, and of each engine file at boot. Nothing executes until a plugin is picked or a project needs it.
- A rejected file is listed in `/Plugins/host.txt` (inserts) with its error number. See [section 13](#13-validation-safe-mode-and-the-crash-marker).

**Using a plugin.** Master inserts: on the Master screen, **Shift + Saturation** opens the Master Inserts page. Columns 2 and 3 pick the plugin of insert 1 and insert 2. Columns 4 to 7 are the parameters of the focused insert, four at a time. Picking a plugin while a built-in Saturation entry is on switches the Saturation to `Inserts`. Synth engines: the engine appears in the synth model list from boot, installed or not. When the list rests on it for 500 ms the host installs it and shows the popup `Loading <NAME>`.

---

## 3. Toolchain and Building

### What you need

- **GNU Arm Embedded Toolchain 10.3-2021.10** (the pinned compiler). The repository carries it in `agt/10.3/` when you set it up as the top-level README describes. Any other location works through `TOOLCHAIN`.
- **Python 3**. The emulator tools also need the `unicorn` module (and `capstone` for the cycle benches):
  ```bash
  python3 -m venv /tmp/phzvenv && /tmp/phzvenv/bin/pip install unicorn capstone
  ```
- `make` and a C++ compiler for the host tests in [section 14](#14-testing-on-the-mac).

**Which toolchain the figures in this guide came from.** The pinned release toolchain is **GNU Arm 10.3-2021.10**, and it is the one to build release plugins with. The sizes, checksums and instruction counts printed in sections 8, 9 and 10 for the two examples and the shipped inserts were taken with **Arm GNU Toolchain 12.3.Rel1** (GCC 12.3.1, native arm64 host), because the 10.3 binaries are x86_64 only. The examples came out the same on both compilers: `tilt.plg` and `sawf.plg` have the same image sizes and rebase counts and the same render checksums. The worst block of `sawf.plg` is 23,243 instructions on 12.3 and was 23,258 on 10.3 (29,410 and 29,426 in scenario 1), so expect small differences in instruction counts between compilers. Checksums of the shipped inserts and engines can differ between compilers (Dirt's `3b1f3dc5` is the one that held on both), so compare them only between builds of the same compiler.

### The build tool: `build_plug.py`

[`synth-open/plug/build_plug.py`](../synth-open/plug/build_plug.py) compiles your sources, links them, checks the result and writes the `.plg`. It always rebuilds everything into a temporary folder, so it never reuses stale objects.

```bash
export TOOLCHAIN=$PWD/agt/10.3/bin/arm-none-eabi-     # prefix, with the trailing dash
python3 synth-open/plug/build_plug.py --id org.example.fx.gain --kind master \
    --version 1.0.0 --opt "-O2 -ffp-contract=off" --out gain.plg gain.c
```

If `TOOLCHAIN` is unset the tool tries `agt/10.3/bin/arm-none-eabi-` next to the repository, then `arm-none-eabi-` on `PATH`.

| Option | Meaning |
| :--- | :--- |
| `sources...` | `.c`, `.cpp`, `.cc` or prebuilt `.o` files. `plug_rt.c` is added automatically |
| `--out FILE` | Output `.plg` |
| `--id ID` | Reverse DNS id, hashed (FNV-1a 32 bit) into the `uid`. **Projects store the uid, never the file name.** Keep the id when you ship a new version |
| `--kind` | `master` or `synth` (others exist in the tool but the host does not load them) |
| `--version A.B.C` | Stored as `A << 16 | B << 8 | C` |
| `--opt "FLAGS"` | Compiler optimisation flags. **Default `-O2 -ffp-contract=off`** |
| `--src-opt NAME=FLAGS` | Different flags for the source with this base name (see "Ports of firmware code" below) |
| `-I DIR` | Extra include directory (repeatable). `synth-open/plug/` is always on the path |
| `--synth STORED,PATCH,TYPE` | The three engine numbers for the manifest (synth only, decimal or `0x` hex, each 0 to `0x7FFFFFFF`) |
| `--logo FILE.plgi` | Append a logo trailer ([section 11](#11-engine-logos-plgi)) |
| `--licence SPDX` | Licence id in the manifest, default `MIT` |
| `--role` | `insert` (default). `reverb` and `delay` are reserved |
| `--keep FILE.elf` | Keep the linked ELF (the checks in `build_plugs.sh` read its relocations) |
| `--ram N`, `--cycles N` | Declared instance bytes and worst case cycles. Informative only in host level 1 |

### What the tool runs

- **Compile**: `-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -ffunction-sections -fdata-sections -ffreestanding -fno-common -fvisibility=hidden`. For C++ also `-std=gnu++20 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit`.
- **Link**: `ld -T plug.ld --gc-sections --emit-relocs`, with only `libgcc`. There is no libc and no libm. See [section 5](#5-rules-every-plugin-follows) for what you can call.
- **Refuse**: static constructors (`.init_array`, `.ctors`), GOT access, any absolute relocation other than `R_ARM_ABS32` (this includes `MOVW`/`MOVT` absolute pairs) and PC-relative references between the text and the writable segment.
- **Write**: the rebase table, the manifest (name, author and parameter count are read from the descriptor marked `PLUG_DESCRIPTOR`), the header and the CRC.

It prints the sizes, for example `image 664 B (text 664, rw 0), bss 8 B, 16 rebase words`.

### Optimisation flags: `-O2` is the default

New plugins use `-O2 -ffp-contract=off`. `-O2` matters: at `-Os` GCC 10.3 kept small per-sample helpers as calls with their state in memory, and moving the shipped engines from `-Os` to `-O2` saved up to about 28 points of CPU at four voices (CS-Type 74.9 to 46.3 percent worst case, see [section 10](#10-memory-and-cpu-budgets)). `-ffp-contract=off` stops GCC from fusing a multiply and an add into a `vfma`, which costs 3 cycles on the M4F against 2 for a multiply followed by an add.

The shipped synth plugins are compiled with the **firmware's own flags per engine**: `-O2 -ffp-contract=off` for the OpenTracker modules, PHZ and PLK, `-O3 -ffp-contract=off` for FM4 and B-Type (the 1.2.0 release recipe, synth-open/README.md, "Release 1.2.0 lines"). They are the same sources the firmware compiled when those engines were built in, and they render bit identical to the built-in engine. The flags of each engine are in `engines/engines.json`. A new plugin has nothing to match, so it uses the defaults.

### Ports of firmware code

Two builds of one source are bit identical only with the same floating point contraction. If your plugin links a firmware source or object that must sound the same as the one in the image, compile it with the flags the firmware uses (`--src-opt NAME=FLAGS`). `dirt.plg` is the case: `ff_dirt_ctl.cpp` is `-Os` in the image, which fuses `1 + 15 d^2` into a `vfma`. At `-O2 -ffp-contract=off` its `Coef::pre` differs by one ulp and the output hash changes.

### Building and checking everything: `build_plugs.sh`

```bash
export TOOLCHAIN=$PWD/agt/10.3/bin/arm-none-eabi-
export PY=/tmp/phzvenv/bin/python
synth-open/plug/build_plugs.sh build/plugins
```

It builds the two examples of this guide and checks each one (the shipped plugins are built and checked the same way in their module repositories, section 18):

- no `R_ARM_GOT*` relocation, and the rebase table holds exactly the `R_ARM_ABS32` words of the ELF
- the insert renders to the same checksum at the SRAM_L home, the FlexNVM home with rw on the heap and in SDRAM, with default settings and again with every parameter moved and one more change in block 5
- the synth example passes the firmware's own attach check ([`validate_synth.py`](#13-validation-safe-mode-and-the-crash-marker)) and renders to the same checksum at two homes

The examples build into `build/plugins/examples/`. The script prints `PASSED` or `FAILED` last.

### Building the shipped engines: `engines/`

The shipped synth engines are built in their module repositories (section 18): the ten public ones in poly-engines-os (`make TOOLCHAIN=... PY=...`, then `make verify`), PHZ and PLK in the private poly-engines. Each build starts from a scratch copy of the files listed in its `sdk/` (the header set the engines compile against), so an engine cannot depend on a header outside it. The set holds the `.plg` and `.plgi` files, `SET.json` (set version, compiler, flags, SDK version and ABI, render checksums, one record per engine) and `SHA256SUMS`: copy the `.plg` and `.plgi` files to `/Plugins/Synth/`. The checks that need the firmware (its own attach check and the card logo reader) run here on a fetched set: `engines/crosscheck.sh DIR`.

---

## 4. The `.plg` Binary Format

All fields are little endian. Sizes are in bytes. The authoritative definitions are in [`plug_abi.h`](../synth-open/plug/plug_abi.h).

```text
offset 0     plug_file_header (64)     magic "PLG1", header_size, abi 1.x, kind, cpu, flags, uid, version, min_host,
                                       image_size, bss_size, rebase_count, entry_offset, ram_bytes, cpu_cycles,
                                       crc32, rw_offset, 8 reserved
offset 64    plug_manifest (96)        struct_size, name[16], author[32], licence[16], num_params, role, reserved
header_size  image (image_size)        [0, rw_offset)          text: code, constants, the descriptor (may run from flash)
                                       [rw_offset, image_size) rw: initialised data (always RAM)
             bss                       bss_size zeroed bytes after the rw data, in RAM, not in the file
             rebase table              rebase_count x u32: image offsets of the words that hold an image address
             logo trailer (optional)   a 1,676 byte PLGI record, outside the CRC (section 11)
```

### Header (`plug_file_header`, 64 bytes)

| Offset | Field | Meaning |
| :--- | :--- | :--- |
| `+0x00` | `magic` | `0x31474C50` (`"PLG1"`) |
| `+0x04` | `header_size` (u16) | 64 plus the manifest: 160 for every file `build_plug.py` writes. The image starts here. The host refuses a value above 1024 |
| `+0x06` | `abi_major` (u8) | 1. The host refuses any other major |
| `+0x07` | `abi_minor` (u8) | The builder writes 0 |
| `+0x08` | `kind` (u8) | `PLUG_KIND_*` |
| `+0x09` | `cpu` (u8) | `PLUG_CPU_CM4F` (1). The host refuses others |
| `+0x0A` | `flags` (u16) | 0. `PLUG_F_S16` (bit 0) is reserved and the host refuses it |
| `+0x0C` | `uid` | FNV-1a 32 of the `--id` string. What a project stores |
| `+0x10` | `version` | `major << 16 | minor << 8 | patch` |
| `+0x14` | `min_host` | Lowest host level needed. The builder writes 1, the host level is 1 (`PLUG_HOST_LEVEL`) |
| `+0x18` | `image_size` | Multiple of 4. The host refuses more than 65,536 |
| `+0x1C` | `bss_size` | Zeroed bytes after the image. The host refuses more than 65,536 |
| `+0x20` | `rebase_count` | Entries in the rebase table. The host refuses more than 16,384 |
| `+0x24` | `entry_offset` | Offset of `plug_entry` in the image. The host adds the Thumb bit |
| `+0x28` | `ram_bytes` | Declared RAM. Informative |
| `+0x2C` | `cpu_cycles` | Declared worst case cycles of one block. Informative |
| `+0x30` | `crc32` | CRC-32 (IEEE, the zlib one) of every byte after the first 64: manifest, image and rebase table |
| `+0x34` | `rw_offset` | Start of the writable segment, 8 byte aligned. `[0, rw_offset)` may run from flash |
| `+0x38` | reserved | 8 bytes, 0 |

A synth plugin needs the ABI 1.1 host table (`exp2f` and `log2f`). It does not rely on the `abi_minor` field for that. `PLUG_SYNTH_PLUGIN` checks `PLUG_HAS(host, plug_host, log2f)` in `plug_entry` and returns `NULL` when the host is older.

### Manifest (`plug_manifest`, 96 bytes at offset 64)

| Offset in the manifest | Field | Meaning |
| :--- | :--- | :--- |
| `+0` | `struct_size` | 96 |
| `+4` | `name[16]` | List entry, copied from `plug_desc.name` by the builder. 15 characters at most |
| `+20` | `author[32]` | Copied from the descriptor |
| `+52` | `licence[16]` | SPDX id, `MIT` by default |
| `+68` | `num_params` (u16) | From the descriptor. At most 32 for an insert |
| `+70` | `role` (u8) | 0 insert. 1 reverb and 2 delay are reserved |
| `+72` | `reserved[0]` | File offset of a logo trailer, 0 for none |
| `+76` .. `+88` | `reserved[1..4]` | Synth engines: stored number, `.patch` engine id, type id, then `0x314E5953` (`"SYN1"`, `PLUG_SYNTH_MANIFEST`) |

The host lists a synth engine before it is installed from these numbers alone. A synth `.plg` without them is not listed.

### Sections and relocations

- **Text** `[0, rw_offset)`: code, `.rodata`, the descriptor and `.data.rel.ro` (constant tables that hold addresses, for example a `const char* const[]` of enum names). It is read only once rebased and can run from flash.
- **rw** `[rw_offset, image_size)`: initialised data, always copied to RAM. Then **bss** `bss_size` zeroed bytes.
- The image is **linked at address 0**. The loader adds the run address of the segment a word points into (text or rw) to every word named in the rebase table. Only `R_ARM_ABS32` is relocated. PC-relative branches and loads are fine inside one segment. There is no GOT, no PIC and no ELF on the device.
- The loader checks that every table offset is a whole aligned word inside the image and that the value in it is below `image_size + bss_size`. A table that breaks this gives error 7 (`kRebase`).
- The descriptor, the parameter tables, the enum name tables and every function pointer must lie in the **text** segment. `checkDesc` refuses the plugin otherwise (error 9, `kDesc`). Declare tables `const`.

### Size limits

| Limit | Value | Where it comes from |
| :--- | :--- | :--- |
| `header_size` | 1,024 | `checkHeader` |
| `image_size`, `bss_size` | 65,536 each | `checkHeader` |
| `rebase_count` | 16,384 and at most `image_size / 4` | `checkHeader`, `checkLayout` |
| Insert 1 | `image + bss` up to 8,192 | the SRAM_L slot |
| Insert 2 | text up to 8,192, `rw + bss` up to 1,024 | FlexNVM and a fixed SRAM buffer |
| Synth engine | `image + bss` up to 32,768 and `rw + bss` within the 3,072 byte pool | staging buffer and rw pool |
| Insert instance | `instance_size` up to 16,384 | `checkDesc` |

Section 10 explains the memory behind each of these.

---

## 5. Rules Every Plugin Follows

1. **No static constructors, no exceptions, no RTTI.** The builder refuses a plugin that has a constructor in `.init_array`. A global object with a vtable and no constructor body is fine (the engine in [section 9](#9-worked-example-sawfplg-a-one-oscillator-saw-synth) is one).
2. **All state lives in the instance.** An insert gets `instance_size` zeroed bytes. Do not keep per instance state in globals. Constant tables of pointers are fine (they are rebased).
3. **Audio context rules.** `process`, `set_param` and every synth call except `init` run in the audio interrupt. No allocation, no file access, no waiting, no locks.
4. **No libc, no libm** except what `plug_rt.c` provides. It forwards `sinf`, `cosf`, `tanf`, `tanhf`, `expf`, `logf` and `powf` to the host's table (and `exp2f` and `log2f` for synth plugins). `sqrtf` is one `vsqrt` instruction. `memcpy` and `memset` are local loops that store words when both pointers are 4 byte aligned. A call to any other libc or libm function fails at link time because only `libgcc` is linked. `__builtin_fabsf` and plain arithmetic need nothing.
5. **Transcendentals belong in `set_param` or `init`, not in the per-sample loop.** Each host libm call is a function call into the firmware.
6. **Use `float`.** Never `double`. The M4F FPU is single precision.
7. **Denormals.** Flush filter state that decays to nothing (the examples use `flush()`).

### The host table (`plug_host`)

`plug_entry` and `init` receive a `const plug_host*`. `plug_rt.c` keeps it in `plug_rt_host`.

```c
struct plug_host {
    uint32_t struct_size, level;             // sizeof, PLUG_HOST_LEVEL (1)
    float sample_rate;                       // 44117.647
    float (*sinf)(float);   float (*cosf)(float);   float (*tanf)(float);   float (*tanhf)(float);
    float (*expf)(float);   float (*logf)(float);   float (*powf)(float, float);
    void* (*memcpy)(void*, const void*, size_t);
    void* (*memset)(void*, int, size_t);
    void* (*alloc)(uint32_t bytes, uint32_t flags);   // init only. flags 0: SRAM heap, 1: SDRAM arena
    uint32_t (*cycles)(void);                          // DWT cycle counter
    const void* (*query)(uint32_t ext_id);             // NULL for every id in host level 1
    float (*exp2f)(float); float (*log2f)(float);      // ABI 1.1: check with PLUG_HAS
};
```

- `alloc` works only inside an insert's `init` (it returns `NULL` anywhere else, and always for a synth engine). Memory is zeroed, rounded up to a multiple of 8 and freed with the instance. An insert slot gets at most 4 heap blocks counting its instance, and 32 KB of SDRAM in total.
- Check optional host fields with `PLUG_HAS(host, plug_host, field)`. Every struct that crosses the boundary starts with `struct_size`. New fields go at the end and nothing is reordered or removed.

### Compatibility

- `abi_major` must equal the host's (1). `abi_minor` and the host level only grow.
- Optional features are queries: `desc->query(PLUG_EXT_*)` and `host->query(...)` return `NULL` or a struct with its own `struct_size`. Only `PLUG_EXT_SYNTH` is used by the host today. `PLUG_EXT_STATE`, `PLUG_EXT_TAIL` and `PLUG_EXT_TEMPO` are declared and the host does not ask for them.
- A host skips a kind, cpu or role it does not know and refuses a plugin whose `min_host` is above its level, before any code runs.

---

## 6. Master Insert API

A master insert is a C or C++ file that fills a `plug_desc`, exports `plug_entry` and implements four callbacks.

```c
typedef struct {
    uint32_t struct_size;       // sizeof(plug_desc) at build time
    uint32_t kind;              // PLUG_KIND_MASTER_INSERT
    const char* name;           // list entry, 1 to 15 characters
    const char* author;
    uint32_t num_params;        // at most 32
    const plug_param* params;
    uint32_t instance_size;     // bytes per instance, 8 aligned, zeroed, at most 16 KB
    int32_t (*init)(void* inst, const plug_host* host);
    void    (*reset)(void* inst);
    void    (*set_param)(void* inst, uint32_t index, float value);
    void    (*process)(void* inst, float* const* ch, uint32_t channels, uint32_t frames);
    const void* (*query)(uint32_t ext_id);        // may be 0
} plug_desc;
```

### Parameters (`plug_param`)

```c
typedef struct {
    const char* name;      // 11 characters or less show in full. Values are stored in the project BY NAME
    const char* unit;      // "", "%", "Hz", "dB", "ms"
    float min, max, def, step;
    uint16_t enum_count;   // > 0: an enum with enum_count names, min 0, max enum_count - 1
    uint16_t flags;        // reserved, 0
    const char* const* enum_names;
} plug_param;
```

- A numeric parameter shows its value and a bar. An enum shows all its names in the stock list control with the current one highlighted.
- The host rounds a value to `step` and clamps it to `[min, max]` before it reaches you. Enums arrive as indices `0 .. enum_count - 1`.
- A project stores values **by name**. Renaming a parameter loses its stored value. Reordering is harmless. A value that is missing takes the default.
- `plug_desc.name` is also the list entry. Names in the Inserts page value line are cut to 9 characters.

### Callbacks and when they run

| Callback | Context | What it must do |
| :--- | :--- | :--- |
| `init(inst, host)` | Main thread, audio for this instance not running yet | Leave every parameter at its default. Return 0. Anything else is error 11 (`kInit`). The host then calls `set_param` for each value the project changes from the default |
| `set_param(inst, i, v)` | **Audio interrupt**, at a block start, before `process` | Store the value and derive coefficients. Only the latest value of each parameter is delivered |
| `process(inst, ch, channels, frames)` | **Audio interrupt** | Process `ch[0..channels-1]`, each `frames` floats, **in place**. `channels` is 2 and `frames` is 128 |
| `reset(inst)` | not called | Declared for later hosts. The phase 2 host never calls it, so do not rely on it to clear state |

Because `set_param` also runs when the inserts are switched on again after a bypass, `init` and `set_param` together must leave the plugin in a state that is correct for any set of values.

### The bus: levels, conversion, NULL and silence

The host converts the bus once for both slots:

- **Input**: `float(int16) / 32768`, so full scale is +-1.0.
- **Output**: `int32(x * 32768)` (truncation toward zero), clamped to the `int16` range. A plugin may overshoot +-1.0 inside the chain. The clamp happens after the second slot, so there is no wrap.
- The conversion pair costs 6,496 cycles measured, shared by the two slots.
- **Order**: insert 1 runs first, then insert 2, on the same buffer.
- **Silent bus**: if both channel blocks are `NULL` (the bus is silent), **`process` is not called and nothing is transmitted**. A plugin with a tail (delay, reverb, chorus) stops mid ring and keeps its old state until sound returns. `PLUG_EXT_TAIL` is declared for this and the host does not ask for it yet.
- **One channel `NULL`**: the host allocates a block of zeros for the missing channel, so you always get two channels of 128 frames.
- **No plugin running** (or both slots bypassed or tripped): the blocks pass untouched.
- The buffer is shared static memory. Do not keep pointers to it between calls.

### State and the project

A plugin has no state beyond its parameters. The project stores, per insert slot, the plugin `uid`, `version`, name and each parameter value by name (see the sidecar in [section 12](#12-sidecar-and-status-files)). A plugin that is missing from the card shows as `Miss:<name>`, the audio passes and its values are kept and saved back. `PLUG_EXT_STATE` (arbitrary extra state) is reserved and not implemented.

### The Mixer column

The Inserts page has a first column, **Mixer**, that does not belong to any plugin. It picks the stage of the tracks and sends mixers that runs **under** the inserts: Hard Clip (default), Soft Clip or Soft Knee. Soft Knee is bit exact below -3 dBFS and a smooth curve into full scale above (`sat_softknee.h`). The project stores it in `plugins.txt` as `mixer <n>` (1 Soft Clip, 10 Soft Knee, no line for Hard Clip). It matters to a plugin author because it sets how hot the bus can be when it reaches your insert: with Hard Clip the mixers clip at full scale before your `process` sees the signal, with Soft Knee the signal is rounded off first.

### Picking and the status line

Picking waits until the selection rests for 400 ms, then loads. The value line of an insert column is at most 9 characters: the plugin name, `None`, `Loading`, `Bypassed` or a tag and as much of the name as fits (`Safe:BBD`, `Miss:Lo-Fi`, `Fail:Dirt`, `CPU:Phase`). A built-in Saturation entry bypasses both slots and keeps them loaded.

The plugin list comes from the last scan of `/Plugins/Master`. Opening the page reads only the folder's directory entries (the names and sizes of the `.plg` files) and scans the files again when they differ from the last scan, so the page opens without a card read per plugin. Every USB storage session and every card change makes the next open scan all files. A file rewritten in place at the same name and size is not seen until then, and picking it is safe: the loader checks that the file still has the `uid` and CRC the list showed, and when it does not the host scans the folder and loads again from the new list.

---

## 7. Synth Engine API

A synth engine plugin is **not** written against the C ABI directly. You write an OpenTracker `ISynthEngine` and an `EngineDef`, and [`synth_plug.h`](../synth-open/plug/synth_plug.h) turns them into a plugin. The C ABI underneath is `plug_ext_synth`, which the host reads through `query(PLUG_EXT_SYNTH)`.

### The three pieces

1. **`ISynthEngine`** ([`ot/audio/synth_engine.h`](../synth-open/ot/audio/synth_engine.h), OpenTracker 3.x, Apache-2.0): `noteOn`, `noteOff`, `render` and optional `prepare`, `slotReset`, `setTempo`, `slotPost`, `setSendTap`. A stateless singleton. All voice state lives in the voice record.
2. **`EngineDef`** ([`open_engine.h`](../synth-open/open_engine.h)): the name, the type id, the parameter, section and macro tables, the voice cap and the engine factory.
3. **The plugin line**: `PLUG_SYNTH_PLUGIN(NAME, DEF, STORED, PATCH_ENGINE)`. One line fills the descriptor, `plug_entry` and the `plug_ext_synth` extension.

```cpp
#include "synth_plug.h"
namespace polysynth::open { extern const EngineDef kJu6Def; }
PLUG_SYNTH_PLUGIN("JU6", polysynth::open::kJu6Def, 7, 0x81)   // name, definition, stored number, .patch engine id
```

`PLUG_SYNTH_PLUGIN_WORK(NAME, DEF, STORED, PATCH_ENGINE, WORK_BYTES, SET_WORK)` adds working memory (see below). Define `PLUG_SYNTH_AUTHOR` before the include to set the author line.

### Engine identity: three numbers

| Number | What it is | Where it is stored |
| :--- | :--- | :--- |
| `stored` | The engine's number in a project (`synthPart[].synthType`) | binary project |
| `patch_engine` | Field 1 of a `.patch` file | patch files |
| `type_id` | The shim's instrument key (`EngineDef::typeId`) | in memory |

All three must be unique among built-in engines and among the engines on the card. If a number is taken, the engine is not listed (`synth.txt` shows `err 6`). The ABI note says a new engine uses its uid for all three, and the example does that: `0x64880b9c` is the FNV-1a hash of `org.example.synth.sawf`. The same three numbers go to `build_plug.py --synth STORED,PATCH,TYPE` so the manifest carries them. They must equal the numbers in the plugin line and the definition's `typeId`, or attach fails with error 8.

The thirteen shipped engines keep the numbers they had when they were built in, so old projects and patches find them: CHIP 6 / `0x08`, JU6 7 / `0x81`, PHZ 8 / `0x82`, FM4 9 / `0x80`, D-Type 10 / `0xA2`, CS-Type 11 / `0x83`, A-Type 12 / `0x84`, T-Type 13 / `0x85`, 8-Type 14 / `0x86`, Hand-Type 15 / `0x87`, B-Type 16 / `0x88`, Arc-Type 17 / `0x89`, PLK 18 / `0x8A`. PHZ and PLK are private modules (source in the private repository `poly-engines`), this repository keeps their numbers.

The engine `name` is 1 to 8 printable characters with no `/`. It is the on-screen name and the `Patches/<name>/` folder.

### Voice lifecycle

The framework (the shim) owns slots, the voice pool, macros, pitch FX, gain and mixing. There are 3 synth parts and one pool shared by them. Each part holds one engine and up to 4 voices (`max_voices` 1 to 4).

1. **Attach.** When a part needs the engine the host installs it, copies its rw data to RAM, zeroes bss, calls `plug_entry`, checks the descriptor, calls `init` (with an 8 byte scratch, a synth engine has no instance), reads `PLUG_EXT_SYNTH`, runs `checkSynth` and compares the numbers with the manifest. If the engine needs working memory it receives it through `set_work` here.
2. **`slotReset(scratch)`**: the part starts using the engine (patch or model change, project load).
3. **Per block, for every part with sounding voices**: `setTempo(bpm)`, then `prepare(scratch, params)` once for shared coefficients, then `render` for each voice, then `slotPost` if the engine has a slot effect.
4. **`noteOn(voice, params, note)`**: a fresh voice has zeroed `state`. A reused voice (`note.reused`, a mono or legato part) keeps its state, so continue envelopes and glide from where they are. `note.overlap` is true when a still gated voice is reused (the glide modes use it).
5. **`noteOff(voice, params)`**: the gate went off. Enter the release.
6. **`render(voice, params, g0, gInc, L, R)`**: see below.
7. **Eviction or a model change**: the host detaches the engine first (the parts on it are cut and silent), then the flash bytes may change.

### The voice record (`SynthVoice`)

The framework writes the fields above `state` and engines read them. The engine may write only `level` and `state`.

| Field | Meaning |
| :--- | :--- |
| `state[128]` | Your per voice state. Cast with `v.as<T>()`. `sizeof(T)` must be at most 128 |
| `slotScratch` | The owning part's scratch: 384 bytes zeroed, cast with `scratch.as<T>()` |
| `bend`, `bendStart` | Step FX pitch offset in semitones, at the block end and the block start (ramp between them) |
| `level` | **You write it**: the amplitude envelope level 0 to 1. The pool steals the quietest released voice first |
| `gate` | Key held. False after `noteOff` |
| `fading` | This block is the voice's last (stolen or cut). The gain ramp falls to 0 and the record is freed after it |
| `cutoffOct` | Performance filter offset of the note's track, in octaves. An engine with a filter adds it to its cutoff octave term inside its own clamp |

### `render`

```cpp
bool render(SynthVoice& v, const SynthParams& p, float g0, float gInc, float* L, float* R);
```

- **Adds** `kBlock` (128) frames into `L` and `R`, multiplied by `g0 + i * gInc`, and returns `false` once the voice is silent for good (the framework then frees it). Never overwrite the buffers, other voices are already in them.
- `g0 + i * gInc` carries the Volume parameter (the one named by `volume_param`), the velocity law and fades. **Do not apply Volume yourself.**
- The sample rate is `ot::audio::kSampleRate` (44,117.647 Hz).
- The output of a synth part is mono: the shim averages `L` and `R` into the left buffer. Write the same value to both.
- Pitch: `SynthNote::pitch` is the note in semitones (MIDI numbering, 69 is 440 Hz). Sounding pitch is your (glided) note pitch plus `v.bend`. A note with a Glide FX (`note.fxGlide`) skips the patch glide.

### Parameters at run time (`SynthParams`)

```cpp
struct SynthParams {
    int32_t engine;  int count;
    bool moving;                 // some start[i] != end[i] this block
    SynthHeld held;              // the part's held notes: count, low, high, mono
    uint32_t serial;             // changes whenever an end[] value changes
    float start[84], end[84];    // value at the block start and the block end (the target)
    float ramp(int i, float& inc) const;
};
```

Values are in engineering units. Enums are indices stored as floats. They are already clamped to their range and have the macros M1 to M6 and the synth automation applied. Read `p.end[i]` once per block, or ramp from `p.start[i]` to `p.end[i]` across the block (`p.ramp(i, inc)`) for parameters that must not step. `serial` and `moving` let you skip a recompute when nothing changed.

### The metadata tables

The host points its engine description **straight at your arrays**. Nothing is copied. They must lie in the text segment and keep the layout of the shim's `ParamDef`, `SectionDef` and `MacroDef`.

**Parameter (`ParamDef`)**: `{ name, def, min, max, fmt, curve, enumNames }`

- `name`: keep it to 11 characters, as the shipped engines do.
- `fmt`: how the value prints, one of the 16 formats below.
- `curve`: the edit curve of the encoder.
- `enumNames`: for `Fmt::kEnum`, `max + 1` names. `min` must be 0 and `max` at most 64.
- At most **84 parameters**.

**The 16 value formats (`PLUG_FMT_*`, `Fmt`)**. The numbers are frozen:

| # | `PLUG_FMT_*` | `Fmt::` | The value `v` prints as |
| :--- | :--- | :--- | :--- |
| 0 | `PLUG_FMT_PCT` | `kPct` | `v * 100` followed by `%` |
| 1 | `PLUG_FMT_PCT_MAX` | `kPctMax` | `v / max(abs(min), abs(max)) * 100` followed by `%` |
| 2 | `PLUG_FMT_HZ` | `kHz` | `440 Hz` below 1000, `1.25 kHz` above |
| 3 | `PLUG_FMT_SEC` | `kSec` | `250 ms` below 1, `1.50 s` above |
| 4 | `PLUG_FMT_LFO_HZ` | `kLfoHz` | `v` with two decimals and `Hz` |
| 5 | `PLUG_FMT_ENUM` | `kEnum` | `enumNames[round(v)]` |
| 6 | `PLUG_FMT_VAL100` | `kVal100` | `v * 100` as a plain number |
| 7 | `PLUG_FMT_ON_OFF` | `kOnOff` | `On` from 0.5, else `Off` |
| 8 | `PLUG_FMT_SEMI` | `kSemi` | `7 st` |
| 9 | `PLUG_FMT_CENTS` | `kCents` | `-25 c` |
| 10 | `PLUG_FMT_INT` | `kInt` | rounded integer |
| 11 | `PLUG_FMT_RATIO` | `kRatio` | `0.25 * (round(v) + 1)`, index 0 to 47 (WTFM ratios) |
| 12 | `PLUG_FMT_MS10` | `kMs10` | `v / 10` with two decimals and `ms` |
| 13 | `PLUG_FMT_DB` | `kDb` | `v` with one decimal and `dB` |
| 14 | `PLUG_FMT_PAN` | `kPan` | `v * 100` as `L50`, `0` or `R50` |
| 15 | `PLUG_FMT_UNIT` | `kUnit` | `v` with two decimals, 0.00 to 1.00 (PHZ X and Y) |

`PLUG_FMT_UNIT` (15) is the last format a plugin may use. It sits after `kPan` on purpose: the firmware also has two PERC-only formats (note name and body wave) that exist only in a build that contains PERC, so the plugin formats are numbered below them and do not depend on which engines a build has. Both `checkSynth` (`p.fmt >= PLUG_FMT_COUNT`) and `validate_synth.py` reject format 16 and up.

**Edit curves (`PLUG_CURVE_*`, `CurveKind`)**: `kLinear` (0), `kExp` (1, geometric when `min > 0`, else quadratic), `kSteep` (2, cubic), `kPower` (3, quadratic). Use `kExp` for frequency, `kSteep` for long times.

**Sections (`SectionDef`)**: `{ name, shortName, ids, count }`. A section is a page of the patch editor (a tile in the grid). `ids` lists parameter indices. At most **10 sections**. The editor picks the section icon and grid label by looking for a word in `name` ([`open_section_icon.h`](../synth-open/open_section_icon.h)): `Oscillator`, `Filter`, `Amplifier`, `Amp Envelope`, `Envelope`, `LFO`, `Modulation`, `Voice`, `Mixer`, `FX`, `Shaper`, `Kick`, `Snare` and others. Give every section a name that contains one of these words so it gets an icon.

**Macros (`MacroDef`)**: `{ name, bipolar, count, { {param, amount}, {param, amount} } }`. One or two targets per macro, amount from -2 to +2 of the parameter range. At most **6 macros**. These are the defaults of an Init patch. A user can change them. Macro 7 (Velocity) and 8 (Pressure) are added by the framework. Velocity is wired to your `volume_param` with amount 1.

**Other `EngineDef` fields**: `volumeParam` (index of the Volume parameter the framework applies as gain, `-1` for none), `maxVoices` (1 to 4), `engine` (factory function returning your `ISynthEngine*`), `slotFx` (true if the engine has a slot effect, see below).

### Optional hooks

| Hook | Use |
| :--- | :--- |
| `prepare(scratch, params)` | Once per block per part, before its voices render: shared coefficients into the 384 byte scratch |
| `slotReset(scratch)` | The part starts using the engine |
| `setTempo(bpm)` | Once per block before `prepare`: tempo synced LFOs or delays |
| `slotPost(slot, scratch, params, L, R)` | The engine's own slot effect (JU6's chorus), on the sum of the voices before slot gain, pan and sends. Return true while it has a tail. Set `slotFx` true in the definition |
| `setSendTap` | Per voice delay and reverb sends. The shim never calls it, do not use |

### Working memory (`work_bytes`, `set_work`)

An engine cannot call `alloc`. Large state (a delay line pool) comes from the host through `PLUG_SYNTH_PLUGIN_WORK`: `work_bytes` and a `set_work(void*, uint32_t)` function. The host hands it out of a 16 KB SDRAM arena, zeroed, 8 byte aligned, after every `init` (every attach) and before any audio call. The block is the engine's while it is attached. When no part plays the engine and another engine needs the room, the host detaches it and gives the block away: when the engine comes back it is attached again (rw copied, `init`, a zeroed block through `set_work`). In the rare case that the blocks of the engines in use have to be packed together, the host stops an engine that plays (its voices are cut), runs `plug_entry` and `init` again and hands it a zeroed block at its new place. So keep nothing in work memory that a fresh `init` would not rebuild, as with rw. The limit is the engines in use at the same time (one per synth part), not every engine used since boot (`synth-open/plug_work.h`). Keep only state there that is touched a few times per sample (a delay line read once per sample). An uncached SDRAM access costs about 20 to 30 cycles on the MK66, so never put hot per-sample state there. The Karplus-Strong plugin PLK (source in the private repository `poly-engines`) uses it.

### Presets and patches

- A **patch** is a `.patch` file (a protobuf `Patch` message, the format the Tracker Mini and OpenTracker use). The engine id in field 1 is your `patch_engine`. Library patches go to `Patches/<NAME>/<name>.patch`. Each synth part has 16 patch slots in the project (`<N>.patch` and `<N>-<n>.patch` in the Workspace and the project folder).
- Parameters are stored **by index**. A parameter that is missing, not finite or out of range takes its default. **Append new parameters at the end. Do not reorder or remove parameters in a released engine**, or old patches change meaning.
- The Init patch and the default macros come from your tables (`initPatchData` builds Init data from the engine definition).
- A part whose engine is not on the card shows `Missing: <NAME>` and stays silent. Its project values and patch files are never rewritten, and installing the plugin brings it back. This holds for any stored number from 6 to 255 (and `.patch` ids from 0x80 up), there is no list of known engines: the name is the one the card's plugin had when the firmware last listed it (`/Plugins/names.txt`), else `Missing: #<stored>`.
- A patch file also carries the synth automation block (field 64). It is the host's, not yours.

### Automation exposure

Each patch has **4 automation slots** (Synth Automation, page 3 of 3 of the Synth screen). A slot is Off, an LFO or an envelope on one engine parameter or one user macro. The host does this, an engine needs no code for it. What it means for an engine:

- **Destinations** are every parameter of the engine **except enums, on/off switches and unused slots (`min == max`)**, plus Macro 1 to 6. Stepped parameters (semitones, ratios) are allowed and step audibly. Volume is allowed.
- **A modulated parameter changes `p.end` every block**, `serial` increments and `moving` is set. Your per block recompute is the real CPU cost. Most shipped engines refresh once per part and block (under 1 point). FAT, VAP, WTFM, B-Type, Hand-Type and PLK also work per voice while `moving` is set on their wave and pitch parameters (+1.9 to +6.2 points at four voices).
- **Identity**: a slot stores the parameter's index, its **name** (up to 20 characters) and which occurrence of that name it is (`nth`: WTFM has three `Attack` parameters). On load the index, name and occurrence must match. If not, the nth parameter of that name is taken, so a plugin update that reordered its table is followed. If the name is gone the slot shows `? <name>`, drives nothing and is written back as it was read. **Keep parameter names stable between versions.** Renaming a parameter orphans the automation slots that point to it.
- **List order**: the wave and position like parameters come first (a word of the name starts with `Shape`, `Wave`, `Pos`, `PW`, `Timbre`, `Morph`, `Table`, `Duty`, `XMod` or `YMod`), then Macro 1 to 6, then everything else in engine order. Name your wave parameter with one of these words to have it first.
- The automation runs at part level, one gate for the part, never per voice.

---

## 8. Worked Example: `tilt.plg`, a Gain and Tilt Insert

Source: [`synth-open/plug/examples/tilt.c`](../synth-open/plug/examples/tilt.c). Three parameters: **Gain** (-12 to +12 dB), **Tilt** (-6 to +6 dB, positive lifts the lows and lowers the highs by the same amount) and **Pivot** (200 to 2000 Hz). The signal is split by a one pole low pass at the pivot: `lo = lp(x)`, `hi = x - lo`, `y = gl * lo + gh * hi`. Because `lo + hi` is exactly `x`, Tilt 0 and Gain 0 are transparent.

**1. Parameters and instance.** The descriptor's parameter table is `const`, so it sits in the text segment. All state is in the instance.

```c
#include "plug_abi.h"
extern const plug_host* plug_rt_host;   // plug_rt.c, set in plug_entry

enum { P_GAIN, P_TILT, P_PIVOT, P_COUNT };

typedef struct {
    float v[P_COUNT];     // the values as the host delivered them
    float a;              // low pass coefficient of the current Pivot
    float gl0, gh0;       // low and high gain at the block start
    float gl1, gh1;       // targets
    float lp[2];          // low pass state, left and right
} Inst;

static const plug_param kParams[P_COUNT] = {
    // name unit min max default step enum_count flags enum_names
    {"Gain", "dB", -12.0f, 12.0f, 0.0f, 0.5f, 0, 0, 0},
    {"Tilt", "dB", -6.0f, 6.0f, 0.0f, 0.5f, 0, 0, 0},
    {"Pivot", "Hz", 200.0f, 2000.0f, 650.0f, 10.0f, 0, 0, 0},
};
```

**2. Coefficients in `set_param`.** `powf` and `expf` are host calls. They run here, once per change, never in `process`.

```c
static void retarget(Inst* s)
{
    const float gain = plug_rt_host->powf(10.0f, s->v[P_GAIN] * 0.05f);
    const float half = plug_rt_host->powf(10.0f, s->v[P_TILT] * 0.025f);   // +-Tilt / 2 dB, as an amplitude
    s->gl1 = gain * half;
    s->gh1 = gain / half;
    s->a = 1.0f - plug_rt_host->expf(-6.2831853f * s->v[P_PIVOT] / plug_rt_host->sample_rate);
}

static void set_param(void* p, uint32_t index, float value)
{
    Inst* s = (Inst*)p;
    if (index >= P_COUNT) return;
    s->v[index] = value;
    retarget(s);
}
```

**3. `init`.** The host zeroed the instance. Put the plugin at its defaults.

```c
static int32_t init(void* p, const plug_host* host)
{
    (void)host;
    Inst* s = (Inst*)p;
    for (uint32_t i = 0; i < P_COUNT; i++) s->v[i] = kParams[i].def;
    retarget(s);
    reset(p);
    return 0;
}
```

**4. `process`.** State and gains go into locals (a store to `L[i]` may alias the instance, so the compiler would otherwise reload them after every sample). The gains move linearly from the value the last block ended on to the new target, so a change never clicks.

```c
static void process(void* p, float* const* ch, uint32_t channels, uint32_t frames)
{
    Inst* s = (Inst*)p;
    if (channels < 2 || frames == 0) return;
    float* const L = ch[0];
    float* const R = ch[1];
    const float a = s->a;
    float lp0 = s->lp[0], lp1 = s->lp[1];
    float gl = s->gl0, gh = s->gh0;
    const float inv = 1.0f / (float)frames;
    const float dgl = (s->gl1 - gl) * inv, dgh = (s->gh1 - gh) * inv;
    for (uint32_t i = 0; i < frames; i++)
    {
        gl += dgl;
        gh += dgh;
        float x = L[i];
        lp0 += a * (x - lp0);
        L[i] = gl * lp0 + gh * (x - lp0);
        x = R[i];
        lp1 += a * (x - lp1);
        R[i] = gl * lp1 + gh * (x - lp1);
    }
    s->lp[0] = flush(lp0);
    s->lp[1] = flush(lp1);
    s->gl0 = s->gl1;
    s->gh0 = s->gh1;
}
```

**5. Descriptor and entry.** `PLUG_DESCRIPTOR` tells the builder where to read the name and parameter count. `plug_entry` refuses a host of another ABI major.

```c
PLUG_DESCRIPTOR static const plug_desc kDesc = {
    sizeof(plug_desc), PLUG_KIND_MASTER_INSERT, "Tilt", "example", P_COUNT, kParams, sizeof(Inst), init, reset, set_param, process, 0,
};

PLUG_ENTRY const plug_desc* plug_entry(const plug_host* host, uint32_t abi)
{
    if ((abi >> 16) != PLUG_ABI_MAJOR || !host) return 0;
    plug_rt_host = host;
    return &kDesc;
}
```

**Build and run it on the Mac:**

```bash
export TOOLCHAIN=$PWD/agt/10.3/bin/arm-none-eabi-
python3 synth-open/plug/build_plug.py --id org.example.fx.tilt --kind master --version 1.0.0 \
    --opt "-O2 -ffp-contract=off" --out tilt.plg synth-open/plug/examples/tilt.c
/tmp/phzvenv/bin/python -I synth-open/plug/emu_plug.py tilt.plg --param 1=4
```

The build prints `Tilt, 3 params, image 664 B (text 664, rw 0), bss 8 B, 16 rebase words`. The emulator prints `instructions per block min/avg/max 2855/2855/2855 (0.55% of a 522,240 cycle block at 1 cycle each)` and, with default settings, `0 of 8192 left samples changed by the effect`, which shows the plugin is transparent at 0 dB and 0 tilt. Copy `tilt.plg` to `/Plugins/Master/`, open the Inserts page and pick it.

---

## 9. Worked Example: `sawf.plg`, a One Oscillator Saw Synth

Source: [`synth-open/plug/examples/saw_synth.cpp`](../synth-open/plug/examples/saw_synth.cpp). One band limited saw or square oscillator through a resonant low pass (a Chamberlin state variable filter), a 2 ms attack and an adjustable release. Six parameters (**Wave**, **Cutoff**, **Reso**, **Tune**, **Release**, **Volume**), four sections and three default macros.

**1. Numbers and voice state.** The framework hands every voice a zeroed 128 byte `state`.

```cpp
#define PLUG_SYNTH_AUTHOR "example (MIT)"   // the descriptor's author line, before the include
#include "synth_plug.h"
#include <math.h>

constexpr int32_t kNumber = 0x64880b9c;   // FNV-1a of "org.example.synth.sawf": stored, patch id and type id

enum Param : int { kWave, kCutoff, kReso, kTune, kRelease, kVolume, kParamCount };

struct VoiceState
{
    float phase, low, band;   // oscillator phase 0..1, filter state
    float env;                // amplitude 0..1
    float pitch;              // the note in semitones (MIDI numbering, 69 = 440 Hz)
    uint8_t released;
};
static_assert(sizeof(VoiceState) <= kSynthVoiceStateBytes, "voice state too large");
```

**2. Note events.** `noteOn` zeroes the state unless the voice is reused. `noteOff` only marks the release. `v.level` is the amplitude level for voice stealing.

```cpp
void noteOn(SynthVoice& v, const SynthParams&, const SynthNote& n) override
{
    VoiceState& s = v.as<VoiceState>();
    if (!n.reused) __builtin_memset(&s, 0, sizeof(s));
    s.pitch = n.pitch;
    s.released = 0;
    v.level = 1.0f;
}

void noteOff(SynthVoice& v, const SynthParams&) override { v.as<VoiceState>().released = 1; }
```

**3. `render`.** Per block it reads the parameters from `p.end`, computes the oscillator step and the filter coefficient once (`exp2f` and `sinf` are host calls, once per voice and block, not per sample), then runs the sample loop. The output is **added** to `L` and `R` and multiplied by the framework's gain ramp `g`. It returns `false` when the release has finished.

```cpp
bool render(SynthVoice& v, const SynthParams& p, float g0, float gInc, float* L, float* R) override
{
    VoiceState& s = v.as<VoiceState>();
    const float* e = p.end;   // this block's values, macros and automation already applied
    const float hz = 440.0f * exp2f((s.pitch + e[kTune] + v.bend - 69.0f) * (1.0f / 12.0f));
    const float dt = clampf(hz / kSampleRate, 0.0001f, 0.45f);
    float cut = e[kCutoff];
    if (v.cutoffOct != 0.0f) cut *= exp2f(v.cutoffOct);   // the track's Performance filter offset, in octaves
    const float fc = clampf(cut, 40.0f, 7000.0f);         // Chamberlin filter, stable up to fs / 6
    const float f = 2.0f * sinf(3.14159265f * fc / kSampleRate);
    const float q = 1.0f - 0.9f * clampf(e[kReso], 0.0f, 1.0f);
    // ... attack and release steps, locals for phase, low, band, env and g ...
    for (uint32_t i = 0; i < kBlock; i++)
    {
        // oscillator with a polynomial band limited step (saw or square), then:
        low += f * band;
        const float high = x - low - q * band;
        band += f * high;
        // envelope, then:
        const float o = low * env * g;
        L[i] += o;
        R[i] += o;
        g += gInc;
    }
    // store phase, low, band, env back into s
    v.level = env;
    return !(s.released && env <= 0.0f) && !v.fading;
}
```

**4. Metadata.** Plain constant arrays. The `Volume` parameter is named in `EngineDef::volumeParam`, so the framework applies it and the engine ignores it.

```cpp
const char* const kWaves[] = {"Saw", "Square"};

const ParamDef kParams[kParamCount] = {
    //  name       default  min     max     format         curve                enum names
    {"Wave", 0.0f, 0.0f, 1.0f, Fmt::kEnum, CurveKind::kLinear, kWaves},
    {"Cutoff", 3000.0f, 40.0f, 7000.0f, Fmt::kHz, CurveKind::kExp, nullptr},
    {"Reso", 0.2f, 0.0f, 1.0f, Fmt::kPct, CurveKind::kLinear, nullptr},
    {"Tune", 0.0f, -24.0f, 24.0f, Fmt::kSemi, CurveKind::kLinear, nullptr},
    {"Release", 0.25f, 0.01f, 2.0f, Fmt::kSec, CurveKind::kExp, nullptr},
    {"Volume", 1.0f, 0.0f, 2.0f, Fmt::kPct, CurveKind::kLinear, nullptr},
};

const uint8_t kOscIds[] = {kWave, kTune};
const uint8_t kFilterIds[] = {kCutoff, kReso};
const uint8_t kAmpIds[] = {kRelease};
const uint8_t kVoiceIds[] = {kVolume};
const SectionDef kSections[] = {
    {"Oscillator", "Osc", kOscIds, sizeof(kOscIds)},
    {"Filter", "Filter", kFilterIds, sizeof(kFilterIds)},
    {"Amplifier", "Amp", kAmpIds, sizeof(kAmpIds)},
    {"Voice", "Voice", kVoiceIds, sizeof(kVoiceIds)},
};

const MacroDef kMacros[] = {
    {"Cutoff", false, 1, {{kCutoff, 0.5f}}},
    {"Reso", false, 1, {{kReso, 0.5f}}},
    {"Release", false, 1, {{kRelease, 0.5f}}},
};
```

**5. The definition and the plugin line.**

```cpp
namespace polysynth::open
{
const EngineDef kSawDef = {
    "SAWF", kNumber, kParams, kParamCount,
    kSections, int(sizeof(kSections) / sizeof(kSections[0])),
    kMacros, int(sizeof(kMacros) / sizeof(kMacros[0])),
    kVolume,       // the framework applies this parameter (and Macro 7) as the gain ramp passed to render
    4,             // voices per instrument, 1..4
    &sawEngine,
    false,         // no slot effect
};
}

PLUG_SYNTH_PLUGIN("SAWF", polysynth::open::kSawDef, kNumber, kNumber)
```

**Build and check it:**

```bash
export TOOLCHAIN=$PWD/agt/10.3/bin/arm-none-eabi-
cd synth-open
python3 plug/build_plug.py --id org.example.synth.sawf --kind synth --version 1.0.0 \
    --opt "-O2 -ffp-contract=off" --synth 0x64880b9c,0x64880b9c,0x64880b9c \
    --out sawf.plg -I . -I ot -I ../synth/include plug/examples/saw_synth.cpp
/tmp/phzvenv/bin/python -I tools/validate_synth.py sawf.plg
```

The build prints `SAWF, 0 params, image 2124 B (text 2120, rw 4), bss 108 B, 77 rebase words` (the descriptor of a synth plugin lists no parameters, the extension carries them). The validator prints `sawf.plg PASS  SAWF params=6 sections=4 macros=3 voices=4 fmts=[0, 2, 3, 5, 8]` and `1 of 1 synth plugins PASS`. Then render it through the harness, a fixed four voice chord. The worst block is 23,243 instructions (4.45 percent of a block) with the defaults (scenario 0) and 29,410 (5.63 percent) with every parameter moved and ramped (scenario 1):

```bash
python3 plug/build_plug.py --id ff.test.synthharness --kind offline --opt "-O2 -fno-builtin" \
    --out h.plg -I . -I ot -I ../synth/include plug/synth/synth_harness.cpp
/tmp/phzvenv/bin/python -I plug/emu_synth.py sawf.plg h.plg --scenario 0
```

Copy `sawf.plg` (and optionally a `sawf.plgi` logo) to `/Plugins/Synth/`. `SAWF` appears in the synth model list. Use the same numbers for `--synth` as in the source. The example has not been run on a device.

---

## 10. Memory and CPU Budgets

### Time

One audio block is **128 frames at 44,117.647 Hz**, which is **522,240 cycles** at 180 MHz. Everything the Tracker does in the audio path has to fit in it.

- **Insert watchdog**: a slot that takes more than **60,000 cycles** (11.5 percent of a block) three blocks in a row is bypassed until it is loaded again. The Inserts page shows `CPU:<name>`. Add the host's conversion pair (6,496 cycles measured, shared by the two slots).
- **Synth governor**: the shim cuts synth voices at a block of 98 to 100 percent load or an average of 80 percent, and restores them after the load stays low. Only synth voices are cut.
- **Voices**: at most 4 per part, 3 parts and one shared pool that defaults to 4.

### How to measure CPU

**In the emulator (no device needed).** `emu_plug.py` and `emu_synth.py` count executed instructions per block. One instruction counts as one cycle, with no wait states and no FPU latencies. That is a **lower bound**. On the device the Dirt insert measured about 1.5 times its emulated instruction count, so multiply by 1.5 until you have a device reading.

```bash
/tmp/phzvenv/bin/python -I synth-open/plug/emu_plug.py my.plg --param 0=50     # instructions per block min/avg/max
/tmp/phzvenv/bin/python -I synth-open/plug/emu_synth.py my.plg h.plg --scenario 1   # worst block, every parameter moved and ramped
```

The firmware's own engine bench (`synth-open/tools/phz_cycles/run_bench.sh`) uses an instruction cost table instead and reads about 5 percent above the device (Dirt block 21,786 emulated, 20,685 on the device). It is built for the engines listed in `engines.mk`, not for external plugins.

**On the device.** Create an empty file named `/.develop` in the SD card root. The Inserts page title then shows `Inserts a.b% c.d%`, the cycles of the last block of insert 1 and insert 2 as a percent of 522,240. The Config screen shows the min, max, avg and last audio load of the whole block. A synth engine has no readout of its own, so measure it with the Config screen: note the figure with the part silent and with 4 voices held.

### Memory

| Resource | Insert 1 | Insert 2 | Synth engine |
| :--- | :--- | :--- | :--- |
| **Code** | SRAM_L slot, 8,192 B for image + bss. No wait states | FlexNVM `0x1001E000`, text up to 8,192 B (the last two 4 KB sectors of the 128 KB data flash). Erased and programmed when a different plugin is loaded there. No flash command when the bytes are already there | FlexNVM bank `0x10000000` to `0x1001E000`: 120 KB, 30 sectors of 4 KB |
| **Writable data** | in the same slot | `rw + bss` up to **1,024 B**, fixed SRAM buffer. More is refused (error 4) | rw + bss from a **3,072 B** SRAM pool shared by the installed engines |
| **Instance** | up to 16 KB from the SRAM heap | same | none (an 8 byte scratch). Per voice 128 B, per part 384 B |
| **Big tables, delay lines** | `alloc(bytes, 1)`: SDRAM arena, 32 KB per slot | same | `work_bytes`: 16 KB SDRAM arena shared by the engines in use at the same time |
| **Limit on the file** | `image + bss` 8,192 | text 8,192 | `image + bss` 32,768 (staging buffer) |

Notes:

- **FlexNVM and SRAM_L run code at flash speed** (device bench). The host runs code only from these two. SDRAM is for data: an uncached access costs about 20 to 30 cycles on the MK66.
- **Whole sectors.** An engine takes whole 4 KB sectors, and a sector never holds two engines, so an install never erases a sector of an engine that may be playing. The thirteen shipped engines need 50 whole sectors (173,428 B packed) and the bank has 30 (122,880 B), so the bank is a **cache** (library of up to 16 files in `/Plugins/Synth`, up to 12 installed at once, least recently used engines out first, never an engine a part plays). At most 3 engines play at once (3 parts).
- **Install cost.** Each flash erase runs with interrupts off and can drop a few audio blocks. An engine of 5 sectors needs up to 5 erases, none on blank sectors. One erase plus 282 phrase programs took about 15.4 ms. The biggest engine (8-Type, 5 sectors) is estimated at 0.1 to 0.2 s including the card read. An engine that returns to the sectors it left only writes what another engine overwrote.
- **Flash wear.** Every build with changed bytes erases the sectors it lands in. `installed.txt` keeps an erase counter per sector. While developing, test an insert in **insert 1** (SRAM_L, no flash writes) and test the DSP on the Mac before you copy it to the card.
- **SRAM heap** is shared with the rest of the firmware. Keep instances small and put large tables in SDRAM.

### Stack

The MK66 has no fixed stack. The heap is sized to the stack pointer minus a 8,192 B margin, which leaves about **8,440 B** between the stack and the heap. The main loop and every interrupt use this one stack, **and the audio interrupt runs on top of whatever the main loop was doing**. A plugin's `process`, `set_param` and `render` run in that interrupt.

From our static call graph walk of the PLUGS image:

- The audio interrupt with the deepest shipped synth plugin (8-Type, 4,180 B) and Dirt (632 B) is **5,852 B** in total. With the exception frame (108 B) and preempting interrupts (384 B) it is **6,344 B**.
- The main loop's worst chain is about 2,152 B. Against 8,440 B that leaves -56 B at the walker's bound and about +288 B in practice. **There is no stack to spare.** Dirt is the deepest shipped insert (632 B) and Lo-Fi uses 48 to 144 B.
- The practical rule: do not go deeper than the deepest shipped plugin of your kind, 632 B for an insert and 4,180 B for an engine, counting everything it calls. Do not put arrays on the stack.

To check your own plugin, compile it with `-fstack-usage` and add the frames along the deepest call chain by hand. The call graph walker we used is not in this repository.

The firmware moved the large buffers of the main loop (image loads, sample copies, project file images) to SDRAM so the shared stack stays shallow (`core/libs/NewDisplay/mainLoopScratch.h`). That work leaves exactly the margin above, not more.

### CPU of the shipped engines

Emulated with the cost table bench, 4 voices playing the chord 57 61 64 68, in percent of a block. Default patch and worst case (every parameter at max, mid or min) with the engine DSP at `-O2` (`-O3` for FM4 and B-Type). Estimates, not device readings:

| Engine | Default | Worst | Engine | Default | Worst |
| :--- | ---: | ---: | :--- | ---: | ---: |
| JU6 | 18.6 | 21.4 | 8-Type | 14.8 | 42.5 |
| PHZ (private) | 17.4 | 23.5 | Hand-Type | 9.3 | 9.3 |
| FM4 | 16.6 | 16.8 | B-Type | 8.8 | 16.2 |
| D-Type | 25.9 | 34.9 | Arc-Type | 21.0 | 27.7 |
| CS-Type | 30.5 | 46.3 | PLK (private) | 5.6 | 6.3 |
| A-Type | 22.5 | 27.3 | T-Type | 9.2 | 9.2 |

Three parts each asking for 4 voices is 12 voices on paper. Only the pool (default 4) keeps the total from exceeding the figures above.

Shipped inserts, emulated with `emu_plug.py` at default settings, instructions per stereo block (average, built with GCC 12.3.1, see [section 3](#3-toolchain-and-building)):

| Insert | Instructions | Percent of a block |
| :--- | ---: | ---: |
| Dirt | 14,721 | 2.82 |
| Tape | 15,992 | 3.07 |
| Lo-Fi | 16,452 | 3.15 |
| Console (private) | 8,794 | 1.68 |
| Tube (private) | 9,681 | 1.85 |
| BBD | 11,762 | 2.25 |
| Phaser | 7,855 (up to 9,181) | 1.76 |
| `tilt.plg` (example) | 2,855 | 0.55 |

Lo-Fi and Tape keep their coefficients and filter state in locals for the block, and the older `plug/README.md` table (Lo-Fi 18.4 k, Tape 16.3 k) predates that change. Phaser's count depends on its mode and on whether a ramp is running. These are instruction counts, so add the margin of [How to measure CPU](#how-to-measure-cpu).

---

## 11. Engine Logos (`.plgi`)

An engine can show a logo in the model list (80x29) and in the patch editor header (108x40), in the stock lettered style and the theme colour. Without one it shows its name as text.

A logo is a **PLGI record**, 1,676 bytes: a 16 byte header and two FT812 `L2` alpha masks (2 bit alpha, pixel 0 in the top two bits of a byte, rows padded to whole bytes).

```text
 0  u32 magic "PLGI" (0x49474C50)       8  u8 med width 108
 4  u8  version 1                       9  u8 med height 40
 5  u8  format 2 (L2)                  10  u16 flags 0
 6  u8  mini width 80                  12  u32 CRC-32 of the 1,660 pixel bytes
 7  u8  mini height 29                 16  mini 80x29 (stride 20, 580 B), then med 108x40 (stride 27, 1,080 B)
```

It comes from one of two places, in this order:

1. a **sidecar** `/Plugins/Synth/<name>.plgi` next to `<name>.plg` (the whole file is the record, any other size is refused)
2. a **trailer** inside the `.plg` after the rebase table, named by `plug_manifest.reserved[0]` (`build_plug.py --logo x.plgi`). The record has its own CRC and the file CRC does not cover it

A record that is missing, truncated, the wrong size or damaged goes to the next source, then to the text name. Nothing can stop the picker. The host keeps the logos in a RAM_G cache (32 list images, 2 editor images, least recently used reused): the list images of the first 32 listed engines at boot, the others when the picker comes near them, the editor image when the patch editor opens. Every listed engine shows its logo, however many there are. `/Plugins/synth.txt` lists what it used (`logo <file> sidecar|trailer|text err <n>`).

### `make_plgi.py`

[`synth-open/tools/make_plgi.py`](../synth-open/tools/make_plgi.py) is pure Python (no Pillow):

```bash
python3 -I synth-open/tools/make_plgi.py --text SAWF -o sawf.plgi --preview sawf.png   # a text logo in the stock style
python3 -I synth-open/tools/make_plgi.py --png logo.png -o sawf.plgi                    # one PNG, scaled to both sizes
python3 -I synth-open/tools/make_plgi.py --mini m.png --med d.png -o sawf.plgi          # one PNG per size
python3 -I synth-open/tools/make_plgi.py --check sawf.plgi                              # check a record or a .plg trailer
python3 -I synth-open/tools/make_plgi.py --selftest                                     # round trip of the stock logos
```

White (or opaque, for a PNG with alpha) is drawn in the theme colour. Black (or transparent) is not drawn. `tools/plgi_sheet.py` stacks previews into one sheet. `plug_logo_test.cpp` ([section 14](#14-testing-on-the-mac)) checks records the way the firmware reads them.

---

## 12. Sidecar and Status Files

"Sidecar" files sit next to something else on the card. The host writes most of them and you read them to find out what happened.

| File | Written by | Content |
| :--- | :--- | :--- |
| `<project>/plugins.txt` (and `Workspace/plugins.txt`) | the host, on project save | The inserts of the project, below |
| `/Plugins/Synth/<name>.plgi` | you | A logo ([section 11](#11-engine-logos-plgi)) |
| `/Plugins/host.txt` | the host, after every insert load | Index size, scan result, rejected files with error numbers, the plugin list with uid and version, each insert's state, error, text and instance address, cycles and max cycles |
| `/Plugins/synth.txt` | the host | Library and bank counts, then one line per file with its name, stored number, state (`on card`, `installed`, `attached`, `not listed`), flash address and `err` code, the erase count of every sector, the last 8 installs (ms, flash commands, erases, evicted) and the logo source and RAM_G use |
| `/Plugins/installed.txt` | the host | The index of the FlexNVM engine bank, with CRCs and erase counters. A damaged line is dropped and the engine is installed again |
| `/Plugins/.loading` | the host | The crash marker, [section 13](#13-validation-safe-mode-and-the-crash-marker) |
| `Patches/<NAME>/*.patch`, `<N>-<n>.patch`, `<N>.names` | the host | Synth patches ([section 7](#presets-and-patches)) |

### `plugins.txt` format

```text
tffplug 1
format 2
mixer 10
slot 1 3a51c2e0 00010000 Tape
p Drive=30
p Mix=100
slot 2 ea8c2de8 00010000 Tilt
p Tilt=3
```

- `tffplug 1` first, else the file reads as empty. `format 2` is written by this host. `mixer <n>` appears when the stage is not Hard Clip (1 Soft Clip, 10 Soft Knee).
- `slot <1 or 2> <uid hex> <version hex> <name>`, then one `p <param>=<value>` line per parameter, **by name**, until the next slot line.
- Unknown lines are skipped. A value that does not parse is skipped. The file is at most 2,560 bytes.
- A project with no plugin and a Hard Clip mixer stage has no file. The binary project is unchanged: the `Inserts` Saturation entry is the reserved value `0x80`, which stock firmware loads as Hard Clip, so a project still opens on stock firmware without the inserts.
- Writes go through `<path>.tmp`, are read back, and the old file is kept as `<path>.bak` until the new one is in place. A read error is not taken for an empty file.

### Synth error codes in `synth.txt`

| `err` | Meaning |
| :--- | :--- |
| 1 | `plug_entry` returned `NULL` (a refused ABI, or a host without `log2f`) |
| 2 | descriptor failed `checkDesc` |
| 3 | `init` returned non-zero |
| 4 | the `PLUG_EXT_SYNTH` extension failed `checkSynth` |
| 5 | no working RAM for `work_bytes` |
| 6 | not listed: a name the shim cannot show, or numbers a built-in or earlier engine already has |
| 7 | not installed, or safe mode |
| 8 | the extension's numbers or name are not the manifest's |
| 9 | no numbers in the manifest (rebuild with `--synth`) |
| 11 | install: image or rw larger than the bank or pool |
| 12 | install: no room next to the engines the parts play |
| 13 | install: the `.plg` did not load (gone, changed, unreadable) |
| 14 | install: `installed.txt` could not be written (no flash command was issued) |
| 15 | install: flash erase, program or verify failed |

### Insert error numbers (`host.txt`, `Fail:`)

1 no file, 2 header (magic or header size), 3 ABI (major, `min_host`, cpu, kind or flags), 4 too big (image, bss or layout against the slot), 5 read, 6 CRC, 7 rebase, 8 entry returned `NULL`, 9 descriptor, 10 commit (FlexNVM write failed), 11 init.

---

## 13. Validation, Safe Mode and the Crash Marker

### `validate_synth.py` is the acceptance gate for engines

```bash
TOOLCHAIN=$PWD/agt/10.3/bin/arm-none-eabi- /tmp/phzvenv/bin/python -I synth-open/tools/validate_synth.py DIR_OR_PLG...
```

[`validate_synth.py`](../synth-open/tools/validate_synth.py) cuts the firmware's real `checkSynth` out of `plug_synth.cpp` as it is, cross compiles it for the MK66, loads each `.plg` the way the firmware's attach does (rebased for its FlexNVM home, rw in SRAM, entry, `init`, `query(PLUG_EXT_SYNTH)`) and runs the real check in the Unicorn emulator. No device is needed. **An engine it rejects is listed in the picker but shows `Missing` when picked (attach error 4), and no other test sees it**, because `emu_synth.py` and the render compare never call `checkSynth`. Exit code 0 only when every synth plugin passes.

What `checkSynth` checks:

- the extension lies inside the engine's text or rw, `struct_size` is the host's and `layout` equals the firmware's `PLUG_SYNTH_LAYOUT(sizeof(SynthVoice), sizeof(SynthParams), sizeof(SynthNote), sizeof(SynthSlotScratch))`
- name 1 to 8 characters with no `/`, at most 84 parameters, at most 10 sections, at most 6 macros, 1 to 4 voices, a valid `volume_param`
- `note_on`, `note_off` and `render` are Thumb functions in the text. The optional hooks, if set, too
- every parameter: name in the text, `fmt < PLUG_FMT_COUNT` (16), `curve < PLUG_CURVE_COUNT`, `max >= min` and for an enum `min == 0`, `max <= 64` and all `max + 1` names in the text
- every section: names and the id array in the text, every id below the parameter count
- every macro: name in the text, at most 2 targets, each target below the parameter count

It also prints the facts (sections, formats used), and fails when a plugin uses a format the firmware has only for a built-in engine.

`build_plugs.sh` runs it on the example, and `engines/crosscheck.sh` on the shipped engines of the four module repositories.

### Crash marker and safe mode

Before a plugin starts, the host writes **`/Plugins/.loading`** with its name. The marker is removed once the plugin has run cleanly for 3 seconds. If the Tracker dies in between, the marker is still there at the next boot.

- **Inserts**: with the marker present, the next boot loads no insert on its own. The Inserts page title says which one crashed (`Safe mode: <name> crashed`). Holding **Shift** during the first 4 seconds after power on does the same. Picking a plugin by hand still works in safe mode. A project that names an insert shows `Safe:<name>`, keeps its values and saves them back.
- **Synth engines**: with the marker present at boot, no engine is attached for that session. The engine list is still shown and the parts that use a plugin engine are silent (`Missing`). An install in safe mode fails with err 7. **Shift at power on does not skip synth plugins**, only the marker does.
- For a synth engine the marker covers the attach (the entry point, `init` and the check), not `render`. A fault later in a render call does not leave a marker.
- Safe mode lasts one session. The marker is deleted during that boot and the next power cycle loads normally.
- Slot state after a failure: `Fail:<name>` (the file is there but did not load, `slotError` has the number), `CPU:<name>` (tripped the watchdog), `Miss:<name>` (not on the card).

A plugin that hangs `init` or the first block needs a power cycle. The next boot is a safe one. Pick `None` or another plugin on the Inserts page, or take the file off the card with a card reader. Keep a spare card with known good plugins while you develop.

---

## 14. Testing on the Mac

You can test almost everything without a device. The tools need `unicorn` for the emulators and a C++ compiler for the host tests.

| Tool | What it does |
| :--- | :--- |
| `plug/build_plugs.sh` | Builds the examples and runs every check below that applies. The module repositories run the same checks on the shipped plugins |
| `plug/emu_plug.py` | The firmware's loader in Python and your insert's ARM code in Unicorn. Checks the header, CRC and descriptor, runs `init` and `process` on 128 frame blocks, prints instructions per block and an output checksum, writes a wav (`--wav`), reads a wav (`--inwav`, 16 bit stereo 44.1 kHz). `--base` picks the home (SRAM_L `0x1FFF0BD0`, FlexNVM `0x1001E000` with `--rw-base 0x20026C00`, SDRAM `0x70000000`). The checksum **must not depend on `--base`**. `--param I=V` sets a parameter after `init`, `--param-at BLOCK:I=V` sets one just before that block to test ramps and crossfades |
| `plug/emu_synth.py` | Renders an engine plugin through `synth/synth_harness.cpp` (a fixed chord, a parameter script) and prints a checksum, the libm calls, the worst block and the manifest numbers. `--scenario 0` is the defaults, `1` has every parameter moved and ramped. Same checksum at the FlexNVM and SRAM homes |
| `tools/validate_synth.py` | The firmware's `checkSynth`, [section 13](#13-validation-safe-mode-and-the-crash-marker) |
| `tools/plug_index_test.cpp` | The `/Plugins/Master` scan and the header, layout and CRC checks on a folder of real `.plg` files |
| `tools/plug_scan_cache_test.cpp` | When the Inserts page scans `/Plugins/Master` again: an unchanged folder is not scanned, an added, removed, renamed or resized file is, and so is any folder after a USB storage session or a card change |
| `tools/plug_bank_test.cpp` | The FlexNVM bank on a simulated flash that loses power after any command. Prints how many engines fit at once at the shipped sizes |
| `tools/plug_logo_test.cpp` | The PLGI parser and the sidecar/trailer order. Give it `.plgi` or `.plg` files |
| `tools/plug_sidecar_test.cpp` | The `plugins.txt` codec and file rules on a card that fails or loses power |
| `tools/plug_ui_nav_test.cpp` | The Inserts page list index logic |
| `tools/run_smoke.sh` | The host smoke test of the open shim for a set of built-in engines. It never loads a `.plg` |
| `tools/run_tests.sh` | Every host check above in one run (5 smoke variants, `build_plugs.sh`, the `plug_*` tests against the pinned modules), ends with `RESULT: GREEN` or `RESULT: RED`. `run_host_tests.sh` is the same without `build_plugs.sh`, against `$PLUGDIR` |

A plugin that is correct on the Mac can still fail on the device in ways the emulator cannot show: the emulated libm comes from Python and rounds each result to `float32` once, instruction counts are a lower bound and nothing models flash wait states or the shared stack. Treat the Mac result as necessary and not sufficient.

---

## 15. Pitfalls from Real Bugs

**Stale pointers after a plugin is unloaded.** An insert's parameter names and enum names live in the plugin's text segment. The Inserts page keeps lists that point at them. Insert 2's text is in FlexNVM and a reload **erases the sector**, so a hidden list read `0xFFFFFFFF` on the next redraw and the Tracker faulted. The host now notifies the UI from its unload path before the slot memory is reused and the labels copy their strings. For you: keep every table `const` in the text segment, never point the host at strings built in rw and never keep a pointer into another plugin or into host memory across calls. Synth engines are detached (parts cut, parameters removed) before their flash or rw bytes change, because the host points at your arrays directly.

**Writable data in insert 2.** Insert 2 has only 1,024 B for `rw + bss`. A plugin with a larger writable table is refused for insert 2 (error 4, `kTooBig`) and loads fine in insert 1, which is easy to miss. Declare tables `const` so they stay in the text segment, and keep state in the instance (up to 16 KB from the heap, not counted against the 1 KB). Read the `rw` and `bss` sizes `build_plug.py` prints, and test your plugin in both slots (`build_plugs.sh` runs the emulator at both homes).

**A PC-relative reference between the text and rw segments** does not survive the copy to different addresses. The builder refuses it and names the relocation. The ordinary absolute addresses in the literal pool are fine, they are in the rebase table.

**Stack depth.** The audio interrupt shares the main stack and the margin is thin ([section 10](#stack)). Several 4 to 5 KB frames in the main loop once ran past the heap line when the audio interrupt arrived. For you: no big local arrays in `process` or `render`, no deep call chains, no `printf`.

**`-Os` versus `-O2`, and stale objects.** `make` does not track compiler flags. A build folder from before a default flag change keeps old `-Os` objects, and the image then comes out smaller than it should (one of our first builds ended 0xEB0C4, smaller than the build it was meant to be larger than). After changing a flag default, delete the `core/build-*` folder for that `SYNTH_TAG` and `platforms/mk66/build-trim`, then rebuild. `build_plug.py` always recompiles from source, so a plugin is not affected. A **port of firmware code** is: it must be built with the flags of the firmware object it is compared with ([section 3](#ports-of-firmware-code)).

**Work in `set_param` runs in the audio interrupt.** It runs before the cycle counter starts, so the readout on the Inserts page does not include it, but the audio block does. Several host libm calls in `set_param` still add up. Compute only what the changed parameter needs.

**A silent bus is not an input.** `process` is not called while the bus is silent, so a delay line keeps its old content and an LFO keeps its phase across the silence. A plugin whose state persists resumes from where it stopped, and the shipped BBD and Phaser do. Do not assume the first block after silence starts from a cleared state, and do not use a long tail you cannot afford to cut.

**`reset` is never called** by this host. Clear state in `init`.

**Changing parameter names or order.** Insert values are stored by name: renaming loses the value, reordering is harmless. Synth patches are stored by index and the synth automation follows names: reordering breaks old patches, renaming breaks automation links. Append new synth parameters at the end.

**Volume applied twice.** The framework applies the parameter named by `volume_param` and the velocity law through `g0` and `gInc`. An engine that also scales by Volume is too quiet by the square.

**Numbers already taken.** An engine whose `stored`, `patch_engine` or `type_id` matches a built-in or earlier engine is not listed (`err 6`). An engine built without `--synth` is not listed either (`err 9`).

**Static constructors.** `static` objects with a constructor body, `std::` containers and anything that needs `atexit` are refused by the builder. The synth macros provide an empty `operator delete(void*, unsigned int)` only so a virtual destructor links. Nothing deletes an engine.

---

## 16. Licence Notes for Plugin Authors

These are facts from the files in this repository, not legal advice.

- **The SDK files** in [`synth-open/plug/`](../synth-open/plug/) are **MIT** (`synth-open/plug/LICENSE`, Copyright (c) 2026 sbialy (tracker-ff)). That file names `plug_abi.h`, `plug_rt.c`, `plug.ld`, `build_plug.py`, `emu_plug.py`, `build_plugs.sh` and `README.md` as its scope. The files `synth_plug.h`, `emu_synth.py` and the `examples/` folder are not named in that scope text. The two examples carry their own MIT header.
- **Your plugin** may use any licence that allows shipping it as a separate file (`synth-open/plug/README.md`). The firmware licence is unchanged. A `.plg` is a separate file loaded by the firmware, and the plugin's own `licence` field in the manifest records the SPDX id you choose.
- **The repository `LICENSE`** is MIT with a Wholesale Commercial Clone Restriction. Its Part 2 names `.ptfx` Sample Editor plugins as expressly permitted to sell. It does not mention `.plg` plugins. Part 2 prohibits selling the complete firmware, a substantially unmodified derivative of it or `.ptf` firmware images. It grants no right to the Polyend trademarks.
- **Third party code your plugin may include.** A synth plugin includes `ot/audio/synth_engine.h` and `ot/model/synth_patch.h`, which are **OpenTracker, Apache-2.0** (`licenses/LICENSE-OpenTracker-Apache-2.0.txt`, `NOTICE.md` item 10, each file has a provenance line). The shipped OpenTracker engine plugins (poly-engines-os) and insert ports (poly-effects-os) are **MIT** from opentracker-synths and opentracker-fx (`licenses/LICENSE-OpenTracker-synths-MIT.txt`, `licenses/LICENSE-OpenTracker-fx-MIT.txt`), with Console and Tube original (private modules, source in the private repository `poly-effects`, proprietary freeware like PHZ and PLK, see `NOTICE.md` items 13 to 13b). Each module repository ships the notices of its plugins with its release files (section 18). Check the headers you include and the notices that come with them before you distribute a binary.
- **Origin of the examples.** `tilt.c` and `saw_synth.cpp` are original code written for this guide. The filter is the textbook Chamberlin state variable filter and the band limiting is the textbook polynomial step. No third party source was copied.
- The firmware is unofficial, noncommercial community work for the Original Tracker. It is not affiliated with or supported by Polyend.

---

## 17. The Browser Twin (`.wasm`)

A plugin can ship a second file for the browser editor: `<id>.wasm`, built from the same sources as the `.plg`, plus a manifest `<id>.json`. The editor loads it at run time, so it can play an engine or an insert without having its source. The format is in [`synth-open/plug/plug_wasm.h`](../synth-open/plug/plug_wasm.h) (module ABI 1.0).

- **Same plugin ABI.** The module exports `plug_entry()` and returns the same `plug_desc` (and for a synth engine the same `plug_ext_synth`) as the `.plg`. wasm32 has 32 bit pointers like the Cortex-M4, so every struct keeps its layout. It also exports `plug_wasm_abi()`.
- **No imports but the five.** The module is position independent (a `dylink.0` section, `emcc -sSIDE_MODULE=2`) and imports only `env.memory`, `env.__indirect_function_table`, `env.__memory_base`, `env.__table_base` and `env.__stack_pointer`. libm and the memory functions come through the host table, as on the device: link [`plug_rt_wasm.c`](../synth-open/plug/plug_rt_wasm.c) instead of `plug_rt.c`.
- **Refused** when the manifest or `plug_wasm_abi()` names another module ABI major, when the plugin ABI major differs, when `min_host` is above the host level, when the size or sha256 differ from the manifest or when the module has any other import.
- **Sound.** Built with the editor's compiler and flags, a module renders bit identical to the same source linked into the editor. It is not bit identical to the device: the device runs the ARM build with its own libm and GCC fuses some multiply-adds on the Cortex-M4 that wasm cannot fuse (a one LSB difference in Dirt).
- **Protection.** A `.wasm` file can be decompiled like a `.plg`. Ship it stripped (no name section, no DWARF, no source map), but do not treat it as secret.

Build it with `emcc` (the editor pins the version) and check it with the editor's loader. The manifest fields are listed in the header of `plug_wasm.h`.

---

## 18. Where the Shipped Plugins Are Built

This repository holds the plugin host and this SDK, but no plugin module. tracker-ff 1.2.0 ships plugins from four module repositories:

| Repository | Visibility | Plugins |
|---|---|---|
| poly-engines-os | public | the synth engines JU6, FM4, B-Type, D-Type, CS-Type, A-Type, T-Type, 8-Type, Hand-Type, Arc-Type |
| poly-engines | private | the synth engines PHZ and PLK (freeware binaries) |
| poly-effects-os | public | the master inserts Dirt, Tape, Lo-Fi, BBD, Phaser |
| poly-effects | private | the master inserts Console and Tube (freeware binaries) |

[`engines/engines.json`](../engines/engines.json) keeps the identity of every shipped module (file id, name, engine numbers, version, repository), [`engines/external.lock.json`](../engines/external.lock.json) pins a release of each repository by tag, commit and the sha256 of every file, and [`engines/README.md`](../engines/README.md) shows how to fetch, check and package them.

**The SDK copy.** A module repository does not point at a tracker-ff checkout. It vendors a pinned copy of the SDK files it compiles against in `sdk/files/`, at their tracker-ff paths (for an engine: the files of [`engines/sdk/headers.txt`](../engines/sdk/headers.txt) and [`engines/sdk/tools.txt`](../engines/sdk/tools.txt) plus `synth-open/plug/LICENSE`, for an insert: the `plug/` files it needs), and names the tracker-ff commit it took them from in `sdk/SOURCE`. Its build reads nothing outside its own checkout, so a clone of the module repository alone builds the published bytes. With a tracker-ff checkout next to it, `make sdk-check FW=../tracker-ff` shows any drift and `make sdk-sync FW=../tracker-ff` takes the new files. The firmware owns these files: an SDK change is made here first, then synced into the module repositories, rebuilt and (when bytes changed) given a new module version. This is chosen over pointing at a firmware checkout because the module build then cannot silently change when someone updates their firmware clone, and a published tag of a module repository rebuilds to the same bytes forever.

**A plugin of your own** can use the same layout: copy `poly-effects-os` (an insert) or `poly-engines-os` (a synth engine), replace the sources and the table, keep `sdk/files/` in sync with the tracker-ff release you target. A synth engine needs numbers no other engine uses (section 15), a new shipped engine of tracker-ff gets its row in `engines/engines.json` and its `engine_list.h` entry first.

