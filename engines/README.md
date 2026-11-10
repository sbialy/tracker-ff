# engines/ - the plugin modules of tracker-ff

tracker-ff 1.2.0 is split into five repositories. This one is the firmware: the plugin host, the plugin SDK, the built-in engines
(ACD FAT VAP WTFM PERC from OpenTracker, CHIP from Polyend's open Chiptune voice) and the `.ptfx` Sample Editor effects. It holds no
plugin module source. Every synth engine plugin and master insert plugin is built in a module repository, and this folder pins and
checks what they publish.

| Repository | Visibility | Lock set | Modules |
|---|---|---|---|
| tracker-ff (this one) | public | | host, SDK, built-in engines, `.ptfx` |
| poly-engines-os | public | `engines-os` | the OpenTracker 3.x module engines JU6, FM4, B-Type, D-Type, CS-Type, A-Type, T-Type, 8-Type, Hand-Type, Arc-Type (`.plg`, `.plgi`, `.wasm`, `.json`) |
| poly-engines | private | `engines` | our own engines PHZ and PLK (FORM and FLUX are in the repository, not in 1.2.0), freeware binaries only |
| poly-effects-os | public | `effects-os` | the OpenTracker fx ports Dirt, Tape, Lo-Fi, BBD, Phaser (`.plg`, `.wasm`, `.json`) |
| poly-effects | private | `effects` | our own inserts Console and Tube, freeware binaries only |

## Files

| File | What |
|---|---|
| `engines.json` | the public identity of every module 1.2.0 ships: file id, descriptor name, stored / patch / type numbers and version of each engine, plug id and version of each insert, visibility and repository. A module repository must not change these. A new engine gets its row here and its `synth-open/engine_list.h` entry first |
| `external.lock.json` | per module repository: tag, commit, visibility, the sha256 of every published module file (`files`) and of the notices and licence texts that travel with them (`notice_files`), the local checkout and build directories |
| `external.py` | `fetch DEST` (release assets: public over https, private with `gh` and a token, or `--from-local`), `check DIR`, `source SET DEST FILE...` (module source at the pinned tag from a local checkout, used by ui_shot), `list`, `relock SET` |
| `check_modules.py` | `verify-external DIR`: every module against its row in `engines.json` (header, CRC, kind, ABI, numbers, name, version, `.plgi`, manifest visibility and source repository, manifest sha256 of the `.wasm` and `.plg`) |
| `crosscheck.sh` | the firmware side checks of a fetched set: the lock, `verify-external`, the firmware's own `checkSynth` on every engine (`synth-open/tools/validate_synth.py`), the card logo reader on every `.plgi` |
| `rebuild_pinned.sh` | rebuilds all four repositories from their pinned tags (git archive of the local checkouts) and checks that the result equals the lock byte for byte |
| `ci_external.sh` | the CI step (`.github/workflows/build.yml`): public sets always, private sets with the `PRIVATE_RELEASES_TOKEN` secret |
| `sdk/headers.txt`, `sdk/tools.txt`, `sdk/VERSION` | the plugin SDK the engine plugins compile against: the module repositories vendor exactly these files |
| `Makefile` | `make -C engines fetch / check / crosscheck / list` |

## Fetch and check

```bash
python3 -I engines/external.py fetch build/modules              # release assets of the pinned tags (private sets need gh and a token)
python3 -I engines/external.py fetch build/modules --from-local # or the build directories of local checkouts (until the tags are published)
python3 -I engines/external.py fetch build/modules --visibility public   # a clone without access to the private repositories
TOOLCHAIN=.../arm-none-eabi- PY=/tmp/phzvenv/bin/python engines/crosscheck.sh build/modules [public]
```

`build/modules` then holds every module flat (`ju6.plg`, `ju6.plgi`, `ju6.wasm`, `ju6.json`, ...) and the notice files of each
repository in `build/modules/<set>/` (every repository has its own `NOTICE.md`; the private ones add `LICENSE.md`, the freeware terms).

## Release build

1. The firmware image: `make original SYNTH_OPEN=1 TRIM=1 PLUG_HOST=1 SYNTH_ENGINES=stock ...` (the AUTOF recipe in
   `synth-open/README.md`). It never reads a module repository.
2. The modules: `engines/rebuild_pinned.sh OUT autof` (from source, checked against the lock) or `external.py fetch` (the published files).
3. The package: `synth-open/tools/package_release.sh <image.ptf> OUT --modules build/modules --effects <ptfx dir> --zip` lays the
   engines out in `Plugins/Synth/`, the inserts in `Plugins/Master/` and the notices of every module repository in
   `licenses/plugins/<repository>/`, after `external.py check` and `verify-external`.

## FORM and FLUX: an optional group, off in the 1.2.0 package

FORM (stored 19, patch and type 0x8B) and FLUX (stored 20, 0x8C) are in `poly-engines` `engines-v1.3.0`. They ship only as plugins in the
release zip, and only after the user has listened and approved them, so everything about them is behind one switch, the group
`formflux`:

- `engines.json`: the rows `form` and `flux` carry `"optional": "formflux"`. `check_modules.py verify-external` leaves them out unless `--with formflux`.
- `external.lock.json`: the set `engines` has `"optional": {"formflux": {"files": ..., "patches": ...}}` (the 8 files of the two plugins
  and the 11 factory presets of `presets/Patches/{FORM,FLUX}`, pinned with sha256). `external.py fetch|check --with formflux` takes them,
  `relock` always re-pins them. The presets are laid out as `DEST/Patches/<ENGINE>/<name>.patch`, their release asset names are
  `<ENGINE>-<name with _ for spaces>.patch`.
- `rebuild_pinned.sh OUT autof all --with formflux`, `crosscheck.sh DIR all --with formflux`, `make -C engines check WITH=formflux`,
  `EXTERNAL_WITH=formflux engines/ci_external.sh`: the same tools with the group on.
- `package_release.sh ... --modules DIR --with formflux`: `Plugins/Synth/form.plg flux.plg` (+ `.plgi`), `Patches/FORM/*.patch`,
  `Patches/FLUX/*.patch`, and the README counts (20 engines, 14 plugins). Without `--with` the package is byte for byte what it was before.

The firmware image is the same with or without them: a plugin brings its own stored, patch and type numbers, the host takes any
free ones (`engine_list.h` lists only the built-in engines and is a pinned SDK header, so FORM and FLUX are not added there).
A project that uses FORM or FLUX on a card without the plugin falls back to a default part, like any engine the build has never heard
of (the "known but missing" placeholders cover stored 7 to 18 only).

To ship them: build both packages again with `--with formflux` (`~/prj/poly/ref/five-repos-1.2.0.md` lists the commands), or drop
`"optional": "formflux"` from the two rows and make the group `files` part of `set["files"]` once nobody wants the switch any more.

## Build order for a new release

1. tracker-ff: change the host or the SDK if needed, commit. If a file of `sdk/headers.txt` or `sdk/tools.txt` (or
   `synth-open/plug/LICENSE`, or Dirt's kernel `synth-open/ff_dirt*`) changed, bump `sdk/VERSION`.
2. Each module repository: `make sdk-check FW=<tracker-ff checkout>`, `make sdk-sync FW=...` when it differs, rebuild, compare the
   `.plg` bytes, bump the module `version` where they changed on purpose (and its row in `engines.json` here), commit, tag (`vX.Y.Z`
   for the public ones, `engines-vX.Y.Z` / `effects-vX.Y.Z` for the private ones), publish the release assets.
3. tracker-ff: `python3 -I engines/external.py relock <set> [--tag TAG]` per changed repository (tag commit and every sha256 from
   the local build directories), `engines/rebuild_pinned.sh` or `crosscheck.sh` on a fetch, commit the lock.

## The SDK copy in the module repositories

A module repository vendors the SDK files it compiles against in `sdk/files/` (at their tracker-ff paths) and names the tracker-ff
commit in `sdk/SOURCE`. Its build reads nothing outside its own checkout, so an outsider who clones only that repository (plus the
toolchain) builds the published bytes. `make sdk-check FW=../tracker-ff` compares the copy with a tracker-ff checkout, `make sdk-sync`
takes the checkout's files. The firmware stays the owner of these files: an SDK change is made here first.

## What the firmware lost with the module sources

- `SYNTH_ENGINES` accepts only the built-in six. A plugin engine name stops the build with a pointer here. The flash report
  (`make synth-engines`) and `tools/phz_cycles` cover the built-in engines only.
- The render compare of each engine plugin against the same engine linked into the firmware (`synth_render_compare.sh`) is gone:
  the linked-in reference no longer exists. The engine builds keep their own render checksums (`SET.json` of poly-engines-os) and the
  1.2.0 `.plg` files are byte identical to the ones that passed that compare.
- `FEATURE_PLUG_BENCH` (`plug_dirt_inimage.cpp`) needs a copy of `fx_dirt_plug.cpp` from poly-effects-os, the build says how.
- The built-in Dirt (`FF_DIRT=1`, a `SYNTH_OPEN` image without `PLUG_HOST`) keeps its kernel here (`synth-open/ff_dirt*`). poly-effects-os
  vendors the same three files in its SDK copy, so `dirt.plg` still renders exactly like the built-in Dirt (checksum 3b1f3dc5).
