#include "ptfx_loader.h"
#include "mtEffectModular.h"
#include "Platform.h"
#include "TrackerImpl.h"
#include "mtAudioEngine.h"
#include "mtStructs.h"

#include <cmath>
#include <cstring>
#include <cstdlib>

extern uint8_t externalRamScratchBuff[];

static void hostSetExtraSamples(uint32_t prependSamples, uint32_t appendSamples)
{
    sampleEditorModular.setExtraSamples(prependSamples, appendSamples);
}

// Host API Function Pointer Table
static const PtfxHostApi s_ptfxHostApi = {
    .sample_rate       = 44100,
    .sinf              = ::sinf,
    .cosf              = ::cosf,
    .tanf              = ::tanf,
    .tanhf             = ::tanhf,
    .powf              = ::powf,
    .expf              = ::expf,
    .sqrtf             = ::sqrtf,
    .logf              = ::logf,
    .log10f            = ::log10f,
    .fminf             = ::fminf,
    .fmaxf             = ::fmaxf,
    .fabsf             = ::fabsf,
    .memcpy            = ::memcpy,
    .memset            = ::memset,
    .rand              = ::rand,
    .set_extra_samples = hostSetExtraSamples,
};

static FileSystem* getFileSystem()
{
    TrackerImpl* tracker = TrackerImpl::getInstance();
    if (!tracker)
        return nullptr;
    Platform* platform = tracker->getPlatform();
    if (!platform)
        return nullptr;
    return platform->getFileSystem();
}

uint8_t* PtfxLoader::getCodeBuffer()
{
    // First 128 KB (IMX) / 32 KB (MK66) of externalRamScratchBuff
    return externalRamScratchBuff;
}

uint32_t PtfxLoader::getMaxCodeSize()
{
#ifdef PLATFORM_IMX
    return kPtfxCodeBufferSize; // 128 KB
#else
    return 32 * 1024; // 32 KB
#endif
}

void* PtfxLoader::getScratchBuffer()
{
    // 640 KB of externalRamScratchBuff ([128 KB .. 768 KB], preserving code buffer)
    return reinterpret_cast<void*>(externalRamScratchBuff + kPtfxScratchOffset);
}

uint32_t PtfxLoader::getMaxScratchSize()
{
    return kPtfxScratchTopOffset - kPtfxScratchOffset; // 655,360 bytes (640 KB)
}

const PtfxHostApi* PtfxLoader::getHostApi()
{
    return &s_ptfxHostApi;
}

#ifdef PLATFORM_IMX
static void cleanInvalidateDCache(void* addr, uint32_t size)
{
    // SCB_BASE (0xE000ED00) + 0x270 = 0xE000EF70 (SCB->DCCIMVAC)
    volatile uint32_t* const scb_dccimvac = reinterpret_cast<volatile uint32_t*>(0xE000EF70);
    uintptr_t p   = reinterpret_cast<uintptr_t>(addr) & ~static_cast<uintptr_t>(0x1F);
    uintptr_t end = reinterpret_cast<uintptr_t>(addr) + size;

    asm volatile("dsb 0xf" ::: "memory");
    while (p < end)
    {
        *scb_dccimvac = static_cast<uint32_t>(p);
        p += 32;
    }
    asm volatile("dsb 0xf" ::: "memory");
    asm volatile("isb 0xf" ::: "memory");
}
#endif

void PtfxLoader::syncInstructionCache([[maybe_unused]] void* addr, [[maybe_unused]] uint32_t size)
{
    asm volatile("dsb 0xf" ::: "memory");
    asm volatile("isb 0xf" ::: "memory");

#ifdef PLATFORM_IMX
    // Clean and Invalidate D-Cache to Point of Coherency (SDRAM) and invalidate entire I-Cache on Cortex-M7
    // SCB_BASE (0xE000ED00) + 0x270 = 0xE000EF70 (SCB->DCCIMVAC)
    // SCB_BASE (0xE000ED00) + 0x250 = 0xE000EF50 (SCB->ICIALLU)
    volatile uint32_t* const scb_dccimvac = reinterpret_cast<volatile uint32_t*>(0xE000EF70);
    volatile uint32_t* const scb_iciallu  = reinterpret_cast<volatile uint32_t*>(0xE000EF50);

    uintptr_t p   = reinterpret_cast<uintptr_t>(addr) & ~static_cast<uintptr_t>(0x1F);
    uintptr_t end = reinterpret_cast<uintptr_t>(addr) + size;

    while (p < end)
    {
        *scb_dccimvac = static_cast<uint32_t>(p);
        p += 32;
    }

    asm volatile("dsb 0xf" ::: "memory");
    *scb_iciallu = 0; // Invalidate all instruction cache
    asm volatile("dsb 0xf" ::: "memory");
    asm volatile("isb 0xf" ::: "memory");
#else
    // Disable NXP System MPU on MK66 to allow instruction execution from SRAM/SDRAM
    volatile uint32_t* const mpu_cesr = reinterpret_cast<volatile uint32_t*>(0x4000D000);
    *mpu_cesr = 0;
    // Push and invalidate Kinetis LMEM Processor Code Bus Cache
    volatile uint32_t* const lmem_pcccr = reinterpret_cast<volatile uint32_t*>(0xE0082000);
    *lmem_pcccr = 0x8F000003;
    asm volatile("dsb 0xf" ::: "memory");
    asm volatile("isb 0xf" ::: "memory");
#endif
}

