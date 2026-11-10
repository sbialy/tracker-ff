#!/usr/bin/env bash
# ext_inert.sh UI_PLAIN UI_EXT OUTDIR [MODULE.plg]
# The runtime extension hooks (EXTHOOKS=1 build) leave the UI as it is when no module runs: the same screens as a plain ui_shot,
# without develop mode, in develop mode with no module, with a module the image refuses (the host image id is 0, so any real module
# is "another image", and the host slot is not the address the module was linked for, which the loader checks first), and with the crash marker of a last boot (skipped, marker removed). The status line of each develop boot is
# checked too. Exit 0 when every case passes.
set -uo pipefail
P=$1; X=$2; O=$3; MOD=${4:-}; mkdir -p "$O"; rc=0
SCRIPT='wait 500;shot S/pattern.png;tap Config;wait 800;shot S/config.png;tap Master;wait 800;shot S/master.png;tap Pattern;wait 500;pad 2;wait 1000;shot S/pad.png'
run() {  # name binary develop(0|1) module(0|1) marker(0|1)
  local d=$O/$1; rm -rf "$d"; mkdir -p "$d/sd" "$d/shots"
  [ "$3" = 1 ] && : > "$d/sd/.develop"
  if [ "$4" = 1 ] || [ "$5" = 1 ]; then mkdir -p "$d/sd/Plugins/Ext"; fi
  [ "$4" = 1 ] && cp "$MOD" "$d/sd/Plugins/Ext/module.plg"
  [ "$5" = 1 ] && echo ext > "$d/sd/Plugins/Ext/.loading"
  timeout 300 "$2" "$d/sd" "${SCRIPT//S\//$d/shots/}" > "$d/log.txt" 2>&1; echo "exit $?" >> "$d/log.txt"
}
same() {  # case reference
  local bad=0
  for f in pattern config master pad; do cmp -s "$O/$1/shots/$f.png" "$O/$2/shots/$f.png" || { bad=1; echo "    $f.png differs from $2"; }; done
  grep -q "exit 0" "$O/$1/log.txt" || { bad=1; echo "    ui_shot did not exit 0: $(tail -2 "$O/$1/log.txt" | tr '\n' ' ')"; }
  return $bad
}
verdict() {  # name ok(0|1) text
  if [ "$2" = 0 ]; then echo "PASS $1: $3"; else echo "FAIL $1: $3"; rc=1; fi
}
status() { cat "$O/$1/sd/Plugins/Ext/status.txt" 2>/dev/null | tr -d '\n'; }

run ref-plain   "$P" 0 0 0
run ref-develop "$P" 1 0 0
run off         "$X" 0 0 0
run none        "$X" 1 0 0
same off ref-plain;   verdict off $? "no develop mode: screens identical to plain ui_shot, no status file ($( [ -e "$O/off/sd/Plugins/Ext/status.txt" ] && echo present || echo absent))"
same none ref-develop; verdict none $? "develop mode, no module: screens identical to plain develop mode (no Ext folder, nothing written)"
if [ -n "$MOD" ]; then
  run refused "$X" 1 1 0
  same refused ref-develop; r=$?; s=$(status refused); case $s in "refused: linked for another address"*|"refused: built for another firmware image"*) ;; *) r=1;; esac
  verdict refused $r "develop mode, module of another image: same screens, status \"$s\""
  [ -e "$O/refused/sd/Plugins/Ext/.loading" ] && verdict refused-marker 1 "marker left on the card" || verdict refused-marker 0 "no crash marker left"
  run crashed "$X" 1 1 1
  same crashed ref-develop; r=$?; s=$(status crashed); case $s in "skipped: the module crashed the last boot"*) ;; *) r=1;; esac
  [ -e "$O/crashed/sd/Plugins/Ext/.loading" ] && r=1
  verdict crashed $r "marker of a crashed boot: module skipped, marker removed, same screens, status \"$s\""
fi
echo "RESULT: $([ $rc = 0 ] && echo GREEN || echo RED)"
exit $rc
