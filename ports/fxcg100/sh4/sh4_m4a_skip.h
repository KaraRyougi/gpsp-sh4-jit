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

/* ------------------------------------------------------------------------
 * Loop models (CGBA_SH4_M4A_LOOP_FOLD)
 *
 * With the PCM work elided, the two hot loops are counters and pointer
 * bumps.  These models advance them by whole iterations in C.  They are
 * called from a prefix emitted at the loop head, and the translated loop
 * still follows the prefix, so:
 *   - an iteration is folded only if the cycle budget stays positive after
 *     it.  The budget only decreases inside an iteration and every gate in
 *     the translated code fires on budget <= 0, so a folded iteration is one
 *     in which no gate would have fired.  The iteration that exhausts the
 *     budget is always left to the translated code, which exits normally;
 *   - loop exits, sample ends and loop points are never folded either.
 * The cycles charged are the interpreter's for the same path: one sequential
 * fetch per instruction on the path (elided ones included), one
 * nonsequential refill per taken branch, and one nonsequential access per
 * transfer priced by its address region.  All flags the loop head expects
 * come from the last SUBS and are recomputed exactly.
 * ------------------------------------------------------------------------ */

#define CGBA_SH4_M4A_LOOP_REVERB 0x010u   /* reverb loop head            */
#define CGBA_SH4_M4A_LOOP_INTERP 0x300u   /* interpolating group head    */

/* Structural loop-level MP2K/m4a matcher (driver-version independent).
 * Rather than relying on a single whole-image hash, each self-contained ARM
 * inner loop (stereo and mono reverb, fixed-rate main/tail, interpolating
 * Flavor A [r9=phase, lr=step] and Flavor B [lr=phase, r9=step]) is matched
 * by its exact opcode sequence with a parameterized stereo PCM buffer stride
 * (e.g. 0x630; stride == 0 denotes a mono channel loop). */
enum {
  CGBA_SH4_M4A_KIND_NONE            = 0,
  CGBA_SH4_M4A_KIND_REVERB          = 1,
  CGBA_SH4_M4A_KIND_FIXED           = 2,
  CGBA_SH4_M4A_KIND_FIXED_TAIL      = 3,
  CGBA_SH4_M4A_KIND_INTERP_A        = 4,
  CGBA_SH4_M4A_KIND_INTERP_B        = 5,
  CGBA_SH4_M4A_KIND_MONO_REVERB     = 6,
  CGBA_SH4_M4A_KIND_MONO_FIXED      = 7,
  CGBA_SH4_M4A_KIND_MONO_FIXED_TAIL = 8,
  CGBA_SH4_M4A_KIND_MONO_INTERP_A   = 9,
  CGBA_SH4_M4A_KIND_MONO_INTERP_B   = 10
};

#define CGBA_SH4_M4A_MAX_LOOP_WORDS 29u

typedef struct cgba_sh4_m4a_match {
  uint32_t head_pc;
  uint16_t pre_bytes;
  uint16_t body_bytes;
  uint16_t stride;
  uint8_t  kind;
} cgba_sh4_m4a_match;

