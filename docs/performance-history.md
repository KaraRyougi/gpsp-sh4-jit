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

**Yoshi palette-store fastmem follow-up** (2026-07-08, `SUPERM0.SVS`):
the requested A/LEFT soak exposed a JIT memory tail that was hidden after the
BIOS HLE fixes. A detail-counter build showed the remaining Thumb load/store
helper storm was nearly all palette RAM writes:
`thumb ldst=65398`, `vid=57996`, `video pal=57996 vram=0 oam=0`, all caused
by unmapped video-region stores. The SH4 fastmem routines now handle aligned
16/32-bit palette stores natively, updating both `palette_ram[]` and
`palette_ram_converted[]`, and also synthesize side-effect-free VRAM store
page bases when `memory_map_read[]` has no VRAM entry. Validation:

- Yoshi 360-frame detail build, A/LEFT every 60 frames: after the palette
  path, `jit helpers thumb ldst=7402` and `jit thumb ldst video pal=0 vram=0
  oam=0`; pre-patch was `thumb ldst=65398` with `video pal=57996`. The frame
  hashes at frames 0/120/240 stayed `0D552414`/`A67C95C0`/`A1DD1CB7`.
- Yoshi 600-frame A/LEFT soak (`ALT_LEFT=1`, period 60, press width 2,
  frameskip 3) completed all 600 frames and `=== done ===`; Thumb load/store
  helpers dropped from `110760` to `14122`. The HLE wall-clock counter stayed
  `fps emu=18 draw=3`, so this removes calculator-side helper work but does
  not by itself prove a 45 fps Yoshi result in calcemu.
- Yoshi 360-frame slot-save repro at frame 300 completed with
  `@@CGBA_SLOTSAVE frame=300 ok=1`, final frame 359 FBSTAT `hash=36130106`,
  and `=== done ===`. The user-reported hardware/menu `WILD=FFFFFFFF` crash
  still needs an on-device or exact-menu reproduction; this HLE path did not
  reproduce it.
- Host emitter/oracle suite passed: `make -C tests`, including
  `SH4 exec oracle passed (143381 native cases)`.

**Yoshi cycle-parity follow-up** (2026-07-08, `SUPERM0.SVS`):
the frame-23 lockstep probe exposed two SH4 timing mismatches. The threaded
ARM translator was still carrying gpSP's old fixed ARM multiply approximations
(`MUL` +2, `MLA`/long multiplies +3), while the interpreter oracle charges no
extra cycles for those instructions. After zeroing those extras on SH4, the
next live mismatch was Yoshi's Thumb `SWI 6` divide veneer at `0812F6C4`: the
JIT had been using the old flat divide HLE cost, then was one cycle high after
modeling the open-BIOS Div body. The HLE now computes the dispatcher/body
charge from the actual operands and debits it at runtime.

Validation:

- `make -C tests` passed; `sh4_cycle_audit` now covers ARM multiply no-extra
  classes and Thumb HLE Div, and its self-test detects the old flat
  64-cycle Div charge.
- Yoshi frame-23 live cycle probe no longer reports the earlier ARM multiply
  mismatch or the `0812F6C4` Div mismatch.
- Yoshi 300-frame no-input oracle/JIT diff still diverges immediately in
  IWRAM/IO and first diverges visibly at frame 25:
  interpreter framebuffer `1EB97F2D`, JIT `A57ADB44`. The artifact is still
  open.
- Requested Yoshi 600-frame A/LEFT gameplay soak
  (`ALT_FRAME=0 ALT_PERIOD=60 ALT_PRESS=60 ALT_LEFT=ON`, loaded from
  `SUPERM0.SVS`) completed all 600 frames and `=== done ===`, but remains slow:
  final frame 599 `hash=3ED0BFE4`, `fps emu=18 draw=3`,
  `rom_flush=3 ram_flush=3 arm_tx=140 thumb_tx=2387 bios_n=9122 cold_n=37236`.
