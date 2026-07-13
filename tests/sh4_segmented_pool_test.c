#include <stdint.h>
#include <stdio.h>

typedef uint8_t u8;
typedef uint32_t u32;

#define CGBA_SH4_SEGMENTED_LITERAL_POOL 1
#include "ports/fxcg100/sh4/sh4_emit_glue.h"

int cgba_sh4_extra_cycles;
u8 ws_cyc_seq[16][2];
u8 ws_cyc_nseq[16][2];

static uint32_t read_be32(const u8 *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

static const u8 *literal_for(const u8 *site)
{
  uintptr_t pcbase = ((uintptr_t)site & ~(uintptr_t)3) + 4;
  return (const u8 *)(pcbase + (uintptr_t)site[1] * 4u);
}

int main(void)
{
  static u8 code[2048];
  u8 *tp = code;
  u8 *a, *b, *c;
  const u8 *la, *lb, *lc;
  int i;

  sh4g_litseg_begin();
  a = tp;
  sh4g_const(&tp, 0x12345679u, SH4_REG_T0);
  for (i = 0; i < 40; i++)
    sh4g_u16(&tp, 0x0009);
  b = tp;
  sh4g_const(&tp, 0x12345679u, SH4_REG_T1);
  for (i = 0; i < 320; i++)
    sh4g_u16(&tp, 0x0009);
  sh4g_litseg_maybe_flush(&tp);

  c = tp;
  sh4g_const(&tp, 0x89ABCDEFu, SH4_REG_T2);
  sh4g_litseg_end(&tp);

  if ((a[0] & 0xF0) != 0xD0 || (b[0] & 0xF0) != 0xD0 ||
      (c[0] & 0xF0) != 0xD0) {
    fprintf(stderr, "segmented pool did not emit MOV.L references\n");
    return 1;
  }
  la = literal_for(a);
  lb = literal_for(b);
  lc = literal_for(c);
  if (la != lb || read_be32(la) != 0x12345679u) {
    fprintf(stderr, "within-segment constant was not deduplicated\n");
    return 1;
  }
  if (lc == la || read_be32(lc) != 0x89ABCDEFu) {
    fprintf(stderr, "second segment literal is invalid\n");
    return 1;
  }
  if (la <= b || (size_t)(la - a) > 1024u ||
      lc <= c || (size_t)(lc - c) > 1024u) {
    fprintf(stderr, "literal exceeded forward MOV.L reach\n");
    return 1;
  }
  if ((size_t)(tp - code) >= sizeof code) {
    fprintf(stderr, "segmented pool test buffer overflow\n");
    return 1;
  }

  puts("SH4 segmented literal pool passed");
  return 0;
}
