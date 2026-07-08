# BIOS / SWI High-Level Emulation

All of this lives in `ports/fxcg100/sh4/sh4_interp_helpers.c`. The SH-4
dynarec never translates BIOS code: every dispatch to a guest PC < 0x4000 is
routed by the assembly stubs into `cgba_sh4_bios_fallback(u32 cycles)`,
which either services the entry with an HLE fast path or interprets the real
open BIOS (`vendor/gpsp/bios/open_gba_bios.bin`) in region-exit stop mode —
`execute_arm` returns the instant PC reaches 0x4000, so BIOS interpretation
never bleeds into game code. (Game code itself runs translated once hot;
cold ROM code goes through the cold-gate interpreter — see
[sh4-jit-architecture.md](sh4-jit-architecture.md). Before region-exit
stop, every IRQ interpreted the rest of the frame; 68% of a "JIT" run was
`execute_arm`.)

## The fallback contract

`cgba_sh4_bios_fallback(cycles)` receives the remaining event-slice budget
and returns a stub-consumable value: remaining cycles (> 0) to continue,
the `update_gba` packed result when the slice ended, or 0x80000000 when the
frame completed inside the BIOS. Hooks, in order:

1. **IntrWait park step** (pc==4, SYSTEM mode, gated by
   `CGBA_SH4_INTRWAIT_HLE`, currently **0** — parked on an unresolved
   BIOS_IF wedge; the halt-loop is interpreted instead).
2. **SWI dispatch** (pc==8, SVC mode) — `cgba_hle_bios_swi(budget)`.
3. **IRQ wrapper HLE** (IRQ mode): pc==0x18 entry (push r0-r3,r12,lr on the
   IRQ stack — bulk host-side writes with SMC tag checks — point lr at
   0x30, jump to the game handler) and pc==0x30 exit (pop, return to the
   interrupted PC, immediate pending-IRQ re-entry). Saves two interpreter
   round-trips per IRQ; the stacked words use `cgba_le32_write` so IRQ
   stacks over tagged RAM code still flush correctly.
4. Otherwise: interpret with `cgba_diff_stop_on_bios_exit = 1`.

The SWI number is decoded from the caller (`[lr-2] & 0xFF` Thumb,
`[lr-4] >> 16 & 0xFF` ARM). HLE'd services set registers/PC/mode directly
and return through a common tail (`reg[REG_PC] = lr`, CPSR/mode from
SPSR_svc). Division (SWI 6/7) is additionally HLE'd inline by the
translator itself (`is_div_swi`), never reaching this layer.

## Why the old memory-SWI HLEs were demoted

The original CpuSet/CpuFastSet/LZ77/RL HLEs (`CGBA_SH4_SWI_MEM_HLE`, now
compiled out by default) reproduced the *memory* effects but neither the
post-SWI scratch registers (the open BIOS dispatcher restores r2/r3/r11/r12
and lr — only **r0/r1 escape** — and the routines leave r1 = dst−src delta
(copy) or dst end (fill), while r0 is per-path: FastSet copy leaves the
source cursor end, CpuSet word-copy the last word loaded, half-copy/fill
and the early-outs leave it unchanged) nor the instruction-fetch cycle cost of
the real routine. That infidelity was invisible while SWI call sites ran
interpreted, and catastrophic once the cold gate's stop-on-hot promoted
those call sites into translated code: the JIT run serviced SWIs via HLE
while the interpreter ran the real BIOS, and the dense Metroid soak
diverged from frame ~420. Demoting everything back to the interpreted BIOS
restored bit-exactness and cost Advance Wars ~9 fps — recovered faithfully
as follows.

## Tier 1: the slice-gated atomic fast path

`CGBA_SH4_SWI_CPUSET_FAITHFUL` (default 1) restores CpuSet (0x0B) and
CpuFastSet (0x0C) as *register- and cycle-faithful* HLEs:

- **Predictors** (`cgba_swi_cpuset_predict` / `cgba_swi_cpufastset_predict`)
  compute the exact post-SWI r0/r1 and the exact cycle cost of the real
  routine from the disassembled open BIOS, using the live waitstate tables
  (`ws_cyc_seq/nseq`). Dispatcher cost:
  `19*S0 + 3*N0 + 8*S3 + 6*S[sp] + N[callback]` (+`S[lr]` for ARM callers).
  FastSet copy chunk: `60*S0 + 8*(N[src]+N[dst]) + 7*N0` per 8-word chunk —
  the 7-instruction inner loop *including the BNE's own fetch*, verified by
  per-instruction trace against the interpreter. Early-outs (source region
  checks, count 0) are modeled per-path.
