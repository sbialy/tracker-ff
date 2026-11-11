#!/usr/bin/env bash
# Package a tracker-ff (synth-open) firmware image with the license texts that must travel with it.
#   synth-open/tools/package_release.sh <image.ptf> [out_dir] [--zip] [--card CARD_DIR | --modules MODULES_DIR [--effects PTFX_DIR] [--with GROUP]]
# Makes <out_dir>/<image name without .ptf>/ (default out_dir: build/release) with
#   the .ptf, LICENSE, NOTICE.md, README-RELEASE.txt, SHA256SUMS and licenses/ (one text per bundled third-party component).
# Texts that exist only as source file headers (FastCRC, Polyverb, FatFs, PJRC Teensy core and audio library) are copied out
# of those headers verbatim (the leading comment block of the file). Never includes factory patches.
# --card CARD_DIR copies the card files of a PLUG_HOST release, laid out as on the SD card (Plugins/Synth/*.plg *.plgi,
# Plugins/Master/*.plg, optional Effects/*.ptfx), into the package next to the .ptf (the 1.2.0 release layout).
# --modules MODULES_DIR does the same from the pinned plugin modules of the four module repositories (python3 -I engines/external.py
# fetch MODULES_DIR): it checks them against engines/external.lock.json and engines/engines.json first, lays the engines of
# engines.json out in Plugins/Synth (.plg + .plgi) and the inserts in Plugins/Master, and copies the notice files of every module
# repository (MODULES_DIR/<set>/: NOTICE.md, LICENSE.md of the freeware modules, licence texts) to licenses/plugins/<repository>/.
# --effects PTFX_DIR adds the .ptfx of that folder as Effects/.
# --with GROUP (needs --modules, repeatable) switches an optional group of engines/engines.json on: formflux = the synth plugins FORM and
# FLUX (Plugins/Synth) and their 11 factory presets (Patches/FORM, Patches/FLUX, from MODULES_DIR/Patches, fetch it with the same
# --with). Off by default: the package is then byte for byte what it was before the switch existed. The firmware image is the same.
# PKG_REV=<commit> names that commit in README-RELEASE.txt instead of the HEAD of this repository (to rebuild an old package).
# With --zip the folder is also zipped next to itself (<name>.zip, needs zip).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ptf=""; out="$root/build/release"; zip=0; card=""; modules=""; effects=""; groups=(); with=()
while [ $# -gt 0 ]; do
  case "$1" in
    --zip) zip=1 ;;
    --card) shift; card="${1:?--card needs a directory}"; [ -d "$card" ] || { echo "no such card dir: $card" >&2; exit 2; } ;;
    --modules) shift; modules="${1:?--modules needs a directory}"; [ -d "$modules" ] || { echo "no such modules dir: $modules" >&2; exit 2; } ;;
    --with) shift; groups+=("${1:?--with needs a group}"); with+=(--with "$1") ;;
    --effects) shift; effects="${1:?--effects needs a directory}"; [ -d "$effects" ] || { echo "no such effects dir: $effects" >&2; exit 2; } ;;
    *) if [ -z "$ptf" ]; then ptf="$1"; else out="$1"; fi ;;
  esac
  shift
done
[ -n "$ptf" ] && [ -f "$ptf" ] || { echo "usage: $0 <image.ptf> [out_dir] [--zip]" >&2; exit 2; }
case "$(basename "$ptf")" in *.ptf) ;; *) echo "not a .ptf: $ptf" >&2; exit 2 ;; esac

