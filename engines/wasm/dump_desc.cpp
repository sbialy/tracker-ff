// Prints the descriptor of a plugin (and its PLUG_EXT_SYNTH extension) as JSON, for the module manifest. Linked with the
// plugin's own sources on the Mac (arm64 clang, -include native_compat.h). Metadata only, nothing renders.
#include "plug_abi.h"
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" const plug_host* plug_rt_host;
extern "C" const plug_desc* plug_entry(const plug_host*, uint32_t);
const plug_host* plug_rt_host;

namespace
{
void str(const char* s)
{
    putchar('"');
    for (; s && *s; s++)
    {
        if (*s == '"' || *s == '\\') putchar('\\');
        putchar(*s);
    }
    putchar('"');
}
void num(float v) { printf("%.9g", double(v)); }
void* alloc(uint32_t, uint32_t) { return nullptr; }
uint32_t cycles() { return 0; }
const void* query(uint32_t) { return nullptr; }
} // namespace

int main()
{
    static const plug_host host = {sizeof(plug_host), PLUG_HOST_LEVEL, 44117.647f, sinf, cosf, tanf, tanhf, expf, logf, powf,
                                   memcpy, memset, alloc, cycles, query, exp2f, log2f};
    plug_rt_host = &host;
    const plug_desc* d = plug_entry(&host, (PLUG_ABI_MAJOR << 16) | PLUG_ABI_MINOR);
    if (!d) return 1;
    printf("{\"kind\":%u,\"name\":", unsigned(d->kind));
    str(d->name);
    printf(",\"author\":");
    str(d->author);
    printf(",\"instance_size\":%u,\"params\":[", unsigned(d->instance_size));
    for (uint32_t i = 0; i < d->num_params; i++)
    {
        const plug_param& p = d->params[i];
        printf("%s{\"name\":", i ? "," : "");
        str(p.name);
        printf(",\"unit\":");
        str(p.unit);
        printf(",\"min\":"), num(p.min), printf(",\"max\":"), num(p.max), printf(",\"def\":"), num(p.def), printf(",\"step\":"), num(p.step);
        if (p.enum_count)
        {
            printf(",\"enum\":[");
            for (int k = 0; k < p.enum_count; k++) printf("%s", k ? "," : ""), str(p.enum_names[k]);
            printf("]");
        }
        printf("}");
    }
    printf("]");
    const auto* x = d->query ? static_cast<const plug_ext_synth*>(d->query(PLUG_EXT_SYNTH)) : nullptr;
    if (x)
    {
        printf(",\"synth\":{\"name\":");
        str(x->name);
        printf(",\"stored\":%d,\"patch_engine\":%d,\"type_id\":%d,\"max_voices\":%u,\"volume_param\":%d,\"work_bytes\":%u,\"slot_fx\":%s,\"params\":[",
               int(x->stored), int(x->patch_engine), int(x->type_id), unsigned(x->max_voices), int(x->volume_param), unsigned(x->work_bytes),
               (x->flags & PLUG_SYNTH_SLOT_FX) ? "true" : "false");
        for (uint32_t i = 0; i < x->param_count; i++)
        {
            const plug_synth_param& p = x->params[i];
            printf("%s{\"name\":", i ? "," : "");
            str(p.name);
            printf(",\"def\":"), num(p.def), printf(",\"min\":"), num(p.min), printf(",\"max\":"), num(p.max);
            printf(",\"fmt\":%u,\"curve\":%u", unsigned(p.fmt), unsigned(p.curve));
            if (p.fmt == PLUG_FMT_ENUM && p.enum_names)
            {
                printf(",\"enum\":[");
                for (int k = 0; k <= int(p.max); k++) printf("%s", k ? "," : ""), str(p.enum_names[k]);
                printf("]");
            }
            printf("}");
        }
        printf("],\"sections\":[");
        for (uint32_t s = 0; s < x->section_count; s++)
        {
            const plug_synth_section& sc = x->sections[s];
            printf("%s{\"name\":", s ? "," : "");
            str(sc.name);
            printf(",\"ids\":[");
            for (int k = 0; k < sc.count; k++) printf("%s%u", k ? "," : "", unsigned(sc.ids[k]));
            printf("]}");
        }
        printf("],\"macros\":[");
        for (uint32_t m = 0; m < x->macro_count; m++) printf("%s", m ? "," : ""), str(x->macros[m].name);
        printf("]}");
    }
    printf("}\n");
    return 0;
}