- **The budget gate**: the fast path applies only when
  `pred.cycles + 64 <= budget` (the remaining event slice). This is the
  load-bearing correctness insight: a SWI whose cost exceeds the slice is
  executed by the interpreter *across* `update_gba` windows — events fire
  and IRQs vector **mid-copy**. Debiting the whole cost atomically would
  move those guest-visible events after the copy (and `update_gba` clamps
  remaining at −64, silently deleting guest time). Within one slice, the
  atomic HLE is event-equivalent by construction.
- **Apply** (`cgba_swi_apply_faithful`): resolves both sides to host
  pointers first, bails (`return 0` → interpreted BIOS) on anything
  unmodeled — non-RAM destination, unresolvable source, overlap — *before*
  any side effect, then memmove/fill, SMC tag scan (`CPU_ALERT_SMC`), set
  r0/r1 from the prediction, charge `cgba_sh4_extra_cycles`.

Verified: `CGBA_SH4_SWI_HLE_VERIFY` builds run predict → real interpreted
BIOS → compare r0/r1 and consumed cycles at the return PC. Final score
checked=4889, bad=0 over a 2000-frame AW boot window.

## ObjAffineSet fast path

`CGBA_SH4_SWI_OBJAFFINE_HLE` (default 1 for dynarec builds) services
ObjAffineSet (SWI 0x0F) when the call fits inside the current event slice.
Yoshi's Island uses this SWI heavily during gameplay, so leaving it
interpreted made the 600-frame A/Left soak spend about 1.9M BIOS
instructions in 300 frames. The HLE mirrors the open BIOS routine at 0x8E0:
it reads each `rx/ry/theta` triple, uses the BIOS sine table at 0x2150,
writes the four OBJ matrix halfwords with the caller-provided stride,
advances only caller-visible `r0/r1`, and lets the common SWI return tail
restore the mode/CPSR/PC. The path is deliberately narrow: Thumb callers
only, aligned source/destination, bounded count/stride, resolvable source,
no destination region wrap, and `cycles + 64 <= budget`. Anything outside
that shape falls back to the real BIOS interpreter.

## Tier 2: the parked/resumable CpuFastSet engine

(CpuSet 0x0B shares the same engine — `cgba_swi_cpuset_materialize` /
`cgba_swi_cpuset_engine`, word-copy loop-top 0x6AC, half-copy 0x668 —
mirroring everything below; only the copy loop-top register layout and
per-element chunking differ.) The oversized tail was the actual cost: per
2000 AW frames, 4,718 declined FastSets moving 864k words (mostly 224-word EWRAM→OAM sprite flushes,
~3.7k guest cycles each ≈ 3 scanlines — IRQs *will* vector mid-copy).
The engine executes them in budgeted 8-word chunks from a **canonical
machine state**: at every pause point, registers, stacks, mode and PC are
exactly what the real interpreter would show at that BIOS PC. Parking is
just… stopping there. Consequences:

- An IRQ raised at a slice boundary vectors normally (lr_irq = loop-top
  PC + 4, a real BIOS address); the ISR returns onto the loop-top PC and
  the resume hook continues the copy.
- A savestate taken mid-copy, a failed resume validation, or any other
  surprise degrades to the interpreter *continuing the same state
  bit-exactly*. There is no hidden engine state to lose.

Geometry (from the open BIOS, empirically confirmed by per-instruction
trace): dispatcher at 0x64 pushes {r11,r12,lr} + SPSR on the SVC stack,
switches to SYSTEM mode (`(spsr & 0x80) | 0x1F`, NZCV cleared), pushes
{r2,r3,lr} on the caller's stack, sets lr=0x94; FastSet at 0x720 pushes r4.
Copy chunk top = 0x7A4 (r3 = src cursor, r1 = dst−src delta, r4 = words
left raw, r12 = 0x0EFFFFFF, r0 = previous chunk end, r2 = last loaded
word); fill chunk top = 0x770 (r1 = dst cursor, r2 = fill word,
r12 = 0x720 jump-table leftover). Epilogue: pop r4; return to 0x94; pop
{r2,r3,lr}; back to SVC (0xD3, r12 = 0xD3 leftover); pop SPSR + {r11,r12,lr};
`MOVS pc, lr`.

