# Building the firmware yourself

This page gets you a `.ptf` you can flash from a source checkout, step by step. It is written for the 1.4.0 release (`tracker-ff-v1.4.0`) and works the same for any other tag, only the version in the file names changes.

The one thing that matters: **build with the Arm GNU Toolchain 12.3.rel1 (gcc 12.3.1).** The release image is built and tested on a unit only with that compiler. Any other compiler links a different image that nobody has run on a unit. It still builds without a warning and it can hang at the boot logo (that is what [issue 3](https://github.com/sbialy/tracker-ff/issues/3) was). These do **not** give a working release image:

- `arm-none-eabi-gcc` from your package manager (`apt install gcc-arm-none-eabi` is gcc 10.3 on Ubuntu 22.04 and gcc 12.2 on Debian 12)
- Arm GNU Toolchain 10.3-2021.10, the one upstream's notes, `docker/Dockerfile` and CI use for the stock CE firmware
- Arm GNU Toolchain 13 and newer (the tree does not compile with them)

The Makefile of the 1.4.0 tag does not look in `./agt/12.3.rel1/` (its README said so, that was wrong). It takes `./agt/10.3/` if that exists and otherwise whatever `arm-none-eabi-gcc` is first on your PATH. So on 1.4.0 **always pass `TOOLCHAIN=` yourself**, as in the commands below. From the first release after 1.4.0 (2.0.0) the Makefile also finds `./agt/12.3.rel1/` for the original (`make original`, `make original-release`, not for `make plus`, `make mini` and `make all`, which keep the old pick and need GCC 10.3) and `make original-release` stops on any compiler that is not 12.3.1. If a `./agt/10.3/` folder exists it is still picked first, so on 2.0.0 either remove it or pass `TOOLCHAIN=` (the stop message tells you which compiler it found).

## What you need

- `git`, `make`, `python3` (3.8 or newer), `curl` and `xz` (to unpack the toolchain)
- about 1.5 GB of free disk
- the original Polyend Tracker. The Tracker+ and the Tracker Mini are not covered here

## Linux

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
git clone --branch tracker-ff-v1.4.0 https://github.com/sbialy/tracker-ff.git
cd tracker-ff
make original-release TOOLCHAIN="$HOME/tracker-build/arm-gnu-toolchain-12.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-"
```

The `--version` line must print `12.3.1`. If it prints anything else stop and fix that first. Do not write `~` after `TOOLCHAIN=` without quotes, make does not expand it and the build fails late. `$HOME` is safe.

The build takes a few minutes. It ends with a size summary and writes the files below.

## macOS

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
git clone --branch tracker-ff-v1.4.0 https://github.com/sbialy/tracker-ff.git
cd tracker-ff
make original-release TOOLCHAIN="$HOME/tracker-build/arm-gnu-toolchain-12.3.rel1-darwin-arm64-arm-none-eabi/bin/arm-none-eabi-"
```

For an Intel Mac replace `darwin-arm64` with `darwin-x86_64` in the four lines that carry it. If macOS blocks the compiler with a "cannot verify the developer" message, run `xattr -dr com.apple.quarantine "$HOME/tracker-build/arm-gnu-toolchain-12.3.rel1-darwin-arm64-arm-none-eabi"` once.

## Always build from a fresh clone

`make` does not notice that the compiler changed, so objects left from an earlier build with another compiler get linked into the new image. If you ever built this folder with another compiler, delete it and clone again (or run `make clean` at the top level). Do not reuse a `build/` folder between compilers.

## Where the result lands

In the `tracker-ff` folder you cloned:

| file | what it is |
|---|---|
| `build/PolyendTracker_FF_1.4.0.ptf` | the firmware image. Copy it as it is, do not rename it |
| `build/modit.plg` | the MOD and IT module bound to this exact image |
| `build/BUILD.txt` | commit, engines, sizes, compiler independent facts about the build |

## Check the result

```bash
ls -l build/PolyendTracker_FF_1.4.0.ptf
shasum -a 256 build/PolyendTracker_FF_1.4.0.ptf     # on Linux: sha256sum
head -4 build/BUILD.txt
```

What a correct 1.4.0 build shows (all from a fresh clone of the tag with the commands above):

| build host | `.ptf` size | `text+data` in `BUILD.txt` | sha256 of the `.ptf` |
|---|---|---|---|
| macOS, Arm archive | 2,874,579 B | 1021968 | `64c147cab981bbe76ed91e1cb656e055aa8c6c78cfc0e24648de40aaa99c3826` |
| Linux x86_64, Arm archive, clone on a file system that lists files like macOS does (a folder shared from a Mac) | 2,874,534 B | 1021952 | `d1968713102eda3327c6dd5c3728a7ddc03445aaa69dd11dd02f784abfab9b85` |
| Linux x86_64, Arm archive, clone on the container's own file system (overlay) | 2,874,603 B | 1021980 | `77c24f0ff65209ed0a25052aa3f0e1da08cb78ec39375f73f48d821e35904ebf` |

- `BUILD.txt` must say `commit: 37bb158` and the first line of `arm-none-eabi-gcc --version` must say `12.3.1`.
- **macOS:** the sha256 is the published image. It must equal the line for `PolyendTracker_FF_1.4.0.ptf` in `SHA256SUMS` of the [release](https://github.com/sbialy/tracker-ff/releases/tag/tracker-ff-v1.4.0). If it does not, you are not on 12.3.rel1 or the folder was not a fresh clone.
- **Linux:** the image is **not** byte identical to the published one and its size changes a little from machine to machine. Two things cause that. Arm's prebuilt C library carries the path it was built in (`/Volumes/data/jenkins/...` in the macOS archive, `/data/jenkins/...` in the Linux ones), which moves one string by 16 B. And the 1.4.0 makefiles collect the source files with `find`, which lists them in the order the file system returns them, so the link order (and with it a few padding bytes and the exact sha) depends on your disk. The image from the second row (the order macOS uses) was run on a unit and boots. The third row, a different link order, was only built and checked for size, it was not run on a unit. So on Linux check the size and the `BUILD.txt` lines, not the sha256, and report which `text+data` you got if it hangs.
- A size far from these (about 11 KB smaller or about 29 KB bigger) means another compiler was used.

## Put it on the card

1. Download `PolyendTracker_FF_1.4.0.zip` from the release page and unzip the whole zip onto the root of the microSD card. This brings the plugins (`Plugins/Synth/`, `Plugins/Master/`) and the effects (`Effects/`), which are not built from the source tree.
2. Replace `Plugins/Ext/modit.plg` on the card with `build/modit.plg` of your build. The one in the zip is bound to the published image and does not fit yours. If it does not match, only MOD and IT import and export are off, the boot does not depend on it.
3. Copy `build/PolyendTracker_FF_1.4.0.ptf` to `Firmware/` on the card. Keep the name, the original's bootloader lists only names that start with `PolyendTrac` and are at most 31 characters long. Remove other `.ptf` files from `Firmware/` if you want to avoid picking the wrong one.
4. Put the card in the Tracker and open **Config > Firmware > Firmware update**, pick the file and confirm.

Back up your `Projects/` folder first. If the unit does not come up, use the emergency recovery in [upstream's README](upstream-README.md#4-flashing-on-hardware--emergency-recovery) and flash the published image again.

## If it still hangs at the logo

Flash the published `.ptf` to get the unit back and open an issue with:

- your OS and CPU (`uname -a`, or `sw_vers` on a Mac)
- the output of the compiler's `--version` (the exact path you passed as `TOOLCHAIN=`)
- the sha256 and size of your `.ptf` and the `text+data` line of `build/BUILD.txt`
- the file system the clone is on (`df -T .` on Linux)
- your `build/BUILD.txt`
- the exact `make` command you ran and whether the folder was a fresh clone

With these I can tell which image you built and compare it against the ones that run.

## Later releases

For another tag use `--branch <tag>` and expect another file name: `PolyendTracker_FF_<version>.ptf`. From 2.0.0 the MOD and IT module is called `extras.plg` (copy `build/extras.plg` to `Plugins/Ext/`). From 2.0.0 you can also unpack the toolchain into `./agt/12.3.rel1/` instead of passing `TOOLCHAIN=`, as long as there is no `./agt/10.3/` next to it. The checksums above belong to 1.4.0 only.

The flags of `make original-release` and the build internals are in the [README](../README.md#build) and in [`synth-open/README.md`](../synth-open/README.md).
