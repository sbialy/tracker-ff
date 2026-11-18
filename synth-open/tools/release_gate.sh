#!/usr/bin/env bash
# The release gate: the firmware and engine mismatch simulator against the previous release, with a list of known findings.
#   synth-open/tools/release_gate.sh [PNG_DIR]
# Runs ui_shot/fw_engine_mismatch_sim.sh (this tree = the new firmware, OLD_REF = the old one, PLG_DIR = the new plugin set) and FAILS (exit 1)
# when a check goes RED that is not on the allowlist ui_shot/mismatch_known_findings.txt, or when the simulator itself cannot run (exit 2 of the
# sim, no RESULT line, empty log): a gate that could not run is not a pass. A RED check is "known" when it starts with an F<n> id listed there.
# A KNOWN line of the simulator (F1) is a permanently accepted finding, printed by the gate and never counted as RED.
# An allowlisted finding that turns GREEN is reported ("remove it from the allowlist") but does not fail.
# The simulator is generic over the release pair (the tree under test against OLD_REF): what the old release is (CHIP a plugin or built in, field 100,
# synth-order.txt, ...) is read from the old ref, so any older release can be the OLD_REF. A finding that only exists for an older pair
# (F2 and F1 exist for a 1.2.x old side, not for 1.3.0) is RED or KNOWN for that pair only: the gate against tracker-ff-v1.2.0 lists F2 as NEW RED.
#   OLD_REF     git ref of the previous release. Default: the latest published release tag, tracker-ff-v<A.B.C> by version order
#               (a tag with a suffix such as -squash or -dev is not a release), peeled to its commit
#   PLG_DIR     the new plugin set, a flat folder of .plg and .plgi (package_release.sh passes its --modules folder)
#   ALLOWLIST   default ui_shot/mismatch_known_findings.txt
#   LOG         where the simulator log goes, default build/release-gate.log
#   everything else the simulator reads (TOOLCHAIN, PLGO_DIR, ENGINES_OS_REPO, UNICORN_DIR, BRIDGE_AXF, REBUILD, ...) passes through (its new host is the BRIDGE=1 build)
# ABI 1.4 (1.4.0, the plugin files extension: PLUG_HOST_LEVEL 1 -> 2, plug_ext_synth grew service and view at its tail, PLUG_EXT_FILES and PLUG_EXT_STRUM added) is NOT on the
# allowlist: it is intended, so the simulator (scenario 0 and A3) asserts the contract instead. Host level and minor may rise, a struct may only grow at its tail,
# min_host is judged against the host level of each firmware (own level accepted, one above refused), and ui_shot/abi_contract.txt pins the ABI of the tree under test
# (every PLUG_ define, every struct layout as a sha1 of its body, every enum value). A deliberate ABI change therefore needs an edit of abi_contract.txt (regenerate it,
# read the diff). Negative controls (2026-11-15, each RED at once, then reverted): PLUG_HOST_LEVEL 4, a field added to plug_ext_files, a field inserted into the
# 1.3.1 part of plug_ext_synth (that one is RED twice, "keeps every field" and "pinned contract").
# FIRST, before the simulator: the image itself. check_ptf.py --min-tail 0x800 FAILS (exit 1) a .ptf whose last 4 KB flash sector holds fewer
# than 0x800 B. A short last sector is the best fit for the 2026-11-12 brick and the refused dev5 module (section 9:
# the unit's bootloader is suspected of leaving it erased, dev7 with 0xD2C B in the sector loads). The tail pad of synth-open/ext_module.cpp
# (FF_TAIL_PAD, sized by make original from the linked image, 0 to 2,064 B, synth-open/tools/tail_pad.py) is what keeps a release above the line,
# the gate is what notices a .ptf that was not built that way.
#   IMAGE       the .ptf to check. Default build/PolyendTracker_FF_<version>.ptf (make original-release). No image = the gate cannot run = exit 2
#   MIN_TAIL    the least bytes in the last sector, default 0x800
#   PROVEN_END  the end of the last image that was run on the unit, default 0xFC2AC. An image that ends past it FAILS the gate (exit 1, before the
#               simulator). ALLOW_UNPROVEN_END=1 overrides, set it only together with the device row that proved the new end (the release notes
#               cite it). The same check is a warning in make original (check_ptf.py --warn-proven)
#   MODIT_PLG  build/modit.plg: when set, the gate checks it against IMAGE (synth-open/plug/check_ext.py), exit 1 on a module of another image.
#               package_release.sh --modit sets it. Run it after the tail pad (make original builds the module only once the image is final)
#   SDRAM_MIN   the least SDRAM left free, default 16384 B. SDRAM free = 16 MiB minus the .noinit_RAM4 size of the linked image (make original writes
#               'sdram free: N' into BUILD.txt next to the .ptf, engine_budget.py). Below it the gate FAILS (exit 1, before the simulator), nothing else watches the
#               SDRAM (1.4.0 release image: 519,416 B free). A BUILD.txt that is not the image's own (its 'firmware:' line) or has no sdram line = the gate cannot run
#               (exit 2). SDRAM_FREE=N gives the figure directly (tests, the negative control: SDRAM_FREE=1000 is RED). The check counts as one check of the RESULT line
#   IMAGE_CHECK_ONLY=1  stop with exit 0 after the image check (host tests)
# The package step (package_release.sh with --modules or --card) runs this gate; MISMATCH_GATE=skip there skips it and says so. About 1 min
# warm, 3 to 4 min when the cache of build/fw-eng-mismatch is cold (the 1.2.0 host and plugins are rebuilt from the old ref and the module tags).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SIM="$HERE/ui_shot/fw_engine_mismatch_sim.sh"
ALLOWLIST="${ALLOWLIST:-$HERE/ui_shot/mismatch_known_findings.txt}"
LOG="${LOG:-$ROOT/build/release-gate.log}"
MIN_TAIL="${MIN_TAIL:-0x800}"
PROVEN_END="${PROVEN_END:-0xFC2AC}"
SDRAM_MIN="${SDRAM_MIN:-16384}"
if [ -z "${IMAGE:-}" ]; then
    ver="$(sed -n 's/^SYNTH_OPEN_VER[[:space:]]*:\{0,1\}=[[:space:]]*//p' "$ROOT/version.mk" | head -1 | sed 's/-.*//')"
    IMAGE="$ROOT/build/PolyendTracker_FF_${ver}.ptf"
