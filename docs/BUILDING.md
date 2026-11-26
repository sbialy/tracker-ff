# Building the firmware yourself

This page gets you a `.ptf` you can flash from a source checkout, step by step, for the original Tracker, the Tracker+ and the Tracker Mini. It works for any tracker-ff tag, only the version in the file names changes. The commands below use `tracker-ff-v2.0.0`, replace it with the tag you want.

The original Tracker and the two IMX trackers use two different compilers:

| tracker | image | compiler |
|---|---|---|
| original Tracker | `PolyendTracker_FF_2.0.0.ptf` | **Arm GNU Toolchain 12.3.rel1 (gcc 12.3.1)** |
| Tracker+ | `TrackerPlus_FF_2.0.0.ptf` | Arm GNU Toolchain 10.3-2021.10 (gcc 10.3.1) |
| Tracker Mini | `TrackerMini_FF_2.0.0.ptf` | Arm GNU Toolchain 10.3-2021.10 (gcc 10.3.1) |

The compiler matters. The image of the original is built and tested on a unit only with 12.3.1. Any other compiler links a different image that nobody has run on a unit. It still builds without a warning and it can hang at the boot logo (that is what [issue 3](https://github.com/sbialy/tracker-ff/issues/3) was). The Tracker+ and the Tracker Mini link a prebuilt library from Polyend that needs gcc 10.3. These do **not** give a working release image:

- `arm-none-eabi-gcc` from your package manager (`apt install gcc-arm-none-eabi` is gcc 10.3 on Ubuntu 22.04 and gcc 12.2 on Debian 12)
- the other one of the two compilers above (10.3 for the original, 12.3 for the Tracker+ and the Mini)
- Arm GNU Toolchain 13 and newer (the tree does not compile with them)

Always pass `TOOLCHAIN=` yourself, as in the commands below. Without it the Makefile uses `./agt/10.3/` if that folder exists. If there is none, `make original` and `make original-release` use `./agt/12.3.rel1/` and then whatever `arm-none-eabi-gcc` comes first on your PATH, while `make plus`, `make mini` and `make all` skip `./agt/12.3.rel1/`. `make original-release` stops on any compiler that is not 12.3.1 and says which one it found.

## What you need

- `git`, `make`, `python3` (3.8 or newer), `curl` and `xz` (to unpack the 12.3 toolchain) or `bzip2` (for the 10.3 one)
- about 1.5 GB of free disk for each tracker
- a Linux or macOS machine for the original Tracker. The Tracker+ and the Tracker Mini are built on a Linux x86_64 machine (the release images are built in an Ubuntu 22.04 x86_64 container)

## The original Tracker on Linux

Works on x86_64 and on aarch64 hosts. Pick the archive name for your host:

| host | archive |
|---|---|
| x86_64 (most PCs) | `arm-gnu-toolchain-12.3.rel1-x86_64-arm-none-eabi.tar.xz` |
| aarch64 (64 bit Arm, Raspberry Pi OS 64 bit) | `arm-gnu-toolchain-12.3.rel1-aarch64-arm-none-eabi.tar.xz` |

Debian or Ubuntu need the host tools first, other distributions have the same packages under similar names:

```bash
sudo apt-get install -y git make python3 curl xz-utils ca-certificates
```

Then, from any folder (the commands use `~/tracker-build`). For an aarch64 host replace `x86_64` with `aarch64` in the two lines that carry it:

```bash
mkdir -p "$HOME/tracker-build" && cd "$HOME/tracker-build"
curl -fLO https://developer.arm.com/-/media/Files/downloads/gnu/12.3.rel1/binrel/arm-gnu-toolchain-12.3.rel1-x86_64-arm-none-eabi.tar.xz
tar -xf arm-gnu-toolchain-12.3.rel1-x86_64-arm-none-eabi.tar.xz
"$HOME/tracker-build/arm-gnu-toolchain-12.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-gcc" --version
git clone --branch tracker-ff-v2.0.0 https://github.com/sbialy/tracker-ff.git
cd tracker-ff
make original-release TOOLCHAIN="$HOME/tracker-build/arm-gnu-toolchain-12.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-"
```

The `--version` line must print `12.3.1`. If it prints anything else stop and fix that first. Do not write `~` after `TOOLCHAIN=` without quotes, make does not expand it and the build fails late. `$HOME` is safe.

The build takes a few minutes. It ends with a size summary and writes the files below.

## The original Tracker on macOS

Apple Silicon (M1 and newer) uses the `darwin-arm64` archive, an Intel Mac the `darwin-x86_64` one. You need the Xcode command line tools (`xcode-select --install`) for `git`, `make` and `python3`.

| host | archive |
|---|---|
| Apple Silicon | `arm-gnu-toolchain-12.3.rel1-darwin-arm64-arm-none-eabi.tar.xz` |
| Intel | `arm-gnu-toolchain-12.3.rel1-darwin-x86_64-arm-none-eabi.tar.xz` |

```bash
mkdir -p "$HOME/tracker-build" && cd "$HOME/tracker-build"
curl -fLO https://developer.arm.com/-/media/Files/downloads/gnu/12.3.rel1/binrel/arm-gnu-toolchain-12.3.rel1-darwin-arm64-arm-none-eabi.tar.xz
tar -xf arm-gnu-toolchain-12.3.rel1-darwin-arm64-arm-none-eabi.tar.xz
"$HOME/tracker-build/arm-gnu-toolchain-12.3.rel1-darwin-arm64-arm-none-eabi/bin/arm-none-eabi-gcc" --version
git clone --branch tracker-ff-v2.0.0 https://github.com/sbialy/tracker-ff.git
cd tracker-ff
make original-release TOOLCHAIN="$HOME/tracker-build/arm-gnu-toolchain-12.3.rel1-darwin-arm64-arm-none-eabi/bin/arm-none-eabi-"
```

For an Intel Mac replace `darwin-arm64` with `darwin-x86_64` in the four lines that carry it. If macOS blocks the compiler with a "cannot verify the developer" message, run `xattr -dr com.apple.quarantine "$HOME/tracker-build/arm-gnu-toolchain-12.3.rel1-darwin-arm64-arm-none-eabi"` once.

## The Tracker+ and the Tracker Mini

Use a Linux x86_64 machine and the Arm GNU Toolchain 10.3-2021.10 archive `gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2`. The commands below build both images. Use `make plus` or `make mini` alone for one of them. Build in a clone of its own, because the original needs another compiler (see below).

```bash
sudo apt-get install -y git make python3 curl bzip2 ca-certificates
mkdir -p "$HOME/tracker-build/gcc-10.3" && cd "$HOME/tracker-build"
curl -fLO https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2
tar -xjf gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2 -C gcc-10.3 --strip-components=1
"$HOME/tracker-build/gcc-10.3/bin/arm-none-eabi-gcc" --version
git clone --branch tracker-ff-v2.0.0 https://github.com/sbialy/tracker-ff.git tracker-ff-imx
cd tracker-ff-imx
make plus mini TOOLCHAIN="$HOME/tracker-build/gcc-10.3/bin/arm-none-eabi-"
```

The `--version` line must print `10.3.1`.

## Always build from a fresh clone

`make` does not notice that the compiler changed, so objects left from an earlier build with another compiler get linked into the new image. If you ever built this folder with another compiler, delete it and clone again (or run `make clean` at the top level). Do not reuse a `build/` folder between compilers, and build the original and the IMX trackers in two clones.

## Where the result lands

In the `tracker-ff` folder you cloned (`tracker-ff-imx` for the IMX trackers):

| file | what it is |
|---|---|
| `build/PolyendTracker_FF_2.0.0.ptf` | the image of the original Tracker. Copy it as it is, do not rename it |
| `build/extras.plg` | the extras module of the original, bound to this exact image |
| `build/BUILD.txt` | commit, engines, sizes, compiler independent facts about the build of the original |
| `build/TrackerPlus_FF_2.0.0.ptf` | the Tracker+ image |
| `build/TrackerMini_FF_2.0.0.ptf` | the Tracker Mini image |

## Check the result

```bash
ls -l build/PolyendTracker_FF_2.0.0.ptf
shasum -a 256 build/PolyendTracker_FF_2.0.0.ptf     # on Linux: sha256sum
head -4 build/BUILD.txt
```

- `BUILD.txt` must name the commit of the tag you cloned and the first line of `arm-none-eabi-gcc --version` must say `12.3.1`.
- **macOS:** the sha256 of the original's image is the published image. It must equal the line for `Firmware/PolyendTracker_FF_2.0.0.ptf` in the `tracker-ff/SHA256SUMS` of the zip from the [releases page](https://github.com/sbialy/tracker-ff/releases). If it does not, you are not on 12.3.rel1 or the folder was not a fresh clone.
- **Linux:** the image of the original is **not** byte identical to the published one and its size changes a little from machine to machine. Two things cause that. Arm's prebuilt C library carries the path it was built in (`/Volumes/data/jenkins/...` in the macOS archive, `/data/jenkins/...` in the Linux ones), which moves one string by 16 B. And the makefiles collect the source files with `find`, which lists them in the order the file system returns them, so the link order (and with it a few padding bytes and the exact sha) depends on your disk. So on Linux check the size and the `BUILD.txt` lines, not the sha256, and report which `text+data` you got if it hangs.
- A `text+data` figure in `BUILD.txt` that is many KB away from the one of the published image means another compiler or other make variables were used.

## Put it on the card

1. Download the zip for your model from the release page (`PolyendTracker_FF_2.0.0.zip`, `TrackerPlus_FF_2.0.0.zip` or `TrackerMini_FF_2.0.0.zip`) and unzip the whole zip onto the root of the microSD card. This brings the plugins (`Plugins/Synth/`, `Plugins/Master/`) and the effects (`Effects/`), which are not built from the source tree.
2. Original Tracker only: replace `Plugins/Ext/extras.plg` on the card with `build/extras.plg` of your build. The one in the zip is bound to the published image and does not fit yours. If it does not match, only the functions that live in that module are off (for example MOD and IT import and export), the boot does not depend on it. The Tracker+ and the Tracker Mini have no `extras.plg`.
3. Copy your `.ptf` to `Firmware/` on the card and remove the one of the zip. Keep the name. The original's bootloader lists only names that start with `PolyendTrac` and are at most 31 characters long. Remove other `.ptf` files from `Firmware/` if you want to avoid picking the wrong one.
4. Put the card in the tracker and open **Config > Firmware > Firmware update**, pick the file and confirm.

Back up your `Projects/` folder first. If the original does not come up, use the emergency recovery in [upstream's README](upstream-README.md#4-flashing-on-hardware--emergency-recovery) and flash the published image again. On the Tracker+ and the Tracker Mini use the recovery steps Polyend documents for your model with the official Polyend firmware.

## If it still hangs at the logo

Flash the published `.ptf` to get the unit back and open an issue with:

- your tracker model, your OS and CPU (`uname -a`, or `sw_vers` on a Mac)
- the output of the compiler's `--version` (the exact path you passed as `TOOLCHAIN=`)
- the sha256 and size of your `.ptf` and, for the original, the `text+data` line of `build/BUILD.txt`
- the file system the clone is on (`df -T .` on Linux)
- your `build/BUILD.txt` (original Tracker)
- the exact `make` command you ran and whether the folder was a fresh clone

With these I can tell which image you built and compare it against the ones that run.

## Another version

For another tag use `--branch <tag>` and expect another file name: `PolyendTracker_FF_<version>.ptf`, `TrackerPlus_FF_<version>.ptf` and `TrackerMini_FF_<version>.ptf`. The tags before 2.0.0 build the original only, their Makefile does not look in `./agt/12.3.rel1/` and the extras module is called `modit.plg` there. Use the build guide of that tag.

The flags of `make original-release` and the build internals are in the [README](../README.md#build) and in [`synth-open/README.md`](../synth-open/README.md).
