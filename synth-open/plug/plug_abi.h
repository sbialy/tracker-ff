// Tracker FF runtime plugin ABI 1.1 (1.0: master inserts. 1.1: synth engines, PLUG_EXT_SYNTH, more libm). MIT licence, see synth-open/plug/README.md.
// Format reference: synth-open/plug/README.md. Plain C, shared by the host (firmware) and by plugins (any licence).
//
// File layout (.plg, little endian):
//   plug_file_header  64 bytes
//   plug_manifest     header_size - 64 bytes (96 in ABI 1.0, absent in early files whose header_size is 64)
//   image             image_size bytes: .text .rodata .data of a link at address 0
//   rebase table      rebase_count x uint32: byte offsets of 32-bit words in image that hold an image address (R_ARM_ABS32).
//                     The loader adds the run address of the segment the value points into. No GOT, no ELF on the device.
// crc32 (CRC-32 IEEE) covers every byte after the first 64: manifest, image and rebase table.
// The bss (bss_size bytes) follows the image in RAM and is zeroed by the loader.
//
// Compatibility rules (section 5 of the design):
//   - abi_major must equal the host's, abi_minor and the host level are additive (min_host <= PLUG_HOST_LEVEL).
//   - every struct crossing the boundary starts with struct_size, a reader only reads fields inside it (PLUG_HAS).
//   - new fields go at the end, nothing is reordered or removed. Optional features are queries (PLUG_EXT_*), not flags.
//   - kind, cpu and role are open enums: a host skips what it does not know.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLUG_MAGIC 0x31474C50u   // "PLG1"
#define PLUG_ABI_MAJOR 1         // bumped only for an incompatible change: a host refuses any other major
#define PLUG_ABI_MINOR 1         // additive changes: new tail fields, new optional entry points, new host services
#define PLUG_HOST_LEVEL 1        // host service table level, a plugin's min_host must be <= this

enum
{
    PLUG_KIND_MASTER_INSERT = 1,   // stereo, in place, after the tracks and sends mixer (and Dirt), before the limiters
    PLUG_KIND_SEND_FX = 2,         // stereo return of a send bus (reserved)
    PLUG_KIND_TRACK_FX = 3,        // mono or stereo per track insert (reserved)
    PLUG_KIND_SYNTH = 4,           // ISynthEngine shaped voice engine (PLUG_EXT_SYNTH)
    PLUG_KIND_OFFLINE = 5,         // sample editor effect (reserved, what .ptfx is today)
};

enum
{
    PLUG_CPU_CM4F = 1,   // Cortex-M4F, fpv4-sp-d16, hard float (original Tracker, MK66)
    PLUG_CPU_CM7 = 2,    // Cortex-M7, fpv5 (Tracker+ / Mini, IMX), no host yet
};

// plug_file_header.flags
enum
{
    // Reserved (decision 14): int16 in and out, the host passes the audio library blocks without the float conversion.
    // Meant for bit identical ports of the stock int16 FX (reserved). A host that does not implement it refuses the file.
    PLUG_F_S16 = 1u << 0,
};

// plug_manifest.role: where a plugin may sit besides its kind's own slots. Reserved for later hosts (override slots of the
// stock reverb and delay returns, section 14.7 of the design): a role plugin's first parameters are the stock ones in
// stock order and range. Hosts before 5b treat every role as PLUG_ROLE_INSERT.
enum
{
    PLUG_ROLE_INSERT = 0,
    PLUG_ROLE_REVERB = 1,
    PLUG_ROLE_DELAY = 2,
};

// Extension ids for plug_desc.query and plug_host.query. A query returns NULL or a struct whose first field is its size.
enum
{
    PLUG_EXT_STATE = 1,   // reserved: state beyond the parameters (save and load through the project sidecar)
    PLUG_EXT_TAIL = 2,    // plug_ext_tail (declared below, the current host does not ask for it yet)
    PLUG_EXT_SYNTH = 3,   // plug_ext_synth (declared below): the C form of ISynthEngine, PLUG_KIND_SYNTH plugins
    PLUG_EXT_TEMPO = 4,   // reserved: set_tempo(inst, bpm) for synced inserts
};

