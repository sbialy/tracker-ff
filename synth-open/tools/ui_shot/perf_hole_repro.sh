#!/bin/bash
# perf_hole_repro.sh: stock Performance mode writes a mixed pattern over the pattern on screen (found by the Parts round, ref/parts.md).
# Pattern 1 track 1 = note 60, pattern 2 track 1 = note 72. Performance, stopped: track 1 switched to pattern 2 (F1 held + encoder),
# back to the Pattern screen (fPerformance.cpp functSwitchModule calls turnOff() before stop(), so stop() sees no Performance mode and
# does not reload the pattern), one note entered on step 5: the save writes the mixed image to pattern_01.mtp.
# Prints HOLE when pattern 1 track 1 step 1 reads note 72 after the reload. BIN: a ui_shot binary (default the no-Parts build).
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="${BIN:-$ROOT/build/ui-shot-noparts/ui_shot}"
SD=$(mktemp -d /tmp/perfhole.XXXXXX)
out=$("$BIN" "$SD" 'pattern 1;plen 16;setstep 0 0 60 48 0 0 0 0;dirty;pattern 2;plen 16;setstep 0 0 72 48 0 0 0 0;dirty;pattern 1;wait 300;tap Performance;wait 500;hold F1;enc 1;unhold F1;wait 300;tap Pattern;wait 500;tap Rec;tap Down 4;pad 5;wait 200;pattern 3;pattern 1;step 0 0' 2>&1)
rm -rf "$SD"
echo "$out" | grep '^step 0 0'
echo "$out" | grep -q '^step 0 0 note 72 ' && echo HOLE || echo "no hole"