- The same Yoshi A/LEFT soak with real slot-save at frame 300 completed with
  `@@CGBA_SLOTSAVE frame=300 ok=1`, produced a 196,608-byte `GAME0.SVS`, and
  reached `=== done ===` with no `WILD=FFFFFFFF`/panic signature under HLE.
- Mario 600-frame A/LEFT boot soak with the same 60-frame held-window pattern
  completed at `fps emu=29 draw=7`, final frame 599 `hash=AD79546F`,
  `rom_flush=2 ram_flush=2 arm_tx=84 thumb_tx=739`. This is below the separate
  45-fps Mario gameplay harness noted below, so it should not be quoted as the
  achieved Mario target.

**Yoshi ObjAffine default-off follow-up** (2026-07-08, `SUPERM0.SVS`):
the 300-frame no-input oracle check showed that the ObjAffineSet HLE was not
event-slice faithful enough for release. The old default first diverged visibly
at frame 25; a stricter 512-cycle budget guard passed the 30-frame smoke but
still diverged at frame 119 and ended frame 299 with a different framebuffer
hash. Disabling ObjAffine HLE by default moves the first visible mismatch to
frame 148 and converges by frame 299 (`fb=A57ADB44`, VRAM/OAM also matching),
while leaving the remaining mid-window Yoshi timing drift documented as open.
`jit swi-miss 0F=505` confirms ObjAffineSet is interpreted in the 300-frame
diagnostic build.

The requested held-window A/LEFT soak was rerun with the default-off build:
`ALT_FRAME=0 ALT_PERIOD=60 ALT_PRESS=60 ALT_LEFT=ON`, 600 frames from
`SUPERM0.SVS`, completed at frame 599 and reached `=== done ===` with
`fps emu=18 draw=4`, `rom_flush=3 ram_flush=3 arm_tx=123 thumb_tx=2387
cold_n=30545`. A cache-sim run of the same binary gave a modeled gameplay
window of about 20.8 fps and did not show a worsening per-tick cycle trend
after warmup. The matching save-slot repro (`SAVE_SLOT_FRAME=300`) completed
with `@@CGBA_SLOTSAVE frame=300 ok=1`, wrote a 196,608-byte `GAME0.SVS`,
continued to frame 599, and had no `WILD=FFFFFFFF`/panic/TLB signature under
HLE. The hardware-only save crash is therefore still not reproduced, but the
HLE-tested save path survives the reported gameplay point.

**Yoshi save-staging/cold-gate follow-up** (2026-07-08, `SUPERM0.SVS`):
the requested held-window A/LEFT run was profiled with calcemu's PC histogram.
The largest text buckets still map to `execute_arm`, reached through
cold-code/BIOS fallback rather than native translated blocks; the final
diagnostic line confirmed `jit interp-instr bios=0 rom=0 ram=0` was not enabled
in that build, so the reliable live counter is `cold_n`. The default cold gate
is now 96 instead of 128 because the existing sweep kept the same
`fps emu=18` and `rom_flush=3` result while reducing cold fallback entries from
`37709` to `31181`. The patched default-96 build completed the exact
600-frame held-window run with final `hash=D48DE1EA`, `fps emu=18 draw=3`,
`rom_flush=3 ram_flush=3 arm_tx=122 thumb_tx=2398 cold_n=30729`.

The save crash photo (`WILD=FFFFFFFF`, host PC in the JIT arena) also motivated
hardening the savestate staging path. JIT saves now capture the raw 416 KiB
state into the already-linked high-RAM checkpoint buffer instead of overwriting
the ROM translation cache with the raw BSON image. The compressed output stream
still borrows the ROM cache as scratch, but it is bracketed by full dynarec
flushes. This does not prove the hardware-only save crash is gone, but it
removes the largest avoidable executable-cache mutation from the save path. The
patched frame-300 slot-save repro reached frame 359 and `=== done ===` under
HLE, with `@@CGBA_SLOTSAVE frame=300 ok=1`.

