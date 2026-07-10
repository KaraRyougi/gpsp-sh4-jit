#ifndef CGBA_SH4_SWI_OAM_H
#define CGBA_SH4_SWI_OAM_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Exact bulk form of sequential, aligned write_memory16() calls to GBA OAM.
 *
 * gpSP stores guest memory as little-endian bytes on every host.  A normal
 * OAM halfword write stores eswap16(value) through a native u16, which leaves
 * exactly the source's two guest bytes in memory on both little- and
 * big-endian hosts.  Copying bytes therefore avoids both per-element swaps
 * and the generic memory-region dispatcher.
 *
 * This helper deliberately declines cases whose generic behavior is not a
 * pure OAM byte copy:
 *   - odd or non-region-7 destinations;
 *   - a sequence that crosses from 0x07ffffff into region 8;
 *   - a source that aliases OAM (CpuSet copies forward element-by-element,
 *     so a bulk memcpy would not reproduce overlap feedback).
 *
 * OAM's 1 KiB mirror is honored for every element, including multiple wraps.
 * The caller continues to own GBA cycle charging, CpuSet registers, and store
 * alerts.  OAM has no SMC tag mirror and write_memory16() returns no alert for
 * it, so this helper intentionally touches only OAM and OAM_UPDATED.
 */
static inline int cgba_sh4_swi_copy_u16_to_oam(
    uint8_t *oam_le, const uint8_t *src_le, uint32_t dest, uint32_t count,
    void *oam_updated)
{
  const uint32_t oam_region_end = UINT32_C(0x08000000);
  const size_t oam_bytes = 0x400u;
  size_t bytes;
  uintptr_t op, sp;
  size_t off;
  uint32_t updated_value = 1;

  if (!oam_le || !src_le || !oam_updated || !count)
    return 0;
  if ((dest >> 24) != 0x07u || (dest & 1u))
    return 0;
  if (count > (oam_region_end - dest) / 2u)
    return 0;                         /* a later write is outside OAM region */

  bytes = (size_t)count * 2u;

  /* The current parked engine's source resolver can only return gamepak,
   * EWRAM, IWRAM, or VRAM, so this is normally free.  Keep the check here so
   * a future OAM source resolver cannot silently change forward-overlap
   * semantics.  Difference comparisons avoid pointer-end overflow. */
  op = (uintptr_t)oam_le;
  sp = (uintptr_t)src_le;
  if (sp <= op ? op - sp < bytes : sp - op < oam_bytes)
    return 0;

  /* gpSP's u32 is unsigned int on SH while stdint's uint32_t is unsigned
   * long.  Copy the native 32-bit value through bytes so either typedef can
   * be passed without an incompatible-pointer diagnostic or aliasing UB. */
  memcpy(oam_updated, &updated_value, sizeof(updated_value));
  off = (size_t)(dest & 0x3ffu);
  while (bytes) {
    size_t n = oam_bytes - off;
    if (n > bytes)
      n = bytes;
    memcpy(oam_le + off, src_le, n);
    src_le += n;
    bytes -= n;
    off = 0;
  }
  return 1;
}

#endif /* CGBA_SH4_SWI_OAM_H */
