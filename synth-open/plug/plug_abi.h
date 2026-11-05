// Tracker FF runtime plugin ABI 1.0 (final for phase 2, master inserts). MIT licence, see synth-open/plug/README.md.
// Design: ~/prj/poly/ref/plugin-architecture.md. Plain C, shared by the host (firmware) and by plugins (any licence).
//
// File layout (.plg, little endian):
//   plug_file_header  64 bytes
//   plug_manifest     header_size - 64 bytes (96 in ABI 1.0, absent in the spike files whose header_size is 64)
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
#define PLUG_ABI_MINOR 0         // additive changes: new tail fields, new optional entry points, new host services
#define PLUG_HOST_LEVEL 1        // host service table level, a plugin's min_host must be <= this

enum
{
    PLUG_KIND_MASTER_INSERT = 1,   // stereo, in place, after the tracks and sends mixer (and Dirt), before the limiters
    PLUG_KIND_SEND_FX = 2,         // stereo return of a send bus (phase 5)
    PLUG_KIND_TRACK_FX = 3,        // mono or stereo per track insert (phase 5)
    PLUG_KIND_SYNTH = 4,           // ISynthEngine shaped voice engine (phase 3, PLUG_EXT_SYNTH)
    PLUG_KIND_OFFLINE = 5,         // sample editor effect (phase 6, what .ptfx is today)
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
    // Meant for bit identical ports of the stock int16 FX (phase 5b). A host that does not implement it refuses the file.
    PLUG_F_S16 = 1u << 0,
};

// plug_manifest.role: where a plugin may sit besides its kind's own slots. Reserved for phase 5b (override slots of the
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
    PLUG_EXT_TAIL = 2,    // plug_ext_tail (declared below, the phase 2 host does not ask for it yet)
    PLUG_EXT_SYNTH = 3,   // reserved: the C form of ISynthEngine (phase 3)
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
    uint32_t reserved[6];    // 0
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

// PLUG_EXT_TAIL (reserved, phase 5b: the host keeps calling a plugin with a tail on a silent bus and lets a quiet one sleep)
typedef struct
{
    uint32_t struct_size;
    uint32_t (*tail_frames)(void* inst);           // frames of output after the input went silent, 0 = none
    uint32_t (*quiet)(void* inst);                 // nonzero: the output is silent now, the host may skip process()
    void (*skip)(void* inst, uint32_t frames);     // advance LFOs and write heads exactly as process() on silence would
} plug_ext_tail;

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
