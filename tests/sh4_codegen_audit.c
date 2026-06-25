/*
 * sh4_codegen_audit.c — byte-exact verification of the SH4 encoder against the
 * real GNU assembler (sh-elf-as). Requires sh-elf-as + sh-elf-objcopy on PATH.
 *
 *   cc -std=c11 -Wall -Wextra -I. tests/sh4_codegen_audit.c -o /tmp/sh4-audit
 *   /tmp/sh4-audit
 *
 * For every "diffable" instruction (register / immediate-R0 / control forms),
 * the encoder output is compared to what sh-elf-as produces for the same
 * mnemonic. PC-relative and branch-displacement forms (which the assembler will
 * not accept with raw displacements) are checked with explicit expected bytes.
 *
 * Distinct rm/rn operands are used throughout so n-vs-m field swaps are caught.
 */

#include "ports/fxcg100/sh4/sh4_codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t code[16384];
static sh4_codegen cg;
static char asmbuf[1 << 16];
static size_t asmlen;
static size_t off[4096];
static const char *txt[4096];
static int count;

static void addasm(const char *s)
{
  asmlen += (size_t)snprintf(asmbuf + asmlen, sizeof(asmbuf) - asmlen, "\t%s\n", s);
}

#define D(text, call)                                  \
  do {                                                 \
    off[count] = (size_t)(cg.ptr - code);              \
    call;                                              \
    addasm(text);                                      \
    txt[count] = (text);                               \
    count++;                                           \
  } while (0)

static int run_diff(void)
{
  FILE *f = fopen("/tmp/sh4_audit.s", "w");
  if (!f) { perror("fopen .s"); return 1; }
  fputs("\t.text\n", f);
  fwrite(asmbuf, 1, asmlen, f);
  fclose(f);

  int rc = system("sh-elf-as --isa=sh4a -big -o /tmp/sh4_audit.o /tmp/sh4_audit.s"
                  " && sh-elf-objcopy -O binary /tmp/sh4_audit.o /tmp/sh4_audit.bin");
  if (rc != 0) { fprintf(stderr, "assembler failed (rc=%d)\n", rc); return 1; }

  FILE *b = fopen("/tmp/sh4_audit.bin", "rb");
  if (!b) { perror("fopen .bin"); return 1; }
  static uint8_t ref[16384];
  size_t refn = fread(ref, 1, sizeof(ref), b);
  fclose(b);

  size_t got = (size_t)(cg.ptr - code);
  int fail = 0;

  if (refn != got) {
    fprintf(stderr, "length mismatch: encoder=%zu assembler=%zu\n", got, refn);
    fail = 1;
  }

  size_t n = refn < got ? refn : got;
  for (size_t i = 0; i < n; i++) {
    if (code[i] != ref[i]) {
      int j = 0;
      while (j + 1 < count && off[j + 1] <= i) j++;
      fprintf(stderr, "byte %zu: encoder=%02x assembler=%02x  [%s]\n",
              i, code[i], ref[i], txt[j]);
      fail = 1;
    }
  }

  if (!fail)
    printf("diff vs sh-elf-as: %d instructions, %zu bytes — OK\n", count, got);
  return fail;
}

/* Explicit-byte check for forms the assembler won't take with raw operands. */
static int expect(const char *name, const uint8_t *p, size_t len,
                  const uint8_t *want)
{
  if (memcmp(p, want, len) == 0) return 0;
  fprintf(stderr, "explicit check failed: %s\n", name);
  for (size_t i = 0; i < len; i++)
    fprintf(stderr, "  byte %zu: got %02x want %02x\n", i, p[i], want[i]);
  return 1;
}

