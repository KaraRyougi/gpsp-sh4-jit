# Testing & Measurement Harness

Three layers: an on-target headless harness compiled into the add-in, host
unit tests in `tests/`, and emulator-side facilities in casio-emu
("calcemu"). The combination gives bit-exact regression checks, per-cycle
performance measurement, and replayable gameplay assets — all runnable
without touching hardware, with hardware round-trip when it matters.

## 1. On-target headless harness

`CGBA_GPSP_HEADLESS_TEST=ON` makes `main()` short-circuit into
`cgba_headless_test()` (ports/fxcg100/gint-gpsp/src/main.c): auto-boot the
first storage ROM, drive a synthetic/scripted/fuzzed input stream for
`CGBA_GPSP_HEADLESS_FRAMES` frames, stream text diagnostics to the
emulator's debug-putchar port (0xb7000000). Machine-parseable lines are
prefixed `@@CGBA_*`; a run ends with the `jit ...` counter block and
`=== done ===` (then an idle loop — the host kills it on timeout).

Configuration is ~40 CMake cache variables that become compile-time
defines. The important ones:

| Knob (CGBA_GPSP_HEADLESS_…) | Default | Meaning |
|---|---|---|
| `FRAMES` / `FRAME_BASE` | 48 / 0 | run frames [base, base+N) |
| `LOG_EVERY` | 1 | `frame %u before/after` interval |
| `STAT_EVERY` | 0 | FBSTAT hash interval (stat frames always render) |
| `FRAMESKIP` | 3 | render 1 in N+1 frames |
| `DYNAREC` | −1 | −1 = runner default, 0/1 force interpreter/JIT |
| `LOAD_STATE` | 0 | restore `CGBACHK.SAV` before the loop |
| `SAVE_STATE_FRAME` | −1 | write the checkpoint after that frame |
| `SAVE_SLOT_FRAME` | −1 | exercise the *real* per-ROM slot-save path |
| `START_*`, `A_*`, `DOWN_*`, `ALT_*`, `ALT_LEFT`, `RUN_*` | — | fixed input generators (below) |
| `FUZZ_SEED` | 0 | seeded input monkey (below) |
| `SCALE` | 0 | force a display scale mode |
| `DIFF_FRAME`/`DIFF_BLOCKS`, `WINDOW_DIFF_FRAME` | −1/0/−1 | live block diff / one-window interp-vs-JIT diff |
| `STATE_EVERY/START/END` | 0/0/max | `@@CGBA_STATE/IO/HASH/TIMER/DMA` dumps |
| `BENCH_FRAMES`, `TRACE_*`, `PHASE_*`, `DUMP_EVERY` | 0/off | micro-bench, targeted traces, loop breadcrumbs, raw frame dumps |

SH-4 diagnostics knobs (same CMakeLists): `CGBA_SH4_INTERP_STATS`
(~4.5% overhead — per-region interpreted-instruction counters),
`CGBA_SH4_DIAG_COUNTERS` (~0.5% — slice/helper/SWI-census counters),
`CGBA_SH4_SWI_HLE_VERIFY` (predict-vs-interpret SWI comparison),
`CGBA_SH4_DIFF_CHECK_CYCLES=1` (live diff also reports per-block cycle
mismatches), `CGBA_SH4_ARM_DEAD_FLAGS=OFF` (A/B),
`CGBA_SH4_HOT_THRESHOLD=0` (cold gate off).

### Input generators

Fixed generators, all compile-time: START hold/mash
(`START_FRAME/HOLD/PERIOD/PRESS`), A-tap trains (`A_FRAME/HOLD/PERIOD/PRESS`
— the "pulsed A" menu-advancer), alternating A/START windows (`ALT_*` —
the standard boot-to-gameplay recipe), alternating A/LEFT windows
(`ALT_LEFT=ON` with `ALT_PERIOD=60 ALT_PRESS=60` — the Yoshi gameplay
stress), a fixed GBA Down hold (`DOWN_FRAME/DOWN_HOLD`), and
hold-LEFT-flip-RIGHT movement (`RUN_FRAME/RUN_FLIP` — the Metroid movement
soak). The fuzz monkey
(`FUZZ_SEED > 0`, xorshift32) replaces them all: hold one direction 12–43
frames, re-roll a tap every 6–21 frames (A 25%, B 12.5%, START ~3%).

