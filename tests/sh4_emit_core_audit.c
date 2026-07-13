/*
 * sh4_emit_core_audit.c — host verification of the SH4 literal-pool manager and
 * guest reg[] addressing helpers (sh4_emit_core.h).
 *
 *   cc -std=c11 -Wall -Wextra -I. tests/sh4_emit_core_audit.c -o /tmp/sh4-core
 *   /tmp/sh4-core
 *
 * Checks, with no hardware:
 *   - small constants use MOV #imm8 (no pool slot),
 *   - large constants resolve to a 4-byte-aligned pool entry whose big-endian
 *     value matches, by decoding each MOV.L @(disp,PC) exactly as the CPU would,
 *   - equal constants share one pool slot,
 *   - guest reg load/store pick @(disp,base) vs @(R0,base) correctly.
 */

#include "ports/fxcg100/sh4/sh4_emit_core.h"

#include <stdio.h>
#include <string.h>

/* 32-byte-aligned code buffer so (base+site)&~3 == base + (site&~3). */
static _Alignas(32) uint8_t code[4096];

static int fail;

static uint16_t rd16(size_t off)
{
  return (uint16_t)((code[off] << 8) | code[off + 1]);
}

static uint32_t rd32be(size_t off)
{
  return ((uint32_t)code[off] << 24) | ((uint32_t)code[off + 1] << 16) |
         ((uint32_t)code[off + 2] << 8) | (uint32_t)code[off + 3];
}

/* Resolve a MOV.L @(disp,PC),Rn at byte offset `site` to the loaded value. */
static uint32_t resolve_pc_load(size_t site)
{
  uint16_t op = rd16(site);
  if ((op & 0xF000) != 0xD000) {
    fprintf(stderr, "site %zu is not MOV.L @(disp,PC): %04x\n", site, op);
    fail = 1;
    return 0;
  }
  unsigned disp = op & 0xFF;
  size_t target = ((site & ~(size_t)3) + 4) + disp * 4;
  return rd32be(target);
}

static void check_u32(const char *what, uint32_t got, uint32_t want)
{
  if (got != want) {
    fprintf(stderr, "%s: got %08x want %08x\n", what, got, want);
    fail = 1;
  }
}

