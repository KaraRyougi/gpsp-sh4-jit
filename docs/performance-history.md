# Performance: Methodology, History, Future Directions

## Measurement methodology

- **The number**: modeled fps = `118e6 / (cycles per frame)`, cycles from
  calcemu's `HLE_CACHESIM` (SH7305-like 32 KiB 4-way model, penalties
  imiss/dmiss 25, wb 10, memop 1). The run window is the cycle delta
  between the first `[CACHESIM] tick` after `running N frames` and the
  last before `=== done`, at fine tick granularity
  (`HLE_CACHESIM_EVERY=10`). Coarse bracketing includes warmup and once
  overstated a baseline as 31.9 fps that was really 27.52 — every
  before/after pair must use the same protocol.
- **Determinism**: a given binary produces identical cycle counts on
  re-runs. Identical numbers from a supposed A/B means the binary didn't
  change (stale build) — this property has caught real measurement
  mistakes twice.
- **Fidelity gate**: no optimization ships without the dense Metroid
  JIT-vs-interpreter FBSTAT battery staying byte-identical (see
  [testing-harness.md](testing-harness.md)) and the exec oracle passing.
  The standing scenario suite: AW 2000-frame boot (pulsed-A), Metroid
  2600-frame dense (parity) and 3000-frame movement soak from the deep
  checkpoint (perf), plus per-goal harnesses.
- **Profiler trap**: casio-emu's `HLE_PROFILE` originally masked PCs to 24
  bits, aliasing JIT-arena samples onto add-in .text and inflating every
  C-function percentage; fixed 2026-07-05 with dual text/arena windows.
  Pre-fix profiles cannot attribute .text time.
- **Counter trap**: `bios_kc` undercounts BIOS residency — it accumulates
  `budget − remaining` per fallback entry, but an interpreted SWI that
  crosses event-slice refills loses the refilled budget from the sum. Use
  `jit interp-instr bios=` (CGBA_SH4_INTERP_STATS) or the profiler for
  real BIOS cost — trusting `bios_kc` once nearly wrote off a 7 fps
  recovery as worthless.

## Chronology (commit-anchored)

Each arc names its commits; full messages carry the detailed numbers.

**Bring-up and first wins.** Dynarec bring-up (`4813850`), then a wave of
diagnostics and hot-path specializations: JIT trace diagnostics
(`a5dee05`), dual-hot indirect target cache (`4787cd2`), NOR through the
P1 cache (`39aca3e`), fixed Thumb IO loads (`b0fe831`, `ca71141`), BX
returns (`de45ce9`), hot stack pushes (`0f6d7f0`). Pure-PC-exit
redispatch + BRA near-chaining (`279a454`). Idle-loop elimination — the
interpreter had been *beating* the JIT on idle-heavy scenes (`fb0e344`).
Crash forensics: on-screen guest-state panics and wild-jump traps
(`c015b52`) immediately found the `sh4_update_gba` PR-stack leak
corrupting the register file on frame-complete (`db3170c`); a JIT/interp
A/B hotkey (`c90f508`) supports same-state hardware comparisons.

**The arena wall.** 768 KiB ROM cache thrashed (44 wholesale flushes/1300
frames); growing to 1.5 MiB overwrote the resident MPM loader at
0x8c700000 — instant hard reset; 1152 KiB also reset. The only proven
layout: arena end 0x8c655300, 1 MiB total, split 896 K/128 K
(`b8dbf12`, `9e55579`, `e13bda7`, `d221353`). This fixed ceiling forced the
entire density arc.

**BIOS fallback region-exit stop** (`ca057f3`): IRQ entries had been
interpreting to the end of the frame — 68% of a "JIT" run inside
`execute_arm`; stopping the instant PC ≥ 0x4000 cut the JIT run's guest
work 6.78B → 4.27B instructions (and to ~2× less work than the
interpreter).

