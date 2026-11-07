# The synth engine plugin table as shell functions, for synth_render_compare.sh (and any older script that sources this file).
# The table itself is engines/engines.json: engine ids, sources, per engine flags, the numbers in the manifest, versions, logo texts
# and the flavours (autof = -ffp-contract=off on every engine, the 1.2.0 release default, fused = the compiler default).
# Functions: synth_plug_info <id> (NAME definition-source DSP-source DSP-flags), synth_plug_numbers <id>, synth_plug_logo <id>, and
# SYNTH_PLUGS. SYNTH_PLUG_OPT_ENGINES and SYNTH_PLUG_OPT_O3 override the OpenTracker module flags and FM4 / B-Type flags of the
# default flavour, for example SYNTH_PLUG_OPT_ENGINES="-O2" SYNTH_PLUG_OPT_O3="-O3" for the fused set.
eval "$(python3 "$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)/../../engines/build_set.py" shell)"
