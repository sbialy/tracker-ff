#!/usr/bin/env bash
# MIDI input caps in the audio interrupt (poly-midi-isr-harden round 2), the firmware's own code on the host:
#   U1..U5 core/source/mtMidi.cpp midiUpdate: at most 32 USB MIDI events per audio block, at most about 1,000 us per block (at least one
#          event), the leftovers keep their order across blocks, nothing is lost. The sim feeds a fake USB receive queue ("usbq N COST_US":
#          N events, each read costs COST_US on the fake micros clock) and runs "usbdrain K": K blocks of the real midiUpdate.
#   C1..C9 core/source/mtSequencer.cpp Sequencer::tick + handleMidiInStop, the external clock catch-up: the sim charges each played pulse
#          ("clockcost US", 96 nano ticks = one pulse on the fake micros clock, 400 us is the Soft Malaka figure). Pulses are played until
#          about 1,000 us of the block are spent, at least one, the rest waits in the backlog (kept to 96 pulses), order and count right,
#          and a Stop plays at most the same budget and drops the rest. The round 3 count cap of 8 played 8 x 400 = 3,200 us per block.
# Usage: midi_isr_cap_sim.sh [UI_SHOT_BINARY]   (default build/ui-shot/ui_shot: make -C synth-open/tools/ui_shot)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BIN="${1:-$ROOT/build/ui-shot/ui_shot}"
[ -x "$BIN" ] || { echo "no $BIN (make -C synth-open/tools/ui_shot)"; echo "RESULT: RED (no binary)"; exit 1; }
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
fails=0
expect() {   # expect TEXT WANT GOT
    if [ "$2" = "$3" ]; then echo "ok    $1"; else echo "FAIL  $1"; echo "      expected: $2"; echo "      got:      $3"; fails=$((fails + 1)); fi
}
sim() {   # sim NAME SCRIPT: the stdout lines of the script
    ASAN_OPTIONS=detect_leaks=0 UISHOT_POKE="$WORK/$1.poke" "$BIN" "$WORK/$1" "wait 500; audio 1; $2" 2>/dev/null | grep -a "^usbdrain"
}
handled() { grep -a "^usbdrain block" | awk '{printf "%s%s", (NR > 1 ? " " : ""), $5}'; }   # handled per block, one line

out="$(sim u1 "usbq 100 0; usbdrain 5")"
expect "U1 100 events queued: 32 32 32 4 0 handled per block (cap 32)" "32 32 32 4 0" "$(echo "$out" | handled)"
expect "U2 the leftovers keep their order across blocks, all 100 read once, 3 blocks left events behind" \
    "usbdrain total 100 in_order 1 deferred 3" "$(echo "$out" | grep -a '^usbdrain total')"

out="$(sim u3 "usbq 20 400; usbdrain 3")"   # 400 us an event: the clock check before the 4th event reads 1,200 us, over 1,000
expect "U3 events of 400 us: 3 per block (the 1,000 us budget, not the cap, ends the drain)" "3 3 3" "$(echo "$out" | handled)"
expect "U3 order across the budget stops" "usbdrain total 9 in_order 1 deferred 3" "$(echo "$out" | grep -a '^usbdrain total')"

out="$(sim u4 "usbq 6 5000; usbdrain 6")"   # one event costs more than the whole budget: always one, never none
expect "U4 events of 5,000 us: exactly one per block, a drain never stalls" "1 1 1 1 1 1" "$(echo "$out" | handled)"
expect "U4 order and the queue empty" "usbdrain total 6 in_order 1 deferred 5" "$(echo "$out" | grep -a '^usbdrain total')"

out="$(sim u5 "usbq 32 0; usbdrain 2")"   # exactly the cap: nothing left behind, not counted as deferred
expect "U5 32 events: all in the first block, no block counted as deferred" "32 0" "$(echo "$out" | handled)"
expect "U5 deferred counter stays 0" "usbdrain total 32 in_order 1 deferred 0" "$(echo "$out" | grep -a '^usbdrain total')"

# ---- clock. A line "clockstate toplay=T pulses=P ticks=N" per poke. cs NAME K FIELD: the field of the Kth print
cs() { grep -a '^clockstate' "$WORK/$1.poke.out" | sed -n "$2p" | sed -E "s/.*[ ]$3=([0-9]+).*/\1/"; }
COST=400
sim c1 "midirt start; clockcost $COST; poke clockstate; midiclk 60; poke clockstate; audioblocks 1; poke clockstate; audioblocks 1; poke clockstate; audioblocks 40; poke clockstate" >/dev/null
t0=$(cs c1 1 ticks); a1=$(cs c1 3 ticks); a2=$(cs c1 4 ticks); fin=$(cs c1 5 ticks); toplay1=$(cs c1 3 toplay); toplay_end=$(cs c1 5 toplay)
expect "C1 60 pulses of 400 us in one block: 3 played (288 nano ticks, 1,200 us), not 8 (3,200 us)" "288" "$((a1 - t0))"
expect "C2 the next block plays 3 more" "288" "$((a2 - a1))"
total=$((toplay1 + 3))   # pulses taken into the backlog by block 1: 60 plus the start compensation
expect "C3 backlog drained after enough blocks" "0" "$toplay_end"
expect "C4 no pulse lost or doubled: the tick count moved by 96 per pulse taken" "$((96 * total))" "$((fin - t0))"

sim c5 "midirt start; clockcost $COST; poke clockstate; midiclk 40; audioblocks 2; midiclk 10; audioblocks 40; poke clockstate; midiclk 1; audioblocks 1; poke clockstate; midiclk 1; audioblocks 1; poke clockstate" >/dev/null
t0=$(cs c5 1 ticks); mid=$(cs c5 2 ticks); l1=$(cs c5 3 ticks); l2=$(cs c5 4 ticks)
comp=$(( total - 60 ))
expect "C5 pulses that arrive during the backlog join it (40 + 10 + the compensation), position ends where the count says" "$((96 * (50 + comp)))" "$((mid - t0))"
expect "C5 a real rate clock after that: one pulse per block, played in that block (96 nano ticks each)" "96 96" "$((l1 - mid)) $((l2 - l1))"

sim c6 "midirt start; clockcost 5000; poke clockstate; midiclk 20; audioblocks 1; poke clockstate; audioblocks 1; poke clockstate" >/dev/null
t0=$(cs c6 1 ticks); b1=$(cs c6 2 ticks); b2=$(cs c6 3 ticks)
expect "C6 a pulse of 5,000 us (over the whole budget): exactly one per block, never none" "96 96" "$((b1 - t0)) $((b2 - b1))"

sim c7 "midirt start; clockcost $COST; poke clockstate; midiclk 5000; poke clockstate; audioblocks 1; poke clockstate" >/dev/null
expect "C7 a flood of 5,000 pulses: the backlog is kept to 96 (minus the 3 played in the block)" "93" "$(cs c7 3 toplay)"

sim c8 "patlen 64; midirt start; clockcost $COST; poke clockstate; midiclk 60; poke clockstate; midirt stop; poke clockstate" >/dev/null
expect "C8 Stop with a backlog of 60 pulses: at most 3 played (the budget), so the stop position is under 1 step (6 pulses a step), not 10" "1" "$(( $(cs c8 3 laststop) < 2 ))"
expect "C8 the backlog is dropped by the Stop" "0" "$(cs c8 3 toplay)"
if [ "$fails" = 0 ]; then echo "RESULT: GREEN"; else echo "RESULT: RED ($fails failing)"; exit 1; fi