**Density arc.** Compact C-helper trampolines: ~70 B sites → ~28-32 B;
rom_flush 606 → 170 (`617c8c0`). Native ARM stores: dense soak 12.77 →
10.08 Gcyc (`b38a099`). Native VRAM stores, MRS/MSR, then **dead-flag
elimination** — cycles −25%, flushes 217 → 30 (`854a1d9`…`c0d63ac`).
The MSR-native garbage-cycle-debit fix (`b01f146`) — a 5th literal read
from a 4-literal site debited the site's own bytes from the budget.
**Fastmem**: out-of-line resident memory routines, sites 90-130 B →
40-44 B; ROM flushes 27 → 3 in the 3 fps thrash scene (`180de63`,
`0af4605`). Density round 2: pinned R9/R10 vector-table exits, constant
synthesis tiers, dead-gate elision — in-regime cycles −20% (`dccaa75`).

**Cold-code gate arc.** Deep gameplay was structural translation thrash
(95% of regime instructions were translate+emit). Gate with T=64,
512-cycle interp chunks: 4.4 → 17.6 fps on the frame-19000 checkpoint
(`406ca6b`). Probe-only heating (external-exit resolution had been heating
targets — a compounding loop): movement 2.77× (`f3d28bb`).
Block-entry-accurate heating + BIOS IRQ wrapper HLE: AW 20.7 → 29.3
(`40bcd21`). Regime-safe +1/halve heating (`b5787ca`). Finally
**stop-on-hot + leaky-bucket decay** (`b5f3aab`): interp chunks end at hot
branch targets (53.9% of movement instructions had been interpreting
already-translated code), and flush decay subtracts 16 instead of halving
(whose fixed point starved mid-warm blocks forever). Metroid movement
24.6 → 32.4 fps.

**Event-loop batching arc** (the AW "45 fps" rounds, 27.52 → 40.54):
sound-timer overflow batching — only IRQ/cascade timers cap the event
slice (AW's 38 kHz sample timer forced ~650 `update_gba` round-trips per
frame); PSR natives; ISR memory diet (`546b67e`). Bulk CpuSet + per-slice
serial gate (`fc5d669`). Idle event batching (`cgba_idle_wait`) + bulk
IRQ-wrapper stack push: `update_gba` C round-trips 3.02M → 1.21M
(`8808169`). Diag gating + timer cap mask (`4d4089e`).

**The fidelity morality tale.** An infidel BIOS SWI HLE (C ports of
CpuSet/CpuFastSet/LZ77/RL charging only data accesses) had delivered
SMA2 17.6 → 35.1 fps (`1832fde`), and an interpreter-side variant measured
+2.85 fps — but bisected as breaking Metroid dense bit-exactness (wrong
post-SWI scratch registers, wrong fetch cycles; divergence by frame 200
via a changed IRQ count). Gated off in `4d4089e`; fully demoted in
`b5f3aab` when stop-on-hot promoted SWI call sites and the dense soak
diverged at frame ~420. Cost: AW 40.5 → 31.3. Recovered *faithfully* in
`5b1f5a9`: slice-gated atomic fast path (verify: checked=4889 bad=0) +
the parked/resumable CpuFastSet engine (see
[bios-swi-hle.md](bios-swi-hle.md)) — AW 2.96 Mcyc/frame = **39.8 fps**
vs the 43.7 infidel ceiling, with the dense battery byte-identical.
The lesson, twice paid for: *a plausible-but-slightly-wrong HLE is
strictly worse than a slow interpreter* — the divergence surfaces weeks
later at 10× the debugging cost.

**Hardware crash classes** (all field-reported): EXC=180 stale chains —
cross-cache links, cold-target backfills, dual-hot table not cleared on
RAM flushes (`a7330d7`, `7ea0480`); EXC=0E0 misaligned longword in the
bulk SMC tag scan — hardware-only, calcemu tolerates misaligned loads
(`02d69f0`); EXC=1A0 in-arena at young cache under overclock — the JIT
memory canary (`7b8889e`) distinguishes memory-margin failures from
codegen bugs; an 8000-frame fuzz soak at stock behavior ran clean.

