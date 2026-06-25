# fx-CG100 Port Plan

## Goals

The goal is playable-speed GBA emulation on fx-CG100-class hardware. ROM size
is not treated as a primary concern because cartridge ROM can be mapped from
NOR flash into the CPU address space.

The main risk is CPU time. The interpreter is useful for debugging and fallback,
but the emulator should be designed around an SH4 dynamic recompiler.

## Milestones

1. Import and inspect gpSP.
   - Keep upstream untouched where possible.
   - Record the exact upstream snapshot.

2. Build a verifiable SH4 emitter subset.
   - Emit big-endian SH4 instruction bytes explicitly.
   - Compare simple instruction sequences against `sh-elf-as` / `objdump`.
   - Start with register moves, immediate loads, ALU ops, memory ops, and
     branches.

3. Boot with interpreter-only execution.
   - Keep audio disabled at first.
   - Render a 240x160 RGB565 frame into the calculator display path.
   - Use simple test ROMs before commercial games.

4. Wire in an SH4 dynarec build mode.
   - Add `SH4_ARCH` selection beside `ARM_ARCH`, `ARM64_ARCH`, `MIPS_ARCH`,
     and `X86_ARCH`.
   - Add SH4 cache sync for emitted code.
   - Add an SH4 stub equivalent to the existing `*_stub.S` files.

5. Implement Thumb translation first.
   - Arithmetic/logical operations.
   - Load/store fast paths for ROM, EWRAM, IWRAM, VRAM, palette, and OAM.
   - Conditional/unconditional branches.
   - PC writes and ARM/Thumb mode switching.

6. Add ARM-mode translation.
   - Data processing.
   - Single and block memory transfers.
   - Multiply and multiply-long.
   - SWI and exception paths.

7. Profile and specialize.
   - Direct ROM and RAM reads.
   - Lazy flag materialization.
   - Hot memory-region dispatch.
   - Frameskip and audio tradeoffs.

## Early Build Assumptions

- Compile for SH4A big-endian without FPU, matching the calculator toolchain
  style: `-mb -m4a-nofpu`.
- Avoid C floating-point in hot paths.
- Do not assume unaligned host word reads are cheap or valid.
- Treat all GBA guest memory as little-endian, even though the host is
  big-endian.

## ROM Mapping

Mapped NOR flash lets ROM reads avoid file paging and full-ROM buffering.
For the dynarec, ROM instruction fetch should pay endian decode once at
translation time. Runtime ROM data loads still need endian-correct GBA memory
semantics.

## Cache Sync

Generated code must be written back from data cache and invalidated from
instruction cache before execution. The exact SH7305 sequence still needs to be
confirmed against the calculator SDK/runtime we use.
