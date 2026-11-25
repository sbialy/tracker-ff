#!/bin/bash
# mpe_out_sim.sh
#
# MPE out (Project Settings > MPE out, core/source/mtMpeOut.h, ref/mpe-out.md) on the real sequencer, the real mtMidi.cpp send path and the real
# project sidecar of this tree (ui_shot, host only, nothing here touches a device). Every row reads the MIDI out byte stream that ui_shot logs
# ("midilog 1": note on, note off, pitch bend, CC, one line each) and never looks at the allocator state. "mpeout N" sets the row the way the menu does.
# Every row has a control run with MPE out Off (the feature disabled) that must NOT satisfy the row, so a row that passes without the feature fails.
# Rows:
#   O1  Off: the stream of a rich scenario (3 tracks, 2 instruments, M FX, CC FX) equals goldens/mpe_off.txt, written by ui_shot BEFORE the feature
#       (commit dc7b7991, UPDATE=1 rewrites it from the binary in BIN). With MPE out on the same scenario differs
#   O2  a 3 note chord on 2 instruments goes to 3 member channels 2 3 4 (not the instrument channels), every off on the channel of its on
#   O3  stealing: 2 members, 3 notes: the oldest note is stopped before the third starts on its channel, never 3 sounding, no off without its on, no stuck
#   O4  a 31 EDO chord stopped in the middle: no stuck note, every member bend back to 0 after Stop
#   O5  a 31 EDO chord: key and bend of every note equal the tuning table (tune off K), bend range 48, the bend comes before the note on
#   O6  the M FX bend is added to the tuning bend of the notes of its instrument (a sounding note moves, the next one starts with it), not to another instrument
#   O7  RPN 6 (zone) and RPN 0 (bend range 48) go out once on enable, again only for a new member count, and 0 members when it is switched off
#   O8  the Project Settings row, through the real UI: the zone is on after the row is set, project.mtff has mpe.out only when it is on, it is read back
# imx_midi_mpe_sim.sh part 2 runs this file on the plus and mini sims (UI_SHOT_BIN): there the MIDI out is the REAL UsbMidiImpl::send -> usb_midi.c midiUsbSend/midiUsbTx
# -> the send buffer ring -> the USB IN packet sink of host_imxmidi.cpp, decoded back to the same "midi ..." lines
# Ends with "RESULT: GREEN" (exit 0) or "RESULT: RED (n failing)".
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAN="${SAN:-}"
BUILD="$ROOT/build/ui-shot${SAN:+-san}"
BIN="${UI_SHOT_BIN:-$BUILD/ui_shot}"  # UI_SHOT_BIN: a prebuilt binary of this flavour, no make (run_tests)
WORK="$(mktemp -d "${TMPDIR:-/tmp}/mpe_out_sim.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$BUILD"
[ -n "${UI_SHOT_BIN:-}" ] || [ -n "${BIN_OVERRIDE:-}" ] || make -s -C "$HERE" -j"$(sh "$HERE/../jobs.sh" n)" ${SAN:+SAN=$SAN} >"$BUILD.make.log" 2>&1 || { tail -30 "$BUILD.make.log"; exit 2; }
BIN="${BIN_OVERRIDE:-$BIN}"
[ -x "$BIN" ] || { echo "mpe_out_sim.sh: no ui_shot at $BIN" >&2; exit 99; }
GOLD="$HERE/goldens/mpe_off.txt"

fails=0; checks=0
check() { checks=$((checks+1)); if [ "$2" = ok ]; then printf 'ok    %s\n' "$1"; else fails=$((fails+1)); printf 'FAIL  %s\n      %s\n' "$1" "$2"; fi; }
eq() { [ "$2" = "$3" ] && echo ok || echo "want '$2' got '$3'"; }
differs() { [ "$2" != "$3" ] && echo ok || echo "the feature-off run gave the same: '$3'"; }   # a control: Off must not look like the row's claim

