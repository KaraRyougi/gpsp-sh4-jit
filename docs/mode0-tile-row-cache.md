# Mode 0 background tile-row cache experiment

Implemented on `codex/emerald-speed-pass`, following the five-item speed pass
at `da84fa3`. The comparison baseline includes all four previously enabled
optimizations. This experiment did not establish a useful real-world speedup
and is **disabled by default**. The small changes below are reproducible inside
one deterministic emulator model, but do not resolve model bias or variation
across gameplay scenes. They are not sufficient evidence to enable the cache.

## Selected configuration

`CGBA_VIDEO_TILE_ROW_CACHE=ON` enables the experiment. It caches ordinary Mode 0,
4-bpp FULLCOLOR background rows for both base and overlay layers. Mosaic,
8-bpp, indexed/blended paths and partial edge tiles retain the existing
renderer. The cache has 128 entries of 32 bytes plus a 512-byte palette
snapshot: approximately 4.5 KiB of extra BSS.

Each entry contains the raw VRAM row word, palette/flip generation tag, eight
RGB565 pixels and a transparency mask. The already decoded tile number indexes
the table; the actual VRAM content remains part of the key. CPU, DMA and native
JIT VRAM writes therefore cannot reuse stale pixels and need no new write hook.
Byte swapping and color expansion happen on misses only.

A separate palette dirty bit is published by existing CPU/DMA/native stores
and reset/state-load paths. The cache compares only the BG palette, preserving
entries across identical writes and OBJ-only palette changes. A real BG color
or backdrop change advances the generation; wrapping the generation clears
the entries. Other palette caches consume their own dirty bits independently.

The cache dispatch occurs once per scanline span. Empty overlay rows skip
immediately. Miss decoding is kept outside the inlined hit path; the hit path
holds the transparency mask in a local value to avoid reloading it after pixel
stores. Fully covered rows use four 32-bit copies when the destination is
aligned; odd-pixel destinations use explicit halfword copies, avoiding SH4
alignment exceptions.

## Emerald performance

Same ROM, checkpoints, inputs and exact casio-emu cycle-window protocol as
[`emerald-speed-pass.md`](emerald-speed-pass.md): 300 frames per scene, stock
clock, every frame rendered, diagnostic counters and frame hashing disabled
during performance runs. FPS is modeled at 118 MHz; these are not hardware
FPS measurements.

| Scene | Cache off, cycles/frame | Cache on, cycles/frame | Modeled FPS, off → on | Change within model |
|---|---:|---:|---:|---:|
| Opening truck | 3,551,856.59 | 3,478,245.28 | 33.22 → 33.93 | **2.12%** |
| Player's house | 4,144,230.95 | 4,111,101.50 | 28.47 → 28.70 | **0.81%** |

The final cache-off control reproduces the previous branch's cycle totals
exactly. A second run of the selected implementation also reproduces its
cycle totals exactly in both scenes.

Larger capacity alone did not solve lookup overhead. The initial 128-entry
prototype was substantially slower despite a 95% hit rate. Skipping empty
rows, separating span dispatch, comparing raw row words, using native tile
attribute bits, and avoiding redundant mask loads were necessary to produce
a reduction in modeled cycles. A larger cache and an extra prepacked layout
for odd destinations did not improve the selected result. Caching both layer types beats caching
only the base or only overlays in both tested scenes.

The result does not demonstrate an effective performance improvement despite
the background renderer's instruction share. An eight-pixel row leaves little
work to amortize cache lookup and memory traffic; the existing unrolled
renderer is already efficient. These opening-gameplay scenes do not establish
performance in battles, late-game areas or other games.

## Validation and reproduction

- Full host suite passes, including 149,519 SH4 execution-oracle cases.
- New tile cache test: 329,681 comparisons covering misses/hits, collisions,
  both flips, transparent/base semantics, odd/even destination alignment,
  palette changes, identical/OBJ-only writes, generation wrap, partial spans,
  map scrolling, and mosaic/8-bpp bypasses.
- The tile test also passes AddressSanitizer and UndefinedBehaviorSanitizer.
- Native STR/STRH tests and production CPU/DMA palette-write tests verify
  cache invalidation notifications.
- Emerald: all 300 post-frame region-hash records and all 300 framebuffer
  records match the previous build in each of the two scenes (600 frames total).
- Metroid Fusion: all 131 sampled framebuffer hashes match the original
  interpreter over the standing 2,600-frame boot check.

Use the prior report's headless build flags with
`-DCGBA_VIDEO_TILE_ROW_CACHE=OFF` and `ON`. Keep
`CGBA_VIDEO_TILE_ROW_PROFILE=OFF` for performance. To check hit/miss counts,
enable it in a separate build; it emits `@@CGBA_TILE_ROWS` before the stop marker.
`CGBA_VIDEO_TILE_ROW_CACHE_ENTRIES` accepts a positive power of two (default 128).

For per-frame correctness, build with `STAT_EVERY=1 STATE_EVERY=1`, and compare
the 600 post-frame memory/framebuffer records with the previous build in each
scene. Run with `tests/emerald_speed_bench.py`, the same patched casio-emu, and
the `emerald-start.SAV` / `emerald-room.SAV` checkpoints described in the prior
report. The runner's `observable_sha256` includes those records and excludes
the pre-frame host timing overlay.

Local ignored assets are under `ports/fxcg100/build-emerald-speed/`:

- `tile-final-truck/` and `tile-final-room/`: final performance, correctness
  and diagnostic logs plus `results.json`.
- `tile-full-tests.log`, `tile-sanitizers.log`: host validation logs.
- `tile-metroid-check/`: the standing 2,600-frame cross-game check.
- `gpSP-CG100-tile-cache.g3a` and `gpSP-CG50-tile-cache.g3a`: playable builds
  of the opt-in experiment, with headless mode disabled and the usual calculator
  overclock setting. They are not the default build configuration.

No ROMs or checkpoints are committed. No physical calculator test was run.