int main(void)
{
  cg.ptr = code;
  cg.limit = code + sizeof(code);
  cg.overflow = 0;

  /* ---- data move ---- */
  D("mov r1, r2", sh4_emit_mov_reg(&cg, 1, 2));
  D("mov #-5, r3", sh4_emit_mov_imm(&cg, -5, 3));
  D("mov #127, r4", sh4_emit_mov_imm(&cg, 127, 4));
  D("movt r5", sh4_emit_movt(&cg, 5));
  D("swap.b r1, r2", sh4_emit_swap_b(&cg, 1, 2));
  D("swap.w r3, r4", sh4_emit_swap_w(&cg, 3, 4));
  D("xtrct r5, r6", sh4_emit_xtrct(&cg, 5, 6));
  D("extu.b r7, r8", sh4_emit_extu_b(&cg, 7, 8));
  D("extu.w r9, r10", sh4_emit_extu_w(&cg, 9, 10));
  D("exts.b r11, r12", sh4_emit_exts_b(&cg, 11, 12));
  D("exts.w r13, r14", sh4_emit_exts_w(&cg, 13, 14));

  /* ---- arithmetic ---- */
  D("add r1, r2", sh4_emit_add_reg(&cg, 1, 2));
  D("add #-1, r3", sh4_emit_add_imm(&cg, -1, 3));
  D("addc r4, r5", sh4_emit_addc(&cg, 4, 5));
  D("addv r6, r7", sh4_emit_addv(&cg, 6, 7));
  D("sub r8, r9", sh4_emit_sub(&cg, 8, 9));
  D("subc r10, r11", sh4_emit_subc(&cg, 10, 11));
  D("subv r12, r13", sh4_emit_subv(&cg, 12, 13));
  D("neg r1, r2", sh4_emit_neg(&cg, 1, 2));
  D("negc r3, r4", sh4_emit_negc(&cg, 3, 4));
  D("dt r5", sh4_emit_dt(&cg, 5));

  /* ---- compares ---- */
  D("cmp/eq r1, r2", sh4_emit_cmpeq(&cg, 1, 2));
  D("cmp/hs r3, r4", sh4_emit_cmphs(&cg, 3, 4));
  D("cmp/ge r5, r6", sh4_emit_cmpge(&cg, 5, 6));
  D("cmp/hi r7, r8", sh4_emit_cmphi(&cg, 7, 8));
  D("cmp/gt r9, r10", sh4_emit_cmpgt(&cg, 9, 10));
  D("cmp/str r11, r12", sh4_emit_cmpstr(&cg, 11, 12));
  D("cmp/pz r13", sh4_emit_cmppz(&cg, 13));
  D("cmp/pl r14", sh4_emit_cmppl(&cg, 14));
  D("cmp/eq #-3, r0", sh4_emit_cmpeq_imm(&cg, -3));

  /* ---- multiply / divide ---- */
  D("mul.l r1, r2", sh4_emit_mul_l(&cg, 1, 2));
  D("dmuls.l r3, r4", sh4_emit_dmuls_l(&cg, 3, 4));
  D("dmulu.l r5, r6", sh4_emit_dmulu_l(&cg, 5, 6));
  D("div0s r7, r8", sh4_emit_div0s(&cg, 7, 8));
  D("div0u", sh4_emit_div0u(&cg));
  D("div1 r9, r10", sh4_emit_div1(&cg, 9, 10));

  /* ---- logic ---- */
  D("and r1, r2", sh4_emit_and(&cg, 1, 2));
  D("or r3, r4", sh4_emit_or(&cg, 3, 4));
  D("xor r5, r6", sh4_emit_xor(&cg, 5, 6));
  D("tst r7, r8", sh4_emit_tst(&cg, 7, 8));
  D("not r9, r10", sh4_emit_not(&cg, 9, 10));
  D("and #240, r0", sh4_emit_and_imm(&cg, 240));
  D("or #15, r0", sh4_emit_or_imm(&cg, 15));
  D("xor #170, r0", sh4_emit_xor_imm(&cg, 170));
  D("tst #5, r0", sh4_emit_tst_imm(&cg, 5));

  /* ---- shift / rotate ---- */
  D("shad r1, r2", sh4_emit_shad(&cg, 1, 2));
  D("shld r3, r4", sh4_emit_shld(&cg, 3, 4));
  D("shll r5", sh4_emit_shll(&cg, 5));
  D("shlr r6", sh4_emit_shlr(&cg, 6));
  D("shal r7", sh4_emit_shal(&cg, 7));
  D("shar r8", sh4_emit_shar(&cg, 8));
  D("shll2 r9", sh4_emit_shll2(&cg, 9));
  D("shlr2 r10", sh4_emit_shlr2(&cg, 10));
  D("shll8 r11", sh4_emit_shll8(&cg, 11));
  D("shlr8 r12", sh4_emit_shlr8(&cg, 12));
  D("shll16 r13", sh4_emit_shll16(&cg, 13));
  D("shlr16 r14", sh4_emit_shlr16(&cg, 14));
  D("rotl r1", sh4_emit_rotl(&cg, 1));
  D("rotr r2", sh4_emit_rotr(&cg, 2));
  D("rotcl r3", sh4_emit_rotcl(&cg, 3));
  D("rotcr r4", sh4_emit_rotcr(&cg, 4));

  /* ---- memory: register indirect ---- */
  D("mov.b @r1, r2", sh4_emit_mov_b_load(&cg, 1, 2));
  D("mov.w @r3, r4", sh4_emit_mov_w_load(&cg, 3, 4));
  D("mov.l @r5, r6", sh4_emit_mov_l_load(&cg, 5, 6));
  D("mov.b r7, @r8", sh4_emit_mov_b_store(&cg, 7, 8));
  D("mov.w r9, @r10", sh4_emit_mov_w_store(&cg, 9, 10));
  D("mov.l r11, @r12", sh4_emit_mov_l_store(&cg, 11, 12));
  D("mov.l @r1+, r2", sh4_emit_mov_l_load_inc(&cg, 1, 2));
  D("mov.l r3, @-r4", sh4_emit_mov_l_store_dec(&cg, 3, 4));

  /* ---- memory: @(disp,Rn) (byte offset = field*4) ---- */
  D("mov.l @(12,r3), r4", sh4_emit_mov_l_load_disp(&cg, 3, 4, 3));
  D("mov.l r5, @(8,r6)", sh4_emit_mov_l_store_disp(&cg, 5, 6, 2));

  /* ---- memory: @(R0,Rn) indexed ---- */
  D("mov.b @(r0,r1), r2", sh4_emit_mov_b_load_r0(&cg, 1, 2));
  D("mov.w @(r0,r3), r4", sh4_emit_mov_w_load_r0(&cg, 3, 4));
  D("mov.l @(r0,r5), r6", sh4_emit_mov_l_load_r0(&cg, 5, 6));
  D("mov.b r7, @(r0,r8)", sh4_emit_mov_b_store_r0(&cg, 7, 8));
  D("mov.w r9, @(r0,r10)", sh4_emit_mov_w_store_r0(&cg, 9, 10));
  D("mov.l r11, @(r0,r12)", sh4_emit_mov_l_store_r0(&cg, 11, 12));

  /* ---- memory: GBR-relative (R0) ---- */
  D("mov.b @(7,gbr), r0", sh4_emit_mov_b_load_gbr(&cg, 7));
  D("mov.w @(10,gbr), r0", sh4_emit_mov_w_load_gbr(&cg, 5));   /* 5*2 = 10 */
  D("mov.l @(20,gbr), r0", sh4_emit_mov_l_load_gbr(&cg, 5));   /* 5*4 = 20 */
  D("mov.b r0, @(3,gbr)", sh4_emit_mov_b_store_gbr(&cg, 3));
  D("mov.w r0, @(8,gbr)", sh4_emit_mov_w_store_gbr(&cg, 4));   /* 4*2 = 8 */
  D("mov.l r0, @(16,gbr)", sh4_emit_mov_l_store_gbr(&cg, 4));  /* 4*4 = 16 */

  /* ---- control transfer (register / no-disp) ---- */
  D("jmp @r5", sh4_emit_jmp(&cg, 5));
  D("nop", sh4_emit_nop(&cg));
  D("jsr @r6", sh4_emit_jsr(&cg, 6));
  D("nop", sh4_emit_nop(&cg));
  D("braf r7", sh4_emit_braf(&cg, 7));
  D("nop", sh4_emit_nop(&cg));
  D("bsrf r8", sh4_emit_bsrf(&cg, 8));
  D("nop", sh4_emit_nop(&cg));
  D("rts", sh4_emit_rts(&cg));
  D("nop", sh4_emit_nop(&cg));

  /* ---- system / no-operand ---- */
  D("sett", sh4_emit_sett(&cg));
  D("clrt", sh4_emit_clrt(&cg));
  D("clrmac", sh4_emit_clrmac(&cg));

  /* ---- system registers ---- */
  D("ldc r1, gbr", sh4_emit_ldc_gbr(&cg, 1));
  D("stc gbr, r2", sh4_emit_stc_gbr(&cg, 2));
  D("lds r3, pr", sh4_emit_lds_pr(&cg, 3));
  D("sts pr, r4", sh4_emit_sts_pr(&cg, 4));
  D("lds.l @r5+, pr", sh4_emit_lds_pr_inc(&cg, 5));
  D("sts.l pr, @-r6", sh4_emit_sts_pr_dec(&cg, 6));
  D("sts macl, r7", sh4_emit_sts_macl(&cg, 7));
  D("sts mach, r8", sh4_emit_sts_mach(&cg, 8));
  D("lds r9, macl", sh4_emit_lds_macl(&cg, 9));
  D("lds r10, mach", sh4_emit_lds_mach(&cg, 10));

  if (cg.overflow) { fprintf(stderr, "encoder buffer overflow\n"); return 2; }

  int fail = run_diff();

  /* ---- explicit checks: PC-relative + branch displacement encoders ---- */
  {
    sh4_codegen e = { code + 8192, code + sizeof(code), 0 };
    uint8_t *p = e.ptr;

    /* mov.w @(2*2+pc),r3 ; field=2 -> 0x93 0x02 */
    sh4_emit_mov_w_load_pc(&e, 2, 3);
    /* mov.l @(4*4+pc),r5 ; field=4 -> 0xD5 0x04 */
    sh4_emit_mov_l_load_pc(&e, 4, 5);
    /* mova @(6*4+pc),r0 ; field=6 -> 0xC7 0x06 */
    sh4_emit_mova(&e, 6);
    /* bra disp=0 -> 0xA0 0x00 ; bsr disp=0 -> 0xB0 0x00 */
    sh4_emit_bra(&e, 0);
    sh4_emit_bsr(&e, 0);
    /* bt disp=-2 -> 0x89 0xFE ; bf disp=4 -> 0x8B 0x04 */
    sh4_emit_bt(&e, -2);
    sh4_emit_bf(&e, 4);
    /* bt/s disp=1 -> 0x8D 0x01 ; bf/s disp=2 -> 0x8F 0x02 */
    sh4_emit_bt_s(&e, 1);
    sh4_emit_bf_s(&e, 2);

    static const uint8_t want[] = {
      0x93, 0x02, 0xD5, 0x04, 0xC7, 0x06,
      0xA0, 0x00, 0xB0, 0x00,
      0x89, 0xFE, 0x8B, 0x04,
      0x8D, 0x01, 0x8F, 0x02,
    };
    fail |= expect("pc-relative + branch displacement", p, sizeof(want), want);
  }

  /* ---- branch displacement helper math ---- */
  {
    /* bra at pc=0x100 to target 0x108: disp = (0x108-(0x100+4))/2 = 2 */
    if (sh4_branch_disp12(0x100, 0x108) != 2) {
      fprintf(stderr, "disp12 helper wrong\n"); fail = 1;
    }
    /* bt at pc=0x200 to 0x1FC: disp = (0x1FC-(0x204))/2 = -4 */
    if (sh4_branch_disp8(0x200, 0x1FC) != -4) {
      fprintf(stderr, "disp8 helper wrong\n"); fail = 1;
    }
    if (!sh4_disp8_fits(127) || sh4_disp8_fits(128) ||
        !sh4_disp12_fits(-2048) || sh4_disp12_fits(2048)) {
      fprintf(stderr, "disp fit helpers wrong\n"); fail = 1;
    }
  }

  if (fail) { fprintf(stderr, "AUDIT FAILED\n"); return 1; }
  puts("SH4 codegen audit passed");
  return 0;
}