static inline int cgba_sh4_m4a_match_loop_words(
  const uint32_t *w, unsigned avail, uint32_t head_pc,
  int has_interp_pre, cgba_sh4_m4a_match *out)
{
  out->kind = CGBA_SH4_M4A_KIND_NONE;
  out->head_pc = 0;
  out->pre_bytes = 0;
  out->body_bytes = 0;
  out->stride = 0;

  if (avail >= 17u && w[0] == 0xE19500D6u) {
    static const uint32_t rev[15] = {
      0xE19500D6u, 0xE1D510D0u, 0xE0800001u, 0xE19710D6u, 0xE0800001u,
      0xE0D710D1u, 0xE0800001u, 0xE0010390u, 0xE1A004C1u, 0xE3100080u,
      0x12800001u, 0xE7C50006u, 0xE4C50001u, 0xE2544001u, 0xCAFFFFF0u
    };
    unsigned i;
    for (i = 1u; i < 15u; i++) {
      if (w[i] != rev[i])
        return 0;
    }
    if ((w[15] & 0xFFFFF000u) != 0xE28F0000u || w[16] != 0xE12FFF10u)
      return 0;
    out->head_pc = head_pc;
    out->body_bytes = 15u * 4u;
    out->kind = CGBA_SH4_M4A_KIND_REVERB;
    return 1;
  }

  /* Mono reverb (2-tap average, 10 body words + 2-word Thumb return epilogue). */
  if (avail >= 12u && w[0] == 0xE1D500D0u) {
    static const uint32_t mrev[10] = {
      0xE1D500D0u, 0xE0D710D1u, 0xE0800001u, 0xE0010390u, 0xE1A00441u,
      0xE3100080u, 0x12800001u, 0xE4C50001u, 0xE2544001u, 0xCAFFFFF5u
    };
    unsigned i;
    for (i = 1u; i < 10u; i++) {
      if (w[i] != mrev[i])
        return 0;
    }
    if ((w[10] & 0xFFFFF000u) != 0xE28F0000u || w[11] != 0xE12FFF10u)
      return 0;
    out->head_pc = head_pc;
    out->body_bytes = 10u * 4u;
    out->kind = CGBA_SH4_M4A_KIND_MONO_REVERB;
    return 1;
  }

  if (avail < 10u || w[0] != 0xE5956000u)
    return 0;

  /* Stereo fixed-rate and interpolating loops: w[1] = ldr r7, [r5, #stride]. */
  if (avail >= 15u && (w[1] & 0xFFFFF000u) == 0xE5957000u) {
    uint32_t stride = w[1] & 0xFFFu;
    if (stride == 0u || (stride & 3u) != 0u)
      return 0;

    /* Fixed-rate main (15 words) and fixed-rate tail/loop-point (17 words). */
    if (w[2] == 0xE0D300D1u && w[3] == 0xE001009Au && w[4] == 0xE3C118FFu &&
        w[5] == 0xE0816466u && w[6] == 0xE001009Bu && w[7] == 0xE3C118FFu &&
        w[8] == 0xE0817467u) {
      if (w[9] == 0xE2955101u && w[10] == 0x3AFFFFF6u &&
          w[11] == (0xE5857000u | stride) && w[12] == 0xE4856004u &&
          w[13] == 0xE2588004u && w[14] == 0xCAFFFFF0u) {
        out->head_pc = head_pc;
        out->body_bytes = 15u * 4u;
        out->stride = (uint16_t)stride;
        out->kind = CGBA_SH4_M4A_KIND_FIXED;
        return 1;
      }
      if (avail >= 17u && w[9] == 0xE2522001u &&
          (w[10] & 0xFF000000u) == 0x0A000000u &&
          w[11] == 0xE2955101u && w[12] == 0x3AFFFFF4u &&
          w[13] == (0xE5857000u | stride) && w[14] == 0xE4856004u &&
          w[15] == 0xE2588004u && (w[16] & 0xFF000000u) == 0xCA000000u) {
        out->head_pc = head_pc;
        out->body_bytes = 17u * 4u;
        out->stride = (uint16_t)stride;
        out->kind = CGBA_SH4_M4A_KIND_FIXED_TAIL;
        return 1;
      }
      return 0;
    }

    /* Interpolating 4-sample mix (27 words + 2-word epilogue check). */
    if (avail >= 29u &&
        w[21] == 0xE2955101u && w[22] == 0x3AFFFFEAu &&
        w[23] == (0xE5857000u | stride) && w[24] == 0xE4856004u &&
        w[25] == 0xE2588004u && w[26] == 0xCAFFFFE4u &&
        w[27] == 0xE2433001u && w[28] == 0xE8BD1010u &&
        (w[15] & 0xFF000000u) == 0xDA000000u &&
        w[17] == 0x00800001u && w[19] == 0xE1F310D1u && w[20] == 0xE0411000u) {
      if (w[2] == 0xE00E0199u && w[3] == 0xE080EBCEu &&
          w[4] == 0xE00C0E9Au && w[5] == 0xE3CCC8FFu &&
          w[6] == 0xE08C6466u && w[7] == 0xE00C0E9Bu &&
          w[8] == 0xE3CCC8FFu && w[9] == 0xE08C7467u &&
          w[10] == 0xE0899004u && w[11] == 0xE1B0EBA9u &&
          w[12] == 0x0A000007u && w[13] == 0xE3C995FEu &&
          w[14] == 0xE052200Eu && w[16] == 0xE25EE001u &&
          w[18] == 0x11B300DEu) {
        out->head_pc = head_pc;
        out->pre_bytes = has_interp_pre ? 12u : 0u;
        out->body_bytes = 27u * 4u;
        out->stride = (uint16_t)stride;
        out->kind = CGBA_SH4_M4A_KIND_INTERP_A;
        return 1;
      }
      if (w[2] == 0xE009019Eu && w[3] == 0xE0809BC9u &&
          w[4] == 0xE00C099Au && w[5] == 0xE3CCC8FFu &&
          w[6] == 0xE08C6466u && w[7] == 0xE00C099Bu &&
          w[8] == 0xE3CCC8FFu && w[9] == 0xE08C7467u &&
          w[10] == 0xE08EE004u && w[11] == 0xE1B09BAEu &&
          w[12] == 0x0A000007u && w[13] == 0xE3CEE5FEu &&
          w[14] == 0xE0522009u && w[16] == 0xE2599001u &&
          w[18] == 0x11B300D9u) {
        out->head_pc = head_pc;
        out->pre_bytes = has_interp_pre ? 12u : 0u;
        out->body_bytes = 27u * 4u;
        out->stride = (uint16_t)stride;
        out->kind = CGBA_SH4_M4A_KIND_INTERP_B;
        return 1;
      }
    }
    return 0;
  }

  /* Mono fixed-rate main (10 words) and tail/loop-point (12 words). */
  if (w[1] == 0xE0D300D1u && w[2] == 0xE001009Au && w[3] == 0xE3C118FFu &&
      w[4] == 0xE0816466u) {
    if (w[5] == 0xE2955101u && w[6] == 0x3AFFFFF9u &&
        w[7] == 0xE4856004u && w[8] == 0xE2588004u &&
        w[9] == 0xCAFFFFF5u) {
      out->head_pc = head_pc;
      out->body_bytes = 10u * 4u;
      out->kind = CGBA_SH4_M4A_KIND_MONO_FIXED;
      return 1;
    }
    if (avail >= 12u && w[5] == 0xE2522001u &&
        (w[6] & 0xFF000000u) == 0x0A000000u &&
        w[7] == 0xE2955101u && w[8] == 0x3AFFFFF7u &&
        w[9] == 0xE4856004u && w[10] == 0xE2588004u &&
        (w[11] & 0xFF000000u) == 0xCA000000u) {
      out->head_pc = head_pc;
      out->body_bytes = 12u * 4u;
      out->kind = CGBA_SH4_M4A_KIND_MONO_FIXED_TAIL;
      return 1;
    }
    return 0;
  }

  /* Mono interpolating 4-sample mix (22 words + 2-word epilogue check). */
  if (avail >= 24u &&
      w[4] == 0xE3CCC8FFu && w[5] == 0xE08C6466u &&
      w[8] == 0x0A000007u && (w[11] & 0xFF000000u) == 0xDA000000u &&
      w[13] == 0x00800001u && w[15] == 0xE1F310D1u &&
      w[16] == 0xE0411000u && w[17] == 0xE2955101u &&
      w[18] == 0x3AFFFFEDu && w[19] == 0xE4856004u &&
      w[20] == 0xE2588004u && w[21] == 0xCAFFFFE9u &&
      w[22] == 0xE2433001u && w[23] == 0xE8BD1010u) {
    if (w[1] == 0xE00E0199u && w[2] == 0xE080EBCEu &&
        w[3] == 0xE00C0E9Au && w[6] == 0xE0899004u &&
        w[7] == 0xE1B0EBA9u && w[9] == 0xE3C995FEu &&
        w[10] == 0xE052200Eu && w[12] == 0xE25EE001u &&
        w[14] == 0x11B300DEu) {
      out->head_pc = head_pc;
      out->pre_bytes = has_interp_pre ? 12u : 0u;
      out->body_bytes = 22u * 4u;
      out->kind = CGBA_SH4_M4A_KIND_MONO_INTERP_A;
      return 1;
    }
    if (w[1] == 0xE009019Eu && w[2] == 0xE0809BC9u &&
        w[3] == 0xE00C099Au && w[6] == 0xE08EE004u &&
        w[7] == 0xE1B09BAEu && w[9] == 0xE3CEE5FEu &&
        w[10] == 0xE0522009u && w[12] == 0xE2599001u &&
        w[14] == 0x11B300D9u) {
      out->head_pc = head_pc;
      out->pre_bytes = has_interp_pre ? 12u : 0u;
      out->body_bytes = 22u * 4u;
      out->kind = CGBA_SH4_M4A_KIND_MONO_INTERP_B;
      return 1;
    }
  }
  return 0;
}