__attribute__((noinline)) bool PtfxLoader::readHeader(const char* filepath, PtfxHeader& outHeader)
{
    FileSystem* fs = getFileSystem();
    if (!filepath || !fs)
        return false;

    auto [isOpen, fileHandle] = fs->openFile(filepath, FileSystem::FileOpenMode::kRead);
    if (!isOpen)
        return false;

    alignas(32) PtfxHeader tempHeader;
    int32_t bytesRead = fs->readFile(&tempHeader, sizeof(PtfxHeader), fileHandle);
    fs->closeFile(fileHandle);

    if (bytesRead != static_cast<int32_t>(sizeof(PtfxHeader)))
        return false;

    if (tempHeader.magic != PTFX_MAGIC || tempHeader.format_version != 1)
        return false;

    memcpy(&outHeader, &tempHeader, sizeof(PtfxHeader));
    return true;
}

bool PtfxLoader::loadPlugin(const char* filepath,
                           PtfxHeader& outHeader,
                           PtfxInitFunc& outInit,
                           PtfxSetParamFunc& outSetParam,
                           PtfxProcessBlockFunc& outProcessBlock)
{
    outInit         = nullptr;
    outSetParam     = nullptr;
    outProcessBlock = nullptr;

    FileSystem* fs = getFileSystem();
    if (!filepath || !fs)
        return false;

    auto [isOpen, fileHandle] = fs->openFile(filepath, FileSystem::FileOpenMode::kRead);
    if (!isOpen)
        return false;

    // Read PtfxHeader in <= 256-byte chunks (e.g. 256 + 216 = 472 bytes)
    size_t headerBytesRead = 0;
    uint8_t* headerPtr = reinterpret_cast<uint8_t*>(&outHeader);
    while (headerBytesRead < sizeof(PtfxHeader))
    {
        size_t chunk = sizeof(PtfxHeader) - headerBytesRead;
        if (chunk > 256)
            chunk = 256;
        int32_t n = fs->readFile(headerPtr + headerBytesRead, chunk, fileHandle);
        if (n != static_cast<int32_t>(chunk))
        {
            fs->closeFile(fileHandle);
            return false;
        }
        headerBytesRead += static_cast<size_t>(n);
    }

    if (outHeader.magic != PTFX_MAGIC || outHeader.format_version != 1)
    {
        fs->closeFile(fileHandle);
        return false;
    }

#ifndef PLATFORM_IMX
    // MK66 cannot run IMX-only plugins
    if (outHeader.flags & PTFX_FLAG_IMX_ONLY)
    {
        fs->closeFile(fileHandle);
        return false;
    }
#endif

    uint32_t totalCodeSize = outHeader.code_size + outHeader.bss_size;
    if (totalCodeSize > getMaxCodeSize())
    {
        fs->closeFile(fileHandle);
        return false;
    }

    if (!fs->seekFile(outHeader.code_offset, fileHandle))
    {
        fs->closeFile(fileHandle);
        return false;
    }

    uint8_t* codeBuf = getCodeBuffer();
    engine.getSdPlayer()->stop();

#ifdef PLATFORM_IMX
    // Clean and invalidate D-Cache across the executable buffer before loading
    cleanInvalidateDCache(codeBuf, getMaxCodeSize());
#endif

    // Read in <= 256-byte chunks so FatFs buffers sectors in internal DTCM (fp->buf)
    // and copies via CPU rather than performing unaligned DMA directly into SDRAM.
    size_t bytesRead = 0;
    while (bytesRead < outHeader.code_size)
    {
        size_t chunk = outHeader.code_size - bytesRead;
        if (chunk > 256)
            chunk = 256;
        int32_t n = fs->readFile(codeBuf + bytesRead, chunk, fileHandle);
        if (n != static_cast<int32_t>(chunk))
            break;
        bytesRead += static_cast<size_t>(n);
    }
    fs->closeFile(fileHandle);

    if (bytesRead != outHeader.code_size)
        return false;

    // Zero-initialize .bss region immediately after code payload
    if (outHeader.bss_size > 0)
    {
        memset(codeBuf + outHeader.code_size, 0, outHeader.bss_size);
    }

    // Synchronize cache: clean & invalidate D-cache to RAM, invalidate I-cache
    syncInstructionCache(codeBuf, totalCodeSize);

    // Compute ARM Cortex-M Thumb-2 entry points (bit 0 set to 1)
    uintptr_t base = reinterpret_cast<uintptr_t>(codeBuf);
    outInit         = reinterpret_cast<PtfxInitFunc>((base + outHeader.init_offset) | 0x1);
    outSetParam     = reinterpret_cast<PtfxSetParamFunc>((base + outHeader.set_param_offset) | 0x1);
    outProcessBlock = reinterpret_cast<PtfxProcessBlockFunc>((base + outHeader.process_block_offset) | 0x1);

    return true;
}
