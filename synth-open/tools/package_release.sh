#!/usr/bin/env bash
# Package a tracker-ff (synth-open) firmware image with the license texts that must travel with it.
#   synth-open/tools/package_release.sh <image.ptf> [out_dir] [--zip] [--card CARD_DIR --notices MODULES_DIR | --modules MODULES_DIR [--effects PTFX_DIR] [--with GROUP]] [--ext MODULE.plg] [--extras EXTRAS.plg]
# Makes <out_dir>/<image name without .ptf>/ (default out_dir: build/release) laid out as the root of the SD card:
#   Firmware/<image>.ptf, Plugins/ (Synth, Master, Ext), Effects/, Patches/ where given, one README-RELEASE.txt, and the
#   folder tracker-ff/ with LICENSE, NOTICE.md, UPGRADING.md, CONTRIBUTORS.md, SHA256SUMS (paths relative to the card root) and licenses/ (one text per
#   bundled third-party component). The card has no other use for a folder it does not know: the sample browser lists it, nothing else.
# Texts that exist only as source file headers (FastCRC, Polyverb, FatFs, PJRC Teensy core and audio library) are copied out
# of those headers verbatim (the leading comment block of the file). Never includes factory patches.
# --card CARD_DIR copies the card files of a PLUG_HOST release, laid out as on the SD card (Plugins/Synth/*.plg *.plgi,
# Plugins/Master/*.plg, optional Plugins/Ext/module.plg, Effects/*.ptfx, Patches/), into the package beside Firmware/.
# --ext MODULE.plg adds the developer mode extension module as Plugins/Ext/module.plg (the path the firmware loads it from).
# --extras EXTRAS.plg (old name --modit) adds the release card modules as Plugins/Ext/extras.plg (build/extras.plg of make original-release: MOD
# import, IT import and IT export are in that file, not in the image, later the other EXT_SUB_* tenants of ext_abi.h). It is checked against the .ptf first (synth-open/plug/check_ext.py: the image id in the module must be the
# one of this image, a module of another build is refused). A package with plugins (--modules or --card) needs it, from --extras or from a card
# kit that has Plugins/Ext/extras.plg: without it the Import Mod and Export As .it buttons of the image only say the module is missing.
# EXTRAS=skip (old name MODIT=skip) leaves it out (a layout test, never a release) and says so.
# --modules MODULES_DIR does the same from the pinned plugin modules of the four module repositories (python3 -I engines/external.py
# fetch MODULES_DIR): it checks them against engines/external.lock.json and engines/engines.json first, lays the engines of
# engines.json out in Plugins/Synth (.plg + .plgi) and the inserts in Plugins/Master, and copies the notice files of every module
# repository (MODULES_DIR/<set>/: NOTICE.md, LICENSE.md of the freeware modules, licence texts) to licenses/plugins/<public name>/.
# --notices MODULES_DIR is that same copy for --card (a card has the plugins but not their notices): the folder of the fetched module sets
# (MODULES_DIR/<set>/, the --modules layout), required with --card, taken from MODULES_DIR with --modules. Every notice file is checked
# against the sha256 of engines/external.lock.json and a missing or changed one stops the package: a release without the plugin licences
# is not made. The folders carry the PUBLIC names, tracker-engines, tracker-effects, tracker-engines-os, tracker-effects-os (the two
# private repositories are published under those names, as in 1.2.0), never the name of a private repository. A file in
# engines/notices/<public name>/ replaces the fetched file of that name (a package copy without modules that are not released).
# --effects PTFX_DIR adds the .ptfx of that folder as Effects/.
# A package with plugins (--modules or --card) also carries Waves/, the curated single cycle waves of releases/waves (synth-open/tools/waves_curated.py).
# --with GROUP (needs --modules, repeatable) switches an optional group of engines/engines.json on. No group is defined in 1.3.0.
# PKG_REV=<commit> names that commit in README-RELEASE.txt instead of the HEAD of this repository (to rebuild an old package).
# A package with plugins (--modules or --card) is a release package: it first runs the release gate, synth-open/tools/release_gate.sh (the
# firmware and engine mismatch simulator against the latest published release tag, known findings in ui_shot/mismatch_known_findings.txt), and
# makes nothing when a check that is not on that list is RED. OLD_REF=<ref> picks another old release, MISMATCH_GATE=skip skips the gate (a
# layout test, never a release) and says so.
# The same condition runs the stack gate check: `make stack-margin-release` (synth-open/tools/stack/stack_margin.py, static worst case stack of the linked
# image against the real stack room, floor 512 B) leaves <image>.ptf.stack-margin next to the image with PASS and the sha256 of the .ptf it ran for.
# A package with plugins is made only from an image that has that stamp with PASS and the same sha256 (a rebuilt image needs a new gate run).
# STACK_GATE=skip skips the check (a layout test, never a release) and says so.
# --imx plus|mini makes the zip of the Tracker+ or the Tracker Mini (IMX): the image is TrackerPlus_<TAG>_<A.B.C>.ptf or
# TrackerMini_<TAG>_<A.B.C>.ptf and goes to Firmware/ of a card layout like the original's, with the shared card content (Plugins/Synth and
# Plugins/Master, Effects, Waves, Tunings, README-RELEASE.txt, tracker-ff/ with its own SHA256SUMS) and without what the IMX image does not read:
# no Plugins/Ext (extras.plg and the console module are linked into the IMX image or bound to the MK66 image, --ext and --extras are refused),
# no Plugins/Synth/chip.plg and chip.plgi (the stock CHIP plays, core/synth/synth_types.h refuses chip.plg) and no Effects/se_*.ptfx (the IMX image
# has the nine Sample Editor effects built in, a card copy would be listed twice). It needs --modules or --card. The stack gate and the mismatch
# gate are about the MK66 image and run with the package of the original from the same module folder, not here. The zip of the IMX package is
# named like its image, TrackerPlus_<TAG>_<A.B.C>.zip.
# With --zip the folder is also zipped next to itself (<name>.zip, needs zip). The zip has no wrapper directory: its root is the card root,
# so it unzips straight onto the SD card. The zip is the only thing a release publishes: the image is inside it as Firmware/<image>.ptf and the
# release page lists a SHA256SUMS of the zip alone (the list inside the zip, tracker-ff/SHA256SUMS, covers every file in it).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ptf=""; imx=""; out="$root/build/release"; zip=0; card=""; modules=""; notices=""; effects=""; ext=""; modit=""; groups=(); with=()
while [ $# -gt 0 ]; do
  case "$1" in
    --zip) zip=1 ;;
    --card) shift; card="${1:?--card needs a directory}"; [ -d "$card" ] || { echo "no such card dir: $card" >&2; exit 2; } ;;
    --modules) shift; modules="${1:?--modules needs a directory}"; [ -d "$modules" ] || { echo "no such modules dir: $modules" >&2; exit 2; } ;;
    --notices) shift; notices="${1:?--notices needs a directory}"; [ -d "$notices" ] || { echo "no such notices dir: $notices" >&2; exit 2; } ;;
    --ext) shift; ext="${1:?--ext needs a module.plg}"; [ -f "$ext" ] || { echo "no such module: $ext" >&2; exit 2; } ;;
    --extras|--modit) shift; modit="${1:?--extras needs an extras.plg}"; [ -f "$modit" ] || { echo "no such module: $modit" >&2; exit 2; } ;;
    --with) shift; groups+=("${1:?--with needs a group}"); with+=(--with "$1") ;;
    --imx) shift; imx="${1:?--imx needs plus or mini}"; case "$imx" in plus | mini) ;; *) echo "--imx takes plus or mini" >&2; exit 2 ;; esac ;;
    --effects) shift; effects="${1:?--effects needs a directory}"; [ -d "$effects" ] || { echo "no such effects dir: $effects" >&2; exit 2; } ;;
    *) if [ -z "$ptf" ]; then ptf="$1"; else out="$1"; fi ;;
  esac
  shift
