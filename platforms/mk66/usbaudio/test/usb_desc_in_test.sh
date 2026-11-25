#!/usr/bin/env bash
# usb_desc_in_test.sh   Host proof of FEATURE_USB_AUDIO_IN (poly-stereo-io step 5): the computer -> Tracker OUT interface.
# Compiles the real teensy_core/usb_desc.c with and without -DFEATURE_USB_AUDIO_IN and with and without -DFEATURE_USB_MSC, serves
# every combination (audio off / stereo / 8+3 mono, drive off / on) and walks the configuration descriptor:
#   - wTotalLength equals the bytes served, every bLength lands on the end, bNumInterfaces equals the interfaces found
#   - the interface numbers are 0..N-1 in order of first appearance, the IAD covers the audio function, the AC header lists
#     the streaming interfaces, terminal ids link up, the endpoint addresses are the expected ones
#   - audio on: IN 0x88 (isochronous, 180 or 990 B), stereo also OUT 0x08 (isochronous asynchronous, 180 B, bSynchAddress 0x8B) and
#     the feedback IN 0x8B (3 B), ENDPT8 = 0x1c, ENDPT11 = 0x14. 8+3 mono leaves the OUT interface out (FS periodic budget): its
#     descriptors equal the build without FEATURE_USB_AUDIO_IN
#   - audio off: neither endpoint is served and ENDPT8 = 0
# Without FEATURE_USB_AUDIO_IN the existing usb_desc_test.sh goldens stay valid (run that too).
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../../../.." && pwd); tc=$root/platforms/mk66/3rdparty/teensy_core
out=${TMPDIR:-/tmp}/usb_desc_in_test; rm -rf "$out"; mkdir -p "$out"
D=(-DUSB_MIDI -D__MK66FX1M0__ -DF_CPU=180000000 -DTEENSYDUINO=142 -DLAYOUT_US_ENGLISH -w -DFEATURE_USB_AUDIO)
rc=0
for msc in 0 1; do
  for inf in 0 1; do
    f=(); [ $msc = 1 ] && f+=(-DFEATURE_USB_MSC); [ $inf = 1 ] && f+=(-DFEATURE_USB_AUDIO_IN)
    cc "${D[@]}" ${f[@]+"${f[@]}"} -I"$tc" "$here/usb_desc_dump.c" -o "$out/dump_${msc}_${inf}" || { echo "FAIL compile msc=$msc in=$inf"; exit 1; }
    for mode in 0 1 2; do
      for drive in $(seq 0 $msc); do
        "$out/dump_${msc}_${inf}" $mode $drive > "$out/d_${msc}_${inf}_${mode}_${drive}.txt"
        python3 -I "$here/usb_desc_in_check.py" "$out/d_${msc}_${inf}_${mode}_${drive}.txt" $inf $mode $drive $msc || rc=1
      done
    done
  done
done
# 8+3 mono with the flag leaves the OUT interface out: the descriptors equal the build without the flag, byte for byte
for msc in 0 1; do
  for drive in $(seq 0 $msc); do
    if diff <(grep -E '^(desc|chname)' "$out/d_${msc}_1_2_${drive}.txt") <(grep -E '^(desc|chname)' "$out/d_${msc}_0_2_${drive}.txt") > /dev/null; then
      echo "ok    8+3 mono msc_built=$msc drive=$drive: descriptors equal to the build without FEATURE_USB_AUDIO_IN"
    else echo "FAIL  8+3 mono msc_built=$msc drive=$drive differs from the build without FEATURE_USB_AUDIO_IN"; rc=1; fi
  done
done
# the flag off must serve what the image without it serves (same bytes as the committed goldens, audio on = stereo / 8+3)
bash "$here/usb_desc_test.sh" > "$out/old.txt" 2>&1 && echo "ok    usb_desc_test.sh (existing goldens, no IN flag) GREEN" || { echo "FAIL  usb_desc_test.sh"; tail -5 "$out/old.txt"; rc=1; }
echo "RESULT: $([ $rc = 0 ] && echo GREEN || echo RED)"
exit $rc