static inline unsigned cgba_sh4_m4a_match_class(
  const cgba_sh4_m4a_match *m, uint32_t pc, uint32_t opcode)
{
  int32_t rel;
  unsigned idx;

  if (!m || m->kind == CGBA_SH4_M4A_KIND_NONE || (pc & 3u))
    return CGBA_SH4_M4A_SKIP_KEEP;
  rel = (int32_t)(pc - m->head_pc);
  if (rel < -(int32_t)m->pre_bytes || rel >= (int32_t)m->body_bytes)
    return CGBA_SH4_M4A_SKIP_KEEP;

  if (rel < 0) {
    /* 3-word interpolating pre-header: ldrsb r0,[r3]; ldrsb r1,[r3,#1]!; sub r1,r1,r0 */
    if (rel == -12 && opcode == 0xE1D300D0u) return CGBA_SH4_M4A_SKIP_MEM;
    if (rel == -8  && opcode == 0xE1F310D1u) return CGBA_SH4_M4A_SKIP_MEM;
    if (rel == -4  && opcode == 0xE0411000u) return CGBA_SH4_M4A_SKIP_DROP;
    return CGBA_SH4_M4A_SKIP_KEEP;
  }

  idx = (unsigned)rel >> 2;
  switch (m->kind) {
  case CGBA_SH4_M4A_KIND_REVERB:
    switch (idx) {
    case 0: case 1: case 3: case 5: case 11: case 12:
      return CGBA_SH4_M4A_SKIP_MEM;
    case 2: case 4: case 6: case 7: case 8: case 9: case 10:
      return CGBA_SH4_M4A_SKIP_DROP;
    default:
      return CGBA_SH4_M4A_SKIP_KEEP;
    }
  case CGBA_SH4_M4A_KIND_FIXED:
    switch (idx) {
    case 0: case 1: case 2: case 11: case 12:
      return CGBA_SH4_M4A_SKIP_MEM;
    case 3: case 4: case 5: case 6: case 7: case 8:
      return CGBA_SH4_M4A_SKIP_DROP;
    default:
      return CGBA_SH4_M4A_SKIP_KEEP;
    }
  case CGBA_SH4_M4A_KIND_FIXED_TAIL:
    switch (idx) {
    case 0: case 1: case 2: case 13: case 14:
      return CGBA_SH4_M4A_SKIP_MEM;
    case 3: case 4: case 5: case 6: case 7: case 8:
      return CGBA_SH4_M4A_SKIP_DROP;
    default:
      return CGBA_SH4_M4A_SKIP_KEEP;
    }
  case CGBA_SH4_M4A_KIND_INTERP_A:
  case CGBA_SH4_M4A_KIND_INTERP_B:
    switch (idx) {
    case 0: case 1: case 18: case 19: case 23: case 24:
      return CGBA_SH4_M4A_SKIP_MEM;
    case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9:
    case 17: case 20:
      return CGBA_SH4_M4A_SKIP_DROP;
    default:
      return CGBA_SH4_M4A_SKIP_KEEP;
    }
  case CGBA_SH4_M4A_KIND_MONO_REVERB:
    switch (idx) {
    case 0: case 1: case 7:
      return CGBA_SH4_M4A_SKIP_MEM;
    case 2: case 3: case 4: case 5: case 6:
      return CGBA_SH4_M4A_SKIP_DROP;
    default:
      return CGBA_SH4_M4A_SKIP_KEEP;
    }
  case CGBA_SH4_M4A_KIND_MONO_FIXED:
    switch (idx) {
    case 0: case 1: case 7:
      return CGBA_SH4_M4A_SKIP_MEM;
    case 2: case 3: case 4:
      return CGBA_SH4_M4A_SKIP_DROP;
    default:
      return CGBA_SH4_M4A_SKIP_KEEP;
    }
  case CGBA_SH4_M4A_KIND_MONO_FIXED_TAIL:
    switch (idx) {
    case 0: case 1: case 9:
      return CGBA_SH4_M4A_SKIP_MEM;
    case 2: case 3: case 4:
      return CGBA_SH4_M4A_SKIP_DROP;
    default:
      return CGBA_SH4_M4A_SKIP_KEEP;
    }
  case CGBA_SH4_M4A_KIND_MONO_INTERP_A:
  case CGBA_SH4_M4A_KIND_MONO_INTERP_B:
    switch (idx) {
    case 0: case 14: case 15: case 19:
      return CGBA_SH4_M4A_SKIP_MEM;
    case 1: case 2: case 3: case 4: case 5: case 13: case 16:
      return CGBA_SH4_M4A_SKIP_DROP;
    default:
      return CGBA_SH4_M4A_SKIP_KEEP;
    }
  default:
    return CGBA_SH4_M4A_SKIP_KEEP;
  }
}

