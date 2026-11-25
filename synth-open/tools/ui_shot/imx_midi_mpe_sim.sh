#!/bin/bash
# imx_midi_mpe_sim.sh
#
# MPE in through the REAL Tracker+ / Mini MIDI driver (poly-sim-imx-midi). The same RAW MIDI byte scenario runs on the original Tracker sim (a decoder
# of the message hands it to the mtMidi.cpp handlers the MK66 drivers call), on the Tracker+ sim and on the Mini sim. On the last two the bytes go
# BELOW the driver (host_imxmidi.cpp): a USB MIDI event packet into the real usb_midi.c receive callback and queue, read by the real
# UsbMidiImpl::read (kPitchBend: 14 bit assembly, kAfterTouchChannel, 1 based channel, setReceiveActions), and DIN bytes into the MidiUart ring,
# parsed by the real Arduino MIDI library through the real UartMidiImpl::setReceiveActions. The script commands are usbmidi / dinmidi S D1 [D2]
# (ui_shot_main.cpp). Pokes only READ ("poke mpevoices 0": slot:note:chan:gate:raw bend:pressure:gain, "poke synthplay": the part's wheel).
# Status bytes: the low nibble is the channel - 1, so member channel 2 is 0x91 (note on) / 0xE1 (bend) / 0xD1 (pressure), the master channel 1 is 0x90 / 0xE0 / 0xD0.
#   I1  per-note pitch bend: chord C E G on member channels 2 3 4 with raw bends +2048, -1024 and +1234 (low 7 bits set: the 14 bit assembly), then one
#       more bend on channel 3 only: that voice moves, the other two keep theirs
#   I1b the ends of the 14 bit range (-8192, 8191) over USB and over DIN
#   I2  channel pressure to the per-note target (Press > Vol 50 %): pressure 32 on member 3 lowers that voice only, the master channel's pressure 0
#       halves every voice, member 3 back to full leaves it at full
#   I3  master channel bend applies to every voice through the part's wheel (0.5 st at the default 2 st range) and leaves the members' own bends
#   I4  a note off after a bend: the member channel's bend is reset to 0 and the channel struck again: the new note has 0, the released tail keeps
#       its finger's bend, a later bend moves the new note only, a note on with velocity 0 releases it
#   I5  the same chord, bends and pressure over DIN (UART bytes, the Arduino MIDI library) give the same voices as over USB
#   I6  zone Off (poke synthset 14 0): channel pressure is not MPE any more (control for the rows above)
# Every row is checked on each flavour AND the flavours must give the same voice lines (parity). A build whose UsbMidiImpl::read lacks the kPitchBend
# case goes RED on I1 for plus and mini (the mutation proof in the REPORT).
# IMX_MIDI_FLAVOURS="og plus mini" (default) picks the flavours, BIN_OG / BIN_PLUS / BIN_MINI the binaries.
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN_OG="${BIN_OG:-${UI_SHOT_BIN:-$ROOT/build/ui-shot/ui_shot}}"
BIN_PLUS="${BIN_PLUS:-$ROOT/build/ui-shot-plus/ui_shot}"
BIN_MINI="${BIN_MINI:-$ROOT/build/ui-shot-mini/ui_shot}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/imx_midi_mpe_sim.XXXXXX")"
trap 'rm -rf "${WORK:?}"' EXIT
FLAVOURS="${IMX_MIDI_FLAVOURS:-og plus mini}"
bin_of() { case "$1" in og) echo "$BIN_OG";; plus) echo "$BIN_PLUS";; mini) echo "$BIN_MINI";; esac; }
for f in $FLAVOURS; do [ -x "$(bin_of "$f")" ] || { echo "imx_midi_mpe_sim.sh: no ui_shot for $f at $(bin_of "$f")" >&2; exit 99; }; done

