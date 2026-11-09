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

#ifdef FW_SDRAM_FILE_POOLS
// SYNTH_OPEN builds (platforms/mk66/makefile). Blocks of 1 KB and more come from a 48 KB arena in the SDRAM, the internal heap (about 52 KB
// in all) keeps the small ones. The heap is too small for the engine metadata: a patch holds a vector of up to 84 parameters of 56 B
// (4,704 B in one piece), a part change builds two of them next to the old one, and the free space of the heap is in pieces by then. An
// operator new that returns null there is a bus fault in the first constructor that writes through it (std::vector<Param>, Model rolls on
// the device, ref/model-roll-fault-1.2.0.md). When both give nothing the unit stops here, in this function, not somewhere far away.
#include "cr_section_macros.h"
#include "new_arena.h"

static constexpr size_t kArenaBytes = 49152;
static constexpr size_t kBigBytes = 1024;
__NOINIT(EXTERNAL_RAM) __attribute__((aligned(8))) static uint8_t s_newArenaBytes[kArenaBytes];
static NewArena s_newArena;   // trivially constructible: no static constructor touches the SDRAM

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

static void * newBlock(size_t size)
{
  void * p = nullptr;
  if (size >= kBigBytes) {
    p = arenaAlloc(size);
  }
  if (p == nullptr) {
    p = malloc(size);
  }
  if (p == nullptr) {
    p = arenaAlloc(size);   // the heap is out: even a small block may use the arena
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