# run NAME "script": the project is empty, MIDI instrument 1 is 48. $WORK/NAME.raw is everything ui_shot printed, NAME.mid the MIDI out lines without the time
run() {
    rm -rf "$WORK/$1"
    UISHOT_POKE="$WORK/$1.poke" ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$BIN" "$WORK/$1" "wait 500; instr 48; patlen 4; audio 1; midilog 1; $2" >"$WORK/$1.raw" 2>"$WORK/$1.err"
    grep -E '^midi ' "$WORK/$1.raw" | sed 's/ t [0-9.]*$//' >"$WORK/$1.mid"
}
# the notes on the wire in order: "on NOTE chCH" / "off NOTE chCH", one line each (bends and CCs left out)
notes() { awk '/^midi on/ {print "on " $3 " ch" $6} /^midi off/ {print "off " $3 " ch" $5}' "$WORK/$1.mid"; }
# invariants of a stream: a note on on a channel that sounds, an off that is not the sounding note of its channel, notes still sounding at the end,
# the most that sounded at once, notes on channel 1 (the master channel) and notes outside the member channels 2..N+1
inv() {
    awk -v nm="$2" '
        /^midi on/  { n=$3; c=$6; if (c in snd) bad++; snd[c]=n; cnt++; ons++; if (cnt>mx) mx=cnt; if (c==1) m1++; if (nm>0 && (c<2 || c>nm+1)) out++ }
        /^midi off/ { n=$3; c=$5; if (!(c in snd) || snd[c]!=n) bad++; else { delete snd[c]; cnt-- } offs++ }
        END { s=0; for (c in snd) s++; printf "ons=%d offs=%d bad=%d stuck=%d max=%d ch1=%d out=%d", ons, offs, bad, s, mx, m1, out }' "$WORK/$1.mid"
}
CH3="setstep 0 0 60 48 0 0 0 0; setstep 1 0 64 48 0 0 0 0; setstep 2 0 67 49 0 0 0 0"   # C E on instrument 1, G on instrument 2 (channel 1 and 2 today)
PLAY="tap Play; wait 300; tap Play; wait 200"                                          # one step is 115 ms: the chord sounds, Stop in the middle of the note
LOOP="tap Play; wait 1100; tap Play; wait 200"                                         # two and a half passes of the 4 step pattern

# --- O1 Off: byte identical to the stream before the feature
SC="setstep 0 0 60 48 34 200 0 0; setstep 1 0 64 48 8 40 0 0; setstep 2 0 67 49 0 0 0 0; setstep 0 2 62 50 34 20 0 0; setstep 3 1 70 48 0 0 0 0; $LOOP"
run o1 "$SC"
if [ "${UPDATE:-}" = 1 ]; then cp "$WORK/o1.mid" "$GOLD"; echo "golden rewritten: $GOLD"; fi
check "O1 MPE out Off: the MIDI out of a 3 track scenario with M FX and a CC FX is the golden of the build before the feature ($(wc -l <"$GOLD" | tr -d ' ') lines)" "$(cmp -s "$GOLD" "$WORK/o1.mid" && echo ok || echo "differs from the golden: $(diff "$GOLD" "$WORK/o1.mid" | head -4 | tr '\n' '|')")"
run o1c "mpeout 15; $SC"
check "O1 control: with MPE out on (15 members) the same scenario is another stream" "$(differs x "$(md5 -q "$GOLD" 2>/dev/null || md5sum <"$GOLD")" "$(md5 -q "$WORK/o1c.mid" 2>/dev/null || md5sum <"$WORK/o1c.mid")")"

# --- O2 a chord on 3 member channels
run o2 "mpeout 15; wait 300; $CH3; $PLAY"
check "O2 C E G (instruments 1 and 2): the three notes start on member channels 2 3 4, offs on the same channels" "$(eq x "on 84 ch2|on 88 ch3|on 91 ch4|off 84 ch2|off 88 ch3|off 91 ch4" "$(notes o2 | paste -sd'|' -)")"
check "O2 invariants: 3 on, 3 off, no stuck note, nothing on the master channel 1" "$(eq x "ons=3 offs=3 bad=0 stuck=0 max=3 ch1=0 out=0" "$(inv o2 15)")"
run o2c "$CH3; $PLAY"
check "O2 control (Off): the notes stay on the instrument channels 1 1 2" "$(differs x "on 84 ch2|on 88 ch3|on 91 ch4" "$(notes o2c | head -3 | paste -sd'|' -)")"

