# SH4 JIT & Performance Optimization Plan (GBA on fx-CG100)

> Research synthesis + execution plan for making GBA games playable on the
> Casio fx-CG100 (SH7305 / SH-4A, big-endian, no FPU). Combines gpSP's dynamic
> recompiler architecture with the low-level platform techniques proven by
> **prizoop** (tswilliamson's GBC emulator for the same calculator family).

---

## 1. Context & realistic verdict

The interpreter is fine for correctness but cannot reach playable GBA speeds on
this CPU. A GBA frame is **280,896 ARM7TDMI cycles** (≈10⁵ guest instructions)
in ≤16.7 ms. The calibration data is sobering:

- A **Dreamcast** (SH-4 @ 200 MHz, *with* a 1.4 GFLOPS FPU and 16 MB RAM) only
  reached **~80–85 % of GBA speed using gpSP's C interpreter** — and the SH4
  dynarec backend that would have closed the gap was never written.
- gpSP's mature **ARM dynarec** needs **~200–260 MHz ARM9** for full-speed 2D.
- The fx-CG100 is weaker than the Dreamcast on every axis except peak core MHz
  (~236 MHz overclocked vs 200): no FPU, ~1–2 MB usable RAM, slow memory bus,
  big-endian (byte-swap tax), ROM streamed from NOR flash.

**Verdict — set expectations now.** Full-speed (59.7 fps) across the library is
not realistic. The achievable target is **lightweight 2D titles at ~30 fps with
frameskip** — simple-PPU, sprite-light, idle-loop-heavy games (early-gen RPGs,
puzzle, simple platformers). Mode-7 / affine-blend-heavy (Mario Kart-class) and
pseudo-3D (Doom) are out of reach. This is the same ceiling the GP2X/Dingoo
gpSP ports hit, on better hardware.

**This plan is worth doing anyway**: each layer below is independently useful,
and the quick wins (Part B/D) make even the interpreter meaningfully better
before the dynarec lands.

---

## 2. Strategy in one sentence

> **Use gpSP's dynarec for the CPU** (a new `sh4/sh4_emit.h` backend that mirrors
> the MIPS emitter but adapts to SH4's 16-register / T-bit-only / literal-pool
> constraints), and **use prizoop's techniques for everything around it** —
> direct-DMA LCD strips, a direct-pointer memory map, on-chip-RAM placement of
> hot code, direct keypad scan, and free-running-timer frame pacing.

> **Overclock is out of scope** — it is set separately on the calculator (e.g.
> Ptune4) before the add-in runs; cgba targets whatever clock is present.

prizoop itself is *not* a dynarec (it is a heavily optimized switch interpreter,
which is enough for the far simpler GB CPU). Its value to us is the **platform
and I/O layer**, not the CPU approach.

---

## 3. PART A — The SH4 dynarec (the core engine, highest leverage)

Add a new gpSP host backend. This is non-negotiable and the hardest part.

### A0. Build wiring
- Add `#elif defined(SH4_ARCH)` → `#include "sh4/sh4_emit.h"` at
  [cpu_threaded.c:215](../vendor/gpsp/cpu_threaded.c) (and the parallel arch
  blocks further down at ~246/250), plus `SH4_ARCH` in `common.h` and the
  Makefile. Create `vendor/gpsp/sh4/{sh4_emit.h, sh4_stub.S}`.
- Promote the standalone [sh4_codegen.h](../ports/fxcg100/sh4/sh4_codegen.h)
  (currently 16 verified instructions) into the encoder layer the emitter calls.
  It must grow to cover the ISA listed in Appendix B.

### A1. Register allocation (the central design constraint)
SH4 has **16 GPRs**; several are forced reserved, so we cannot keep all 16 guest
ARM regs resident like AArch64. Proposed map (mirrors the MIPS model in
[mips_emit.h](../vendor/gpsp/mips/mips_emit.h), tightened for SH4):

