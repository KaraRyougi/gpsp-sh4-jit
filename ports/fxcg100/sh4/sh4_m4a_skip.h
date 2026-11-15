#ifndef CGBA_SH4_M4A_SKIP_H
#define CGBA_SH4_M4A_SKIP_H

/*
 * Translation-time elision of discarded PCM work in the m4a/MP2K mixer.
 *
 * The calculator port has no sound backend: the direct-sound FIFOs go nowhere,
 * so the only consumer of the driver's PCM mix buffer is the driver itself
 * (reverb feedback).  The game still runs `SoundMainRAM` from IWRAM every
 * frame, and with reverb plus four interpolated channels that routine alone
 * executes ~17k guest instructions per frame.
 *
 * This is NOT a helper that replaces the routine.  The routine keeps running
 * instruction by instruction through the ordinary translator; selected
 * instructions simply emit less host code:
 *
 *   DROP  pure arithmetic whose only purpose is to produce a PCM sample
 *         (MUL/BIC/ADD-ROR accumulation, reverb averaging).  Nothing is
 *         emitted.  The translate-time cycle count still includes it.
 *   MEM   a sample/PCM-buffer load or store.  The transfer is not performed;
 *         base-register writeback and the runtime memory cycle charge (by the
 *         address's region, same table as the real access) are still emitted.
 *
 * Every branch, flag-setting loop counter, budget gate, and every store to
 * the SoundChannel structures (status, count, fw, current) is left alone, so
 * guest control flow, guest cycle accounting, event-slice boundaries, envelope
 * progress and sample-end detection are unchanged by construction.  What
 * changes: the PCM buffer is no longer written by translated code, and the
 * scratch registers below hold stale values while inside the routine.
 *
 * Liveness (audited against the disassembly; `tests/sh4_m4a_skip_test.c`
 * checks every table row decodes to the form this argument relies on):
 *   r0, r1   sample / delta temporaries.  Every live read is preceded by a
 *            kept write (loop-length reload, envelope/ROR setup, ADR + BX).
 *   r6, r7   PCM accumulators.  Only read by dropped arithmetic and by PCM
 *            buffer stores.
 *   ip, lr   interpolation temporaries.  ip is saved/restored around the
 *            interpolating path by the routine's own PUSH/POP {r4, ip}; the
 *            elided writes leave it holding the value POP restores anyway.
 *            lr is rewritten by the kept `LSRS lr, r9, #23` before each use.
 *   flags    the single dropped flag setter (`TST r0, #0x80`, reverb) feeds
 *            only the dropped `ADDNE`; `SUBS r4, r4, #1` rewrites NZCV before
 *            the loop branch reads them.
 *
 * The signature is the exact 227-word image [reverb entry, mix exit) of the
 * Pokemon Emerald (BPEE) build of the driver.  The compressed/reversed sample
 * paths after it are untouched.  Other MP2K builds do not match and translate
 * normally.
 *
 * This header has no target dependencies so the table can be audited on the
 * host.
 */

#include <stdint.h>

#define CGBA_SH4_M4A_SKIP_WORDS      227u
#define CGBA_SH4_M4A_SKIP_BYTES      (CGBA_SH4_M4A_SKIP_WORDS * 4u)
#define CGBA_SH4_M4A_SKIP_HASH       0x5583F4BAu
#define CGBA_SH4_M4A_SKIP_FIRST_WORD 0xE3540002u  /* cmp r4, #2 (reverb entry) */
/* Dispatcher-visible ARM entries (reached by BX from the Thumb half). */
#define CGBA_SH4_M4A_SKIP_ENTRY_REVERB 0x000u
#define CGBA_SH4_M4A_SKIP_ENTRY_MIX    0x190u

enum {
  CGBA_SH4_M4A_SKIP_KEEP = 0,
  CGBA_SH4_M4A_SKIP_DROP = 1,
  CGBA_SH4_M4A_SKIP_MEM  = 2
};

typedef struct cgba_sh4_m4a_skip_row {
  uint16_t offset;      /* byte offset from the reverb entry */
  uint32_t opcode;
  uint8_t  cls;
} cgba_sh4_m4a_skip_row;

