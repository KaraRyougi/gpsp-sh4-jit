/*
 * sh4_native_ldst_audit.c — host checks for native SH4 load/store fast paths.
 * Side-effecting stores must stay on the C helper path. Plain Thumb RAM stores
 * and block stores may emit native guarded code as long as the generated path
 * still falls back for non-RAM and SMC-tagged writes.
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
#define CGBA_SH4_THUMB_BLOCK_NATIVE 1
#include "ports/fxcg100/sh4/sh4_thumb_dp_emit.h"
#include "ports/fxcg100/sh4/sh4_thumb_block_emit.h"
#include "ports/fxcg100/sh4/sh4_arm_ldst_emit.h"
#include "ports/fxcg100/sh4/sh4_arm_block_emit.h"

u8 *memory_map_read[0x2000];
u8 iwram[1024 * 32 * 2];
u16 io_registers[512];
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

int cgba_sh4_thumb_block(u32 opcode, u32 pc)
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

void sh4_helper_exit(u32 pc)
{
  (void)pc;
}

void sh4_indirect_branch_thumb(u32 address)
{
  (void)address;
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
    fprintf(stderr, "%s: native path accepted fallback opcode\n", name);
    fail = 1;
  }
  if (p != code) {
    fprintf(stderr, "%s: fallback opcode emitted %ld bytes\n",
            name, (long)(p - code));
    fail = 1;
  }
}

static void expect_thumb_native_transfer(const char *name, u32 opcode)
{
  u8 *p = code;
  memset(code, 0xCC, sizeof(code));

  if (!sh4g_thumb_ldst_native(&p, opcode, 0x08000000, 1)) {
    fprintf(stderr, "%s: native path rejected transfer opcode\n", name);
    fail = 1;
  }
  if (p == code) {
    fprintf(stderr, "%s: native transfer emitted no code\n", name);
    fail = 1;
  }
}

static void expect_thumb_const_io_load(const char *name, u32 opcode, u32 address)
{
  u8 *p = code;
  u32 const_val[16] = {0};
  memset(code, 0xCC, sizeof(code));
  const_val[0] = address;

  if (!sh4g_thumb_ldst_const_native(&p, opcode, 1u << 0, const_val)) {
    fprintf(stderr, "%s: const IO path rejected load opcode\n", name);
    fail = 1;
  }
  if (p == code) {
    fprintf(stderr, "%s: const IO load emitted no code\n", name);
    fail = 1;
  }
}

static void expect_thumb_const_io_fallback(const char *name, u32 opcode,
                                           u32 address)
{
  u8 *p = code;
  u32 const_val[16] = {0};
  memset(code, 0xCC, sizeof(code));
  const_val[0] = address;

  if (sh4g_thumb_ldst_const_native(&p, opcode, 1u << 0, const_val)) {
    fprintf(stderr, "%s: const IO path accepted fallback opcode\n", name);
    fail = 1;
  }
  if (p != code) {
    fprintf(stderr, "%s: const IO fallback emitted %ld bytes\n",
            name, (long)(p - code));
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

static void expect_thumb_block_fallback(const char *name, u32 opcode)
{
  u8 *p = code;
  memset(code, 0xCC, sizeof(code));

  if (sh4g_thumb_block_native(&p, opcode, 0x08000000, 1)) {
    fprintf(stderr, "%s: native path accepted fallback opcode\n", name);
    fail = 1;
  }
  if (p != code) {
    fprintf(stderr, "%s: fallback opcode emitted %ld bytes\n",
            name, (long)(p - code));
    fail = 1;
  }
}

static void expect_thumb_block_native_transfer(const char *name, u32 opcode)
{
  u8 *p = code;
  memset(code, 0xCC, sizeof(code));

  if (!sh4g_thumb_block_native(&p, opcode, 0x08000000, 1)) {
    fprintf(stderr, "%s: native path rejected transfer opcode\n", name);
    fail = 1;
  }
  if (p == code) {
    fprintf(stderr, "%s: native transfer emitted no code\n", name);
    fail = 1;
  }
}

int main(void)
{
  expect_arm_single_fallback("STR r1,[r0]",  0xE5801000u);
  expect_arm_single_fallback("STRB r1,[r0]", 0xE5C01000u);
  expect_arm_single_fallback("STRH r1,[r0]", 0xE1C010B0u);
  expect_arm_single_native_load("LDR r1,[r0]", 0xE5901000u);

  expect_thumb_native_transfer("LDR r0,[pc,#0]", 0x4800u);
  expect_thumb_native_transfer("LDRB r1,[r0,#0]", 0x7801u);
  expect_thumb_native_transfer("STR r1,[r0,#0]", 0x6001u);
  expect_thumb_native_transfer("STRB r1,[r0,#0]", 0x7001u);
  expect_thumb_native_transfer("STRH r1,[r0,#0]", 0x8001u);
  expect_thumb_fallback("non-ldst Thumb opcode", 0xDF00u);
  expect_thumb_const_io_load("const LDRH r1,[r0,#0] KEYINPUT", 0x8801u,
                             0x04000130u);
  expect_thumb_const_io_load("const LDRB r1,[r0,#0] IO", 0x7801u,
                             0x04000130u);
  expect_thumb_const_io_fallback("const STR r1,[r0,#0]", 0x6001u,
                                 0x04000130u);
  expect_thumb_const_io_fallback("const LDRH r1,[r0,#0] non-IO", 0x8801u,
                                 0x03000130u);

  expect_arm_block_fallback("STMIA r0,{r1,r2}", 0xE8800006u);
  expect_arm_block_native_load("LDMIA r0,{r1,r2}", 0xE8900006u);

  expect_thumb_block_native_transfer("PUSH {r0,r1}", 0xB403u);
  expect_thumb_block_native_transfer("PUSH {lr}", 0xB500u);
  expect_thumb_block_native_transfer("STMIA r0!,{r1,r2}", 0xC006u);
  expect_thumb_block_native_transfer("POP {r0,r1}", 0xBC03u);
  expect_thumb_block_native_transfer("POP {r0,pc}", 0xBD01u);
  expect_thumb_block_native_transfer("LDMIA r0!,{r1,r2}", 0xC806u);
  expect_thumb_block_fallback("non-block Thumb opcode", 0xDF00u);

  if (fail) {
    fprintf(stderr, "SH4 native ldst audit failed\n");
    return 1;
  }
  puts("SH4 native ldst audit passed");
  return 0;
}