| SH4 reg | Role | Notes |
|---|---|---|
| `R15` | host SP | hardware-forced |
| `R0`  | scratch | **forced** index for `@(R0,Rn)` and operand for imm-ALU/`@(disp,GBR)` — keep free |
| `GBR` | **guest state base** → `reg[]` | gives `@(disp,GBR)` base-relative spill access "for free" (no GPR), reach ~1 KB; **but** it forces R0 as the data reg, so weigh per use |
| `R14` | cycle counter | callee-saved; decremented per block, exit at ≤0 |
| `R13` | block/current PC | callee-saved; relative-PC base for cheap `load_pc` |
| `R12` | flags / temp | see A3 |
| `R8–R11` | 4 hottest guest regs, pinned | callee-saved; chosen by measured per-game ARM register-use frequency (as gpSP did) |
| `R1–R7` | scratch + C-call args | `R4–R7` = arg0–3, `R0–R3` caller-saved per SH ELF ABI |

Cold guest registers live in `reg[]` and are loaded/stored on demand via
`@(disp,GBR)` or a base pointer (gpSP's `mem_reg = ~0U` "lives in memory"
sentinel). This is the **x86-32 backend's** model (8 GPRs, most guest regs in
the state struct) more than the register-rich MIPS/ARM64 model — study
`x86/x86_emit.h` as a second reference for surviving register pressure.

### A2. Constant materialization & literal pools
SH4 has **no 32-bit immediate move** (`MOV #imm8` is signed 8-bit only; **no
MOVI20** on SH-4A). Materialize 32-bit constants via **PC-relative literal-pool
loads**: `MOV.L @(disp,PC),Rn`, disp 8-bit ×4 ⇒ reach **+0…+1020 bytes,
4-byte-aligned**, forward only.
- Maintain a per-block constant pool emitted at/near block end; if a block grows
  beyond ~1 KB of code, flush a pool mid-block (like GCC does) and jump over it.
- **Cache hot constants** in pinned regs instead of re-loading: guest-memory
  region base pointers, region masks (`0x7FFF`, `0x3FFFF`), the `reg[]` base.
  These are referenced constantly; keeping them resident avoids a pool load per
  memory op.

### A3. Flags — only the T bit exists
SH4 has **no NZCV register**; the single **T bit** is the only condition flag.
The MIPS backend's "one host register per flag (N/Z/C/V)" approach is too
expensive here (4 callee-saved regs we don't have). Instead:

- **Lean hard on gpSP's existing dead-flag elimination** (`arm_dead_flag_eliminate`,
  liveness over `block_data[].flag_data` in
  [cpu_threaded.c](../vendor/gpsp/cpu_threaded.c)). On SH4, flag synthesis is the
  *most expensive* part of each ALU op, so **not computing unused flags is the
  single biggest dynarec micro-optimization.** Thumb code (≥50 % of GBA
  execution) sets flags on almost every instruction, most never read — this
  pays off enormously.
- For flags that *are* live, prefer **lazy materialization**: keep the result
  (and, where needed, the operands) of the last flag-setting op and synthesize
  N/Z/C/V on demand at the consuming conditional, rather than eagerly after every
  ALU op. N and Z are cheap (`CMP/PZ`, `TST Rn,Rn`); C and V need `ADDC/SUBC`/
  `ADDV/SUBV` (which route carry/overflow into T) or explicit synthesis.
- Pack any persistent flag state into one host word (`R12`) or the state struct,
  not four registers.

ARM condition codes (EQ/NE/CS/…/LE) compile to a `CMP`/`TST` that sets T,
followed by `BT`/`BF`. The `arm_conditional_block_header` path becomes: synthesize
the needed flag into T, then `BF` over the predicated body.

### A4. Branches, delay slots, block linking
- **Delay slots:** `BRA/BSR/JMP/JSR/RTS/BT-S/BF-S/BRAF/BSRF` all execute the
  next instruction *before* transferring. **Emit `NOP` (`0x0009`) in every slot
  to start**, then hoist a safe instruction in once correct. **Never** place a
  branch, a PC-relative load (`MOV.L/W @(disp,PC)`, `MOVA`), or `TRAPA` in a
  slot (illegal-slot exception). Plain `BT/BF` have **no** slot.