[ ${#groups[@]} = 0 ] || [ -n "$modules" ] || { echo "--with needs --modules" >&2; exit 2; }
name="$(basename "$ptf" .ptf)"
dest="$out/$name"
rm -rf "$dest"
mkdir -p "$dest/licenses"

cp "$ptf" "$dest/"
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
  [ ${#groups[@]} = 0 ] || [ ! -d "$modules/Patches" ] || cp -R "$modules/Patches" "$card/Patches"
  python3 - "$root/engines/external.lock.json" "$root/engines/engines.json" "$modules" "$dest/licenses/plugins" <<'PY'
import json, os, shutil, sys
sets = json.load(open(sys.argv[1]))["sets"]
for name, s in sets.items():
    d = os.path.join(sys.argv[4], s["repo"].split("/")[-1])
    os.makedirs(d, exist_ok=True)
    for f in s.get("notice_files", {}):
        shutil.copyfile(os.path.join(sys.argv[3], name, f), os.path.join(d, f))
PY
fi
if [ -n "$card" ]; then
  for d in Plugins/Synth Plugins/Master; do
    ls "$card/$d"/*.plg > /dev/null 2>&1 || { echo "no .plg in $card/$d" >&2; exit 1; }
  done
  cp -R "$card/Plugins" "$dest/Plugins"
  [ ! -d "$card/Effects" ] || cp -R "$card/Effects" "$dest/Effects"
  [ ! -d "$card/Patches" ] || cp -R "$card/Patches" "$dest/Patches"
fi
cp "$root/LICENSE" "$root/NOTICE.md" "$dest/"

# Full license texts that are files in the repository
copy() { cp "$root/$1" "$dest/licenses/$2" || { echo "missing license file $1" >&2; exit 1; }; }
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
  local src="$root/$1" outf="$dest/licenses/$2"
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
# The words of the README that depend on the optional groups (none on: 13 plugin engines, CHIP among them since 1.3.0)
nplug=13; nword=thirteen; ntotal=18; freeware="PHZ, PLK, Console, Tube and Glue"
nofactory="Factory patches are not part of this package."; patchesline=""
for g in ${groups[@]+"${groups[@]}"}; do
  case "$g" in
    formflux) nplug=$((nplug + 2)); nword=fifteen; ntotal=$((ntotal + 2)); freeware="PHZ, PLK, FORM, FLUX, Console, Tube and Glue"
              nofactory="Factory patches are not part of this package, except the 11 presets of FORM and FLUX."
              patchesline="
Patches/ (the FORM and FLUX presets) goes to /Patches/ on the card, next to the folders of your own patches." ;;
    *) echo "unknown group: $g" >&2; exit 2 ;;
  esac
done
plugnotes=""
[ -z "$modules" ] || plugnotes="licenses/plugins/<repository>/   the notices and licences of the plugins, per module repository ($freeware are freeware: LICENSE.md there)
"
if [ -n "$card" ]; then
cat > "$dest/README-RELEASE.txt" <<EOF
tracker-ff  ($name)
Unofficial community firmware for the original Polyend Tracker (MK66), a fork of Polyend's Tracker Community Edition with $ntotal open synth engines (5 built in, $nplug plugins).
Not affiliated with, endorsed by or supported by Polyend. Free, non-commercial.
Built from commit $rev of https://github.com/sbialy/tracker-ff (fork of https://github.com/polyend/tracker-ce).

LICENSE       the repository license (MIT with the Wholesale Commercial Clone Restriction): free to share, not to sell
NOTICE.md     third-party notices and attributions
licenses/     the full license text of every bundled third-party component (OpenTracker Apache-2.0, opentracker-synths MIT, opentracker-fx MIT, ...)
${plugnotes}
This image links neither libPolySynth.a nor libPolyDSP.a. $nofactory
Copy the .ptf to /Firmware on the SD card (Config > Firmware, the file name must stay as it is) and keep only one .ptf there.
Plugins/Synth/ goes to /Plugins/Synth/ and Plugins/Master/ goes to /Plugins/Master/ on the card. Without the synth plugins the projects that use the $nword plugin engines show Missing on those parts.
Effects/ (optional) goes to /Effects/ on the card.${patchesline}
EOF
else
cat > "$dest/README-RELEASE.txt" <<EOF
tracker-ff  ($name)
Unofficial community firmware for the original Polyend Tracker (MK66), a fork of Polyend's Tracker Community Edition with 16 open synth engines.
Not affiliated with, endorsed by or supported by Polyend. Free, non-commercial.
Built from commit $rev of https://github.com/sbialy/tracker-ff (fork of https://github.com/polyend/tracker-ce).

LICENSE       the repository license (MIT with the Wholesale Commercial Clone Restriction): free to share, not to sell
NOTICE.md     third-party notices and attributions
licenses/     the full license text of every bundled third-party component (OpenTracker Apache-2.0, opentracker-synths MIT, opentracker-fx MIT, ...)

This image links neither libPolySynth.a nor libPolyDSP.a. Factory patches and .ptfx plugins are not part of this package.
Copy only the .ptf to the SD card (Config > Firmware). The file name must stay as it is.
EOF
fi

( cd "$dest" && find . -type f ! -name SHA256SUMS | sort | sed 's|^\./||' | while read -r f; do shasum -a 256 "$f"; done > SHA256SUMS )

if [ "$zip" = 1 ]; then
  ( cd "$out" && rm -f "$name.zip" && zip -qr "$name.zip" "$name" )
  echo "zip:    $out/$name.zip"
fi
echo "folder: $dest"
find "$dest" -type f | sort | sed "s|^$dest/|  |"