**Yoshi cold-gate cache-sim sweep** (2026-07-08, `SUPERM0.SVS`):
the current A/LEFT scene was remeasured with `HLE_CACHESIM_EVERY=10` and the
same 600-frame held-window input. The PC histogram showed `.text` samples still
dominated by `execute_arm`; JIT-arena samples were only about 2.4%, so the next
safe knob was reducing repeated cold fallback without falling into translation
thrash.

Measured modeled fps:

- T=96/leak=16 default: 20.52 fps, `rom_flush=3`, `thumb_tx=2388`,
  `cold_n=30618`.
- T=64/leak=16: 20.65 fps, `rom_flush=4`, `thumb_tx=2648`,
  `cold_n=25379`.
- T=32/leak=16: 20.79 fps, `rom_flush=4`, `thumb_tx=3447`,
  `cold_n=20103`.
- T=16/leak=16: 20.92 fps, `rom_flush=4`, `thumb_tx=3640`,
  `cold_n=16037`.
- T=8/leak=16: 21.20 fps, `rom_flush=5`, `thumb_tx=4020`,
  `cold_n=10248`.
- T=4/leak=16: 21.13 fps, `rom_flush=6`, `thumb_tx=5321`,
  `cold_n=7995`.
- T=0 was rejected: it did not reach `=== done ===` inside the same cap and
  modeled only about 7.5 fps from the partial cache-sim window.
- T=8/leak=4: 21.35 fps, `rom_flush=4`, `thumb_tx=3836`, `cold_n=7552`.
- T=8/leak=0 was slightly faster (21.42 fps) but paid `rom_flush=6` and
  `thumb_tx=7146`; no-decay heat is deliberately not the default because stale
  hot counts survive across generations.

The opt-in JIT default is now T=8/leak=4: a conservative 4% Yoshi gain over
T=96/leak=16 while preserving leaky-bucket decay and avoiding the
translate-everything cliff.

**Yoshi BG-scroll fastmem follow-up** (2026-07-08, `SUPERM0.SVS`):
the shared fastmem IO16 store tail now handles regular BG0/BG1/BG2/BG3
HOFS/VOFS halfword stores directly. These registers are plain `io_registers`
writes, unlike the affine reference registers later in the IO page, so this
keeps Yoshi's per-frame scroll traffic out of the C helper without expanding
every translated Thumb `STRH` site.

Correctness/perf evidence:

- 300-frame no-input interpreter/JIT visual check: frame-299 post-render
  framebuffer hash matched at `A57ADB44`.
- Requested 600-frame A/LEFT soak (`ALT_FRAME=0 ALT_PERIOD=60 ALT_PRESS=60
  ALT_LEFT=ON`) completed under cache-sim with no crash signature, final
  `fbhash=BF3F6442`, `rom_flush=3 ram_flush=3 arm_tx=122 thumb_tx=2398
  cold_n=30729`, and Thumb helpers `ldst=11806 blk=2064 div=272`.
- Modeled gameplay speed for that window was 21.00 fps
  (`cycles=67601636 -> 3439594101`). The previous T=8/leak=4 sweep measured
  21.35 fps on a slightly different binary, so the BG-scroll fast path is
  helper-count cleanup rather than a meaningful speed win.
- Frame-300 save-slot repro completed with `@@CGBA_SLOTSAVE frame=300 ok=1`,
  reached frame 599, and printed `=== done ===`; the reported
  `WILD=FFFFFFFF` save crash did not reproduce in HLE.
- An overlapping CpuFastSet widening experiment reached about 28.3 modeled
  fps, but failed the 300-frame no-input visual oracle (`A57ADB44` interpreter
  vs `64D6CA64` JIT at frame 299, with a transient `PC=000007A4`), so it was
  reverted and is not part of HEAD.

**Yoshi late A/LEFT save-crash follow-up** (2026-07-08, `SUPERM0.SVS`):
the exact held-window request was rerun from clean source: `ALT_FRAME=0
ALT_PERIOD=60 ALT_PRESS=60 ALT_LEFT=ON`, 600 frames, `STAT_EVERY=60`, JIT on.
The run completed with no crash signature and reported `fps emu=18 draw=4`,
`rom_flush=4 ram_flush=2 arm_tx=41 thumb_tx=3836 bios_n=9816 bios_kc=796
cold_n=7552`. It still diverges visually late in the window (`frame 540
hash=05C2726A`; interpreter oracle for the same sample is `9939617C`), so the
frame-drop/correctness work remains open.