done
[ -n "$ptf" ] && [ -f "$ptf" ] || { echo "usage: $0 <image.ptf> [out_dir] [--zip]" >&2; exit 2; }
case "$(basename "$ptf")" in *.ptf) ;; *) echo "not a .ptf: $ptf" >&2; exit 2 ;; esac
if [ -n "$imx" ]; then
  # an IMX image: TrackerPlus_<TAG>_<A.B.C>.ptf or TrackerMini_<TAG>_<A.B.C>.ptf, named for the model (make plus, make mini)
  case "$imx" in plus) want=TrackerPlus ;; *) want=TrackerMini ;; esac
  [[ "$(basename "$ptf")" =~ ^${want}_[A-Za-z0-9]+_[0-9]+\.[0-9]+\.[0-9]+\.ptf$ ]] || { echo "--imx $imx needs an image named ${want}_<TAG>_<A.B.C>.ptf, not $(basename "$ptf")" >&2; exit 2; }
  [ -z "$modit" ] && [ -z "$ext" ] || { echo "--imx takes no --extras and no --ext: the IMX image reads neither" >&2; exit 2; }
else
# Both name shapes pass, the public release PolyendTracker_<TAG>_<A.B.C>.ptf and the build image PolyendTracker_<TAG>_<sha7>.ptf
# (rc, dev, test, console), with the 31 char limit of the bootloader. The folder and zip take the name of the image.
"$root/synth-open/tools/check_ptf_name.sh" "$ptf" >&2 || exit 2
fi
[ -z "$imx" ] || [ -n "$modules" ] || [ -n "$card" ] || { echo "--imx needs --modules or --card" >&2; exit 2; }