**Display scale modes** (`b249fef`): 4:3 320×212 and fullscreen 384×216,
packed-pair RGB565 blends composed into the strip banks under the LCD DMA;
+1.9/+2.8 Mcyc per rendered frame before DMA overlap. An orthogonal SCALE
FILTER (SMOOTH / SHARP / CRISP-nearest) selects the horizontal row scaler
and vertical tap composition; CRISP is the cheapest (no blends).

**Mario JIT/memory-tail target** (this change set): the Mario harness
(`~/Downloads/Mario.gba`, 1997 frames, START at frame 30, A held from
frame 200, headless JIT, `HLE_TURBO=1`, `HLE_FORCE_R61524=1`) reaches
**45 emulated fps** in calcemu with diagnostics off and
`CGBA_TUNE_HOTFILES_O3=ON`. Correctness anchor is the pre-overlay frame
1996 FBSTAT:
`black=1120/38400 hash=8BEF0CBC p00=FFD3 p11=FFD3 pc=3186`; the final
`fbhash` line is overlay-contaminated and should not be used as the clean
visual hash. The speedup came from pushing more hot helper traffic into
native SH4: direct IWRAM fastmem loads through a vector-table
`iwram + 0x8000` entry, native halfword IO stores for safe window/blend
registers plus IF/IE handling, dead-flag ARM MOV/MVN register-specified
shifts, and a no-active-timer event-loop skip. The last measured step
added native `WININ`/`WINOUT`/`BLDALPHA` stores, dropping Mario's Thumb
ldst helper count to 47,578 in the 1997-frame run. Broad selected-hot-file
`-O3` is part of this measurement: narrowing it back to `cpu.cc` and
`video.cc` kept the same clean hash but fell to 42 fps in the same harness.

