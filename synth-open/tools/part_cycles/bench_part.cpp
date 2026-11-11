// Cost of one synth part inside the open shim on an emulated Cortex-M4F (NOT firmware). Real code: open_synth.cpp (OpenSynth,
// OpenInstrument::render, OpenPatch::effective), open_cold.cpp (patch pool in the SDRAM section, as the firmware), the engine named
// by the build (VAP by default), governor and cpu_meter. The bench drives OpenSynth::renderAudioBuffer(main, aux, part) the way
// PolySynth::update does and the host side (run_bench.py) attributes cycles and SDRAM accesses per function and per source line.
// Scenarios (bench_setup): 0 = one voice held, 1 = no voice held but the part kept alive (voices allowed, patch loaded, gate off and
// the envelope released is not reachable: use 0 with BENCH_VOICES), 2 = N voices held (BENCH_VOICES notes), live clock off.
// BENCH_LIVE=1 gives the shim a cycle counter like the device (reportBlockStart), so renderStarts and learn() run.
#include "open_load.h"
#include "open_patch.h"
#include "PolySynth/Instrument.h"
#include "PolySynth/MidiSource.h"
#include "PolySynth/PatchStore.h"
#include "PolySynth/Synth.h"
#include "PolySynth/sdk/InstrumentTypeRegistry.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <new>
#include <cstdlib>


