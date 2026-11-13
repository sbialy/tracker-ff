#!/bin/bash
# Deepest entry of every plugin ELF in a directory, as "name depth" (bytes of the plugin's own stack from the entry point,
# an indirect call inside the plugin may reach any address-taken function of it). The ELFs are what the module repositories
# build with the linked ELF kept (build_plug.py --keep). Feed the output to stack_margin.py --plugin-depths.
# usage: TOOLCHAIN=.../arm-none-eabi- plug_depth.sh ELFDIR
set -eu
S=$(cd "$(dirname "$0")" && pwd); D=$1
: "${TOOLCHAIN:?}"
for f in "$D"/*.elf; do
  n=$(basename "$f" .elf)
  r=$(python3 -I "$S/stack_depth.py" "$f" --resolve "$S/stack_resolve_plug.txt" \
        --root '^plugsynth::(render|prepare|slotPost|process|noteOn|noteOff|slotReset|setTempo|sendTap)\(' --root '^plug_entry$' 2>&1 \
        | grep '^root' | sed 's/.*depth *//; s/ B//' | sort -n | tail -1)
  printf "%-8s %s\n" "$n" "$r"
done