fi
[ -x "$SIM" ] || { echo "release_gate: no simulator at $SIM" >&2; exit 2; }
[ -f "$ALLOWLIST" ] || { echo "release_gate: no allowlist $ALLOWLIST" >&2; exit 2; }

if [ -z "${OLD_REF:-}" ]; then
    OLD_REF="$(git -C "$ROOT" tag --list 'tracker-ff-v[0-9]*' --sort=-v:refname | grep -E '^tracker-ff-v[0-9]+\.[0-9]+\.[0-9]+$' | head -1)"
    [ -n "$OLD_REF" ] || { echo "release_gate: no release tag tracker-ff-v<A.B.C> in this repository, set OLD_REF" >&2; exit 2; }
fi
OLD_COMMIT="$(git -C "$ROOT" rev-parse --verify -q "$OLD_REF^{commit}")" || { echo "release_gate: OLD_REF $OLD_REF is not a commit here" >&2; exit 2; }
[ -f "$IMAGE" ] || { echo "release_gate: no image $IMAGE (make original-release, or set IMAGE=): the tail check cannot run" >&2; exit 2; }
mkdir -p "$(dirname "$LOG")"
if tailout="$(python3 -I "$HERE/check_ptf.py" --min-tail "$MIN_TAIL" --proven "$PROVEN_END" "$IMAGE" 2>&1)"; then
    echo "release gate: image tail ok ($(basename "$IMAGE"), $(printf '%s\n' "$tailout" | sed -n 's/.*highest address \(0x[0-9A-F]*\).*/ends \1/p' | head -1), last sector needs >= $MIN_TAIL B, end within the device-proven $PROVEN_END)"
    printf '%s\n' "$tailout" | grep 'WARNING' | sed 's/^ */release gate: /'
    if [ -n "${MODIT_PLG:-}" ]; then
        if mout="$(python3 -I "$ROOT/synth-open/plug/check_ext.py" "$MODIT_PLG" "$IMAGE" 2>&1)"; then echo "release gate: modit.plg ok ($(printf '%s' "$mout" | sed 's/.*(image id/image id/'))"
        else echo "release gate: FAIL - modit.plg"; printf '%s\n' "$mout" | sed 's/^/  /'; exit 1; fi
    fi
    # SDRAM: 16 MiB minus .noinit_RAM4, from SDRAM_FREE or the BUILD.txt that make original wrote next to this very image
    sdram_free="${SDRAM_FREE:-}"
    if [ -z "$sdram_free" ]; then
        bt="$(dirname "$IMAGE")/BUILD.txt"
        [ -f "$bt" ] || { echo "release gate: no $bt and no SDRAM_FREE: the SDRAM check cannot run" >&2; exit 2; }
        [ "$(sed -n 's/^firmware: //p' "$bt" | head -1)" = "$(basename "$IMAGE")" ] || { echo "release gate: $bt is not the build info of $(basename "$IMAGE"): the SDRAM check cannot run" >&2; exit 2; }
        sdram_free="$(sed -n 's/^sdram free: //p' "$bt" | head -1)"
        [ -n "$sdram_free" ] || { echo "release gate: $bt has no 'sdram free:' line (built before the SDRAM check): rebuild the image" >&2; exit 2; }
    fi
    case "$sdram_free" in ''|*[!0-9-]*) echo "release gate: SDRAM free '$sdram_free' is not a number" >&2; exit 2;; esac
    if [ "$sdram_free" -lt "$SDRAM_MIN" ]; then
        echo "release gate: FAIL - SDRAM: $sdram_free B free, the floor is $SDRAM_MIN B (16 MiB minus .noinit_RAM4)"; exit 1
    fi
    echo "release gate: SDRAM ok ($sdram_free B free of 16 MiB, floor $SDRAM_MIN B)"
    [ -z "${IMAGE_CHECK_ONLY:-}" ] || exit 0