# --- O3 stealing past the member count
run o3 "mpeout 2; wait 300; $CH3; $LOOP"
check "O3 2 members, 3 notes: the third note stops the oldest on its channel (off 84 ch2) and takes that channel" "$(eq x "on 84 ch2|on 88 ch3|off 84 ch2|on 91 ch2" "$(notes o3 | head -4 | paste -sd'|' -)")"
r=$(inv o3 2)
check "O3 over 2.5 passes: never more than 2 sounding, no off without its note on, no stuck note, only channels 2 and 3" "$([[ "$r" == *"bad=0 stuck=0 max=2 ch1=0 out=0" ]] && echo ok || echo "$r")"
run o3c "mpeout 3; wait 300; $CH3; $LOOP"
check "O3 control (3 members): no steal, the third note starts without an off before it" "$(differs x "on 84 ch2|on 88 ch3|off 84 ch2|on 91 ch2" "$(notes o3c | head -4 | paste -sd'|' -)")"

# --- O4 Stop with sounding members: no stuck note, bends back to 0
T31="tune 31; setstep 0 0 61 48 0 0 0 0; setstep 1 0 63 48 0 0 0 0; setstep 2 0 67 49 0 0 0 0"
run o4 "mpeout 15; wait 300; $T31; $PLAY"
fin=$(awk '/^midi pb/ {b[$5]=$3; if ($3 != 0 && $5 > 1) nz++} END {s=0; for (c in b) if (b[c] != 0) s++; printf "nonzero_bends_sent=%d members_left_bent=%d", (nz > 0), s}' "$WORK/o4.mid")
r=$(inv o4 15)
check "O4 31 EDO chord stopped mid note: bends were sent, every member is back to 0 after Stop, no stuck note" "$([[ "$fin" == "nonzero_bends_sent=1 members_left_bent=0" && "$r" == "ons=3 offs=3 bad=0 stuck=0"* ]] && echo ok || echo "$fin / $r")"
run o4c "$T31; $PLAY"
check "O4 control (Off): no member channel and no bend at all (the notes are the plain keys)" "$(differs x "nonzero_bends_sent=1" "$(awk '/^midi pb/ {nz=1} END {printf "nonzero_bends_sent=%d", nz}' "$WORK/o4c.mid")")"

# --- O5 a 31 EDO chord against the tuning table
TOFF="tune off 61; tune off 63; tune off 67"
tabcheck() {   # NAME NOTES...: the i-th note on of NAME.mid against the table lines of NAME.raw. P = k + off(k), key = round(P) + 24 (middle C), bend = (P + 24 - key) / 48 * 8192
    local n=$1; shift
    awk -v notes="$*" -v T=24 '
        FNR==NR { if ($1=="tune" && $2=="off") off[$3]=$4; next }
        /^midi pb/ { pb[$5]=$3 }
        /^midi on/ {
            i++; split(notes, nk, " "); k=nk[i]; P=k+off[k]; key=int(P+0.5)+T; e=(P+T-key)*8192/48; eb=int(e<0 ? e-0.5 : e+0.5)
            c=$6; b=(c in pb) ? pb[c] : 0; rec=$3+b*48/8192-(P+T); d=b-eb; if (d<0) d=-d; r=rec<0?-rec:rec
            if ($3!=key || d>1 || r>0.004) { printf "note %d: key %d bend %d, table wants key %d bend %d (pitch error %.4f st)", k, $3, b, key, eb, rec; bad=1; exit }
            if (c<2) { printf "note %d on channel %d", k, c; bad=1; exit }
        }
        END { if (!bad) print (i==split(notes, q, " ") ? "ok" : "only " i " notes") }' <(grep '^tune off' "$WORK/$n.raw") "$WORK/$n.mid"
}
run o5 "mpeout 15; wait 300; tune 31; $TOFF; setstep 0 0 61 48 0 0 0 0; setstep 1 0 63 48 0 0 0 0; setstep 2 0 67 49 0 0 0 0; $PLAY"
check "O5 31 EDO keys 61 63 67: key and bend of each note on its member channel are the table's (error under 0.004 st), bend range 48" "$(tabcheck o5 61 63 67)"
b=$(awk '/^midi on/ {n++} END {print n}' "$WORK/o5.mid")
check "O5 the bend is on the wire before every note on (the last bend of its channel, never the old one)" "$(awk '/^midi pb/ {pb[$5]=1} /^midi on/ {if (!($6 in pb)) bad++} END {print (bad ? "a note on without a bend before it" : "ok")}' "$WORK/o5.mid")"
run o5c "tune 31; $TOFF; setstep 0 0 61 48 0 0 0 0; setstep 1 0 63 48 0 0 0 0; setstep 2 0 67 49 0 0 0 0; $PLAY"
check "O5 control (Off): the same project plays the plain keys on the instrument channels, the table check fails" "$(differs x ok "$(tabcheck o5c 61 63 67)")"

