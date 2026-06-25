#include "ports/fxcg100/sh4/sh4_codegen.h"

#include <stdio.h>
#include <string.h>

static int check_bytes(const uint8_t *actual, const uint8_t *expected,
                       size_t size)
{
  size_t i;

  if (!memcmp(actual, expected, size))
    return 0;

  fprintf(stderr, "SH4 codegen mismatch\n");
  for (i = 0; i < size; i++) {
    if (actual[i] != expected[i]) {
      fprintf(stderr, "  byte %zu: got %02x expected %02x\n",
              i, actual[i], expected[i]);
    }
  }

  return 1;
}

int main(void)
{
  uint8_t buffer[64];
  sh4_codegen cg = { buffer, buffer + sizeof(buffer), 0 };
  uint32_t pc;

  memset(buffer, 0xCC, sizeof(buffer));

  sh4_emit_mov_reg(&cg, 4, 0);
  sh4_emit_add_reg(&cg, 5, 0);
  sh4_emit_rts(&cg);
  sh4_emit_nop(&cg);
  sh4_emit_mov_imm(&cg, 127, 0);
  sh4_emit_mov_imm(&cg, -1, 1);
  sh4_emit_add_imm(&cg, 5, 2);
  sh4_emit_mov_l_load_disp(&cg, 4, 3, 3);
  sh4_emit_mov_l_store_disp(&cg, 3, 4, 2);
  sh4_emit_mov_l_load(&cg, 4, 3);
  sh4_emit_mov_l_store(&cg, 3, 4);

  pc = (uint32_t)sh4_offset(&cg, buffer);
  sh4_emit_bra(&cg, sh4_branch_disp12(pc, 0));
  sh4_emit_nop(&cg);

  pc = (uint32_t)sh4_offset(&cg, buffer);
  sh4_emit_bsr(&cg, sh4_branch_disp12(pc, 0));
  sh4_emit_nop(&cg);

  pc = (uint32_t)sh4_offset(&cg, buffer);
  sh4_emit_bt(&cg, sh4_branch_disp8(pc, 0));

  pc = (uint32_t)sh4_offset(&cg, buffer);
  sh4_emit_bf(&cg, sh4_branch_disp8(pc, 0));

  pc = (uint32_t)sh4_offset(&cg, buffer);
  sh4_emit_bt_s(&cg, sh4_branch_disp8(pc, 0));

  pc = (uint32_t)sh4_offset(&cg, buffer);
  sh4_emit_bf_s(&cg, sh4_branch_disp8(pc, 0));

  {
    const uint8_t expected[] = {
      0x60, 0x43, 0x30, 0x5C, 0x00, 0x0B, 0x00, 0x09,
      0xE0, 0x7F, 0xE1, 0xFF, 0x72, 0x05, 0x53, 0x43,
      0x14, 0x32, 0x63, 0x42, 0x24, 0x32, 0xAF, 0xF3,
      0x00, 0x09, 0xBF, 0xF1, 0x00, 0x09, 0x89, 0xEF,
      0x8B, 0xEE, 0x8D, 0xED, 0x8F, 0xEC
    };

    if (cg.overflow)
      return 2;

    if (sh4_offset(&cg, buffer) != sizeof(expected))
      return 3;

    if (check_bytes(buffer, expected, sizeof(expected)))
      return 4;
  }

  puts("SH4 codegen smoke test passed");
  return 0;
}
