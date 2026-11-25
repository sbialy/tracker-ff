#!/usr/bin/env bash
# ext_imx_rows.sh BIN OUTDIR MODULE.plg [MODEL]
# The extension module loader of Tracker+ and Mini (synth-open/ext_imx.cpp, P10 H7) in a plus or mini BRIDGE sim (ui_shot PLATFORM=IMX
# BRIDGE=1, -DUISHOT_EXT_IMX): the module (poly-devtools firmware/ext_imx, build_imx_module.sh) runs under Unicorn (host_extimx.cpp).
# The loader waits the 4 s key window after the boot, so every case waits 5 s before it asks.
#   X1 no /.develop, module on the card: nothing is read (status On, no status.txt), a console line gets rc=-2
#   X2 /.develop, no module: "No module", status.txt "none: no module file"
#   X3 /.develop and the module: Loaded, one clean + invalidate + barrier, status.txt "loaded", the crash marker is gone 3 s later,
#      the console verbs answer through the command hook (ping, ver, uptime with update calls, exists, touch writes the card)
#   X4 the module packed for another image (UISHOT_EXT_IMAGE_ID): "Other img", no entry, a console line gets rc=-2
#   X5 the crash marker of the last boot: "Crashed", crash.txt written, the marker removed, nothing entered
#   X6 the slot is borrowed from the sample pool in develop mode only: the pool sizes and offsets of X1 and X3
#   X7 Shift held at power on (UISHOT_SHIFT_BOOT=1) through the key window (decision D4): Safe, no status.txt, nothing entered
#   X8 a power off (mtSleep::powerOff -> ext_imx_power_down) drops the loaded module: status Off, console rc=-2
# Prints one "ok"/"FAIL" line per row, exit 0 when every row passes.
set -uo pipefail
BIN=$1; O=$2; MOD=$3; MODEL=${4:-plus}
mkdir -p "$O"; rc=0
row() {  # name develop(0|1) module(0|1) marker(0|1) script [env...]
  local d=$O/$1; rm -rf "$d"; mkdir -p "$d/sd/Plugins/Ext" "$d/shots"
  [ "$2" = 1 ] && : > "$d/sd/.develop"
  [ "$3" = 1 ] && cp "$MOD" "$d/sd/Plugins/Ext/module.plg"
  [ "$4" = 1 ] && echo ext > "$d/sd/Plugins/Ext/.loading"
  local script=$5; shift 5
  env "$@" UISHOT_POKE="$d/poke" timeout 300 "$BIN" "$d/sd" "$script" > "$d/log.txt" 2>&1; echo "exit $?" >> "$d/log.txt"
}
has() { grep -qF -- "$2" "$1" 2>/dev/null; }
verdict() { if [ "$2" = 0 ]; then echo "ok    ext $MODEL $1"; else echo "FAIL  ext $MODEL $1"; rc=1; fi; }
P=wait\ 5000

row x1 0 1 0 "$P;poke ext;poke ext ping"
b=0; has "$O/x1/poke.out" "ext status=On" || b=1; has "$O/x1/poke.out" "ext rc=-2" || b=1
[ -e "$O/x1/sd/Plugins/Ext/status.txt" ] && b=1; has "$O/x1/log.txt" "exit 0" || b=1
verdict "X1 no /.develop: nothing read, status On, no status.txt, console rc=-2" $b

row x2 1 0 0 "$P;poke ext"
b=0; has "$O/x2/poke.out" "ext status=No module" || b=1; has "$O/x2/sd/Plugins/Ext/status.txt" "none: no module file" || b=1
has "$O/x2/log.txt" "exit 0" || b=1
verdict "X2 /.develop, no module: No module, status.txt says so" $b

row x3 1 1 0 "$P;poke ext;poke ext ping;poke ext ver;poke ext exists /.develop;poke ext touch /p10.txt;poke ext nosuchverb;wait 3500;poke ext uptime"
o=$O/x3/poke.out; b=0
has "$o" "ext status=Loaded cache clean=1 inval=1 barrier=1" || b=1
has "$o" "ext rc=4 reply=pong" || b=1
has "$o" "reply=ext-imx 0.1.0 abi 494d0001 image" || b=1
has "$o" "reply=exists /.develop 1" || b=1
has "$o" "reply=touch /p10.txt 1" || b=1; has "$O/x3/sd/p10.txt" "ext-imx" || b=1
has "$o" "ext rc=-1 reply=" || b=1
grep -qE "ext rc=[0-9]+ reply=ms [0-9]+ updates [1-9][0-9]*" "$o" || b=1
has "$O/x3/sd/Plugins/Ext/status.txt" "loaded (module image 494d5831 at 80f00000)" || b=1
[ -e "$O/x3/sd/Plugins/Ext/.loading" ] && b=1   # removed after 3 s of clean running
has "$O/x3/log.txt" "exit 0" || b=1
verdict "X3 module loaded, cache steps 1/1/1, ping ver exists touch uptime answer, unknown verb -1, marker cleared" $b

