#!/usr/bin/env bash
# features.sh UI_SHOT OUTDIR [--update]
# Config > Features on the Mac (EXTHOOKS=1 ui_shot, plain or SAN): the pages against the goldens in
# goldens/features/ (byte equal PNGs, --update writes them), the files the page writes, damaged files, Reset config, Restart now,
# Developer mode hidden without /.develop (D8), Shift at power on (D4) and the stock Metronome > State crash (null description).
# A SAN build's sanitizer reports fail the case, except the known stock mtWavetableEnvelopes.cpp:206.
# Exit 0 when every case passes.
set -uo pipefail
# The developer view of these screens prints "free heap": the goldens hold the fixed figure of the host heap, not the device heap model
# (host_heapmodel.cpp, on by default), whose figure moves with every allocation of the image. The model has its own sims.
export UISHOT_HEAP=host
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

# 1 list, card without /.develop (a): none of Restart mode, Developer mode, Demo start is listed while Off (D8), Capture is listed and on (2.0.0: no /.develop gate), USB audio is the first row,
#   nothing written by looking
fresh list
run list "" "wait 300;tap Config;wait 800;shot S/f1-config.png;tap Down 4;wait 300;tap F8;wait 500;shot S/f2-features.png;features"
r=0; end list || r=1; for s in f1-config f2-features; do golden list $s || r=1; done
has list "feature restart on 0 listed 0" || { echo "    Restart mode listed without /.develop while Off"; r=1; }
has list "feature develop on 0 listed 0" || { echo "    Developer mode listed without /.develop"; r=1; }
has list "feature demo on 0 listed 0" || { echo "    Demo start listed without /.develop while Off"; r=1; }
has list "feature capture on 1 listed 1 value On" || { echo "    Capture not listed, or not on by default, without /.develop"; r=1; }
has list "feature usbaudio on 0 listed 1" || { echo "    USB audio not listed"; r=1; }
[ -e "$O/list/sd/Features" ] && { echo "    /Features written by a start that changed nothing"; r=1; }
verdict list $r "no /.develop, Restart mode Off: Restart mode, Developer mode and Demo start not listed, Capture listed and On, USB audio first, no /Features written"

# 1b (b) with /.develop both rows are listed, Restart mode first, its page opens (Off)
fresh listdev; : > "$O/listdev/sd/.develop"
run listdev "" "$NAV;shot S/f2d-features-develop.png;tap F8;wait 500;shot S/f3-restart-off.png;features"
r=0; end listdev || r=1; for s in f2d-features-develop f3-restart-off; do golden listdev $s || r=1; done
has listdev "feature restart on 0 listed 1 value Off" || { echo "    Restart mode not listed with /.develop"; r=1; }
has listdev "feature develop on 0 listed 1" || { echo "    Developer mode not listed with /.develop"; r=1; }
has listdev "feature demo on 0 listed 1" || { echo "    Demo start not listed with /.develop"; r=1; }
has listdev "feature capture on 1 listed 1" || { echo "    Capture not listed, or not on by default, with /.develop"; r=1; }
[ -e "$O/listdev/sd/Features" ] && { echo "    /Features written by a start that changed nothing"; r=1; }
verdict listdev $r "/.develop: Restart mode, Developer mode, Demo start and Capture all listed, Restart mode page off, no /Features written"

# 1c (c) no /.develop but Restart mode On in its file: still listed (so it can be switched off), Developer mode still hidden. The page opens from the
#    first row and State Off through it is written and keeps the row for the rest of the session
fresh liston; mkdir -p "$O/liston/sd/Features"; printf 'feature 1\non 1\n' > "$O/liston/sd/Features/restart.cfg"
run liston "" "features;$NAV;shot S/f2o-features-restart-on.png;tap F8;wait 500;tap F8;wait 300;tap Up;wait 200;tap F8;wait 600;features;wait 1500;restartmode"
r=0; end liston || r=1; golden liston f2o-features-restart-on || r=1
has liston "feature restart on 1 listed 1 value On" || { echo "    Restart mode On is not listed without /.develop"; r=1; }
has liston "feature develop on 0 listed 0" || { echo "    Developer mode listed without /.develop"; r=1; }
has liston "feature restart on 0 listed 1 value Off" || { echo "    State Off through the page did not switch it off, or the row left the list"; r=1; }
has liston "restart mode 0 leaving 0 platform 0" || { echo "    switching it off did not reach the platform"; r=1; }
run liston "" "features"   # the next start on the same card: Off now, the row is gone
has liston "feature restart on 0 listed 0" || { echo "    Restart mode Off listed again after a start without /.develop"; r=1; }
verdict liston $r "no /.develop, Restart mode On: still listed, switched Off through its page, gone from the list at the next start"