The reported hardware save crash pointed at a `WILD=FFFFFFFF` JIT-era failure
after a warmed-up Yoshi scene. The compressed savestate stream had been using
`rom_translation_cache` from byte 0 as scratch; that can overwrite the resident
BIOS/SWI entry below `rom_cache_watermark`, while a normal dynarec flush
intentionally preserves the watermark. The save/load staging scratch now starts
above `rom_cache_watermark` and guards against a rounded compressed file
exceeding the remaining ROM-cache space. The patched repro build saved at frame
540, wrote `GAME0.SVS` at 196,608 bytes, reached frame 619 and `=== done ===`,
with no `HOST PC`/`WILD` panic markers under HLE. This is a targeted cache
coherency fix for the save path; it does not make Yoshi a 45-fps release.

## Zelda checkpoint: full-render result and withdrawn milestones (2026-07-09)

The requested `ZELDA0.SVS` run uses `Zelda.gba` (`AZLE`, A Link to the
Past/Four Swords) for 600 deterministic frames. The exact cache-sim window is
the first tick after `loaded OK; running` through the last tick before frame
599. The experimental build combines faithful forward-overlap CpuSet, the
Thumb divide suffix, stable-palette blend terms, opaque 4-bpp row
specialization, the signature-checked AZLE idle hint, bulk CpuSet-to-OAM
writes, and a signature-gated m4a/Sappy four-sample mixer fusion. These remain
opt-in CMake options.

The honest comparison renders every frame (`frameskip=0`):

| Build | Modeled cycles/frame | Modeled fps |
|---|---:|---:|
| New Zelda options off | 8,055,579.530 | 14.648232 |
| New Zelda options on | 5,819,081.298 | 20.278115 |

This is a real 38.43% modeled-fps improvement (27.76% fewer modeled cycles),
but it is not close to 45 fps. Earlier figures of 45.421191 fps at one rendered
frame in six and 60.168423 fps with rendering suppressed are diagnostic
ceilings only. Frameskip is an existing gpSP feature and does not count as an
emulation-speed improvement; rendering suppression does not measure playable
full-render speed. The previous 45/60-fps milestone claim is withdrawn.

The final mixer/OAM ON and OFF builds produced 110 byte-identical checkpoints
(pre/post state every 60 frames): all registers/CPSR, scheduler clocks,
interrupt and IO state, timers, DMA, IWRAM/EWRAM/VRAM/palette/OAM hashes, and
framebuffer hashes matched. The host suite also covers 300,000 mixer
state/memory/cycle cases, 645,990 OAM cases, the emitted udiv prefix, renderer
differentials, and the 143,383-case SH4 execution oracle; ASan/UBSan is clean
for the new mixer and OAM models.

The first production package from this work is also withdrawn: it accidentally
retained `sh4_diff_harness.c`'s emulator-only 0x78400-byte snapshots in
`.cgba.highbss`, ending at 0x8c6cd700 rather than the hardware-proven ceiling
0x8c655300. `start_gpsp()` cleared that invalid range while loading a ROM,
which explains the reported OS reset on `Emerald.trimmed.gba`. Production now
compiles the differential harness out, the linker and runtime both enforce
0x8c655300, and savestate staging borrows the existing GamePak page cache. The
corrected production image ends exactly at 0x8c655300 and passes the host suite
and headless save/load test. Physical fx-CG100 testing then confirmed that both
`gpSP-hw-layout-baseline-test.g3a` (new Zelda options off) and
`gpSP-hw-layout-zelda-opt-test.g3a` load and run `Emerald.trimmed.gba` without
an OS reset. The optimized build was qualitatively a little faster on hardware;
the subsequent Oldale Town A/B below provides the first numeric hardware result.