- **Block chaining:** patch near targets with `BRA disp12` (reach ≈ ±4 KB,
  matching gpSP's `generate_branch_patch_unconditional`). For targets beyond
  ±4 KB, load the target into a reg (pool) + `JMP @Rn`, or use `BRAF Rm`
  (PC + register). Conditional internal branches use `BT/BF` (±256 B) or
  `BT/BF` over a `BRA` for longer reach.
- The existing [sh4_codegen.h](../ports/fxcg100/sh4/sh4_codegen.h) already emits
  `BRA/BSR/BT/BF/BT-S/BF-S` and the disp helpers — extend with `JMP/JSR/BRAF` and
  a back-patch helper that rewrites a placeholder displacement after layout.

### A5. Memory access fast paths (combine gpSP + prizoop)
This is the second-biggest dynarec cost after flags.

- **Hot regions inline, cold via helper.** Mirror gpSP's `tmemld[type][region]`
  / `tmemst[type][region]` region dispatch, but for the hot regions
  (IWRAM `0x03…`, EWRAM `0x02…`, ROM `0x08…`, VRAM `0x06…`) emit a **direct load**:
  base pointer (resident, per A2) + masked offset, no call. I/O (`0x04…`),
  EEPROM/Flash, and unmapped fall through to the C `execute_load_*` / `execute_store_*`
  helpers. This is gpSP's design informed by **prizoop's `memoryMap[256]`
  direct-pointer table + `specialMap[256]` "needs-validation" bitmask**
  (`src/memory.cpp`) — common case is a branchless indexed load; expensive MMIO
  semantics gated behind a cheap mask test.
- **Big-endian tax (host BE, GBA LE):**
  - *Instruction fetch* pays the endian decode **once, at translation time** —
    decode LE Thumb/ARM opcodes when scanning the block (ROM is mapped NOR, read
    directly).
  - *Data* loads/stores must preserve GBA LE semantics at runtime → byte-swap
    with `SWAP.B`/`SWAP.W` (or maintain guest RAM in swapped form and document
    it). Per the dynarec contract, treat raw `u16*`/`u32*` host casts of guest
    memory as suspicious. This is a real per-access cost — budget for it.
- **SMC / RAM-code invalidation** stays gpSP's `ramtag` system (guest writes to
  EW/IWRAM invalidate translated blocks via the IWRAM/EWRAM mirror tags). Keep
  this **separate** from host I-cache sync (A7).

### A6. Multiply / shift / divide
- ARM barrel-shifter amounts → **`SHAD`/`SHLD`** (variable shift, count in Rm,
  negative = right; the cheap path).
- `MUL/MLA` → `MUL.L` (→ MACL); `UMULL/SMULL/UMLAL/SMLAL` → `DMULU.L`/`DMULS.L`
  (32×32→64 into MACH:MACL) + `STS MACL/MACH,Rn`.
- Division is **iterative** (`DIV0S/U` + 32× `DIV1`) — rare in Thumb; route to a
  software helper.

### A7. Host I-cache sync after emit
Already implemented correctly in
[sh4_cache.h](../ports/fxcg100/sh4/sh4_cache.h): per-32-byte-line
`OCBWB` (write back dirty operand-cache lines) → `SYNCO` → `ICBI` (invalidate
I-cache) → `SYNCO`. This matches the SH-4A manual's verbatim SMC sequence
(§2.7.1) — **ordering is right** (`OCBWB → SYNCO → ICBI`, not the reverse). These
cache ops are **not privileged** and gint runs add-ins privileged anyway. The
on-device [jit_probe.c](../ports/fxcg100/jit_probe.c) already proves emitted code
executes after this sync. **Executable memory caveat:** generated code must run
from an executable mapping — gint's `mmu_uram()` **P1 alias** (cached, executable)
or on-chip **ILRAM `0xE5200000`** / **XYRAM `0xE500E000`** (marked `rwx`). The
plain `0x081xxxxx` add-in data alias is **no-execute** — never `JMP` into it.

### A8. Thumb-first, then ARM
Per the port plan: implement the **Thumb subset first** (ALU, imm/PC-relative
loads, reg/imm `LDR/STR`, conditional + unconditional branches, `BX`) because
GBA code is Thumb-heavy; unsupported ops fall back to interpreter/C stubs while
the emitter grows. Then add ARM mode (data-proc, LDM/STM, multiply-long, SWI).

---

## 4. PART B — Idle-loop & HALT elimination (biggest per-game win, cheap)

The highest **impact-per-effort** item. GBA games busy-wait for VBlank; VBlank
alone is ~83,776 cycles/frame, so games idle away most of a frame. gpSP collapses
those thousands of polled iterations into one jump.

