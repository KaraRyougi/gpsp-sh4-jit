/*
 * sh4_cond_audit.c — host verification that sh4g_cond_to_T() emits SH4 that
 * leaves T = (ARM condition satisfied) for EVERY ARM condition code across all
 * 16 NZCV flag combinations. This is the regression guard for the compound
 * condition fix (HI/LS/GE/LT/GT/LE need two-flag combinations, not one).
 *
 *   cc -std=c11 -Wall -Wextra -I. tests/sh4_cond_audit.c -o /tmp/sh4-cond
 *   /tmp/sh4-cond
 *
 * The emitted sequence is straight-line; we run it on a tiny interpreter that
 * models exactly the opcodes sh4g_cond_to_T can emit, then compare the final T
 * to the architectural reference. CPSR is read via MOV.L @(R0,base) — the one
 * memory access — which we service from a fake guest reg[] array.
 */

#include <stdint.h>
typedef uint8_t  u8;  typedef uint16_t u16; typedef uint32_t u32;
typedef int8_t   s8;  typedef int16_t  s16; typedef int32_t  s32;
typedef uint64_t u64; typedef int64_t  s64;

#include "ports/fxcg100/sh4/sh4_emit_glue.h"

#include <stdio.h>

/* ---- architectural ARM condition reference (T = condition holds) ---------- */
static int arm_cond(unsigned cc, int N, int Z, int C, int V)
{
  switch (cc & 0xF) {
  case 0x0: return  Z;                 /* EQ */
  case 0x1: return !Z;                 /* NE */
  case 0x2: return  C;                 /* CS/HS */
  case 0x3: return !C;                 /* CC/LO */
  case 0x4: return  N;                 /* MI */
  case 0x5: return !N;                 /* PL */
  case 0x6: return  V;                 /* VS */
  case 0x7: return !V;                 /* VC */
  case 0x8: return  C && !Z;           /* HI */
  case 0x9: return !C ||  Z;           /* LS */
  case 0xA: return  N == V;            /* GE */
  case 0xB: return  N != V;            /* LT */
  case 0xC: return !Z && (N == V);     /* GT */
  case 0xD: return  Z || (N != V);     /* LE */
  case 0xE: return  1;                 /* AL */
  default:  return  0;                 /* NV (0xF) */
  }
}

/* ---- minimal SH4 interpreter for the opcodes sh4g_cond_to_T emits --------- */
static u32 g_reg[64];          /* fake guest reg[]; reg[16] = CPSR */

/* Returns final T after executing n bytes of straight-line SH4 at code. */
static int run_sh4(const u8 *code, size_t n, int *ok)
{
  u32 R[16] = {0};
  int T = 0;
  R[SH4_REG_BASE] = 0;         /* base is virtual; the load is intercepted */
  R[SH4_REG_CPSR] = g_reg[SH4_GREG_CPSR]; /* runtime contract: R8 = CPSR */
  *ok = 1;

  for (size_t i = 0; i + 1 < n; i += 2) {
    u16 op = (u16)((code[i] << 8) | code[i + 1]);
    unsigned nn = (op >> 8) & 0xF, mm = (op >> 4) & 0xF;

    if      (op == 0x0009) { /* NOP  */ }
    else if (op == 0x0018) { T = 1; }                              /* SETT */
    else if (op == 0x0008) { T = 0; }                              /* CLRT */
    else if ((op & 0xF000) == 0xE000)                              /* MOV #imm,Rn */
      R[nn] = (u32)(s32)(s8)(op & 0xFF);
    else if ((op & 0xFF00) == 0x8800)                              /* CMP/EQ #imm,R0 */
      T = ((s32)R[0] == (s32)(s8)(op & 0xFF));
    else if ((op & 0xFF00) == 0xC900)                              /* AND #imm,R0 */
      R[0] &= (u32)(op & 0xFF);
    else if ((op & 0xF00F) == 0x6003) R[nn] = R[mm];               /* MOV Rm,Rn */
    else if ((op & 0xF00F) == 0x6007) R[nn] = ~R[mm];              /* NOT Rm,Rn */
    else if ((op & 0xF00F) == 0x2009) R[nn] &= R[mm];              /* AND Rm,Rn */
    else if ((op & 0xF00F) == 0x200B) R[nn] |= R[mm];              /* OR  Rm,Rn */
    else if ((op & 0xF00F) == 0x200A) R[nn] ^= R[mm];              /* XOR Rm,Rn */
    else if ((op & 0xF00F) == 0x000E) {                            /* MOV.L @(R0,Rm),Rn */
      if (mm != SH4_REG_BASE) { *ok = 0; return 0; }
      R[nn] = g_reg[R[0] >> 2];
    }
    else if ((op & 0xF00F) == 0x400D) {                            /* SHLD Rm,Rn */
      s32 s = (s32)R[mm];
      if (s >= 0)                  R[nn] <<= (s & 0x1F);
      else if ((s & 0x1F) == 0)    R[nn]  = 0;
      else                         R[nn] >>= (32 - (s & 0x1F));    /* logical */
    }
    else if ((op & 0xF0FF) == 0x4000) {                            /* SHLL Rn */
      T = (int)((R[nn] >> 31) & 1); R[nn] <<= 1;
    }
    else if ((op & 0xF0FF) == 0x4008) R[nn] <<= 2;                 /* SHLL2 Rn */
    else if ((op & 0xF0FF) == 0x4011) T = ((s32)R[nn] >= 0);       /* CMP/PZ Rn */
    else { *ok = 0; return 0; }                                    /* unmodeled op */
  }
  return T;
}

