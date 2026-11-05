// Tracker FF runtime plugin ABI, spike version (branch plugin-spike, never shipped).
// Design: ~/prj/poly/ref/plugin-architecture.md. Plain C, shared by the host (firmware) and by plugins (MIT or any licence).
//
// File layout (.plg, little endian):
//   plug_file_header (header_size bytes, 64 in ABI 1)
//   image            image_size bytes: .text .rodata .data of a link at address 0
//   rebase table     rebase_count x uint32: byte offsets of 32-bit words in image that hold an absolute address (R_ARM_ABS32);
//                    the loader adds the run address to each of them. Nothing else is relocated, no GOT, no ELF on the device.
// The bss (bss_size bytes) follows the image in RAM and is zeroed by the loader.
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
    PLUG_KIND_MASTER_INSERT = 1,   // stereo, in place, after the tracks and sends mixer, before the limiters
    PLUG_KIND_SEND_FX = 2,         // stereo return of a send bus
    PLUG_KIND_TRACK_FX = 3,        // mono or stereo per track insert
    PLUG_KIND_SYNTH = 4,           // ISynthEngine shaped voice engine
    PLUG_KIND_OFFLINE = 5,         // sample editor effect (what .ptfx is today)
};

enum
{
    PLUG_CPU_CM4F = 1,   // Cortex-M4F, fpv4-sp-d16, hard float (original Tracker, MK66)
    PLUG_CPU_CM7 = 2,    // Cortex-M7, fpv5 (Tracker+ / Mini, IMX)
};

typedef struct
{
    uint32_t magic;          // PLUG_MAGIC
    uint16_t header_size;    // sizeof at build time, the host reads that many bytes and ignores fields it does not know
    uint8_t abi_major;
    uint8_t abi_minor;
    uint8_t kind;            // PLUG_KIND_*
    uint8_t cpu;             // PLUG_CPU_*
    uint16_t flags;          // reserved, 0
    uint32_t uid;            // stable id (FNV-1a of the reverse-dns id string), what a project stores
    uint32_t version;        // plugin version, major << 16 | minor << 8 | patch
    uint32_t min_host;       // lowest PLUG_HOST_LEVEL it needs
    uint32_t image_size;     // multiple of 4
    uint32_t bss_size;
    uint32_t rebase_count;
    uint32_t entry_offset;   // plug_entry(), Thumb bit added by the host
    uint32_t ram_bytes;      // declared instance + arena bytes (checked against what init asks for)
    uint32_t cpu_cycles;     // declared worst case cycles of one 128-frame process() (host emulator measure)
    uint32_t crc32;          // CRC-32 (IEEE) of image and rebase table
    uint32_t rw_offset;      // start of the writable segment (.data) in the image, 8 aligned: [0, rw_offset) may run from flash
    uint32_t reserved[2];
} plug_file_header;

typedef struct
{
    const char* name;         // <= 15 chars
    const char* unit;         // "", "%", "Hz", "dB", "ms"
    float min, max, def, step;
    uint16_t enum_count;      // > 0: an enum, enum_names has enum_count entries, min 0, max enum_count - 1
    uint16_t flags;           // reserved
    const char* const* enum_names;
} plug_param;

typedef struct plug_host plug_host;

typedef struct
{
    uint32_t struct_size;     // sizeof(plug_desc) the plugin was built with: the host never reads past it
    uint32_t kind;
    const char* name;         // list entry, <= 15 chars
    const char* author;
    uint32_t num_params;
    const plug_param* params;
    uint32_t instance_size;   // bytes the host gives every instance (8-byte aligned, zeroed)
    // Main thread, audio for this instance not running yet. 0 = ok.
    int32_t (*init)(void* inst, const plug_host* host);
    // Audio context: clear the tails.
    void (*reset)(void* inst);
    // Audio context, at a block start (the host queues main thread changes).
    void (*set_param)(void* inst, uint32_t index, float value);
    // Audio context, in place, ch[0..channels-1] of frames floats each, full scale +-1.0.
    void (*process)(void* inst, float* const* ch, uint32_t channels, uint32_t frames);
    // Optional extensions (state save/load, presets, synth voice API): NULL or a struct of its own struct_size.
    const void* (*query)(uint32_t ext_id);
} plug_desc;

struct plug_host
{
    uint32_t struct_size;
    uint32_t level;           // PLUG_HOST_LEVEL
    float sample_rate;
    float (*sinf)(float);
    float (*cosf)(float);
    float (*tanf)(float);
    float (*tanhf)(float);
    float (*expf)(float);
    float (*logf)(float);
    float (*powf)(float, float);
    void* (*memcpy)(void*, const void*, size_t);
    void* (*memset)(void*, int, size_t);
    void* (*alloc)(uint32_t bytes, uint32_t flags);   // init only, from the plugin arena, never freed alone
    uint32_t (*cycles)(void);
    const void* (*query)(uint32_t ext_id);
};

// The one exported symbol. abi is the host's PLUG_ABI_MAJOR << 16 | PLUG_ABI_MINOR.
typedef const plug_desc* (*plug_entry_fn)(const plug_host* host, uint32_t abi);

#ifdef __cplusplus
}
static_assert(sizeof(plug_file_header) == 64, "plug_file_header is 64 bytes in ABI 1");
#endif
