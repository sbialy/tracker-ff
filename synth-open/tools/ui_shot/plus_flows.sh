#!/bin/bash
# plus_flows.sh [plus|mini|both]   (ref/ff-imx-port-plan.md P4)
# One .flow per FF feature on the Tracker+ / Mini sim: Project Settings page, 4 FX flip, Parts arrange and play, Follow chord,
# Scale snap, Master Inserts (Plus: Shift + Saturation, Mini: the Menu popup's Inserts mode), Play stop lock (Plus: Config key, Mini: Menu box 7),
# synth patch value (Mini: Menu box 1, F8, Mid, the + / - keys),
# the Patch and Morph FX (P12). Every flow ends on a state read: an expect peek, or the poke trace poke_ok checks (P8). Replays flows/*.flow with the devtools mirror replay.ts against this tree's BRIDGE=1 plus or mini ui_shot (build it first: plus_smoke.sh
# or mini_smoke.sh does). Host only. Exit 0 when every replay held. FLOWS=GLOB runs only the flows whose file name matches (e.g. '*_order*').
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
WHICH="${1:-both}"
if [ -z "${DEVTOOLS_DIR:-}" ]; then
    for d in "$HOME/prj/poly-devtools/.wt/rel141-int" "$HOME/prj/poly-devtools/.wt/main"; do
        [ -f "$d/tools/mirror/replay.ts" ] && { DEVTOOLS_DIR="$d"; break; }
    done