typedef struct cgba_sh4_m4a_loop_costs {
  uint32_t seq;                 /* ws_cyc_seq[code region][word]  */
  uint32_t branch;              /* ws_cyc_nseq[code region][word] */
  const uint8_t (*mem)[2];      /* ws_cyc_nseq[16][2]             */
} cgba_sh4_m4a_loop_costs;

static inline uint32_t cgba_sh4_m4a_subs_flags(uint32_t a, uint32_t b)
{
  uint32_t r = a - b;
  return (r & 0x80000000u) | (r == 0 ? 0x40000000u : 0u) |
         (a >= b ? 0x20000000u : 0u) |
         ((((a ^ b) & (a ^ r)) >> 31) << 28);
}

/* Reverb loop, one sample per iteration (15 instructions, 6 byte transfers):
 *   r4 = samples left, r5/r7 = buffer cursors, r6 = constant channel offset.
 * Returns the cycles spent; 0 means nothing was folded and nothing changed. */
static inline uint32_t cgba_sh4_m4a_fold_reverb(
  uint32_t *r4, uint32_t *r5, uint32_t r6, uint32_t *r7, uint32_t *cpsr,
  int32_t budget, const cgba_sh4_m4a_loop_costs *c)
{
  uint32_t n4 = *r4, n5 = *r5, n7 = *r7, spent = 0, last = 0;
  uint32_t rem, base_cost;
  const uint8_t (*mem)[2] = c->mem;

  if (budget <= 1 || (int32_t)n4 <= 1)
    return 0;
  rem = (uint32_t)(budget - 1);       /* strictly positive remaining budget */
  base_cost = 15u * c->seq + c->branch;

  while ((int32_t)n4 > 1) {           /* keep the final (exiting) iteration */
    uint32_t a0 = n5 + r6, a2 = n7 + r6, cost;
    uint32_t max_k, by_budget, room, r1, r2, r3, step_cost;
    if ((a0 >> 28) | (n5 >> 28) | (a2 >> 28) | (n7 >> 28))
      break;                          /* outside the charge table */
    cost = base_cost +
           2u * mem[a0 >> 24][0] + 2u * mem[n5 >> 24][0] +
           mem[a2 >> 24][0] + mem[n7 >> 24][0];
    if (cost > rem)
      break;
    /* All four cursors increment by 1/iter, so cost stays constant until one
     * crosses a 16 MiB region boundary. Fold the whole uniform run in O(1). */
    max_k = n4 - 1u;
    by_budget = rem / cost;
    if (by_budget < max_k)
      max_k = by_budget;
    room = 0x01000000u - (a0 & 0x00FFFFFFu);
    r1 = 0x01000000u - (n5 & 0x00FFFFFFu);
    r2 = 0x01000000u - (a2 & 0x00FFFFFFu);
    r3 = 0x01000000u - (n7 & 0x00FFFFFFu);
    if (r1 < room) room = r1;
    if (r2 < room) room = r2;
    if (r3 < room) room = r3;
    if (room < max_k)
      max_k = room;
    step_cost = max_k * cost;
    spent += step_cost;
    rem -= step_cost;
    last = n4 - max_k + 1u;
    n4 -= max_k;
    n5 += max_k;
    n7 += max_k;
  }
  if (!spent)
    return 0;
  *r4 = n4; *r5 = n5; *r7 = n7;
  *cpsr = (*cpsr & 0x0FFFFFFFu) | cgba_sh4_m4a_subs_flags(last, 1u);
  return spent;
}