RTC follow-up: those first hardware-layout candidates predated main commit
`f1c1549` (`gba: enable cartridge RTC support`) and launched ROMs with RTC
explicitly disabled. The RTC change is now ported into this branch without the
unrelated web-ROM-trimmer changes: cartridge RTC is autodetected, fx-CG100
builds use the calculator wall clock, and the S-3511A reset/status/edge protocol
matches Pokemon Emerald. The focused loader/GPIO test and full host suite pass;
the rebuilt baseline and optimized images still end `.cgba.highbss` exactly at
0x8c655300. Physical validation of Emerald's clock-dependent behavior remains
pending for the new RTC-enabled images.

**Emerald hardware A/B** (Oldale Town walking, same calculator/setup): the
original safe-layout baseline reported 44 emulated fps and the eight-option
optimized build 47 emulated fps. That is a nominal 6.82% throughput gain, or
6.38% less time per emulated frame (22.73 ms to 21.28 ms). The on-device meter
uses integer, roughly one-second windows, so this is a combined directional A/B
rather than precise per-feature attribution. The bundle enables forward-overlap
CpuSet/FastSet, halfword CpuSet-to-OAM bulk, the Thumb libgcc divide suffix, the
exact m4a mixer loop, three renderer paths (stable blend terms, opaque 4-bpp
tiles, unrolled opaque rows), and the AZLE idle hint. AZLE is definitively
irrelevant to Emerald; static ROM inspection finds the divide signature twice
at 0x082e7582 and 0x082e7b98 but no exact mixer signature. The most plausible
contributors in Oldale are therefore the divide suffix and opaque 4-bpp
renderer paths. Renderer-only and divide-only hardware builds are needed to
split the measured gain.

**Emerald held-Down renderer pass** (2026-07-09): the deterministic follow-up
loads `EMERAL0.SVS` with `Emerald.trimmed.gba`, disables frameskip, and holds
GBA Down for all 300 frames (`DOWN_FRAME=0`, `DOWN_HOLD=300`). The checkpoint
is a four-background Mode 0 scene whose final-frame SH profile put 54.2% of all
sampled host instructions in the two `u16` FULLCOLOR text-background
renderers. Cache flushes were zero, the ARM mixer had zero tries, and all 581
missed ObjAffineSet calls used the tiny `count=1, offset=2` form, so another
game-specific idle hint or BIOS shortcut was not the right lever.

The exact renderer change has two parts. Opaque 4-bpp FULLCOLOR rows now use an
eight-pixel `u16` specialization with constant destination offsets, removing
the pointer/counter/branch loop bookkeeping. Base rows use a 256-entry shadow
of the converted BG palette whose color-zero slot in every sub-palette is the
backdrop color; this makes zero and nonzero nibbles the same unconditional
lookup and lets the base specialization bypass the opacity classifier. The
shadow is invalidated independently of the blend cache by every CPU/DMA,
native-JIT, reset, and savestate palette mutation. This is a general Mode 0
renderer optimization, not a Pokemon PC hint.

Fine-tick cache-sim results for the same full-render 300-frame window:

| Renderer build | Modeled cycles/frame | Modeled fps |
|---|---:|---:|
| Existing optimized build | 4,568,522.610 | 25.828919 |
| + FULLCOLOR `u16` row unroll | 4,232,726.960 | 27.878009 |
| + base backdrop-shadow palette | 4,083,763.963 | 28.894912 |

The retained result removes 10.61% of modeled cycles and raises modeled fps by
11.87%. A proposed 128-entry expanded-row cache was measured and removed: once
the direct row writer was unrolled, cache tag/hash/copy work plus D-cache
pressure regressed the result to 4,531,654.687 cycles/frame (26.039054 fps).