int main(void)
{
  memset(code, 0xCC, sizeof(code));

  sh4_codegen cg = { code, code + sizeof(code), 0 };
  sh4_emitter e;
  sh4_emit_init(&e, &cg);

  /* --- literal pool --- */
  size_t s_big1 = sh4_emit_pos(&e);
  sh4_emit_load_imm32(&e, 0x12345678u, 1);   /* pool */
  size_t s_small = sh4_emit_pos(&e);
  sh4_emit_load_imm32(&e, 5u, 2);            /* MOV #5,r2 (no pool) */
  size_t s_big2 = sh4_emit_pos(&e);
  sh4_emit_load_imm32(&e, 0x12345678u, 3);   /* pool, dedup with s_big1 */
  size_t s_big3 = sh4_emit_pos(&e);
  sh4_emit_load_imm32(&e, 0xDEADBEEFu, 4);   /* pool */
  size_t s_negsmall = sh4_emit_pos(&e);
  sh4_emit_load_imm32(&e, (uint32_t)-3, 6);  /* MOV #-3,r6 (no pool) */

  /* a few filler instructions to push the pool further from the loads */
  for (int i = 0; i < 8; i++)
    sh4_emit_nop(&cg);

  sh4_emit_flush_pool(&e);

  if (e.error) { fprintf(stderr, "emitter error flag set\n"); return 1; }
  if (cg.overflow) { fprintf(stderr, "codegen overflow\n"); return 1; }

  /* small constants must be MOV #imm8 (0xE000), not pool loads */
  if ((rd16(s_small) & 0xF0FF) != (0xE000 | 5))
    { fprintf(stderr, "small const not MOV #imm8: %04x\n", rd16(s_small)); fail = 1; }
  if ((rd16(s_negsmall) & 0xF000) != 0xE000)
    { fprintf(stderr, "neg small const not MOV #imm8: %04x\n", rd16(s_negsmall)); fail = 1; }

  /* large constants must resolve to their values */
  check_u32("big1", resolve_pc_load(s_big1), 0x12345678u);
  check_u32("big2(dedup)", resolve_pc_load(s_big2), 0x12345678u);
  check_u32("big3", resolve_pc_load(s_big3), 0xDEADBEEFu);

  /* dedup: big1 and big2 must point at the SAME pool entry */
  {
    unsigned d1 = rd16(s_big1) & 0xFF, d2 = rd16(s_big2) & 0xFF;
    size_t t1 = ((s_big1 & ~(size_t)3) + 4) + d1 * 4;
    size_t t2 = ((s_big2 & ~(size_t)3) + 4) + d2 * 4;
    if (t1 != t2) { fprintf(stderr, "dedup failed: %zu vs %zu\n", t1, t2); fail = 1; }
  }

  /* register destinations encoded correctly (rn in bits 8..11) */
  if (((rd16(s_big1) >> 8) & 0xF) != 1) { fprintf(stderr, "big1 rn wrong\n"); fail = 1; }
  if (((rd16(s_big3) >> 8) & 0xF) != 4) { fprintf(stderr, "big3 rn wrong\n"); fail = 1; }

  /* --- guest reg[] addressing --- */
  {
    sh4_codegen rg = { code + 2048, code + sizeof(code), 0 };
    uint8_t *b = rg.ptr;

    sh4_emit_load_greg(&rg, 0, 3);    /* guest r0 cache -> MOV r11,r3      */
    sh4_emit_store_greg(&rg, 2, 0);   /* guest r0 cache <- MOV r2,r11      */
    sh4_emit_load_greg(&rg, 5, 1);    /* r5: off 20 -> MOV.L @(5,base),r1  */
    sh4_emit_load_greg(&rg, 15, 2);   /* PC: off 60 -> MOV.L @(15,base),r2 */
    sh4_emit_store_greg(&rg, 3, 16);  /* CPSR: register-cached -> MOV r3,r8 */
    sh4_emit_load_greg(&rg, 16, 2);   /* CPSR read -> MOV r8,r2 */
    sh4_emit_store_greg(&rg, 3, 17);  /* CPU_MODE: off 68 -> @(R0,base), R0=68 */

    uint16_t l0  = (uint16_t)((b[0] << 8) | b[1]);   /* MOV r11,r3 */
    uint16_t st0 = (uint16_t)((b[2] << 8) | b[3]);   /* MOV r2,r11 */
    uint16_t l5  = (uint16_t)((b[4] << 8) | b[5]);
    uint16_t l15 = (uint16_t)((b[6] << 8) | b[7]);
    uint16_t stc = (uint16_t)((b[8] << 8) | b[9]);   /* MOV r3,r8 */
    uint16_t ldc = (uint16_t)((b[10] << 8) | b[11]); /* MOV r8,r2 */
    uint16_t mov = (uint16_t)((b[12] << 8) | b[13]); /* MOV #68,r0 */
    uint16_t st  = (uint16_t)((b[14] << 8) | b[15]); /* MOV.L r3,@(R0,base) */

    /* MOV Rm,Rn = 0x6003 | rn<<8 | rm<<4. */
    if (l0 != (0x6003 | (3 << 8) | (SH4_REG_GUEST_R0 << 4)))
      { fprintf(stderr, "load_greg(0) mov from r11 wrong: %04x\n", l0); fail = 1; }
    if (st0 != (0x6003 | (SH4_REG_GUEST_R0 << 8) | (2 << 4)))
      { fprintf(stderr, "store_greg(0) mov to r11 wrong: %04x\n", st0); fail = 1; }
    /* MOV.L @(disp,Rm),Rn = 0x5000 | rn<<8 | rm<<4 | disp4 */
    if (l5 != (0x5000 | (1 << 8) | (SH4_REG_BASE << 4) | 5))
      { fprintf(stderr, "load_greg(5) wrong: %04x\n", l5); fail = 1; }
    if (l15 != (0x5000 | (2 << 8) | (SH4_REG_BASE << 4) | 15))
      { fprintf(stderr, "load_greg(15) wrong: %04x\n", l15); fail = 1; }
    /* MOV Rm,Rn = 0x6003 | rn<<8 | rm<<4 — CPSR lives in host R8 */
    if (stc != (0x6003 | (SH4_REG_CPSR << 8) | (3 << 4)))
      { fprintf(stderr, "store_greg(16) mov->r8 wrong: %04x\n", stc); fail = 1; }
    if (ldc != (0x6003 | (2 << 8) | (SH4_REG_CPSR << 4)))
      { fprintf(stderr, "load_greg(16) mov r8-> wrong: %04x\n", ldc); fail = 1; }
    if (mov != (0xE000 | (0 << 8) | 68))
      { fprintf(stderr, "store_greg(17) mov #68 wrong: %04x\n", mov); fail = 1; }
    /* MOV.L Rm,@(R0,Rn) = 0x0006 | rn<<8 | rm<<4 */
    if (st != (0x0006 | (SH4_REG_BASE << 8) | (3 << 4)))
      { fprintf(stderr, "store_greg(17) store wrong: %04x\n", st); fail = 1; }
  }

  if (fail) { fprintf(stderr, "EMIT CORE AUDIT FAILED\n"); return 1; }
  puts("SH4 emit-core audit passed");
  return 0;
}
