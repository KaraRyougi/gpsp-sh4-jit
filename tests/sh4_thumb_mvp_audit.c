/*
 * sh4_thumb_mvp_audit.c — host verification that the reference Thumb -> SH-4A
 * data-processing translator (sh4_thumb_mvp.h) emits valid SH4 and the right
 * core operation per Thumb opcode.
 *
 *   cc -std=c11 -Wall -Wextra -I. tests/sh4_thumb_mvp_audit.c -o /tmp/sh4-thumb
 *   /tmp/sh4-thumb            # structural checks
 *   /tmp/sh4-thumb dump | sh-elf-objdump -b binary -m sh4 -EB -D -   # disasm
 *
 * Each opcode is translated into a fresh buffer; we then confirm the translator
 * accepted it, emitted no errors, and produced at least one host instruction
 * matching the expected SH4 operation (masking out register fields).
 */

#include "ports/fxcg100/sh4/sh4_thumb_mvp.h"

#include <stdio.h>
#include <string.h>

static _Alignas(32) uint8_t code[512];
static int fail;

/* Translate one Thumb opcode, flush, and require a host op matching mask/val. */
static void check(const char *name, uint16_t thumb, uint16_t mask, uint16_t val)
{
  sh4_codegen cg = { code, code + sizeof(code), 0 };
  sh4_emitter e;
  sh4_emit_init(&e, &cg);

  int handled = sh4_translate_thumb(&e, thumb);
  sh4_emit_flush_pool(&e);

  if (!handled) { fprintf(stderr, "%s: not handled\n", name); fail = 1; return; }
  if (e.error || cg.overflow) { fprintf(stderr, "%s: emit error\n", name); fail = 1; return; }

  size_t n = (size_t)(cg.ptr - code);
  int found = 0;
  for (size_t i = 0; i + 1 < n; i += 2) {
    uint16_t w = (uint16_t)((code[i] << 8) | code[i + 1]);
    if ((w & mask) == val) { found = 1; break; }
  }
  if (!found)
    { fprintf(stderr, "%s: expected op %04x/%04x not emitted\n", name, val, mask); fail = 1; }
}

static void dump(void)
{
  /* Emit a small Thumb program and write the raw SH4 to stdout for objdump. */
  sh4_codegen cg = { code, code + sizeof(code), 0 };
  sh4_emitter e;
  sh4_emit_init(&e, &cg);

  sh4_translate_thumb(&e, 0x2005);  /* MOV  r0, #5      */
  sh4_translate_thumb(&e, 0x1888);  /* ADD  r0, r1, r2  */
  sh4_translate_thumb(&e, 0x1A88);  /* SUB  r0, r1, r2  */
  sh4_translate_thumb(&e, 0x4008);  /* AND  r0, r1      */
  sh4_translate_thumb(&e, 0x00C8);  /* LSL  r0, r1, #3  */
  sh4_emit_flush_pool(&e);

  fwrite(code, 1, (size_t)(cg.ptr - code), stdout);
}

int main(int argc, char **argv)
{
  if (argc > 1 && strcmp(argv[1], "dump") == 0) { dump(); return 0; }

  memset(code, 0xCC, sizeof(code));

  /* SH4 op masks (register fields masked out). */
  check("MOV #5",       0x2005, 0xF0FF, 0xE005);  /* MOV #5,Rn          */
  check("ADD reg",      0x1888, 0xF00F, 0x300C);  /* ADD Rm,Rn          */
  check("SUB reg",      0x1A88, 0xF00F, 0x3008);  /* SUB Rm,Rn          */
  check("ADD #imm3",    0x1DC8, 0xF00F, 0x300C);  /* (I=1) ADD Rm,Rn    */
  check("AND reg",      0x4008, 0xF00F, 0x2009);  /* AND Rm,Rn          */
  check("EOR reg",      0x4048, 0xF00F, 0x200A);  /* XOR Rm,Rn          */
  check("ORR reg",      0x4308, 0xF00F, 0x200B);  /* OR  Rm,Rn          */
  check("MVN reg",      0x43C8, 0xF00F, 0x6007);  /* NOT Rm,Rn          */
  check("LSL #3",       0x00C8, 0xF00F, 0x400D);  /* SHLD Rm,Rn         */
  check("LSR #3",       0x08C8, 0xF00F, 0x400D);  /* SHLD Rm,Rn (neg)   */
  check("ASR #4",       0x1108, 0xF00F, 0x400C);  /* SHAD Rm,Rn         */
  check("CMP #imm8",    0x2805, 0xF00F, 0x3008);  /* SUB for flags      */

  /* set_nz pattern must appear (SHLD by 30 for the Z bit) -> MOV #30,R0. */
  check("flags MOV #30", 0x2005, 0xF0FF, 0xE000 | 30);

  /* opcode outside the MVP subset is declined (interpreter fallback). */
  {
    sh4_codegen cg = { code, code + sizeof(code), 0 };
    sh4_emitter e; sh4_emit_init(&e, &cg);
    if (sh4_translate_thumb(&e, 0xDF00)) {        /* SWI */
      fprintf(stderr, "SWI should not be handled by MVP\n"); fail = 1;
    }
  }

  if (fail) { fprintf(stderr, "THUMB MVP AUDIT FAILED\n"); return 1; }
  puts("SH4 Thumb MVP audit passed");
  return 0;
}
