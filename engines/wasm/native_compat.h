// The plugin sources compiled for the Mac (dump_desc.cpp): Mach-O has no ELF section names, so the two placement attributes
// of plug_abi.h become plain "used". Nothing else changes, the descriptor and the extension are the plugin's own.
#pragma once
#include "plug_abi.h"
#undef PLUG_DESCRIPTOR
#undef PLUG_ENTRY
#define PLUG_DESCRIPTOR __attribute__((used))
#define PLUG_ENTRY __attribute__((used))
