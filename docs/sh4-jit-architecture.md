# SH-4 JIT: As-Built Architecture

The dynarec that actually shipped, as opposed to the plan in
[sh4-jit-optimization-plan.md](sh4-jit-optimization-plan.md). Target:
SH7305 (SH-4A core, big-endian, no FPU) on the Casio fx-CG100. All paths
below are relative to the repo root; identifier names are stable anchors
even where line numbers drift.

## Layering

- **vendor/gpsp/cpu_threaded.c** — gpSP's classic recompiler driver:
  `scan_block` finds block ends/exits, a flag-liveness pass rewrites
  per-instruction `flag_data`, a translate loop macro-expands each ARM/Thumb
  opcode through the host emitter header. SH4-only additions live behind
  `SH4_ARCH`: an ARM dead-flag pass, branch-target cycle-gate seams,
  unconditional-end gate elision, cross-cache chain bans, and the cold-code
  translation gate.
- **vendor/gpsp/sh4/sh4_emit.h** — the emitter seam. Every instruction class
  follows *"try native emitter, else compact C-helper trampoline"*:
  `thumb_data_proc → sh4g_thumb_dp_native` else `SH4_CALL_OP2(cgba_sh4_thumb_dp)`,
  and analogously for ARM DP/ldst/block/mul/PSR. `block_prologue_size = 0`;
  every far reference is a self-contained inline literal, so block entry is
  the first real instruction.
