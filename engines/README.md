# engines/ - the synth engine .plg set, built on its own

Ten custom synth engines (JU6 FM4 D-Type CS-Type A-Type T-Type 8-Type Hand-Type B-Type Arc-Type) are built here as runtime
`.plg` plugins. Two more, PHZ and PLK, are our own code and are built from the private repository `poly-engines`, their release
files are checked against the numbers kept here (`build_set.py verify-external`). This folder builds, checks, versions and releases
the public ten **without building or touching the firmware**. The
firmware image build in turn needs none of the engines (the image carries the stock six: ACD FAT VAP WTFM PERC CHIP).

```bash
# the release set (AUTO-F: -ffp-contract=off on every engine) into build/engines-autof
make -C engines TOOLCHAIN=/path/to/agt/12.3.rel1/bin/arm-none-eabi- PY=/tmp/phzvenv/bin/python
make -C engines TOOLCHAIN=... FLAVOUR=fused OUT=/tmp/set       # the compiler default, the unpublished fallback
make -C engines verify OUT=build/engines-autof                 # SHA256SUMS, SET.json, ABI against the firmware's plug_abi.h
make -C engines package OUT=build/engines-autof                # the release zip
make -C engines check-sdk TOOLCHAIN=...                        # the file lists still match the sources
engines/crosscheck.sh build/engines-autof                      # needs a firmware tree: checkSynth, render compare, logo tests
```

`TOOLCHAIN` has no default (a standalone build never guesses a compiler). `PY` is a Python with `unicorn`: without it the render
checks are skipped and `SET.json` says `render_checks: skipped`. Apple make 3.81 is enough. Takes about 90 s.

## What a build writes

`OUT/` is the whole shippable set. Copy it to `/Plugins/Synth/` on the card (older `.plg` files without the engine number in the
header are not listed, so copy the whole folder).

| File | What |
|---|---|
| `<id>.plg` x12 | the engines, each with its own version in the `.plg` header (`version` in `engines.json`) |
| `<id>.plgi` x12 | the card logo sidecars, `previews/` the PNGs and `sheet.png` |
| `SET.json` | set version, flavour and flags, compiler, SDK version and ABI, digests of the SDK headers and engine sources, one record per engine (version, uid, numbers, sizes, CRC, sha256, render checksums) |
| `SHA256SUMS` | every other file, `shasum -a 256 -c SHA256SUMS` |

No date, host name or path goes into any file: the same inputs give the same bytes, so a rebuild is a diff.

## The browser modules (.wasm) of the public engines and inserts

```bash
make -C engines wasm                                  # build/modules: <id>.wasm + <id>.json for every PUBLIC engine and insert
make -C engines wasm WASM_OUT=/tmp/m                  # the same into another directory (the private modules are built by their own repositories)
make -C engines verify-inserts INS=build/plugs TOOLCHAIN=...   # engines.json "inserts" give the bytes of synth-open/plug/build_plugs.sh
```

`engines/wasm/build_modules.py` builds the `.wasm` twin of a plugin for the browser editor (format: `synth-open/plug/plug_wasm.h`,
runtime `plug_rt_wasm.c`), checks it (only the five module imports, `plug_entry` and `plug_wasm_abi` exported, no name section or
DWARF) and writes the manifest. Needs emsdk 6.0.13 (`$EMSDK`, `agt/emsdk` or `~/prj/poly/agt/emsdk`, the version is pinned in
`wasm/wasm.json` and in every manifest) and an arm64 `clang` (the descriptor is read from the plugin's own sources natively, never an
x86_64 binary). The output is `MODULES.json` (the set), `SHA256SUMS`, `NOTICE.md` and the licence texts the modules name, and is byte
reproducible. `engine_cxxflags` in `wasm/wasm.json` must equal the editor's `scripts/build-wasm.ts` `common`, or an engine module is
not bit identical to the engine linked into the editor.

`engines.json` marks every engine and insert `"visibility": "public"` (source in this repository, artifacts built and published
from here) or `"private"` (our own module, source in the private repository named in `repo`, which builds and publishes the same
files). `make wasm` builds the public ones and lists the private ones in `MODULES.json` under `external_private`.

### Checking the private modules (`EXTERNAL_DIR`)