fails=0
expect() { if [ "$2" = "$3" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }
near() { awk -v a="$1" -v b="$2" -v tol="${3:-0.02}" 'BEGIN {d = a - b; if (d < 0) d = -d; exit !(b > 0 && d <= b * tol)}'; }
expect_near() { if near "$3" "$2" "${4:-0.02}"; then printf 'ok    %s (%s, want %s)\n' "$1" "$3" "$2"; else fails=$((fails+1)); printf 'FAIL  %s\n      expected: %s\n      got:      %s\n' "$1" "$2" "$3"; fi; }

# the Synth play page on part 1 (instrument 64): Shift + Performance on the original and the Tracker+, the Menu box on the Mini (Pattern box, Up, Left)
enter_of() { case "$1" in mini) echo "tap Pattern; wait 300; hold Menu; tap Up; tap Left; unhold Menu; wait 800";; *) echo "hold Shift; tap Performance; unhold Shift; wait 300";; esac; }
# run FLAVOUR NAME "script": $WORK/FLAVOUR/NAME.poke.out holds what the pokes printed
run() {
    local f="$1" n="$2" b; b="$(bin_of "$1")"
    mkdir -p "$WORK/$f"; rm -rf "${WORK:?}/$f/$n" "$WORK/$f/$n.poke.out"
    UISHOT_POKE="$WORK/$f/$n.poke" ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$b" "$WORK/$f/$n" "wait 500; instr 64; audio 1; $(enter_of "$f"); $3" >"$WORK/$f/$n.out" 2>"$WORK/$f/$n.err"
    [ -f "$WORK/$f/$n.poke.out" ] || : >"$WORK/$f/$n.poke.out"
}
MV='poke mpevoices 0'; PS='poke synthplay'
# vf F NAME K CHAN FIELD: field (1 slot 2 note 3 chan 4 gate 5 raw bend 6 pressure 7 gain) of the voice of channel CHAN in the Kth "poke mpevoices" print
vf() { grep '^mpevoices 0 ' "$WORK/$1/$2.poke.out" | sed -n "$3p" | tr ' ' '\n' | awk -F: -v c="$4" -v f="$5" 'NF == 7 && $3 == c {print $f; exit}'; }
# vfn: the same for the voice of a NOTE (a tail has channel 0)
vfn() { grep '^mpevoices 0 ' "$WORK/$1/$2.poke.out" | sed -n "$3p" | tr ' ' '\n' | awk -F: -v c="$4" -v f="$5" 'NF == 7 && $2 == c {print $f; exit}'; }
vgates() { grep '^mpevoices 0 ' "$WORK/$1/$2.poke.out" | sed -n "$3p" | tr ' ' '\n' | awk -F: 'NF == 7 {print $2 ":" $4}' | sort -n | tr '\n' ' ' | sed 's/ $//'; }
bendv() { grep '^synthplay part 0 ' "$WORK/$1/$2.poke.out" | sed -n "$3p" | awk '{print $5}'; }

# raw bend R (-8192..8191) as the two data bytes "D1 D2" of a pitch bend
bend2() { local v=$(( $1 + 8192 )); echo "$(( v & 127 )) $(( (v >> 7) & 127 ))"; }
# the chord with bends on USB (usbmidi) or DIN (dinmidi), members 2 3 4
chord() {
    echo "$1 0xE1 $(bend2 2048); $1 0xE2 $(bend2 -1024); $1 0xE3 $(bend2 1234); $1 0x91 60 100; $1 0x92 64 100; $1 0x93 67 100;"
}