- The machinery is **already in gpSP and wired into the interpreter**:
  `idle_loop_target_pc` is set from the per-game `gba_over.h` database
  ([gba_memory.c:1664](../vendor/gpsp/gba_memory.c)) and checked in
  [cpu.cc:3076](../vendor/gpsp/cpu.cc) / 3570. **111 of 200** curated games need it.
- **What's needed for the dynarec:** in the SH4 branch-emit path, when
  `pc == idle_loop_target_pc`, emit the idle path that zeroes the remaining
  cycle counter (`reg_cycles`) so `update_gba` fast-forwards to the next
  scheduled event and raises the pending IRQ — exactly as the MIPS backend does.
- Pair with **HALT/SWI HLE** (`CPU_HALT_STATE`): for games that call SWI 0x02
  (Halt) properly, advance hardware with zero CPU instructions until IRQ.

This is the difference between "unplayable" and "playable" for spin-heavy titles,
for a few hundred lines of emit code.

---

## 5. PART C — Memory model & big-endian (shared interpreter + dynarec)
Covered tactically in A5; the strategic points:
- Keep the GBA address map and I/O handlers in `gba_memory.c` as the source of
  truth; the dynarec's inline fast paths are an optimization layered on top, not
  a fork.
- Treat **all guest memory as little-endian** even though the host is big-endian
  (`-DMSB_FIRST=1` is already set in the port Makefile). Audit every host-pointer
  cast of guest memory. The palette is the model to copy: gpSP keeps a
  **pre-converted** `palette_ram_converted[]` (RGB565) updated on write, avoiding
  per-pixel conversion in the renderer — do the same kind of "pay-once-on-write"
  trick wherever guest data is consumed hot.

---

## 6. PART D — Rendering & LCD (prizoop's highest-value reusable assets)

Once the CPU is recompiled, the **software PPU renderer becomes a comparable or
larger cost** than the CPU (the SNES analog: per-pixel rendering ≈ 40 % of total).
Two fronts: the LCD *push*, and the *render* itself.

### D1. LCD push — strip DMA bypassing VRAM (steal directly)
prizoop's single biggest platform trick (`src/display_directlcd.cpp`): render a
few scanlines into a **small double-buffered strip** in fast RAM, fire the SH7305
**DMAC ch0** straight to the R61524 GRAM port (`LCD_BASE 0xB4000000`), and keep
doing CPU/audio work while the DMA runs (`DmaWaitNext` polls TE while pumping
audio). Moving the DMA source **off the slow VRAM path** was worth ~10 % (58.5 →
64 fps) for prizoop.
- On gint, the equivalent is **`r61524_display(vram, start, height, R61524_DMA)`**
  (returns immediately after starting the background DMA) and the **`dma_*`** API
  — or the raw `DMA0_*` registers (`0xFE008020–60`, CHCR `0x00101400` = 32-byte
  bursts) if going freestanding. **Coherency caveat:** gint's display DMA assumes
  an *uncached* `gint_vram`; if we DMA our **cached** framebuffer we must add a
  manual `OCBWB` writeback first (gint skips it).
- GBA's 240×160 fits inside the 396×224 panel **natively (no scaling)** — center
  it. Optional scaling later can reuse prizoop's hand-written RGB565 averaging
  ASM (`(X+Y-((X^Y)&0x0821))>>1`, endian-agnostic).