/* Mono reverb loop, one sample per iteration (10 instructions, 3 byte
 * transfers: ldrsb [r5], ldrsb [r7],#1, strb [r5],#1). */
static inline uint32_t cgba_sh4_m4a_fold_reverb_mono(
  uint32_t *r4, uint32_t *r5, uint32_t *r7, uint32_t *cpsr,
  int32_t budget, const cgba_sh4_m4a_loop_costs *c)
{
  uint32_t n4 = *r4, n5 = *r5, n7 = *r7, spent = 0, last = 0;
  uint32_t rem, base_cost;
  const uint8_t (*mem)[2] = c->mem;

  if (budget <= 1 || (int32_t)n4 <= 1)
    return 0;
  rem = (uint32_t)(budget - 1);
  base_cost = 10u * c->seq + c->branch;

  while ((int32_t)n4 > 1) {
    uint32_t cost, max_k, by_budget, room, r1, step_cost;
    if ((n5 >> 28) | (n7 >> 28))
      break;
    cost = base_cost + 2u * mem[n5 >> 24][0] + mem[n7 >> 24][0];
    if (cost > rem)
      break;
    max_k = n4 - 1u;
    by_budget = rem / cost;
    if (by_budget < max_k)
      max_k = by_budget;
    room = 0x01000000u - (n5 & 0x00FFFFFFu);
    r1 = 0x01000000u - (n7 & 0x00FFFFFFu);
    if (r1 < room) room = r1;
    if (room < max_k)
      max_k = room;
    step_cost = max_k * cost;
    spent += step_cost;
    rem -= step_cost;
    last = n4 - max_k + 1u;
    n4 -= max_k;
    n5 += max_k;
    n7 += max_k;
  }
  if (!spent)
    return 0;
  *r4 = n4; *r5 = n5; *r7 = n7;
  *cpsr = (*cpsr & 0x0FFFFFFFu) | cgba_sh4_m4a_subs_flags(last, 1u);
  return spent;
}

