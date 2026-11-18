#!/usr/bin/env bash
# usb_menu_sim.sh UI_SHOT OUTDIR [--update]
# Config > USB on the Mac. UI_SHOT is an EXTHOOKS=1 USB_MSC=1 build (plain or SAN). The USB entry of the Config list is listed while USB audio
# or USB drive is on in Config > Features and hidden when both are off, a change shows at once (no restart). Its rows: Storage (drive on),
# Audio mode and FX in 8+3 (audio on), Restart now (the audio switch or mode waits for a restart). Cases: the four states of the two
# switches (screens against goldens/usbmenu/, byte equal PNGs, --update writes them), the live switch through the Features page, and the
# files the rows write (usbaudio.cfg keeps its format, the EEPROM mode byte follows), then the device rows UM2 to UM5 as far as the emu goes: um2 mode
# round trip with Restart now on the USB page, um3 drive On, restart, Storage, Off, um4 mode 1 carry-over, um5 Reset config, metro the Metronome
# shortcut of the Sample Recorder. Exit 0 when every case passes.
set -uo pipefail
U=$1; O=$2; UPD=${3:-}; mkdir -p "$O"; rc=0
G=$(cd "$(dirname "$0")" && pwd)/goldens/usbmenu; mkdir -p "$G"
run() {  # name env script: OUT/name/sd is the card, OUT/name/log.txt the output
  local d=$O/$1; rm -rf "$d/shots" "$d/log.txt"; mkdir -p "$d/sd" "$d/shots"
  env $2 timeout 300 "$U" "$d/sd" "${3//S\//$d/shots/}" > "$d/log.txt" 2>&1; echo "exit $?" >> "$d/log.txt"
}
fresh() { rm -rf "$O/$1"; mkdir -p "$O/$1/sd/Features"; }
verdict() { if [ "$2" = 0 ]; then echo "PASS $1: $3"; else echo "FAIL $1: $3"; rc=1; fi; }
has() { grep -qE "$2" "$O/$1/log.txt"; }
san() { ! grep -E "runtime error|AddressSanitizer|SUMMARY:" "$O/$1/log.txt" | grep -v "mtWavetableEnvelopes.cpp:206" | grep -q .; }
golden() {  # case shot: equal to the golden (or written with --update)
  local f=$O/$1/shots/$2.png
  [ -f "$f" ] || { echo "    $2.png not written"; return 1; }
  if [ "$UPD" = --update ]; then cp "$f" "$G/$2.png"; return 0; fi
  cmp -s "$f" "$G/$2.png" || { echo "    $2.png differs from goldens/usbmenu/$2.png"; return 1; }
}
end() {
  has "$1" "^exit 0" || { echo "    no exit 0: $(tail -2 "$O/$1/log.txt" | tr '\n' ' ')"; return 1; }
  san "$1" || { echo "    sanitizer: $(grep -E "runtime error|SUMMARY" "$O/$1/log.txt" | grep -v mtWavetable | head -2 | tr '\n' ' ')"; return 1; }
}
cfg() { cat "$O/$1/sd/Features/$2.cfg" 2>/dev/null | tr '\n' ' '; }
AUDIO='feature 1\nid usbaudio\nversion 1\non 1\n'
DRIVE='feature 1\nid usbdisk\nversion 1\non 1\n'
# Config base list, then the USB entry (base row 3, listed) into its rows
CFG='wait 300;tap Config;wait 800'
USB='tap Down 3;wait 300;tap F8;wait 500'

# the four states of the two switches. UISHOT_USB_FLAG / UISHOT_DRIVE_FLAG: the boot flags follow the files, so no row reads Restart
fresh off
run off "" "$CFG;shot S/s1-base.png;tap Down 4;wait 300;shot S/s1-features-row.png"
r=0; end off || r=1; golden off s1-base || r=1; golden off s1-features-row || r=1
verdict off $r "USB audio off, USB drive off: no USB entry (Features is the fifth entry)"

fresh audio; printf "$AUDIO" > "$O/audio/sd/Features/usbaudio.cfg"
run audio "UISHOT_USB_FLAG=1" "$CFG;shot S/s2-base.png;$USB;shot S/s2-usb.png"
r=0; end audio || r=1; golden audio s2-base || r=1; golden audio s2-usb || r=1
verdict audio $r "USB audio on only: USB entry with Audio mode and FX in 8+3, no Storage"

