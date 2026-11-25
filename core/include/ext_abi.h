#ifndef EXT_ABI_H_
#define EXT_ABI_H_

// Extension module contract (FEATURE_EXT_HOOKS, MK66): what an image and the module it loads in develop mode share. No macros
// that collide with devhooks.h, so a module compiled with its own hook set can include it too. Plain C and C++.
//
// The module is a plugin file (synth-open/plug/plug_abi.h) of kind PLUG_KIND_EXT at /Plugins/Ext/module.plg, linked at a fixed
// run address against one exact image (synth-open/plug/pack_ext.py):
//   header.reserved[0]  CRC-32 of that image's flash bytes [0, _etext + .data size), what the bootloader programmed
//   header.reserved[1]  the run address it was linked for (the image's SDRAM slot, ext_module.cpp)
//   rebase_count        0 (no rebase at a fixed address)
// The slot is EXT_SLOT_BYTES of SDRAM that the image never clears. The loader writes text, data and bss into the first
// EXT_SLOT_BYTES - EXT_NOINIT_BYTES and never touches the tail: the module keeps its reset surviving records there (fault,
// trace), they live as long as the SDRAM keeps its contents across a reset.

#include <stdarg.h>
#include <stdint.h>

#define EXT_ABI_VERSION 1u
#define EXT_PLUG_KIND 6u                       // PLUG_KIND_EXT
#define EXT_SLOT_BYTES (48u * 1024u)
#define EXT_NOINIT_BYTES 2048u                 // the slot tail: never written by the loader
#define EXT_LOAD_BYTES (EXT_SLOT_BYTES - EXT_NOINIT_BYTES)
// Release modules (extras.plg): a second SDRAM region of their own, never shared with the console's slot above, so the release modules and the
// develop console are loaded at the same time. One bundled file /Plugins/Ext/extras.plg per image, linked at the region (image symbol s_extRel).
// Loaded whole on the first use of any tenant, resident until power off. No reset surviving tail: the loader may write the whole region.
// The tenants sit back to back, each at a EXT_SUB_ALIGN boundary (synth-open/modit/extras.ld). The linker places them from their real sizes and
// pack_ext.py writes the resulting sub-slot table into the file (ext_rel_table_t, right after the manifest, inside header_size). The loader takes
// the offsets from that table, checks it against the file and the region, and refuses a file without it (round 1 files: fixed sub-slots, no table).
#define EXT_REL_BYTES (40u * 1024u)
#define EXT_REL_TABLE_MAGIC 0x54524558u        // "EXRT"
#define EXT_REL_TABLE_VERSION 2u               // 1 was round 1's fixed sub-slots (no table in the file)
#define EXT_SUB_ALIGN 1024u
#define EXT_SUB_MODIT 0u                       // the bundle entry and MOD import, IT import, IT export (ext_hooks.modit)
#define EXT_SUB_THEME 1u                       // the custom theme JSON parser (ext_hooks.theme)
#define EXT_SUB_SELFTEST 2u                    // the factory self test (ext_hooks.selftest)
#define EXT_SUB_TAKEFILE 3u                    // the Capture take file writer and reader (ext_hooks.capture ops CAPTURE_OP_FLUSHNOW, CAPTURE_OP_FILE + n)
#define EXT_SUB_CAPTURE 4u                     // the Capture take logic (ext_hooks.capture), its bss is the bss of the file
#define EXT_SUB_COUNT 5u
// A sub-slot: [at, at + bytes) of the region, at a multiple of EXT_SUB_ALIGN, bytes rounded up to it (the next tenant's start, the last one's
// text, data and bss). used_bytes is the end of the last one, never past region_bytes (this image's EXT_REL_BYTES).
typedef struct
{
    uint32_t at;
    uint32_t bytes;
} ext_sub_t;
typedef struct
{
    uint32_t magic;          // EXT_REL_TABLE_MAGIC
    uint16_t version;        // EXT_REL_TABLE_VERSION
    uint16_t count;          // EXT_SUB_COUNT
    uint32_t region_bytes;   // the region the file was packed for (EXT_REL_BYTES of its image)
    uint32_t used_bytes;
    ext_sub_t sub[EXT_SUB_COUNT];
} ext_rel_table_t;           // 56 B
// A fault record in the tail (the module's fault handler writes it, magic last): EXT_FAULT_MAGIC, pc, lr, xpsr, cfsr,
// ~(pc ^ EXT_FAULT_MAGIC). The image reads it on the boot that skips a crashed module and writes /Plugins/Ext/crash.txt.
#define EXT_FAULT_MAGIC 0xFA17C0DEu
#define EXT_FAULT_WORDS 6u

