# SH4 JIT & Phase 0 — Implementation Status

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
| **Literal pool + reg[] access** | [sh4_emit_core.h](../ports/fxcg100/sh4/sh4_emit_core.h) — 32-bit const materialization via PC-relative pools (dedup, back-patch), guest reg load/store | `tests/sh4_emit_core_audit.c` decodes each `MOV.L @(disp,PC)` to its pool entry; cross-checked with `sh-elf-objdump` |
| **Thumb→SH4 data-proc translation** | [sh4_thumb_mvp.h](../ports/fxcg100/sh4/sh4_thumb_mvp.h) — MOV/CMP/ADD/SUB imm, ADD/SUB reg, shifts, ALU reg, N/Z flags into CPSR | `tests/sh4_thumb_mvp_audit.c` checks the emitted SH4 op per Thumb opcode; disassembly inspected |
| **Entry/exit trampoline** | [sh4_stub.S](../vendor/gpsp/sh4/sh4_stub.S) — `execute_arm_translate_internal`, dispatch, `sh4_update_gba`, indirect branches, SMC flush, SWI | Assembles with `sh-elf-gcc`; disassembly reviewed |
| **Build seam** | `SH4_ARCH` include in [cpu_threaded.c](../vendor/gpsp/cpu_threaded.c); [sh4_emit.h](../vendor/gpsp/sh4/sh4_emit.h) | `sh4_emit.h` cross-compiles standalone with `sh-elf-gcc` |
| **Cache sync** | [sh4_cache.h](../ports/fxcg100/sh4/sh4_cache.h) — `OCBWB → SYNCO → ICBI` | Pre-existing; ordering confirmed correct vs SH-4A manual; [jit_probe.c](../ports/fxcg100/jit_probe.c) proves on-device execution |
| **Phase 0 auto-frameskip** | [frame_pacing.c](../ports/fxcg100/gint-gpsp/src/frame_pacing.c) — RTC-windowed adaptive controller behind the menu's "AUTOMATIC" type | `fxsdk build-cg` → `CGBA-GPSP.g3a` builds + links clean |
| **Phase 0 strip-DMA LCD** | already present in [gint_platform.c](../ports/fxcg100/gint-gpsp/src/gint_platform.c) (`r61524_start_frame` + `dma_transfer_async`, 32B blocks) | reviewed; builds |
| **Menu display handoff** | gameplay blit narrows the R61524 window; restore full 396×224 via `r61524_win_set` before any gint push ([gint_platform.c](../ports/fxcg100/gint-gpsp/src/gint_platform.c) `restore_full_window`) | builds; menu must render via gint only, never direct DMA |

Run the host suite (needs `sh-elf-as`/`objcopy` on PATH for the audit):

```sh
make -C tests          # smoke + core + thumb + sh-elf-as audit
```

Build the calculator add-in:

```sh
cd ports/fxcg100/gint-gpsp && fxsdk build-cg   # -> CGBA-GPSP.g3a
```

## MVP design choices (correctness first)

- **All guest ARM registers stay in `reg[]`**; load → op → store per instruction.
  No host-resident hot registers yet.
- **Flags materialized directly in `REG_CPSR`**. Native Thumb data-processing
  emits exact flags for the supported op set; unsupported/rarer forms route
  through the interpreter helpers.
- **Memory mostly through C helpers** (`execute_load_*` / `execute_store_*`);
  the native Thumb byte-load fast path is guarded by the diff harness
  (`THUMB_LDST_NATIVE=OFF`) so it can be isolated.
- Register model: `R14`=reg[] base, `R13`=cycle counter (both callee-saved),
  `R0` kept free (forced index/operand), `R1–R7` scratch / C-args.

These are deliberately the simplest correct choices; the speed work
(resident regs, lazy/dead flags, inline memory, block chaining, idle-loop emit)
layers on top — see the optimization plan.

## Dynarec bring-up — DONE (compiles, links, runs the control-flow skeleton)

The three bring-up tasks are complete at the build/link level (runtime
correctness is now the differential harness's job):

| Task | Status | Artifact |
|---|---|---|
| **1. Executable JIT cache** | DONE | The translation caches are placed in the `.cgba.highbss` arena (P1 `0x8c2…`, cached + **executable**) via [sh4_dynarec_state.c](../ports/fxcg100/sh4/sh4_dynarec_state.c), not the no-execute `0x081…` add-in alias. `platform_cache_sync` gains an `SH4_ARCH` branch ([cpu_threaded.c](../vendor/gpsp/cpu_threaded.c)) calling the `OCBWB→SYNCO→ICBI` sequence. Verified: `nm` shows `rom_translation_cache @ 0x8c4cede0`. |
| **2. Differential interp-vs-dynarec harness** | DONE | [sh4_diff_harness.c](../ports/fxcg100/sh4/sh4_diff_harness.c) snapshots reg[]+memory, runs the same window under `execute_arm` and `execute_arm_translate`, and reports the first divergent register/region (interpreter = oracle). `cgba_gpsp_run_frame` dispatches on the live `dynarec_enable` toggle. |
| **3. `sh4_emit.h` glue until `-DSH4_ARCH` links** | DONE | The full 45-macro host-emitter contract is implemented in [sh4_emit.h](../vendor/gpsp/sh4/sh4_emit.h) + [sh4_emit_glue.h](../ports/fxcg100/sh4/sh4_emit_glue.h); `sh-elf-gcc -DSH4_ARCH -c cpu_threaded.c` compiles clean and the whole `CGBA_DYNAREC=ON` add-in links into `CGBA-GPSP.g3a`. |

Build it: `cmake -B build-cg -DCGBA_DYNAREC=ON && cmake --build build-cg`
(the default `OFF` build stays interpreter-only and is the shipping target).

**Bring-up design choices (correctness deferred to the harness):**
- Self-contained inline literals (`MOV.L @(disp,PC)` + branch-over) for every
  constant/far target — no deferred pool, nothing range-limited, zero-size
  block prologue. All block exits funnel through one `sh4_block_exit` stub entry.
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