/* Fixed-rate mix, four samples per group:
 *   stereo (stride != 0): 15 instructions, 42 sequential fetches + 4 taken
 *     branches + 4 word buffer transfers + 4 byte wave loads;
 *   mono   (stride == 0): 10 instructions, 28 sequential fetches + 4 taken
 *     branches + 2 word buffer transfers + 4 byte wave loads.
 *   r3 = wave cursor, r5 = buffer cursor (top bits clear at group head),
 *   r8 = output samples left, stride = stereo channel offset (0 for mono).
 * Returns the cycles spent; 0 means nothing was folded and nothing changed. */
static inline uint32_t cgba_sh4_m4a_fold_fixed(
  uint32_t *r3, uint32_t *r5, uint32_t *r8, uint32_t *cpsr, uint32_t stride,
  int32_t budget, const cgba_sh4_m4a_loop_costs *c)
{
  uint32_t n3 = *r3, n5 = *r5, n8 = *r8, spent = 0;
  uint32_t rem, base_cost;
  const uint8_t (*mem)[2] = c->mem;

  if (budget <= 1 || (int32_t)n8 <= 4)
    return 0;
  rem = (uint32_t)(budget - 1);       /* strictly positive remaining budget */
  base_cost = (stride ? 42u : 28u) * c->seq + 4u * c->branch;

  while ((int32_t)n8 > 4) {           /* keep the final (exiting) group */
    uint32_t n5_b = n5 + stride;
    uint32_t buf_cost, cost, max_k, by_budget, k_r3, k_r5, k_r5b, step_cost;
    if ((n3 >> 28) | (n5 >> 28) | (n5_b >> 28))
      break;
    buf_cost = 2u * mem[n5 >> 24][1] + (stride ? 2u * mem[n5_b >> 24][1] : 0u);
    k_r3 = (0x01000000u - (n3 & 0x00FFFFFFu)) >> 2;
    if (k_r3 == 0u) {
      /* Wave pointer straddles a 16 MiB region seam within this 4-byte group. */
      uint32_t a1 = n3 + 1u, a2 = n3 + 2u, a3 = n3 + 3u;
      if ((a1 >> 28) | (a2 >> 28) | (a3 >> 28))
        break;
      cost = base_cost + buf_cost +
             mem[n3 >> 24][0] + mem[a1 >> 24][0] +
             mem[a2 >> 24][0] + mem[a3 >> 24][0];
      if (cost > rem)
        break;
      spent += cost;
      rem -= cost;
      n3 += 4u;
      n5 += 4u;
      n8 -= 4u;
      continue;
    }
    cost = base_cost + buf_cost + 4u * mem[n3 >> 24][0];
    if (cost > rem)
      break;
    max_k = (n8 - 1u) >> 2;
    by_budget = rem / cost;
    if (by_budget < max_k) max_k = by_budget;
    if (k_r3 < max_k) max_k = k_r3;
    k_r5 = ((0x00FFFFFFu - (n5 & 0x00FFFFFFu)) >> 2) + 1u;
    k_r5b = ((0x00FFFFFFu - (n5_b & 0x00FFFFFFu)) >> 2) + 1u;
    if (k_r5 < max_k) max_k = k_r5;
    if (k_r5b < max_k) max_k = k_r5b;
    step_cost = max_k * cost;
    spent += step_cost;
    rem -= step_cost;
    n3 += max_k << 2;
    n5 += max_k << 2;
    n8 -= max_k << 2;
  }
  if (!spent)
    return 0;
  *r3 = n3; *r5 = n5; *r8 = n8;
  *cpsr = (*cpsr & 0x0FFFFFFFu) | cgba_sh4_m4a_subs_flags(n8 + 4u, 4u);
  return spent;
}

