#!/usr/bin/env bash
# The release gate: the firmware and engine mismatch simulator against the previous release, with a list of known findings.
#   synth-open/tools/release_gate.sh [PNG_DIR]
# Runs ui_shot/fw_engine_mismatch_sim.sh (this tree = the new firmware, OLD_REF = the old one, PLG_DIR = the new plugin set) and FAILS (exit 1)
# when a check goes RED that is not on the allowlist ui_shot/mismatch_known_findings.txt, or when the simulator itself cannot run (exit 2 of the
# sim, no RESULT line, empty log): a gate that could not run is not a pass. A RED check is "known" when it starts with an F<n> id listed there.
# A KNOWN line of the simulator (F1) is a permanently accepted finding, printed by the gate and never counted as RED.
# An allowlisted finding that turns GREEN is reported ("remove it from the allowlist") but does not fail.
#   OLD_REF     git ref of the previous release. Default: the latest published release tag, tracker-ff-v<A.B.C> by version order
#               (a tag with a suffix such as -squash or -dev is not a release), peeled to its commit
#   PLG_DIR     the new plugin set, a flat folder of .plg and .plgi (package_release.sh passes its --modules folder)
#   ALLOWLIST   default ui_shot/mismatch_known_findings.txt
#   LOG         where the simulator log goes, default build/release-gate.log
#   everything else the simulator reads (TOOLCHAIN, PLG12_DIR, ENGINES_OS_REPO, CHIP_PLUGIN_SRC, REBUILD, ...) passes through
# FIRST, before the simulator: the image itself. check_ptf.py --min-tail 0x800 FAILS (exit 1) a .ptf whose last 4 KB flash sector holds fewer
# than 0x800 B. A short last sector is the best fit for the 2026-11-12 brick and the refused dev5 module (section 9:
# the unit's bootloader is suspected of leaving it erased, dev7 with 0xD2C B in the sector loads). The tail pad of synth-open/ext_module.cpp
# (FF_TAIL_PAD, 4,608 B) is what keeps a release above the line, the gate is what notices when a later change moves it below.
#   IMAGE       the .ptf to check. Default build/PolyendTracker_FF_<version>.ptf (make original-release). No image = the gate cannot run = exit 2
#   MIN_TAIL    the least bytes in the last sector, default 0x800
# The package step (package_release.sh with --modules or --card) runs this gate; MISMATCH_GATE=skip there skips it and says so. About 1 min
# warm, 3 to 4 min when the cache of build/fw-eng-mismatch is cold (the 1.2.0 host and plugins are rebuilt from the old ref and the module tags).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SIM="$HERE/ui_shot/fw_engine_mismatch_sim.sh"
ALLOWLIST="${ALLOWLIST:-$HERE/ui_shot/mismatch_known_findings.txt}"
LOG="${LOG:-$ROOT/build/release-gate.log}"
MIN_TAIL="${MIN_TAIL:-0x800}"
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
if tailout="$(python3 -I "$HERE/check_ptf.py" --min-tail "$MIN_TAIL" "$IMAGE" 2>&1)"; then
    echo "release gate: image tail ok ($(basename "$IMAGE"), $(printf '%s\n' "$tailout" | sed -n 's/.*highest address \(0x[0-9A-F]*\).*/ends \1/p' | head -1), last sector needs >= $MIN_TAIL B)"
else
    echo "release gate: FAIL - image check"; printf '%s\n' "$tailout" | sed 's/^/  /'; exit 1
fi
echo "release gate: new = $(git -C "$ROOT" rev-parse --short HEAD), old = $OLD_REF ($(git -C "$ROOT" rev-parse --short "$OLD_COMMIT")), allowlist $(basename "$ALLOWLIST")"

FW12_REF="$OLD_COMMIT" "$SIM" ${1:+"$1"} >"$LOG" 2>&1
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

echo "release gate: $result, $allowed known, $fails not on the allowlist (log $LOG)"
if [ "$fails" -ne 0 ]; then echo "release gate: FAIL"; exit 1; fi
echo "release gate: PASS"