else
    echo "release gate: FAIL - image check"; printf '%s\n' "$tailout" | sed 's/^/  /'; exit 1
fi
echo "release gate: new = $(git -C "$ROOT" rev-parse --short HEAD), old = $OLD_REF ($(git -C "$ROOT" rev-parse --short "$OLD_COMMIT")), allowlist $(basename "$ALLOWLIST")"

OLD_REF="$OLD_REF" "$SIM" ${1:+"$1"} >"$LOG" 2>&1
simrc=$?

known="$(sed -e 's/#.*//' "$ALLOWLIST" | awk -F'|' 'NF >= 2 { gsub(/[ \t]/, "", $1); r = $2; gsub(/^[ \t]+|[ \t]+$/, "", r); if ($1 != "" && r != "") print $1 }' | sort -u)"
# an allowlist line without a reason is not an entry (every line has to say why)
bad_lines="$(sed -e 's/#.*//' "$ALLOWLIST" | awk 'NF && !/\|[ \t]*[^ \t]/' | head -3)"
[ -z "$bad_lines" ] || { echo "release_gate: allowlist lines without '<id> | <reason>':"; echo "$bad_lines" | sed 's/^/  /'; exit 1; }

result="$(grep -E '^RESULT: ' "$LOG" | tail -1)"
if [ -z "$result" ]; then
    echo "release gate: FAIL - the simulator gave no RESULT line (exit $simrc), see $LOG"; tail -5 "$LOG" | sed 's/^/  /'; exit 1
fi
if [ "$simrc" -ne 0 ] && [ "$simrc" -ne 1 ]; then
    echo "release gate: FAIL - the simulator stopped with exit $simrc, see $LOG"; tail -5 "$LOG" | sed 's/^/  /'; exit 1
fi

fails=0; allowed=0
while IFS= read -r line; do
    name="${line#FAIL  }"
    id="$(printf '%s' "$name" | sed -n 's/^\(F[0-9][0-9]*\)\([^0-9].*\)\{0,1\}$/\1/p')"
    if [ -n "$id" ] && printf '%s\n' "$known" | grep -qx "$id"; then
        allowed=$((allowed + 1)); echo "  known   $id  $(printf '%s' "$name" | cut -c1-110)"
    else
        fails=$((fails + 1)); echo "  NEW RED ${id:--}  $name"
    fi
done < <(grep -E '^FAIL  ' "$LOG")

# findings the simulator accepts itself (a KNOWN line, e.g. F1): listed, not a RED check, so they are not in the FAIL count below
while IFS= read -r line; do echo "  accepted ${line#KNOWN }" | cut -c1-150; done < <(grep -E '^KNOWN ' "$LOG")

# a listed finding that is GREEN now
for id in $known; do
    if ! grep -E "^FAIL  $id([^0-9]|\$)" "$LOG" >/dev/null && grep -E "^ok    $id([^0-9]|\$)" "$LOG" >/dev/null; then
        echo "  note: $id is on the allowlist but its check is GREEN, remove it from $(basename "$ALLOWLIST")"
    fi
done

# every RED the simulator counted has to be one of the lines read above, else the log was not understood
counted="$(printf '%s' "$result" | sed -n 's/^RESULT: RED (\([0-9]*\) failing.*/\1/p')"
if [ "${counted:-0}" -ne $((allowed + fails)) ]; then
    echo "release gate: FAIL - the simulator counted ${counted:-0} RED checks, this gate read $((allowed + fails)) FAIL lines in $LOG"; exit 1
fi

# the SDRAM floor above is one more check of this gate: the count in the RESULT line includes it
result="$(printf '%s' "$result" | awk '{ if (match($0, /[0-9]+ checks/)) { n = substr($0, RSTART, RLENGTH - 7) + 1; print substr($0, 1, RSTART - 1) n " checks" substr($0, RSTART + RLENGTH) } else print }')"
echo "release gate: $result, $allowed known, $fails not on the allowlist (log $LOG)"
if [ "$fails" -ne 0 ]; then echo "release gate: FAIL"; exit 1; fi
echo "release gate: PASS"
