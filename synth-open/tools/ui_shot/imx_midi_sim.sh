#!/bin/bash
# imx_midi_sim.sh in|out
#
# Runs the MIDI sims of the Tracker+ and Mini simulators, whose MIDI is the REAL IMX driver over the byte level stub (host_imxmidi.cpp, imx_stub/):
#   in   imx_midi_mpe_sim.sh: the same raw byte MPE in scenario on the original (decoder), the Tracker+ and the Mini sim, voices and parity
#   out  mpe_out_sim.sh on the Tracker+ and on the Mini sim: the MIDI out byte stream of the real sequencer through the real UsbMidiImpl::send,
#        usb_midi.c midiUsbSend / midiUsbTx and the USB IN packet sink (the Mini skips the Project Settings UI row O8: no File / F7 / F8 keys)
# build/ui-shot-plus and build/ui-shot-mini are made here (host simulator only, never the firmware targets), the original's build/ui-shot comes
# from UI_SHOT_BIN or is made as well. Ends with the RESULT line of the sim (exit 0 GREEN).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
MODE="${1:?usage: imx_midi_sim.sh in|out}"
J="$(sh "$HERE/../jobs.sh" n)"
mk() {   # mk LOG [make args]
    local log="$1"; shift
    make -s -C "$HERE" -j"$J" "$@" >"$log" 2>&1 || { tail -30 "$log"; exit 2; }
}
mkdir -p "$ROOT/build"
FL=" ${IMX_MIDI_FLAVOURS:-og plus mini} "
[ "$MODE" = out ] && FL=" plus mini "
case "$FL" in *" plus "*) mk "$ROOT/build/ui-shot-plus.make.log" PLATFORM=IMX PRODUCT=PLUS;; esac
case "$FL" in *" mini "*) mk "$ROOT/build/ui-shot-mini.make.log" PLATFORM=IMX PRODUCT=MINI;; esac
case "$MODE" in
    in)
        case "$FL" in *" og "*) [ -n "${UI_SHOT_BIN:-}" ] || mk "$ROOT/build/ui-shot.make.log";; esac
        exec bash "$HERE/imx_midi_mpe_sim.sh" ;;
    out)
        rc=0
        for f in plus mini; do
            echo "---- mpe_out_sim on the $f sim"
            skip=""; [ "$f" = mini ] && skip=1
            MPE_OUT_SKIP_UI="$skip" UI_SHOT_BIN="$ROOT/build/ui-shot-$f/ui_shot" bash "$HERE/mpe_out_sim.sh" || rc=1
        done
        exit $rc ;;
    *) echo "imx_midi_sim.sh: in or out" >&2; exit 99 ;;
esac