[ ${#groups[@]} = 0 ] || [ -n "$modules" ] || { echo "--with needs --modules" >&2; exit 2; }
[ -z "$notices" ] || [ -n "$card" ] || { echo "--notices goes with --card (--modules reads its own folder)" >&2; exit 2; }
[ -z "$card" ] || [ -n "$notices" ] || { echo "--card needs --notices MODULES_DIR: the plugin notices and licences ship in every package with plugins" >&2; exit 2; }
[ -z "$modules" ] || notices="$modules"
name="$(basename "$ptf" .ptf)"
# absolute image and out_dir: the zip step below runs after a cd into the folder, a relative path would not resolve there
ptf="$(cd "$(dirname "$ptf")" && pwd)/$(basename "$ptf")"
mkdir -p "$out" && out="$(cd "$out" && pwd)" || { echo "package_release: cannot create $out" >&2; exit 2; }
dest="$out/$name"

gate_modit="$modit"
[ -n "$gate_modit" ] || { [ -z "$card" ] || [ ! -f "$card/Plugins/Ext/extras.plg" ] || gate_modit="$card/Plugins/Ext/extras.plg"; }
if [ -n "$imx" ]; then
  echo "package_release: --imx $imx: the stack gate and the mismatch gate (MK66 image) are not run here, they run with the package of the original from the same modules" >&2
elif [ -n "$modules" ] || [ -n "$card" ]; then
  if [ "${STACK_GATE:-}" = skip ]; then
    echo "WARNING: STACK_GATE=skip, the static stack gate was NOT checked for this package" >&2
  else
    stamp="$ptf.stack-margin"
    [ -f "$stamp" ] || { echo "package_release: no stack gate stamp $stamp, run make stack-margin-release (same variables as the build of this image) first (STACK_GATE=skip only for a layout test)" >&2; exit 1; }
    sum="$(python3 -I -c 'import hashlib, sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' "$ptf")"
    case "$(cat "$stamp")" in
      "PASS ptf_sha256=$sum "*) ;;
      *) echo "package_release: the stack gate stamp $stamp is not a PASS for this image (sha256 $sum): $(cat "$stamp")" >&2; exit 1 ;;
    esac
  fi
  if [ "${MISMATCH_GATE:-}" = skip ]; then
    echo "WARNING: MISMATCH_GATE=skip, the firmware and engine mismatch gate was NOT run for this package" >&2
  else
    gate_plg="$modules"
    if [ -z "$gate_plg" ]; then   # --card: the gate wants one flat folder of the plugins
      gate_plg="$(mktemp -d)"; trap 'rm -rf "$gate_plg"' EXIT
      cp "$card"/Plugins/Synth/*.pl* "$card"/Plugins/Master/*.pl* "$gate_plg"/ 2>/dev/null || true
    fi
    IMAGE="$ptf" PLG_DIR="$gate_plg" EXTRAS_PLG="$gate_modit" "$root/synth-open/tools/release_gate.sh" >&2 || { echo "package_release: the release gate failed, no package made (MISMATCH_GATE=skip only for a layout test)" >&2; exit 1; }
  fi
fi
# the MOD/IT module of a release package: bound to this image, or the package is not made
if [ -n "$modit" ]; then
  python3 -I "$root/synth-open/plug/check_ext.py" --rel "$modit" "$ptf" >&2 || { echo "package_release: $modit does not belong to $ptf, no package made (build it after the image: make original-release)" >&2; exit 1; }
fi
if [ -z "$imx" ] && { [ -n "$modules" ] || [ -n "$card" ]; }; then
  if [ -z "$modit" ] && { [ -z "$card" ] || [ ! -f "$card/Plugins/Ext/extras.plg" ]; }; then
    if [ "${EXTRAS:-${MODIT:-}}" = skip ]; then
      echo "WARNING: EXTRAS=skip, this package has NO MOD/IT module (Plugins/Ext/extras.plg)" >&2
    else
      echo "package_release: no MOD/IT module: pass --extras build/extras.plg (make original-release builds it) or put Plugins/Ext/extras.plg in the card kit" >&2; exit 1
    fi
  elif [ -z "$modit" ]; then
    python3 -I "$root/synth-open/plug/check_ext.py" --rel "$card/Plugins/Ext/extras.plg" "$ptf" >&2 || { echo "package_release: the card kit's extras.plg does not belong to $ptf" >&2; exit 1; }
  fi
fi
rm -rf "$dest"
docs="$dest/tracker-ff"
mkdir -p "$docs/licenses" "$dest/Firmware"

cp "$ptf" "$dest/Firmware/"
if [ -n "$modules" ]; then
  [ -z "$card" ] || { echo "--card and --modules exclude each other" >&2; exit 2; }
  python3 -I "$root/engines/external.py" check "$modules" ${with[@]+"${with[@]}"} >&2 || { echo "the modules in $modules do not match engines/external.lock.json" >&2; exit 1; }
  python3 -I "$root/engines/check_modules.py" verify-external "$modules" --host-abi "$root/synth-open/plug/plug_abi.h" ${with[@]+"${with[@]}"} >&2 || exit 1
  card="$(mktemp -d)"; trap 'rm -rf "$card"' EXIT
  mkdir -p "$card/Plugins/Synth" "$card/Plugins/Master"
  python3 - "$root/engines/engines.json" "$modules" "$card" "${groups[*]:-}" <<'PY'
import json, os, shutil, sys
t = json.load(open(sys.argv[1]))
on = sys.argv[4].split()
for e in t["engines"]:
    if e.get("optional") and e["optional"] not in on:
        continue
    for x in (".plg", ".plgi"):
        shutil.copyfile(os.path.join(sys.argv[2], e["id"] + x), os.path.join(sys.argv[3], "Plugins", "Synth", e["id"] + x))
for e in t["inserts"]:
    shutil.copyfile(os.path.join(sys.argv[2], e["id"] + ".plg"), os.path.join(sys.argv[3], "Plugins", "Master", e["id"] + ".plg"))
PY
  [ -z "$effects" ] || { mkdir -p "$card/Effects"; cp "$effects"/*.ptfx "$card/Effects/"; }
  # the nine Sample Editor effects that are no longer in the image (se_*.ptfx, tracker-effects-os, pinned by the lock) go to Effects/ too
  if ls "$modules"/se_*.ptfx >/dev/null 2>&1; then mkdir -p "$card/Effects"; cp "$modules"/se_*.ptfx "$card/Effects/"; fi
  [ ${#groups[@]} = 0 ] || [ ! -d "$modules/Patches" ] || cp -R "$modules/Patches" "$card/Patches"
fi
if [ -n "$notices" ]; then
  # tracker-ff/licenses/plugins/<public name>/: the NOTICE.md, the freeware LICENSE.md and the licence texts of every module set
  python3 -I - "$root/engines/external.lock.json" "$notices" "$docs/licenses/plugins" "$root/engines/notices" <<'PY'
import hashlib, json, os, shutil, sys
# the lock names every module set by its public repository name, a private working name never reaches the zip
sets = json.load(open(sys.argv[1]))["sets"]
n = 0
for name, s in sets.items():
    repo = s["repo"].split("/")[-1]
    pub = repo
    if "poly" in pub:
        sys.exit("package_release: %s would ship under the private name %s" % (name, pub))
    d = os.path.join(sys.argv[3], pub)
    os.makedirs(d, exist_ok=True)
    for f, want in s.get("notice_files", {}).items():
        src = os.path.join(sys.argv[2], name, f)
        if not os.path.isfile(src):
            sys.exit("package_release: no notice file %s (set %s), fetch it with engines/external.py fetch" % (src, name))
        if hashlib.sha256(open(src, "rb").read()).hexdigest() != want:
            sys.exit("package_release: %s does not match the sha256 of engines/external.lock.json" % src)
        # engines/notices/<public name>/<file>: a package copy that replaces the fetched file (the pinned text still names modules
        # that are not in the release). The fetched file must still match the lock.
        over = os.path.join(sys.argv[4], pub, f)
        shutil.copyfile(over if os.path.isfile(over) else src, os.path.join(d, f))
        n += 1
if n == 0:
    sys.exit("package_release: the lock lists no notice files")
print("licenses/plugins: %d notice files of %d module sets" % (n, len(sets)), file=sys.stderr)
PY
fi
if [ -n "$card" ]; then
  for d in Plugins/Synth Plugins/Master; do
    ls "$card/$d"/*.plg > /dev/null 2>&1 || { echo "no .plg in $card/$d" >&2; exit 1; }
  done
  cp -R "$card/Plugins" "$dest/Plugins"
  [ ! -d "$card/Effects" ] || cp -R "$card/Effects" "$dest/Effects"
  if [ -n "$imx" ]; then
    # what the IMX image does not read: no Ext folder, no chip.plg (stock CHIP plays), no se_*.ptfx (built in)
    rm -rf "$dest/Plugins/Ext" "$dest/Plugins/Synth/chip.plg" "$dest/Plugins/Synth/chip.plgi"
    rm -f "$dest"/Effects/se_*.ptfx
  fi
  [ ! -d "$card/Patches" ] || cp -R "$card/Patches" "$dest/Patches"
  # the curated /Waves set (releases/waves, 143 AKWF-FREE single cycle waves, flat, with the CC0 credit and a readme): checked against its manifest first
  python3 -I "$root/synth-open/tools/waves_curated.py" stage "$dest" >&2 || { echo "package_release: the curated /Waves set does not match releases/waves/MANIFEST.txt" >&2; exit 1; }
  # the example Scala tunings (releases/tunings/*.scl, Project Settings > Tuning > Scale file reads /Tunings)
  [ ! -d "$root/releases/tunings" ] || { mkdir -p "$dest/Tunings"; cp "$root"/releases/tunings/*.scl "$dest/Tunings/"; }
fi
[ -z "$ext" ] || { mkdir -p "$dest/Plugins/Ext"; cp "$ext" "$dest/Plugins/Ext/module.plg"; }
[ -z "$modit" ] || { mkdir -p "$dest/Plugins/Ext"; cp "$modit" "$dest/Plugins/Ext/extras.plg"; }
cp "$root/LICENSE" "$root/NOTICE.md" "$root/UPGRADING.md" "$root/CONTRIBUTORS.md" "$docs/"

# Full license texts that are files in the repository
copy() { cp "$root/$1" "$docs/licenses/$2" || { echo "missing license file $1" >&2; exit 1; }; }
copy licenses/LICENSE-OpenTracker-Apache-2.0.txt     LICENSE-OpenTracker-Apache-2.0.txt
copy licenses/LICENSE-OpenTracker-synths-MIT.txt      LICENSE-OpenTracker-synths-MIT.txt
copy licenses/LICENSE-OpenTracker-fx-MIT.txt          LICENSE-OpenTracker-fx-MIT.txt
copy licenses/LICENSE-BSD-3-Clause-NXP.txt          LICENSE-BSD-3-Clause-NXP.txt
copy licenses/LICENSE-LiberationFonts-OFL-1.1.txt   LICENSE-LiberationFonts-OFL-1.1.txt
copy util/3rdparty/fmt/LICENSE.rst                  LICENSE-fmt-MIT.txt
copy util/3rdparty/nanopb/LICENSE.txt               LICENSE-nanopb-zlib.txt
copy util/3rdparty/tiny/LICENSE                     LICENSE-tiny-BSL-1.0.txt
copy util/3rdparty/constexpr-string/LICENSE         LICENSE-constexpr-string-MIT.txt

# License texts that exist only as the leading comment block of a source file: copy that block verbatim.
# Block comment (/* ... */) or run of // lines, stops at the first line that is neither.
header() { # <repo path> <out name> <title>
  local src="$root/$1" outf="$docs/licenses/$2"
  [ -f "$src" ] || { echo "missing source $1" >&2; exit 1; }
  {
    echo "$3"
    echo "Verbatim from the header of $1"
    echo "----------------------------------------------------------------------"
    awk '
      { sub(/\r$/, "") }
      NR==1 && /^[[:space:]]*\/\*/ { blk=1 }
      NR==1 && /^[[:space:]]*\/\// { lin=1 }
      blk { print; if ($0 ~ /\*\//) exit; next }
      lin { if ($0 ~ /^[[:space:]]*\/\//) { print; next } else exit }
      { exit }' "$src"
  } > "$outf"
  [ "$(wc -l < "$outf")" -gt 12 ] || { echo "no license header found in $1" >&2; exit 1; }
  grep -qi 'permission\|redistribution' "$outf" || { echo "header of $1 has no license text" >&2; exit 1; }
}
header platforms/mk66/3rdparty/FastCRC/FastCRC.h           LICENSE-FastCRC-MIT.txt            "FastCRC (Frank Boesing), MIT"
header core/libs/Audio/Polyverb/reverb.h                    LICENSE-Polyverb-MutableInstruments-MIT.txt "Polyverb, from Mutable Instruments (Emilie Gillet), MIT"
header platforms/mk66/3rdparty/SD/fatfs/source/ff.c         LICENSE-FatFs-ChaN.txt             "FatFs (ChaN), BSD-style license"
header platforms/mk66/3rdparty/teensy_core/pins_teensy.c    LICENSE-PJRC-TeensyCore.txt        "Teensyduino core (PJRC.COM, LLC), MIT with the PJRC device-list condition"
header platforms/mk66/3rdparty/Audio/input_i2s.h            LICENSE-PJRC-AudioLibrary-MIT.txt  "Teensy Audio Library (Paul Stoffregen, PJRC.COM, LLC), MIT"

# Where to find the source of this build
rev="${PKG_REV:-$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo unknown)}"
# A public release image (PolyendTracker_<TAG>_<A.B.C>.ptf) names its release tag, as the published 1.2.0 kit did, never a local commit
case "$name" in
  PolyendTracker_*_[0-9]*.[0-9]*.[0-9]* | TrackerPlus_*_[0-9]*.[0-9]*.[0-9]* | TrackerMini_*_[0-9]*.[0-9]*.[0-9]*) [ -n "${PKG_REV:-}" ] || rev="tag tracker-ff-v${name##*_}" ;;
esac
case "$rev" in tag\ *) ;; *) rev="commit $rev" ;; esac
# The words of the README that depend on the optional groups (none on: 14 plugin engines, CHIP among them since 1.3.0, ALCO since 1.4.0)
nplug=14; nword=fourteen; ntotal=19; freeware="PHZ, PLK, ALCO, Console, Tube and Glue"
nofactory="Factory patches are not part of this package."; patchesline=""
for g in ${groups[@]+"${groups[@]}"}; do
  case "$g" in
    *) echo "unknown group: $g" >&2; exit 2 ;;
  esac
