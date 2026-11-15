#include <stdint.h>
#include <stdio.h>

#include "ports/fxcg100/sh4/sh4_m4a_skip.h"

/* Audit of the m4a elision table.  The liveness argument in sh4_m4a_skip.h
 * holds only if each row is the kind of instruction it claims to be; this
 * decodes every row and enforces that. */

#define FAIL(...) do { printf("m4a skip table: " __VA_ARGS__); printf("\n"); \
                       return 1; } while (0)

/* Registers a dropped instruction may write / a skipped load may target.
 * The reverb loop (before the mix entry) uses r5/r7 as buffer pointers and r6
 * as a constant index, so only its r0/r1 temporaries are scratch there; in
 * the mix paths r6/r7 are the PCM accumulators and ip/lr interpolation
 * temporaries. */
static unsigned row_offset;
static int dead_reg(unsigned r)
{
  if (r == 0 || r == 1)
    return 1;
  if (row_offset < CGBA_SH4_M4A_SKIP_ENTRY_MIX)
    return 0;
  return r == 6 || r == 7 || r == 12 || r == 14;
}

int main(void)
{
  unsigned i, drops = 0, mems = 0, flag_setters = 0;
  uint32_t previous = 0;

  for (i = 0; i < CGBA_SH4_M4A_SKIP_ROWS; i++) {
    const cgba_sh4_m4a_skip_row *row = &cgba_sh4_m4a_skip_rows[i];
    uint32_t op = row->opcode;
    unsigned cond = op >> 28, rd = (op >> 12) & 15;

    row_offset = row->offset;
    if ((row->offset & 3) || row->offset >= CGBA_SH4_M4A_SKIP_BYTES)
      FAIL("row %u: bad offset %x", i, row->offset);
    if (i && row->offset <= previous)
      FAIL("row %u: offsets not strictly increasing", i);
    previous = row->offset;
    if (cond == 15)
      FAIL("row %u: unconditional-space opcode", i);
    if (cgba_sh4_m4a_skip_class(row->offset, op) != row->cls)
      FAIL("row %u: class lookup mismatch", i);
    if (cgba_sh4_m4a_skip_class(row->offset, op ^ 0x00010000u) !=
        CGBA_SH4_M4A_SKIP_KEEP)
      FAIL("row %u: a different opcode at this offset was not kept", i);

    if (row->cls == CGBA_SH4_M4A_SKIP_DROP) {
      drops++;
      if ((op & 0x0FC000F0u) == 0x00000090u) {            /* MUL / MLA */
        unsigned mrd = (op >> 16) & 15;
        if (op & 0x00100000u)
          FAIL("row %u: flag-setting multiply", i);
        if (!dead_reg(mrd))
          FAIL("row %u: multiply writes live r%u", i, mrd);
      } else if ((op & 0x0C000000u) == 0) {               /* data processing */
        unsigned opc = (op >> 21) & 15;
        if ((op & 0x02000000u) == 0 && (op & 0x90u) == 0x90u)
          FAIL("row %u: not a data-processing opcode", i);
        if (opc >= 8 && opc <= 11) {                      /* TST/TEQ/CMP/CMN */
          flag_setters++;
        } else {
          if (op & 0x00100000u)
            FAIL("row %u: flag-setting ALU op", i);
          if (!dead_reg(rd))
            FAIL("row %u: ALU op writes live r%u", i, rd);
        }
      } else {
        FAIL("row %u: DROP row is not ALU/MUL", i);
      }
    } else if (row->cls == CGBA_SH4_M4A_SKIP_MEM) {
      cgba_sh4_m4a_skip_mem m;
      mems++;
      if (!cgba_sh4_m4a_skip_mem_decode(op, &m))
        FAIL("row %u: MEM row does not decode", i);
      if (rd == 15)
        FAIL("row %u: PC transfer", i);
      if ((op & 0x00100000u) && !dead_reg(rd))
        FAIL("row %u: load targets live r%u", i, rd);
      /* Writeback bases must stay exact, so they may not also be elided
       * destinations of the same instruction. */
      if ((op & 0x00100000u) && m.writeback && rd == m.rn)
        FAIL("row %u: load writes back into its own destination", i);
      if (dead_reg(m.rn))
        FAIL("row %u: base r%u is a scratch register", i, m.rn);
    } else {
      FAIL("row %u: unknown class", i);
    }
  }

  /* Exactly one dropped flag setter (reverb TST), immediately followed by the
   * dropped conditional that is its only reader. */
  if (flag_setters != 1)
    FAIL("%u dropped flag setters, expected 1", flag_setters);
  for (i = 0; i + 1 < CGBA_SH4_M4A_SKIP_ROWS; i++) {
    const cgba_sh4_m4a_skip_row *row = &cgba_sh4_m4a_skip_rows[i];
    if (row->opcode == 0xE3100080u) {
      if (row[1].offset != row->offset + 4u ||
          row[1].cls != CGBA_SH4_M4A_SKIP_DROP || (row[1].opcode >> 28) != 1)
        FAIL("TST is not followed by its dropped ADDNE");
    }
  }

  /* Addressing decode spot checks. */
  {
    cgba_sh4_m4a_skip_mem m;
    if (!cgba_sh4_m4a_skip_mem_decode(0xE5957630u, &m) || m.rn != 5 ||
        !m.pre || !m.up || m.writeback || m.reg_offset || !m.is_word ||
        m.offset != 0x630)
      FAIL("decode ldr r7,[r5,#0x630]");
    if (!cgba_sh4_m4a_skip_mem_decode(0xE4856004u, &m) || m.rn != 5 ||
        m.pre || !m.up || !m.writeback || !m.is_word || m.offset != 4)
      FAIL("decode str r6,[r5],#4");
    if (!cgba_sh4_m4a_skip_mem_decode(0x11B300DEu, &m) || m.rn != 3 ||
        !m.pre || !m.up || !m.writeback || !m.reg_offset || m.rm != 14 ||
        m.is_word)
      FAIL("decode ldrsbne r0,[r3,lr]!");
    if (!cgba_sh4_m4a_skip_mem_decode(0xE0D300D1u, &m) || m.rn != 3 ||
        m.pre || !m.writeback || m.reg_offset || m.offset != 1 || m.is_word)
      FAIL("decode ldrsb r0,[r3],#1");
    if (!cgba_sh4_m4a_skip_mem_decode(0xE7C50006u, &m) || m.rn != 5 ||
        !m.pre || m.writeback || !m.reg_offset || m.rm != 6 || m.is_word)
      FAIL("decode strb r0,[r5,r6]");
    if (cgba_sh4_m4a_skip_mem_decode(0xE7950106u, &m))
      FAIL("shifted register offset must be rejected");
    if (cgba_sh4_m4a_skip_mem_decode(0xE59F0004u, &m))
      FAIL("PC-relative load must be rejected");
    if (cgba_sh4_m4a_skip_mem_decode(0xE0010390u, &m))
      FAIL("MUL must not decode as a transfer");
  }

  if (cgba_sh4_m4a_skip_class(CGBA_SH4_M4A_SKIP_BYTES, 0xE0800001u) ||
      cgba_sh4_m4a_skip_class(0x01Au, 0xE0800001u) ||
      cgba_sh4_m4a_skip_class(0x000u, CGBA_SH4_M4A_SKIP_FIRST_WORD))
    FAIL("out-of-table offsets must be kept");

  printf("m4a skip table: %u rows audited (%u drop, %u mem)\n",
         CGBA_SH4_M4A_SKIP_ROWS, drops, mems);
  return 0;
}
