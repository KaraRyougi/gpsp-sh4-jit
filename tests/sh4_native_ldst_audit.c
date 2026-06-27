/*
 * sh4_native_ldst_audit.c — host checks for native SH4 load/store fast paths.
 * Stores must stay on the C helper path so SMC detection and store-raised alerts
 * cannot be bypassed when native load paths are enabled.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef uint64_t u64;
typedef int64_t  s64;

#define CGBA_SH4_ARM_LDST_NATIVE   1
#define CGBA_SH4_THUMB_LDST_NATIVE 1
#include "ports/fxcg100/sh4/sh4_thumb_dp_emit.h"
#include "ports/fxcg100/sh4/sh4_arm_ldst_emit.h"
#include "ports/fxcg100/sh4/sh4_arm_block_emit.h"

u8 *memory_map_read[0x2000];
int cgba_sh4_extra_cycles;
u8 ws_cyc_seq[16][2];
u8 ws_cyc_nseq[16][2];

int cgba_sh4_arm_ldst(u32 opcode, u32 pc)
{
  (void)opcode;
  (void)pc;
  return 0;
}

int cgba_sh4_thumb_ldst(u32 opcode, u32 pc)
{
  (void)opcode;
  (void)pc;
  return 0;
}

int cgba_sh4_arm_block(u32 opcode, u32 pc)
{
  (void)opcode;
  (void)pc;
  return 0;
}

void sh4_block_exit(u32 pc)
{
  (void)pc;
}

static _Alignas(32) u8 code[4096];
static int fail;

static void expect_arm_single_fallback(const char *name, u32 opcode)
{
  u8 *p = code;
  memset(code, 0xCC, sizeof(code));

  if (sh4g_arm_ldst_native(&p, opcode, 0x08000000, 1)) {
    fprintf(stderr, "%s: native path accepted store opcode\n", name);
    fail = 1;
  }
  if (p != code) {
    fprintf(stderr, "%s: fallback opcode emitted %ld bytes\n",
            name, (long)(p - code));
    fail = 1;
  }
}

static void expect_arm_single_native_load(const char *name, u32 opcode)
{
  u8 *p = code;
  memset(code, 0xCC, sizeof(code));

  if (!sh4g_arm_ldst_native(&p, opcode, 0x08000000, 1)) {
    fprintf(stderr, "%s: native path rejected load opcode\n", name);
    fail = 1;
  }
  if (p == code) {
    fprintf(stderr, "%s: native load emitted no code\n", name);
    fail = 1;
  }
}

static void expect_thumb_fallback(const char *name, u32 opcode)
{
  u8 *p = code;
  memset(code, 0xCC, sizeof(code));

  if (sh4g_thumb_ldst_native(&p, opcode, 0x08000000, 1)) {
    fprintf(stderr, "%s: native path accepted store opcode\n", name);
    fail = 1;
  }
  if (p != code) {
    fprintf(stderr, "%s: fallback opcode emitted %ld bytes\n",
            name, (long)(p - code));
    fail = 1;
  }
}

static void expect_thumb_native_load(const char *name, u32 opcode)
{
  u8 *p = code;
  memset(code, 0xCC, sizeof(code));

  if (!sh4g_thumb_ldst_native(&p, opcode, 0x08000000, 1)) {
    fprintf(stderr, "%s: native path rejected load opcode\n", name);
    fail = 1;
  }
  if (p == code) {
    fprintf(stderr, "%s: native load emitted no code\n", name);
    fail = 1;
  }
}

static void expect_arm_block_fallback(const char *name, u32 opcode)
{
  u8 *p = code;
  memset(code, 0xCC, sizeof(code));

  if (sh4g_arm_block_native(&p, opcode, 0x08000000, 1)) {
    fprintf(stderr, "%s: native path accepted store opcode\n", name);
    fail = 1;
  }
  if (p != code) {
    fprintf(stderr, "%s: fallback opcode emitted %ld bytes\n",
            name, (long)(p - code));
    fail = 1;
  }
}

static void expect_arm_block_native_load(const char *name, u32 opcode)
{
  u8 *p = code;
  memset(code, 0xCC, sizeof(code));

  if (!sh4g_arm_block_native(&p, opcode, 0x08000000, 1)) {
    fprintf(stderr, "%s: native path rejected load opcode\n", name);
    fail = 1;
  }
  if (p == code) {
    fprintf(stderr, "%s: native load emitted no code\n", name);
    fail = 1;
  }
}

int main(void)
{
  expect_arm_single_fallback("STR r1,[r0]",  0xE5801000u);
  expect_arm_single_fallback("STRB r1,[r0]", 0xE5C01000u);
  expect_arm_single_fallback("STRH r1,[r0]", 0xE1C010B0u);
  expect_arm_single_native_load("LDR r1,[r0]", 0xE5901000u);

  expect_thumb_fallback("STRB r1,[r0,#0]", 0x7001u);
  expect_thumb_native_load("LDRB r1,[r0,#0]", 0x7801u);

  expect_arm_block_fallback("STMIA r0,{r1,r2}", 0xE8800006u);
  expect_arm_block_native_load("LDMIA r0,{r1,r2}", 0xE8900006u);

  if (fail) {
    fprintf(stderr, "SH4 native ldst audit failed\n");
    return 1;
  }
  puts("SH4 native ldst audit passed");
  return 0;
}