done
plugnotes=""
[ -z "$notices" ] || plugnotes="tracker-ff/licenses/plugins/<module set>/   the notices and licences of the plugins, per module set ($freeware are freeware: LICENSE.md there)
"
if [ -n "$imx" ]; then
case "$imx" in plus) model="Tracker+" ;; *) model="Tracker Mini" ;; esac
cat > "$dest/README-RELEASE.txt" <<EOF
tracker-ff  ($name)
Unofficial community firmware for the Polyend $model (IMX), a fork of Polyend's Tracker Community Edition. It is built from the same source as the firmware of the original Tracker. The synth parts keep the stock engines and the image also reads $((nplug - 1)) plugin synth engines from the card.
Not affiliated with, endorsed by or supported by Polyend. Free, non-commercial.
Built from $rev of https://github.com/sbialy/tracker-ff (fork of https://github.com/polyend/tracker-ce).

tracker-ff/LICENSE     the repository license (MIT with the Wholesale Commercial Clone Restriction): free to share, not to sell
tracker-ff/NOTICE.md   third-party notices and attributions
tracker-ff/UPGRADING.md  how to update and how to go back, read it before you flash
tracker-ff/CONTRIBUTORS.md  the people who contributed reports, fixes or ideas
tracker-ff/licenses/   the full license text of every bundled third-party component (OpenTracker Apache-2.0, opentracker-synths MIT, opentracker-fx MIT, ...)
tracker-ff/SHA256SUMS  checksums of every file, paths from the card root: shasum -a 256 -c tracker-ff/SHA256SUMS
${plugnotes}
Install
1. Back up the SD card first. UPGRADING.md lists what to copy.
2. Unzip this whole zip onto the root of the SD card and let the computer replace the files that are there. The zip is laid out like the card: Firmware/ holds the image, Plugins/Synth/ and Plugins/Master/ the plugins, Effects/ the optional effects, Waves/ and Tunings/ the example waves and scales.
3. Put the card in the $model, open Config > Firmware > Firmware update, pick the image and confirm. Leave the .ptf of your old version in /Firmware as your way back. The file name of the image must stay as it is.
4. Wait until the $model restarts by itself. Config > Firmware then shows the version.