static inline uint32_t cgba_sh4_m4a_fold_fixed_mono(
  uint32_t *r3, uint32_t *r5, uint32_t *r8, uint32_t *cpsr,
  int32_t budget, const cgba_sh4_m4a_loop_costs *c)
{
  return cgba_sh4_m4a_fold_fixed(r3, r5, r8, cpsr, 0u, budget, c);
}

/* Interpolating mix, four samples per group (stride == 0 selects mono):
 *   r2 = samples left in the wave, r3 = wave cursor, r4 = phase step,
 *   r5 = buffer cursor (sample-in-group counter in its top two bits),
 *   r8 = output samples left, r9 = phase, lr = last integer step. */
typedef struct cgba_sh4_m4a_interp_state {
  uint32_t r2, r3, r4, r5, r8, r9, lr, cpsr;
} cgba_sh4_m4a_interp_state;

static inline uint32_t cgba_sh4_m4a_fold_interp_stride(
  cgba_sh4_m4a_interp_state *s, uint32_t stride, int32_t budget,
  const cgba_sh4_m4a_loop_costs *c)
{
  uint32_t spent = 0, rem;
  uint32_t r2 = s->r2, r3 = s->r3, r4 = s->r4, r5 = s->r5;
  uint32_t r8 = s->r8, r9 = s->r9, lr = s->lr;
  uint32_t seq = c->seq, br = c->branch;
  uint32_t fixed_cost = (stride ? 58u : 44u) * seq + 4u * br;
  uint32_t step_seq = 8u * seq;
  const uint8_t (*mem)[2] = c->mem;

  if (budget <= 1 || (int32_t)r8 <= 4)
    return 0;
  rem = (uint32_t)(budget - 1);       /* strictly positive remaining budget */

  /* Cache region-derived wait-state costs across groups: in practice r5 stays
   * in IWRAM (0x03) and r3 stays in ROM/RAM for the entire mix call. */
  uint32_t r5_reg = 0xFFFFFFFFu, r5_b_reg = 0xFFFFFFFFu;
  uint32_t r5_cost = 0;
  uint32_t r3_reg = r3 >> 24;
  uint32_t r3_mem = (r3_reg < 16u) ? mem[r3_reg][0] : 0u;

  /* O(1) uniform-region batch fast path: fold up to 63 groups (252 samples)
   * at a time using pure 32-bit arithmetic (4*63*f < 2^31).  The exact 1-group
   * tail loop below finishes the final 0..2 groups and any edge cases. */
  if ((int32_t)r2 > 0 && r9 < 0x00800000u && r4 < 0x3F800000u &&
      (r3 >> 28) == 0u) {
    uint32_t q = r4 >> 23;
    uint32_t f = r4 & 0x007FFFFFu;
    uint32_t step1 = step_seq + r3_mem;
    uint32_t step2 = step1 + r3_mem;
    for (;;) {
      uint32_t r5_b = r5 + stride;
      uint32_t k, k_bud, k_r5, k_r5b, max_gcost, n_samp, phase_sum, carry;
      uint32_t total_steps, max_steps, r3_room, batch_cost;
      if ((r5 >> 28) | (r5_b >> 28) || (int32_t)r8 <= 4)
        break;
      if ((r5 >> 24) != r5_reg || (r5_b >> 24) != r5_b_reg) {
        r5_reg = r5 >> 24;
        r5_b_reg = r5_b >> 24;
        r5_cost = fixed_cost + 2u * mem[r5_reg][1] +
                  (stride ? 2u * mem[r5_b_reg][1] : 0u);
      }
      if (q == 0u)
        max_gcost = r5_cost + 4u * (step1 > br ? step1 : br);
      else
        max_gcost = r5_cost + 4u * step2;
      k_bud = rem / max_gcost;
      if (k_bud == 0u)
        break;
      k = (r8 - 1u) >> 2;
      if (k > 63u) k = 63u;
      if (k_bud < k) k = k_bud;
      k_r5 = (0x01000000u - (r5 & 0x00FFFFFFu)) >> 2;
      k_r5b = (0x01000000u - (r5_b & 0x00FFFFFFu)) >> 2;
      if (k_r5 < k) k = k_r5;
      if (k_r5b < k) k = k_r5b;
      if (k == 0u)
        break;
      r3_room = 0x00FFFFFFu - (r3 & 0x00FFFFFFu);
      max_steps = r2 - 1u;
      if (r3_room < max_steps) max_steps = r3_room;
      n_samp = k << 2;
      phase_sum = r9 + n_samp * f;
      carry = phase_sum >> 23;
      total_steps = n_samp * q + carry;
      if (total_steps > max_steps) {
        k = max_steps / ((q + 1u) << 2);
        if (k == 0u)
          break;
        n_samp = k << 2;
        phase_sum = r9 + n_samp * f;
        carry = phase_sum >> 23;
        total_steps = n_samp * q + carry;
      }
      r9 = phase_sum & 0x007FFFFFu;
      if (q == 0u) {
        batch_cost = k * r5_cost + (n_samp - carry) * br + carry * step1;
        lr = 0u;
      } else if (q == 1u) {
        batch_cost = k * (r5_cost + 4u * step1) + carry * r3_mem;
        lr = (r9 < f) ? 1u : 0u;
      } else {
        batch_cost = k * (r5_cost + 4u * step2);
        lr = (q - 1u) + ((r9 < f) ? 1u : 0u);
      }
      spent += batch_cost;
      rem -= batch_cost;
      r2 -= total_steps;
      r3 += total_steps;
      r5 += n_samp;
      r8 -= n_samp;
    }
  }

  for (;;) {
    uint32_t n2 = r2, n3 = r3, n9 = r9, nlr = lr;
    uint32_t r5_b, cost, i;

    /* Group head: top bits clear, and the group must not be the last one
     * (SUBS r8, r8, #4 ; BGT must be taken). */
    r5_b = r5 + stride;
    if ((r5 >> 28) | (r5_b >> 28) || (int32_t)r8 <= 4)
      break;
    if ((r5 >> 24) != r5_reg || (r5_b >> 24) != r5_b_reg) {
      r5_reg = r5 >> 24;
      r5_b_reg = r5_b >> 24;
      r5_cost = fixed_cost + 2u * mem[r5_reg][1] +
                (stride ? 2u * mem[r5_b_reg][1] : 0u);
    }
    cost = r5_cost;
    for (i = 0; i < 4u; i++) {
      n9 += r4;
      nlr = n9 >> 23;
      if (nlr == 0u) {
        cost += br;                              /* BEQ over the step code */
      } else {
        n9 &= ~0x3F800000u;
        if ((int32_t)n2 <= (int32_t)nlr)
          goto done;                             /* wave end / loop point */
        n2 -= nlr;
        cost += step_seq;
        nlr -= 1u;
        if (nlr != 0u) {
          n3 += nlr;
          if (n3 >> 28)
            goto done;
          if (__builtin_expect((n3 >> 24) != r3_reg, 0)) {
            r3_reg = n3 >> 24;
            r3_mem = mem[r3_reg][0];
          }
          cost += r3_mem;
        }
        n3 += 1u;
        if (n3 >> 28)
          goto done;
        if (__builtin_expect((n3 >> 24) != r3_reg, 0)) {
          r3_reg = n3 >> 24;
          r3_mem = mem[r3_reg][0];
        }
        cost += r3_mem;
      }
    }
    if (cost > rem)
      break;
    spent += cost;
    rem -= cost;
    r2 = n2; r3 = n3; r5 += 4u; r9 = n9; lr = nlr;
    r8 -= 4u;
  }
done:
  if (spent) {
    s->cpsr = (s->cpsr & 0x0FFFFFFFu) | cgba_sh4_m4a_subs_flags(r8 + 4u, 4u);
    s->r2 = r2; s->r3 = r3; s->r5 = r5; s->r8 = r8; s->r9 = r9; s->lr = lr;
  }
  return spent;
}

static inline uint32_t cgba_sh4_m4a_fold_interp(
  cgba_sh4_m4a_interp_state *s, int32_t budget,
  const cgba_sh4_m4a_loop_costs *c)
{
  return cgba_sh4_m4a_fold_interp_stride(s, 0x630u, budget, c);
}

static inline uint32_t cgba_sh4_m4a_fold_interp_mono(
  cgba_sh4_m4a_interp_state *s, int32_t budget,
  const cgba_sh4_m4a_loop_costs *c)
{
  return cgba_sh4_m4a_fold_interp_stride(s, 0u, budget, c);
}

#endif /* CGBA_SH4_M4A_SKIP_H */
