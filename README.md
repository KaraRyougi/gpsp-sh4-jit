# gpSP with JIT for CASIO fx-CG50 / fx-CG100 graphing calculators

This is an experimental Game Boy Advance emulator for the Casio fx-CG50 / fx-CG100
(SH7305 / SH-4A, big-endian, no FPU) graphing calculators. It is built on gpSP, imported under
`vendor/gpsp`, and adds an SH-4 dynamic recompiler (the "JIT") so GBA games run
at playable speeds on the calculator's CPU.

## Target

- CPU: SH7305, SH-4A family, big-endian, no hardware FPU
- Display: 396x224 calculator LCD, with the GBA frame rendered at 240x160
- ROM: mapped NOR flash, so full-ROM RAM buffering is not the first constraint
- Primary constraint: CPU throughput

## Status

gpSP's interpreter is the correctness baseline and the shipping default
(`CGBA_DYNAREC=OFF`). The SH-4 dynarec is the opt-in speed path: a full
ARM/Thumb host translator with fastmem, dead-flag elimination, block linking,
self-modifying-code handling, and a cold-code translation gate, all pinned to a
hardware-fixed memory layout. It runs real games — the Metroid Fusion
JIT-vs-interpreter frame battery is bit-exact — and reaches roughly 30–40
modeled fps on the standing test scenarios. The measurement methodology, the
commit-anchored optimization chronology, and the remaining bottlenecks are in
[docs/performance-history.md](docs/performance-history.md).

## Documentation

[docs/README.md](docs/README.md) is the index. The current as-built docs:

- [SH-4 JIT architecture](docs/sh4-jit-architecture.md) — dynarec internals:
  translation flow, dispatch, emitters, fastmem, dead flags, block linking,
  SMC, the cold-code gate, and the hardware-pinned memory layout.
- [BIOS / SWI HLE](docs/bios-swi-hle.md) — the BIOS/SWI high-level emulation
  layer and its fidelity harness.
- [Testing & measurement harness](docs/testing-harness.md) — headless harness
  knobs, FBSTAT parity batteries, the exec oracle, calcemu facilities.
- [Performance history](docs/performance-history.md) — methodology, chronology,
  future directions.

The bring-up-era research and design docs (port plan, optimization plan,
implementation-status tracker, dynarec contract, upstream snapshot, and the
new-game decompress-bug postmortem) are kept under `docs/` for history.

## Building the calculator add-in

The calculator port lives in `ports/fxcg100/gint-gpsp` and builds against the
fxSDK / gint. The playable JIT add-in:

```sh
ports/fxcg100/build-calc-jit.sh
```

This emits `ports/fxcg100/gint-gpsp/gpSP.g3a` with the SH-4 dynarec and the
menu / ROM-picker front end. Copy it to the calculator's main memory and put a
GBA ROM named `GAME.GBA` alongside it (or pick one with the in-app ROM browser).
Set `FXSDK_PREFIX=/path` if the fxSDK is not auto-detected.

## SH-4 codegen smoke test

The SH-4 instruction encoder is audited byte-for-byte against `sh-elf-as`. A
minimal host-side smoke test verifies big-endian emission without a calculator:

```sh
cc -std=c11 -Wall -Wextra -I. tests/sh4_codegen_smoke.c -o /tmp/gpsp-sh4-smoke
/tmp/gpsp-sh4-smoke
```

The full regression suite — host unit tests in `tests/`, the on-target headless
harness, and the calcemu round-trip — is documented in
[docs/testing-harness.md](docs/testing-harness.md).