**Yoshi/Mario A-LEFT follow-up** (2026-07-08 diagnostic harness):
two correctness issues were fixed while chasing reported frame drops. First,
Thumb conditional idle elimination now parks at the same `idle_loop_target_pc`
as the interpreter, instead of advancing one loop leg to the back-edge target
(Yoshi's `08002BA4 -> 08002B9C` poll exposed this). Second, SH4's Div/DivArm
SWI fast path now matches the bundled open BIOS dispatcher: observable results
are `r0/r1`, while caller `r3` is preserved; DivArm also uses the swapped
operand order. Validation:

- Yoshi (`SUPERM0.SVS`) frame-0 block lockstep changed from
  `B2413 p8002B9C PC i8002BA4 d8002B9C` to `MATCH 4000 blocks`.
- Yoshi 300-frame no-key visual parity is still not solved:
  first FBSTAT mismatch remains frame 25 (`JIT hash=8FA4B031`,
  interpreter `1EB97F2D`), so artifact work is still open.
- Yoshi 600-frame A/LEFT soak (`ALT_LEFT=1`, period 60, from `SUPERM0.SVS`)
  completed all 600 frames and `=== done ===`, but only measured
  `fps emu=12 draw=12` in the diagnostic build
  (`rom_flush=2 ram_flush=3 arm_tx=129 thumb_tx=1121`).
- Mario 600-frame A/LEFT boot soak completed all 600 frames at
  `fps emu=17 draw=17` in the same diagnostic style
  (`rom_flush=2 ram_flush=2 arm_tx=81 thumb_tx=466`). No Mario savestate was
  available, so this is a boot/menu soak, not the 1997-frame gameplay harness.
- Yoshi slot-save repro at frame 120 completed with
  `@@CGBA_SLOTSAVE frame=120 ok=1`, 300 FBSTAT frames, and no `WILD=FFFFFFFF`
  panic signature.

**Yoshi save-state IRQ follow-up** (2026-07-08, `SUPERM0.SVS`):
the save-state crash report pointed at a timing-sensitive IRQ return path.
The SH4 store-alert helper now leaves a bare pending IRQ for the normal
`update_gba` scheduler boundary instead of vectoring immediately at the store
PC; SMC still exits immediately. This matches the interpreter's IRQ service
point and avoids saving a different `LR_irq` before `SUBS PC,LR,#4` returns.
The headless interpreter/JIT comparison harness front-end also now shares the
backup-save cadence macro between JIT and runtime-interpreter comparison
builds. Validation:

- Yoshi 32-frame JIT/interpreter FBSTAT parity still matches through frame 24
  and first diverges at frame 25 (`JIT hash=8FA4B031`, interpreter
  `1EB97F2D`). Exact per-instruction cycle-boundary JIT also diverges at
  frame 25 (`EDE03768`), so the visible artifact is not just grouped cycle
  amortization.
- Yoshi 300-frame no-input JIT run with checkpoint save at frame 120 completed
  with `@@CGBA_CHECKPOINT save frame=120 ok=1`, frame 299 FBSTAT, and
  `=== done ===`; no `WILD=FFFFFFFF` or panic signature appeared. Re-enabling
  the IRQ wrapper HLE in the same scenario also completed with the checkpoint
  save and `=== done ===` (`fps emu=19 draw=4` with stats off), so the fix is
  the IRQ service boundary rather than disabling the fast IRQ wrapper.
- Yoshi 600-frame A/LEFT soak (`ALT_LEFT=1`, period 60, final source with IRQ
  wrapper HLE enabled) completed all 600 frames and `=== done ===`, no
  `WILD=FFFFFFFF` or panic signature. Final frame 599 FBSTAT:
  `hash=7FC8363F`; still at `fps emu=11 draw=11`
  (`rom_flush=3 ram_flush=3 arm_tx=139 thumb_tx=2383`). This remains far short
  of the 45 fps gameplay target.

**Yoshi ObjAffineSet/save-state hardening follow-up** (2026-07-08,
`SUPERM0.SVS`): a later 600-frame gameplay report alternated GBA `A` and
`Left` every 60 frames and showed significant frame drops after a short soak.
The profiler counters pointed at one remaining interpreted BIOS hotspot:
Yoshi calls ObjAffineSet (SWI 0x0F) hundreds of times in this scene. The SH4
BIOS fallback now has a faithful, budget-gated ObjAffineSet HLE that uses the
bundled BIOS sine table, preserves the open-BIOS caller-visible register
contract, charges the dispatcher/routine cycles, and declines back to the
interpreter when the call would cross an event-slice boundary. The cold-code
fallback chunk is also a CMake knob (`CGBA_SH4_COLD_CHUNK_CYCLES`) for future
profiling; sweeps from 512 to whole-slice chunks reduced cold fallback entries
by about 10% but did not move the HLE fps counter. Finally, state saving now
flushes both ROM and RAM translation caches after borrowing executable cache
memory for the save buffer; this treats savestate writes as a full JIT cache
coherency boundary, matching the hardware crash signature of a wild in-arena
host PC after saving.

Validation:

- Yoshi 300-frame no-input, interp-stat build: before ObjAffineSet HLE,
  `jit interp-instr bios=1920258` and `jit swi-miss 0F=473`; after it,
  `jit swi-miss` is empty in the same scenario. The final fps counter stayed
  `fps emu=19 draw=4`, so this removes interpreter work without solving the
  late frame-drop target.
- Yoshi 600-frame A/Left soak (`ALT_LEFT=1`, period 60, press width 2,
  frameskip 3) completed all 600 frames and `=== done ===`, with no
  `WILD=FFFFFFFF` or panic signature. Final frame 599 FBSTAT:
  `hash=47DA13A6`; `fps emu=19 draw=4`,
  `rom_flush=2 ram_flush=3 arm_tx=124 thumb_tx=1111 bios_n=8641 cold_n=29697`,
  and `jit interp-instr bios=0 rom=0 ram=0`.
- Yoshi 600-frame A/Left slot-save repro at frame 300 completed with
  `@@CGBA_SLOTSAVE frame=300 ok=1`, wrote a 196,608-byte `GAME0.SVS`
  beginning with `CZS1`, continued to frame 599, and reached `=== done ===`.
  HLE still cannot prove the hardware-only save crash is fixed, but the cache
  boundary has been tightened at the reported failure point.

## State at HEAD

Shipping default is the interpreter (`CGBA_DYNAREC=OFF`); the JIT is the
opt-in hardware artifact with cold gate T=64, heat leak 16, faithful
CpuSet/FastSet HLE on, all infidel HLEs compiled out, IntrWait HLE off.
Modeled numbers on the standing scenarios: AW 39.8 fps, Metroid movement
32.4 fps, Metroid dense parity green, SMA2/Zelda from the 30fps-goal era
35.1/58.5 (pre-demotion protocol — remeasure before quoting). The current
Mario-specific calcemu harness reaches 45 emulated fps with the clean
frame-1996 hash `8BEF0CBC`.

## Future directions

Ordered by expected value; the first two have concrete measurements
behind them.

1. **Extend the parked-HLE treatment to the SWI tail** (tracked as the
   active follow-up). Census per 2000 AW frames: BgAffineSet n=429
   (pure math — model ARM7TDMI early-termination multiply cycles),
   LZ77UnCompWram (0x11) n=5 / 59,480 B + LZ77UnCompVram (0x12)
   n=7 / 71,680 B (token-walking cycle model, same canonical parking for
   slice crossings), CpuSet n=40. This tail is the ~260 kcyc/frame gap
   from 39.8 to the 43.7 infidel ceiling; an AW 45 fps target
   (2.62 Mcyc/frame) needs another ~80 kcyc/frame beyond it.
2. **IntrWait HLE** (`CGBA_SH4_INTRWAIT_HLE`): three hardening layers done,
   one precise wedge left — the ISR acks REG_IF but never writes BIOS_IF,
   so the faithful poll never satisfies. Whatever resolves it must decide
   what the real BIOS observes in that state (hardware test).
3. **AW JIT-vs-interp divergence** (pre-existing, ~51/81 FBSTAT frames
   from boot, present with all SWI HLEs off): not a regression, but until
   it's root-caused AW can't serve as a second bit-exactness anchor.
   Suspects: affine paths, timing-sensitive intro IRQs, open-bus reads.
4. **Renderer**: with the JIT tail shrinking, `render_scanline_text` /
   `render_w_effects` / affine renderers total ~15% of host instructions
   in the AW profile and `update_gba` ~7%. gpSP's renderer is untouched
   so far — palette-conversion caching, per-scanline dirty tracking, or
   SH-4-tuned inner loops are unexplored.
5. **Cold-gate tuning by regime**: the gate's constants (T=64, chunk 512,
   leak 16) were tuned on Metroid movement + AW; a per-game or
   feedback-driven adaptation (e.g. leak scaled by flush period) may buy
   a few fps in other flush-thrash titles. Sweeps showed single-constant
   changes are a wash — the wins historically came from *dynamics* fixes
   (stop-on-hot, probe-only), not constants.
6. **Density round 3 candidates**: per-block register caching of the top
   1-2 guest registers (the load→op→store model leaves obvious wins on
   the table but costs allocator complexity); Thumb const-tracker
   extensions (const stores, const compare folding); block-local CPSR
   liveness across conditional runs.
7. **Audio**: sound is fully stubbed. Any future audio needs the
   sound-timer batching revisited (sample-accurate FIFO DMA re-caps the
   event slice) — budget for a real-time mixer is ~unknown on this CPU.
8. **calcemu alignment checking**: an opt-in mode that faults misaligned
   host-pointer loads would have caught the EXC=0E0 class in soaks
   instead of on hardware.
9. **Overclock support**: cgba targets whatever clock Ptune sets; the
   canary exists, but a menu-visible "memory margin self-test" would cut
   the support burden of overclock-induced EXC=1A0 reports.
