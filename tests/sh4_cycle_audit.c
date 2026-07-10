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
 *     ws_cyc_seq[OWN-pc] while the interpreter uses ws_cyc_seq[POST-pc]
 *     (cpu.cc:3095). They can still differ at region boundaries or for indirect
 *     branches whose target fetch is attributed to the next block, but ordinary
 *     ROM/gamepak fetches now track the game's live WAITCNT-derived timings.
 *
 * Interpreter (oracle), vendor/gpsp/cpu.cc:
 *   - per instruction (skip_instruction 3095/3600): ws_cyc_seq[POST-pc][word?1:0]
 *   - single ld/st (862/887): + ws_cyc_nseq[data][word?1:0]
 *   - LDM/STM per reg (902/922): + ws_cyc_seq[data][1]
 *   - Thumb conditional B, taken or not (1347): + ws_cyc_nseq[new_pc][0]
 *   - taken B/BL/BX-to-ARM (3060/3070/2140): + ws_cyc_nseq[target][word?1:0]
 *   - BX-to-Thumb (2134 goto thumb_loop): no refill, no post-pc fetch.
 *
 * Dynarec, vendor/gpsp/cpu_threaded.c + vendor/gpsp/sh4/sh4_emit.h + the helpers:
 *   - per instruction: ws_cyc_seq[OWN-pc][word?1:0]
 *   - memory (cgba_sh4_charge_mem, sh4_interp_helpers.c:100; native
 *     sh4g_charge_mem_run): ws_cyc[region][size_index], seq for block / nseq for
 *     single -- mirrors the interpreter.
 *   - taken refill (generate_branch_cycle_update sh4_emit.h:173): ws_cyc_nseq
 *     [target][word?1:0]; an exhausted internal-branch gate also charges the
 *     target sequential fetch before update_gba; BX via sh4g_charge_indirect_
 *     refill (ARM target only).
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

enum kind {
  K_DP, K_LD, K_ST, K_LDM, K_STM, K_B, K_BCOND_T, K_BCOND_NT,
  K_BX_ARM, K_BX_THUMB, K_TBX_THUMB, K_TMUL, K_AMUL, K_AMLA, K_AMULL,
  K_AMLAL, K_ASMULL, K_ASMLAL, K_TSWI, K_THLE_DIV
};
typedef struct {
  u32 pc; int kind; u32 data; int width; int nregs; u32 target;
} insn;
typedef struct { int thumb; int n; insn ins[16]; u32 dpc; } block;

static int fcol(int thumb) { return thumb ? 0 : 1; }

static int swi_dispatch_cycles(int sp_region, int lr_region, int thumb)
{
  int c = 19 * ws_seq[0][1] + 3 * ws_nseq[0][1];
  c += 8 * ws_seq[3][1];
  c += 6 * ws_seq[sp_region & 0xF][1];
  c += ws_nseq[lr_region & 0xF][0];
  if (!thumb)
    c += ws_seq[lr_region & 0xF][1];
  return c;
}

static int hle_div_cycles(const insn *in)
{
  int align = in->width;
  return swi_dispatch_cycles((int)in->data, REGION(in->target), 1) +
    (22 + 10 * align) * ws_seq[0][1] + (2 * align) * ws_nseq[0][1] - 1;
}

/* Interpreter mem/branch charge (cpu.cc rules), the oracle. */
static int interp_mb(const insn *in, int col)
{
  switch (in->kind) {
    case K_LD: case K_ST:   return ws_nseq[REGION(in->data)][in->width];
    case K_LDM: case K_STM: return in->nregs * ws_seq[REGION(in->data)][1];
    case K_B: case K_BCOND_T: case K_BCOND_NT:
      return ws_nseq[REGION(in->target)][col];
    case K_BX_ARM:
      return ws_nseq[REGION(in->target)][1] + ws_seq[REGION(in->target)][1];
    case K_THLE_DIV:
      return hle_div_cycles(in);
    default:                return 0;  /* DP, BX_THUMB: none */
  }
}
/* Dynarec mem/branch charge, transcribed from the SH4 backend (cgba_sh4_charge_
 * mem / sh4g_charge_mem_run / generate_branch_cycle_update / sh4g_charge_
 * indirect_refill). Equal to the oracle for correct code; `inject` reproduces a
 * historical backend bug so the audit's detection can be self-validated. */
