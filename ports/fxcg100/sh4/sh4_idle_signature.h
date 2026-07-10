#ifndef CGBA_SH4_IDLE_SIGNATURE_H
#define CGBA_SH4_IDLE_SIGNATURE_H

#include <stdint.h>

#define CGBA_SH4_AZLE_IDLE_PC 0x080683A8u
#define CGBA_SH4_AZLE_IDLE_HALFWORDS 5u
#define CGBA_SH4_AZLE_IDLE_BYTES (CGBA_SH4_AZLE_IDLE_HALFWORDS * 2u)

/* Zelda: A Link to the Past/Four Swords (AZLE, revision 0) waits for bit 0
 * of its VBlank-updated RAM flag with this exact Thumb loop.  Keep the
 * database hint tied to the verified code, since revisions and ROM hacks can
 * retain the same four-byte game code while changing the instruction stream. */
static inline int cgba_sh4_azle_idle_signature_match(uint16_t w0,
                                                      uint16_t w1,
                                                      uint16_t w2,
                                                      uint16_t w3,
                                                      uint16_t w4)
{
  return w0 == 0x8811u &&                    /* ldrh r1, [r2] */
         w1 == 0x1C18u &&                    /* mov  r0, r3 */
         w2 == 0x4008u &&                    /* and  r0, r1 */
         w3 == 0x2800u &&                    /* cmp  r0, #0 */
         w4 == 0xD0FAu;                      /* beq  0x080683A8 */
}

#endif
