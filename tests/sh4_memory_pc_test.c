/*
 * Regression coverage for the SH4 JIT's generic memory-helper PC contract.
 *
 * Translated blocks keep the live guest PC in host state, but gpSP's generic
 * memory core consults reg[REG_PC] for BIOS protection and open-bus reads.
 * These tests link the production helper implementation and deliberately seed
 * a stale BIOS-resident reg[REG_PC]. Every helper must publish the PC value
 * that the interpreter would expose before it calls read_memory*().
 */

#include <stdio.h>
#include <string.h>

#include "vendor/gpsp/common.h"

u32 reg[64];
u32 reg_mode[7][7];
u32 spsr[6];
u8 ws_cyc_seq[16][2];
u8 ws_cyc_nseq[16][2];
u16 io_registers[512];
u8 ewram[1024 * 256 * 2] = {0};
u8 iwram[1024 * 32 * 2] = {0};
dma_transfer_type dma[DMA_CHAN_CNT];

const u32 cpu_modes[16] = {0};
const u32 cpsr_masks[4][2] = {{0}};
const u32 spsr_masks[4] = {0};

enum memory_model {
  MEMORY_BIOS_PROTECTED,
  MEMORY_OPEN_BUS
};

static enum memory_model active_model;
static u32 observed_pc;
static u32 observed_address;
static unsigned read_calls;
static unsigned write_calls;
static cpu_alert_type write_alert;

static u32 protected_bios_value(u32 pc)
{
  return 0xB1050000u ^ pc;
}

static u32 open_bus_value(u32 pc)
{
  return 0x0B050000u ^ pc;
}

static u32 modeled_read(u32 address)
{
  observed_pc = reg[REG_PC];
  observed_address = address;
  read_calls++;

  if (active_model == MEMORY_BIOS_PROTECTED) {
    /* A stale PC below 0x4000 leaks BIOS; a correctly published JIT PC does
     * not. Distinct values make that architectural distinction observable. */
    return reg[REG_PC] < 0x4000u ? 0x1EA7B105u
                                : protected_bios_value(reg[REG_PC]);
  }

  /* gpSP's real open-bus path selects data from the instruction stream using
   * reg[REG_PC]. Encoding the same input directly keeps this test focused on
   * the helper/publication boundary rather than duplicating the memory core. */
  return open_bus_value(reg[REG_PC]);
}

u32 function_cc read_memory8(u32 address)
{
  return modeled_read(address) & 0xFFu;
}

u32 function_cc read_memory8s(u32 address)
{
  return (u32)(s32)(s8)modeled_read(address);
}

u32 function_cc read_memory16(u32 address)
{
  return modeled_read(address) & 0xFFFFu;
}

u16 function_cc read_memory16_signed(u32 address)
{
  return (u16)modeled_read(address);
}

u32 function_cc read_memory16s(u32 address)
{
  return (u32)(s32)(s16)modeled_read(address);
}

u32 function_cc read_memory32(u32 address)
{
  return modeled_read(address);
}

cpu_alert_type function_cc write_memory8(u32 address, u8 value)
{
  (void)address;
  (void)value;
  write_calls++;
  return write_alert;
}

cpu_alert_type function_cc write_memory16(u32 address, u16 value)
{
  (void)address;
  (void)value;
  write_calls++;
  return write_alert;
}

cpu_alert_type function_cc write_memory32(u32 address, u32 value)
{
  (void)address;
  (void)value;
  write_calls++;
  return write_alert;
}

void flush_translation_cache_ram(void)
{
}

void set_cpu_mode(cpu_mode_type new_mode)
{
  reg[CPU_MODE] = new_mode;
}

int cgba_sh4_thumb_ldst(u32 opcode, u32 pc);
int cgba_sh4_thumb_block(u32 opcode, u32 pc);
int cgba_sh4_arm_ldst(u32 opcode, u32 pc);
int cgba_sh4_arm_block(u32 opcode, u32 pc);
int cgba_sh4_arm_swap(u32 opcode, u32 pc);

typedef int (*memory_helper)(u32 opcode, u32 pc);

typedef struct helper_case {
  const char *name;
  memory_helper helper;
  u32 opcode;
  u32 pc;
  u32 published_pc;
  unsigned expected_writes;
} helper_case;

static int failures;