# --- O6 M FX plus the tuning bend. M value 200 is +4000 of a plain channel (+-8192 = 2 st) = +167 member units, value 20 is -3192 = -133
MFX="tune 31; $TOFF; setstep 0 0 61 48 34 200 0 0; setstep 0 2 61 48 34 20 0 0; setstep 1 0 63 49 0 0 0 0"
run o6 "mpeout 15; wait 300; $MFX; tap Play; wait 400; tap Play; wait 200"
sum=$(awk '
    FNR==NR { if ($1=="tune" && $2=="off") off[$3]=$4; next }
    function tb(k,   P, key, e) { P=k+off[k]; key=int(P+0.5)+24; e=(P+24-key)*8192/48; return int(e<0 ? e-0.5 : e+0.5) }
    /^midi pb/ { pb[$5]=$3; hist[$5]=hist[$5] " " $3 }
    /^midi on/ { n++; c[n]=$6; at[n]=(($6) in pb) ? pb[$6] : 0 }
    END {
        t61=tb(61); t63=tb(63)
        # on 1: note 61 with M +167, on 2: note 63 of the other instrument with no M, on 3: note 61 again after the second M (-133)
        d1=at[1]-(t61+167); d2=at[2]-t63; d3=at[3]-(t61-133)
        if (d1<0) d1=-d1; if (d2<0) d2=-d2; if (d3<0) d3=-d3
        # the first note moved while it sounded: its channel saw tune - 133 between its on and its off
        mv=index(hist[c[1]], " " (t61-133)) > 0
        printf "%s", (d1<=1 && d2<=1 && d3<=1 && mv) ? "ok" : "note1 " at[1] " want " t61+167 ", note2 " at[2] " want " t63 ", note3 " at[3] " want " t61-133 ", moved " mv
    }' <(grep '^tune off' "$WORK/o6.raw") "$WORK/o6.mid")
check "O6 31 EDO + M FX: note 61 starts at table + 167, the note of the other instrument at the table alone, the second M (-133) moves the sounding note and the next one starts with it" "$sum"
run o6c "$MFX; tap Play; wait 400; tap Play; wait 200"
check "O6 control (Off): the M FX is the plain channel bend 4000 / -3192 on channel 1, the member sum does not exist" "$(differs x ok "$([ "$(grep -c '^midi pb 4000 ch 1' "$WORK/o6c.mid")" = 0 ] && echo ok || echo plain-bend)")"

# --- O7 RPN 6 and RPN 0 once on enable
cfg() { grep '^midi cc ' "$WORK/$1.mid"; }
want4=$(printf '%s\n' "midi cc 101 0 ch 1" "midi cc 100 6 ch 1" "midi cc 6 4 ch 1" "midi cc 101 127 ch 1" "midi cc 100 127 ch 1"; for c in 2 3 4 5; do printf '%s\n' "midi cc 101 0 ch $c" "midi cc 100 0 ch $c" "midi cc 6 48 ch $c"; done)
run o7a "mpeout 4; wait 300"
check "O7 enable with 4 members: RPN 6 = 4 on channel 1 (then the null RPN), RPN 0 = 48 on channels 2 3 4 5" "$(eq x "$want4" "$(cfg o7a)")"
check "O7 every member also gets a centred bend, once" "$(eq x "2 3 4 5" "$(grep '^midi pb' "$WORK/o7a.mid" | awk '$3==0 {print $5}' | paste -sd' ' -)")"
run o7b "mpeout 4; wait 300; mpeout 4; wait 300; $CH3; $LOOP"
check "O7 the same setting again, then a whole song: no RPN is sent a second time (17 CC lines)" "$(eq x 17 "$(cfg o7b | wc -l | tr -d ' ')")"
run o7c "mpeout 4; wait 300; mpeout 6; wait 400"
check "O7 a new member count sends the zone again with 6 (CC 6 = 6 on channel 1, 23 CC lines more)" "$(eq x "40 midi cc 6 6 ch 1" "$(cfg o7c | wc -l | tr -d ' ') $(cfg o7c | grep 'cc 6 6 ch 1' | head -1)")"
run o7d "mpeout 4; wait 300; mpeout 0; wait 200"
check "O7 switched off again: RPN 6 = 0 members on channel 1 and nothing else (22 CC lines)" "$(eq x "22 midi cc 6 0 ch 1" "$(cfg o7d | wc -l | tr -d ' ') $(cfg o7d | grep 'cc 6 0 ch 1' | head -1)")"
run o7e "mpeout 0; wait 300; $CH3; $PLAY"
check "O7 control (Off from the start): no RPN, no bend, not one CC line in a whole run" "$(eq x "0 0" "$(cfg o7e | wc -l | tr -d ' ') $(grep -c '^midi pb' "$WORK/o7e.mid")")"

# MPE_OUT_SKIP_UI=1 (the Mini sim: its keys are Fn1..Fn4, the File / F7 / F8 walk to Project Settings does not exist there, O8 is not run)
if [ -n "${MPE_OUT_SKIP_UI:-}" ]; then echo "info  O8 skipped (MPE_OUT_SKIP_UI): the Project Settings UI walk is for the original and the Tracker+ keys"; else
# --- O8 the row through the real UI (File F7 opens Project Settings, the last row is MPE out), project.mtff, the read back
GO="tap File; wait 800; tap F7; wait 800; tap Down 20; wait 200;"
ui() { rm -rf "$WORK/o8sd"; rm -f "$WORK/poke" "$WORK/poke.out"
       UISHOT_POKE="$WORK/poke" "$BIN" "$WORK/o8sd" "wait 500; audio 1; midilog 1; $1" >"$WORK/o8.raw" 2>>"$WORK/o8.err" || true; }
mtff="$WORK/o8sd/Workspace/project.mtff"
ui "poke projsave"
check "O8 a project with MPE out Off saves no mpe line (the file it had before)" "$(grep -qs '^mpe\.' "$mtff" && echo "has an mpe line" || echo ok)"
ui "$GO tap F8; wait 200; enc 3; tap F8; wait 500; poke projsave"
check "O8 the last Project Settings row set to 3 through the UI: the zone is configured (RPN 6 = 3 on channel 1)" "$(eq x 1 "$(grep -c '^midi cc 6 3 ch 1' "$WORK/o8.raw")")"
check "O8 project.mtff gets mpe.out 3 and no other new line" "$(eq x "mpe.out 3" "$(grep '^mpe\.' "$mtff" | tr '\n' ' ' | sed 's/ $//')")"
rm -f "$WORK/poke" "$WORK/poke.out"; "$BIN" "$WORK/o8sd" "wait 300; audio 1; midilog 1; wait 400" >"$WORK/o8b.raw" 2>>"$WORK/o8.err" || true
check "O8 the next power up reads it back: the zone with 3 members goes out when the MIDI out is on" "$(eq x 1 "$(grep -c '^midi cc 6 3 ch 1' "$WORK/o8b.raw")")"
ui "$GO tap F8; wait 200; enc 3; tap F8; wait 500; $GO tap F8; wait 200; enc -3; tap F8; wait 300; poke projsave"
check "O8 set to 3 and back to Off: the line is gone again and RPN 6 = 0 went out" "$(eq x "0 1" "$(grep -c '^mpe\.' "$mtff" | tr -d ' ') $(grep -c '^midi cc 6 0 ch 1' "$WORK/o8.raw")")"
fi

if [ "$fails" -eq 0 ]; then echo "RESULT: GREEN ($checks checks)"; exit 0; fi
echo "RESULT: RED ($fails failing of $checks)"; exit 1
