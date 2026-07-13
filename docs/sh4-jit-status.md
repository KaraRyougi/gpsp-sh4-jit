# SH4 JIT & Phase 0 — Implementation Status

> **Historical.** This tracked the original bring-up. The as-built system is
> documented in [sh4-jit-architecture.md](sh4-jit-architecture.md) (JIT
> internals), [bios-swi-hle.md](bios-swi-hle.md) (BIOS/SWI HLE),
> [testing-harness.md](testing-harness.md) (harness + protocols), and
> [performance-history.md](performance-history.md) (methodology, chronology,
> future directions).

Tracks execution of [docs/sh4-jit-optimization-plan.md](sh4-jit-optimization-plan.md).
Overclock is out of scope (set externally on the calculator).

Dev environment has the full SH4 cross toolchain (`sh-elf-gcc/as/objdump`),
`fxsdk`, and casio-emu. Encoder output is checked byte-for-byte against the real
assembler, builds are compiled, and the Metroid interp-vs-JIT harness now runs
under casio-emu.

## Done and verified

| Area | Artifact | How verified |
|---|---|---|
| **SH4 encoder** | [sh4_codegen.h](../ports/fxcg100/sh4/sh4_codegen.h) — full ISA subset (ALU, shifts, mul, mem, branches, system regs) | `tests/sh4_codegen_audit.c` diffs 106 instructions byte-for-byte vs `sh-elf-as`; smoke test for base ops |
| **Literal pool + reg[] access** | [sh4_emit_core.h](../ports/fxcg100/sh4/sh4_emit_core.h) and [sh4_emit_glue.h](../ports/fxcg100/sh4/sh4_emit_glue.h) — bounded production pool segments (dedup/back-patch), guest reg load/store | core audit plus `tests/sh4_segmented_pool_test.c` verify each `MOV.L @(disp,PC)`, reach, branches, and deduplication |
| **Thumb→SH4 data-proc translation** | [sh4_thumb_mvp.h](../ports/fxcg100/sh4/sh4_thumb_mvp.h) — MOV/CMP/ADD/SUB imm, ADD/SUB reg, shifts, ALU reg, N/Z flags into CPSR | `tests/sh4_thumb_mvp_audit.c` checks the emitted SH4 op per Thumb opcode; disassembly inspected |
| **Entry/exit trampoline** | [sh4_stub.S](../vendor/gpsp/sh4/sh4_stub.S) — `execute_arm_translate_internal`, dispatch, `sh4_update_gba`, indirect branches, SMC flush, SWI | Assembles with `sh-elf-gcc`; disassembly reviewed |
| **Build seam** | `SH4_ARCH` include in [cpu_threaded.c](../vendor/gpsp/cpu_threaded.c); [sh4_emit.h](../vendor/gpsp/sh4/sh4_emit.h) | `sh4_emit.h` cross-compiles standalone with `sh-elf-gcc` |
| **Cache sync** | [sh4_cache.h](../ports/fxcg100/sh4/sh4_cache.h) — `OCBWB → SYNCO → ICBI` | Pre-existing; ordering confirmed correct vs SH-4A manual; [jit_probe.c](../ports/fxcg100/jit_probe.c) proves on-device execution |
| **Phase 0 auto-frameskip** | [frame_pacing.c](../ports/fxcg100/gint-gpsp/src/frame_pacing.c) — RTC-windowed adaptive controller behind the menu's "AUTOMATIC" type | `fxsdk build-cg` → `CGBA-GPSP.g3a` builds + links clean |
| **Phase 0 strip-DMA LCD** | [gint_platform.c](../ports/fxcg100/gint-gpsp/src/gint_platform.c): ten 16-row unscaled strips, with opt-in renderer-to-LCD scanline streaming | geometry tests; production cross-link; physical Ace Attorney A/B rejected streaming as slower |
| **Menu display handoff** | gameplay blit narrows the R61524 window; restore full 396×224 via `r61524_win_set` before any gint push ([gint_platform.c](../ports/fxcg100/gint-gpsp/src/gint_platform.c) `restore_full_window`) | builds; menu must render via gint only, never direct DMA |

Run the host suite (needs `sh-elf-as`/`objcopy` on PATH for the audit):

```sh
make -C tests          # smoke + core + thumb + sh-elf-as audit
```

Build the calculator add-in:

```sh
cd ports/fxcg100/gint-gpsp && fxsdk build-cg   # -> CGBA-GPSP.g3a
```

## Backend design choices (correctness first)

- **Guest ARM r0 stays resident in SH-4 R11** while translated code runs.
  Every helper/interpreter boundary publishes it to `reg[]` and reloads it on
  return; the remaining guest registers still use load → op → store.
- **Flags materialized directly in `REG_CPSR`**. Native Thumb data-processing
  emits exact flags for the supported op set; unsupported/rarer forms route
  through the interpreter helpers.
- **Common memory forms use resident out-of-line fastmem routines**; uncommon
  or side-effectful accesses route through `execute_load_*` / `execute_store_*`
  helpers. ARM and Thumb native load/store families remain independently
  switchable for differential tests.
