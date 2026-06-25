# SH4 JIT & Phase 0 — Implementation Status

Tracks execution of [docs/sh4-jit-optimization-plan.md](sh4-jit-optimization-plan.md).
Overclock is out of scope (set externally on the calculator).

Dev environment has the full SH4 cross toolchain (`sh-elf-gcc/as/objdump`) and
`fxsdk`, so encoder output is checked byte-for-byte against the real assembler
and builds are compiled — but on-hardware / casio-emu **runtime** validation of
the dynarec is still pending.

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
- **Flags materialized directly in `REG_CPSR`** (N/Z implemented; C/V are TODO).
  No flag caching; gpSP's dead-flag elimination will make this cheap.
- **Memory through C helpers** (`execute_load_*` / `execute_store_*`); no inline
  fast paths yet.
- Register model: `R14`=reg[] base, `R13`=cycle counter (both callee-saved),
  `R0` kept free (forced index/operand), `R1–R7` scratch / C-args.

These are deliberately the simplest correct choices; the speed work
(resident regs, lazy/dead flags, inline memory, block chaining, idle-loop emit)
layers on top — see the optimization plan.

## Remaining (next milestones)

1. **Complete the gpSP emitter macro contract** (the "REMAINING CONTRACT"
   checklist in [sh4_emit.h](../vendor/gpsp/sh4/sh4_emit.h)) until
   `sh-elf-gcc -DSH4_ARCH -c cpu_threaded.c` compiles and links with the stub.
   This is the bulk of the dynarec and needs runtime validation.
2. **Wire a dynarec build target** for the calculator (add `cpu_threaded.c`,
   define `SH4_ARCH` + `MMAP_JIT_CACHE`, allocate an executable code cache via
   `kmalloc_max`, shrink the translation caches per the plan's RAM budget).
3. **Differential-test** the dynarec against the interpreter on the same ROM
   (keep `dynarec_enable` togglable), then validate on hardware / casio-emu.
4. **Idle-loop emit path** (Part B) — already wired in the interpreter; emit the
   cycle-zeroing skip in the SH4 branch path.
5. **Optimize**: resident hot registers, lazy/dead-flag elimination, inline
   memory fast paths, block chaining.
