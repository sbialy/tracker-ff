// Patch codec check for the wave file references (patch field 101), built once against the new tree (-DTREE_WAVES) and once against an old tree
// (wave_ref_compat_check.sh). Both codecs are the real open_patchio.cpp of their release.
//   enc CASE OUT       write a patch of CASE: plain (a patch of 25 values, User 16 = 37 and User B 21 = 0, no reference: what every release
//                      wrote), ref2 (the same values plus references for 16 and 21), ref1 (16 only), refidx0 (16 with a path and index 0: a file the list does not
//                      reach), ref1 and the others are TREE_WAVES only
//   dec IN             decode IN, print what this codec reads, the references and whether each is in force, and whether re-encoding gives IN back
//   edit IN OUT P V    decode IN, set value P to V (what an older firmware does when the encoder moves User), encode to OUT
#include "open_patchdata.h"
#include <cmath>
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
        pd->nParams = 25;
        for (int i = 0; i < pd->nParams; i++) pd->values[i] = 0.25f * float(i + 1);
        pd->values[16] = 37;
        pd->values[21] = 0;
        pd->nMacros = kPatchMacros;
        std::strncpy(pd->macros[0].name, "M1", kMacroNameMax);
        pd->macros[0].count = 1;
        pd->macros[0].param[0] = 1;
        pd->macros[0].amount[0] = 0.5f;
#ifdef TREE_WAVES
        auto add = [&](int param, int index, const char* path)
        {
            WaveRef& r = pd->waves.ref[pd->waves.n++];
            r.param = int16_t(param);
            r.index = int16_t(index);
            std::strncpy(r.path, path, kWavePath - 1);
        };
        if (!strcmp(c, "ref2")) { add(16, 37, "AKWF_0004/AKWF_0301.wav"); add(21, 0, "AKWF_vgame/AKWF_vgame_0009.wav"); }
        else if (!strcmp(c, "ref1")) add(16, 37, "AKWF_0004/AKWF_0301.wav");
        else if (!strcmp(c, "refidx0")) { pd->values[16] = 0; add(16, 0, "AKWF_vgame/AKWF_vgame_0009.wav"); }
#else
        if (strcmp(c, "plain")) return 5; // an old tree writes no references
#endif
        std::vector<uint8_t> out(8192);
        const size_t m = encodePatch(*pd, out.data(), out.size());
        if (!m) { printf("encode failed\n"); return 3; }
        FILE* f = fopen(argv[3], "wb");
        fwrite(out.data(), 1, m, f);
        fclose(f);
        return 0;
    }
    if (!strcmp(argv[1], "dec") || !strcmp(argv[1], "edit"))
    {
        const auto in = slurp(argv[2]);
        if (in.empty() || !decodePatch(in.data(), in.size(), *pd)) { printf("decode failed\n"); return 2; }
        if (!strcmp(argv[1], "edit") && argc == 6) pd->values[atoi(argv[4])] = float(atof(argv[5]));
        std::vector<uint8_t> out(8192);
        const size_t m = encodePatch(*pd, out.data(), out.size());
        if (!strcmp(argv[1], "edit"))
        {
            FILE* f = fopen(argv[3], "wb");
            fwrite(out.data(), 1, m, f);
            fclose(f);
            return m ? 0 : 3;
        }
        const bool same = m == in.size() && !memcmp(out.data(), in.data(), m);
        int refs = 0;
#ifdef TREE_WAVES
        refs = pd->waves.n;
#endif
        printf("bytes %zu user %g userb %g unknown-raw %d refs %d resave %s", in.size(), double(pd->values[16]), double(pd->values[21]), int(pd->extra.len), refs,
               same ? "same" : "DIFFERENT");
#ifdef TREE_WAVES
        for (int i = 0; i < pd->waves.n; i++)
        {
            const WaveRef& r = pd->waves.ref[i];
            printf(" | param %d index %d %s %s", r.param, r.index, r.path, std::lround(pd->values[r.param]) == r.index ? "in-force" : "stale");
        }
#endif
        printf("\n");
        return 0;
    }
    return 1;
}
