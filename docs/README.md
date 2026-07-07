# cgba documentation

As-built (current):

- [sh4-jit-architecture.md](sh4-jit-architecture.md) — the SH-4 dynarec:
  translation flow, dispatch, emitters, fastmem, dead flags, block linking,
  SMC, the cold-code gate, the hardware-pinned memory layout.
- [bios-swi-hle.md](bios-swi-hle.md) — the BIOS/SWI HLE layer: fallback
  contract, faithful slice-gated CpuSet/CpuFastSet, the parked/resumable
  FastSet engine, verify harness, why the infidel HLEs were demoted.
- [testing-harness.md](testing-harness.md) — headless harness knobs, FBSTAT
  parity batteries, the exec oracle, calcemu facilities, standing
  measurement protocols.
- [performance-history.md](performance-history.md) — measurement
  methodology and its traps, the commit-anchored optimization chronology,
  state at HEAD, future directions.

Historical (bring-up era):

- [sh4-jit-optimization-plan.md](sh4-jit-optimization-plan.md) — the
  original research + plan.
- [sh4-jit-status.md](sh4-jit-status.md) — MVP-era implementation tracker.
- [sh4-dynarec-contract.md](sh4-dynarec-contract.md),
  [fxcg100-port-plan.md](fxcg100-port-plan.md),
  [gpsp-upstream.md](gpsp-upstream.md),
  [sh4-newgame-decompress-bug.md](sh4-newgame-decompress-bug.md).
