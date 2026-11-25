#ifndef EXT_IMX_ABI_H_
#define EXT_IMX_ABI_H_

// The extension module ABI of Tracker+ and Mini (PLATFORM_IMX, ref/ff-imx-port-plan.md R2.1g, P10 H7). The file rules are the
// MK66 ones (synth-open/ext_loader.h: /Plugins/Ext/module.plg, kind EXT, crash marker, status.txt, image id in reserved[0], run
// address in reserved[1]). What differs from the MK66 ext_abi.h:
//   - the module is not linked against the image's symbols (ld --just-symbols): the image hands it a services table in the hooks
//     block, so one module source serves Tracker+, Mini and the plus sim (whose "image" is a Mac binary),
//   - the block is all 32-bit words (guest addresses), the same layout seen from the module, the image and the sim's Unicorn bridge,
//   - the abi word of the entry is EXT_IMX_ABI_VERSION: an MK66 module (EXT_ABI_VERSION) refuses it, an IMX module refuses the MK66 one,
//   - one more hook, command: a console line in, the reply text out (the console without the USB transport, plan R2.1g).
// The MK66 image does not include this header.

#include <stdint.h>

#define EXT_IMX_ABI_VERSION 0x494D0001u   // 'IM' << 16 | 1

#ifdef __cplusplus
extern "C" {
#endif
typedef struct
{
    uint32_t struct_size;      // sizeof(ext_imx_hooks_t), set by the image
    // services, set by the image before the entry (addresses of functions of the image)
    uint32_t svc_log;          // void log(const char* text)
    uint32_t svc_millis;       // uint32_t millis(void)
    uint32_t svc_exists;       // int exists(const char* path): 1 when the card has it
    uint32_t svc_write_text;   // int writeText(const char* path, const char* text, int n): 1 when written
    uint32_t svc_build_id;     // const char* buildId(void)
    // hooks, zero until the entry fills them (addresses of functions of the module)
    uint32_t update;           // void update(void): main loop, end of TrackerImpl::update
    uint32_t command;          // int32_t command(const char* line, char* out, uint32_t cap): reply length, -1 for an unknown verb
    uint32_t log;              // void log(const char* msg): LOGE text
} ext_imx_hooks_t;

// int32_t ext_entry(ext_imx_hooks_t* hooks, uint32_t abi, uint32_t develop): 0 when it filled the hooks
#ifdef __cplusplus
}
#endif

#endif /* EXT_IMX_ABI_H_ */