`EXTERNAL_DIR=DIR engines/crosscheck.sh SET_DIR` and `EXTERNAL_DIR=DIR engines/ci_external.sh` check the published files of PHZ, PLK, Console
and Tube in `DIR` (one flat directory) against the pins of `external.lock.json` and against the numbers, names and versions kept in
`engines.json` (`build_set.py verify-external`). CI fetches them with `external.py fetch` when the repository has a secret
`PRIVATE_RELEASES_TOKEN` that can read the two private repositories, and skips the check cleanly when `EXTERNAL_DIR` is not set.
The tags are local until the private releases are published, until then `external.py fetch --from-local` takes the files from the local
build directories of the checkouts.

## Layout

| Path | What |
|---|---|
| `engines.json` | the table: per engine id, name, sources, flag class, extra flags, numbers (stored, patch, type), version, logo text, visibility, licence. Plus `inserts` (the master insert plugins), the flavours and the set version |
| `wasm/` | `build_modules.py` (the `.wasm` twins and manifests of engines and inserts), `wasm_common.py`, `wasm.json` (emcc pin and flags), `dump_desc.cpp`, `native_compat.h` |
| `build_set.py` | `build`, `verify`, `logo-test` and `shell` (the table as shell functions for the old scripts) |
| `closure.py` | computes the include closure of every engine with the compiler and checks `sdk/*.txt`, the ABI in `sdk/VERSION` and the engine list against `synth-open/engines.mk` |
| `crosscheck.sh` | the checks that need the firmware tree |
| `external.lock.json`, `external.py`, `ci_external.sh` | the private modules as this repository pins them: release tag, commit and sha256 of the published files of `poly-engines` and `poly-effects`. `external.py fetch DIR` collects them (release assets with `gh`, or `--from-local`), `check DIR` verifies them, `source` reads a source file at the pinned tag of a local checkout (the `ui_shot` Console and Tube twins). `ci_external.sh` is the CI step, see below |
| `sdk/VERSION` | SDK version and the plugin ABI (`abi 1.1`, `host_level 1`) the set is built against |
| `sdk/headers.txt` | **the plugin SDK header set**: the 14 headers the engines compile against and the firmware shares (`plug_abi.h`, `synth_plug.h`, `open_engine.h`, `engine_list.h`, `ot/audio/*.h`, `ot/model/synth_patch.h`, `ot/fastrun.h`, `PolySynth/config.h`) |
| `sdk/sources.txt` | the engine owned files: wrappers `plug/synth/*_plug.cpp`, definitions `engine_*.cpp`, DSP in `ot-synths/` |
| `sdk/tools.txt` | the plugin toolchain (`build_plug.py`, `plug_rt.c`, `plug.ld`), the emulators and the logo tools. Written by hand |

A build copies exactly the files of the three lists into a scratch tree and compiles there with the working directory
`synth-open/` of that tree, the way the firmware tree is laid out. A header outside the list is not found and the build fails, so
the dependency on the SDK header set is enforced and not only documented. `make check-sdk` fails when an engine starts to include
a header that is not listed (or a listed file is no longer used), and when `engines.json` and `synth-open/engines.mk` disagree.

## Flavours and flags

| Flavour | OpenTracker modules | FM4 and B-Type |
|---|---|---|
| `autof` (default, the 1.2.0 release) | `-O2 -ffp-contract=off` | `-O3 -ffp-contract=off` |
| `fused` | `-O2` | `-O3` |

Every source also gets the firmware's own flags (`fwflags` in `engines.json`), the wrapper is `-Os`, and 8-Type and T-Type add
their `*_WORK_POOL` define. `SYNTH_PLUG_OPT_ENGINES` and `SYNTH_PLUG_OPT_O3` in the environment replace the first two columns for a
test set. The sets are byte identical to what the in-tree `build_plugs.sh` produced before this folder existed (checked for both).

## Versions and the ABI

- **Set version**: `set_version` in `engines.json`, copied to `SET.json`. **Plugin version**: `version` per engine, written into the
  `.plg` header (`major << 16 | minor << 8 | patch`). Bump an engine's when its bytes change on purpose.
- **SDK version**: `sdk/VERSION`. Bump it when a header in `sdk/headers.txt` or a tool changes what an engine compiles to or the
  layout it shares with the firmware (`SynthVoice`, `SynthParams`, `SynthNote`, `SynthSlotScratch`, `EngineDef`). `abi` and
  `host_level` there must equal `plug_abi.h` (checked by `closure.py` and by every build).
