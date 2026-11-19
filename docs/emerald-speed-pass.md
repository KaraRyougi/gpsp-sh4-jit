# Emerald speed pass — November 2026

Baseline: `338c3c8` (pinned Docker toolchain). Tested with
`Pokemon - Emerald Version (USA, Europe).trimmed.gba` in casio-emu.
The default combination improves modeled throughput by **1.93%** in the two
short opening-gameplay scenarios below. These are emulator measurements, not
calculator frame-rate measurements or a whole-game performance claim.

## Changes and defaults

| Item | CMake option | Default | Behavior |
|---|---|---|---|
| 1 | `CGBA_SH4_SELECTIVE_R0_SYNC` | ON | Publish pinned guest r0 only when a fastmem store consumes it; reload only when a load/writeback changes it. Slow trampolines retain their own synchronization. |
| 2 | `CGBA_SILENT_TIMERS` | ON | Batch silent timer overflows, preserving final counters, cascades and IRQs. The single-overflow case avoids division. Only suitable for the calculator's no-op sound backend. |
| 3 | `CGBA_BULK_DMA` | ON | Copy ordinary, nonoverlapping, contiguous incrementing spans with `memcpy`, retaining bus value, SMC alerts, OAM invalidation and address updates. |
| 4 | `CGBA_VIDEO_SPRITE_DECODE_CACHE` | ON | Decode visible sprite attributes when OAM ordering is rebuilt; reuse them across scanlines. Affine matrices remain live reads. Costs 4 KiB of BSS. |
| 5 | `CGBA_SH4_REG_FORWARD` | OFF | Experimental write-through forwarding between adjacent eligible ALU instructions. Removes a guest-register reload without adding dirty-register state. Slightly slower in these measurements. |

Bulk DMA falls back for fixed/decrementing transfers, short transfers, physical
overlap (including guest aliases), mirror boundaries, ROM page boundaries,
palette writes, IO, EEPROM and other special regions. ROM source spans must fit
within one resolved 4 KiB page. Guest DMA cycle charging is unchanged.

Forwarding is confined to unconditional non-S ARM ALU operations and selected
dead-flag Thumb operations. Only the first host instruction of the successor
can consume the forwarded value. Internal entries, helpers, control flow and
literal-pool seams cannot inherit it. It remains available for further work,
but the measured default excludes it.

## Measurement

Both baseline and candidate use SH GCC 14.1 from
`cgba-fxsdk:14.1-gint-2bc6f7e`, JIT enabled, stock clock, full rendering
(`FRAMESKIP=0`), 1 MiB ROM / 512 KiB RAM translation caches, and fresh processes.
Each scenario restores the same checkpoint and runs 300 GBA frames. Input:
`A_FRAME=60 A_HOLD=300 A_PERIOD=30 A_PRESS=20 SCRIPT=30:R:120`.

Performance builds disable `LOG_EVERY`, `STAT_EVERY`, `STATE_EVERY` and
`DUMP_EVERY`, and set `CGBA_SH4_BLOCK_PROFILE=OFF` to omit the otherwise implicit
headless block-entry counters. The original source receives only that same
counter-disable switch for a fair comparison. The normal profiling default
remains ON.

casio-emu uses its SH7305-like 32 KiB, four-way cache model with penalties
`imiss=25 dmiss=25 wb=10 memop=1`. The accompanying
[`casio_emu_window.patch`](../tests/casio_emu_window.patch) reports exact
cumulative modeled cycles at the existing `loaded OK; running` and
`@@CGBA_PROFILE_STOP` messages. It modifies host reporting only. FPS is
`118,000,000 / cycles_per_frame`; wall time is not the performance metric.
The patch also lets `HLE_PROFILE` recognize the start message when periodic
frame logging is disabled.