# 1d no /.develop but Demo start and Capture On in their files: Demo start still listed (so it can be switched off), Capture listed too
#    (2.0.0: Capture is not behind /.develop), Restart mode and Developer mode hidden
fresh liston2; mkdir -p "$O/liston2/sd/Features"; printf 'feature 1\non 1\n' > "$O/liston2/sd/Features/demo.cfg"; printf 'feature 1\non 1\n' > "$O/liston2/sd/Features/capture.cfg"
run liston2 "" "features"
r=0; end liston2 || r=1
has liston2 "feature demo on 1 listed 1" || { echo "    Demo start On is not listed without /.develop"; r=1; }
has liston2 "feature capture on 1 listed 1" || { echo "    Capture not listed without /.develop"; r=1; }
has liston2 "feature restart on 0 listed 0" || { echo "    Restart mode listed without /.develop"; r=1; }
verdict liston2 $r "no /.develop, Demo start On: still listed, Capture On in its file: listed and On"

# 2 Restart mode on through the page: the params appear, the file is written after the debounce, values persist across a start
fresh restart; : > "$O/restart/sd/.develop"   # Restart mode is listed only with /.develop (or while On): the first row of the list
run restart "" "$NAV;tap F8;wait 500;tap F8;wait 300;tap Down;wait 200;tap F8;wait 600;shot S/f4-restart-on.png;tap Down 2;wait 300;tap F8;wait 300;tap Down;wait 200;tap F8;wait 600;shot S/f5-param.png;wait 1500;restartmode;resets"
r=0; end restart || r=1; golden restart f4-restart-on || r=1; golden restart f5-param || r=1
c=$(cfg restart restart); [ "$c" = "feature 1 id restart version 1 on 1 button 1 return 1 reckey 1 " ] || { echo "    restart.cfg: $c"; r=1; }
has restart "start state 1" || { echo "    Power return Start did not set the start state"; r=1; }
run restart "" "restartmode;features"   # the next start on the same card
has restart "restart mode 1 leaving 0 platform 1 file 0 cfg 1" && has restart "feature restart on 1 listed 1 value On" || r=1
verdict restart $r "State On shows the params, Power return Start, file $(cfg restart restart| cut -c1-60)..., kept across a start"

# 3 USB audio on: Restart, the Restart now row, the restart picks the new flag (the model's usb_init)
fresh usb
run usb "" "$NAV;tap F8;wait 500;tap F8;wait 300;tap Down;wait 200;tap F8;wait 600;shot S/f6-usb-restart.png;features;usbaudio;tap Down 2;wait 300;tap F8;wait 3000;resets;usbaudio;usbmode"
r=0; end usb || r=1; golden usb f6-usb-restart || r=1
has usb "feature usbaudio on 1 listed 1 value Restart" && has usb "usb audio running 0 flag 1" || r=1
has usb "mcu reset \(restart\): start RUN" && has usb "usb audio running 1 flag 1" || { echo "    Restart now did not restart into USB audio"; r=1; }
[ "$(cfg usb usbaudio)" = "feature 1 id usbaudio version 1 on 1 " ] || { echo "    usbaudio.cfg: $(cfg usb usbaudio)"; r=1; }
has usb "usb audio mode running 0 eeprom 0" || { echo "    Stereo (the default Mode) did not start in stereo"; r=1; }
verdict usb $r "USB audio On reads Restart, Restart now saves and restarts, the next start enumerates with audio, Mode Stereo writes no mode line"

