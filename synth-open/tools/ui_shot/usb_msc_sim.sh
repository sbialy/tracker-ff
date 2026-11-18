#!/usr/bin/env bash
# usb_msc_sim.sh UI_SHOT OUTDIR   Drive mode of the USB drive (USB_MSC=1) on the Mac: simulator rows S2 and S6 of
# ref/usb-mass-storage-feasibility.md, and the Exit that reads /Workspace back (decision 3). UI_SHOT is an EXTHOOKS=1 USB_MSC=1 build
# (plain or SAN). The SD folder plays the card, the script's `put`, `hostcp` and `hostrm` play the computer. host_drive.h has the gate
# model, hostp::g_driveBackend counts every call that reaches the SD folder while the computer owns the card. Exit 0 when every case passes.
set -uo pipefail
U=$1; O=$2; mkdir -p "$O"; rc=0
run() {  # name env script: OUT/name/sd is the card, OUT/name/log.txt the output (appended, a case can start the unit again)
  local d=$O/$1; mkdir -p "$d/sd"
  env UISHOT_DRIVE_FLAG=1 $2 timeout 300 "$U" "$d/sd" "$3" >> "$d/log.txt" 2>&1; echo "exit $?" >> "$d/log.txt"
}
fresh() {  # the card with the feature on (/Features/usbdisk.cfg) so the boot flag stays on
  rm -rf "$O/$1"; mkdir -p "$O/$1/sd/Features"; printf 'feature 1\nid usbdisk\nversion 1\non 1\n' > "$O/$1/sd/Features/usbdisk.cfg"
}
verdict() { if [ "$2" = 0 ]; then echo "PASS $1: $3"; else echo "FAIL $1: $3"; rc=1; fi; }
has() { grep -qE "$2" "$O/$1/log.txt"; }
san() { ! grep -E "runtime error|AddressSanitizer|SUMMARY:" "$O/$1/log.txt" | grep -v "mtWavetableEnvelopes.cpp:206" | grep -q .; }
end() {  # every start ended with exit 0, clean sanitizer log
  ! grep -E "^exit [^0]" "$O/$1/log.txt" | grep -q . || { echo "    a start did not exit 0: $(grep -E '^exit' "$O/$1/log.txt" | tr '\n' ' ')"; return 1; }
  san "$1" || { echo "    sanitizer: $(grep -E "runtime error|SUMMARY" "$O/$1/log.txt" | grep -v mtWavetable | head -2 | tr '\n' ' ')"; return 1; }
}
last() { grep -E "$2" "$O/$1/log.txt" | tail -1; }
NAV='tap Config;wait 800;tap Down 5;wait 300;tap F8;wait 500'   # Config, Features (base row 5 with the USB group), into the list

# S2: the mode entered while a song plays, with a pending autosave, a Features change inside its debounce, /.develop on the card (the
# extension loader rules run), pads and MIDI notes during the mode. Zero backend calls while the computer owns the card, the change
# reaches the card after the Exit, the autosave resumes.
fresh s2; touch "$O/s2/sd/.develop"
run s2 "" "wait 3000;tap Pattern;wait 300;tap Play;wait 1000;$NAV;tap F8;wait 500;tap F8;wait 300;tap Down;wait 200;tap F8;wait 100;tempo 101;drive on;drivestat;pad 2;pad 5;midion 60 100;wait 500;midioff 60;wait 15000;drivestat;fm;tap F8;wait 5000;drivestat;fm;status;proj;tempo 102;wait 12000;features;proj"
r=0; end s2 || r=1
has s2 "^drive on: mode 1 owned 1" || { echo "    the mode did not lend the card"; r=1; }
in=$(grep "^drivestat owned 1" "$O/s2/log.txt" | tail -1); out=$(last s2 "^drivestat owned 0")
echo "$in" | grep -q " backend 0 " || { echo "    backend calls while owned: $in"; r=1; }
echo "$out" | grep -q " backend 0 " || { echo "    backend calls counted after the Exit: $out"; r=1; }
has s2 "feature restart on 1 " || { echo "    the Features change was lost"; r=1; }
grep -q "^on 1" "$O/s2/sd/Features/restart.cfg" 2>/dev/null || { echo "    restart.cfg not written after the Exit"; r=1; }
has s2 "^pattern 1 " || { echo "    not back on the pattern editor"; r=1; }
grep -E "^fm status" "$O/s2/log.txt" | tail -1 | grep -q "status 0 op 0" || { echo "    file manager not idle after the Exit"; r=1; }
verdict s2 $r "song playing + pending autosave + Features debounce + /.develop: ${in#drivestat }, change written after Exit"