/* ---- conditional-skip reach test ----------------------------------------- *
 * sh4g_emit_cond_skip_far must let the condition-FALSE path reach a post-body
 * target at ANY distance (the [P1] bug: a disp8/disp12 skip silently wraps once
 * the predicated run is large). Emit the skip, patch, and confirm the reach:
 * within BRA disp12 range the patcher chain-converts the site to a direct
 * BRA + delay NOP landing on the target; past that range it stores the full
 * 32-bit target in the far jump's literal. Also confirm the shared patcher
 * still fixes a disp8 BF (the bounded Thumb path). */
static int test_cond_skip(void)
{
  static _Alignas(32) u8 buf[16384];
  size_t body_bytes[] = { 8, 300, 4096, 9000 };   /* incl. > disp8 and > disp12 */
  int fail = 0;

  for (unsigned t = 0; t < sizeof body_bytes / sizeof body_bytes[0]; t++) {
    u8 *p = buf;
    sh4g_cond_to_T(&p, 0x0 /*EQ*/);
    u8 *bt = p;
    u8 *site = sh4g_emit_cond_skip_far(&p);
    u8 *body = p;

    u16 btop = (u16)((bt[0] << 8) | bt[1]);       /* BT must hop to the body */
    u8 *bt_target = bt + 4 + (signed char)bt[1] * 2;
    if ((btop & 0xFF00) != 0x8900) { fprintf(stderr, "skip: BT missing\n"); fail = 1; }
    if (bt_target != body) { fprintf(stderr, "skip: BT misses body\n"); fail = 1; }

    for (size_t i = 0; i < body_bytes[t]; i += 2) sh4g_u16(&p, 0x0009);
    u8 *target = p;
    sh4g_patch_cond_skip(site, target);

    long disp = ((long)(uintptr_t)target - ((long)(uintptr_t)site + 4)) / 2;
    if (disp >= -2048 && disp <= 2047) {
      /* near: chain-converted to BRA disp12 + delay-slot NOP */
      u16 bra = (u16)((site[0] << 8) | site[1]);
      u16 slot = (u16)((site[2] << 8) | site[3]);
      long d12 = (long)(bra & 0x0FFF);
      if (d12 >= 0x800) d12 -= 0x1000;            /* sign-extend disp12 */
      u8 *bra_target = site + 4 + d12 * 2;
      if ((bra & 0xF000) != 0xA000) {
        fprintf(stderr, "skip: body=%zu near patch is not a BRA (%04X)\n",
                body_bytes[t], bra);
        fail = 1;
      }
      if (bra_target != target) {
        fprintf(stderr, "skip: body=%zu BRA misses target\n", body_bytes[t]);
        fail = 1;
      }
      if (slot != 0x0009) {
        fprintf(stderr, "skip: body=%zu BRA delay slot not a NOP (%04X)\n",
                body_bytes[t], slot);
        fail = 1;
      }
    } else {
      /* far: full 32-bit target in the literal */
      u8 *lit = (u8 *)(((uintptr_t)(site + 6) + 3u) & ~(uintptr_t)3u);
      uint32_t stored = ((uint32_t)lit[0] << 24) | ((uint32_t)lit[1] << 16) |
                        ((uint32_t)lit[2] << 8) | lit[3];
      if (stored != (uint32_t)(uintptr_t)target) {
        fprintf(stderr, "skip: body=%zu far target truncated (%08X != %08X)\n",
                body_bytes[t], stored, (uint32_t)(uintptr_t)target);
        fail = 1;
      }
    }
  }

  {                                               /* shared patcher still does disp8 BF */
    u8 *p = buf;
    u8 *bf = sh4g_emit_bf_placeholder(&p);
    for (int i = 0; i < 20; i++) sh4g_u16(&p, 0x0009);
    u8 *target = p;
    sh4g_patch_cond_skip(bf, target);
    if (((bf[0] << 8 | bf[1]) & 0xFF00) != 0x8B00) { fprintf(stderr, "skip: BF clobbered\n"); fail = 1; }
    if (bf + 4 + (signed char)bf[1] * 2 != target) { fprintf(stderr, "skip: BF disp8 wrong\n"); fail = 1; }
  }

  if (!fail)
    printf("SH4 conditional-skip reach test passed (disp8 BF + far jump past disp12)\n");
  return fail;
}

int main(void)
{
  static _Alignas(32) u8 code[128];
  int fail = 0, checks = 0;
  static const char *name[16] = {
    "EQ","NE","CS","CC","MI","PL","VS","VC",
    "HI","LS","GE","LT","GT","LE","AL","NV"
  };

  for (unsigned cc = 0; cc < 16; cc++) {
    for (int f = 0; f < 16; f++) {
      int N = (f >> 3) & 1, Z = (f >> 2) & 1, C = (f >> 1) & 1, V = f & 1;
      g_reg[SH4_GREG_CPSR] = ((u32)N << 31) | ((u32)Z << 30) |
                             ((u32)C << 29) | ((u32)V << 28);

      u8 *p = code;
      sh4g_cond_to_T(&p, cc);
      size_t n = (size_t)(p - code);

      int ok = 0;
      int got = run_sh4(code, n, &ok);
      int want = arm_cond(cc, N, Z, C, V);
      checks++;

      if (!ok) {
        fprintf(stderr, "%s: emitted an opcode the test interpreter does not model\n",
                name[cc]);
        fail = 1;
        break;
      }
      if (got != want) {
        fprintf(stderr,
          "%s NZCV=%d%d%d%d: T=%d expected %d\n",
          name[cc], N, Z, C, V, got, want);
        fail = 1;
      }
    }
  }

  if (test_cond_skip()) fail = 1;

  if (fail) { fprintf(stderr, "SH4 condition audit FAILED\n"); return 1; }
  printf("SH4 condition audit passed (%d cases: 16 conds x 16 NZCV)\n", checks);
  return 0;
}
