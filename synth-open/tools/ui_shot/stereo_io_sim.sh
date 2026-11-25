#!/bin/bash
# stereo_io_sim.sh [BIN]
#
# poly-stereo-io steps 1 and 2 on the real firmware code of this tree (ui_shot built with SIO_DEFS="-DFEATURE_STEREO_REC -DFEATURE_RADIO_STEREO"),
# host only, no device:
#   R1  a project with linked pairs records Line in and Radio as a STEREO take (two planes), a project without pairs, the mic sources and a Line in
#       channel of Mono Left / Mono Right stay a mono take (one plane, as before)
#   R2  the stereo take saved as WAV is a 2 channel file with distinct L and R, imported into an instrument it is a stereo .pti (392 + 4 B per frame,
#       planar L then R) with the same distinct L and R
#   R3  the stereo take halves the memory: the buffer is split in two planes (recstat size = 2 * 2 * len)
#   R4  USB source (needs a ui_shot with -DFEATURE_USB_AUDIO_IN, SIO_USB=0 skips): stereo in a pair project, mono without pairs
#   R5  8+3 mono USB audio does not offer the USB in function: the source list ends before USB (UISHOT_USB_FLAG=1 UISHOT_USB_MODE=1), a stored USB
#       source loads as Line in, stereo mode keeps it (needs the same ui_shot as R4)
#   D1  radio: Radio::setStereo(true) only for a Radio source in a stereo take project, false otherwise (the Si4703 register write is needs-device)
# Ends with "RESULT: GREEN" or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="${1:-$ROOT/build/ui-shot-sio/ui_shot}"
[ -x "$BIN" ] || { echo "stereo_io_sim.sh: no $BIN" >&2; exit 2; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/stereo_io_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
fails=0; checks=0
check() { checks=$((checks+1)); if [ "$2" = "ok" ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
run() { # run SD SCRIPT -> $WORK/poke.out
    rm -f "$WORK/poke.out"; ASAN_OPTIONS=detect_leaks=0 UISHOT_POKE="$WORK/poke" "$BIN" "$1" "$2" >/dev/null 2>&1; }
mkproj() { # mkproj SD PAIRKEY(0|1): a project on the card, optionally with the pair key
    rm -rf "$1"; ASAN_OPTIONS=detect_leaks=0 "$BIN" "$1" "wait 500; samplestereo 0 441 882 8000 save; instr 0; setstep 0 0 60 0 0 0 0 0; dirty; wait 8000" >/dev/null 2>&1
    [ "$2" = 1 ] && echo "track.1.pair 1" >>"$1/Workspace/project.mtff"; }
field() { grep "^$1 " "$WORK/poke.out" | tail -n "${3:-1}" | head -1 | sed -n "s/.*$2 \([-0-9]*\).*/\1/p"; }
REC='tap SampleRec; wait 800; tap Rec; wait 800'

echo "### R1: which takes are stereo"
mkproj "$WORK/sd_pair" 1; mkproj "$WORK/sd_nopair" 0
run "$WORK/sd_pair" "wait 500; poke recsource 0; $REC; poke recstat; tap Rec; wait 500"
check "pair project, Line in, Stereo: two planes" "$([ "$(field recstat planes)" = 2 ] && [ "$(field recstat stereotake)" = 1 ] && echo ok || cat "$WORK/poke.out")"
run "$WORK/sd_pair" "wait 500; poke recsource 1; $REC; poke recstat; tap Rec; wait 500"
check "pair project, Radio, Stereo: two planes" "$([ "$(field recstat planes)" = 2 ] && echo ok || cat "$WORK/poke.out")"
run "$WORK/sd_pair" "wait 500; poke recsource 2; $REC; poke recstat; tap Rec; wait 500"
check "pair project, mic (low gain): one plane" "$([ "$(field recstat planes)" = 1 ] && echo ok || cat "$WORK/poke.out")"
run "$WORK/sd_pair" "wait 500; poke recsource 3; $REC; poke recstat; tap Rec; wait 500"
check "pair project, mic (high gain): one plane" "$([ "$(field recstat planes)" = 1 ] && echo ok || cat "$WORK/poke.out")"
run "$WORK/sd_nopair" "wait 500; poke recsource 0; $REC; poke recstat; tap Rec; wait 500"
check "project without pairs, Line in: one plane, as before" "$([ "$(field recstat planes)" = 1 ] && [ "$(field recstat stereotake)" = 0 ] && echo ok || cat "$WORK/poke.out")"
run "$WORK/sd_nopair" "wait 500; poke recsource 1; $REC; poke recstat; tap Rec; wait 500"
check "project without pairs, Radio: one plane" "$([ "$(field recstat planes)" = 1 ] && echo ok || cat "$WORK/poke.out")"
for t in 1 2; do
    run "$WORK/sd_pair" "wait 500; poke linein $t; poke recsource 0; $REC; poke recstat; tap Rec; wait 500"
    check "pair project, Line in channel $t (Mono Left/Right): one plane" "$([ "$(field recstat planes)" = 1 ] && [ "$(field recstat stereotake)" = 0 ] && echo ok || cat "$WORK/poke.out")"
done

echo "### R2/R3: the take on the card"
run "$WORK/sd_pair" "wait 500; poke recsource 0; $REC; poke recfill 4096; tap Rec; wait 800; poke recstat; poke recsave takeS 5; wait 4000; poke recsave takeW; wait 3000"
check "the stereo take: size = 2 planes * 2 B * length" "$([ "$(field recstat size)" = 16384 ] && [ "$(field recstat len)" = 4096 ] && echo ok || cat "$WORK/poke.out")"
res=$(python3 -I - "$WORK/sd_pair" <<'PY'
import struct, sys
sd = sys.argv[1]
try:
    w = open(sd + '/Recorded/takeS.wav', 'rb').read()
    ch = struct.unpack('<H', w[22:24])[0]
    d = struct.unpack('<%dh' % ((len(w) - 44) // 2), w[44:])
    ok = ch == 2 and len(d) == 8192 and all(d[2*i] == i and d[2*i+1] == -i for i in range(4096))
    print('ok' if ok else 'wav ch %d samples %d' % (ch, len(d)))
except Exception as e:
    print('wav: %s' % e)
PY
)
check "WAV save: 2 channels, L ramp and R negative ramp" "$res"
res=$(python3 -I - "$WORK/sd_pair" <<'PY'
import struct, sys
try:
    p = open(sys.argv[1] + '/Workspace/instruments/instrument_06.pti', 'rb').read()
    n = (len(p) - 392) // 2
    x = struct.unpack('<%dh' % n, p[392:392 + n * 2])
    ok = n == 8192 and all(x[i] == i for i in range(4096)) and all(x[4096 + i] == -i for i in range(4096))
    print('ok' if ok else 'pti size %d' % len(p))
except Exception as e:
    print('pti: %s' % e)
PY
)
check "import into instrument 6: stereo .pti (392 + 4 B per frame), planar L then R, distinct" "$res"

echo "### D1: radio chip stereo decode (the Si4703 register is needs-device)"
run "$WORK/sd_pair" "wait 500; poke recsource 1; tap SampleRec; wait 800; poke recstat"
check "pair project, Radio source: Radio::setStereo(true)" "$([ "$(field recstat radiostereo)" = 1 ] && echo ok || cat "$WORK/poke.out")"
run "$WORK/sd_nopair" "wait 500; poke recsource 1; tap SampleRec; wait 800; poke recstat"
check "project without pairs, Radio source: Radio::setStereo(false), chip stays mono" "$([ "$(field recstat radiostereo)" = 0 ] && echo ok || cat "$WORK/poke.out")"
run "$WORK/sd_pair" "wait 500; poke recsource 0; tap SampleRec; wait 800; poke recstat"
check "pair project, Line in source: Radio::setStereo(false)" "$([ "$(field recstat radiostereo)" = 0 ] && echo ok || cat "$WORK/poke.out")"
run "$WORK/sd_pair" "wait 500; poke linein 1; poke recsource 1; tap SampleRec; wait 800; poke recstat"
check "pair project, Radio, Line in channel Mono Left: Radio::setStereo(false)" "$([ "$(field recstat radiostereo)" = 0 ] && echo ok || cat "$WORK/poke.out")"

if [ "${SIO_USB:-1}" = 1 ]; then
echo "### R4: the USB source (FEATURE_USB_AUDIO_IN) takes stereo in a pair project whatever Config > Line in channel says"
run "$WORK/sd_pair" "wait 500; poke linein 1; poke recsource 4; $REC; poke recstat; tap Rec; wait 500"
check "pair project, USB source, Line in channel Mono Left: two planes" "$([ "$(field recstat planes)" = 2 ] && [ "$(field recstat stereotake)" = 1 ] && echo ok || cat "$WORK/poke.out")"
run "$WORK/sd_nopair" "wait 500; poke recsource 4; $REC; poke recstat; tap Rec; wait 500"
check "project without pairs, USB source: one plane" "$([ "$(field recstat planes)" = 1 ] && echo ok || cat "$WORK/poke.out")"

echo "### R5: 8+3 mono has no USB in function, so no USB recorder source"
mkproj "$WORK/sd_usb" 0
run "$WORK/sd_usb" "wait 500; poke recproj 4; wait 8000"
USB_SD="$WORK/sd_usb"
runmode() { # runmode FLAG MODE SD SCRIPT -> $WORK/poke.out
    rm -f "$WORK/poke.out"; UISHOT_USB_FLAG="$1" UISHOT_USB_MODE="$2" ASAN_OPTIONS=detect_leaks=0 UISHOT_POKE="$WORK/poke" "$BIN" "$3" "$4" >/dev/null 2>&1; }
runmode 1 0 "$USB_SD" "wait 500; tap SampleRec; wait 800; poke recstat"
check "stereo USB audio, project stores USB: five sources, USB kept" "$([ "$(field recstat sources)" = 5 ] && [ "$(field recstat source)" = 4 ] && echo ok || cat "$WORK/poke.out")"
runmode 1 1 "$USB_SD" "wait 500; tap SampleRec; wait 800; poke recstat"
check "8+3 mono, project stores USB: four sources, source falls back to Line in" "$([ "$(field recstat sources)" = 4 ] && [ "$(field recstat source)" = 0 ] && echo ok || cat "$WORK/poke.out")"
runmode 0 1 "$USB_SD" "wait 500; tap SampleRec; wait 800; poke recstat"
check "8+3 selected but USB audio off (mode not running): USB kept as before" "$([ "$(field recstat sources)" = 5 ] && echo ok || cat "$WORK/poke.out")"
fi

if [ "$fails" = 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; else echo "RESULT: RED ($fails failing of $checks)"; exit 1; fi