for F in $FLAVOURS; do
    echo "---- flavour $F ($(bin_of "$F"))"
    # I1
    run "$F" i1 "$(chord usbmidi) wait 100; $MV; usbmidi 0xE2 $(bend2 4096); wait 100; $MV"
    expect "I1 $F raw bends 2048 / -1024 / 1234 sit on the voices of channels 2 / 3 / 4 (the 14 bit value is assembled from both data bytes)" "2048 -1024 1234" "$(vf $F i1 1 2 5) $(vf $F i1 1 3 5) $(vf $F i1 1 4 5)"
    expect "I1 $F one more bend on channel 3 moves that voice only (4096), channels 2 and 4 keep theirs" "2048 4096 1234" "$(vf $F i1 2 2 5) $(vf $F i1 2 3 5) $(vf $F i1 2 4 5)"
    # I1b the ends of the 14 bit range, USB then DIN (the two data bytes 0 0 and 127 127)
    run "$F" i1b "usbmidi 0xE1 $(bend2 -8192); usbmidi 0xE2 $(bend2 8191); usbmidi 0x91 60 100; usbmidi 0x92 64 100; wait 100; $MV; dinmidi 0xE1 $(bend2 8191); dinmidi 0xE2 $(bend2 -8192); wait 100; $MV"
    expect "I1b $F USB bend -8192 and 8191 (the ends of the 14 bit range) reach the voices of channels 2 and 3" "-8192 8191" "$(vf $F i1b 1 2 5) $(vf $F i1b 1 3 5)"
    expect "I1b $F the same ends over DIN (the Arduino MIDI library, running status off): swapped, 8191 and -8192" "8191 -8192" "$(vf $F i1b 2 2 5) $(vf $F i1b 2 3 5)"
    # I2
    run "$F" i2 "poke synthset 16 50; usbmidi 0x91 60 100; usbmidi 0x92 64 100; usbmidi 0x93 67 100; wait 100; $MV; usbmidi 0xD2 32; wait 100; $MV; usbmidi 0xD0 0; wait 100; $MV; usbmidi 0xD2 127; wait 100; $MV"
    g0=$(vf $F i2 1 3 7)
    expect "I2 $F three voices of the same velocity start at the same level" "$g0 $g0" "$(vf $F i2 1 2 7) $(vf $F i2 1 4 7)"
    want=$(awk -v g="${g0:-0}" 'BEGIN {printf "%d", g * (1 + 0.5 * (32 / 127 - 1))}')
    expect_near "I2 $F channel pressure 32 on member 3 at Press > Vol 50 %: that voice is 1 - 0.5 x (1 - 32/127) of its level" "$want" "$(vf $F i2 2 3 7)" 0.02
    expect "I2 $F ... the other two voices keep their level" "$g0 $g0" "$(vf $F i2 2 2 7) $(vf $F i2 2 4 7)"
    expect_near "I2 $F the master channel's pressure 0 puts the voice of channel 2 at half" "$((g0 / 2))" "$(vf $F i2 3 2 7)" 0.02
    expect_near "I2 $F ... member 3 at full pressure again is back at its full level" "$g0" "$(vf $F i2 4 3 7)" 0.02
    expect "I2 $F the stored pressure of member 3 is the byte that was sent (32 then 127)" "32 127" "$(vf $F i2 2 3 6) $(vf $F i2 4 3 6)"
    # I3
    run "$F" i3 "$(chord usbmidi) usbmidi 0xE0 $(bend2 2048); wait 100; $PS; $MV"
    expect "I3 $F master channel bend 2048 at the default 2 st range: the part's wheel is 0.5 st" "0.500" "$(bendv $F i3 1)"
    expect "I3 $F ... the voices keep their own member bends" "2048 -1024 1234" "$(vf $F i3 1 2 5) $(vf $F i3 1 3 5) $(vf $F i3 1 4 5)"
    # I4
    run "$F" i4 "usbmidi 0xE1 $(bend2 1024); usbmidi 0x91 60 100; usbmidi 0x92 64 100; wait 40; usbmidi 0x81 60 0; wait 40; usbmidi 0xE1 $(bend2 0); usbmidi 0x91 62 100; wait 40; $MV; usbmidi 0xE1 $(bend2 -512); wait 40; $MV; usbmidi 0x91 62 0; wait 40; $MV"
    expect "I4 $F the note off of channel 2 left a tail (note 60, channel 0, released) with the bend of its finger (1024), the new note 62 has the reset bend 0" "0 0 1024|2 1 0" "$(vfn $F i4 1 60 3) $(vfn $F i4 1 60 4) $(vfn $F i4 1 60 5)|$(vfn $F i4 1 62 3) $(vfn $F i4 1 62 4) $(vfn $F i4 1 62 5)"
    expect "I4 $F a later bend on channel 2 moves the new note only (-512), the tail stays at 1024" "-512 1024" "$(vfn $F i4 2 62 5) $(vfn $F i4 2 60 5)"
    expect "I4 $F a note on with velocity 0 releases note 62 only, the other channel's note 64 stays" "60:0 62:0 64:1" "$(vgates $F i4 3)"
    # I5
    run "$F" i5 "$(chord dinmidi) poke synthset 16 50; wait 100; $MV; dinmidi 0xD2 32; wait 100; $MV"
    expect "I5 $F the same chord and bends over DIN: the same raw bends per voice" "2048 -1024 1234" "$(vf $F i5 1 2 5) $(vf $F i5 1 3 5) $(vf $F i5 1 4 5)"
    g5=$(vf $F i5 1 3 7)
    want5=$(awk -v g="${g5:-0}" 'BEGIN {printf "%d", g * (1 + 0.5 * (32 / 127 - 1))}')
    expect_near "I5 $F DIN channel pressure 32 on member 3 lowers that voice only" "$want5" "$(vf $F i5 2 3 7)" 0.02
    expect "I5 $F ... the other two keep their level" "$g5 $g5" "$(vf $F i5 2 2 7) $(vf $F i5 2 4 7)"
    # I6
    run "$F" i6 "poke synthset 14 0; poke synthset 16 100; usbmidi 0x91 60 100; usbmidi 0xD1 0; usbmidi 0xD0 0; wait 100; $MV"
    expect "I6 $F zone Off: channel pressure is not MPE, the voice keeps full pressure 127" "127" "$(vf $F i6 1 0 6)"
    # parity record: every voice line the rows read
    { for n in i1 i1b i2 i3 i4 i5; do grep -h '^mpevoices\|^synthplay part 0' "$WORK/$F/$n.poke.out"; done; } > "$WORK/$F.trace"
done
# the flavours produce the same voices, bends, pressures and gains for the same bytes
first=""
for F in $FLAVOURS; do
    [ -z "$first" ] && { first=$F; continue; }
    if cmp -s "$WORK/$first.trace" "$WORK/$F.trace"; then printf 'ok    parity: %s equals %s on every voice line of I1-I5 (incl. I1b) (%s lines)\n' "$F" "$first" "$(wc -l < "$WORK/$F.trace" | tr -d ' ')"
    else fails=$((fails+1)); printf 'FAIL  parity: %s differs from %s\n' "$F" "$first"; diff "$WORK/$first.trace" "$WORK/$F.trace" | head -6; fi
done
if [ "$fails" = 0 ]; then echo "RESULT: GREEN"; exit 0; fi
echo "RESULT: RED ($fails failing)"; exit 1