typedef struct
{
    uint32_t magic;          // PLUG_MAGIC
    uint16_t header_size;    // 64 + manifest bytes: the image starts here
    uint8_t abi_major;
    uint8_t abi_minor;
    uint8_t kind;            // PLUG_KIND_*
    uint8_t cpu;             // PLUG_CPU_*
    uint16_t flags;          // PLUG_F_*, 0 for a float plugin
    uint32_t uid;            // stable id (FNV-1a of the reverse-dns id string), what a project stores
    uint32_t version;        // plugin version, major << 16 | minor << 8 | patch
    uint32_t min_host;       // lowest PLUG_HOST_LEVEL it needs
    uint32_t image_size;     // multiple of 4
    uint32_t bss_size;
    uint32_t rebase_count;
    uint32_t entry_offset;   // plug_entry(), Thumb bit added by the host
    uint32_t ram_bytes;      // declared instance + alloc bytes (informative in host level 1)
    uint32_t cpu_cycles;     // declared worst case cycles of one 128-frame process() (informative in host level 1)
    uint32_t crc32;          // CRC-32 (IEEE) of everything after these 64 bytes
    uint32_t rw_offset;      // start of the writable segment (.data) in the image, 8 aligned: [0, rw_offset) may run from flash
    uint32_t reserved[2];    // 0
} plug_file_header;

// Readable without running any code: the host lists, explains and refuses a plugin from this alone (logue SDK lesson).
typedef struct
{
    uint32_t struct_size;    // sizeof(plug_manifest) at build time (96 in ABI 1.0)
    char name[16];           // list entry, NUL terminated, copied from plug_desc.name by the builder
    char author[32];
    char licence[16];        // SPDX id, "MIT"
    uint16_t num_params;     // plug_desc.num_params
    uint8_t role;            // PLUG_ROLE_*
    uint8_t reserved0;
    uint32_t reserved[6];    // 0, except [0] the file offset of a logo trailer (synth engines, plug_logo.h) and [1] to [4] the
                             // numbers of a synth engine: stored, patch_engine, type_id, PLUG_SYNTH_MANIFEST (below)
} plug_manifest;

typedef struct
{
    const char* name;         // <= 11 chars shows in full on the parameter page, the sidecar stores values by this name
    const char* unit;         // "", "%", "Hz", "dB", "ms"
    float min, max, def, step;
    uint16_t enum_count;      // > 0: an enum, enum_names has enum_count entries, min 0, max enum_count - 1
    uint16_t flags;           // reserved, 0
    const char* const* enum_names;
} plug_param;

typedef struct plug_host plug_host;

typedef struct
{
    uint32_t struct_size;     // sizeof(plug_desc) the plugin was built with: the host never reads past it
    uint32_t kind;
    const char* name;         // list entry, <= 15 chars
    const char* author;
    uint32_t num_params;      // <= 32
    const plug_param* params;
    uint32_t instance_size;   // bytes the host gives every instance (8-byte aligned, zeroed), <= 16 KB
    // Main thread, audio for this instance not running yet. 0 = ok. Parameters are at their defaults afterwards: the host
    // then sends set_param for every parameter whose project value differs from the default.
    int32_t (*init)(void* inst, const plug_host* host);
    // Audio context: clear the tails.
    void (*reset)(void* inst);
    // Audio context, at a block start (the host queues main thread changes and applies the latest value only).
    void (*set_param)(void* inst, uint32_t index, float value);
    // Audio context, in place, ch[0..channels-1] of frames floats each, full scale +-1.0. Not called while the bus is silent.
    void (*process)(void* inst, float* const* ch, uint32_t channels, uint32_t frames);
    // Optional extensions: NULL or a struct of its own struct_size (PLUG_EXT_*). The pointer itself may be NULL.
    const void* (*query)(uint32_t ext_id);
} plug_desc;

// PLUG_EXT_TAIL (reserved for later hosts: the host keeps calling a plugin with a tail on a silent bus and lets a quiet one sleep)
typedef struct
{
    uint32_t struct_size;
    uint32_t (*tail_frames)(void* inst);           // frames of output after the input went silent, 0 = none
    uint32_t (*quiet)(void* inst);                 // nonzero: the output is silent now, the host may skip process()
    void (*skip)(void* inst, uint32_t frames);     // advance LFOs and write heads exactly as process() on silence would
} plug_ext_tail;