fresh drive; printf "$DRIVE" > "$O/drive/sd/Features/usbdisk.cfg"
run drive "UISHOT_DRIVE_FLAG=1" "$CFG;shot S/s3-base.png;$USB;shot S/s3-usb.png"
r=0; end drive || r=1; golden drive s3-base || r=1; golden drive s3-usb || r=1
verdict drive $r "USB drive on only: USB entry with Storage, no audio rows"

fresh both; printf "$AUDIO" > "$O/both/sd/Features/usbaudio.cfg"; printf "$DRIVE" > "$O/both/sd/Features/usbdisk.cfg"
run both "UISHOT_USB_FLAG=1 UISHOT_DRIVE_FLAG=1" "$CFG;shot S/s4-base.png;$USB;shot S/s4-usb.png"
r=0; end both || r=1; golden both s4-base || r=1; golden both s4-usb || r=1
verdict both $r "USB audio and USB drive on: USB entry with Storage, Audio mode, FX in 8+3"

# live: the entry shows and hides while Config stays open (Features > USB audio State On, then Off), selection stays on Features
fresh live
run live "" "$CFG;tap Down 4;wait 300;tap F8;wait 500;tap F8;wait 500;tap F8;wait 300;tap Down;wait 200;tap F8;wait 600;shot S/s5-live-on.png;tap F8;wait 300;tap Up;wait 200;tap F8;wait 600;shot S/s5-live-off.png;wait 1500;features"
r=0; end live || r=1; golden live s5-live-on || r=1; golden live s5-live-off || r=1
has live "feature usbaudio on 0 listed 1 value Off" || { echo "    off again: $(grep 'feature usbaudio' "$O/live/log.txt" | tail -1)"; r=1; }
verdict live $r "State On shows the USB entry at once, State Off hides it again, Features stays selected"

# mode: Audio mode 8+3 mono from the USB page: mode 1 in usbaudio.cfg (the format of Features > USB audio before the move), the EEPROM mode byte,
# Stereo again drops the line. The restart itself is features.sh (usbmc, usbmc2)
fresh mode; printf "$AUDIO" > "$O/mode/sd/Features/usbaudio.cfg"
run mode "UISHOT_USB_FLAG=1" "$CFG;$USB;tap F8;wait 300;tap Down;wait 200;tap F8;wait 600;shot S/s6-mode.png;usbmode;wait 1500"
r=0; end mode || r=1; golden mode s6-mode || r=1
has mode "usb audio mode running 0 eeprom 1" || { echo "    EEPROM mode byte: $(grep 'usb audio mode' "$O/mode/log.txt" | tr '\n' '|')"; r=1; }
c=$(cfg mode usbaudio); [ "$c" = "feature 1 id usbaudio version 1 on 1 mode 1 " ] || { echo "    8+3: usbaudio.cfg: $c"; r=1; }
rm -rf "$O/mode2"; mkdir -p "$O/mode2"; cp -R "$O/mode/sd" "$O/mode2/sd"   # the next start on the same card, the EEPROM mode byte at 8+3
run mode2 "UISHOT_USB_FLAG=1 UISHOT_USB_MODE=1" "$CFG;$USB;tap F8;wait 300;tap Up;wait 200;tap F8;wait 600;usbmode;wait 1500"
has mode2 "usb audio mode running 1 eeprom 0" || { echo "    Stereo again: $(grep 'usb audio mode' "$O/mode2/log.txt" | tr '\n' '|')"; r=1; }
c=$(cfg mode2 usbaudio); [ "$c" = "feature 1 id usbaudio version 1 on 1 " ] || { echo "    Stereo again: usbaudio.cfg: $c"; r=1; }
verdict mode $r "Audio mode on the USB page: mode 1 line and EEPROM mode byte, Stereo again drops the line"