### Framebuffer statistics — the parity primitive

`@@CGBA_FBSTAT frame=%u black=%u/38400 hash=%08lX p00=%04X p11=%04X pc=%04X`
— FNV-1a over the 240×160 RGB565 framebuffer (2 bytes/pixel, high byte
first), plus black-pixel count and three probe pixels. Byte-identical
FBSTAT streams between two runs are the parity criterion. `@@CGBA_HASH`
lines extend this to IWRAM/EWRAM/VRAM/palette/converted-palette/OAM/IO
hashes for narrowing a divergence to a memory domain.

### Persistence between calculator and emulator

The headless harness calls Casio BFile syscalls by absolute address (open
0x803338d0, size 0x80333b04, read 0x80333dc2, create 0x80333ef0, write
0x80333f9e, remove 0x80334212, close 0x80333a4e); calcemu HLEs exactly that
table onto a host directory (`HLE_FLS0=dir`). `CGBACHK.SAV` is the
416 KiB checkpoint blob (raw `gba_save_state` image; the loader also
accepts the word-RLE compressed `.SVS` format, so a state saved on the
calculator can be copied in and used as the deep-gameplay checkpoint). The
`.SVS` slot saves and `CGBACHK.SAV` round-trip between device and emulator.

## 2. Host unit tests (`tests/`)

- **`sh4_exec_oracle.c`** — the JIT's semantic oracle, 148,963 cases at
  HEAD. It emits *real* native code through the production emitter headers
  and executes it in a built-in mini SH-4 interpreter (big-endian memory
  windows, JSR/RTS/delay slots, PC-relative literals, trampoline
  detection), comparing the full register file, the CPSR flag-liveness
  contract, memory effects, and fast-vs-slow routing against C reference
  semantics. Coverage: Thumb shifts (register and immediate), Thumb DP
  formats 2/3/4/5 (including high-register PC redispatch), all 16 ARM DP opcodes × operand2 forms × S-bit,
  dead-flag ARM MOV/MVN register-specified shifts, MRS/MSR/SPSR with mode
  rebanking, PC-literal loads, direct IF/IE and safe display/blend IO
  stores, IWRAM-vector fastmem loads, and the fastmem single/block
  routines and sites. Sound is stubbed in the port, so MP2K audio ALU bugs
  never show in frame hashes — the oracle is the layer that catches them.
- **`sh4_codegen_audit.c`** — every encoder byte-for-byte against
  `sh-elf-as` output.
- **`scale_test.c`** — the RGB565 upscaling cores against a per-channel
  reference (packed-pair avg trick, 240→320 and 240→384 row kernels,
  160→216 vertical map coverage/monotonicity, strip-geometry invariants).
- **`sh4_segmented_pool_test.c`** — production segmented-pool reach,
  deduplication, and branch-over checks.

## 3. Emulator-side facilities (casio-emu)

Env-gated; all combine freely with the headless builds:

- `HLE_TURBO=1` — uncap wall-clock pacing.
- `HLE_CACHESIM=1` — SH7305-like cache model (32 KiB, 4-way, 32 B lines,
  I+operand), penalties `imiss=25 dmiss=25 wb=10 memop=1`; P2 and
  on-chip/P4 segments are uncached (memop-only — XY-RAM writes cost 1).
  `HLE_CACHESIM_EVERY=N` prints a cumulative
  `[CACHESIM] tick: ... cycles=...` line every N frames.
- `HLE_PROFILE=file` — instruction-PC profiler with *dual windows*: P0/U0
  PCs → add-in .text histogram, P1/P2 → physical arena histogram. (The
  single-window version aliased JIT-arena PCs onto .text and contaminated
  a whole diagnosis — profiles from before 2026-07-05 can't attribute
  .text.) `HLE_PROFILE_AFTER=N` opens the capture window at frame N.
- `HLE_FBALL=prefix` (+`HLE_FORCE_R61524=1`) — dump every presented panel
  frame as PPM; the way the scale modes' output geometry was verified.
- `HLE_FLS0=dir` — host directory backing the BFile HLE (read/write).

## 4. Standing protocols