// ---- PLUG_EXT_SYNTH: a synth engine (PLUG_KIND_SYNTH). The host wraps it in an ISynthEngine adapter, so the
// shim's voice pool, macros, patch slots and governor treat it as a built-in engine. The audio calls get the OpenTracker 3.x
// engine structs (synth-open/ot/audio/synth_engine.h: SynthVoice, SynthParams, SynthNote, SynthSlotScratch) by pointer,
// frozen for ABI 1: plug_ext_synth.layout carries their sizes and the host refuses a plugin built against others. Same
// contract as ISynthEngine: audio context, no allocation, no I/O, Volume is the host's.
//
// The metadata arrays have the layout of the shim's ParamDef, SectionDef and MacroDef (open_engine.h, static_assert in the
// host): the host points its engine description straight at them, nothing is copied. They must sit in the text segment.
enum
{   // plug_synth_param.fmt: how the value prints (the shim's open::Fmt, numbers frozen)
    PLUG_FMT_PCT = 0, PLUG_FMT_PCT_MAX, PLUG_FMT_HZ, PLUG_FMT_SEC, PLUG_FMT_LFO_HZ, PLUG_FMT_ENUM, PLUG_FMT_VAL100,
    PLUG_FMT_ON_OFF, PLUG_FMT_SEMI, PLUG_FMT_CENTS, PLUG_FMT_INT, PLUG_FMT_RATIO, PLUG_FMT_MS10, PLUG_FMT_DB, PLUG_FMT_PAN,
    PLUG_FMT_UNIT, // 0.00 .. 1.00 (PHZ X / Y)
    PLUG_FMT_COUNT // 16. The shim's formats past PLUG_FMT_UNIT (PERC's note and body wave) are the built-in PERC's only
};
enum
{   // plug_synth_param.curve: the edit curve of the encoder (open::CurveKind)
    PLUG_CURVE_LINEAR = 0, PLUG_CURVE_EXP, PLUG_CURVE_STEEP, PLUG_CURVE_POWER, PLUG_CURVE_COUNT
};
typedef struct
{
    const char* name;
    float def, min, max;
    uint8_t fmt;                     // PLUG_FMT_*
    uint8_t curve;                   // PLUG_CURVE_*
    const char* const* enum_names;   // PLUG_FMT_ENUM: max + 1 names (min is 0)
} plug_synth_param;
typedef struct
{
    const char* name;         // the editor picks the section icon from it ("Oscillator", "Filter", "Amplifier", ...)
    const char* short_name;
    const uint8_t* ids;       // parameter indices
    uint8_t count;
} plug_synth_section;
typedef struct
{
    const char* name;
    uint8_t bipolar;
    uint8_t count;            // 1 or 2 targets
    struct
    {
        uint8_t param;
        float amount;
    } targets[2];
} plug_synth_macro;

enum
{
    PLUG_SYNTH_SLOT_FX = 1u << 0,   // slot_post is the engine's slot effect (JU6's chorus): the host renders the slot on a private bus
};
// plug_manifest.reserved[4] of a synth engine whose numbers (plug_ext_synth stored, patch_engine, type_id) are in reserved[1..3]
// (build_plug.py --synth). The host lists such an engine before it is installed and refuses to attach it when the extension
// says otherwise. "SYN1"
#define PLUG_SYNTH_MANIFEST 0x314E5953u
// sizeof SynthVoice, SynthParams, SynthNote and SynthSlotScratch of the OpenTracker 3.x interface the plugin was built with
#define PLUG_SYNTH_LAYOUT(voice, params, note, scratch) \
    ((uint32_t)(voice) | ((uint32_t)(params) << 8) | ((uint32_t)(note) << 20) | ((uint32_t)((scratch) >> 3) << 26))

