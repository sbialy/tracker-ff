// ui_shot BRIDGE=1 only (emu parity plan T5): plug_synth.cpp start() for an engine in the bridge's FlexNVM window. See host_synthcheck.cpp.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ucbs
{
// One installed engine as plug_synth's start() sees it: its bank entry (guest addresses) and its library entry
struct StartIn
{
    uint32_t text;           // kBankRun + e.off: the engine's text in the FlexNVM window
    uint32_t textLen;        // e.text
    uint32_t len;            // e.len: text + rw initial bytes in the flash
    uint32_t entry;          // e.entry: plug_entry offset in the text
    uint32_t rwRun, rwLen;   // where its rw + bss live (s_pool, SRAM_U)
    int32_t stored, patch, type;   // s_sidx[k].synthStored, synthPatch, synthType (the manifest)
    const char* name;        // s_lib[k].name
    size_t nameCap;          // sizeof(s_lib[k].name)
};
struct StartOut
{
    uint32_t ext = 0;        // the guest plug_ext_synth (0 unless start gave 0)
    bool chordSet = false;   // start wrote chord, mode and strum (it does once the extension passed checkSynth)
    int8_t chord = -1, mode = -1, strum = -1;
    bool fault = false;      // a guest call faulted (or ran out of ucb::setCallTimeout): the unit would hard fault
};
// plug_synth.cpp start() step for step with the image's own code for every call (plug_entry, plug::checkDesc, init, query) and the
// two checks the image inlines into start() (checkSynth, checkChord, with chordStrum) on guest reads. 0 or start()'s codes:
// 1 entry refused the host, 2 descriptor, 3 init, 4 extension, 8 the extension's numbers or name are not the manifest's.
// A guest fault stops it with code 1 and out.fault (see the overlay).
int start(const StartIn& in, StartOut& out);

// Tests only (host_synthcheck_test.cc): called right after every query call of start() with the id and the answer
void setQueryHook(void (*h)(uint32_t id, uint32_t answer));

// The ports alone, on guest addresses (the device's plug_ext_synth, plug_synth_param, ... layouts): the same verdicts as the image's
bool checkSynth(uint32_t x, uint32_t t, uint32_t tn, uint32_t rw, uint32_t rwn);
bool checkChord(uint32_t c, uint32_t x, uint32_t t, uint32_t tn, uint32_t rw, uint32_t rwn);
int8_t chordStrum(uint32_t c, uint32_t x);
}   // namespace ucbs
