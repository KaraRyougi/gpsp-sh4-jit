#ifndef CGBA_SH4_SWI_OVERLAP_H
#define CGBA_SH4_SWI_OVERLAP_H

#include <stdint.h>
#include <string.h>

/* Copy in the same increasing-address element order as the bundled open BIOS
 * CpuSet/CpuFastSet loops.  Unlike memmove(), this deliberately lets an
 * earlier destination write feed a later source read (pattern expansion).
 * Callers guarantee width is 2 or 4 and both pointers are width-aligned. */
static inline void cgba_sh4_swi_copy_forward(void *dst_ptr,
                                              const void *src_ptr,
                                              uint32_t len,
                                              uint32_t width)
{
  uint8_t *dst = (uint8_t *)dst_ptr;
  const uint8_t *src = (const uint8_t *)src_ptr;
  uint32_t i;

  /* Callers have already applied the BIOS alignment masks.  Use typed aligned
   * accesses, as gpSP's address16/address32 macros do, so SH4 gets one MOV.L or
   * MOV.W load/store per element.  The pointers are deliberately not restrict:
   * each store may feed a later load from the overlapping source range. */
  if (width == 4) {
    uint32_t *dst32 = (uint32_t *)dst;
    const uint32_t *src32 = (const uint32_t *)src;
    for (i = 0; i < len / 4; i++)
      dst32[i] = src32[i];
  } else {
    uint16_t *dst16 = (uint16_t *)dst;
    const uint16_t *src16 = (const uint16_t *)src;
    for (i = 0; i < len / 2; i++)
      dst16[i] = src16[i];
  }
}

/* True only for the increasing-address overlap supported by the opt-in HLE.
 * uintptr_t avoids relational comparisons between unrelated C objects. */
static inline int cgba_sh4_swi_is_forward_overlap(const void *dst_ptr,
                                                   const void *src_ptr,
                                                   uint32_t len)
{
  uintptr_t dst = (uintptr_t)dst_ptr;
  uintptr_t src = (uintptr_t)src_ptr;
  return dst > src && dst - src < (uintptr_t)len;
}

#endif
