# cgba

`cgba` is an experimental Game Boy Advance emulator port for the Casio
fx-CG100 / SH7305 target.

The current base emulator is gpSP, imported under `vendor/gpsp` for study and
port work. gpSP is a good fit because its architecture already separates the
ARM7TDMI translator from host-specific dynamic-recompiler emitters.

## Target

- CPU: SH7305, SH-4A family, big-endian, no hardware FPU
- Display: 396x224 calculator LCD, with the GBA frame rendered at 240x160
- ROM: mapped NOR flash, so full-ROM RAM buffering is not the first constraint
- Primary constraint: CPU throughput

## Direction

The port should keep gpSP's interpreter as the correctness baseline and add an
SH4 dynamic recompiler for speed. The first dynarec milestone is a Thumb-only
translator subset, because most GBA game code is Thumb-heavy.

See:

- [fx-CG100 Port Plan](docs/fxcg100-port-plan.md)
- [SH4 JIT & Optimization Plan](docs/sh4-jit-optimization-plan.md)
- [SH4 JIT Implementation Status](docs/sh4-jit-status.md)
- [gpSP Upstream Snapshot](docs/gpsp-upstream.md)
- [SH4 Dynarec Contract](docs/sh4-dynarec-contract.md)

## First Smoke Test

The initial SH4 emitter is intentionally tiny. It verifies byte-level
big-endian SH4 instruction emission before being wired into gpSP.

```sh
cc -std=c11 -Wall -Wextra -I. tests/sh4_codegen_smoke.c -o /tmp/cgba-sh4-smoke
/tmp/cgba-sh4-smoke
```

## fx-CG100 Calculator Port

The calculator build lives in `ports/fxcg100`. The default target is now a
hardware-safe boot/menu smoke add-in, not the full gpSP interpreter. It uses the
standard fx-CG add-in RAM window, keeps the loader-provided stack, avoids the
emulator debug port, skips generated-code probes, and returns through the loader
when explicitly requested. This keeps physical bring-up focused on launch,
direct LCD writes, and keypad behavior before gpSP's larger memory footprint is
reintroduced.

```sh
make -C ports/fxcg100
```

The default build emits `ports/fxcg100/build/cgba.g3a` and
`ports/fxcg100/build/cgba.raw.g3a`. The full gpSP interpreter bring-up target is
still available explicitly:

```sh
make -C ports/fxcg100 BUILD=build-full CGBA_FULL_GPSP=1
```

The physical LCD result showed that the freestanding direct-R61524 path can run
but only produces a single line on hardware. Treat that build as a low-level
probe. The current display bring-up candidate is the `gint`-backed smoke app,
which uses the same fxSDK/gint startup and display driver family as the working
CGBC port:

```sh
cd ports/fxcg100/gint-smoke
fxsdk build-cg -c
fxsdk build-cg
```

It emits `ports/fxcg100/gint-smoke/CGBA-GINT.g3a`. It draws one full-screen
`CGBA GINT SMOKE` frame through `dclear()`/`dtext()`/`dupdate()`, holds briefly,
then returns through the add-in loader. Keyboard is intentionally disabled in
this isolate because direct HLE currently rejects gint's keyscan timer register
write.

The smoke app should show `CGBA SAFE BOOT`, diagnostic stack/key lines, and
bottom color bars. The on-calculator shell uses a CGBC-like `ON` settings key
with gpSP-style per-action input binding:

- `ON`: open the in-game settings menu
- `HOME`: return through the loader from the safe-boot screen after the startup
  grace period
- All other calculator keys, including `TOOLS`, `SHIFT`, `AC`, `HOME`, digits,
  operators, and arrows, are bindable

The default map mirrors CGBC's feel: `A=SHIFT`, `B=ALPHA`, `SELECT=VAR`,
`START=EXE`, D-pad arrows, `L=BEGIN`, and `R=END`. gpSP hotkeys such as fast
forward, load state, save state, save+exit, and display FPS are configured as
separate bindings and default to `NONE`. The menu exposes graphics/frameskip/FPS
display, load/save-state slot, gpSP-style key mapping, config save/load at
`\\fls0\\CGBA.CFG`, cheats/misc, load/restart/return/quit. Manual frameskip
currently skips LCD blits; game save/state storage-backed options are present
but marked TODO until file/NOR save plumbing is added.

An experimental helper for the longer OS/MPM path is also present:

```sh
ports/fxcg100/run-mpm-smoke.sh
```

It follows the local `~/Dev/casio-emu` flash provisioning flow and expects the
documented local assets (`os200_correct.bin`, `fls0_16MiB.bin`, `mpm.bin`, and
`/tmp/os200_mpm_fixed.bin`). Current validation reaches the patched OS MPM entry
point; add-in USB install timing still needs work before this is a full
end-to-end MPM launch test.

For the CGBC-style shortcut, use a copied Zelda flash image and pass only the
`.g3a` as the emulator positional argument:

```sh
ports/fxcg100/run-zelda-flash.sh ports/fxcg100/build/cgba.g3a
```

The current safe build has been verified with this `~/Dev/casio-emu` harness:
the emulator logs direct R61524 writes and `/tmp/cgba-zelda-safe.ppm` captures
the `CGBA SAFE BOOT` screen.

The `gint` display isolate has also been verified through the same shortcut:

```sh
env HLE_EXIT_AT=120000 HLE_GRAMLOG=1 \
  CGBA_FBDUMP=/tmp/cgba-gint-zelda-final.ppm \
  CGBA_LOG=/tmp/cgba-gint-zelda-final.log \
  ports/fxcg100/run-zelda-flash.sh ports/fxcg100/gint-smoke/CGBA-GINT.g3a
```

Expected log shape:

```text
[FLASHLOAD] loaded NOR filesystem (...)
[disp] f=1 driven=1 hot=1 use=0 h[0..395] (88704 writes)
```