- **DMA the frame from uncached on-chip RAM, never from `gint_vram`.** This was
  the real cause of the **white screen on a loaded game**: `gint_vram` is in
  *cached* RAM, so the DMAC reads stale memory and the panel never shows the
  frame. casio-emu has no cache model, so it rendered fine in the emulator and
  masked the bug. The partial-horizontal window is *not* the problem (the working
  cgbc/prizoop ports use partial windows). Fix (mirrors cgbc's shipping
  `CGBC_DIRECT_LCD_STRIP_DMA` path and prizoop's strip presenter): copy the frame
  in 12-row strips into on-chip **XY-RAM** (`0xe5007000`/`0xe5017000`, uncached,
  DMA-safe) and `dma_transfer_async` each strip to the centred R61524 window,
  double-buffered. See [gint_platform.c](../ports/fxcg100/gint-gpsp/src/gint_platform.c).
- **Menu/UI renders through gint (`dupdate`).** The strip DMA narrows the window;
  `restore_full_window()` restores the full 396×224 window before the next gint push.
- **HLE NOR testing (no USB):** casio-emu now HLE-hooks `Bfile_GetBlockAddress`
  (+ `Find*` stubs) so the port's direct-mapped NOR loader works from a host
  directory — drop a ROM as `GAME.GBA` in `$HLE_FLS0` and run the add-in; no flash
  image, no USB transfer, no FAT size cap. Used to root-cause the white screen.

### D2. Render itself — cut work
gpSP's `video.cc` already has mode-specific fast paths and deferred
indexed-color blending. Layer on the standard low-power playbook:

- **Frameskip** (auto) — the biggest droppable cost; gpSP already supports
  off/auto/threshold/fixed. Wire the menu's frameskip option (currently TODO) to
  the real `skip_next_frame` path. Note the ceiling: if render = fraction R of
  frame work, max speedup is `1/(1−R)` — frameskip helps least on CPU-bound,
  sprite-light scenes.
- **Dirty-scanline tracking** — skip unchanged lines.
- **Pre-blended palette tables** — turn per-pixel blend MAC into table lookups.
- **Optional fidelity drops** — affine / alpha-blend / mosaic are the expensive,
  commonly-approximated effects; offer a "fast" toggle that skips them.
- Keep `sprite_limit` on (already forced in the stubs).

---

## 7. PART E — Frame pacing (overclock is out of scope)

**Overclock is out of scope for this project** — it is applied separately on the
calculator (e.g. Ptune4 reprogramming the CPG/BSC registers) before the add-in
runs. cgba simply targets whatever clock is present and does no clock setup.

Frame pacing is still ours to handle: pace the emulator toward ~59.7 fps and
drive auto-frameskip from a free-running timer (gint `timer_*`, TMU ~250 ns res)
with a coarse RTC backstop — the prizoop pacing pattern, minus the clock-setting
step.

---

## 8. PART F — Audio (defer; off by default)

Disable audio by default (already stubbed in
[fxcg100_sound_stub.c](../ports/fxcg100/fxcg100_sound_stub.c)). It costs ~10–20 %
and GBA audio (2 PSG + 2 DMA PCM) is much heavier than GB's. If revisited:

- prizoop's **cooperative `condSoundUpdate()`** polling model (refill from the
  DMA-wait loop and hot paths, no audio ISR) is the clean decoupling pattern.
- Output options: gint's sound module, or prizoop/NESizm's **1-bit serial** hack
  (low-lag mono via the 2.5 mm jack). Expect poor fidelity regardless.

---

## 9. PART G — RAM budget & code placement

Usable RAM is **~1–2 MB** (fx-CG50 ≈ 491 KB `_uram` + ~350 KB `_ostk` + ~128 KB
OS heap; **fx-CG100 currently gets no `_ostk` arena** under gint — *less* easy
heap). gpSP's defaults do not fit:

| Buffer | gpSP default | `SMALL_TRANSLATION_CACHE` | fx-CG100 target |
|---|---|---|---|
| ROM translation cache | 10 MB | 2 MB | **≤ 1 MB** (tune down further) |
| RAM translation cache | 512 KB | 384 KB | **128–256 KB** |
| `ROM_BRANCH_HASH_SIZE` | 64 K entries = **256 KB** | — | **shrink `ROM_BRANCH_HASH_BITS`** (e.g. 12–13 → 16–32 KB) |
| ROM buffer | 32 MB | — | **map NOR flash directly**, don't buffer full ROM |

- Define `SMALL_TRANSLATION_CACHE` and shrink further; the cache flushes wholesale
  when full (acceptable).
- Allocate the big code buffer with gint **`kmalloc_max(&sz, "_uram")`** (largest
  contiguous block, P1/executable), or the adventurous `0x8c200000`+ region
  (several MB, prototype-verify first).
- Place the **hottest dispatch/code** in on-chip **ILRAM/XYRAM** (fast, but only
  4 KB + 16 KB) — e.g. the block-dispatch trampoline and most-used helpers.
- Keep the GBA's own RAM regions in fast/on-chip memory where possible (prizoop
  moved emulated VRAM to on-chip RAM for a measurable win).

---

## 10. Phased roadmap (impact × effort)