#ifdef __cplusplus
extern "C" {
#endif
// Filled by the module's entry. Every pointer may stay null. The image calls them from:
typedef struct
{
    uint32_t struct_size;                           // sizeof(ext_hooks_t) of the image
    void (*update)(void);                           // main loop, end of TrackerImpl::update
    void (*usbSof)(void);                           // USB interrupt, every start of frame once configured
    void (*usbLineCoding)(uint32_t baud);           // USB interrupt, CDC SET_LINE_CODING
    int (*spinExpired)(uint32_t site);              // a bounded FT812 wait gave up: 1 = stop waiting
    void (*coprocReset)(void);                      // after spinExpired: the FT812 co-processor reset
    void (*log)(const char* msg);                   // LOGE / LOG text (any context, the audio interrupt included)
    void (*logv)(const char* format, va_list args);
    uint32_t (*modit)(uint32_t op);                 // MOD/IT import and IT export (extras.plg): EXT_MODIT_* below, ~0 = no such call
    uint32_t (*capture)(uint32_t op);               // Capture take logic (extras.plg): CAPTURE_OP_* of core/source/mtCapture.h, main loop only
    uint32_t (*theme)(const char* json, uint32_t len, void* theme);   // custom theme JSON into a Theme (extras.plg): 1 = parsed, main loop
    uint32_t (*selftest)(uint32_t op, void* arg);   // the factory self test (extras.plg): EXT_SELFTEST_* below, main loop
} ext_hooks_t;

// The factory self test (EXT_SUB_SELFTEST): the image keeps the pad combination, the module the whole test (core/source/mtTestGlue.cpp)
#define EXT_SELFTEST_SETUP 0u   // arg: the Platform*, the module takes its codec, radio, buttons, encoder, timer, battery and card
#define EXT_SELFTEST_START 1u   // arg: the InputProcessor*, the test takes the inputs over (cTest::runTestingProcedure)
#define EXT_SELFTEST_LOOP 2u    // every main loop pass (cTest::testLoop), arg null

// MOD/IT is the first tenant of extras.plg (EXT_SUB_MODIT): loaded on the first MOD or IT use into the release region, the bundle fills
// the modit hook (a tail field: struct_size tells a module whether this image has it). The image merges a module's hooks field by field,
// so the console module and extras.plg keep each other's pointers. The file manager forwards four calls, see
// core/FileManager/fileManagerModit.cpp. A progress reply is the value of the progress getter (a float's bits for the import).
#define EXT_MODIT_UPDATE_IMPORT 0u     // one step of the MOD or IT import (cFileManager::updateImportModFile), 0 = done
#define EXT_MODIT_UPDATE_EXPORT 1u     // one step of the IT export
#define EXT_MODIT_PROGRESS_IMPORT 2u   // the import progress, a float's bits (0 to 100)
#define EXT_MODIT_PROGRESS_EXPORT 3u   // the export progress, 0 to 100
#define EXT_MODIT_NONE 0xFFFFFFFFu

// The entry, at header.entry_offset (Thumb bit added by the image). abi = EXT_ABI_VERSION, develop = 1. 0 = hooks filled. The
// module installs its own SysTick and fault vectors in the RAM vector table (VTOR = _VectorsRam), the image puts vectors 3 to 6
// and 15 back when the entry returns non zero.
typedef int32_t (*ext_entry_fn)(ext_hooks_t* hooks, uint32_t abi, uint32_t develop);
#ifdef __cplusplus
}
#endif

#endif /* EXT_ABI_H_ */