# negative control: the same S2 run with the gate model switched off (UISHOT_DRIVE_NOGATE=1) must count backend calls while owned,
# else the backend count cannot see a leak
fresh neg; touch "$O/neg/sd/.develop"
run neg "UISHOT_DRIVE_NOGATE=1" "wait 3000;tap Pattern;wait 300;tap Play;wait 1000;$NAV;tap F8;wait 500;tap F8;wait 300;tap Down;wait 200;tap F8;wait 100;tempo 101;drive on;pad 2;wait 15000;drivestat;tap F8;wait 3000"
r=0; n=$(grep "^drivestat owned 1" "$O/neg/log.txt" | tail -1 | sed -E 's/.* backend ([0-9]+) .*/\1/')
[ "${n:-0}" -gt 0 ] || { echo "    no backend calls seen without the gate"; r=1; }
verdict neg-control $r "without the gate the backend count sees ${n:-0} calls while owned"

# S2b (decision 3): what the computer wrote to /Workspace wins. RAM tempo 90, the computer puts back a project with 133, the Exit reads
# it (the File screen loads it, then the pattern editor), a later change autosaves on top of the computer's file
fresh reload
run reload "" "wait 3000;tempo 133;wait 12000;proj"
cp "$O/reload/sd/Workspace/project.mt" "$O/reload/host-133.mt"
run reload "" "wait 3000;tempo 90;wait 12000;proj;tap Config;wait 800;drive on;hostcp $O/reload/host-133.mt Workspace/project.mt;wait 2000;proj;tap F8;wait 4000;proj;fm;status;tempo 95;wait 12000;proj"
r=0; end reload || r=1
p=$(grep "^proj tempo" "$O/reload/log.txt" | tr '\n' ' ')
[ "$(grep "^proj tempo" "$O/reload/log.txt" | sed -n 2,5p | tr '\n' ' ')" = "proj tempo 90.0 proj tempo 90.0 proj tempo 133.0 proj tempo 95.0 " ] || { echo "    tempos: $p"; r=1; }
has reload "^pattern 1 " || { echo "    not back on the pattern editor"; r=1; }
cmp -s "$O/reload/sd/Workspace/project.mt" "$O/reload/host-133.mt" && { echo "    the change after the Exit was not autosaved"; r=1; }
verdict reload $r "RAM 90, computer writes 133, Exit reads 133 on the File screen, then 95 autosaves"

# S2c: the card pulled while the computer owns it (handleCardSlotAction leaves the mode, remount finds nothing)
# not modelled: the host card is always in (HFileSystem::isSdCardInserted), see the report

# S2d: the drive was not enumerated at start (feature off): Storage backs out, nothing is lent, the unit works on
fresh off; rm "$O/off/sd/Features/usbdisk.cfg"
run off "UISHOT_DRIVE_FLAG=0" "wait 3000;drive on;drivestat;status;tempo 88;wait 12000;proj"
r=0; end off || r=1
has off "^drive on: mode 0 owned 0" || { echo "    $(last off '^drive on')"; r=1; }
has off "^drivestat owned 0 running 0 backend 0 refused 0" || { echo "    $(last off '^drivestat')"; r=1; }
grep -q "^on 1" "$O/off/sd/Features/usbdisk.cfg" 2>/dev/null && { echo "    usbdisk.cfg written"; r=1; }
verdict off $r "feature off at start: the Storage action backs out, no card lent"

