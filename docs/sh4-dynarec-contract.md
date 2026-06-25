# SH4 Dynarec Contract

This document captures the first contract for a gpSP SH4 host emitter.

## Integration Points

gpSP's dynamic core enters through:

- `execute_arm_translate(cycles)`
- `block_lookup_address_arm(pc)`
- `block_lookup_address_thumb(pc)`
- `translate_block_arm(pc, ram_region)`
- `translate_block_thumb(pc, ram_region)`

The existing host emitters are selected in `cpu_threaded.c`:

```c
#if defined(MIPS_ARCH)
  #include "mips/mips_emit.h"
#elif defined(ARM_ARCH)
  #include "arm/arm_emit.h"
#elif defined(ARM64_ARCH)
  #include "arm/arm64_emit.h"
#else
  #include "x86/x86_emit.h"
#endif
```

An SH4 port should add `SH4_ARCH` here and provide `sh4/sh4_emit.h` plus a
matching stub assembly file.

## Register Strategy

SH4 has only sixteen general registers, so it cannot keep every GBA register
resident like AArch64. The first practical mapping should be conservative:

- keep the gpSP register-state base pointer resident,
- keep the cycle counter resident,
- keep the current block PC resident,
- keep a small set of hot guest registers resident,
- spill less common guest registers to gpSP's `reg[]` state,
- keep flags cached only when profitable.

The MIPS emitter is the closest structural reference because it maps a limited
register file and spills through a base pointer.

## First Thumb Subset

The first useful emitted subset should cover:

- `MOV`, `ADD`, `SUB`, `CMP`
- shifts by immediate
- `AND`, `ORR`, `EOR`, `BIC`, `MVN`
- PC-relative loads
- register and immediate `LDR` / `STR`
- unconditional and conditional branches
- `BX` for ARM/Thumb switching

Unsupported instructions can fall back to C helpers or interpreter stubs while
the emitter grows.

## Endianness

SH7305 is big-endian. GBA memory is little-endian.

Rules:

- generated SH4 instruction bytes are emitted in big-endian order;
- guest instruction fetch from ROM/RAM must decode little-endian ARM/Thumb
  opcodes;
- guest data loads/stores must preserve GBA little-endian behavior;
- direct host casts to `u16 *` or `u32 *` must be treated as suspicious unless
  the pointed memory is explicitly stored in host-endian form.

## Block Rules

Generated blocks should return to the gpSP dispatcher when:

- the cycle budget reaches the next event,
- a branch target is not directly chained,
- the block writes PC,
- ARM/Thumb mode changes,
- SWI/IRQ paths are entered,
- translated RAM may have been modified.

## Cache Rules

After emitting code:

- write back the data cache for the emitted range,
- invalidate the instruction cache for the emitted range,
- then publish the block pointer.

RAM-code invalidation must remain separate from host instruction-cache sync:
guest writes to EWRAM/IWRAM invalidate translated guest blocks, while host
cache sync makes newly emitted SH4 instructions executable.