// The firmware's operator new (platforms/mk66/3rdparty/teensy_core/new.cpp, FW_SDRAM_FILE_POOLS): blocks of 1 KB and more come from an
// arena in the SDRAM, the rest from the heap. OpenInstrument (voices, params, scratch) is far over 1 KB, so it lives in the SDRAM.
// BENCH_ARENA=0 puts everything on the heap instead (the old, wrong model of the device).
#ifndef BENCH_ARENA
#define BENCH_ARENA 1
#endif
__attribute__((section(".sdram"), aligned(8))) static uint8_t gArena[1 << 20];
static uint32_t gArenaUsed;
uint32_t gBigLog[16];
static int gBigN;
static void* bigAlloc(size_t n)
{
    if (BENCH_ARENA && n >= 1024 && gArenaUsed + n + 16 <= sizeof(gArena))
    {
        uint8_t* p = gArena + gArenaUsed;
        gArenaUsed = (gArenaUsed + uint32_t(n) + 15u) & ~7u;
        if (gBigN < 16)
            gBigLog[gBigN++] = uint32_t(n);
        return p;
    }
    return malloc(n);
}
static void bigFree(void* p)
{
    if (p && !(static_cast<uint8_t*>(p) >= gArena && static_cast<uint8_t*>(p) < gArena + sizeof(gArena)))
        free(p);
}
void* operator new(size_t n) { return bigAlloc(n); }
void* operator new[](size_t n) { return bigAlloc(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return bigAlloc(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return bigAlloc(n); }
void operator delete(void* p) noexcept { bigFree(p); }
void operator delete[](void* p) noexcept { bigFree(p); }
void operator delete(void* p, size_t) noexcept { bigFree(p); }
void operator delete[](void* p, size_t) noexcept { bigFree(p); }

void disableAudioInterrupt() {}
void enableAudioInterrupt() {}
extern "C"
{
    void* __dso_handle;
    void _exit(int) { for (;;) {} }
    int _getpid() { return 1; }
    int _kill(int, int) { return -1; }
    void* _sbrk(int n)
    {
        static char heap[1 << 20] __attribute__((aligned(8)));
        static unsigned used;
        if (used + n > sizeof(heap))
            return (void*)-1;
        void* p = heap + used;
        used += n;
        return p;
    }
    extern void (*__init_array_start[])(void);
    extern void (*__init_array_end[])(void);
}

namespace polysynth
{
void InstrumentTypeRegistry::registerInstrumentTypes(InstrumentTypeRegistry& r, const Synth::Allocator&)
{
    open::registerEngines(r);
}
} // namespace polysynth

using namespace polysynth;

namespace
{
struct Src : MidiSource
{
    Src() : MidiSource(Type::kInternalKeyboard) {}
    std::weak_ptr<MidiEventCallback> cb;
    void subscribeMidiEvents(int, std::weak_ptr<MidiEventCallback> c) override { cb = c; }
    void send(MidiEvent::Type t, int ch, int a, int b)
    {
        MidiEvent e{};
        e.type = t;
        e.channel = ch;
        e.data.rawData.value1 = a;
        e.data.rawData.value2 = b;
        (*cb.lock())(e, 0);
    }
};

std::shared_ptr<Synth> gSynth;
std::shared_ptr<Instrument> gInst;
Src gSrc;
std::array<float, 128> gL, gR, gA1, gA2;
uint32_t gFakeNow;
uint32_t gSink;
extern "C" uint32_t gHash; uint32_t gHash;   // rotate-xor of every output sample's bits, bench_setup warm-up and bench_run
static void hashBlock() { for (float v : gL) { uint32_t b; std::memcpy(&b, &v, 4); gHash = ((gHash << 5) | (gHash >> 27)) ^ b; } }
int gPart;

uint32_t fakeClock() { gFakeNow += 4; return gFakeNow; }
} // namespace

extern "C" void bench_setup(int sc)
{
    static bool once;
    if (!once)
    {
        once = true;
        for (auto f = __init_array_start; f < __init_array_end; f++)
            (*f)();
    }
    gSynth = Synth::createSynth([](size_t) { return nullptr; });
    auto store = gSynth->createPatchStore(Synth::FileSystemId::kPatches, true);
    auto patch = std::shared_ptr<Patch>(store->createPatch(open::firstEngineTypeId()));
    gInst = gSynth->createInstrument(0);
    gInst->assignPatch(patch);
    gInst->connect(gSrc);
    gInst->setVoiceCount(sc == 0 ? 0 : (BENCH_VOICES));
    gInst->setMidiChannel(0);
    std::shared_ptr<Instrument> arr[1] = { gInst };
    gSynth->setup(std::span<std::shared_ptr<Instrument>>(arr, 1), {}, nullptr, [] { return tiny::optional<float>(120.0f); });
#if BENCH_LIVE
    open::reportBlockStart(0, fakeClock);
#endif
    std::array<AudioBufferRef, 2> aux{ AudioBufferRef(gA1), AudioBufferRef(gA2) };
    static const int notes[4] = { 57, 61, 64, 68 };
    if (sc != 0)
    {
        for (int i = 0; i < BENCH_VOICES; i++)
            gSrc.send(MidiEvent::Type::kNoteOn, 0, notes[i & 3], 100);
    }
    for (int b = 0; b < 40; b++) // warm up: the voices are past their note-on block, the params settled
    {
#if BENCH_LIVE
        open::reportBlockStart(gFakeNow, fakeClock);
#endif
        gSynth->renderAudioBuffer({ AudioBufferRef(gL), AudioBufferRef(gR) }, std::span<AudioBufferRef>(aux), 0);
        hashBlock();
#if BENCH_LIVE
        open::reportBlockCycles(300000);
#endif
    }
}

extern "C" void bench_run(int blocks)
{
    std::array<AudioBufferRef, 2> aux{ AudioBufferRef(gA1), AudioBufferRef(gA2) };
    for (int b = 0; b < blocks; b++)
    {
#if BENCH_LIVE
        open::reportBlockStart(gFakeNow, fakeClock);
#endif
        gSynth->renderAudioBuffer({ AudioBufferRef(gL), AudioBufferRef(gR) }, std::span<AudioBufferRef>(aux), 0);
        hashBlock();
#if BENCH_LIVE
        open::reportBlockCycles(300000);
#endif
        gSink += uint32_t(gL[0] * 1000.0f);
    }
}