Flow: at dispatch (pc==8, oversized, count≠0), materialize the
post-prologue state — the stack frames are *written to guest memory for
real* — charge dispatcher+prologue cycles, then loop: copy chunks while
budget remains (bulk memmove + tag scan when the destination resolves to a
host pointer; per-word `execute_store_u32` otherwise, which is what makes
OAM/palette/IO destinations correct — the store helpers handle
`oam_update`, palette conversion, and IO side effects); on exhaustion call
`update_gba(remaining)` exactly like the interpreter's internal refill,
handle frame-completion (return 0x80000000 with the park state intact) and
IRQ vectoring (delegate to the pc==0x18 wrapper HLE, return to the stub);
on completion run the epilogue analytically, *reading the popped values
back from guest memory*. Per-chunk charges mirror the verified predictor
decomposition exactly (the fill path refunds the final chunk's absent
back-branch fetch).

The resume hook fires on any fallback entry at pc ∈ {0x7A4, 0x770} in
SYSTEM mode, validates the register signature (r12 magic, r4 > 0, regions,
alignment) and continues; any doubt declines with the `CGBA_FS_DECLINE`
sentinel and the fallback falls through to the interpreter, which picks up
the canonical state. Nested SWIs from an ISR work because the outer park
*is* guest state — the SVC stack frame nests naturally.

Result: AW 31.8 → 39.8 fps (2.96M cycles/frame); interpreted-BIOS
instructions halved (13.4M → 6.9M per 2000 frames); FastSet interp count
4,718 → 8. Metroid dense parity battery: byte-identical to the
pre-engine baseline. Known position tolerance: within a slice, the engine
pauses at chunk (8-word) granularity where the interpreter pauses at
instruction granularity — an ISR that reads the destination mid-copy could
see up to 7 words more progress, and lr_irq differs by a few loop
instructions. Accepted; the parity batteries police it.

## Remaining interpreted tail (per 2000 AW frames)

CpuSet (0x0B) copies now have their own parked engine too
(`cgba_swi_cpuset_engine`), covering word- and half-copy to any region —
the LttP rain intro rebuilds OAM every frame via ~256-word oversized
CpuSets that this reclaims (Zelda render-off floor 24.5 -> 29.1 fps;
Metroid dense parity stays bit-exact). Fills stay interpreted (rare).
Remaining declined SWIs (per `jit swi-census`): BgAffineSet (0x0E),
LZ77UnCompWram/Vram (0x11/0x12), oversized ObjAffineSet calls that cross an
event slice, and small fitting copies. The
parked-engine pattern extends to the decompression SWIs next (see
[performance-history.md](performance-history.md), Future directions).

## Bulk helpers and a hardware lesson

`cgba_bulk_ram_host(addr, len, &tags)` resolves EWRAM/IWRAM (with tag
mirrors) and VRAM (no tags); `cgba_bulk_src_host` adds single-page gamepak
sources. Anything else (OAM, palette, IO) is per-word via the store
helpers. `cgba_bulk_tags_smc` aligns its scan head byte-wise before the
u32 sweep: the tag pointer inherits the guest destination's alignment, and
a halfword-aligned CpuSet destination (AW unit-select, 0x03001FF2) made the
u32 scan read at +2 — an SH-4 address error (EXC=0E0) *on hardware only*;
calcemu's host CPU tolerates misaligned loads. Any host pointer derived
from a guest address must be byte-wise/memcpy-accessed or aligned first.

## Verify harness notes

`CGBA_SH4_SWI_HLE_VERIFY=ON` stashes the prediction, lets the real BIOS
run, and compares at the return PC, filtering out records where the SWI
crossed an `update_gba` window (pc ≠ return, IRQ count changed, or
remaining < 0): for multi-slice SWIs the "used cycles" measurement is
meaningless because `execute_arm` refills its budget internally — this
exact artifact (used=66 for a 3,762-cycle copy, with completed registers)
is what led to the slice-gate + parked design. One-shot per-instruction
cycle tracing (`cgba_diff_trace_cycles`) can be armed on a specific SWI
signature to print `c<pc>:<remaining>` lines for formula derivation.
