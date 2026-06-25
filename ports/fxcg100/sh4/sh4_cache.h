#ifndef CGBA_SH4_CACHE_H
#define CGBA_SH4_CACHE_H

#include <stdint.h>

static inline void cgba_sh4_cache_sync(void *start, void *end)
{
  uintptr_t first = (uintptr_t)start & ~(uintptr_t)31;
  uintptr_t last = ((uintptr_t)end + 31) & ~(uintptr_t)31;
  uintptr_t p;

  for (p = first; p < last; p += 32)
    __asm__ volatile ("ocbwb @%0" :: "r"(p) : "memory");

  __asm__ volatile ("synco" ::: "memory");

  for (p = first; p < last; p += 32)
    __asm__ volatile ("icbi @%0" :: "r"(p) : "memory");

  __asm__ volatile ("synco" ::: "memory");
}

#endif
