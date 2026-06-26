/* Static cycle audit: the SH4 dynarec's GBA-cycle charge vs the interpreter's
 * per-instruction formula, per instruction class. No emulator, no ROM, no SH4
 * execution -- both cycle models are deterministic, so divergences are found by
 * comparing the two formulas over synthetic blocks, not by grinding an emulator.
 *
 * It separates two kinds of divergence:
 *   - EMIT gap (REAL): a difference in how the SH4 backend emits memory / branch
 *     / refill charges. This is the fxcg100 dynarec's own accounting and MUST be
 *     zero; a nonzero EMIT gap fails the audit.
 *   - FETCH gap (TOLERATED): the dynarec charges each instruction's fetch from
 *     def_seq_cycles[OWN-pc] (arm_base_cycles, cpu_threaded.c:2793 -- GENERIC
 *     gpSP, every backend) while the interpreter uses the live ws_cyc_seq[POST-pc]
 *     (cpu.cc:3095). They differ for ROM/gamepak once a game speeds up WAITCNT,
 *     and at region boundaries. This divergence is INHERENT to stock gpSP's
 *     dynarec (which boots games anyway), so it is reported but not a failure --
 *     it means "bit-exact vs the interpreter" is a stricter bar than "boots".
 *
 * Interpreter (oracle), vendor/gpsp/cpu.cc:
 *   - per instruction (skip_instruction 3095/3600): ws_cyc_seq[POST-pc][word?1:0]
 *   - single ld/st (862/887): + ws_cyc_nseq[data][word?1:0]
 *   - LDM/STM per reg (902/922): + ws_cyc_seq[data][1]
 *   - taken B/BL/BX-to-ARM (3060/3070/2140): + ws_cyc_nseq[target][word?1:0]
 *   - BX-to-Thumb (2134 goto thumb_loop): no refill, no post-pc fetch.
 *
 * Dynarec, vendor/gpsp/cpu_threaded.c + vendor/gpsp/sh4/sh4_emit.h + the helpers:
 *   - per instruction: def_seq_cycles[OWN-pc][word?1:0]
 *   - memory (cgba_sh4_charge_mem, sh4_interp_helpers.c:100; native
 *     sh4g_charge_mem_run): ws_cyc[region][size_index], seq for block / nseq for
 *     single -- mirrors the interpreter.
 *   - taken refill (generate_branch_cycle_update sh4_emit.h:173): ws_cyc_nseq
 *     [target][word?1:0]; BX via sh4g_charge_indirect_refill (ARM target only).
 */
#include <stdio.h>
#include <stdint.h>

typedef uint8_t  u8;
typedef uint32_t u32;

/* ---- gpSP cycle tables (vendor/gpsp/gba_memory.c:279-340) ---------------- */
/* [region][bus width: 0 = 8/16-bit, 1 = 32-bit]. RAM rows are fixed; the
 * gamepak rows (0x8..0xD) are filled by reload_timing_info() from WAITCNT. */
