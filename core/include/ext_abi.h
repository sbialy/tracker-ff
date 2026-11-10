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
} ext_hooks_t;

// The entry, at header.entry_offset (Thumb bit added by the image). abi = EXT_ABI_VERSION, develop = 1. 0 = hooks filled. The
// module installs its own SysTick and fault vectors in the RAM vector table (VTOR = _VectorsRam), the image puts vectors 3 to 6
// and 15 back when the entry returns non zero.
typedef int32_t (*ext_entry_fn)(ext_hooks_t* hooks, uint32_t abi, uint32_t develop);
#ifdef __cplusplus
}
#endif

#endif /* EXT_ABI_H_ */
