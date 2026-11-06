# The synth engine plugins, sourced by build_plugs.sh and synth_render_compare.sh. One line per engine:
#   plugin name, SYNTH_ENGINES name (engines.mk), definition source, DSP source, extra flags of the DSP source
# The DSP source gets the firmware's own flags (make -n of core/makefile at TRIM=1): -Os for the OpenTracker modules
# (SYNTH_OPT_ENGINES, fp-contract left at its fused default), -O2 -ffp-contract=off for PHZ and PLK (SYNTH_OPT_PHZ and
# SYNTH_OPT_PLK). PLK's plugin adds -DPLUCK_WORK_POOL (the delay lines in host work memory, pluck_plug.cpp).
SYNTH_PLUGS="ju6 fm4 btype phz dtype cstype atype ttype eight hand arc pluck"
synth_plug_info() {
    case "$1" in
        ju6)    echo "JU6 engine_ju6.cpp ot-synths/ju6/ju6.cpp -Os" ;;
        fm4)    echo "FM4 engine_fm4.cpp ot-synths/fm4/fm4.cpp -Os" ;;
        btype)  echo "BTYPE engine_btype.cpp ot-synths/btype/btype.cpp -Os" ;;
        phz)    echo "PHZ engine_phz.cpp phz/phz.cpp -O2 -ffp-contract=off" ;;
        dtype)  echo "DTYPE engine_dtype.cpp ot-synths/dtype/dtype.cpp -Os" ;;
        cstype) echo "CSTYPE engine_cstype.cpp ot-synths/cstype/cstype.cpp -Os" ;;
        atype)  echo "ATYPE engine_atype.cpp ot-synths/atype/atype.cpp -Os" ;;
        ttype)  echo "TTYPE engine_ttype.cpp ot-synths/ttype/ttype.cpp -Os" ;;
        eight)  echo "8TYPE engine_eight.cpp ot-synths/eight/eight.cpp -Os" ;;
        hand)   echo "HANDTYPE engine_hand.cpp ot-synths/hand/hand.cpp -Os" ;;
        arc)    echo "ARCTYPE engine_arc.cpp ot-synths/arc/arc.cpp -Os" ;;
        pluck)  echo "PLK engine_pluck.cpp pluck/pluck.cpp -O2 -ffp-contract=off -DPLUCK_WORK_POOL" ;;
        *) return 1 ;;
    esac
}
# The numbers of each engine (stored, .patch engine id, type id: the PLUG_SYNTH_PLUGIN line of plug/synth/<name>_plug.cpp and
# the definition's typeId), written into the manifest by build_plug.py --synth so the host lists the engine before it is installed.
# emu_synth.py checks them against the extension.
synth_plug_numbers() {
    case "$1" in
        ju6) echo 7,0x81,0x81 ;; phz) echo 8,0x82,0x82 ;; fm4) echo 9,0x80,0x80 ;; dtype) echo 10,0xA2,0xA2 ;;
        cstype) echo 11,0x83,0x83 ;; atype) echo 12,0x84,0x84 ;; ttype) echo 13,0x85,0x85 ;; eight) echo 14,0x86,0x86 ;;
        hand) echo 15,0x87,0x87 ;; btype) echo 16,0x88,0x88 ;; arc) echo 17,0x89,0x89 ;; pluck) echo 18,0x8A,0x8A ;;
        *) return 1 ;;
    esac
}
# The on-card logo text of each engine (tools/make_plgi.py --text)
synth_plug_logo() {
    case "$1" in
        ju6) echo JU6 ;; fm4) echo FM4 ;; btype) echo B-TYPE ;; phz) echo PHZ ;; dtype) echo D-TYPE ;; cstype) echo CS-TYPE ;;
        atype) echo A-TYPE ;; ttype) echo T-TYPE ;; eight) echo 8-TYPE ;; hand) echo HAND-TYPE ;; arc) echo ARC-TYPE ;; pluck) echo PLUCK ;;
    esac
}