The 300 post-frame region hashes and FBSTAT records are byte-identical before
and after (stream SHA-256
`2c7368243cb6a1331a79935298a159b51f7308c6d12c519b2dcf3ce223b947b6`).
Frame 299 remains `iw=AB58DF3A`, `ew=C1CF3CD1`, `vr=8FD659AD`,
`pal=919B865B`, `oam=9EBEB785`, `io=22D10517`, `fb=9D0B6330`.
Focused renderer tests cover 1,407,616 opaque-row cases and 1,356,940
backdrop-shadow pixel comparisons; the full host suite and production SH4
cross-link pass. The calculator candidate keeps RTC enabled and ends
`.cgba.highbss` exactly at the hardware-proven `0x8c655300` ceiling. Its
on-device speed and the 45/60 fps milestones remain pending hardware
measurement; no frameskip result is counted here.

Compiler tuning was not the lever here. GCC 14.1 `-O2` remains the measured
winner: compiling `video.cc` alone at `-O3` added 41,248 bytes and changed this
checkpoint from 36.080742 to 36.079399 fps in the earlier A/B.  The wins came
from reducing exact hot-path work, not switching compiler families.

**Renderer/R11/fastmem placement pass** (2026-07-12): the two exact Emerald
renderer paths above are now production defaults:
`CGBA_VIDEO_OPAQUE_ROW_UNROLL=ON` and
`CGBA_VIDEO_BACKDROP_SHADOW_PALETTE=ON`. The broader stable-palette blend cache
and opaque-tile classifier remain opt-in. Interpreter A/B runs from saved
gameplay checkpoints produced identical FBSTAT streams for all 60 frames in
Emerald, Advance Wars, SimCity 2000, and Yoshi's Island. Zelda matched all 34
common frames before the slower control hit the 60-second instruction-level
emulator cap; neither run produced a mismatch.

The SH-4 backend now keeps guest ARM r0 in callee-saved R11 across translated
code. Stub/helper boundaries explicitly publish and reload it, including HLE
division and fastmem calls. Fastmem sites retain the already-classified GBA
region for cycle charging, direct EWRAM paths use a fixed region charge, and
palette stores use their fixed native charge. The EWRAM specialization also
fixed a latent mirror-mask error (the full 256 KiB mirror is now selected,
not only its low 16 KiB); the oracle includes a high mirrored address to keep
that correction covered.

Placement is now split by physical on-chip bank, accounting for the aliasing
of all `e500xxxx` XRAM windows and all `e501xxxx` YRAM windows. The 2,064-byte
dispatch-stub section links at `e5200000` in the 4 KiB ILRAM budget. The 8 KiB
generated fastmem buffer occupies XRAM at `e500e000..e5010000`; the LCD
presenter double-buffers smaller strips in the two 4 KiB halves of YRAM so it
cannot overwrite fastmem. A 20 KiB main-RAM backup mode preserves both on-chip
banks across gint world switches. Linker assertions enforce both budgets in
the fx-CG100 and fx-CG50 scripts.

The optimized JIT was compared with an otherwise-identical build with R11
pinning and XYRAM fastmem disabled. Emerald, Advance Wars, SimCity 2000,
Zelda, and Yoshi's Island each completed a six-frame saved-checkpoint run with
byte-identical post-frame IWRAM/EWRAM/VRAM/palette/OAM/IO/framebuffer hashes.
The host suite now includes 143,419 native execution-oracle cases, fixed-cycle
debit assertions, the high EWRAM mirror case, and a resident-image size guard.
Both production calculator cross-links pass with `.cgba.highbss` unchanged at
`0x8c5d5300`. Physical fx-CG100 validation remains required for the new
ILRAM/XRAM execution layout, world-switch preservation, and split-YRAM LCD DMA
before treating the placement gain as field-proven.

The first physical fx-CG100 run rejected that combined layout as substantially
slower. The clearest coupled regression is display-side: reserving all of XRAM
for fastmem reduced the scaled presenter from 12/8-row DMA strips to four rows,
doubling or tripling DMA launch/wait frequency. `CGBA_SH4_FASTMEM_XYRAM` is now
an opt-in hardware A/B again; the release candidate keeps generated fastmem in
ordinary executable RAM and restores the full-bank 12/8-row strip geometry.
ILRAM dispatch placement remains enabled for the next isolated hardware test.