| Phase | Work | Impact | Effort | Depends on |
|---|---|---|---|---|
| **0. Free wins** | Wire real auto-frameskip; strip-DMA LCD blit; direct keypad; idle-loop already on in interpreter | High (validates platform; interpreter goes from unusable → "barely playable" on light games) | Low | gint dev branch, prizoop driver port |
| **1. Thumb dynarec MVP** | `sh4_emit.h` + `sh4_stub.S`; register map (A1); literal pools (A2); T-bit conditions (A3); near block chaining (A4); inline mem fast paths for IW/EW/ROM/VRAM (A5); **idle-loop emit path (B)** | **Highest** (the actual unlock) | **High** | Phase 0, A0 wiring |
| **2. ARM mode + flag tuning** | ARM data-proc, LDM/STM, multiply-long, SWI; lazy-flag + dead-flag tuning; far-branch handling; per-game hot-register selection | High | High | Phase 1 |
| **3. Renderer pass** | dirty-scanline tracking; pre-blended palettes; "fast" affine/blend toggle; profile-guided hotspots | Medium–High (renderer dominates once CPU is cheap) | Medium | Phase 1 |
| **4. Polish** | NOR ROM save/state plumbing; optional audio; menu options made functional | Low–Medium | Medium | — |

Front-load Phase 0 (it de-risks the platform and is reusable regardless of the
dynarec) and treat Phase 1 as the make-or-break milestone.

---

## 11. Verification

- **Encoder correctness:** extend [sh4_codegen_smoke.c](../tests/sh4_codegen_smoke.c)
  to assert every new opcode against known-good bytes (`sh-elf-as`/`objdump`),
  as it does today for the 16 base instructions.
- **On-device codegen:** extend [jit_probe.c](../ports/fxcg100/jit_probe.c) to
  run emitted ALU/branch/memory sequences and check results (cache-sync path
  already proven).
- **Differential correctness:** run the dynarec against the interpreter on the
  same ROM/inputs and diff register/memory state per block — the interpreter is
  the correctness oracle (keep `dynarec_enable` togglable).
- **Perf:** per-frame timing via the free-running TMU; report fps + dynarec/render
  split (prizoop's `perfnotes.txt` methodology). Use `mode3_smoke` / `input_probe`
  test ROMs, then a light commercial 2D title.
- **Current JIT performance warning:** do not assume the correctness MVP speeds
  up real hardware yet. The user reports no noticeable JIT improvement during
  the Metroid intro cutscene. First confirm the tested `.g3a` was configured
  with `-DCGBA_DYNAREC=ON`; the normal `fxsdk build-cg` path is still
  interpreter-only because the dynarec is default-off. Then measure interpreter
  vs JIT on the physical fx-CG100 with the same ROM, scene, frameskip/display
  settings, and overlay metric before optimizing. Also log
  translation/cache-flush counters; the 4099-frame Metroid JIT harness showed
  heavy translation churn
  (`rom_flush=1780`, `arm_tx=13640`, `thumb_tx=604734`), which can erase native
  execution gains until cache sizing, block reuse, block chaining, resident
  regs, inline memory, and idle-loop emit are implemented.
- **Harnesses:** the existing `~/Dev/casio-emu` flow (`run-zelda-flash.sh`,
  framebuffer/PPM dumps, GRAM-write logging) before physical hardware.

---

## 12. Risks & open questions

- **Register pressure** is the core risk: 16 GPRs, R0 forced, GBR-as-base trades
  against R0. The x86-32 backend proves a register-poor backend *can* work, but
  expect more spills than MIPS/ARM and accept it.
- **Flag synthesis cost** on a T-bit-only ISA — mitigated by dead-flag
  elimination, but it's the dominant per-op overhead. Measure early.
- **Big-endian byte-swap** on every guest data access is unavoidable overhead.
- **fx-CG100 platform is on gint's `dev` branch** (no stable tag, no `_ostk`
  arena, F5 clamped); RAM size and exact clock ceilings are **unverified vs
  fx-CG50** — confirm on hardware, don't assume "more RAM."
- **No SH4-host JIT prior art** exists beyond the `sh4asm` encoder
  (washingtondc-emu, BSD-3 — reusable as an encoder reference). We are writing a
  novel backend; gpSP's MIPS/x86-32 emitters are the structural template.