#define CHECK(case_name, condition, fmt, ...) do {                           \
  if (!(condition)) {                                                        \
    fprintf(stderr, "%s: " fmt "\n", (case_name), __VA_ARGS__);             \
    failures++;                                                              \
  }                                                                          \
} while (0)

static void run_case(const helper_case *test, enum memory_model model)
{
  const char *model_name = model == MEMORY_BIOS_PROTECTED ? "BIOS" : "open-bus";
  u32 address = model == MEMORY_BIOS_PROTECTED ? 0x00000100u : 0x10000000u;
  u32 expected_value = model == MEMORY_BIOS_PROTECTED
    ? protected_bios_value(test->published_pc)
    : open_bus_value(test->published_pc);
  int result;

  memset(reg, 0, sizeof(reg));
  memset(iwram, 0, sizeof(iwram));
  memset(ewram, 0, sizeof(ewram));
  reg[1] = address;
  reg[2] = 0x12345678u;
  reg[REG_PC] = 0x00000100u; /* deliberately stale and BIOS-resident */
  observed_pc = 0;
  observed_address = 0;
  read_calls = 0;
  write_calls = 0;
  write_alert = CPU_ALERT_NONE;
  active_model = model;

  result = test->helper(test->opcode, test->pc);

  CHECK(test->name, result == 0, "%s helper returned %d (expected 0)",
        model_name, result);
  CHECK(test->name, read_calls == 1, "%s performed %u reads (expected 1)",
        model_name, read_calls);
  CHECK(test->name, write_calls == test->expected_writes,
        "%s performed %u writes (expected %u)", model_name, write_calls,
        test->expected_writes);
  CHECK(test->name, observed_address == address,
        "%s read address %08X (expected %08X)", model_name,
        observed_address, address);
  CHECK(test->name, observed_pc == test->published_pc,
        "%s read saw reg[PC]=%08X (expected %08X)", model_name,
        observed_pc, test->published_pc);
  CHECK(test->name, reg[0] == expected_value,
        "%s loaded %08X (expected %08X)", model_name, reg[0],
        expected_value);
}

static void test_timing_alert_redispatch(void)
{
  const char *name = "Thumb WAITCNT redispatch";
  const u32 pc = 0x08015000u;
  int result;

  memset(reg, 0, sizeof(reg));
  reg[0] = 0x0014u;
  reg[1] = 0x04000204u;
  reg[REG_PC] = 0x00000100u;
  write_calls = 0;
  write_alert = CPU_ALERT_TIMING;

  /* STRH r0,[r1,#0]: the real WAITCNT writer has already flushed both JIT
   * caches when it returns this alert. The helper must abandon the old block
   * by pure redispatch, without entering an extra scheduler pass. */
  result = cgba_sh4_thumb_ldst(0x00008008u, pc);

  CHECK(name, result == 1, "helper returned %d (expected redispatch=1)",
        result);
  CHECK(name, write_calls == 1, "performed %u writes (expected 1)",
        write_calls);
  CHECK(name, reg[REG_PC] == pc + 2u,
        "published next PC %08X (expected %08X)", reg[REG_PC], pc + 2u);
  write_alert = CPU_ALERT_NONE;
}

int main(void)
{
  static const helper_case cases[] = {
    { "Thumb LDR",   cgba_sh4_thumb_ldst,  0x00006808u, 0x08010000u,
      0x08010002u, 0 }, /* LDR r0,[r1,#0] */
    { "Thumb LDMIA", cgba_sh4_thumb_block, 0x0000C901u, 0x08011000u,
      0x08011002u, 0 }, /* LDMIA r1!,{r0} */
    { "ARM LDR",     cgba_sh4_arm_ldst,    0xE5910000u, 0x08012000u,
      0x08012004u, 0 }, /* LDR r0,[r1] */
    { "ARM LDMIA",   cgba_sh4_arm_block,   0xE8B10001u, 0x08013000u,
      0x08013004u, 0 }, /* LDMIA r1!,{r0} */
    { "ARM SWP",     cgba_sh4_arm_swap,    0xE1010092u, 0x08014000u,
      0x08014000u, 1 }, /* SWP r0,r2,[r1] */
  };
  unsigned i;

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    run_case(&cases[i], MEMORY_BIOS_PROTECTED);
    run_case(&cases[i], MEMORY_OPEN_BUS);
  }
  test_timing_alert_redispatch();

  if (failures)
    return 1;

  puts("SH4 memory-helper PC regressions passed");
  return 0;
}