# the Features row: USB drive On through Config > Features (row 1: USB audio, USB drive, Restart mode is hidden without /.develop), Restart now, the next start
# enumerates the drive (the model's usb_init) and Config > USB > Storage lends the card. Screens in OUT/feature/*.png. While USB drive and
# USB audio are both off the USB entry is not in the Config list at all (the first run only looks at the list), so Features is base row 4
rm -rf "$O/feature"; mkdir -p "$O/feature/sd"
USBNAV='tap Config;wait 800;tap Down 3;wait 300;tap F8;wait 500'   # Config, USB (base row 3, listed while the drive is on), into the group (Storage)
NAV0='tap Config;wait 800;tap Down 4;wait 300;tap F8;wait 500'     # Config, Features (base row 4, no USB entry), into the list
run feature "UISHOT_DRIVE_FLAG=0" "wait 3000;tap Config;wait 800;shot $O/feature/config.png;drivestat"
run feature "UISHOT_DRIVE_FLAG=0" "wait 3000;$NAV0;tap Down 1;wait 300;tap F8;wait 500;tap F8;wait 300;tap Down;wait 200;tap F8;wait 600;shot $O/feature/usbdisk-restart.png;features;drivestat;tap Down 2;wait 300;tap F8;wait 3000;resets;drivestat"
# the next start: the EEPROM flag Restart now left (the model keeps no EEPROM between runs), the card says on
run feature "UISHOT_DRIVE_FLAG=1" "wait 3000;$USBNAV;tap F8;wait 800;shot $O/feature/storage.png;drivestat;tap F8;wait 4000;drivestat;features"
r=0; end feature || r=1
grep -q "^on 1" "$O/feature/sd/Features/usbdisk.cfg" 2>/dev/null || { echo "    usbdisk.cfg: $(cat "$O/feature/sd/Features/usbdisk.cfg" 2>/dev/null | tr '\n' ' ')"; r=1; }
has feature "feature usbdisk on 1 listed 1 value Restart" || { echo "    $(grep 'feature usbdisk' "$O/feature/log.txt" | tail -1)"; r=1; }
st=$(grep "^drivestat" "$O/feature/log.txt" | tr '\n' '|')
echo "$st" | grep -qE "^drivestat owned 0 running 0 [^|]*\|drivestat owned 0 running 0 [^|]*\|drivestat owned 0 running 1 [^|]*\|drivestat owned 1 running 1 [^|]*\|drivestat owned 0 running 1" \
  || { echo "    drive states: $st"; r=1; }
verdict feature $r "no USB entry while off, Features > USB drive On reads Restart, Restart now, Storage lends the card, Exit returns it"

# S6: power in Drive mode. The storage screen blocks every input but Exit (Interface::activateFileTransferModeScreen,
# isPowerButtonBlocked), so the power press does nothing while the computer owns the card: no power off, no /Workspace save, the
# computer's file stays. After the Exit the power off works and saves the project that was read back (the computer's).
fresh s6
run s6 "" "wait 3000;tempo 133;wait 12000;proj"
cp "$O/s6/sd/Workspace/project.mt" "$O/s6/host-133.mt"
run s6 "" "wait 3000;tempo 90;wait 12000;tap Play;wait 500;drive on;hostcp $O/s6/host-133.mt Workspace/project.mt;tempo 77;power hold 8000;power release;wait 500;drivestat;proj"
r=0; end s6 || r=1
has s6 "^power hold: off 0 " || { echo "    $(last s6 '^power hold')"; r=1; }
st=$(grep "^drivestat owned 1" "$O/s6/log.txt" | tail -1)
echo "$st" | grep -q " backend 0 " || { echo "    backend calls while owned: $st"; r=1; }
cmp -s "$O/s6/sd/Workspace/project.mt" "$O/s6/host-133.mt" || { echo "    /Workspace/project.mt changed while the computer owned the card"; r=1; }
# the Exit, then the power off (fresh start of the same card: the unit is on and in the mode no more)
run s6 "" "wait 3000;proj;drive on;tap F8;wait 4000;proj;power hold 8000"
has s6 "^power hold: off 1 " || { echo "    no power off after the Exit: $(last s6 '^power hold')"; r=1; }
run s6 "" "wait 3000;proj"
[ "$(last s6 '^proj tempo')" = "proj tempo 133.0" ] || { echo "    after the power off: $(last s6 '^proj tempo') (want 133.0, the computer's)"; r=1; }
verdict s6 $r "power press ignored in Drive mode (${st#drivestat }), computer's /Workspace kept, Exit then power off saves it"

echo "RESULT: $([ $rc = 0 ] && echo GREEN || echo RED)"
exit $rc