**Ranked priority 1–10 follow-on** (2026-07-12): the remaining code paths from
the optimization review were implemented behind independently testable
switches. The priority-2 survivor-generation experiment was subsequently
removed: observed ROM thrashing is rare, while two semispaces halve the normal
one-generation working set and add metadata, promotion, and chain-safety
complexity. Production keeps one contiguous 1 MiB ROM JIT arena and performs a
whole-cache reset only on the uncommon capacity or semantic flush.

Production translation now uses bounded 704-byte literal-pool segments with
per-segment constant deduplication, compact single-fastmem tuples, known ARM
and Thumb target microcaches, classified-region immediate cycle debits, and
native Thumb high-register PC operations. Release assembly and C paths compile
the single-block differential checks away; per-patch I-cache resynchronization
is optional and separately counted from final block publication. Emission
telemetry reports literal-pool use, helper/tuple bytes, total bytes per block,
ARM/Thumb totals, and six block-size buckets.

The unscaled presenter now uses ten 16-row strips per frame. An experimental
`CGBA_LCD_SCANLINE_STREAM=ON` path copies each completed 16-row renderer group
to the DMA-safe YRAM bank immediately. Scaling, FPS-overlay frames, and
incomplete streams fall back to the established end-of-frame presenter.

Physical Ace Attorney testing found the scanline-streamed path substantially
slower. An otherwise-identical build with streaming disabled nearly recovered
the previous release's speed. Follow-up builds disabling guest-r0 pinning and
the known-target microcaches produced no further measurable difference, so
those JIT optimizations remain enabled and scanline streaming returns to an
opt-in hardware experiment.

The full host suite passes, including 148,963 native execution-oracle cases,
1,407,616 opaque-row cases, 1,356,940 backdrop-shadow comparisons, the
segmented-pool reach/dedup test, and the SH-4 assembler audit. Production and
differential-harness calculator cross-links pass.

## State at HEAD

Shipping default is the interpreter (`CGBA_DYNAREC=OFF`); the JIT is the
opt-in hardware artifact with cold gate T=8, heat leak 4, faithful
CpuSet/FastSet HLE on, experimental ObjAffine HLE off by default, all other
infidel HLEs compiled out, IntrWait HLE off.
Guest-r0 pinning, segmented pools, compact fastmem tuples, known-target caches,
exact renderer paths, and 16-row strips are enabled in JIT hardware candidates.
Scanline streaming and XYRAM fastmem default off after physical regressions;
dispatch stubs remain in ILRAM, and the ROM JIT remains one contiguous arena.
Modeled numbers on the standing scenarios: AW 39.8 fps, Metroid movement
32.4 fps, Metroid dense parity green, SMA2 from the 30fps-goal era 35.1
(pre-demotion protocol — remeasure before quoting), the full-render Zelda
checkpoint 14.65 fps with the new options off and 20.28 fps with them on, and
Yoshi A/LEFT about 21.0 modeled fps. The Emerald held-Down checkpoint is 28.89
modeled fps with the exact backdrop-shadow and opaque-row options now enabled
by default. The current Mario-specific
calcemu harness
reaches 45 emulated fps with the clean frame-1996 hash `8BEF0CBC`. The Zelda
45/60 targets have not been reached. Both corrected hardware-layout candidates
boot `Emerald.trimmed.gba` on a physical fx-CG100; the optimized candidate is
qualitatively a little faster than the baseline there.

## Future directions

These are open or partial items, not a list completed by the Zelda work.

