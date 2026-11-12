// Patch codec check for the macro depth mode (field 100), built once against the 1.3.0 tree (-DTREE13) and once against the 1.2.x tree
// (macro_mode_compat_check.sh). Both codecs are the real open_patchio.cpp of their release.
//   enc CASE OUT    write a patch of CASE: old (Add, macro amount 0.5, as 1.2.x writes it), old0 (Add, no amount), new0 (Scale, no macro amount, a new patch),
//                   newN (Scale, macro amount 0.5, 1.3.0 only)
//   dec IN          decode IN, print what this codec reads and whether its re-encoded bytes equal the input
#include "open_patchdata.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace polysynth::open;

static std::vector<uint8_t> slurp(const char* path)
{
    std::vector<uint8_t> in(8192);
    FILE* f = fopen(path, "rb");
    if (!f) { in.clear(); return in; }
    in.resize(fread(in.data(), 1, in.size(), f));
    fclose(f);
    return in;
}

int main(int argc, char** argv)
{
    if (argc < 3) return 1;
    static PatchData* pd = new PatchData();
    if (!strcmp(argv[1], "enc") && argc == 4)
    {
        const char* c = argv[2];
        pd->engine = 1;
        pd->nParams = 3;
        for (int i = 0; i < pd->nParams; i++) pd->values[i] = 0.25f * float(i + 1);
        pd->nMacros = kPatchMacros;
        std::strncpy(pd->macros[0].name, "M1", kMacroNameMax);
        pd->macros[0].count = 1;
        pd->macros[0].param[0] = 1;
        pd->macros[0].amount[0] = !strcmp(c, "new0") || !strcmp(c, "old0") ? 0.0f : 0.5f;
#ifdef TREE13
        pd->macroMode = !strncmp(c, "old", 3) ? kMacroModeAdd : kMacroModeScale;
#else
        if (strncmp(c, "old", 3)) return 5; // 1.2.x has no Scale
#endif
        std::vector<uint8_t> out(8192);
        const size_t m = encodePatch(*pd, out.data(), out.size());
        if (!m) { printf("encode failed\n"); return 3; }
        FILE* f = fopen(argv[3], "wb");
        fwrite(out.data(), 1, m, f);
        fclose(f);
        return 0;
    }
    if (!strcmp(argv[1], "dec"))
    {
        const auto in = slurp(argv[2]);
        if (in.empty() || !decodePatch(in.data(), in.size(), *pd)) { printf("decode failed\n"); return 2; }
        std::vector<uint8_t> out(8192);
        const size_t m = encodePatch(*pd, out.data(), out.size());
        const bool same = m == in.size() && !memcmp(out.data(), in.data(), m);
        const char* play;
#ifdef TREE13
        play = macroModeOnLoad(*pd) == kMacroModeScale ? "Scale" : "Add";
#else
        play = "Add"; // 1.2.x has one rule
#endif
        printf("bytes %zu amount %g unknown-raw %d plays %s resave %s\n", in.size(), double(pd->macros[0].amount[0]), int(pd->extra.len), play, same ? "same" : "DIFFERENT");
        return 0;
    }
    return 1;
}