- **The honest ceiling:** even with everything above, demanding games won't be
  playable. Scope the success criterion to light 2D at frameskipped 30 fps.

---

## Appendix A — prizoop reusable assets (ranked)

| # | Asset | prizoop source | Use for |
|---|---|---|---|
| 1 | Strip-DMA LCD driver (`DmaDrawStrip`/`DmaWaitNext`, `DMA0_*`, CHCR `0x00101400`) | `src/display_directlcd.cpp`, `src/display.h` | LCD push (D1) |
| 2 | 256-entry direct-pointer page table + `specialMap` MMIO mask | `src/memory.cpp/.h` | Memory fast paths (A5/C) |
| 3 | Overclock setup (`Ptune2_LoadSetting`, FRQCR-kick, clock-aware pacing) | `ptune2_simple/` | Overclock (E) |
| 4 | Free-running-TMU pacing + auto-frameskip + RTC backstop | `display_directlcd.cpp` | Frame pacing (E) |
| 5 | Direct keypad scan at `0xA44B0000` (skips OS `GetKey`) | `src/keys.cpp` | Input |
| 6 | Hand-written SH4 pixel ASM (RGB565 avg, swap-replicate, delay-slot-filled stores) | `src/asm/*.S` | Scaler/blit (D1) |
| 7 | Cooperative `condSoundUpdate()` (no audio ISR) + 1-bit serial out | `src/snd_main.cpp`, NESizm | Audio (F) |
| 8 | Build flags `-O2 -funroll-loops -fno-rtti -fno-exceptions -fno-trapv`, `SYNCO()` barrier | `Makefile`, `platform.h` | Build |

Repo: <https://github.com/tswilliamson/prizoop> · dev log:
<https://www.cemetech.net/forum/viewtopic.php?t=13633> · sister engine (cleaner
1-bit sound): <https://github.com/tswilliamson/nesizm>

## Appendix B — SH-4A emitter ISA cheat-sheet

- **GPRs:** 16 × 32-bit. R0 = forced `@(R0,Rn)` index / imm-ALU & `@(disp,GBR)`
  operand. R15 = SP. ABI: R4–R7 args, R0–R3 caller-saved, R8–R14 callee-saved,
  return in R0 (R0:R1 for 64-bit). PR = link (save with `STS.L PR,@-R15`).
- **Immediates:** `MOV #imm8` (signed −128..127) only; **no MOVI20**. 32-bit
  consts via `MOV.L @(disp,PC),Rn` (disp ×4, +0..1020 B, 4-byte-aligned, forward).
- **Branches:** `BRA/BSR` disp12 ×2 (≈ ±4 KB, **delay slot**); `BT/BF` disp8 ×2
  (≈ ±256 B, **no slot**); `BT/S, BF/S` (slot); `JMP/JSR @Rm`, `BRAF/BSRF Rm`,
  `RTS` (all slots). Illegal in slot: any branch, any PC-relative (`@(disp,PC)`,
  `MOVA`), `TRAPA`, `LDC …,SR`.
- **Flags = T bit only.** Set by `CMP/*`, `TST`, `ADDC/SUBC/NEGC` (carry),
  `ADDV/SUBV` (overflow), shifts/rotates (`SHLL/SHLR/ROT*`). `SETT/CLRT/MOVT/DT`.
  `BT/BF` test T. (`SHAD/SHLD/SHLL2/8/16` do **not** touch T.)
- **Workhorses:** `SHAD/SHLD` (variable shift, neg = right), `DMULU.L/DMULS.L`
  (→ MACH:MACL) + `STS MACL/MACH,Rn`, `EXTU/EXTS.B/W`, `SWAP.B/W`, `DT` (loop
  counter), `DIV0S/U`+`DIV1` (iterative divide).
- **SMC / I-cache sync:** per 32-byte line `OCBWB` → `SYNCO` → `ICBI`
  (→ `SYNCO`). Not privileged. Execute only from an executable mapping
  (P1 `mmu_uram()` alias, or ILRAM/XYRAM); `0x081xxxxx` data alias is no-execute.
- **Sources:** SH-4A SW Manual REJ09B0003; SH7724 HW Manual REJ09B0560;
  <https://shared-ptr.com/sh_insns.html>; SH ELF ABI.
