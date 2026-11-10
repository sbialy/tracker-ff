#!/usr/bin/env bash
# features.sh UI_SHOT OUTDIR [--update]
# Config > Features on the Mac (ref/features-design.md, EXTHOOKS=1 ui_shot, plain or SAN): the pages against the goldens in
# goldens/features/ (byte equal PNGs, --update writes them), the files the page writes, damaged files, Reset config, Restart now,
# Developer mode hidden without /.develop (D8), Shift at power on (D4) and the stock Metronome > State crash (null description).
# A SAN build's sanitizer reports fail the case, except the known stock mtWavetableEnvelopes.cpp:206 (ref/features-design.md 6).
# Exit 0 when every case passes.
set -uo pipefail
U=$1; O=$2; UPD=${3:-}; mkdir -p "$O"; rc=0
G=$(cd "$(dirname "$0")" && pwd)/goldens/features; mkdir -p "$G"
NAV='tap Config;wait 800;tap Down 4;wait 300;tap F8;wait 500'   # Config, Features (base row 4), into the list
run() {  # name env script: OUT/name/sd is the card, OUT/name/log.txt the output
  local d=$O/$1; rm -rf "$d/shots"; mkdir -p "$d/sd" "$d/shots"
  env $2 timeout 300 "$U" "$d/sd" "${3//S\//$d/shots/}" > "$d/log.txt" 2>&1; echo "exit $?" >> "$d/log.txt"
}
fresh() { rm -rf "$O/$1"; mkdir -p "$O/$1/sd"; }
verdict() { if [ "$2" = 0 ]; then echo "PASS $1: $3"; else echo "FAIL $1: $3"; rc=1; fi; }
has() { grep -qE "$2" "$O/$1/log.txt"; }
san() { ! grep -E "runtime error|AddressSanitizer|SUMMARY:" "$O/$1/log.txt" | grep -v "mtWavetableEnvelopes.cpp:206" | grep -q .; }
golden() {  # case shot: equal to the golden (or written with --update)
  local f=$O/$1/shots/$2.png
  [ -f "$f" ] || { echo "    $2.png not written"; return 1; }
  if [ "$UPD" = --update ]; then cp "$f" "$G/$2.png"; return 0; fi
  cmp -s "$f" "$G/$2.png" || { echo "    $2.png differs from goldens/features/$2.png"; return 1; }
}
end() {  # case: exit 0 and a clean sanitizer log
  has "$1" "^exit 0" || { echo "    no exit 0: $(tail -2 "$O/$1/log.txt" | tr '\n' ' ')"; return 1; }
  san "$1" || { echo "    sanitizer: $(grep -E "runtime error|SUMMARY" "$O/$1/log.txt" | grep -v mtWavetable | head -2 | tr '\n' ' ')"; return 1; }
}
cfg() { cat "$O/$1/sd/Features/$2.cfg" 2>/dev/null | tr '\n' ' '; }

# 1 list and pages, card without /.develop: Restart mode and USB audio, no Developer mode (D8), nothing written by looking
fresh list
run list "" "wait 300;tap Config;wait 800;shot S/f1-config.png;tap Down 4;wait 300;tap F8;wait 500;shot S/f2-features.png;tap F8;wait 500;shot S/f3-restart-off.png;features"
r=0; end list || r=1; for s in f1-config f2-features f3-restart-off; do golden list $s || r=1; done
has list "feature develop on 0 listed 0" || { echo "    Developer mode listed without /.develop"; r=1; }
[ -e "$O/list/sd/Features" ] && { echo "    /Features written by a start that changed nothing"; r=1; }
verdict list $r "Config base list, Features list, Restart mode page off, Developer mode hidden, no /Features written"