- **Dense parity battery** (the correctness anchor): Metroid, 2600 frames
  from boot, `STAT_EVERY` FBSTAT stream, JIT build vs `HEADLESS_DYNAREC=0`
  interpreter build. Must be byte-identical except the known
  presentation-phase artifacts (fade frames 760/820/2580 and final frame
  2599). Anchor hashes: f1000=71282ED4, f1800=D7DF4354, f2400=197E2B00.
- **Movement soak**: 3000 frames from the deep-gameplay checkpoint
  (`LOAD_STATE=1` + CGBACHK.SAV), hold-LEFT with flips — the cold-gate /
  translation-churn stress. Metric: modeled fps (below).
- **Yoshi A/LEFT soak**: 600 frames from `SUPERM0.SVS` copied in as
  `CGBACHK.SAV`, `LOAD_STATE=1`, `ALT_FRAME=0 ALT_PERIOD=60 ALT_PRESS=60
  ALT_LEFT=ON`, JIT on. July 8 2026 requested soak completed all 600 frames
  under HLE (`=== done ===`) with the final hash-every-frame diagnostic build
  at `fps emu=11 draw=11`, `irqin=101643`, `cap vid=520836`, `rom_flush=3`,
  `ram_flush=3`. With `STAT_EVERY=0`, the cold-gate sweep was:
  threshold 64 → `fps emu=18`, `rom_flush=4`, `thumb_tx=3437`, `cold_n=28262`;
  threshold 96 → `fps emu=18`, `rom_flush=3`, `thumb_tx=2425`, `cold_n=31181`;
  threshold 128 → `fps emu=18`, `rom_flush=3`, `thumb_tx=2379`, `cold_n=37709`;
  threshold 192 → `fps emu=17`, `rom_flush=3`, `thumb_tx=2319`, `cold_n=48945`.
  Default is 96: it keeps the 128-flush/HLE-FPS result while reducing cold
  fallback entries by ~17%. A save-slot repro build
  (`SAVE_SLOT_FRAME=120`, 300 frames) wrote `GAME0.SVS`
  (`@@CGBA_SLOTSAVE frame=120 ok=1`) and reached `=== done ===`; the
  hardware `WILD=FFFFFFFF` save crash was not reproduced under HLE.
  After the palette-store fastmem fix, the same requested 600-frame A/LEFT
  pattern with press width 2 still measured `fps emu=18 draw=3`, but Thumb
  load/store helpers dropped from `110760` to `14122`. The frame-300 slot-save
  repro completed with `@@CGBA_SLOTSAVE frame=300 ok=1` and `=== done ===`.
  After the cycle-parity follow-up, the held-window requested pattern
  (`ALT_FRAME=0 ALT_PERIOD=60 ALT_PRESS=60 ALT_LEFT=ON`) still completes but
  remains at `fps emu=18 draw=3`; the current frame-300 slot-save repro again
  completes with `@@CGBA_SLOTSAVE frame=300 ok=1` and no HLE panic signature.
  During calculator saves, raw and compressed savestate images are staged
  together in the 1 MiB aligned fallback cache. Its 32 KiB aliases are
  invalidated before and after the overwrite, and the separate 256 KiB mini-ROM
  staging buffer remains untouched. This preserves both JIT caches and their
  heat state. State loads still flush translated code after replacing guest
  memory. The headless `SAVE_STATE_FRAME` writer also records every raw BFile
  mutation attempt and refreshes both cartridge maps before continuing, so a
  rejected or partial checkpoint write cannot leave stale NOR pointers hidden
  in a validation run. The patched default-96 run completed all
  600 frames with final `hash=D48DE1EA`, `fps emu=18 draw=3`,
  `rom_flush=3 ram_flush=3 arm_tx=122 thumb_tx=2398 cold_n=30729`; its
  frame-300 slot-save repro reached frame 359 and `=== done ===`.
  The ObjAffineSet HLE experiment is now default-off: the old default first
  diverged visibly from the interpreter at frame 25, a stricter 512-cycle guard
  only delayed the 300-frame no-input mismatch to frame 119, and the default-off
  build delayed it to frame 148 while converging by frame 299. The requested
  held-window A/LEFT soak still completed at `fps emu=18 draw=4`; the matching
  frame-300 slot-save repro completed with `@@CGBA_SLOTSAVE frame=300 ok=1`,
  wrote a 196,608-byte `GAME0.SVS`, and reached `=== done ===`.
  The July 8 cache-sim cold-gate follow-up moved the JIT default from
  T=96/leak=16 to T=8/leak=4: the held-window run improved from 20.52 modeled
  fps (`rom_flush=3 thumb_tx=2388 cold_n=30618`) to 21.35 modeled fps
  (`rom_flush=4 thumb_tx=3836 cold_n=7552`). T=0 was rejected because it fell
  into translation churn and failed to complete within the same cap; leak=0 was
  not chosen despite 21.42 modeled fps because it keeps stale hot counts across
  flush generations and raised translations to `thumb_tx=7146`.
  A BG-scroll fastmem tail follow-up then routed regular BG0/BG1/BG2/BG3
  HOFS/VOFS halfword stores directly instead of through the C helper. The
  300-frame no-input JIT screen check still converged with the interpreter at
  frame 299 (`FBSTAT hash=A57ADB44`). The requested 600-frame A/LEFT cache-sim
  soak completed (`=== done ===`) with no `WILD`/bad-jump crash signature,
  final `fbhash=BF3F6442`, `rom_flush=3 ram_flush=3 arm_tx=122
  thumb_tx=2398 cold_n=30729`, and Thumb helpers `ldst=11806 blk=2064
  div=272`. The gameplay-window model was 21.00 fps
  (`cycles=67601636 -> 3439594101`), so this reduces helper traffic but does
  not materially fix Yoshi's frame drops. The current frame-300 save-slot
  repro also completed with `@@CGBA_SLOTSAVE frame=300 ok=1`, reached frame
  599, and printed `=== done ===`. A wider overlapping CpuFastSet fast-path
  experiment measured about 28.3 modeled fps, but failed the 300-frame
  no-input visual oracle (`A57ADB44` interpreter vs `64D6CA64` JIT at frame
  299), so it was not kept.
  The July 8 late-follow-up rebuilt from clean source after removing that
  overlap experiment and ran the exact held-window input (`ALT_FRAME=0
  ALT_PERIOD=60 ALT_PRESS=60 ALT_LEFT=ON`) for 600 frames. It completed with
  no crash signature, `fps emu=18 draw=4`, `rom_flush=4 ram_flush=2
  arm_tx=41 thumb_tx=3836 bios_n=9816 bios_kc=796 cold_n=7552`, and still
  showed the known late visual drift (`frame 540 hash=05C2726A` vs interpreter
  `9939617C`). A patched save-slot repro at frame 540 ran 620 frames, wrote
  `GAME0.SVS` (`@@CGBA_SLOTSAVE frame=540 ok=1`, 196,608 bytes), reached
  `=== done ===`, and printed no `HOST PC`/`WILD` panic markers.
