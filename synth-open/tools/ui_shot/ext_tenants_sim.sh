#!/bin/bash
# ext_tenants_sim.sh: the theme and self test tenants of /Plugins/Ext/extras.plg (ext_abi.h EXT_SUB_THEME, EXT_SUB_SELFTEST) on the ui_shot host.
# EXT_BIN is the EXTHOOKS=1 binary (the release flavour: the JSON parser and the self test reached through ext_hooks.theme / .selftest, the
# real loader rules on a staged card, make_host_module.py's stand-in file, ext_module.cpp enterModit sets the hooks). PLAIN_BIN is the plain
# sim (both resident, as on Tracker+ and Mini). The `theme N` and `selftest` commands of ui_shot_main.cpp print what happened:
#   T1  extras.plg present: /Themes/Test.json is applied (current 3, its background), the hook is set
#   T2  no extras.plg: the custom theme falls back to the built in default (current 0), no crash
#   T3  a round 1 extras.plg (no sub-slot table): refused, the same fallback
#   T4  the resident parser (plain sim) gives the very theme bytes the module gave in T1
#   S1  extras.plg present: the start code starts the self test from the module (its screen)
#   S2  no extras.plg: the popup "Self test needs extras.plg", the test does not start
#   S3  the resident self test (plain sim) starts as before
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing of m)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
EXT_BIN="${EXT_BIN:-$ROOT/build/ui-shot-ext/ui_shot}"
PLAIN_BIN="${PLAIN_BIN:-$ROOT/build/ui-shot/ui_shot}"
[ -x "$EXT_BIN" ] && [ -x "$PLAIN_BIN" ] || { echo "needs EXT_BIN (EXTHOOKS=1) and PLAIN_BIN ui_shot binaries"; exit 2; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/ext_tenants.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
fails=0; checks=0
pass() { checks=$((checks+1)); printf 'ok    %s\n' "$1"; }
fail() { checks=$((checks+1)); fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "${2:-}"; }
# card NAME [make_host_module.py args | none]
card() {
    local sd="$WORK/$1"; shift; mkdir -p "$sd/Themes"
    printf '{ "background": "#123456", "topBar": { "text": "#FF00FF", "background": "#000000", "frame": "#222222" } }' > "$sd/Themes/Test.json"
    if [ "${1:-}" != none ]; then mkdir -p "$sd/Plugins/Ext"; python3 -I "$ROOT/synth-open/tools/modit/make_host_module.py" "$sd/Plugins/Ext/extras.plg" "$@"; fi
    echo "$sd"
}
runsim() { ASAN_OPTIONS=detect_leaks=0 "$1" "$2" "wait 500; $3" 2>&1 | grep -E '^(theme|selftest) '; }
expect() { if grep -qE "$2" <<<"$3"; then pass "$1"; else fail "$1" "got: $(tr '\n' '|' <<<"$3")"; fi; }

out1=$(runsim "$EXT_BIN" "$(card t1)" "theme 3"); expect "T1 extras.plg: custom theme applied through the module hook" '^theme 3 ok 1 current 3 count 4 bg 123456 .* hook set$' "$out1"
out=$(runsim "$EXT_BIN" "$(card t2 none)" "theme 3"); expect "T2 no extras.plg: default theme, hook null" '^theme 3 ok 0 current 0 count 4 .* hook null$' "$out"
out=$(runsim "$EXT_BIN" "$(card t3 --no-table)" "theme 3"); expect "T3 round 1 extras.plg (no table): refused, default theme" '^theme 3 ok 0 current 0 .* hook null$' "$out"
out4=$(runsim "$PLAIN_BIN" "$(card t4 none)" "theme 3"); expect "T4 resident parser applies the theme" '^theme 3 ok 1 current 3 count 4 bg 123456 .* hook resident$' "$out4"
c1=$(sed -n 's/.* custom \([0-9a-f]*\) .*/\1/p' <<<"$out1"); c4=$(sed -n 's/.* custom \([0-9a-f]*\) .*/\1/p' <<<"$out4")
if [ -n "$c1" ] && [ "$c1" = "$c4" ]; then pass "T4 module and resident parser write the same theme ($c1)"; else fail "T4 same theme bytes" "module $c1 resident $c4"; fi

sd=$(card s1); out=$(runsim "$EXT_BIN" "$sd" "selftest; shot $WORK/s1.png"); expect "S1 extras.plg: self test starts from the module" '^selftest started 1 .* hook set$' "$out"
sd=$(card s2 none); out=$(runsim "$EXT_BIN" "$sd" "selftest; shot $WORK/s2.png"); expect "S2 no extras.plg: not started" '^selftest started 0 .* hook null$' "$out"
sd=$(card s3 none); out=$(runsim "$PLAIN_BIN" "$sd" "selftest; shot $WORK/s3.png"); expect "S3 resident self test starts" '^selftest started 1 .* hook resident$' "$out"
if [ -f "$WORK/s1.png" ] && [ -f "$WORK/s3.png" ] && cmp -s "$WORK/s1.png" "$WORK/s3.png"; then pass "S1 the module's test screen is the resident one"; else fail "S1 test screen" "s1.png and s3.png differ"; fi
if [ -f "$WORK/s2.png" ] && ! cmp -s "$WORK/s2.png" "$WORK/s3.png"; then pass "S2 the popup screen, not the test screen"; else fail "S2 screen" "no popup screen"; fi
[ -n "${KEEP:-}" ] && cp "$WORK"/*.png "$KEEP"/ 2>/dev/null

if [ "$fails" = 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