What the card files do
- Plugins/Synth/ holds the plugin synth engines. A synth part that uses one loads it when the part picks it, and without the file the part shows Missing and keeps its settings and its patch file. The stock CHIP engine plays CHIP, so there is no chip.plg in this zip.
- Plugins/Master/ holds the master insert plugins.
- Effects/ holds the optional effects. The nine Sample Editor effects are built into this image, so there are no se_*.ptfx files in this zip.
- Waves/ holds 143 single cycle waves for ALCO Wave mode (A File and B File), flat in /Waves, from AKWF-FREE by Kristoffer Ekstrand (CC0 1.0, credit in Waves/AKWF-LICENSE.txt). Waves you already have in /Waves stay, nothing there is deleted. The full AKWF-FREE set (4,358 waves) is at https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE
- Tunings/ holds five example Scala tunings (just intonation, Bohlen-Pierce, 22 shruti and the overtone series). Pick one in Project Settings > Tuning > Scale file. Put your own .scl files in the same folder.
- No Plugins/Ext/extras.plg: the MOD and IT import and export, the custom themes, the factory self test and Capture are part of this image.
EOF
elif [ -n "$card" ]; then
cat > "$dest/README-RELEASE.txt" <<EOF
tracker-ff  ($name)
Unofficial community firmware for the original Polyend Tracker (MK66), a fork of Polyend's Tracker Community Edition with $ntotal open synth engines (5 built in, $nplug plugins).
Not affiliated with, endorsed by or supported by Polyend. Free, non-commercial.
Built from $rev of https://github.com/sbialy/tracker-ff (fork of https://github.com/polyend/tracker-ce).