fi
[ -f "${DEVTOOLS_DIR:-/x}/tools/mirror/replay.ts" ] || { echo "plus_flows: DEVTOOLS_DIR must hold tools/mirror/replay.ts" >&2; exit 2; }
case "$WHICH" in plus) MS="plus";; mini) MS="mini";; both) MS="plus mini";; *) echo "usage: plus_flows.sh [plus|mini|both]" >&2; exit 2;; esac
fails=0
# *_inserts flows (P6 r4, P8): a card with the pinned dirt.plg in /Plugins/Master (engines/external.py, effects-os, sha256 checked, or DIRT_PLG=FILE),
# the sim traces both insert slots to a poke file (UISHOT_TRACE_INSERTS, host_poke.cpp) and the flow passes only when insert 1 ran Dirt
INS_CARD="${TMPDIR:-/tmp}/plus_flows_card.$$"
stage_inserts() {
    [ -s "$INS_CARD/Plugins/Master/dirt.plg" ] && return 0
    local fx="${TMPDIR:-/tmp}/plus_flows_fx.$$"
    rm -rf "$INS_CARD" "$fx"; mkdir -p "$INS_CARD/Plugins/Master" "$fx"
    if [ -n "${DIRT_PLG:-}" ]; then cp "$DIRT_PLG" "$INS_CARD/Plugins/Master/dirt.plg"
    else python3 -I "$ROOT/engines/external.py" fetch "$fx" --from-local --set effects-os >/dev/null && cp "$fx/dirt.plg" "$INS_CARD/Plugins/Master/dirt.plg"; fi
    rm -rf "$fx"
    [ -s "$INS_CARD/Plugins/Master/dirt.plg" ]
}
# *_logos flows (P7): a card with pluck.plg and alco.plg and their .plgi logos in /Plugins/Synth (LOGO_CARD, default the OG emu card), the sim
# traces the logo slots to a poke file (UISHOT_TRACE_LOGOS, host_poke.cpp)
LOGO_SRC="${LOGO_CARD:-$HOME/prj/poly/build/emu/current/card/Plugins/Synth}"
LOGO_CARD_DIR="${TMPDIR:-/tmp}/plus_flows_logos.$$"
# *_order.flow: a card with an empty /Plugins folder (the order file can be written, replay.ts adds the emu card's engines), and the head of
# the list the flow ends on: DIRT hidden and gone,
# WTFM hidden but at the top because Synth 1 of the default project plays it (the MK66 rule, docs/synth_order.md 4)
ORDER_CARD="${TMPDIR:-/tmp}/plus_flows_order.$$"
ORDER_EXPECT=${ORDER_EXPECT:-WTFM|ACD|FAT|VAP|PERC|CHIP}
# *_units flows (P7 polish): the value text PLK's Pick Pos shows at its default on the MK66 (OG emu synthfmt)
UNITS_EXPECT="${UNITS_EXPECT:-Pick Pos = 35%}"
stage_logos() {
    [ -s "$LOGO_CARD_DIR/Plugins/Synth/alco.plgi" ] && return 0
    rm -rf "$LOGO_CARD_DIR"; mkdir -p "$LOGO_CARD_DIR/Plugins/Synth"
    cp "$LOGO_SRC/pluck.plg" "$LOGO_SRC/pluck.plgi" "$LOGO_SRC/alco.plg" "$LOGO_SRC/alco.plgi" "$LOGO_CARD_DIR/Plugins/Synth/" 2>/dev/null
}
# mini_keys_ok FLOW: a flow that runs on the Mini sim presses only keys of the Mini panel (devtools keys.ts MINI_PANEL_KEYS: F1-F8, Note Instr
# Vol Fx Play Rec Insert Delete Copy Shift, the d-pad, Mid Menu Fn1-4 Plus10 Plus1 Minus1 Minus10), no pad and no enc step, also not through the
# snippets that press Plus keys (boot-to-pattern, goto-*, select-*). The sim takes every key id, so a Plus key would pass on the Mini (P8)
MINI_OK='F[1-8]|Note|Instr|Vol|Fx|Play|Rec|Insert|Delete|Copy|Shift|Up|Down|Left|Right|Mid|Menu|Fn[1-4]|Plus10|Plus1|Minus1|Minus10'
mini_keys_ok() {
    local bad
    bad=$(grep -nE '^[[:space:]]*(tap|tap-if|press|release|hold)[[:space:]]' "$1" | grep -vE "^[0-9]+:[[:space:]]*(tap|tap-if|press|release|hold)[[:space:]]+(${MINI_OK})([[:space:]]|$)")
    bad+=$(grep -nE '^[[:space:]]*(pad|enc)[[:space:]]|^[[:space:]]*use[[:space:]]+(boot-to-pattern|goto-|select-)' "$1")
    [ -z "$bad" ] && return 0
    echo "$bad" | head -3; echo "not a Mini key or step"; return 1
}
# poke_ok FLOW OUT: the poke read that ends a traced flow (host_poke.cpp traces)
#   *_inserts   insert 1 ran Dirt
#   *_playlock  the play state went 1 then 0 and nothing else: the second Play did not stop the pattern, Shift + Play did
#   *_logos     the last logo line: PLK has a list and an editor slot in RAM_G, ALCO a list slot ("logos n=N NAME:stored:list:editor ...")
#   *_grid      the last section tile line of the patch editor grid (synthgrid line): ten tiles, Combine and Sub among them, each with a label
#               and an icon (P7 polish 2, ALCO)
#   *_units     the last value text of the selected patch editor parameter (synthtext line) is the MK66 text UNITS_EXPECT (P7 polish)
#   *_patch     one parameter of the patch editor, the last three value changes went up, then down, and the value ends above where it started
#   *_vary      the Vary amount reaches 20, then the values of Synth 1 move away (F4) and end where they were at 20 (Shift + F4) (P12)
#   *_morphtarget  the Morph target of Synth 1 went 1, 2, 3 and nothing else, the last heading is "Slot 0  Morph 3:" (P12, Shift + Right)
#   *_follow    one track's Follow chord mode went Snap, Transp, Tr+Snap, Off (1 2 3 0) and no other track changed
#   *_parts     in this order the Parts view came up, Build went on, the list grew to 2 Parts, then Part 1 played from Arrange with Part 1
#               selected, and the flow ends stopped in Arrange with 2 Parts in the list
#   *_features  Play stop lock went on once (0 -> 1) and stays on
#   *_order     the Model list as drawn (orderLine): the edit mode showed ACD|FAT|VAP|-WTFM|-DIRT|PERC|CHIP (PERC moved below DIRT, DIRT and
#               WTFM hidden, the card's plugin engines follow), the list ends out of the edit mode starting ORDER_EXPECT with no DIRT. The
#               list out of the edit mode is read from /Plugins/synth-order.txt of the card (replay.ts plays on a copy of it), so its order
#               proves the file was written: without it the default order ACD|FAT|VAP|WTFM|PERC|DIRT|CHIP comes back (P12 round 6)
#   *_splay     the Synth play page (splayLine): open with the MIDI in mode of the page (5) and the voices only on part A (Synth 1), a held
#               cell (note:1), then an open line with no cell, and the last line closed with the MIDI in mode back to 0 (P12 round 7)
#   *_tables    track 1 ran table 1 on Synth 1 (instrument 64) from row 0 into a later row, and no table runs at the end (IMX round 2)
#   *_partauto  Synth 1's Pan and Reverb send went +40 / +30 with the store mask 5 and came back to the base. The replay runs with --wav
#               (an offline replay without it never starts the audio callback, so updateSynthAuto never runs) and the pokes come 7 s and 9 s after
#               the first pattern screen (the console boot ends about 6 s after it, no audio block runs before that) (IMX round 3)
#   *_synthauto the Synth Automation page (IMX round 3): slot 1 of Synth 1 went to an LFO on Part Pan (slot0 1:0), the engine took the Pan
#               destination (engine mask bit 0) with at least two different Pan values, and at the end slot 1 is Off, the store mask 0, the
#               engine mask 0 and the Pan back where it was before the slot went on. Replayed with --wav (the LFO runs in the audio callback)
#   *_chance    (IMX round 4, UISHOT_TRACE_FFSTATE) the Chance 0 % note never reached the synth part, the plain note and the COND note did (>= 2 each), the run ended stopped
#   *_tape      the voice of track 1 started, stopped on the Tape step while the pattern still ran (pl 1) and started again on a later pass
#   *_plen      track 1 was cut to 4 steps (tlen 3,31,31) and sounded at least 3 times as often as track 2
#   *_seed      with seed 7 the first 5 note ons of two Plays came in the same order (both takes seen, the seed stays 7)
#   *_duck      send ducking on Synth 1 (source 11) at 80 % pulled the duck gain below 300 permille (dgmin) with the source 11 and depth 80 kept
#   *_playmodes the play modes 1-Shot Rev, Pingpong Rev and Oscillator (pm 8 9 10) each started the voice (a start while that mode was set), the Oscillator voice still sounded 1.5 s after the time a 1-Shot Rev voice lasts (it loops), the 1-Shot Rev voice lasted > 0.8 s and a 1-Shot with Repitch Steps 4 lasted under 0.8 of that
#   *_ab        the A/B mode of the page: as *_splay with part B on (partb > 0) and 4 voices on each side of the pair (the stock pool)
poke_ok() {
    case "$1" in
        *_chance.flow) awk '/^ffstate /{n=split($9, e, ","); delete c; for (i=1; i<=n; i++) {split(e[i], kv, ":"); c[kv[1]]=kv[2]} last=$3; ok=(c[48]==0 && c[50]>=2 && c[52]>=2 && $11 ~ /^4:0,/)}
                END{exit !(ok && last==0)}' "$2" 2>/dev/null && return 0
            echo "poke: the Chance 0 % note sounded, or the plain and COND notes did not sound twice";;
        *_tape.flow) awk '/^ffstate /{split($21, st, ","); split($23, pl, ","); if (st[1]>=1 && !a) a=1; if (a==1 && pl[1]==0 && $3==1) {a=2; s=st[1]} if (a==2 && st[1]>s && $3==1) b=1}
                END{exit !b}' "$2" 2>/dev/null && return 0
            echo "poke: the voice of track 1 did not stop on the Tape step and start again on a later pass";;
        *_plen.flow) awk '/^ffstate /{n=split($9, e, ","); delete c; for (i=1; i<=n; i++) {split(e[i], kv, ":"); c[kv[1]]=kv[2]} t=$7; a=c[48]+0; b=c[50]+0}
                END{exit !(t ~ /^3,31,31$/ && b>=2 && a>=3*b)}' "$2" 2>/dev/null && return 0
            echo "poke: track 1 is not cut to 4 steps (3,31,31) or did not sound 3 times as often as track 2";;
        *_seed.flow) awk '/^ffstate /{n=split($9, e, ","); delete c; for (i=1; i<=n; i++) {split(e[i], kv, ":"); c[kv[1]]=kv[2]}
                if ($3==1 && pp==0) play++; pp=$3
                for (k=48; k<56; k++) if (c[k]+0 > o[k]+0 && play>=1 && play<=2) {cnt[play]++; if (cnt[play]<=5) seq[play]=seq[play] k " "}
                for (k=48; k<56; k++) o[k]=c[k]; sd=$5}
                END{exit !(play==2 && cnt[1]>=5 && cnt[2]>=5 && seq[1]==seq[2] && sd==7)}' "$2" 2>/dev/null && return 0
            echo "poke: the first 5 note ons of the two seeded Plays differ, or a Play did not come, or the seed is not 7";;
        *_duck.flow) awk '/^ffstate /{split($13, d, ","); g=$15; sd=d[1]; dp=d[2]} END{exit !(g<300 && sd==11 && dp==80)}' "$2" 2>/dev/null && return 0
            echo "poke: the duck gain never went below 300 permille with source 11 (Synth 1) at 80 %";;
        *_playmodes.flow) awk '/^ffstate /{split($23, pl, ","); pm=$17; rs=$19; t=$NF; sub(/^T=/, "", t); t+=0
                if (pl[1]==1 && pp==0) {on[pm]=1; ton=t; mon=pm; rson=rs} if (pl[1]==0 && pp==1) {dur[mon]=t-ton; if (mon==0 && rson>0) rdur=t-ton}
                if (pl[1]==1 && mon==10 && dur[8]>0 && t-ton>dur[8]+1500) loop=1; pp=pl[1]}
                END{exit !(on[8] && on[9] && on[10] && loop && dur[8]>800 && rdur>0 && rdur<0.8*dur[8])}' "$2" 2>/dev/null && return 0
            echo "poke: 1-Shot Rev, Pingpong Rev and Oscillator did not each start the voice or Repitch Steps stayed 0";;
        *_splay.flow) awk '/^splay /{if ($3==1 && $5==5 && $11>0 && $12==0 && $13==0) o=1; if (o && $3==1 && $0 ~ / cells .*[0-9]+:1/) h=1;
                if (h && $3==1 && $0 ~ / cells$/) e=1; l3=$3; l5=$5} END{exit !(o && h && e && l3==0 && l5==0)}' "$2" 2>/dev/null && return 0
            echo "poke: the page did not open voiced on part A, sound a held note, let go of it and close";;
        *_ab.flow) awk '/^splay /{if ($3==1 && $5==5 && $9>0 && $11==4 && $12==4) o=1; if (o && $3==1 && $0 ~ / cells .*[0-9]+:1/) h=1;
                if (h && $3==1 && $0 ~ / cells$/) e=1; l3=$3; l5=$5} END{exit !(o && h && e && l3==0 && l5==0)}' "$2" 2>/dev/null && return 0
            echo "poke: the page did not open with B on and 4 + 4 voices, sound a held note, let go of it and close";;
        *_inserts.flow) grep -q ' 1=running:Dirt' "$2" 2>/dev/null && return 0; echo "poke: insert 1 never ran Dirt";;
        *_playlock.flow) [ "$(sed -n 's/^playing \([01]\) .*/\1/p' "$2" 2>/dev/null | tr -d '\n')" = 10 ] && return 0; echo "poke: play state is not 1 then 0";;
        *_logos.flow) awk '/^logos /{l=$0} END{if (l !~ / PLK:[0-9]+:[0-9]+:[0-9]+( |$)/ || l !~ / ALCO:[0-9]+:[0-9]+:-?[0-9]+( |$)/) exit 1}' "$2" 2>/dev/null \
                && return 0; echo "poke: PLK list + editor logo or ALCO list logo not in RAM_G";;
        *_grid.flow) awk '/^synthgrid /{l=$0} END{n=split(l, t, / \| /); if (l !~ /^synthgrid n=10 / || n != 11 || l !~ /:Combine\/[0-9]+( |$)/ \
                || l !~ /:Sub\/[0-9]+( |$)/) exit 1; for (i = 2; i <= n; i++) if (t[i] ~ /:\// || t[i] ~ /\/-1$/) exit 1}' "$2" 2>/dev/null \
                && return 0; echo "poke: the ALCO grid is not ten tiles each with a label and an icon";;
        *_units.flow) [ "$(sed -n 's/^synthtext pos [0-9]* id [0-9]* //p' "$2" 2>/dev/null | tail -1)" = "$UNITS_EXPECT" ] && return 0
            echo "poke: the selected parameter does not read '$UNITS_EXPECT'";;
        *_patch.flow) awk '/^synthval /{pos=$3; id=$5; n[pos" "id]++; v[pos" "id, n[pos" "id]]=$NF; last=pos" "id}
                END{k=last; c=n[k]; if (c < 5) exit 1; a=v[k,c-4]; b=v[k,c-1]; e=v[k,c]; exit !(b>a && e<b && e>a)}' "$2" 2>/dev/null && return 0
            echo "poke: the patch value did not go up then down";;
        *_morphtarget.flow) [ "$(sed -n 's/^morph part 0 target \([0-9]*\) .*/\1/p' "$2" 2>/dev/null | uniq | tr '\n' ' ')" = "1 2 3 " ] \
            && [[ "$(grep '^morph part 0 ' "$2" 2>/dev/null | tail -1)" == 'morph part 0 target 3 heading label Slot 0  Morph 3:'* ]] && return 0
            echo "poke: the Morph target of Synth 1 did not go 1, 2, 3 with the heading Slot 0  Morph 3:";;
        *_vary.flow) grep '^vary part 0 ' "$2" 2>/dev/null | awk '{p[NR]=$5; h[NR]=$7} END{if (NR < 3) exit 1; k=0; for (i=1; i<=NR; i++) if (p[i]==20 && !k) k=i;
                if (!k) exit 1; moved=0; for (i=k; i<=NR; i++) if (h[i]!=h[k]) moved=1; exit !(moved && h[NR]==h[k] && p[NR]==20)}' && return 0
            echo "poke: the amount did not reach 20 or the values did not move and come back";;
        *_follow.flow) [ "$(awk '/^follow track /{t[$3]=1; m=m $5} END{n=0; for (k in t) n++; print n ":" m}' "$2" 2>/dev/null)" = 1:1230 ] && return 0
            echo "poke: follow mode of one track is not Snap, Transp, Tr+Snap, Off";;
        *_parts.flow) awk '/^parts /{if ($7==1) v=1; if (v && $9==1) b=1; if (b && $3==2) c=1; if (c && $9==0 && $5==0 && $13==0) p=1; lc=$3; lb=$9; lp=$13}
                END{exit !(p && lc==2 && lb==0 && lp==-1)}' "$2" 2>/dev/null && return 0
            echo "poke: Parts list is not view, Build, 2 Parts, Arrange at Part 1, Part 1 played, stopped";;
        *_features.flow) [ "$(sed -n 's/^playstoplock \([01]\) .*/\1/p' "$2" 2>/dev/null | tr -d '\n')" = 1 ] && return 0
            echo "poke: Play stop lock did not go on once";;
        *_order.flow) local last; last=$(grep '^order ' "$2" 2>/dev/null | tail -1 | sed 's/.* rows //')
            grep -qE '^order label label Edit list\|.* rows ACD\|FAT\|VAP\|-WTFM\|-DIRT\|PERC\|CHIP(\||$)' "$2" 2>/dev/null \
            && [[ "$last|" == "$ORDER_EXPECT|"* ]] && [[ "|$last|" != *"|DIRT|"* ]] && return 0
            echo "poke: the edit mode did not show ACD|FAT|VAP|-WTFM|-DIRT|PERC|CHIP or the list does not start $ORDER_EXPECT without DIRT";;
        *_tables.flow) awk '/^tablerun /{split($4, a, /[=\/]/); if (a[2]==1 && a[4]==64 && a[3]==0) s=1; if (s && a[2]==1 && a[4]==64 && a[3]>0) w=1; last=a[2]; lm=$3}
                END{exit !(s && w && last==0 && lm=="0000")}' "$2" 2>/dev/null && return 0
            echo "poke: track 1 did not run table 1 on Synth 1 from row 0 into a later row and stop";;
        *_partauto.flow) awk '/^partauto part 0 /{split($9, v, ","); if ($5==0 && !g) {bp=v[1]; br=v[3]} if ($5==5) {g=1; dp=v[1]-bp; dr=v[3]-br; if (dp>39.9 && dp<40.1 && dr>29.9 && dr<30.1) on=1}
                lm=$5; lp=v[1]; lr=v[3]} END{exit !(on && lm==0 && lp==bp && lr==br)}' "$2" 2>/dev/null && return 0
            echo "poke: Synth 1 Pan and Reverb send did not move by +40 and +30 and come back";;
        *_synthauto_save.flow) awk '/^partauto part 0 /{n++; s=$19; split(s, c, ","); if (c[1] == "1:0:2:6:50:50:10:500:500") {if (!set) set=n; else if (clr && !back) back=n} else if (set && c[1] ~ /^0:-1:/) clr=n}
                /^partauto part 2 /{last=$19} END{split(last, d, ","); exit !(set && clr && back && d[4] == "2:3:0:0:-35:80:20:1500:250" && d[1] ~ /^0:-1:/ && d[2] ~ /^0:-1:/)}' "$2" 2>/dev/null && return 0
            echo "poke: slot 1 of Synth 1 (LFO on Pan) and slot 4 of Synth 3 (Envelope on Delay) did not come back field for field after save, Off and open";;
        *_synthauto.flow) awk '/^partauto part 0 /{split($9, v, ","); if (!seen && $17 ~ /^0:/) {bp=v[1]; seen=1}
                if ($17 == "1:0" && $11 % 2 == 1) {on=1; p[v[1]]=1} ls=$17; lm=$5; le=$11; lp=v[1]}
                END{n=0; for (k in p) n++; exit !(seen && on && n >= 2 && ls ~ /^0:/ && lm == 0 && le == 0 && lp == bp)}' "$2" 2>/dev/null && return 0
            echo "poke: slot 1 of Synth 1 did not run an LFO on Pan (moving values) and come back Off with the Pan at its base";;
        *) return 0;;
    esac
    tail -3 "$2" 2>/dev/null; return 1
}
for m in $MS; do
    BIN="${UI_SHOT_BIN:-$ROOT/build/ui-shot-$m-console-bridge/ui_shot}"
    [ -x "$BIN" ] || { echo "plus_flows: no $BIN" >&2; exit 2; }
    for f in "$HERE"/flows/*.flow; do
        case "$(basename "$f")" in plus_only_*) [ "$m" = plus ] || continue;; mini_only_*) [ "$m" = mini ] || continue;; esac   # keys the Mini maps differently (P8)
        [ -z "${FLOWS:-}" ] || [[ "$(basename "$f")" == $FLOWS ]] || continue   # FLOWS=GLOB: only the flows whose file name matches
        extra=(); poke=""; trace=""
        case "$(basename "$f")" in
            *_inserts.flow)
                stage_inserts || { printf 'FAIL  %s %s (no dirt.plg for the card)\n' "$m" "$(basename "$f")"; fails=$((fails+1)); continue; }
                trace=UISHOT_TRACE_INSERTS; extra=(--card "$INS_CARD");;
            *_logos.flow|*_units.flow|*_grid.flow)
                stage_logos || { printf 'FAIL  %s %s (no pluck/alco .plg + .plgi in %s)\n' "$m" "$(basename "$f")" "$LOGO_SRC"; fails=$((fails+1)); continue; }
                trace=UISHOT_TRACE_LOGOS; extra=(--card "$LOGO_CARD_DIR")
                case "$(basename "$f")" in *_units.flow) trace=UISHOT_TRACE_SYNTHTEXT;; *_grid.flow) trace=UISHOT_TRACE_SYNTHGRID;; esac;;
            *_playlock.flow) trace=UISHOT_TRACE_PLAY;;
            *_patch.flow) trace=UISHOT_TRACE_SYNTHVAL;;
            *_morphtarget.flow) trace=UISHOT_TRACE_MORPH;;
            *_vary.flow) trace=UISHOT_TRACE_VARY;;
            *_splay.flow|*_ab.flow) trace=UISHOT_TRACE_SPLAY;;
            *_follow.flow) trace=UISHOT_TRACE_FOLLOW;;
            *_parts.flow) trace=UISHOT_TRACE_PARTS;;
            *_features.flow) trace=UISHOT_TRACE_FEATURES;;
            *_order.flow) rm -rf "$ORDER_CARD"; mkdir -p "$ORDER_CARD/Plugins"; trace=UISHOT_TRACE_ORDER; extra=(--card "$ORDER_CARD");;
            *_tables.flow) trace=UISHOT_TRACE_TABLES;;
            *_chance.flow|*_tape.flow|*_plen.flow|*_seed.flow|*_duck.flow|*_playmodes.flow) trace=UISHOT_TRACE_FFSTATE; extra=(--wav "${TMPDIR:-/tmp}/plus_flows_partauto.$$.wav");;
            *_partauto.flow|*_synthauto.flow|*_synthauto_save.flow) trace=UISHOT_TRACE_PARTAUTO; extra=(--wav "${TMPDIR:-/tmp}/plus_flows_partauto.$$.wav");;   # --wav turns the audio callback on (engine.update)
        esac
        [ -n "$trace" ] && { poke="${TMPDIR:-/tmp}/plus_flows_poke.$$"; rm -f "$poke" "$poke.out"; }
        # the setup of a flow, written to the poke file before the sim starts ("pat MS" lines wait for the pattern screen after the boot project load, host_poke.cpp)
        case "$(basename "$f")" in
            *_chance.flow) printf '%s\n' 'pat 7000 lastinstr 64' 'pat 7000 stepnote 0 0 48 64' 'pat 7000 stepnote 1 0 50 64' 'pat 7000 stepnote 2 0 52 64' \
                'pat 7000 ffset stepfx 0 0 0 4 0' 'pat 7000 ffset stepfx 2 0 0 48 0' > "$poke";;
            *_tape.flow) printf '%s\n' 'pat 7000 ffset sample 0 1' 'pat 7000 stepnote 0 0 48 0' 'pat 7000 ffset stepfx 0 4 0 50 8' > "$poke";;
            *_plen.flow) printf '%s\n' 'pat 7000 lastinstr 64' 'pat 7000 stepnote 0 0 48 64' 'pat 7000 stepnote 1 0 50 64' 'pat 7000 ffset tlen 0 3' > "$poke";;
            *_seed.flow) { printf '%s\n' 'pat 7000 lastinstr 64' 'pat 7000 ffset seed 7'; for i in 0 1 2 3 4 5 6 7; do printf 'pat 7000 stepnote 0 %d %d 64\npat 7000 ffset stepfx 0 %d 0 4 50\n' $i $((48 + i)) $i; done; } > "$poke";;
            *_duck.flow) printf '%s\n' 'pat 7000 lastinstr 64' 'pat 7000 stepnote 0 0 48 64' 'pat 7000 stepnote 0 4 48 64' 'pat 7000 stepnote 0 8 48 64' \
                'pat 7000 ffset duck 11 80 30 10' > "$poke";;
            *_playmodes.flow) printf '%s\n' 'pat 7000 stepnote 0 0 48 0' 'pat 7000 ffset sample 0 8' 'pat 12800 ffset sample 0 9' 'pat 15800 ffset sample 0 10' \
                'pat 23500 ffset sample 0 0 4' > "$poke";;
            *_tables.flow) printf '%s\n' 'pat 0 lastinstr 64' 'pat 0 stepnote 0 0 48 64' 'pat 0 table 1 4 1 4' 'pat 0 table row 1 0 0 255 0 0' 'pat 0 table row 1 1 2 255 0 0' \
                'pat 0 table row 1 2 -1 255 0 0' 'pat 0 table row 1 3 1 255 0 0' > "$poke";;
            *_synthauto_save.flow) printf '%s\n' 'pat 18000 partauto 2 slot 3 2 0 0 3 -35 80 20 1500 250' 'pat 19000 fmsave - SA1' 'pat 26000 partauto 0 slot 0 off' \
                'pat 26000 partauto 2 slot 3 off' 'pat 28000 fmopen - SA1' 'pat 40000 partauto 2' > "$poke";;
            *_partauto.flow) printf '%s\n' 'pat 7000 partauto 0 0 40' 'pat 7000 partauto 0 2 30' 'pat 9000 partauto 0 0 off' 'pat 9000 partauto 0 2 off' > "$poke";;
        esac
        if [ "$m" = mini ] && ! mini_keys_ok "$f"; then printf 'FAIL  %s %s\n' "$m" "$(basename "$f")"; fails=$((fails+1)); continue; fi
        # a replay that dies on the console's own 60 s "_advance" timeout (a loaded host, load 20+ while other suites run) is run once more, any
        # other failure (a failed expect, a crash, a trace that poke_ok refuses) is red at once, and so is a second timeout (IMX round 4)
        run_flow() {
            [ -z "$poke" ] || rm -f "$poke.out"
            (cd "$DEVTOOLS_DIR/tools/mirror" && { [ -z "$poke" ] || export "$trace=1" UISHOT_POKE="$poke"; } \
                && bun replay.ts "$f" --target sim --model "$m" --offline --ui "$BIN" "${extra[@]}" >"${TMPDIR:-/tmp}/plus_flows.$$.log" 2>&1)
        }
        run_flow || { grep -q 'timeout after 60000 ms: _advance' "${TMPDIR:-/tmp}/plus_flows.$$.log" 2>/dev/null && { printf 'retry %s %s (the 60 s _advance timeout, once)\n' "$m" "$(basename "$f")"; run_flow; }; }
        if [ $? = 0 ] && { [ -z "$poke" ] || poke_ok "$(basename "$f")" "$poke.out"; }; then
            printf 'ok    %s %s\n' "$m" "$(basename "$f")"
        else
            tail -5 "${TMPDIR:-/tmp}/plus_flows.$$.log"; printf 'FAIL  %s %s\n' "$m" "$(basename "$f")"; fails=$((fails+1))
        fi
    done
done
rm -rf "${TMPDIR:-/tmp}/plus_flows_partauto.$$.wav" "${TMPDIR:-/tmp}/plus_flows.$$.log" "$INS_CARD" "$LOGO_CARD_DIR" "$ORDER_CARD" "${TMPDIR:-/tmp}/plus_flows_poke.$$" "${TMPDIR:-/tmp}/plus_flows_poke.$$.out"
[ "$fails" = 0 ] && echo "plus_flows: GREEN" || { echo "plus_flows: RED ($fails)"; exit 1; }