enum bug {
  BUG_NONE,
  BUG_B1_BLOCK_FREE,
  BUG_REFILL_WRONG_COL,
  BUG_COND_NT_FREE,
  BUG_GATE_FETCH_FREE,
  BUG_THUMB_MUL_EXTRA,
  BUG_ARM_MUL_EXTRA,
  BUG_HLE_DIV_FLAT,
  BUG_THUMB_SWI_FETCH,
  BUG_ARM_BX_TARGET_FETCH_FREE
};
static enum bug g_inject;

static int dyn_mb(const insn *in, int col)
{
  switch (in->kind) {
    case K_LD: case K_ST:   return ws_nseq[REGION(in->data)][in->width];
    case K_LDM: case K_STM:
      if (g_inject == BUG_B1_BLOCK_FREE) return 0;   /* pre-B1: native path charged nothing */
      return in->nregs * ws_seq[REGION(in->data)][1];
    case K_B:
      if (g_inject == BUG_REFILL_WRONG_COL) return ws_nseq[REGION(in->target)][0];
      return ws_nseq[REGION(in->target)][col];
    case K_BCOND_T:
      return ws_nseq[REGION(in->target)][col];
    case K_BCOND_NT:
      if (g_inject == BUG_COND_NT_FREE) return 0;
      return ws_nseq[REGION(in->target)][col];
    case K_BX_ARM:
      if (g_inject == BUG_REFILL_WRONG_COL) return ws_nseq[REGION(in->target)][0];
      return ws_nseq[REGION(in->target)][1] +
        ((g_inject == BUG_ARM_BX_TARGET_FETCH_FREE) ? 0 : ws_seq[REGION(in->target)][1]);
    case K_TMUL:
      return (g_inject == BUG_THUMB_MUL_EXTRA) ? 2 : 0;
    case K_AMUL:  return (g_inject == BUG_ARM_MUL_EXTRA) ? 2 : 0;
    case K_AMLA:  return (g_inject == BUG_ARM_MUL_EXTRA) ? 3 : 0;
    case K_AMULL: return (g_inject == BUG_ARM_MUL_EXTRA) ? 3 : 0;
    case K_AMLAL: return (g_inject == BUG_ARM_MUL_EXTRA) ? 3 : 0;
    case K_ASMULL:return (g_inject == BUG_ARM_MUL_EXTRA) ? 2 : 0;
    case K_ASMLAL:return (g_inject == BUG_ARM_MUL_EXTRA) ? 3 : 0;
    case K_THLE_DIV:
      return (g_inject == BUG_HLE_DIV_FLAT) ? 64 : hle_div_cycles(in);
    default:                return 0;
  }
}

/* interpreter fetch model: POST-pc region, live ws_cyc_seq; BX->Thumb takes none */
static int interp_fetch(const block *b, int i, int col)
{
  const insn *in = &b->ins[i];
  if (in->kind == K_TSWI) return 0;
  if (in->kind == K_BX_ARM) return 0;
  if (in->kind == K_BX_THUMB) return 0;
  if (in->kind == K_TBX_THUMB) return ws_seq[REGION(in->target)][0];
  u32 next = (i+1 < b->n) ? b->ins[i+1].pc : b->dpc;
  return ws_seq[REGION(next)][col];
}
/* dynarec fetch model: OWN-pc region, live ws_cyc_seq; BX cancels its own fetch */
static int dyn_fetch(const block *b, int i, int col)
{
  const insn *in = &b->ins[i];
  if (in->kind == K_TSWI)
    return (g_inject == BUG_THUMB_SWI_FETCH) ? ws_seq[REGION(in->pc)][0] : 0;
  if (in->kind == K_BX_ARM || in->kind == K_BX_THUMB) return 0;
  if (in->kind == K_TBX_THUMB) return ws_seq[REGION(in->target)][0];
  return ws_seq[REGION(in->pc)][col];
}

static int n_pass, n_real, scenario_waitcnt;