tracker-ff/LICENSE     the repository license (MIT with the Wholesale Commercial Clone Restriction): free to share, not to sell
tracker-ff/NOTICE.md   third-party notices and attributions
tracker-ff/UPGRADING.md  how to update from 1.3.x, 1.2.0 or 1.1.0 and how to go back, read it before you flash
tracker-ff/CONTRIBUTORS.md  the people who contributed reports, fixes or ideas
tracker-ff/licenses/   the full license text of every bundled third-party component (OpenTracker Apache-2.0, opentracker-synths MIT, opentracker-fx MIT, ...)
tracker-ff/SHA256SUMS  checksums of every file, paths from the card root: shasum -a 256 -c tracker-ff/SHA256SUMS
${plugnotes}
This image links neither libPolySynth.a nor libPolyDSP.a. $nofactory

Install
1. Back up the SD card first. UPGRADING.md lists what to copy.
2. Unzip this whole zip onto the root of the SD card and let the computer replace the files that are there. The zip is laid out like the card: Firmware/ holds the image, Plugins/Synth/, Plugins/Master/ and Plugins/Ext/ the plugins and the MOD and IT module and Effects/ the Sample Editor effects.
3. Put the card in the Tracker, open Config > Firmware > Firmware update, pick the image and confirm. Leave the .ptf of your old version in /Firmware as your way back. The file name of the image must stay as it is.
4. Wait until the Tracker restarts by itself. Config > Info > About names the version.
Flash the image from this zip and not the .ptf alone, because the image needs the card files below.