typedef struct
{
    uint32_t struct_size;
    uint32_t layout;          // PLUG_SYNTH_LAYOUT(...) of the plugin's build
    // Identity. stored: the engine's number in a project (strMtValues synthPart[].synthType), patch_engine: field 1 of a
    // .patch file, type_id: the shim's instrument type key. An engine that used to be built in keeps its numbers (JU6 7,
    // 0x81, 0x81, FM4 9, 0x80, 0x80, engine_list.h), so old projects and patches find it. A new engine uses its uid for all
    // three. The host refuses a plugin whose numbers a built-in or an earlier installed engine already has.
    int32_t stored;
    int32_t patch_engine;
    int32_t type_id;
    const char* name;         // on-screen name and the Patches/<name>/ folder, 1..8 printable chars, no '/'
    const plug_synth_param* params;
    uint32_t param_count;     // <= 84 (kSynthParamMax)
    const plug_synth_section* sections;
    uint32_t section_count;   // <= 10 (PHZ has 10, D-Type, CS-Type and T-Type 9)
    const plug_synth_macro* macros;
    uint32_t macro_count;     // <= 6
    int32_t volume_param;     // the host applies it as voice gain (-1: none)
    uint32_t max_voices;      // voices per instrument, 1..4
    uint32_t flags;           // PLUG_SYNTH_*
    // Working RAM from the host (delay lines, pools: Pluck's 8.7 KB), SDRAM (about 20 to 30 cycles per uncached access on the
    // MK66: fine for a delay line read once per sample, not for hot state), zeroed, 8 aligned, given once through set_work
    // after init and before any audio call, kept while the engine is installed. 0 = none (set_work may be NULL then).
    uint32_t work_bytes;
    void (*set_work)(void* work, uint32_t bytes);
    // ISynthEngine, one to one (the pointers are SynthSlotScratch*, SynthParams*, SynthNote*, SynthVoice*). NULL = the
    // ISynthEngine default (nothing) for prepare, slot_reset, set_tempo, slot_post and set_send_tap.
    void (*prepare)(void* scratch, const void* params);
    void (*slot_reset)(void* scratch);
    void (*set_tempo)(float bpm);
    void (*note_on)(void* voice, const void* params, const void* note);
    void (*note_off)(void* voice, const void* params);
    uint32_t (*render)(void* voice, const void* params, float g0, float g_inc, float* l, float* r);   // 0 = finished
    uint32_t (*slot_post)(int32_t slot, void* scratch, const void* params, float* l, float* r);    // 1 = tail
    uint32_t (*set_send_tap)(int32_t slot, float* delay, float* reverb, float gain_d, float gain_r);
} plug_ext_synth;

struct plug_host
{
    uint32_t struct_size;
    uint32_t level;           // PLUG_HOST_LEVEL
    float sample_rate;        // 44117.647 on the MK66
    float (*sinf)(float);
    float (*cosf)(float);
    float (*tanf)(float);
    float (*tanhf)(float);
    float (*expf)(float);
    float (*logf)(float);
    float (*powf)(float, float);
    void* (*memcpy)(void*, const void*, size_t);
    void* (*memset)(void*, int, size_t);
    // init only: flags 0 = SRAM heap, 1 = SDRAM arena (32 KB per insert slot). Freed with the instance, never alone.
    void* (*alloc)(uint32_t bytes, uint32_t flags);
    uint32_t (*cycles)(void);
    const void* (*query)(uint32_t ext_id);   // NULL for every id in host level 1
    // ABI 1.1 (check with PLUG_HAS): more of the firmware's libm, so a port of built-in DSP calls the very same functions.
    // Only what an engine that moves out needs (each host function pulls its libm code into the image): JU6 calls exp2f.
    float (*exp2f)(float);
    float (*log2f)(float);
};

// The one exported symbol. abi is the host's PLUG_ABI_MAJOR << 16 | PLUG_ABI_MINOR.
typedef const plug_desc* (*plug_entry_fn)(const plug_host* host, uint32_t abi);

// True when struct p (of type T, first field struct_size) carries field f.
#define PLUG_HAS(p, T, f) ((p)->struct_size >= offsetof(T, f) + sizeof(((T*)0)->f))

// Put the descriptor here: build_plug.py reads name, author and num_params from it into the manifest.
#define PLUG_DESCRIPTOR __attribute__((section(".rodata.plug_desc"), used))
// The entry point, first in the image.
#define PLUG_ENTRY __attribute__((used, section(".text.plug_entry")))

#ifdef __cplusplus
}
static_assert(sizeof(plug_file_header) == 64, "plug_file_header is 64 bytes in ABI 1");
static_assert(sizeof(plug_manifest) == 96, "plug_manifest is 96 bytes in ABI 1.0");
#endif