static void check_gate(const char *name, u32 target, int col)
{
  int interp = ws_nseq[REGION(target)][col] + ws_seq[REGION(target)][col];
  int dyn = ws_nseq[REGION(target)][col];
  if (g_inject != BUG_GATE_FETCH_FREE)
    dyn += ws_seq[REGION(target)][col];

  int emit_gap = dyn - interp;
  if (emit_gap) n_real++; else n_pass++;
  printf("  [%-9s] %-34s interp=%-3d dynarec=%-3d  fetch_gap=%+d  emit_gap=%+d%s\n",
         emit_gap ? "REAL-BUG" : "ok", name, interp, dyn, 0, emit_gap,
         emit_gap ? "  <== backend bug" : "");
}

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

  check("Thumb BX->Thumb(IWRAM)", &(block){.thumb=1,.n=1,.dpc=0x03007D24,.ins={
    {.pc=0x080A3438,.kind=K_TBX_THUMB,.target=0x03007D24}}});

  check("IWRAM blk, BX->ARM(ROM)", &(block){.thumb=0,.n=2,.dpc=0x08000500,.ins={
    {.pc=0x03000400,.kind=K_DP},{.pc=0x03000404,.kind=K_BX_ARM,.target=0x08000500}}});

  check("Thumb blk in IWRAM (pop+b)", &(block){.thumb=1,.n=2,.dpc=0x03000500,.ins={
    {.pc=0x03000400,.kind=K_LDM,.data=0x03007F00,.nregs=3},
    {.pc=0x03000402,.kind=K_B,.target=0x03000500}}});

  check("Thumb-resident ROM blk", &(block){.thumb=1,.n=3,.dpc=0x08000600,.ins={
    {.pc=0x08000400,.kind=K_DP},{.pc=0x08000402,.kind=K_DP},
    {.pc=0x08000404,.kind=K_B,.target=0x08000600}}});

  check("Thumb MUL has no SH4 extra", &(block){.thumb=1,.n=3,.dpc=0x0800252C,.ins={
    {.pc=0x08002526,.kind=K_DP}, {.pc=0x08002528,.kind=K_DP},
    {.pc=0x0800252A,.kind=K_TMUL}}});

  check("ARM MUL has no SH4 extra", &(block){.thumb=0,.n=1,.dpc=0x03001004,.ins={
    {.pc=0x03001000,.kind=K_AMUL}}});
  check("ARM MLA has no SH4 extra", &(block){.thumb=0,.n=1,.dpc=0x03001008,.ins={
    {.pc=0x03001004,.kind=K_AMLA}}});
  check("ARM UMULL has no SH4 extra", &(block){.thumb=0,.n=1,.dpc=0x0300100C,.ins={
    {.pc=0x03001008,.kind=K_AMULL}}});
  check("ARM UMLAL has no SH4 extra", &(block){.thumb=0,.n=1,.dpc=0x03001010,.ins={
    {.pc=0x0300100C,.kind=K_AMLAL}}});
  check("ARM SMULL has no SH4 extra", &(block){.thumb=0,.n=1,.dpc=0x03001014,.ins={
    {.pc=0x03001010,.kind=K_ASMULL}}});
  check("ARM SMLAL has no SH4 extra", &(block){.thumb=0,.n=1,.dpc=0x03001018,.ins={
    {.pc=0x03001014,.kind=K_ASMLAL}}});

  check("Thumb Div HLE tracks BIOS cycles", &(block){.thumb=1,.n=1,.dpc=0x0812F6C6,.ins={
    {.pc=0x0812F6C4,.kind=K_THLE_DIV,.data=3,.width=6,.target=0x0812F6C6}}});

  check("Thumb SWI vectors with no fetch", &(block){.thumb=1,.n=1,.dpc=0x00000008,.ins={
    {.pc=0x08004B9C,.kind=K_TSWI,.target=0x00000008}}});

  check("Thumb cond not-taken ROM refill", &(block){.thumb=1,.n=7,.dpc=0x08003044,.ins={
    {.pc=0x08003066,.kind=K_DP}, {.pc=0x08003068,.kind=K_DP},
    {.pc=0x0800306A,.kind=K_BCOND_NT,.target=0x0800306C},
    {.pc=0x0800306C,.kind=K_LD,.data=0x08003074,.width=1},
    {.pc=0x0800306E,.kind=K_DP}, {.pc=0x08003070,.kind=K_DP},
    {.pc=0x08003072,.kind=K_B,.target=0x08003044}}});

  check_gate("Thumb loop gate fetch", 0x08004C76, 0);
  check_gate("ARM loop gate fetch",   0x03000100, 1);
}

/* Self-validation: inject a known historical backend bug and confirm the audit
 * flags it REAL-BUG. Proves the emit_gap check actually detects mis-charges. */