static const cgba_sh4_m4a_skip_row cgba_sh4_m4a_skip_rows[] = {
  /* reverb: 4-tap average of both channels, written back to both */
  { 0x010u, 0xE19500D6u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r0,[r5,r6]     */
  { 0x014u, 0xE1D510D0u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r1,[r5]        */
  { 0x018u, 0xE0800001u, CGBA_SH4_M4A_SKIP_DROP }, /* add r0,r0,r1         */
  { 0x01Cu, 0xE19710D6u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r1,[r7,r6]     */
  { 0x020u, 0xE0800001u, CGBA_SH4_M4A_SKIP_DROP },
  { 0x024u, 0xE0D710D1u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r1,[r7],#1     */
  { 0x028u, 0xE0800001u, CGBA_SH4_M4A_SKIP_DROP },
  { 0x02Cu, 0xE0010390u, CGBA_SH4_M4A_SKIP_DROP }, /* mul r1,r0,r3         */
  { 0x030u, 0xE1A004C1u, CGBA_SH4_M4A_SKIP_DROP }, /* asr r0,r1,#9         */
  { 0x034u, 0xE3100080u, CGBA_SH4_M4A_SKIP_DROP }, /* tst r0,#0x80         */
  { 0x038u, 0x12800001u, CGBA_SH4_M4A_SKIP_DROP }, /* addne r0,r0,#1       */
  { 0x03Cu, 0xE7C50006u, CGBA_SH4_M4A_SKIP_MEM  }, /* strb r0,[r5,r6]      */
  { 0x040u, 0xE4C50001u, CGBA_SH4_M4A_SKIP_MEM  }, /* strb r0,[r5],#1      */
  /* fixed-rate mix, four-sample groups */
  { 0x1F4u, 0xE5956000u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldr r6,[r5]          */
  { 0x1F8u, 0xE5957630u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldr r7,[r5,#0x630]   */
  { 0x1FCu, 0xE0D300D1u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r0,[r3],#1     */
  { 0x200u, 0xE001009Au, CGBA_SH4_M4A_SKIP_DROP }, /* mul r1,sl,r0         */
  { 0x204u, 0xE3C118FFu, CGBA_SH4_M4A_SKIP_DROP }, /* bic r1,r1,#0xff0000  */
  { 0x208u, 0xE0816466u, CGBA_SH4_M4A_SKIP_DROP }, /* add r6,r1,r6,ror #8  */
  { 0x20Cu, 0xE001009Bu, CGBA_SH4_M4A_SKIP_DROP }, /* mul r1,fp,r0         */
  { 0x210u, 0xE3C118FFu, CGBA_SH4_M4A_SKIP_DROP },
  { 0x214u, 0xE0817467u, CGBA_SH4_M4A_SKIP_DROP }, /* add r7,r1,r7,ror #8  */
  { 0x220u, 0xE5857630u, CGBA_SH4_M4A_SKIP_MEM  }, /* str r7,[r5,#0x630]   */
  { 0x224u, 0xE4856004u, CGBA_SH4_M4A_SKIP_MEM  }, /* str r6,[r5],#4       */
  /* fixed-rate mix, tail / loop-point groups */
  { 0x238u, 0xE5956000u, CGBA_SH4_M4A_SKIP_MEM  },
  { 0x23Cu, 0xE5957630u, CGBA_SH4_M4A_SKIP_MEM  },
  { 0x240u, 0xE0D300D1u, CGBA_SH4_M4A_SKIP_MEM  },
  { 0x244u, 0xE001009Au, CGBA_SH4_M4A_SKIP_DROP },
  { 0x248u, 0xE3C118FFu, CGBA_SH4_M4A_SKIP_DROP },
  { 0x24Cu, 0xE0816466u, CGBA_SH4_M4A_SKIP_DROP },
  { 0x250u, 0xE001009Bu, CGBA_SH4_M4A_SKIP_DROP },
  { 0x254u, 0xE3C118FFu, CGBA_SH4_M4A_SKIP_DROP },
  { 0x258u, 0xE0817467u, CGBA_SH4_M4A_SKIP_DROP },
  { 0x26Cu, 0xE5857630u, CGBA_SH4_M4A_SKIP_MEM  },
  { 0x270u, 0xE4856004u, CGBA_SH4_M4A_SKIP_MEM  },
  /* interpolating mix */
  { 0x2F4u, 0xE1D300D0u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r0,[r3]        */
  { 0x2F8u, 0xE1F310D1u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r1,[r3,#1]!    */
  { 0x2FCu, 0xE0411000u, CGBA_SH4_M4A_SKIP_DROP }, /* sub r1,r1,r0         */
  { 0x300u, 0xE5956000u, CGBA_SH4_M4A_SKIP_MEM  },
  { 0x304u, 0xE5957630u, CGBA_SH4_M4A_SKIP_MEM  },
  { 0x308u, 0xE00E0199u, CGBA_SH4_M4A_SKIP_DROP }, /* mul lr,r9,r1         */
  { 0x30Cu, 0xE080EBCEu, CGBA_SH4_M4A_SKIP_DROP }, /* add lr,r0,lr,asr #23 */
  { 0x310u, 0xE00C0E9Au, CGBA_SH4_M4A_SKIP_DROP }, /* mul ip,sl,lr         */
  { 0x314u, 0xE3CCC8FFu, CGBA_SH4_M4A_SKIP_DROP }, /* bic ip,ip,#0xff0000  */
  { 0x318u, 0xE08C6466u, CGBA_SH4_M4A_SKIP_DROP }, /* add r6,ip,r6,ror #8  */
  { 0x31Cu, 0xE00C0E9Bu, CGBA_SH4_M4A_SKIP_DROP }, /* mul ip,fp,lr         */
  { 0x320u, 0xE3CCC8FFu, CGBA_SH4_M4A_SKIP_DROP },
  { 0x324u, 0xE08C7467u, CGBA_SH4_M4A_SKIP_DROP }, /* add r7,ip,r7,ror #8  */
  { 0x344u, 0x00800001u, CGBA_SH4_M4A_SKIP_DROP }, /* addeq r0,r0,r1       */
  { 0x348u, 0x11B300DEu, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsbne r0,[r3,lr]!  */
  { 0x34Cu, 0xE1F310D1u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r1,[r3,#1]!    */
  { 0x350u, 0xE0411000u, CGBA_SH4_M4A_SKIP_DROP }, /* sub r1,r1,r0         */
  { 0x35Cu, 0xE5857630u, CGBA_SH4_M4A_SKIP_MEM  },
  { 0x360u, 0xE4856004u, CGBA_SH4_M4A_SKIP_MEM  },
};

#define CGBA_SH4_M4A_SKIP_ROWS \
  ((unsigned)(sizeof(cgba_sh4_m4a_skip_rows) / sizeof(cgba_sh4_m4a_skip_rows[0])))

/* FNV-1a over the little-endian bytes of each word. */
#define CGBA_SH4_M4A_SKIP_HASH_INIT 0x811C9DC5u
static inline uint32_t cgba_sh4_m4a_skip_hash_word(uint32_t hash, uint32_t word)
{
  unsigned k;
  for (k = 0; k < 4u; k++) {
    hash ^= (word >> (8u * k)) & 0xFFu;
    hash *= 0x01000193u;
  }
  return hash;
}

/* Class of the instruction at `offset` inside a verified image.  The opcode
 * is re-checked so a row can never apply to a different instruction. */
static inline unsigned cgba_sh4_m4a_skip_class(uint32_t offset, uint32_t opcode)
{
  unsigned i;
  if (offset >= CGBA_SH4_M4A_SKIP_BYTES || (offset & 3u))
    return CGBA_SH4_M4A_SKIP_KEEP;
  for (i = 0; i < CGBA_SH4_M4A_SKIP_ROWS; i++) {
    if (cgba_sh4_m4a_skip_rows[i].offset == offset)
      return cgba_sh4_m4a_skip_rows[i].opcode == opcode
        ? cgba_sh4_m4a_skip_rows[i].cls : CGBA_SH4_M4A_SKIP_KEEP;
  }
  return CGBA_SH4_M4A_SKIP_KEEP;
}

/* Decoded addressing of a MEM row: single transfer, immediate or plain
 * register offset.  Returns 0 for any form the elision emitter does not
 * implement (the caller then translates the instruction normally). */
typedef struct cgba_sh4_m4a_skip_mem {
  uint8_t rn, rm;
  uint8_t pre, up, writeback, reg_offset, is_word;
  uint32_t offset;
} cgba_sh4_m4a_skip_mem;

static inline int cgba_sh4_m4a_skip_mem_decode(uint32_t opcode,
                                               cgba_sh4_m4a_skip_mem *m)
{
  m->pre = (opcode >> 24) & 1u;
  m->up = (opcode >> 23) & 1u;
  m->writeback = ((opcode >> 21) & 1u) || !m->pre;
  m->rn = (opcode >> 16) & 0xFu;
  m->rm = 0;
  m->reg_offset = 0;
  m->offset = 0;
  if ((opcode & 0x0E000090u) == 0x00000090u) {      /* halfword / signed form */
    if (!(opcode & 0x60u))
      return 0;                                      /* MUL / SWP space */
    if (opcode & 0x00400000u)
      m->offset = ((opcode >> 4) & 0xF0u) | (opcode & 0xFu);
    else {
      m->reg_offset = 1;
      m->rm = opcode & 0xFu;
    }
    m->is_word = 0;
  } else if ((opcode & 0x0C000000u) == 0x04000000u) { /* word / byte form */
    if (opcode & 0x02000000u) {
      if (opcode & 0xFF0u)
        return 0;                                    /* shifted offset */
      m->reg_offset = 1;
      m->rm = opcode & 0xFu;
    } else {
      m->offset = opcode & 0xFFFu;
    }
    m->is_word = !((opcode >> 22) & 1u);
  } else {
    return 0;
  }
  /* r0 is pinned in a host register and r15 needs pipeline arithmetic;
   * neither occurs as a base or index in the mixer. */
  if (m->rn == 0 || m->rn == 15 || (m->reg_offset && (m->rm == 0 || m->rm == 15)))
    return 0;
  return 1;
}

#endif /* CGBA_SH4_M4A_SKIP_H */