static u8 ws_seq[16][2] = {
  {1,1},{1,1},{3,6},{1,1},{1,1},{1,2},{1,2},{1,2},
  {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{1,1},{1,1},
};
static u8 ws_nseq[16][2] = {
  {1,1},{1,1},{3,6},{1,1},{1,1},{1,2},{1,2},{1,2},
  {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{1,1},{1,1},
};
/* The DEFAULT (baked-at-translate) fetch table the dynarec uses, == ws_cyc at
 * WAITCNT=0 (the slowest gamepak timing). Never tracks a sped-up WAITCNT. */
static const u8 def_seq[16][2] = {
  {1,1},{1,1},{3,6},{1,1},{1,1},{1,2},{1,2},{1,2},
  {3,6},{3,6},{5,9},{5,9},{9,17},{9,17},{1,1},{1,1},
};
static const u8 ws012_nonseq[] = {4,3,2,8};
static const u8 ws0_seq[] = {2,1}, ws1_seq[] = {4,1}, ws2_seq[] = {8,1};

/* reload_timing_info() (gba_memory.c:436): fill gamepak rows from a WAITCNT. */
static void reload_timing(u32 waitcnt)
{
  ws_seq[0x8][0]=ws_seq[0x9][0]=1+ws0_seq[(waitcnt>>4)&1];
  ws_seq[0xA][0]=ws_seq[0xB][0]=1+ws1_seq[(waitcnt>>7)&1];
  ws_seq[0xC][0]=ws_seq[0xD][0]=1+ws2_seq[(waitcnt>>10)&1];
  for(int i=0x8;i<=0xD;i++) ws_seq[i][1]=ws_seq[i][0]*2;
  ws_nseq[0x8][0]=ws_nseq[0x9][0]=1+ws012_nonseq[(waitcnt>>2)&3];
  ws_nseq[0xA][0]=ws_nseq[0xB][0]=1+ws012_nonseq[(waitcnt>>5)&3];
  ws_nseq[0xC][0]=ws_nseq[0xD][0]=1+ws012_nonseq[(waitcnt>>8)&3];
  for(int i=0x8;i<=0xD;i++) ws_nseq[i][1]=1+ws_nseq[i][0]+ws_seq[i][0];
}

#define REGION(addr) (((addr) >> 24) & 0xF)

enum kind { K_DP, K_LD, K_ST, K_LDM, K_STM, K_B, K_BX_ARM, K_BX_THUMB };
typedef struct {
  u32 pc; int kind; u32 data; int width; int nregs; u32 target;
} insn;
typedef struct { int thumb; int n; insn ins[16]; u32 dpc; } block;

static int fcol(int thumb) { return thumb ? 0 : 1; }

/* Interpreter mem/branch charge (cpu.cc rules), the oracle. */
static int interp_mb(const insn *in, int col)
{
  switch (in->kind) {
    case K_LD: case K_ST:   return ws_nseq[REGION(in->data)][in->width];
    case K_LDM: case K_STM: return in->nregs * ws_seq[REGION(in->data)][1];
    case K_B: case K_BX_ARM:return ws_nseq[REGION(in->target)][in->kind==K_B?col:1];
    default:                return 0;  /* DP, BX_THUMB: none */
  }
}
/* Dynarec mem/branch charge, transcribed from the SH4 backend (cgba_sh4_charge_
 * mem / sh4g_charge_mem_run / generate_branch_cycle_update / sh4g_charge_
 * indirect_refill). Equal to the oracle for correct code; `inject` reproduces a
 * historical backend bug so the audit's detection can be self-validated. */
enum bug { BUG_NONE, BUG_B1_BLOCK_FREE, BUG_REFILL_WRONG_COL };
static enum bug g_inject;
static int dyn_mb(const insn *in, int col)
{
  switch (in->kind) {
    case K_LD: case K_ST:   return ws_nseq[REGION(in->data)][in->width];
    case K_LDM: case K_STM:
      if (g_inject == BUG_B1_BLOCK_FREE) return 0;   /* pre-B1: native path charged nothing */
      return in->nregs * ws_seq[REGION(in->data)][1];
    case K_B: case K_BX_ARM:
      if (g_inject == BUG_REFILL_WRONG_COL) return ws_nseq[REGION(in->target)][0];
      return ws_nseq[REGION(in->target)][in->kind==K_B?col:1];
    default:                return 0;
  }
}
/* interpreter fetch model: POST-pc region, live ws_cyc_seq; BX->Thumb takes none */
static int interp_fetch(const block *b, int i, int col)
{
  const insn *in = &b->ins[i];
  if (in->kind == K_BX_THUMB) return 0;
  u32 next = (i+1 < b->n) ? b->ins[i+1].pc : b->dpc;
  return ws_seq[REGION(next)][col];
}
/* dynarec fetch model: OWN-pc region, baked def_seq; BX cancels its own fetch */
static int dyn_fetch(const block *b, int i, int col)
{
  const insn *in = &b->ins[i];
  if (in->kind == K_BX_ARM || in->kind == K_BX_THUMB) return 0;
  return def_seq[REGION(in->pc)][col];
}

static int n_pass, n_real, scenario_waitcnt;
static void check(const char *name, const block *b)
{
  int col = fcol(b->thumb);
  int dynf = 0, intf = 0, imb = 0, dmb = 0;
  for (int i = 0; i < b->n; i++) {
    intf += interp_fetch(b, i, col);
    dynf += dyn_fetch(b, i, col);
    imb  += interp_mb(&b->ins[i], col);
    dmb  += dyn_mb(&b->ins[i], col);
  }
  int interp = intf + imb;            /* interpreter total                    */
  int dyn    = dynf + dmb;            /* dynarec total                        */
  int fetch_gap = dynf - intf;        /* tolerated: gpSP-generic fetch model  */
  int emit_gap  = dmb  - imb;         /* REAL: SH4 backend memory/branch error */
  if (emit_gap) n_real++; else n_pass++;
  const char *tag = emit_gap ? "REAL-BUG" : (fetch_gap ? "tolerated" : "ok");
  printf("  [%-9s] %-34s interp=%-3d dynarec=%-3d  fetch_gap=%+d  emit_gap=%+d%s\n",
         tag, name, interp, dyn, fetch_gap, emit_gap,
         emit_gap ? "  <== backend bug" : "");
}

static void run_suite(void)
{
  printf("\n-- WAITCNT=0x%04X (%s) --\n", scenario_waitcnt,
         scenario_waitcnt ? "game-set, fast ROM" : "reset, slow ROM");

  check("IWRAM blk, branch->IWRAM (B1)", &(block){.thumb=0,.n=4,.dpc=0x03000BE8,.ins={
    {.pc=0x20,.kind=K_STM,.data=0x03007FA0,.nregs=6},{.pc=0x24,.kind=K_DP},
    {.pc=0x28,.kind=K_DP},{.pc=0x2C,.kind=K_LD,.data=0x03FFFFFC,.width=1}}});

  check("IWRAM blk, B->ROM", &(block){.thumb=0,.n=3,.dpc=0x08000100,.ins={
    {.pc=0x03000100,.kind=K_DP},{.pc=0x03000104,.kind=K_DP},
    {.pc=0x03000108,.kind=K_B,.target=0x08000100}}});

  check("ROM-resident blk (3 dp)", &(block){.thumb=0,.n=3,.dpc=0x08000200,.ins={
    {.pc=0x08000100,.kind=K_DP},{.pc=0x08000104,.kind=K_DP},
    {.pc=0x08000108,.kind=K_B,.target=0x08000200}}});

  check("IWRAM blk, EWRAM word load", &(block){.thumb=0,.n=2,.dpc=0x03000300,.ins={
    {.pc=0x03000200,.kind=K_LD,.data=0x02000000,.width=1},
    {.pc=0x03000204,.kind=K_B,.target=0x03000300}}});

  check("IWRAM blk, ROM word load", &(block){.thumb=0,.n=2,.dpc=0x03000400,.ins={
    {.pc=0x03000300,.kind=K_LD,.data=0x08123456,.width=1},
    {.pc=0x03000304,.kind=K_B,.target=0x03000400}}});

  check("IWRAM blk, BX->Thumb(ROM) (B3)", &(block){.thumb=0,.n=2,.dpc=0x08000400,.ins={
    {.pc=0x03000300,.kind=K_DP},{.pc=0x03000304,.kind=K_BX_THUMB,.target=0x08000400}}});

  check("IWRAM blk, BX->ARM(ROM)", &(block){.thumb=0,.n=2,.dpc=0x08000500,.ins={
    {.pc=0x03000400,.kind=K_DP},{.pc=0x03000404,.kind=K_BX_ARM,.target=0x08000500}}});

  check("Thumb blk in IWRAM (pop+b)", &(block){.thumb=1,.n=2,.dpc=0x03000500,.ins={
    {.pc=0x03000400,.kind=K_LDM,.data=0x03007F00,.nregs=3},
    {.pc=0x03000402,.kind=K_B,.target=0x03000500}}});

  check("Thumb-resident ROM blk", &(block){.thumb=1,.n=3,.dpc=0x08000600,.ins={
    {.pc=0x08000400,.kind=K_DP},{.pc=0x08000402,.kind=K_DP},
    {.pc=0x08000404,.kind=K_B,.target=0x08000600}}});
}

/* Self-validation: inject a known historical backend bug and confirm the audit
 * flags it REAL-BUG. Proves the emit_gap check actually detects mis-charges. */
static int self_test(void)
{
  int fails = 0;
  /* branch target in EWRAM ({3,6}) so a wrong bus-width column is visible
   * (in IWRAM both columns are 1 and the bug would hide). */
  block blk = {.thumb=0,.n=2,.dpc=0x02000100,.ins={
    {.pc=0x03000000,.kind=K_STM,.data=0x03007F00,.nregs=6},
    {.pc=0x03000004,.kind=K_B,.target=0x02000100}}};
  struct { enum bug b; const char *name; } cases[] = {
    { BUG_B1_BLOCK_FREE,     "native block transfer charges no wait-states (pre-B1)" },
    { BUG_REFILL_WRONG_COL,  "branch refill uses the wrong bus-width column" },
  };
  for (unsigned i = 0; i < sizeof cases/sizeof cases[0]; i++) {
    g_inject = cases[i].b;
    int col = 1, imb = 0, dmb = 0;
    for (int k = 0; k < blk.n; k++) { imb += interp_mb(&blk.ins[k],col); dmb += dyn_mb(&blk.ins[k],col); }
    int detected = (dmb - imb) != 0;
    printf("  [%s] would detect: %s\n", detected ? "OK  " : "MISS", cases[i].name);
    if (!detected) fails++;
  }
  g_inject = BUG_NONE;
  return fails;
}

int main(void)
{
  printf("== SH4 dynarec static cycle audit ==\n");
  printf("REAL-BUG = SH4 backend mem/branch mis-charge (must be zero).\n"
         "tolerated = gpSP-generic def_seq[own-pc] vs live ws_cyc[post-pc] fetch\n"
         "model -- also present in stock gpSP's dynarec, which boots games anyway,\n"
         "so this is NOT a bug: it means \"bit-exact vs the interpreter\" is a\n"
         "stricter bar than \"boots correctly\".\n");

  scenario_waitcnt = 0x0000; reload_timing(scenario_waitcnt); run_suite();
  scenario_waitcnt = 0x4014; reload_timing(scenario_waitcnt); run_suite();  /* fast WS0 */

  printf("\n-- self-test: confirm the audit detects backend bugs --\n");
  int st = self_test();

  printf("\nModeled classes: ARM/Thumb fetch, single load/store, LDM/STM, B/BL,\n"
         "BX (ARM & Thumb targets), branch refill. NOT yet modeled (add here if a\n"
         "residual per-block gap appears): MSR/MRS, SWP, MUL/MLA internal cycles,\n"
         "SWI, conditional-fail instruction fetch accounting.\n");

  printf("\n== %d ok/tolerated, %d REAL backend bug(s); self-test %s ==\n",
         n_pass, n_real, st ? "FAILED" : "passed");
  return (n_real || st) ? 1 : 0;
}
