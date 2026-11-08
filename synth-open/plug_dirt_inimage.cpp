// The Dirt plugin source built into the image (bench reference F): same file, same flags as the .plg build.
// The plugin wrapper lives in poly-effects-os (src/synth-open/plug/fx_dirt_plug.cpp). A FEATURE_PLUG_BENCH build needs a copy at
// synth-open/plug/fx_dirt_plug.cpp (python3 -I engines/external.py source effects-os synth-open/plug fx_dirt_plug.cpp), no other build reads it.
#ifdef FEATURE_PLUG_BENCH
#if !__has_include("plug/fx_dirt_plug.cpp")
#error "FEATURE_PLUG_BENCH: put fx_dirt_plug.cpp of poly-effects-os into synth-open/plug/ (engines/external.py source effects-os synth-open/plug fx_dirt_plug.cpp)"
#endif
#define PLUG_IN_IMAGE 1
#include "plug/fx_dirt_plug.cpp"
#endif