# UM2: the whole mode round trip on the USB page, Restart now on that page (no Features visit): Stereo -> 8+3 mono pending (Restart now row), the restart,
# the next start reads 8+3 mono, back to Stereo, the restart, the next start is Stereo and the file has no mode line again
fresh um2a; printf "$AUDIO" > "$O/um2a/sd/Features/usbaudio.cfg"
run um2a "UISHOT_USB_FLAG=1" "$CFG;$USB;shot S/u2a-stereo.png;tap F8;wait 300;tap Down;wait 200;tap F8;wait 600;shot S/u2b-pending.png;usbmode;tap Down 2;wait 300;tap F8;wait 3000;resets;usbmode"
r=0; end um2a || r=1; golden um2a u2a-stereo || r=1; golden um2a u2b-pending || r=1
has um2a "usb audio mode running 0 eeprom 1" || { echo "    8+3 before the restart: $(grep 'usb audio mode' "$O/um2a/log.txt" | head -1)"; r=1; }
has um2a "mcu reset \(restart\): start RUN" && has um2a "usb audio mode running 1 eeprom 1" || { echo "    Restart now on the USB page did not restart into 8+3: $(grep 'usb audio mode' "$O/um2a/log.txt" | tail -1)"; r=1; }
c=$(cfg um2a usbaudio); [ "$c" = "feature 1 id usbaudio version 1 on 1 mode 1 " ] || { echo "    8+3: usbaudio.cfg: $c"; r=1; }
rm -rf "$O/um2b"; mkdir -p "$O/um2b"; cp -R "$O/um2a/sd" "$O/um2b/sd"
run um2b "UISHOT_USB_FLAG=1 UISHOT_USB_MODE=1" "$CFG;$USB;shot S/u2c-8p3-after-restart.png;usbmode;tap F8;wait 300;tap Up;wait 200;tap F8;wait 600;shot S/u2d-stereo-pending.png;tap Down 2;wait 300;tap F8;wait 3000;resets;usbmode"
end um2b || r=1; golden um2b u2c-8p3-after-restart || r=1; golden um2b u2d-stereo-pending || r=1
has um2b "usb audio mode running 1 eeprom 1" || { echo "    the start with the 8+3 byte did not run 8+3: $(grep 'usb audio mode' "$O/um2b/log.txt" | head -1)"; r=1; }
has um2b "usb audio mode running 0 eeprom 0" || { echo "    back to Stereo: $(grep 'usb audio mode' "$O/um2b/log.txt" | tail -1)"; r=1; }
c=$(cfg um2b usbaudio); [ "$c" = "feature 1 id usbaudio version 1 on 1 " ] || { echo "    Stereo again: usbaudio.cfg: $c"; r=1; }
verdict um2 $r "UM2 Stereo <-> 8+3 mono with Restart now on the USB page, mode 1 line and EEPROM mode byte read back after each restart"

# UM3: USB drive. Features > USB drive On (no restart yet: the entry is listed, status Restart), the restart from the Features page, the next start,
# Config > USB > Storage lends the card (UI only: no host enumeration here, the card owner is the host model), Exit, State Off hides the entry
NAV0='tap Config;wait 800;tap Down 4;wait 300;tap F8;wait 500'
fresh um3a
run um3a "UISHOT_DRIVE_FLAG=0" "wait 3000;tap Config;wait 800;shot S/u3a-config-off.png;$NAV0;tap Down 1;wait 300;tap F8;wait 500;tap F8;wait 300;tap Down;wait 200;tap F8;wait 600;shot S/u3b-drive-pending.png;features;drivestat;tap Down 2;wait 300;tap F8;wait 3000;resets;drivestat"
r=0; end um3a || r=1; golden um3a u3a-config-off || r=1; golden um3a u3b-drive-pending || r=1
has um3a "feature usbdisk on 1 listed 1 value Restart" || { echo "    State On did not read Restart: $(grep 'feature usbdisk' "$O/um3a/log.txt" | head -1)"; r=1; }
has um3a "mcu reset \(restart\): start RUN" && has um3a "^drivestat owned 0 running 1" || { echo "    the restart did not run the drive: $(grep '^drivestat' "$O/um3a/log.txt" | tail -1)"; r=1; }
rm -rf "$O/um3b"; mkdir -p "$O/um3b"; cp -R "$O/um3a/sd" "$O/um3b/sd"
run um3b "UISHOT_DRIVE_FLAG=1" "wait 3000;tap Config;wait 800;shot S/u3c-config-on.png;tap Down 3;wait 300;tap F8;wait 500;shot S/u3d-storage.png;tap F8;wait 4000;drivestat;shot S/u3e-lent.png;tap F8;wait 5000;drivestat;tap Config;wait 1500;tap Left;wait 300;tap Down 2;wait 300;tap Right;wait 400;tap Down 1;wait 300;tap F8;wait 500;tap F8;wait 300;tap Up;wait 200;tap F8;wait 600;shot S/u3f-drive-off.png;features;tap Left;wait 300;tap Left;wait 300;tap Left;wait 300;shot S/u3g-entry-gone.png;wait 1500;features"
end um3b || r=1; for g in u3c-config-on u3d-storage u3e-lent u3f-drive-off; do golden um3b $g || r=1; done
has um3b "^drivestat owned 1 running 1" && has um3b "^drivestat owned 0 running 1" || { echo "    Storage did not lend and return the card: $(grep '^drivestat' "$O/um3b/log.txt" | tr '\n' '|')"; r=1; }
has um3b "feature usbdisk on 0 listed 1 value Restart" || { echo "    State Off: $(grep 'feature usbdisk' "$O/um3b/log.txt" | tail -1)"; r=1; }
[ "$(cfg um3b usbdisk)" = "feature 1 id usbdisk version 1 on 0 " ] || { echo "    usbdisk.cfg: $(cfg um3b usbdisk)"; r=1; }
verdict um3 $r "UM3 drive On shows the entry (Restart), the restart runs it, Storage lends and returns the card (emu: UI only, enumeration not covered), State Off hides the entry at once"