row x4 1 1 0 "$P;poke ext;poke ext ping" UISHOT_EXT_IMAGE_ID=0x12345678
b=0; has "$O/x4/poke.out" "ext status=Other img cache clean=0" || b=1; has "$O/x4/poke.out" "ext rc=-2" || b=1
has "$O/x4/sd/Plugins/Ext/status.txt" "refused: built for another firmware image" || b=1; has "$O/x4/log.txt" "exit 0" || b=1
verdict "X4 module for another image: refused, not entered" $b

row x5 1 1 1 "$P;poke ext"
b=0; has "$O/x5/poke.out" "ext status=Crashed cache clean=0" || b=1; [ -e "$O/x5/sd/Plugins/Ext/crash.txt" ] || b=1
[ -e "$O/x5/sd/Plugins/Ext/.loading" ] && b=1; has "$O/x5/log.txt" "exit 0" || b=1
verdict "X5 crash marker: Crashed, crash.txt, marker removed, not entered" $b
row x7 1 1 0 "$P;poke ext;poke ext ping" UISHOT_SHIFT_BOOT=1
b=0; has "$O/x7/poke.out" "ext status=Safe cache clean=0" || b=1; has "$O/x7/poke.out" "ext rc=-2" || b=1
[ -e "$O/x7/sd/Plugins/Ext/status.txt" ] && b=1; has "$O/x7/log.txt" "exit 0" || b=1
verdict "X7 Shift held in the key window: Safe, nothing read or entered, console rc=-2" $b

row x8 1 1 0 "$P;poke ext ping;power hold 5000;poke ext;poke ext ping"
b=0; has "$O/x8/poke.out" "ext rc=4 reply=pong" || b=1; has "$O/x8/poke.out" "ext status=Off" || b=1
has "$O/x8/poke.out" "ext rc=-2" || b=1; grep -qE "power hold: off [1-9]" "$O/x8/log.txt" || b=1; has "$O/x8/log.txt" "exit 0" || b=1
verdict "X8 power off: the loaded module is dropped (status Off, console rc=-2)" $b

# X6 the develop-mode borrow (core/source/Tracker.cpp borrowExtSlot): without /.develop (X1) the pool is the full SAMPLE_MEMORY_SIZE, no
# slot, the edit buffers end at the pool end. With it (X3) the pool, the project limit and the edit buffers move down by the slot (48 KiB,
# up to 31 B more for the 32 B alignment) and the slot sits in the freed range at the new pool end
f() { grep -oE "pool=[0-9]+ max=[0-9]+ slot=-?[0-9]+ fx=-?[0-9]+" "$1" | head -1 | sed -E 's/^pool=([0-9]+) max=([0-9]+) slot=(-?[0-9]+) fx=(-?[0-9]+)/\1 \2 \3 \4/'; }
read -r p1 m1 s1 x1 <<< "$(f "$O/x1/poke.out")"; read -r p3 m3 s3 x3 <<< "$(f "$O/x3/poke.out")"
echo "pool without /.develop: pool=$p1 max=$m1 slot=$s1 fx=$x1, with: pool=$p3 max=$m3 slot=$s3 fx=$x3" > "$O/x6.txt"
b=0
if [ -z "${p1:-}" ] || [ -z "${p3:-}" ]; then b=1; else
  cut=$((p1 - p3))
  [ "$m1" = "$p1" ] && [ "$s1" = -1 ] && [ "$x1" = $((p1 - 15728640)) ] || b=1
  [ "$cut" -ge 49152 ] && [ "$cut" -le 49183 ] || b=1
  [ "$m3" = "$p3" ] && [ "$s3" = "$p3" ] && [ "$x3" = $((x1 - cut)) ] || b=1
fi
verdict "X6 pool $p1 B without /.develop, ${p3:-?} B with it (slot at the new pool end, edit buffers moved down)" $b
exit $rc