What the card files do
- Without the synth plugins the projects that use the $nword plugin engines show Missing on those parts. A Missing part keeps its settings and its patch file.
- When you update from 1.2.x or 1.1.0, Plugins/Synth/chip.plg is required: CHIP is no longer in the image and without chip.plg the CHIP parts show Missing.
- Plugins/Ext/extras.plg holds the MOD and IT module. MOD import, IT import and MOD/IT export load it from the card on first use (about a second) and the image does not hold that code. It belongs to this exact image, so after a firmware update copy the extras.plg of the same release. A modit.plg from 1.4.0 is no longer read and can be deleted. Without it or with the one of another release the import and export keys say so and nothing else changes.
- Effects/ holds the optional effects and the nine Sample Editor effects that are no longer in the image (se_*.ptfx: Amplifier, Overdrive, Delay, Bitcrusher, Chorus, Flanger, Equalizer, Limiter, Compressor). Without those files their rows in the Sample Editor show Missing.
- Waves/ holds 143 single cycle waves for ALCO Wave mode (A File and B File), flat in /Waves, from AKWF-FREE by Kristoffer Ekstrand (CC0 1.0, credit in Waves/AKWF-LICENSE.txt). Waves you already have in /Waves stay, nothing there is deleted. The full AKWF-FREE set (4,358 waves) is at https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE
- Tunings/ holds five example Scala tunings (just intonation, Bohlen-Pierce, 22 shruti and the overtone series). Pick one in Project Settings > Tuning > Scale file. Put your own .scl files in the same folder.${patchesline}
EOF
else
cat > "$dest/README-RELEASE.txt" <<EOF
tracker-ff  ($name)
Unofficial community firmware for the original Polyend Tracker (MK66), a fork of Polyend's Tracker Community Edition with $ntotal open synth engines (5 built in, $nplug as plugins that this package does not hold).
Not affiliated with, endorsed by or supported by Polyend. Free, non-commercial.
Built from $rev of https://github.com/sbialy/tracker-ff (fork of https://github.com/polyend/tracker-ce).

