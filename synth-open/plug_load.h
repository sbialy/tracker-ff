// The plugin code loader of the IMX host (Tracker+ / Mini, ref/ff-imx-port-plan.md R2.1b, P5 H1): a .plg read from the card is
// copied into an SDRAM code slot, rebased, its bss zeroed, then the D-cache is cleaned over the image and bss, the I-cache is
// invalidated and DSB + ISB run before the entry is called. No FlexNVM, no bank: the IMX host loads from the card on every pick.
// Header only and free of the platform (as plug_index.h): synth-open/tools/plug_load_test.cpp runs the same code on the Mac
// against real .plg files, the IMX host passes Ops with DCACHE_CleanByRange, SCB_InvalidateICache, __DSB and __ISB
// (platforms/imx/drivers/fsl_cache.h). The MK66 host does not use it (its inserts run from SRAM_L, its engines from FlexNVM).
#pragma once

#include "plug_index.h"

namespace plug
{

// SDRAM code slots of the IMX host (R2.1b, R3: ITCM 9,304 B and OCRAM 9,556 B free, both short of the insert slot, so every
// slot is SDRAM). A synth slot holds the largest engine image plus bss (ALCO 31,940 + 1,276 = 33,216 B, CHIP 32,744 B), one per synth part that plays
// a plugin engine. The insert slot is the MK66 SRAM_L slot size (plug_index.h kInsertSlot rules). P6 and P7 own the buffers.
constexpr uint32_t kImxSynthSlotBytes = 36864;   // 36 KiB: ALCO image + bss 33,216 B (P7 option A, droppable)
constexpr int kImxSynthSlots = 3;
constexpr uint32_t kImxInsertSlotBytes = 9728;
constexpr uint32_t kImxCodeSlotsBytes = kImxSynthSlots * kImxSynthSlotBytes + kImxInsertSlotBytes;   // 120,320 B

// Loads one .plg held whole in file[0..fileLen) into slot[0..slotLen), a single piece: text, rw, bss. slotAddr is the address the
// code runs at (the slot's own address on the target, a guest address in a host test). On kOk entryAddr is the Thumb address of
// plug_entry. Ops:
//   void cleanDCache(const void* p, uint32_t n)    write the copied and rebased image and the zeroed bss back to the SDRAM
//   void invalidateICache()                        drop stale code lines of an earlier plugin in the same slot
//   void barrier()                                 DSB then ISB
// The caller stops the audio calls into the slot before a load (an unload never runs beside a render, as on the MK66).
template <class Ops>
Err loadImage(const uint8_t* file, uint32_t fileLen, uint32_t wantKind, uint8_t* slot, uint32_t slotLen, uint32_t slotAddr,
              uint32_t& entryAddr, Ops& ops)
{
    entryAddr = 0;
    plug_file_header h;
    if (fileLen < sizeof(h)) return kHeader;
    memcpy(&h, file, sizeof(h));
    Err err = checkHeader(h, wantKind);
    if (err != kOk) return err;
    err = checkLayout(h, slotLen, 0);
    if (err != kOk) return err;
    if ((slotAddr & 7u) || h.header_size < sizeof(h)) return kTooBig;
    const uint32_t tableEnd = h.header_size + h.image_size + 4u * h.rebase_count;   // every term bound by checkHeader
    if (fileLen < tableEnd) return kRead;
    // the CRC covers the manifest, the image and the rebase table (not the first 64 bytes, not a logo trailer)
    if (crc32(0, file + sizeof(h), tableEnd - sizeof(h)) != h.crc32) return kCrc;
    const uint8_t* image = file + h.header_size;
    const uint8_t* table = image + h.image_size;
    memcpy(slot, image, h.image_size);
    memset(slot + h.image_size, 0, h.bss_size);
    for (uint32_t k = 0; k < h.rebase_count; k++)
    {
        uint32_t off, v;
        memcpy(&off, table + 4 * k, 4);
        if (!rebaseOffsetOk(off, h.image_size)) return kRebase;
        memcpy(&v, slot + off, 4);
        if (v >= h.image_size + h.bss_size) return kRebase;
        v += slotAddr;   // one piece: rw follows the text at the same offset it was linked at
        memcpy(slot + off, &v, 4);
    }
    ops.cleanDCache(slot, h.image_size + h.bss_size);
    ops.invalidateICache();
    ops.barrier();
    entryAddr = (slotAddr + h.entry_offset) | 1u;
    return kOk;
}

}  // namespace plug
