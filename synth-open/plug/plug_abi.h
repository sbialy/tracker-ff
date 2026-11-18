// Tracker FF runtime plugin ABI 1.4 (1.0: master inserts. 1.1: synth engines, PLUG_EXT_SYNTH, more libm. 1.2: PLUG_EXT_CHORD,
// enum parameters of synth engines up to 128 values. 1.3: PLUG_EXT_TEMPO, the sequencer's tempo and beat for master inserts, PLUG_EXT_TAIL quiet() for
// master inserts that make sound of their own. 1.4: host level 2, PLUG_EXT_FILES, the optional service and view of
// PLUG_EXT_SYNTH). MIT licence, see synth-open/plug/README.md.
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
#define PLUG_ABI_MINOR 4         // additive changes: new tail fields, new optional entry points, new host services
#define PLUG_HOST_LEVEL 2        // host service table level, a plugin's min_host must be <= this (2: host->query answers PLUG_EXT_FILES)

enum
{
    PLUG_KIND_MASTER_INSERT = 1,   // stereo, in place, after the tracks and sends mixer (and Dirt), before the limiters
    PLUG_KIND_SEND_FX = 2,         // stereo return of a send bus (reserved)
    PLUG_KIND_TRACK_FX = 3,        // mono or stereo per track insert (reserved)
    PLUG_KIND_SYNTH = 4,           // ISynthEngine shaped voice engine (PLUG_EXT_SYNTH)
    PLUG_KIND_OFFLINE = 5,         // sample editor effect (reserved, what .ptfx is today)
    PLUG_KIND_EXT = 6,             // extension module of one exact image (core/include/ext_abi.h), develop mode only
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
    PLUG_EXT_TAIL = 2,    // plug_ext_tail (declared below): ABI 1.3 reads quiet() of a master insert, the other fields are reserved
    PLUG_EXT_SYNTH = 3,   // plug_ext_synth (declared below): the C form of ISynthEngine, PLUG_KIND_SYNTH plugins
    PLUG_EXT_TEMPO = 4,   // plug_ext_tempo (declared below, ABI 1.3): the sequencer's tempo and beat for a master insert
    PLUG_EXT_FILES = 5,   // plug_ext_files (declared below, ABI 1.4), asked through plug_host.query: host level 2
    PLUG_EXT_CHORD = 6,   // plug_ext_chord (declared below, ABI 1.2): a synth engine that plays a MIDI Chord step as one voice
    PLUG_EXT_STRUM = 7,   // plug_ext_strum (declared below), asked through plug_host.query: the project's Strum ramp depth
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

// plug_param.flags. A numeric parameter (enum_count 0) may name its end values: the host shows the name instead of the number
// and the unit (the bar and the stored value stay numeric). enum_names then holds the names in this order: first the one for
// min if PLUG_PARAM_NAME_MIN is set, then the one for max if PLUG_PARAM_NAME_MAX is set. An older host ignores the flags and
// shows the number, so a plugin that sets them still runs there. Flags a host does not know are ignored.
enum
{
    PLUG_PARAM_NAME_MIN = 1u << 0,   // enum_names[0] is shown while the value is min ("Auto", "Off")
    PLUG_PARAM_NAME_MAX = 1u << 1,   // the next enum_names entry is shown while the value is max
};

typedef struct
{
    const char* name;         // <= 11 chars shows in full on the parameter page, the sidecar stores values by this name
    const char* unit;         // "", "%", "Hz", "dB", "ms"
    float min, max, def, step;
    uint16_t enum_count;      // > 0: an enum, enum_names has enum_count entries, min 0, max enum_count - 1
    uint16_t flags;           // PLUG_PARAM_*, 0 for none (the field was reserved as 0, so an older host ignores a flag)
    const char* const* enum_names;
} plug_param;

// Number of names a numeric parameter's flags promise in enum_names (0 for an enum: its own enum_count rules)
static inline uint32_t plug_param_name_count(const plug_param* p)
{
    return p->enum_count ? 0u : ((p->flags & PLUG_PARAM_NAME_MIN) ? 1u : 0u) + ((p->flags & PLUG_PARAM_NAME_MAX) ? 1u : 0u);
}

// The name to show for value v instead of the number (a numeric parameter at an end it names), NULL for the number
static inline const char* plug_param_value_name(const plug_param* p, float v)
{
    if (p->enum_count || !p->enum_names) return 0;
    uint32_t i = 0;
    if (p->flags & PLUG_PARAM_NAME_MIN)
    {
        if (v <= p->min) return p->enum_names[0];
        i++;
    }
    if ((p->flags & PLUG_PARAM_NAME_MAX) && v >= p->max) return p->enum_names[i];
    return 0;
}

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

// PLUG_EXT_TAIL. ABI 1.3, master inserts: the host does not call process() while the input bus is silent (nothing was played, no
// tail is left), so an insert that makes sound of its own (a noise bed, a riser) would be mute in the gaps. A plugin that answers
// this query is asked, on a silent bus only, at the start of the block (after its set_param calls and, with PLUG_EXT_TEMPO, its
// transport): quiet(inst) nonzero = the plugin would output silence now, the host skips it as before. Zero = process() runs on zeros
// (the whole insert chain of the block runs, the plugins without this query too, they see a silent block). Audio context. quiet is
// the only field the host reads: tail_frames and skip stay reserved (NULL is valid for both). With signal on the bus every insert
// runs as always, quiet is not asked.
typedef struct
{
    uint32_t struct_size;
    uint32_t (*tail_frames)(void* inst);           // reserved: frames of output after the input went silent, 0 = none
    uint32_t (*quiet)(void* inst);                 // nonzero: the output is silent now, the host may skip process()
    void (*skip)(void* inst, uint32_t frames);     // reserved: advance LFOs and write heads exactly as process() on silence would
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
    // ---- ABI 1.2, optional (NULL = none). A host reads them only when struct_size covers them (PLUG_HAS): a 1.1 host never calls
    // them, so an engine that sets them still loads and plays there. Both run on the MAIN THREAD, never in the audio interrupt, and
    // only while the engine is attached (after set_work). `values` are the stored values of the part's patch (count entries, the
    // engine's own parameters, no macros and no automation): what the patch editor shows.
    // service: called by a host level 2 for every part (0..2) that plays this engine, about every 50 ms. The only place where the
    // engine may call the PLUG_EXT_FILES functions (file reads take milliseconds, the audio interrupt keeps running meanwhile).
    // Whatever it prepares for the audio calls (a table in the work memory) it publishes with one aligned store that the audio code
    // reads (an index or a pointer), never by a lock. Returns nonzero when what the part sounds like changed (a new wave is in place),
    // so the host redraws a view of it.
    uint32_t (*service)(uint32_t part, const float* values, uint32_t count);
    // view: one cycle (or a short stretch) of what the oscillator plays with these values, n samples (n <= PLUG_VIEW_MAX), full scale
    // -127..127. A pure read of tables, no state changes: the patch editor draws it. Not called for an engine without it.
    void (*view)(const float* values, uint32_t count, int8_t* out, uint32_t n);
} plug_ext_synth;
#define PLUG_VIEW_MAX 256u

// ---- PLUG_EXT_FILES (host level 2): single cycle waves from the card for a synth engine. The host lists the .wav files of the
// card folder /Waves and of its subfolders one level down (the AKWF layout: /Waves/AKWF_bw_saw/AKWF_saw_0001.wav), sorted by
// path (byte order), at most PLUG_FILES_MAX of them. The list is built at the first call of a session (a walk of the folder, up to
// a second for a full one) and kept until the next boot. A plugin names a file by its index in that list or, with the tail of the
// struct, by a checked path under /Waves, so it reads nothing outside /Waves. Every function is for plug_ext_synth.service only: called anywhere else (init, the audio
// interrupt, an insert) it does nothing and returns PLUG_FILES_E_CONTEXT. The tail of the struct names a file by path (below).
#define PLUG_FILES_MAX 1024u      // entries of the list
#define PLUG_FILES_PATH 48u       // bytes of a path relative to /Waves, NUL included: a longer one is not listed
#define PLUG_FILES_FRAMES 2048u   // longest wave read_wave delivers (AKWF: 600 frames)
enum
{
    PLUG_FILES_E_NONE = -1,      // no such entry
    PLUG_FILES_E_FORMAT = -2,    // not a RIFF WAVE the host reads: PCM 8, 16, 24 or 32 bit, float 32 bit, 1 or 2 channels
    PLUG_FILES_E_LONG = -3,      // more than PLUG_FILES_FRAMES frames (or none)
    PLUG_FILES_E_CARD = -4,      // the card failed
    PLUG_FILES_E_CONTEXT = -5,   // not called from service
};
typedef struct
{
    uint32_t struct_size;
    int32_t (*count)(void);   // entries of the list, 0..PLUG_FILES_MAX
    // the path of entry index relative to /Waves into name (cap bytes, NUL terminated, cut to fit): its length, or < 0
    int32_t (*name)(uint32_t index, char* name, uint32_t cap);
    // frames [first, first + max) of entry index as mono int16 (the first channel, 8 bit and 24/32 bit scaled to 16 bit, float
    // clamped) into dst: returns the frame count of the whole file (1..PLUG_FILES_FRAMES), or < 0. Copies min(max, frames - first)
    // frames, so a plugin with a small buffer reads a file in pieces. Each call opens, reads and closes the file.
    int32_t (*read_wave)(uint32_t index, uint32_t first, int16_t* dst, uint32_t max);
    // ---- by path (still ABI 1.4, optional tail, read only when struct_size covers it: PLUG_HAS). A patch may name a wave file by its
    // path as well as by the index (a field of the .patch, see wave_ref). The functions below never list the folder, so they reach every
    // file of the card whatever PLUG_FILES_MAX does, and a path stays right when /Waves gains or loses files. A path is relative to /Waves,
    // '/' between folders, at most PLUG_FILES_PATH - 1 characters, ends in .wav, no segment starts with '.' (host: wavePathOk).
    // The path stored for engine parameter `param` of part `part` (0..2) in that part's patch, into path (cap bytes, NUL terminated, cut
    // to fit): its length, or PLUG_FILES_E_NONE when the patch has none for this parameter or the parameter's value is no longer the one
    // the path was stored with (it was edited since, the path is out of date, use the value). The engine asks for the parameter it
    // reads the file index from, with the part number its service() got. It checks nothing on the card: a path may name a file that is not there.
    int32_t (*wave_ref)(uint32_t part, uint32_t param, char* path, uint32_t cap);
    // read_wave on a path: frames of the whole file (1..PLUG_FILES_FRAMES) or < 0, PLUG_FILES_E_NONE for a path that is not valid or not on
    // the card, so "the file is gone" and "the path is bad" are one answer and the engine takes its fallback (the index, else the built-in wave).
    int32_t (*read_path)(const char* path, uint32_t first, int16_t* dst, uint32_t max);
    // The path after `path` in the order of the list (byte order of the relative paths) among all the files of /Waves and of its subfolders one
    // level down, uncapped: what "the next file" means for a file the list does not reach. Into out (cap bytes), its length, or
    // PLUG_FILES_E_NONE when none follows (or `path` is not a valid path). A walk of the card: a few ms for a normal folder, service only.
    int32_t (*next_path)(const char* path, char* out, uint32_t cap);
} plug_ext_files;

// ---- PLUG_EXT_CHORD (ABI 1.2): paraphony. A synth engine that can sound a whole chord in one voice (one filter, one set of
// envelopes for every chord note) answers this query with the indices of two of its own parameters:
//   param_index  an enum of exactly PLUG_CHORD_COUNT values in the order of plug_chord.h (the MIDI Chord FX table, 0 = the
//                played note alone). The engine reads p->end[param_index] in note_on and latches it in the voice: the host
//                may change the value right after note_on returns.
//   mode_param   an enum: 0 Poly (the engine as it was, Chord is not sounded), 1 Para (every note plays its chord in one voice).
//                The patch's own choice, so poly and paraphonic patches of one engine coexist.
// What the host does with it: a MIDI Chord step (FX "0") on a part whose patch is at Para sends ONE note-on (the step note, the
// lowest note of the chord) and puts the step's chord value into param_index for that note-on only. The note-off is the step
// note's alone. With mode_param at Poly, with Arp in the other FX slot or on a host without the query, the step keeps the stock
// path: one note-on per chord note. An engine without the query is never asked anything (older plugins are unchanged).
// The live keys and MIDI in play one note-on per key as always: in Para the engine sounds the patch's Chord on each.
// Older hosts (ABI 1.1, firmware 1.3.0 and before) refuse an enum parameter with more than 65 values. A plugin that must also
// load there declares the Chord parameter numeric (PLUG_FMT_INT, 0 to PLUG_CHORD_COUNT - 1), it reads the same value.
// Strum (a tail field, read only when struct_size covers it): strum_param is an integer parameter from -63 to 63, the time between
// two successive chord notes in units of 2 ms, + up (the lowest note first), - down. The host puts a Strum FX of the step into it
// for that note-on only, like the chord. PLUG_CHORD_NO_STRUM (or a shorter struct): the engine plays every chord unstrummed.
#define PLUG_CHORD_NO_STRUM 0xFFFFFFFFu
typedef struct
{
    uint32_t struct_size;
    uint32_t param_index;     // the Chord parameter (plug_chord.h order)
    uint32_t mode_param;      // the Chord Mode parameter (0 Poly, 1 Para)
    uint32_t strum_param;     // tail: the Strum parameter, or PLUG_CHORD_NO_STRUM
} plug_ext_chord;

// ---- PLUG_EXT_TEMPO (ABI 1.3): a master insert that follows the sequencer (a gater, a synced delay, a tempo LFO). A plugin that
// answers desc->query(PLUG_EXT_TEMPO) is called in the audio context at the start of EVERY block, right before its pending
// set_param calls and process() (not only when something changed). Both functions are optional (NULL = not asked).
//   set_tempo(inst, bpm)               the sequencer's current tempo: the project tempo, a tempo FX or the external clock. > 0.
//   set_transport(inst, beat, playing) (tail field, read only when struct_size covers it)
//       playing  1 while the sequencer plays, 0 stopped (live play, Config, the sample editor)
//       beat     quarter notes since the Play started (4 steps of the pattern = 1 beat), at the first frame of the block, so the
//                same instant the rest of the block belongs to. It runs on through pattern and song changes, it is 0 while
//                stopped, and it wraps at PLUG_TEMPO_BEAT_WRAP beats: a plugin that derives a phase from it must use a rate that
//                is a whole number of cycles in that many beats (every division of a beat and every 1/1024 of one is).
// The values are as fresh as the block: the audio interrupt runs the sequencer first. A host before ABI 1.3 never calls these, so a
// plugin keeps its own default tempo (and a Tempo parameter) for it. The struct and both functions must sit in the text segment.
#define PLUG_TEMPO_BEAT_WRAP 1024.0f
typedef struct
{
    uint32_t struct_size;
    void (*set_tempo)(void* inst, float bpm);
    void (*set_transport)(void* inst, float beat, uint32_t playing);
} plug_ext_tempo;
// PLUG_EXT_STRUM, asked through plug_host.query (a host before it answers NULL: ramp depth 100, strum_ramp.h). The answer is a LIVE
// pointer into firmware RAM, valid for the life of the plugin: ask once at init, read ramp_pct at every note-on (the project can
// change it between notes). The project's Strum ramp setting, 0..100 percent (strum_ramp.h): 0 = no ramp, the plain level, 100 =
// the full ramp. A reader only reads fields inside struct_size (PLUG_HAS).
typedef struct
{
    uint32_t struct_size;
    uint32_t ramp_pct;
} plug_ext_strum;

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
    const void* (*query)(uint32_t ext_id);   // NULL for every id in host level 1, level 2: PLUG_EXT_FILES
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