# 4b Mode 8+3 mono (Config > USB > Audio mode, the USB entry shows once USB audio is on): the file gets "mode 1" (Stereo never writes the
# line), the EEPROM mode byte follows, the restart starts 8+3, back to Stereo drops the line again. An older image reading "mode 1" is
# features_test.cpp (omit). USBNAV: Features page -> Left -> Left (the base list) -> Up 2 (USB, above Metronome) -> Right (its rows)
USBNAV='tap Left;wait 300;tap Left;wait 300;tap Up 2;wait 300;tap Right;wait 400'
fresh usbmc
run usbmc "UISHOT_USB_FLAG=1" "$NAV;tap F8;wait 500;tap F8;wait 300;tap Down;wait 200;tap F8;wait 400;shot S/f6a-usb-entry.png;$USBNAV;tap F8;wait 300;tap Down;wait 200;tap F8;wait 600;shot S/f6b-usb-mode.png;features;usbmode;tap Down 2;wait 300;tap F8;wait 3000;resets;usbaudio;usbmode;features"
r=0; end usbmc || r=1; golden usbmc f6a-usb-entry || r=1; golden usbmc f6b-usb-mode || r=1
c=$(cfg usbmc usbaudio); [ "$c" = "feature 1 id usbaudio version 1 on 1 mode 1 " ] || { echo "    usbaudio.cfg: $c"; r=1; }
has usbmc "feature usbaudio on 1 listed 1 value Restart" && has usbmc "usb audio mode running 0 eeprom 1" || { echo "    8+3 before the restart"; r=1; }
has usbmc "mcu reset \(restart\): start RUN" && has usbmc "usb audio mode running 1 eeprom 1" || { echo "    the restart did not start in 8+3 mono"; r=1; }
# the next power on of the same card and EEPROM (flag on, mode byte 8+3): the row reads the mode, then back to Stereo
rm -rf "$O/usbmc2"; mkdir -p "$O/usbmc2"; cp -R "$O/usbmc/sd" "$O/usbmc2/sd"
run usbmc2 "UISHOT_USB_FLAG=1 UISHOT_USB_MODE=1" "features;usbmode;tap Config;wait 800;tap Down 3;wait 300;tap F8;wait 500;tap F8;wait 300;tap Up;wait 200;tap F8;wait 2000;usbmode;features"
end usbmc2 || r=1
has usbmc2 "feature usbaudio on 1 listed 1 value 8\+3 mono" && has usbmc2 "usb audio mode running 1 eeprom 1" || { echo "    8+3 start: row or mode"; r=1; }
c=$(cfg usbmc2 usbaudio); [ "$c" = "feature 1 id usbaudio version 1 on 1 " ] || { echo "    back to Stereo: usbaudio.cfg: $c"; r=1; }
has usbmc2 "usb audio mode running 1 eeprom 0" && has usbmc2 "feature usbaudio on 1 listed 1 value Restart" || { echo "    back to Stereo: mirror or row"; r=1; }
verdict usbmc $r "Mode 8+3 mono: mode 1 in the file, EEPROM mode byte set, Restart starts 8+3, back to Stereo drops the line and asks for a restart"

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
run reset "" "features;tap Config;wait 800;tap Down 6;wait 300;tap F8;wait 500;tap Down 3;wait 300;tap F8;wait 500;tap F8;wait 2000;shot S/f13-reset-usb.png;features;restartmode;resets"
r=0; end reset || r=1; golden reset f13-reset-usb || r=1   # USB audio was on (the USB entry listed), Reset config takes it out of the list
has reset "feature restart on 0 listed 1 value Off" && has reset "feature usbaudio on 0 listed 1 value Off" || r=1
ls "$O/reset/sd/Features"/*.cfg* > /dev/null 2>&1 && { echo "    left: $(ls "$O/reset/sd/Features")"; r=1; }
has reset "restart mode 0 leaving 0 platform 0" && has reset "start state 0" || r=1
verdict reset $r "Reset config: restart mode and USB audio off, power return off, every feature file removed"

# 9 stock bug: Metronome > State has no description, the hidden description box was drawn with a null text (crash on the Mac)
fresh nulldesc
run nulldesc "" "tap Config;wait 800;tap Down 3;wait 300;tap F8;wait 500;tap F8;wait 500;shot S/f10-metronome.png;tap F7;wait 300"
r=0; end nulldesc || r=1; golden nulldesc f10-metronome || r=1; has nulldesc "signal 11" && r=1
verdict nulldesc $r "Metronome > State value picker opens (no description), no crash"

# 10 Chord names (on by default): the MIDI Chord grid cell shows the short name, off through the page shows the interval digits,
#    the file keeps it across a start
fresh chordnm
run chordnm "" "setstep 0 0 48 64 14 24 0 0;grid 0 0;features;$NAV;tap Down 1;wait 300;tap F8;wait 500;shot S/f11-chordnames.png;tap F8;wait 300;tap Up;wait 200;tap F8;wait 600;shot S/f12-chordnames-off.png;wait 1500;features"
r=0; end chordnm || r=1; golden chordnm f11-chordnames || r=1; golden chordnm f12-chordnames-off || r=1
has chordnm "feature chordnm on 1 listed 1 value On" && has chordnm "grid 0 0 fx0 \[0\^7 \]" || { echo "    default: not on, or the grid is not ^7"; r=1; }
has chordnm "feature chordnm on 0 listed 1 value Off" || { echo "    the page did not switch it off"; r=1; }
[ "$(cfg chordnm chordnm)" = "feature 1 id chordnm version 1 on 0 " ] || { echo "    chordnm.cfg: $(cfg chordnm chordnm)"; r=1; }
run chordnm "" "setstep 0 0 48 64 14 24 0 0;grid 0 0;features"   # the next start on the same card
has chordnm "feature chordnm on 0 listed 1 value Off" && has chordnm "grid 0 0 fx0 \[047B\]" || { echo "    off not kept: $(grep -E '^grid|chordnm' "$O/chordnm/log.txt" | tail -2 | tr '\n' ' ')"; r=1; }
verdict chordnm $r "Chord names: on by default (^7), Off through the page (digits 47B), chordnm.cfg on 0 kept across a start"

# Capture (ref/capture-move-redesign.md D1, 2.0.0: on by default on every card, NOT behind /.develop, no parameter): button 3 of the pattern editor
# writes a played note into the pattern with no Features file (capture_sim.sh has the rules). capture.cfg of the current version with on 0
# (the kill switch) turns it off and keeps it off, a file of an older version loses its on line (features.cpp version rule): on again.
# The take logic is the Capture tenant of extras.plg in this build (EXTHOOKS=1, FEATURE_CAPTURE_MODULE): the card gets the host stand-in of
# tools/modit/make_host_module.py. Without the file the square stays hidden and button 3 shows the extras.plg popup, the ring still fills
# None of these cards carries /.develop.
MH="$G/../../../modit/make_host_module.py"
CAPRUN="features;tap Pattern;wait 300;instr 64;audio 1;tap Play;wait 300;midion 60 100 100;midioff 60 0 200;tap F4;wait 100;step 0 0;wait 1500;capind"
fresh capture; mkdir -p "$O/capture/sd/Plugins/Ext"; python3 -I "$MH" "$O/capture/sd/Plugins/Ext/extras.plg" >/dev/null
run capture "" "$CAPRUN"
r=0; end capture || r=1
[ ! -e "$O/capture/sd/.develop" ] || { echo "    the card has /.develop"; r=1; }
has capture "feature capture on 1 listed 1 value On" || { echo "    not on by default without /.develop"; r=1; }
has capture "step 0 0 note 36 instr 64" || { echo "    button 3 did not capture: $(grep -E '^step ' "$O/capture/log.txt" | tail -1)"; r=1; }
[ -e "$O/capture/sd/Features/capture.cfg" ] && { echo "    capture.cfg written by a start that changed nothing"; r=1; }
# the current version with on 0 is the kill switch and stays off
fresh captureoff; mkdir -p "$O/captureoff/sd/Features" "$O/captureoff/sd/Plugins/Ext"; python3 -I "$MH" "$O/captureoff/sd/Plugins/Ext/extras.plg" >/dev/null
printf 'feature 1\nid capture\nversion 3\non 0\n' > "$O/captureoff/sd/Features/capture.cfg"
run captureoff "" "features;tap Pattern;wait 300;instr 64;audio 1;tap Play;wait 300;midion 60 100 100;midioff 60 0 200;tap F4;wait 100;step 0 0;capind"
end captureoff || r=1
has captureoff "feature capture on 0 listed 1 value Off" && has captureoff "step 0 0 note -1 " && has captureoff "capind state 0 " || { echo "    the kill switch did not turn it off: $(grep -E '^step |^capind|capture' "$O/captureoff/log.txt" | tail -3 | tr '\n' ' ')"; r=1; }
# a file of an older version (1: the old opt-in, 2: the build that was listed with /.develop only) loses its on line: Capture is on
for v in 1 2; do
  fresh capturev$v; mkdir -p "$O/capturev$v/sd/Features" "$O/capturev$v/sd/Plugins/Ext"; python3 -I "$MH" "$O/capturev$v/sd/Plugins/Ext/extras.plg" >/dev/null
  printf 'feature 1\nid capture\nversion %s\non 0\nidle 20\n' "$v" > "$O/capturev$v/sd/Features/capture.cfg"
  run capturev$v "" "features;tap Pattern;wait 300;instr 64;audio 1;tap Play;wait 300;midion 60 100 100;midioff 60 0 200;tap F4;wait 100;step 0 0"
  end capturev$v || r=1
  has capturev$v "feature capture on 1 listed 1 value On" && has capturev$v "step 0 0 note 36 instr 64" || { echo "    version $v capture.cfg with on 0 kept Capture off: $(grep -E '^step |feature capture' "$O/capturev$v/log.txt" | tr '\n' ' ')"; r=1; }
done
# off through the page writes version 3 on 0 and the next start keeps it off (a user who turns it off stays off)
fresh capturetog; mkdir -p "$O/capturetog/sd/Features"; printf 'feature 1\nid capture\nversion 2\non 0\n' > "$O/capturetog/sd/Features/capture.cfg"
run capturetog "" "features;$NAV;tap Down 5;wait 300;tap F8;wait 500;tap F8;wait 300;tap Up;wait 200;tap F8;wait 600;wait 1500;features"
end capturetog || r=1
has capturetog "feature capture on 0 listed 1 value Off" || { echo "    the page did not switch Capture off: $(grep -E 'feature ' "$O/capturetog/log.txt" | tail -3 | tr '\n' ' ')"; r=1; }
[ "$(cfg capturetog capture)" = "feature 1 id capture version 3 on 0 " ] || { echo "    capture.cfg: $(cfg capturetog capture)"; r=1; }
run capturetog "" "features;tap Pattern;wait 300;instr 64;audio 1;tap Play;wait 300;midion 60 100 100;midioff 60 0 200;tap F4;wait 100;step 0 0;capind"
has capturetog "feature capture on 0 listed 1 value Off" && has capturetog "step 0 0 note -1 " || { echo "    off not kept across a start: $(grep -E '^step |feature capture' "$O/capturetog/log.txt" | tr '\n' ' ')"; r=1; }
# no extras.plg: the square stays hidden, nothing is captured, the press names Capture and extras.plg (the take logic is the module)
fresh capturenomod
run capturenomod "" "tap Pattern;wait 300;instr 64;audio 1;tap Play;wait 300;midion 60 100 100;midioff 60 0 200;wait 6000;capind;tap F4;wait 100;shot S/cap-nomod-press.png;step 0 0;capind"
end capturenomod || r=1
golden capturenomod cap-nomod-press || r=1
[ "$(grep -cE '^capind state 0 draws 0 take [0-9] ring [0-9]+ seq 2' "$O/capturenomod/log.txt")" = 2 ] && has capturenomod "step 0 0 note -1 " || { echo "    no extras.plg: $(grep -E '^step |^capind' "$O/capturenomod/log.txt" | tr '\n' ' ')"; r=1; }
verdict capture $r "Capture without /.develop: on by default and captures (extras.plg), version 3 on 0 stays off (also after a toggle through the page and a start), version 1 and 2 files with on 0 are on, no extras.plg: hidden, nothing captured, the press names Capture and extras.plg"

echo "RESULT: $([ $rc = 0 ] && echo GREEN || echo RED)$([ "$UPD" = --update ] && echo ' (goldens written)')"
exit $rc