tracker-ff/LICENSE     the repository license (MIT with the Wholesale Commercial Clone Restriction): free to share, not to sell
tracker-ff/NOTICE.md   third-party notices and attributions
tracker-ff/UPGRADING.md  how to update from 1.3.x, 1.2.0 or 1.1.0 and how to go back, read it before you flash
tracker-ff/CONTRIBUTORS.md  the people who contributed reports, fixes or ideas
tracker-ff/licenses/   the full license text of every bundled third-party component (OpenTracker Apache-2.0, opentracker-synths MIT, opentracker-fx MIT, ...)
tracker-ff/SHA256SUMS  checksums of every file, paths from the card root: shasum -a 256 -c tracker-ff/SHA256SUMS

This image links neither libPolySynth.a nor libPolyDSP.a. Factory patches, plugins and .ptfx effects are not part of this package (a layout test, not a release).
Install: unzip this whole zip onto the root of the SD card. It is laid out like the card, so Firmware/ holds the image.
Then open Config > Firmware > Firmware update on the Tracker, pick the image and confirm. The file name of the image must stay as it is.
EOF
fi

( cd "$dest" && find . -type f ! -path ./tracker-ff/SHA256SUMS | sort | sed 's|^\./||' | while read -r f; do shasum -a 256 "$f"; done > tracker-ff/SHA256SUMS )

if [ "$zip" = 1 ]; then
  # no wrapper directory: the entries start at Firmware/, Plugins/ ... so the zip unzips onto the card root (-D no directory entries, -X no extra file attributes)
  ( cd "$dest" && rm -f "$out/$name.zip" && zip -qrDX "$out/$name.zip" . -x '*.DS_Store' )
  echo "zip:    $out/$name.zip"
fi
echo "folder: $dest"
find "$dest" -type f | sort | sed "s|^$dest/|  |"