# UM4: carry-over. The usbaudio.cfg of 1.4.0 pre-release (mode 1 on the Features page) on a unit whose EEPROM mode byte reads 8+3: the USB page reads
# 8+3 mono, the file is not touched by looking, and the same file on a unit with the mode byte at Stereo reads 8+3 too (the file is the setting)
fresh um4; printf "$AUDIO" > "$O/um4/sd/Features/usbaudio.cfg"; printf 'mode 1\n' >> "$O/um4/sd/Features/usbaudio.cfg"
cp "$O/um4/sd/Features/usbaudio.cfg" "$O/um4.orig"
run um4 "UISHOT_USB_FLAG=1 UISHOT_USB_MODE=1" "$CFG;shot S/u4a-config.png;$USB;shot S/u4b-usb-8p3.png;usbmode;features"
r=0; end um4 || r=1; golden um4 u4a-config || r=1; golden um4 u4b-usb-8p3 || r=1
has um4 "feature usbaudio on 1 listed 1 value 8\+3 mono" && has um4 "usb audio mode running 1 eeprom 1" || { echo "    8+3 mono carry: $(grep -E 'feature usbaudio|usb audio mode' "$O/um4/log.txt" | tr '\n' '|')"; r=1; }
cmp -s "$O/um4.orig" "$O/um4/sd/Features/usbaudio.cfg" || { echo "    usbaudio.cfg touched by a start that changed nothing: $(cfg um4 usbaudio)"; r=1; }
verdict um4 $r "UM4 an existing mode 1 usbaudio.cfg boots in 8+3 mono, the USB page reads 8+3 mono, the file is byte equal after the session"

# UM5: Reset config with both switches on (entry listed, then gone, every feature file removed, both rows read Restart: the boot flags are still on), and the Metronome shortcut of the Sample Recorder
# (Config button F4) lands on Metronome with the USB entry listed and not listed
fresh um5; printf "$AUDIO" > "$O/um5/sd/Features/usbaudio.cfg"; printf "$DRIVE" > "$O/um5/sd/Features/usbdisk.cfg"
run um5 "UISHOT_USB_FLAG=1 UISHOT_DRIVE_FLAG=1" "$CFG;shot S/u5a-before-reset.png;tap Down 6;wait 300;tap F8;wait 500;tap Down 3;wait 300;tap F8;wait 500;tap F8;wait 2000;shot S/u5b-after-reset.png;features"
r=0; end um5 || r=1; golden um5 u5a-before-reset || r=1; golden um5 u5b-after-reset || r=1
has um5 "feature usbaudio on 0 listed 1 value Restart" && has um5 "feature usbdisk on 0 listed 1 value Restart" || { echo "    after Reset config: $(grep -E 'feature usb' "$O/um5/log.txt" | tr '\n' '|')"; r=1; }
ls "$O/um5/sd/Features"/*.cfg > /dev/null 2>&1 && { echo "    left: $(ls "$O/um5/sd/Features")"; r=1; }
verdict um5 $r "UM5 Reset config takes the USB entry out (both switches off, feature files removed)"
fresh metro; printf "$AUDIO" > "$O/metro/sd/Features/usbaudio.cfg"
run metro "UISHOT_USB_FLAG=1" "wait 300;tap SampleRec;wait 800;tap F4;wait 1500;shot S/u5c-metro-with-usb.png"
r=0; end metro || r=1; golden metro u5c-metro-with-usb || r=1
fresh metro2
run metro2 "" "wait 300;tap SampleRec;wait 800;tap F4;wait 1500;shot S/u5d-metro-no-usb.png"
end metro2 || r=1; golden metro2 u5d-metro-no-usb || r=1
verdict metro $r "UM5 Sample Recorder > Metronome Config opens Config on the Metronome row with the USB entry listed (row 4) and not listed (row 3)"

echo "RESULT: $([ $rc = 0 ] && echo GREEN || echo RED)$([ "$UPD" = --update ] && echo ' (goldens written)')"
exit $rc