- **The ROM JIT uses one contiguous arena.** Capacity flushes reset the whole
  cache above the resident watermark. The experimental survivor split was
  removed because ROM thrashing is rare in observed play and halving the
  normal working set penalizes the common case.
- **Known ARM/Thumb indirect targets have small direct-mapped microcaches**
  ahead of the branch hash. Release builds compile differential-harness tests
  out, while patch and final-publication I-cache syncs have separate counters.
- Register model: `R14`=reg[] base, `R13`=cycle counter, `R11`=guest r0
  (all callee-saved), `R0` kept free (forced index/operand), and `R1–R7`
  scratch / C-args.

These retain explicit correctness boundaries while the speed work layers on
top — see the optimization plan.

## Current Metroid Fusion harness findings (2026-06-28)

The frame-700 Metroid Fusion new-game freeze is fixed by
`b665a78 Fix SH4 Thumb BL prefix resume state`, with the fx-CG100 key mapping
commit `6561561 fxcg100: map CG100 keys through gint` used for pulsed
`SHIFT`/GBA `A` input. The fix materializes the Thumb `BL` prefix temporary LR
in the SH4 emitter, so a cycle exit between the prefix and suffix resumes with
the correct link value.

Verified with the headless Metroid harness using the fast casio-emu branch:

```sh
START_FRAME=30 START_HOLD=8 \
A_FRAME=120 A_HOLD=<frames> A_PERIOD=120 A_PRESS=6 \
CASIO_EMU=/private/tmp/cgv-casio-emu-block-dispatch \
ports/fxcg100/run-playtest.sh /Users/ryougi/Downloads/Metroid.gba
```

Evidence:

- Baseline before the fix: JIT diverged at frame 700
  (`black=32412/38400 hash=9C795F0E`) while the interpreter frame 700 was still
  rendering (`hash=07989D3C`).
- After the fix, the 760-frame paired run reached frame 759 for both cores with
  `done=1 crash=0`; sampled frames 640/660/680/700/720/740 matched exactly, and
  frame 700 was fixed (`black=0/38400 hash=07989D3C`).
- A 3000-frame paired pulsed-`A` run reached frame 2999 for both cores with
  `done=1 crash=0`. The JIT matched the interpreter exactly through frame 1500;
  sampled framebuffer hashes start diverging at frame 1600, but both continue
  through the intro sequence.

New open correctness issue: the longer JIT-only soak showed black frames at
4000/5000. A focused interp-vs-JIT run from 3000 to 4099 narrowed this to a
dynarec-only transition between frames 3875 and 3900:

| frame | JIT FBSTAT | interp FBSTAT |
|---:|---|---|
| 3800 | `black=739/38400 hash=0FD72129 pc=2AC4` | `black=739/38400 hash=40511CD9 pc=2AC4` |
| 3875 | `black=953/38400 hash=7AFB01DC pc=5407` | `black=1077/38400 hash=A2974FCE pc=5407` |
| 3900 | `black=38400/38400 hash=6AD58DC5 pc=0000` | `black=0/38400 hash=455EE41A pc=5CB9` |
| 4000 | `black=38400/38400 hash=6AD58DC5 pc=0000` | `black=714/38400 hash=023B1E64 pc=DFFF` |
| 4099 | `black=38139/38400 hash=2DEBC153 pc=0000` | `black=2748/38400 hash=143367F9 pc=DFFF` |

Artifacts from that run:

- `/tmp/cgba-metroid-window-3000-4000/jit.log`
- `/tmp/cgba-metroid-window-3000-4000/interp.log`
- `/tmp/cgba-metroid-window-3000-4000/png/contact_interp_jit_3000_4100.png`

Conclusion: the frame-700 BL-prefix crash and BIOS reboot are fixed, but Metroid
still has an accumulated dynarec-only divergence later in the intro. It is not
an expected cutscene fade: the interpreter renders the white/blue transition at
frame 3900 and keeps rendering afterward while the JIT is fully black.

Next correctness target: narrow frames 3875-3900 with denser sampling and branch
state, then run true lockstep/no-reseed diff around the first bad branch or
state write. The old per-block diff still cannot catch accumulated drift because
it reseeds the dynarec from interpreter state at every block boundary.

## Current performance caveat

The present SH4 dynarec should still be treated as a correctness MVP, not a
performance win. On real fx-CG100 hardware, the user reports that JIT does not
noticeably improve performance during the Metroid intro cutscene. That is
plausible for the current backend, but first verify that the hardware artifact
was actually built with the dynarec. `CGBA_DYNAREC` is still CMake-default
`OFF`, so a plain `fxsdk build-cg` produces an interpreter-only `.g3a`.

Build a hardware JIT artifact explicitly:

```sh
cd ports/fxcg100/gint-gpsp
fxsdk build-cg -c -DCGBA_DYNAREC=ON
fxsdk build-cg
```

If the tested `.g3a` was built this way and still shows no gain, that is
plausible for the current backend:

- guest ARM r0 is resident in callee-saved SH-4 R11 while translated code is
  running; the other guest registers still spill through `reg[]`, and helper /
  interpreter boundaries explicitly publish and reload r0;