- **AW measure**: 2000 frames from boot, pulsed-A harness
  (`A_PERIOD=12 A_PRESS=2`), CACHESIM fine ticks (`EVERY=10`).
- **fps model**: `fps = 118e6 / (window_cycles / frames)`, where the run
  window is the CACHESIM cycle delta between the first tick after
  `running N frames` and the last tick before `=== done`. Use fine ticks:
  coarse bracketing once overstated a baseline as 31.9 fps that was really
  27.52. Emulation is fully deterministic per binary — identical cycle
  counts on re-runs; two different numbers means two different binaries
  (which is also how stale-build mistakes get caught).
- **AW JIT-vs-interpreter caveat**: AW's FBSTAT streams have a
  pre-existing ~51/81-frame divergence from boot (present with all SWI
  HLEs off; under investigation). Metroid dense is the bit-exactness
  anchor; AW parity comparisons must be engine-vs-baseline, not
  JIT-vs-interp.

## 5. Reading the counters

End-of-run `jit ...` lines (each gated by its knob): `jit stats`
(rom/ram flushes, arm/thumb translations, `bios_n/bios_kc` fallback entries
and *entry-overhead* kilocycles, `cold_n` cold chunks), `jit interp-instr
bios/rom/ram` (interpreted guest instructions by region — the *real* BIOS
residency; `bios_kc` is only per-call overhead, a distinction that once
mis-sized a whole optimization), `jit swi-census NN n=… words=…`
(per-SWI interpreter fallbacks + payload words), `jit swiv checked/bad`
(verify mode), `jit cap …` (event-slice cap sources), `jit bios entries
swi/irq/other`.
