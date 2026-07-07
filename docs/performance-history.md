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

## State at HEAD

Shipping default is the interpreter (`CGBA_DYNAREC=OFF`); the JIT is the
opt-in hardware artifact with cold gate T=64, heat leak 16, faithful
CpuSet/FastSet HLE on, all infidel HLEs compiled out, IntrWait HLE off.
Modeled numbers on the standing scenarios: AW 39.8 fps, Metroid movement
32.4 fps, Metroid dense parity green, SMA2/Zelda from the 30fps-goal era
35.1/58.5 (pre-demotion protocol — remeasure before quoting).

## Future directions

Ordered by expected value; the first two have concrete measurements
behind them.

1. **Extend the parked-HLE treatment to the SWI tail** (tracked as the
   active follow-up). Census per 2000 AW frames: BgAffineSet n=429
   (pure math — model ARM7TDMI early-termination multiply cycles),
   LZ77UnCompWram (0x11) n=5 / 59,480 B + LZ77UnCompVram (0x12)
   n=7 / 71,680 B (token-walking cycle model, same canonical parking for
   slice crossings), CpuSet n=40. This tail is the ~260 kcyc/frame gap
   from 39.8 to the 43.7 infidel ceiling; the 45 fps target
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