| Scene / variant | Modeled cycles/frame | Modeled FPS at 118 MHz | Throughput vs original |
|---|---:|---:|---:|
| Truck, original | 3,620,333 | 32.594 | — |
| Truck, items 1–3 | 3,552,907 | 33.212 | +1.90% |
| Truck, final defaults (1–4) | 3,551,857 | 33.222 | **+1.93%** |
| Truck, all five | 3,557,336 | 33.171 | +1.77% |
| Player's house, original | 4,224,446 | 27.933 | — |
| Player's house, items 1–3 | 4,145,119 | 28.467 | +1.91% |
| Player's house, final defaults (1–4) | 4,144,231 | 28.473 | **+1.94%** |
| Player's house, all five | 4,147,517 | 28.451 | +1.85% |

Initial individual truck measurements attributed about +1.14% to silent timer
batching, +0.24% to selective r0 synchronization and +0.10% to bulk DMA. Sprite
caching adds only about 0.02–0.03% to the combined build here. Register forwarding
reduces combined throughput by about 0.08–0.15%, so it is default-off. Changes
to code layout affect the modeled cache; individual gains are not additive.
Repeating the original and final truck runs reproduced both cycle totals exactly.

These checkpoints exercise the opening truck and house dialogue/movement,
including the transition into the house during checkpoint preparation. They
do not cover battles, menus throughout the game, or late-game slow areas.

## Where a larger refactor could help

A full-window `HLE_PROFILE` run of the final house build collected 877,990,553
executed SH4 instructions. Symbol attribution used that exact build's ELF:

| Path | Share of executed SH4 instructions |
|---|---:|
| FULLCOLOR text backgrounds, overlay layers | 29.18% |
| FULLCOLOR text backgrounds, base layer | 13.60% |
| Generated code in the JIT arena | 16.38% |
| `update_gba` | 7.28% |
| `execute_arm` | 3.06% |
| `update_scanline` | 2.45% |

These are instruction shares, not cycle/time shares. The profiler's separate
"other" bucket also contains generated resident routines and platform code;
the JIT arena row is not the total cost of guest CPU execution. Full profile:
`build-emerald-speed/emerald-house-full.prof`; symbol summary:
`build-emerald-speed/emerald-house-profile-summary.txt`.

The first structural experiment should target **background tile-row reuse**.
The renderer already has opaque-row unrolling and backdrop-shadow palettes;
another small unroll is unlikely to change its overall cost much. A bounded
cache of decoded/RGB565 rows plus transparency masks could reuse the repeated
4-bpp expansion, palette lookup and flip work. A content-based row key can
observe VRAM changes without adding a check to every VRAM store; palette,
backdrop and rendering-mode changes still need correct invalidation. Start
with ordinary Mode 0 FULLCOLOR rows and keep the existing path for complex
effects, then measure hit rate, cache footprint and modeled cycles before
expanding it.

A second, broader experiment is **basic-block register allocation**: introduce
a small intermediate representation, compute liveness, retain multiple guest
registers across operations, and spill at helpers/exits. This differs from
item 5's single adjacent reload elimination and could remove both loads and
stores. It also requires more invasive correctness work around IRQs, banked
registers, SMC and exception exits; this profile does not establish its gain.

The event scheduler is a third candidate: represent next timer/IRQ deadlines
explicitly and avoid rescanning state on each ordinary event slice. Existing
idle and timer batching must be preserved, with exact guest-visible counter
values at IO reads. Its measured instruction share suggests a smaller ceiling
than the background renderer in this scene. The tile-row experiment was
subsequently implemented; see [mode0-tile-row-cache.md](mode0-tile-row-cache.md)
for its measured result. The register allocator and scheduler proposals remain
unimplemented.

## Validation

- Full `make -C tests`, with `sh-elf-as` on PATH, passes.
- JIT execution oracle: 149,515 native cases, including 552 new adjacent ALU
  execution cases with/without forwarding and explicit boundary resets.
- Timer regression: 4,000 comparisons against overflow-by-overflow behavior,
  covering prescalers, cascades, IRQs and published timer values.
- Bulk DMA: reference-loop comparison across RAM/VRAM/OAM and ROM spans,
  mirror/page boundaries, physical overlap, widths and addressing modes.
- Sprite cache: 153,600 cached-versus-direct scanline comparisons, including
  affine, mosaic, window, mapping and rendering-mode combinations.