static int self_test(void)
{
  int fails = 0;
  /* branch target in EWRAM ({3,6}) so a wrong bus-width column is visible
   * (in IWRAM both columns are 1 and the bug would hide). */
  block refill_blk = {.thumb=0,.n=2,.dpc=0x02000100,.ins={
    {.pc=0x03000000,.kind=K_STM,.data=0x03007F00,.nregs=6},
    {.pc=0x03000004,.kind=K_B,.target=0x02000100}}};
  block cond_blk = {.thumb=1,.n=1,.dpc=0x0800306C,.ins={
    {.pc=0x0800306A,.kind=K_BCOND_NT,.target=0x0800306C}}};
  block gate_blk = {.thumb=1,.n=1,.dpc=0x08004C76,.ins={
    {.pc=0x08004C82,.kind=K_BCOND_T,.target=0x08004C76}}};
  block tmul_blk = {.thumb=1,.n=1,.dpc=0x0800252C,.ins={
    {.pc=0x0800252A,.kind=K_TMUL}}};
  block amul_blk = {.thumb=0,.n=1,.dpc=0x03001004,.ins={
    {.pc=0x03001000,.kind=K_AMUL}}};
  block div_blk = {.thumb=1,.n=1,.dpc=0x0812F6C6,.ins={
    {.pc=0x0812F6C4,.kind=K_THLE_DIV,.data=3,.width=6,.target=0x0812F6C6}}};
  block tswi_blk = {.thumb=1,.n=1,.dpc=0x00000008,.ins={
    {.pc=0x08004B9C,.kind=K_TSWI,.target=0x00000008}}};
  block abx_blk = {.thumb=0,.n=1,.dpc=0x00000720,.ins={
    {.pc=0x00000090,.kind=K_BX_ARM,.target=0x00000720}}};
  struct { enum bug b; const char *name; const block *blk; } cases[] = {
    { BUG_B1_BLOCK_FREE,     "native block transfer charges no wait-states (pre-B1)", &refill_blk },
    { BUG_REFILL_WRONG_COL,  "branch refill uses the wrong bus-width column", &refill_blk },
    { BUG_COND_NT_FREE,      "not-taken Thumb conditional branch skips refill", &cond_blk },
    { BUG_GATE_FETCH_FREE,   "exhausted loop gate skips target fetch", &gate_blk },
    { BUG_THUMB_MUL_EXTRA,   "Thumb MUL carries the old +2 threaded approximation", &tmul_blk },
    { BUG_ARM_MUL_EXTRA,     "ARM MUL carries the old threaded approximation", &amul_blk },
    { BUG_HLE_DIV_FLAT,      "HLE Div carries the old flat 64-cycle charge", &div_blk },
    { BUG_THUMB_SWI_FETCH,   "Thumb SWI charges its skipped instruction fetch", &tswi_blk },
    { BUG_ARM_BX_TARGET_FETCH_FREE, "ARM BX-to-ARM skips target fetch", &abx_blk },
  };
  for (unsigned i = 0; i < sizeof cases/sizeof cases[0]; i++) {
    g_inject = cases[i].b;
    const block *blk = cases[i].blk;
    int col = fcol(blk->thumb), imb = 0, dmb = 0;
    if (cases[i].b == BUG_GATE_FETCH_FREE) {
      imb = ws_nseq[REGION(blk->ins[0].target)][col] +
            ws_seq[REGION(blk->ins[0].target)][col];
      dmb = ws_nseq[REGION(blk->ins[0].target)][col];
    } else {
      for (int k = 0; k < blk->n; k++) {
        imb += interp_mb(&blk->ins[k], col) + interp_fetch(blk, k, col);
        dmb += dyn_mb(&blk->ins[k], col) + dyn_fetch(blk, k, col);
      }
    }
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
         "tolerated = live ws_cyc[own-pc] vs live ws_cyc[post-pc] fetch model,\n"
         "mainly at region crossings or indirect-branch block boundaries.\n");

  scenario_waitcnt = 0x0000; reload_timing(scenario_waitcnt); run_suite();
  scenario_waitcnt = 0x4014; reload_timing(scenario_waitcnt); run_suite();  /* fast WS0 */

  printf("\n-- self-test: confirm the audit detects backend bugs --\n");
  int st = self_test();

  printf("\nModeled classes: ARM/Thumb fetch, single load/store, LDM/STM, B/BL,\n"
         "BX (ARM & Thumb targets), ARM/Thumb MUL no-extra, Thumb HLE Div, Thumb SWI,\n"
         "branch refill. NOT yet\n"
         "modeled (add here if a residual per-block gap appears): MSR/MRS, SWP,\n"
         "SWI, conditional-fail instruction fetch accounting.\n");

  printf("\n== %d ok/tolerated, %d REAL backend bug(s); self-test %s ==\n",
         n_pass, n_real, st ? "FAILED" : "passed");
  return (n_real || st) ? 1 : 0;
}