- uncommon ARM/Thumb operations still route through C helpers; register-count
  shifts and high-register PC data-processing forms are now native;
- native scalar/block memory paths, direct chaining, dead flags, idle-loop
  emit, and known-target microcaches are present, but uncommon memory forms
  still fall back to C and broader guest-value liveness is not implemented;
- cutscenes can be render/LCD/IRQ/timing-bound, so CPU recompilation alone may
  not move the user-visible FPS;
- the focused JIT run by frame 4099 reported heavy translation activity
  (`rom_flush=1780`, `ram_flush=7`, `arm_tx=13640`, `thumb_tx=604734`), so cache
  churn or repeated translation may be eating any native-execution gain.

Do not use casio-emu FPS as final proof here; it is useful for instruction-share
and regression direction only. The next performance pass needs a physical
calculator A/B with the same ROM, scene, frameskip setting, and overlay metric,
plus a split of core time vs render/LCD time and translation/cache-flush counts.

## Dynarec bring-up — DONE (compiles, links, runs the control-flow skeleton)

The three bring-up tasks are complete at the build/link level (runtime
correctness is now the differential harness's job):

| Task | Status | Artifact |
|---|---|---|
| **1. Executable JIT cache** | DONE | The translation caches are placed in the `.cgba.highbss` arena (P1 `0x8c2…`, cached + **executable**) via [sh4_dynarec_state.c](../ports/fxcg100/sh4/sh4_dynarec_state.c), not the no-execute `0x081…` add-in alias. `platform_cache_sync` gains an `SH4_ARCH` branch ([cpu_threaded.c](../vendor/gpsp/cpu_threaded.c)) calling the `OCBWB→SYNCO→ICBI` sequence. The production map must keep both caches in P1 and `_cgba_highbss_end` at or below the hardware-proven ceiling. |
| **2. Differential interp-vs-dynarec harness** | DONE | [sh4_diff_harness.c](../ports/fxcg100/sh4/sh4_diff_harness.c) snapshots reg[]+memory, runs the same window under `execute_arm` and `execute_arm_translate`, and reports the first divergent register/region (interpreter = oracle). `cgba_gpsp_run_frame` dispatches on the live `dynarec_enable` toggle. |
| **3. `sh4_emit.h` glue until `-DSH4_ARCH` links** | DONE | The full 45-macro host-emitter contract is implemented in [sh4_emit.h](../vendor/gpsp/sh4/sh4_emit.h) + [sh4_emit_glue.h](../ports/fxcg100/sh4/sh4_emit_glue.h); `sh-elf-gcc -DSH4_ARCH -c cpu_threaded.c` compiles clean and the whole `CGBA_DYNAREC=ON` add-in links into `CGBA-GPSP.g3a`. |

Build it: `cmake -B build-cg -DCGBA_DYNAREC=ON && cmake --build build-cg`
(the default `OFF` build stays interpreter-only and is the shipping target).

**Bring-up design choices (correctness deferred to the harness):**
- Bounded segmented literal pools deduplicate large constants while keeping
  every `MOV.L @(disp,PC)` in range; resident routines retain self-contained
  literals. All block exits funnel through one `sh4_block_exit` stub entry.
- Thumb data-proc / immediate-shifts / branches / conditions / cycle counter
  emit real SH4; memory, block transfers, register-amount shifts, ARM
  data-proc, multiply(-long), PSR, SWAP route to C helpers
  ([sh4_interp_helpers.c](../ports/fxcg100/sh4/sh4_interp_helpers.c), correct by
  reuse of `read_memory`/`write_memory`).
- **Current Metroid result** (`ports/fxcg100/run-jit-diff.sh
  ~/Downloads/Metroid.gba`, `SHIFT` pressed every 200 frames): the early native
  flag-liveness and not-taken Thumb conditional-branch cycle bugs are fixed.
  The remaining first end-to-end divergence is frame 11, isolated by the
  preserving window diff to `REG_TM0D` (`io@100`) with registers and main memory
  matching. Disabling native Thumb byte loads leaves the same timer skew, so the
  residual is the coarse loop/event boundary around the tight byte-copy loop at
  `08004C76`, not the native LDRB fast path.

## Remaining (next milestones)

1. **Tighten cycle/event boundary accuracy** for hot Thumb loops. The current
   diagnostic target is the frame-11 Metroid timer skew at `08004C76`, where the
   JIT and interpreter reach equivalent CPU/memory state but stop on different
   instruction boundaries inside a byte-copy loop.
2. **Run the differential harness longer** on casio-emu / hardware after the
   timer skew is closed. The harness is
   `ports/fxcg100/run-jit-diff.sh ~/Downloads/Metroid.gba`; it advances the
   title/game flow with `SHIFT`'s default GBA `A` binding every 200 frames.
3. **SWI / BIOS HLE** and additional PC-write redispatch edge cases.
4. **Idle-loop emit path** (Part B) — already wired in the interpreter; emit the
   cycle-zeroing skip in the SH4 branch path when `pc == idle_loop_target_pc`.
5. **Optimize**: resident hot registers, lazy/dead-flag elimination, inline
   memory fast paths, real block chaining (replace the C-helper handlers).