- **What refuses a mismatch** (all of this exists in the firmware, the engine project adds only the set level checks):
  `plug_index.h` (`kAbi`) refuses a `.plg` whose `abi_major` differs from `PLUG_ABI_MAJOR`, whose `min_host` is above
  `PLUG_HOST_LEVEL`, whose cpu is not the Cortex-M4F or whose kind is not the one asked for. The plugin entry also gets
  `(PLUG_ABI_MAJOR << 16) | PLUG_ABI_MINOR` and returns `NULL` for another major. `checkSynth` refuses an engine whose
  `layout` (the sizes of `SynthVoice` and the other shared structs) differs from the firmware's, and the stored, patch and type
  numbers must be unique. The build refuses a set whose header ABI differs from `sdk/VERSION`, `verify --host-abi plug_abi.h`
  repeats the host's header test for every file of a finished set against a given firmware header.
- The `.plg` header's `abi_minor` byte is written as 0 by `build_plug.py` (not the SDK's minor 1) and has been so for every shipped
  file. The host does not read it, so it is left alone to keep the files byte identical. `SET.json` records the real ABI.

## What the firmware needs from the engines, and what it does not

| Firmware side | Needs the engines? |
|---|---|
| Image build (`make original SYNTH_OPEN=1 TRIM=1 PLUG_HOST=1 SYNTH_ENGINES=stock`) | **No.** It compiles the stock six (`ot/audio/engines/*.cpp`, `engine_acd/fat/vap/wtfm/perc/chip.cpp`, `ChiptuneSynthVoice.cpp`) and the plugin host (`plug_*.cpp`, `plug_abi.h`, `open_engine.h`, `engine_list.h`) |
| `EXT_ENGINES=none` | `make` stops if `SYNTH_ENGINES` names one of the ten (`SYNTH_ENGINES_EXT` in `synth-open/engines.mk`). The default is `allow`: a test image can still link an engine in |
| Flash report (`make synth-engines`), render compare (`crosscheck.sh`), `engine_budget` tools | Yes, they compile the engines into an image or into objects on purpose |

So the engine sources stay in `synth-open/` for now, because the same files are the reference the render compare builds
into the firmware. Nothing in the engine build reads any other firmware file.

## Moving this to its own repository (not done, nothing created)

1. Copy `engines/` as the repo root and the files of `sdk/sources.txt` and `sdk/tools.txt` into it, keeping the relative paths
   (`synth-open/...` or drop the prefix and change the stage step in `build_set.py` to read the tree it sits in).
2. Vendor the files of `sdk/headers.txt` the same way and record the firmware commit they came from in `sdk/VERSION`. The firmware
   repo stays the owner of `plug_abi.h`, `synth_plug.h`, `open_engine.h` and `ot/`, so a firmware change to them is a new SDK
   version and a re-sync, `closure.py --check` tells when the lists drift.
3. In the firmware repo: delete the engine sources (and the ten `SYNTH_ENGINE_SRCS_*` lines, `engine_list.h` entries and
   `SYNTH_ENGINES_EXT`), which also ends the in-image test build of a custom engine and the render compare reference. Keep the
   compare as a job that builds the engine repo's objects. `crosscheck.sh` and `validate_synth.py` then take the firmware as a
   checkout path (`FIRMWARE_TREE`, already an argument).
4. Licences: `synth-open/plug/LICENSE` (MIT), `licenses/` for the OpenTracker sources travel with the files. PHZ and PLK are proprietary freeware, their source and `LICENSE.md` are in the private repository `poly-engines`, and PHZ keeps the OpenTracker Apache-2.0 notice.

## Releasing a set

1. Bump `version` of the engines whose bytes changed and `set_version` (and `sdk_version` when the SDK files changed).
2. `make check-sdk`, `make FLAVOUR=autof`, `make verify`, `crosscheck.sh`, then the same for `fused` if it is wanted.
3. `make package` writes `tracker-ff-engines-<set>-<flavour>.zip` next to `OUT/` (reproducible, the files under `Plugins/Synth/`, no previews),
   and refuses a set that does not verify. `SET.json` and `SHA256SUMS` are the manifest of the release. Tag the commit `engines-v<set>`.