# 2 Restart mode on through the page: the params appear, the file is written after the debounce, values persist across a start
fresh restart
run restart "" "$NAV;tap F8;wait 500;tap F8;wait 300;tap Down;wait 200;tap F8;wait 600;shot S/f4-restart-on.png;tap Down 2;wait 300;tap F8;wait 300;tap Down;wait 200;tap F8;wait 600;shot S/f5-param.png;wait 1500;restartmode;resets"
r=0; end restart || r=1; golden restart f4-restart-on || r=1; golden restart f5-param || r=1
c=$(cfg restart restart); [ "$c" = "feature 1 id restart version 1 on 1 button 1 return 1 reckey 1 " ] || { echo "    restart.cfg: $c"; r=1; }
has restart "start state 1" || { echo "    Power return Start did not set the start state"; r=1; }
run restart "" "restartmode;features"   # the next start on the same card
has restart "restart mode 1 leaving 0 platform 1 file 0 cfg 1" && has restart "feature restart on 1 listed 1 value On" || r=1
verdict restart $r "State On shows the params, Power return Start, file $(cfg restart restart| cut -c1-60)..., kept across a start"

# 3 USB audio on: Restart, the Restart now row, the restart picks the new flag (the model's usb_init)
fresh usb
run usb "" "$NAV;tap Down 1;wait 300;tap F8;wait 500;tap F8;wait 300;tap Down;wait 200;tap F8;wait 600;shot S/f6-usb-restart.png;features;usbaudio;tap Down 2;wait 300;tap F8;wait 3000;resets;usbaudio"
r=0; end usb || r=1; golden usb f6-usb-restart || r=1
has usb "feature usbaudio on 1 listed 1 value Restart" && has usb "usb audio running 0 flag 1" || r=1
has usb "mcu reset \(restart\): start RUN" && has usb "usb audio running 1 flag 1" || { echo "    Restart now did not restart into USB audio"; r=1; }
[ "$(cfg usb usbaudio)" = "feature 1 id usbaudio version 1 on 1 " ] || { echo "    usbaudio.cfg: $(cfg usb usbaudio)"; r=1; }
verdict usb $r "USB audio On reads Restart, Restart now saves and restarts, the next start enumerates with audio"

# 4 the boot flag follows the file: a card without usbaudio.cfg on a unit whose flag is on (Reset config elsewhere, file deleted)
fresh usbsync
run usbsync "UISHOT_USB_FLAG=1" "features;usbaudio"
r=0; end usbsync || r=1
has usbsync "feature usbaudio on 0 listed 1 value Restart" && has usbsync "usb audio running 1 flag 0" || r=1
verdict usbsync $r "flag on, no file: flag cleared at start, row reads Restart (this session still has audio)"

# 5 an image without USB audio does not list it
fresh nousb
run nousb "UISHOT_NO_USB_AUDIO=1" "$NAV;shot S/f7-no-usb.png;features"
r=0; end nousb || r=1; golden nousb f7-no-usb || r=1; has nousb "feature usbaudio" && r=1
verdict nousb $r "image without USB_AUDIO=1: no USB audio row"

# 6 damaged files: defaults, and the old marker switches the restart mode on (and is removed)
fresh damaged; mkdir -p "$O/damaged/sd/Features"
head -c 4000 /dev/urandom > "$O/damaged/sd/Features/restart.cfg"
printf 'feature 1\non 7\next 9\n' > "$O/damaged/sd/Features/develop.cfg"
python3 -c "print('feature 1\non 1\n' + 'x' * 600)" > "$O/damaged/sd/Features/usbaudio.cfg"
: > "$O/damaged/sd/.autorestart"; : > "$O/damaged/sd/.develop"
run damaged "" "$NAV;shot S/f8-damaged.png;features;restartmode"
r=0; end damaged || r=1; golden damaged f8-damaged || r=1
has damaged "feature restart on 1 listed 1" && has damaged "feature develop on 0 listed 1" && has damaged "feature usbaudio on 0" || r=1
[ -e "$O/damaged/sd/.autorestart" ] && { echo "    marker not removed"; r=1; }
verdict damaged $r "random restart.cfg = defaults then the marker switched it on (removed), develop.cfg bad values = defaults, usbaudio.cfg over 384 B refused whole = defaults"

