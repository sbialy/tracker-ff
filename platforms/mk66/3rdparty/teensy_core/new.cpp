/* Teensyduino Core Library
 * http://www.pjrc.com/teensy/
 * Copyright (c) 2017 PJRC.COM, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "new.h"

#if defined(FW_SDRAM_FILE_POOLS) && !defined(FW_NO_NEW_ARENA)
// SYNTH_OPEN builds (platforms/mk66/makefile). Blocks of 1 KB and more come from a 48 KB arena in the SDRAM, the internal heap (about 52 KB
// in all) keeps the small ones. The heap is too small for the engine metadata: a patch holds a vector of up to 84 parameters of 56 B
// (4,704 B in one piece), a part change builds two of them next to the old one, and the free space of the heap is in pieces by then. An
// operator new that returns null there is a bus fault in the first constructor that writes through it (std::vector<Param>, Model rolls on
// the device). When both give nothing the unit stops here, in this function, not somewhere far away.
#include "cr_section_macros.h"
#include "new_arena.h"

static constexpr size_t kArenaBytes = 49152;
static constexpr size_t kBigBytes = 1024;
__NOINIT(EXTERNAL_RAM) __attribute__((aligned(8))) static uint8_t s_newArenaBytes[kArenaBytes];
static NewArena s_newArena;   // trivially constructible: no static constructor touches the SDRAM
static uint32_t s_newBigFallbacks, s_newSmallFallbacks, s_newAllocs, s_newMaxRequest;

// After the SDRAM is up (hardware.cpp). Until then everything comes from the heap.
void operatorNewArenaInit()
{
  s_newArena.init(s_newArenaBytes, kArenaBytes);
}

static inline unsigned irqOff()
{
  unsigned primask;
  __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r" (primask) :: "memory");
  return primask;
}

static inline void irqRestore(unsigned primask)
{
  __asm__ volatile("msr primask, %0" :: "r" (primask) : "memory");
}

static void * arenaAlloc(size_t size)
{
  if (!s_newArena.ready()) {
    return nullptr;
  }
  const unsigned m = irqOff();
  void * p = s_newArena.alloc(size);
  irqRestore(m);
  return p;
}

// Console `arena` command (development console): size, in use, peak, big blocks served, big blocks that found no room (went to the heap), small
// blocks the heap could not give (went to the arena), the largest request, the largest free piece, free bytes
void operatorNewArenaStats(uint32_t * o)
{
  o[0] = kArenaBytes;
  o[1] = s_newArena.usedBytes();
  o[2] = s_newArena.peakBytes();
  o[3] = s_newAllocs;
  o[4] = s_newBigFallbacks;
  o[5] = s_newSmallFallbacks;
  o[6] = s_newMaxRequest;
  o[7] = s_newArena.largestFree();
  o[8] = s_newArena.freeBytes();
}

static void * newBlock(size_t size)
{
  void * p = nullptr;
  s_newAllocs++;
  if (size > s_newMaxRequest) {
    s_newMaxRequest = size;
  }
  if (size >= kBigBytes) {
    p = arenaAlloc(size);
    if (p == nullptr) {
      s_newBigFallbacks++;
    }
  }
  if (p == nullptr) {
    p = malloc(size);
  }
  if (p == nullptr) {
    p = arenaAlloc(size);   // the heap is out: even a small block may use the arena
    if (p != nullptr) {
      s_newSmallFallbacks++;
    }
  }
  if (p == nullptr) {
    __builtin_trap();
  }
  return p;
}

static void deleteBlock(void * ptr)
{
  if (s_newArena.owns(ptr)) {
    const unsigned m = irqOff();
    s_newArena.free(ptr);
    irqRestore(m);
    return;
  }
  free(ptr);
}
#else
#ifdef FW_SDRAM_FILE_POOLS
// FW_NO_NEW_ARENA (an experiment: the release without the arena): the entry points stay, the arena does nothing
void operatorNewArenaInit() {}
void operatorNewArenaStats(uint32_t * o) { for (int i = 0; i < 9; i++) o[i] = 0; }
#endif
static inline void * newBlock(size_t size) { return malloc(size); }
static inline void deleteBlock(void * ptr) { free(ptr); }
#endif

void * operator new(size_t size)
{
  return newBlock(size);
}

void * operator new[](size_t size)
{
  return newBlock(size);
}

void operator delete(void * ptr) noexcept
{
  deleteBlock(ptr);
}

void operator delete[](void * ptr) noexcept
{
  deleteBlock(ptr);
}

void operator delete(void * ptr, size_t size) noexcept
{
  deleteBlock(ptr);
}

void operator delete[](void * ptr, size_t size) noexcept
{
  deleteBlock(ptr);
}

//int __cxa_guard_acquire(__guard *g) {return !*(char *)(g);};
//void __cxa_guard_release (__guard *g) {*(char *)g = 1;};
//void __cxa_guard_abort (__guard *) {}; 

//void __cxa_pure_virtual(void) {};

