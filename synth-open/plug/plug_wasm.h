// Tracker FF plugin modules for the browser editor: the .wasm twin of a .plg. MIT licence, see synth-open/plug/README.md.
//
// A plugin ships as a device file (.plg, ARM code, plug_abi.h) and a browser file (.wasm) built from the same sources. The
// .wasm keeps the plugin ABI of plug_abi.h unchanged: the same plug_entry(), plug_desc, plug_param, plug_host and, for a
// synth engine, plug_ext_synth. wasm32 has 32-bit pointers like the Cortex-M4, so every struct has the same layout. A
// function pointer is an index into the host's function table.
//
// File format: a position independent WebAssembly module with a "dylink.0" custom section (what `emcc -sSIDE_MODULE=2` and
// `wasm-ld --shared` write). The host loads it into its own memory:
//   imports, all from "env", nothing else: memory, __indirect_function_table, __memory_base, __table_base, __stack_pointer
//   (the module gets its own stack, the host gives it the top of a block it allocated). No function import: libm and the
//   memory functions come through the host table (plug_rt_wasm.c), as on the device. A module with any other import is refused.
//   exports: plug_entry and plug_wasm_abi, plus the toolchain's __wasm_apply_data_relocs and __wasm_call_ctors when present.
// Load order: check the manifest, instantiate, call plug_wasm_abi() and refuse another major, apply the data relocations,
// run the constructors, call plug_entry(host, PLUG_ABI_MAJOR << 16 | PLUG_ABI_MINOR) and refuse NULL, then check the
// descriptor as for a .plg (kind, struct sizes and for a synth engine plug_ext_synth.layout).
//
// The manifest (<id>.json next to the .wasm) is readable without running any code, like plug_manifest:
//   { "format": "ffmod", "format_version": 1, "id", "kind": "synth" | "master", "name", "version",
//     "abi": "1.1", "wasm_abi": "1.0", "min_host": 1, "licence", "author",
//     "files": { "wasm": { "name", "size", "sha256" }, "plg": { "name", "size", "sha256" } },
//     synth: "synth": { "stored", "patch_engine", "type_id", "params", "sections", "macros", "max_voices", "volume_param" },
//     insert: "params": [ { "name", "unit", "min", "max", "def", "step", "enum": [..] } ] }
// The host refuses a module whose sha256 differs from the manifest, whose abi major or wasm_abi major is not its own or
// whose min_host is above its level.
//
// Bit identical to the editor's linked-in code: build the plugin sources with the editor host's own compiler and flags (see the
// engine repository's wasm build) and give the module the host's own libm through plug_host. wasm has strict IEEE float
// and no implicit fused multiply-add, so the same source gives the same samples. It does NOT sound bit identical to the
// device: the device runs the ARM build, its libm and (for an engine) the half float parameter storage.
#pragma once

#include "plug_abi.h"

#define PLUG_WASM_ABI_MAJOR 1   // module format: imports, exports, load order above. A host refuses any other major
#define PLUG_WASM_ABI_MINOR 0

#ifdef __cplusplus
extern "C" {
#endif

// plug_wasm_abi() of a module: PLUG_WASM_ABI_MAJOR << 16 | PLUG_WASM_ABI_MINOR of its build
#define PLUG_WASM_ABI ((PLUG_WASM_ABI_MAJOR << 16) | PLUG_WASM_ABI_MINOR)

#if defined(__wasm__)
// The two exports. plug_rt_wasm.c defines plug_wasm_abi(), the plugin source its plug_entry() as for a .plg.
#define PLUG_WASM_EXPORT __attribute__((visibility("default"), used))
PLUG_WASM_EXPORT uint32_t plug_wasm_abi(void);
#endif

#ifdef __cplusplus
}
#endif