# 7 Developer mode with /.develop: listed, Extension Load needs a restart. Shift at power on: the module is skipped, Safe (D4)
fresh develop; : > "$O/develop/sd/.develop"
run develop "" "$NAV;tap Down 1;wait 300;tap F8;wait 500;tap F8;wait 300;tap Down;wait 200;tap F8;wait 600;shot S/f9-develop.png;features"
r=0; end develop || r=1; golden develop f9-develop || r=1; has develop "feature develop on 1 listed 1 value Restart" || r=1
verdict develop $r "/.develop: Developer mode listed, State On reads Restart (a switch that takes effect at the next start)"
fresh safe; : > "$O/safe/sd/.develop"; mkdir -p "$O/safe/sd/Plugins/Ext"; echo x > "$O/safe/sd/Plugins/Ext/module.plg"
run safe "UISHOT_SHIFT_BOOT=1" "features"
r=0; end safe || r=1; has safe "feature develop on 0 listed 1 value Off" || r=1
[ -e "$O/safe/sd/Plugins/Ext/status.txt" ] && { echo "    the loader ran: $(cat "$O/safe/sd/Plugins/Ext/status.txt")"; r=1; }
mkdir -p "$O/safe/sd/Features"; printf 'feature 1\non 1\next 1\n' > "$O/safe/sd/Features/develop.cfg"
run safe "UISHOT_SHIFT_BOOT=1" "features"
has safe "feature develop on 1 listed 1 value Safe" || r=1
[ -e "$O/safe/sd/Plugins/Ext/status.txt" ] && { echo "    the loader ran with Shift held"; r=1; }
run safe "" "features"
has safe "feature develop on 1 listed 1 value Refused" && [ -e "$O/safe/sd/Plugins/Ext/status.txt" ] || { echo "    no Shift: the loader did not run"; r=1; }
verdict safe $r "Shift at power on: the module is not read (Safe), built-ins stay as they are, without Shift it is read (Refused: not a module)"

# 8 Reset config (D5): every feature to its defaults, /Features/<id>.cfg removed
fresh reset; mkdir -p "$O/reset/sd/Features"
printf 'feature 1\non 1\nbutton 1\nreturn 1\nreckey 1\n' > "$O/reset/sd/Features/restart.cfg"
printf 'feature 1\non 1\n' > "$O/reset/sd/Features/usbaudio.cfg"; : > "$O/reset/sd/Features/restart.cfg.bak"
run reset "" "features;tap Config;wait 800;tap Down 5;wait 300;tap F8;wait 500;tap Down 3;wait 300;tap F8;wait 500;tap F8;wait 2000;features;restartmode;resets"
r=0; end reset || r=1
has reset "feature restart on 0 listed 1 value Off" && has reset "feature usbaudio on 0 listed 1 value Off" || r=1
ls "$O/reset/sd/Features"/*.cfg* > /dev/null 2>&1 && { echo "    left: $(ls "$O/reset/sd/Features")"; r=1; }
has reset "restart mode 0 leaving 0 platform 0" && has reset "start state 0" || r=1
verdict reset $r "Reset config: restart mode and USB audio off, power return off, every feature file removed"

# 9 stock bug: Metronome > State has no description, the hidden description box was drawn with a null text (crash on the Mac)
fresh nulldesc
run nulldesc "" "tap Config;wait 800;tap Down 3;wait 300;tap F8;wait 500;tap F8;wait 500;shot S/f10-metronome.png;tap F7;wait 300"
r=0; end nulldesc || r=1; golden nulldesc f10-metronome || r=1; has nulldesc "signal 11" && r=1
verdict nulldesc $r "Metronome > State value picker opens (no description), no crash"

echo "RESULT: $([ $rc = 0 ] && echo GREEN || echo RED)$([ "$UPD" = --update ] && echo ' (goldens written)')"
exit $rc