- Emerald: original JIT versus final defaults, every post-frame memory-region
  hash and framebuffer hash matches for all 300 frames in each scene. All five
  enabled also matches the truck run. Pre-frame framebuffer hashes are excluded
  because they contain the host timing overlay.
- Metroid Fusion: 2,600 frames from boot, original interpreter versus final
  JIT; all 131 framebuffer samples (every 20 frames plus the last) match.
  Logs and results are in `build-emerald-speed/metroid-parity/`.
- CG100 and CG50 playable JIT add-ins build successfully with the final
  defaults and headless mode disabled. No physical calculator test was run.

## Reproduction and local assets

Apply the host-only patch to a disposable copy of the local casio-emu source
with `git apply --unidiff-zero /path/to/tests/casio_emu_window.patch`, then build it with `USE_SDL_GUI=ON` and a Release configuration. This session's
patched emulator binary SHA-256 is
`f63dc8ba717ee2c99b2070c7f5c6a18739874d5f825a47235dc3195c54b1cf4e`.

Build the add-ins with `CGBA_GPSP_HEADLESS_TEST=ON`,
`CGBA_GPSP_HEADLESS_DYNAREC=1`, `CGBA_GPSP_HEADLESS_LOAD_STATE=1`,
`CGBA_GPSP_HEADLESS_FRAMES=300`, `CGBA_GPSP_HEADLESS_PROFILE_STOP=ON`,
`CGBA_GPSP_HEADLESS_SAVE_STATE_FRAME=-1`, and the measurement/input flags above.
For correctness builds, set `STAT_EVERY=1 STATE_EVERY=1` and compare the
runner's `observable_sha256` (600 records per scene). Do not use these verbose
builds for performance figures.

```sh
python3 tests/emerald_speed_bench.py \
  --emulator /path/to/patched/calcemu \
  --rom '/path/to/Pokemon - Emerald Version (USA, Europe).trimmed.gba' \
  --state /path/to/emerald-start.SAV --frames 300 \
  --output /path/to/results \
  original=/path/to/original.g3a final=/path/to/final.g3a
```

The runner makes a separate BFile directory per variant and records ROM,
checkpoint and binary SHA-256 hashes, cycle totals, logs and observable hashes
in `results.json`. Omit `--state` for a boot test with `LOAD_STATE=0`.

ROM SHA-256:
`3c449ed16fabf12402711a73dffdebbc0f8e72fdbdafd3632fee2422c8c9cdd8`.
No ROM or checkpoint is committed. The generated assets remain in the ignored
`ports/fxcg100/build-emerald-speed/` directory:

- `emerald-start.SAV`: truck checkpoint,
  `8c0c34980b9213393ad205c4b19c5322ecbcbea59e7d150aae9117f9dd78edc2`.
- `emerald-room.SAV`: player-house checkpoint,
  `10a7a4e1d7075c527bb71339cf6221b64da155d1f02baf9c34a4eafbb1cacae1`.
- `refined/`, `room-bench/`: final performance results and logs.
- `final-parity/`, `room-parity/`: dense correctness results and logs.
- `gpSP-CG100-speed.g3a`, `gpSP-CG50-speed.g3a`: playable builds with the
  port's usual overclock setting enabled, unlike the stock-clock benchmarks.

The checkpoints were created from a fresh game with the original build. Each
stage restores the previous stage's `CGBACHK.SAV`, saves on its last frame,
and uses `A_PERIOD=30`, full rendering and no START input:

| Stage | Frames | A frame / hold / press | Script |
|---|---:|---|---|
| Boot (no checkpoint) | 1800 | 60 / 1740 / 2 | empty |
| Introduction | 1200 | 60 / 1200 / 2 | empty |
| Naming and truck | 3000 | 60 / 3000 / 29 | empty |
| Move toward truck exit | 1200 | 60 / 1200 / 29 | `30:R:120,600:D:300` |
| Exit truck and enter house | 1200 | 300 / 900 / 29 | `0:R:300` |

Save the third stage as `emerald-start.SAV` and the fifth as `emerald-room.SAV`.