- **ports/fxcg100/sh4/** — the port layer: `sh4_codegen.h` (verified 16-bit
  encoder, byte-for-byte audited against `sh-elf-as`), `sh4_emit_glue.h`
  (constants, conditions, flags, call sites, branch exits),
  `sh4_thumb_dp_emit.h` / `sh4_arm_*_emit.h` (native emitters),
  `sh4_fastmem.{h,c}` (resident out-of-line memory routines),
  `sh4_interp_helpers.c` (C helpers + the BIOS HLE layer — see
  [bios-swi-hle.md](bios-swi-hle.md)), `sh4_cache.h` (OCBWB→SYNCO→ICBI sync).
- **vendor/gpsp/sh4/sh4_stub.S** — the assembly spine: entry/exit, dispatch,
  inline indirect-branch resolvers, the four C-helper trampolines, funnels
  for BIOS fallback and cold interpretation.

## Execution model

Host register pinning (holds across generated code and stubs):

| Reg | Role |
|-----|------|
| R14 | `reg[]` guest state base (`SH4_REG_BASE`) |
| R13 | down-counting cycle budget (`SH4_REG_CYCLES`) |
| R9  | resident 12-entry vector table (`cgba_sh4_vec_table`, order = `SH4G_VEC_*`) |
| R10 | `&sh4_block_exit` |
| R8  | cached guest CPSR — authoritative while JIT code runs, synced around every C call (`CPSR_FLUSH`/`CPSR_RELOAD`) |
| R0–R7 | scratch/args (`SH4_REG_RET`, `T0..T2`, `ARG0..3`) |

Guest registers are **not** pinned: every guest ARM register lives in the
`reg[]` array, addressed off R14, load→op→store per instruction (offsets ≤60
use the short `MOV.L @(disp4,R14)` form). NZCV stay architecturally
materialized in `reg[REG_CPSR]` bits 31..28. A per-block Thumb constant
tracker (`sh4_thumb_const_mask/val[16]`) propagates known register values
(MOV imm8, LDR pc-literal) into const-address IO load fast paths.

## Translation flow

Blocks are keyed per cache:

- **RAM code** (IWRAM/EWRAM): a 16-bit tag mirror (EWRAM tags at
  `ewram[0x40000 + (pc & 0x3FFFF)]`, IWRAM tags at `iwram[pc & 0x7FFF]`,
  data at `iwram+0x8000`) doubles as block index and SMC detector. Tag
  values: `0` = data, `0x0101` = code-but-not-block-start, `>0x0101` =
  index into a `ramtag_type{offset_arm, offset_thumb}` table growing *down*
  from the end of the RAM translation cache.
- **ROM code** (BIOS + gamepak regions): Knuth hash
  (`(key * 2654435761u) >> (32 - ROM_BRANCH_HASH_BITS)`, key = `pc|thumb`,
  `ROM_BRANCH_HASH_BITS=12` → 16 KiB table) into chained
  `hashhdr_type{pc_value, next_entry}` headers embedded in the ROM cache
  itself.
- Any other region resolves to the untranslatable sentinel `(u8*)~0`;
  executing it traps in dispatch via `cgba_sh4_wild_jump` (gint panic with
  the guest PC) instead of jumping to host `0xFFFFFFFF`.

`translate_block_{arm,thumb}`: scan → dead-flag pass → translate loop. At
every internal branch-target seam the driver force-closes any open ARM
conditional run, emits the cycle accounting *before* the seam and a cycle
gate *at* the seam (loop-backs re-check the budget without re-charging), and
forces `flag_data |= 0xF00` at the seam (an IRQ at the gate latches CPSR into
SPSR_irq, which the linear liveness pass cannot see). Cache overflow flushes
the whole owning cache and restarts translation (up to 4 retries in the
resolver). The fall-through translation gate is elided when the block
provably ended unconditionally (`arm_scan_terminal_emitted` whitelists
B/BL/SWI/BX/LDM{pc}; Thumb always terminal) — ~20-24 bytes per block. The
cursor is re-4-aligned with a never-executed NOP pad (headers and literal
tuples are written with 32-bit stores).

Hard limits: `MAX_BLOCK_SIZE=1024` instructions, `MAX_EXITS=32`.

## Dispatch

`execute_arm_translate_internal` pins the register set, honors
`CPU_HALT_STATE`, and falls into `lookup_pc`, which probes the ROM branch
hash **inline in assembly** (`sh4_indirect_branch_{arm,thumb}`, keyed on the
R8-cached Thumb bit) instead of calling the C resolver. Indirect/dual
branches additionally consult `cgba_dynarec_dual_hot_key/ptr[64]` — a
direct-mapped cache of resolved *host pointers*. This table must be cleared
on **both** ROM and RAM flushes; leaving Thumb-return pointers into a
rewritten RAM cache caused wild jumps long after the flush (EXC=180 class).

`sh4_block_exit` (R4 = target guest PC) stores `reg[REG_PC]`, flushes CPSR,
calls `update_gba(R13)`; bit31 of the packed return = frame complete →
return to the C frame loop, else reload the budget from `ret & 0x7FFF` and
go to `lookup_pc` (it is jumped to, so there is no resume path).
`sh4_update_gba` — the *mid-block* cycle-exhaustion entry, which is called —
additionally decodes bit30 (PC changed → `lookup_pc`, clear → resume the
block). Helper returns route through `sh4_helper_exit`:
store-alert codes (IRQ unmask/HALT/SMC) take the `update_gba` pass; pure PC
changes take `sh4_pc_redispatch`, which skips `update_gba` entirely to match
interpreter event policy (one hash probe + JMP per function return).

BIOS: guest PC < 0x4000 never translates — everything funnels to
`sh4_bios_fallback_entry` → `cgba_sh4_bios_fallback` (see
[bios-swi-hle.md](bios-swi-hle.md)). The BIOS IRQ wrapper is HLE'd at the
resolver (0x18 entry / 0x30 exit) to avoid two interpreter round-trips per
IRQ.

## Emitters

- **Constants** (`sh4g_const`) in five tiers: imm8 (2 B); MOV+EXTU.B (4 B);
  MOV+NOT (4 B); `s8 << n` synthesized from T-safe shift chains
  ({SHLL16, SHLL8, SHLL2}, candidate order {16,8,2,24,18,10,4,26}); inline
  literal island (10-12 B, never executed). The tier order matters:
  `sh4g_const` runs tens of millions of times in flush-thrash scenes.
- **Conditions**: `sh4g_cond_to_T` evaluates all 14 ARM conditions from
  CPSR, including compound ones (HI = C&!Z, LT = N^V, LE = Z|(N^V) folded
  into a sign bit). ARM same-condition runs skip via a far jump (unbounded
  length), Thumb conditional bodies via `BF disp8`.
- **Flags** (`sh4g_set_flags`): writes only the live flags per the
  dead-flag mask, rounded *up* to a top-contiguous set {N, NZ, NZC, NZCV} —
  producing a dead flag correctly is always safe, and the rounding enables
  the ROTCR trick: pre-shift the cached CPSR left by the live-flag count,
  then rotate each new flag in through T (12/16/24/30 bytes vs 18-48 for
  mask-build+OR chains). Exact C/V for arithmetic via `clrt;addc`,
  `cmp/hs`, `addv/subv`; carry-in ops preload T.
- **Multiplies**: MUL/MLA → `mul.l`; UMULL/SMULL → `dmulu.l/dmuls.l`;
  UMLAL/SMLAL via `clrt;addc;addc` 64-bit chains. (~282k C dispatches per
  measurement window on math-heavy games before nativization.)
- **C fallback sites** (`sh4g_op2_tramp_call`): a fixed ~28-32-byte site —
  JSR through a literal tuple `{trampoline, helper_fn, opcode, pc[,
  cycle_count]}` — read by four shared stub trampolines (plain / +memory
  cycle debit / +PC-redispatch / both). Replacing ~70-byte inline glue
  halved helper-heavy ARM blocks; this was a translation-cache *capacity*
  fix as much as a speed fix.

## Fastmem (out-of-line memory routines)

The density fix for load/store sites. Inline fast paths cost ~90-130 bytes
per site (guards + page probe + transfer + SMC tags + wait-state charge);
Metroid's working set overflowed the 896 KiB ROM cache and wholesale-flushed
~3×/frame (~3 fps). Now each fast-path *shape* is emitted exactly once at
`cgba_sh4_fastmem_init` into a resident 8 KiB `.bss` buffer (survives
translation flushes; P1 RAM is executable after cache sync), and sites are
a fixed ~36-44-byte tuple call. 20 routines: 8 base kinds
{LOAD_W/B/UH/SH/SB, STORE_W/UH/B} × writeback twin, plus 4 block routines
(LDM/STM ± writeback). The same tuple serves both paths: a guard failure
jumps straight into `sh4_op2_pc_mem_tramp`, which re-executes the access via
the C helper from *original* state (writeback is never committed before
guards pass).

Guards: address < 0x10000000, non-NULL 32 KiB page, loads restricted to
regions 2..12, stores to EWRAM/IWRAM (tag-probed) or VRAM word/half, guest
and host alignment (NOR pages can be fragmented). Transfers byte-reverse via
`swap.b/swap.w` (big-endian host, little-endian guest). STORE_UH carries an
IO fast path mirroring `write_io_register16` for REG_IF (acknowledge) and
REG_IE (with pending-IRQ check falling to C) — AW's per-scanline ISR hits
these ~550k times per 2000 frames. Block routines pre-scan *all* destination
SMC tags before any store (no partial writes). LDM/STM lists shorter than
`CGBA_SH4_FASTMEM_BLOCK_MIN=3` keep the faster unrolled inline path.

The resident buffer also hosts `cgba_sh4_psr_rebank_routine` (~110 B
`set_cpu_mode` re-bank with full FIQ semantics) — inlining it at every
MSR-mode-change site had cost +59% I-cache misses on AW.

## Dead-flag elimination

`flag_data` per scanned instruction: bits 0-3 = flags the instruction MAY
modify (rewritten by the pass to the set it SHOULD generate), bits 4-7 =
MUST modify, bits 8-11 = REQUIRES. The pass is bottom-up liveness with
`needed_mask` starting 0xFF at block end. gpSP upstream only had the Thumb
scan; the ARM pass (`arm_flag_status` + `arm_cond_requires[16]`) is
SH4-only, with two upstream scan bugs fixed on the way (TST classified as
modifies-all; `ADD pc, rs` jump-table idiom now requires-all). Net effect
when it landed: dense-soak cycles −25%, and blocks shrank enough that
flushes fell 217 → 30.

Documented trade: stores pass liveness through, so a mid-block store-alert
exit may observe a skipped dead flag (only visible if an ISR inspects SPSR
NZCV mid-block). Making every store a liveness barrier measured −10% on the
Metroid movement soak from block growth. `CGBA_SH4_ARM_DEAD_FLAGS=OFF` is
the A/B switch; `CGBA_SH4_EXACT_CYCLE_BOUNDARIES` builds force all-live
(per-instruction CPSR compares would read skips as false mismatches).

## Block linking

Direct branch exits emit: cycle check → `JMP @R10` (budget spent) →
patchable far jump. `sh4g_chain_patch` rewrites the far jump to `BRA+NOP`
when the target is within ±4 KiB, else patches the literal (a data-side
write — no I-cache resync needed; `CGBA_SH4_PATCH_RESYNC=1` resyncs
instruction rewrites anyway on the calculator as overclock
belt-and-suspenders). `reg[REG_PC]` is deliberately not stored on the
chained path.

External-exit resolution has four hard rules, each earned by a field crash:

1. BIOS targets (< 0x4000) are never chained (`bios_swi_entrypoint` is NULL
   on SH4); the old `== 0x8` abort left later exits unchained.
2. The `(u8*)~0` sentinel is never patched in.
3. Cold-gate targets (NULL + `cgba_cold_pending`) leave the exit unpatched —
   treating them as translate-failure wholesale-flushed the cache in a loop.
4. **Never chain across the ROM/RAM caches** — a flush of one side leaves
   the other's direct chains dangling into freed arena (EXC=180 at a host PC
   inside the arena, long after the flush). Resolution probes run under
   `cgba_cold_gate_probe` so speculative lookups don't heat the gate.

## SMC handling

Every translated byte is tagged at scan time. Native stores probe the tag
mirror before writing and fall back to C on any nonzero tag word; the C
helper (`cgba_store_alert_break`) calls `flush_translation_cache_ram()`
itself, commits the PC, and returns the store-alert code, which
`sh4_helper_exit` routes through `sh4_block_exit` (the `smc_write` stub
other backends jump to is dead code on SH4). VRAM has no tag mirror
(region 6 is not translatable). RAM
flushes leave the ROM cache in place — hence linking rule 4 above.

## Cold-code gate

The arena is hardware-pinned (see below), and Metroid-class working sets can
overflow even a large ROM cache, so ROM code is interpreted until proven
hot: a hashed 16 K-entry u8 heat counter (`cgba_hot_count`) increments per
block-entry dispatch; below `CGBA_SH4_HOT_THRESHOLD=64` the block runs in
the interpreter in ≤512-cycle chunks (`cgba_sh4_cold_interp`). Refinements
that made it work (chronology in
[performance-history.md](performance-history.md)):

- **Probe-only resolution**: external-exit and speculative lookups don't
  heat (`cgba_cold_gate_probe`), so linking doesn't fake execution counts.
- **Stop-on-hot** (`CGBA_COLD_HEAT`): a cold interp chunk *ends* when a
  branch target's heat crosses the threshold (`collapse_flags()` +
  budget-stop return) instead of interpreting straight through
  already-translated code — chunk overshoot had 54% of all movement-soak
  instructions inside `execute_arm`.
- **Leaky-bucket decay** on capacity flush (`h -= CGBA_SH4_HEAT_FLUSH_LEAK`,
  default 16) instead of halving: `h_next=(h+N)/2` has fixed point `h*=N`,
  which permanently interpreted every block executed fewer than 64 times
  per flush epoch.
- Tunables: `CGBA_SH4_HEAT_QUIET_FRAMES=180`, `CGBA_SH4_HEAT_SLOW=1`,
  `CGBA_SH4_HEAT_FLUSH_LEAK=16` (cmake cache variables).

Interpreter chunks must call `collapse_flags()` before returning mid-block —
a budget stop between CMP and Bcc without it loses the compare (the cold
gate's original bring-up blocker).

## Memory layout (hardware-pinned)

Both translation caches live in the NOLOAD `.cgba.highbss` arena at
0x8c200000 (P1 cached+executable alias), fixed split **1024 KiB ROM / 256 KiB
RAM**, with `ROM_BRANCH_HASH_BITS=12`. The added 256 KiB does not grow the
arena: embedded mini ROMs share the tail of the mutually exclusive 2 MiB
GamePak page cache instead of reserving a duplicate buffer. The arena end
0x8c655300 is the only layout proven safe on hardware: the resident MPM loader
lives at 0x8c700000 (1.5 MiB cache overwrote it → instant hard reset), and
live OS state sits below it (an arena ending at 0x8c6b5300 also hard-reset).
The linker script asserts the production ceiling; do not raise it without
on-device proof. GBA state arrays (ewram ×2, iwram ×2, vram, bios,
backup) share the same arena. Calculator savestate saves borrow the non-JIT
GamePak cache while guest execution is stopped, preserving translated code and
heat. State loads still flush both JIT caches after replacing guest memory.

ROMs execute in place from NOR flash: 32 KiB pages direct-mapped into
`memory_map_read[8192]` only when all eight 4 KiB flash blocks are
contiguous and 4-byte aligned (the JIT does wide loads through the map);
fragmented pages fault through a 64-page LRU gather cache. Page 0 always
uses a RAM shadow (gpSP writes GPIO shadows into the ROM image). Only P1/P2
block pointers are accepted — TLB-mapped P0 addresses fault long after load.

## Cycle accounting

Translate-time constants accrue per instruction from the WAITCNT-live
`ws_cyc_seq/ws_cyc_nseq[16][2]` tables and debit R13 at gates and exits;
runtime memory costs (nonseq data, refills) are charged by the fastmem
routines and helpers via `cgba_sh4_extra_cycles`. Exhaustion funnels to
`update_gba` through `sh4_block_exit`/`sh4_update_gba`. On the
frame-complete path `sh4_update_gba` must pop its saved PR before returning
to main — missing that shifted every callee-saved restore one slot
(register-file corruption; found via the on-screen crash reporter).

## Verification

The JIT's correctness net (details in
[testing-harness.md](testing-harness.md)):

- `tests/sh4_codegen_audit.c` — encoder vs `sh-elf-as`, byte-for-byte.
- `tests/sh4_exec_oracle.c` — 142,875 cases: emits real native code and runs
  it in a mini SH-4 interpreter against C-helper semantics (covers Thumb
  ALU/shift/MSR/MRS, full ARM DP, PSR/banking, PC-literal loads, IF/IE
  stores, fastmem sites and routines).
- Dense JIT-vs-interpreter pixel-exact soaks — the project's correctness
  anchor (Metroid 2600 frames, byte-identical FBSTAT streams).
- The interpreter (`CGBA_DYNAREC=OFF`) remains the shipping default and the
  correctness oracle; the JIT is the opt-in hardware artifact.
