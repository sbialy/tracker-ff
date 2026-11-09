#!/usr/bin/env bash
# power_off_repro.sh UI_SHOT OUTDIR MODE   MODE=bug (43a7eaa: the cases marked 0 never power off) or fixed (every case powers off)
# Power off after Workspace saves that fail on a short heap: SlotNames (512 B, Synth::persistSlotNames) gets no memory, as on the MK66
# where operator new returns null. host_fault.cpp injects it while the arm file exists (only firmware allocations, the FT812 model and
# the host file layer are exempt). A case that crashes ui_shot (signal 11) counts as not off: on the MK66 that is the fault_isr loop,
# screen lit, every key dead.
U=$1; O=$2; MODE=$3; mkdir -p $O; rc=0
run() {  # name want(bug) script [fail spec, default size=512,count=0]
  local sd=$O/sd-$1 arm=$O/arm-$1 want=$2 spec=${4:-size=512,count=0}; [ "$MODE" = fixed ] && want=1
  rm -rf $sd $arm $O/rep-$1
  local r; r=$(UISHOT_FAIL=$spec UISHOT_FAIL_ARM=$arm UISHOT_FAIL_REPORT=$O/rep-$1 timeout 300 $U $sd "${3//ARM/$arm}" 2>&1; echo "exit $?")
  local crash=""; echo "$r" | grep -q "signal 11" && crash="CRASH "
  r=$(echo "$r" | grep -E "^(fm|power|exit)" | tr '\n' ' ')
  local off; off=$(echo "$r" | sed -nE 's/.*power hold: off ([0-9]+).*/\1/p'); off=${off:-0}
  local inj; inj=$(cat $O/rep-$1 2>/dev/null | wc -l | tr -d ' ')
  local v=PASS; [ "$off" = "$want" ] || { v=FAIL; rc=1; }
  echo "$v $1 (want off=$want): injected $inj, $crash$r"
}
run healthy        1 "pad 2;wait 12000;fm;power hold 5000"
run song-short     0 "pad 2;touch ARM;tap Song;wait 12000;fm;power hold 10000"
run master-short   0 "pad 2;touch ARM;tap Master;wait 12000;fm;power hold 10000"
run params-short   0 "pad 2;touch ARM;tap Params;wait 12000;fm;power hold 10000"
run song-recovers  0 "pad 2;touch ARM;tap Song;wait 12000;rm ARM;fm;power hold 10000"
run shutdown-short 0 "pad 2;wait 300;touch ARM;power hold 10000"
run config-short   1 "pad 2;touch ARM;tap Config;wait 12000;rm ARM;fm;power hold 10000"
# round 2: the power press itself on a short heap. activateShutdownMode creates the bar control (72 B), a plain new returns null
# and the constructor writes through it (cDisplay::createControl): the main loop is gone, keys dead
run press-short    0 "pad 2;wait 300;touch ARM;power hold 10000" "size=72,count=1"
# round 2: a status left in a running value (1 = fmBrowsingSamples) with no operation behind it. 851a317 clears only End and error
# values, the bounded wait (POWER_OFF_IDLE_WAIT) powers off 3 s after the bar
run stuck-busy     0 "pad 2;wait 300;power press;fmset 1;power hold 10000"
exit $rc