1. **Extend the parked-HLE treatment to the SWI tail — partial.** CpuSet has
   faithful atomic handling and parked word/halfword copies, but fills and
   some small non-RAM destinations still fall back. BgAffineSet has no HLE.
   LZ77 WRAM/VRAM only have the old default-off, memory-only implementations;
   they are not canonical parked state machines. The historical census per
   2000 AW frames was BgAffineSet n=429,
   LZ77UnCompWram (0x11) n=5 / 59,480 B + LZ77UnCompVram (0x12)
   n=7 / 71,680 B and CpuSet n=40. The associated ~260 kcycles/frame estimate
   was not remeasured on this dirty tree, and Zelda's final trace had no
   `jit swi-miss` entries. BgAffine timing must not introduce ARM7TDMI
   early-termination multiply charges only in HLE while interpreter and JIT
   deliberately use zero extra multiply cycles.
2. **IntrWait HLE — open and disabled.** `CGBA_SH4_INTRWAIT_HLE` is hardcoded
   to zero. The authentic BIOS and open BIOS both poll/clear BIOS_IF at
   0x03fffff8; the IRQ wrapper does not synthesize it, so copying REG_IF into
   BIOS_IF would be invented behavior. The current prototype also parks at a
   magic PC using hidden, unserialized state instead of materializing the BIOS
   stack/register state. The next step is a signature-validated canonical
   wait-loop hook and interpreted-vs-HLE wake-state comparison; hardware is
   final validation, not the present blocker.
3. **AW JIT-vs-interp divergence — open.** The first A/B should now be the BIOS
   IRQ-wrapper HLE. Its own comment says about ten fetch cycles per IRQ are
   uncharged, and accumulated extra cycles from the bulk push/pop are not
   debited by its current callers. AW takes about 119 IRQs/frame, while the
   single-block oracle deliberately bypasses these hooks. Gate the whole
   wrapper off for an exact JIT/interpreter run before pursuing affine,
   open-bus, or renderer suspects.
4. **Renderer/presentation — partial.** With the JIT tail shrinking, `render_scanline_text` /
   `render_w_effects` / affine renderers total ~15% of host instructions
   in the historical AW profile and `update_gba` ~7%. The Zelda work added
   opt-in stable-palette blend-term caching and an opaque 4-bpp tile fast path.
   The exact unrolled opaque row and backdrop-shadow palette are now production
   defaults after five-game checkpoint testing. The unscaled presenter now
   streams completed 16-row groups to LCD DMA; physical qualification is open.
   Per-scanline dirty tracking, affine inner loops, and broader renderer work
   remain unexplored.
5. **Cold-gate tuning by regime — partial.** The current global defaults are
   T=8, chunk 512, leak 4 after later Yoshi tuning, not the historical
   T=64/leak 16 quoted here previously. Existing feedback also heats by four
   after 180 flush-quiet frames and by one shortly after a flush. Yoshi sweeps
   changed the retained constants (T96/leak16 at 20.52 fps to T8/leak4 at
   21.35 fps); Mario threshold/chunk variants were also tested. There is no
   isolated adaptive-versus-fixed A/B, per-game policy, or feedback-driven
   leak controller. Compare the current +4/+1 heat rule with fixed +1 under
   otherwise identical settings before adding more control dynamics.
6. **Density round 3 candidates — partial; one rejected experiment.** Guest r0
   is now globally resident in R11, but per-block caching of 1-2 additional
   guest registers was not implemented. A local Thumb
   constant-store experiment reduced helper calls but regressed the Mario
   checkpoint to 37 fps and was not retained. Constant-compare folding was not
   implemented. The existing CPSR cache and linear dead-flag pass predate this
   TODO; no new block-local liveness across conditional runs was added. No
   Segmented literal pools and compact single-fastmem descriptors are now
   present; a per-block descriptor table and additional pinned registers are
   still open.
7. **Audio**: sound is fully stubbed. Any future audio needs the
   sound-timer batching revisited (sample-accurate FIFO DMA re-caps the
   event slice) — budget for a real-time mixer is ~unknown on this CPU.
8. **calcemu alignment checking**: an opt-in mode that faults misaligned
   host-pointer loads would have caught the EXC=0E0 class in soaks
   instead of on hardware.
9. **Overclock support**: cgba targets whatever clock Ptune sets; the
   canary exists, but a menu-visible "memory margin self-test" would cut
   the support burden of overclock-induced EXC=1A0 reports.
