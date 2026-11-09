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

#include "devhooks.h"
#include <malloc.h>

// Console images note a failed allocation (DEVHOOK_TRACE ring, `trace` command): operator new returns the null of malloc here, and the
// first thing that writes through it is a bus fault far from the cause. tag 0x4F4F4D = OOM, a = size, b = free bytes in the heap's free
// list, c = the stack pointer.
static inline void * newNoteNull(void * p, size_t size)
{
#ifdef ENABLE_DEV_CONSOLE
  unsigned sp;
  __asm__ volatile("mov %0, sp" : "=r" (sp));
  if (p == nullptr) {
    DEVHOOK_TRACE(0x4F4F4D, size, mallinfo().fordblks, sp);
    __builtin_trap();   // DIAGNOSTIC: a null from malloc becomes a fault inside operator new, the record names it
  } else if ((unsigned)p < 0x1FFF0000u || (unsigned)p + size > 0x20030000u) {
    DEVHOOK_TRACE(0x4F4F52, (unsigned)p, size, sp);   // a block outside the RAM
  }
#else
  (void)size;
#endif
  return p;
}

void * operator new(size_t size)
{
  return newNoteNull(malloc(size), size);
}

void * operator new[](size_t size)
{
  return newNoteNull(malloc(size), size);
}

void operator delete(void * ptr) noexcept
{
  free(ptr);
}

void operator delete[](void * ptr) noexcept
{
  free(ptr);
}

void operator delete(void * ptr, size_t size) noexcept
{
  free(ptr);
}

void operator delete[](void * ptr, size_t size) noexcept
{
  free(ptr);
}

//int __cxa_guard_acquire(__guard *g) {return !*(char *)(g);};
//void __cxa_guard_release (__guard *g) {*(char *)g = 1;};
//void __cxa_guard_abort (__guard *) {}; 

//void __cxa_pure_virtual(void) {};

