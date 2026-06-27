# SH4 dynarec: new-game freeze (Metroid Fusion)

Status: **cutscene now renders; the dynarec freezes ~frame 700 on a wild jump
into the BIOS boot.** Distinct from the cache-overflow crash fixed in `d3fb4f8`.

> NB the filename says "decompress" — that was the original (wrong) hypothesis.
> The decompression codegen is **fine**; see "Correction" below. Kept for history.

## Symptom (updated 2026-06-27)

Driving Metroid Fusion past the file-select into a new game, the dynarec **does**
render the intro: a frame-by-frame framebuffer compare (interp build vs JIT
build, both via casio-emu, `STAT_EVERY=1`) is pixel-identical (matching FBSTAT
black-pixel counts and hashes) from the menu through **frame ~699** — the JIT
plays the cutscene. Then at **frame 700** it diverges hard: the interpreter keeps
animating (`black=0`, cutscene continues) while the JIT jumps to a mostly-black
screen (`black=32412`) whose two hashes (`9C795F0E`/`87F1677A`) then **repeat
frame 700→720** = the JIT is frozen. Disassembly of where it lands (BIOS
`0x200`–`0x258`: `bl 0xB5C` decompress, set DISPCNT, then a `r1=0x77`
count-down VBlank wait at `0x22C`–`0x24C`) shows it is the **BIOS boot/logo
sequence** — i.e. the guest control flow went *wild* and re-entered the BIOS
boot. Not a SoftReset SWI (a SWI trace showed none).

So the real blocker is a corrupted return/jump value in frame ~697 → the guest
branches to `0x0` (reset) → BIOS boot → freeze.

## Pinned: the wild jump (resolver tracer)

Instrumenting `block_lookup_address_arm/thumb` (`cpu_threaded.c`, gated on
`CGBA_GPSP_HEADLESS_TEST`) to log guest jumps into the boot/vector region
(`pc < 0x260`) with a ring of recent block PCs + LR caught it at frame ~697:

```
@@WJ 00000000 lr08001873 : … 08001846 08001870 0800223F 00000000
```

The guest jumps to **`0x0` (reset)** from Thumb game code at **`0x08002248`**
(Metroid Fusion ROM):

```
08002248: pop {r0}      ; pops a code pointer off the stack
0800224a: bx  r0        ; r0 == 0  → reset → BIOS boot → freeze
```

`lr` at the jump is `0x08001873` (not 0), so it is NOT `bx lr`; a **stack slot
that should hold a return/code address is 0**. The `pop {r0}; bx r0` is a
function epilogue returning to a pushed address (likely the saved LR), so either
that LR was 0 at the matching `push` (a call set LR=0, or the fn was entered by
a branch leaving LR stale) or a push/store wrote 0 onto the stack.

### Ruled out

- **Native LDST emitters are NOT the cause.** Building with
  `-DCGBA_SH4_THUMB_LDST_NATIVE=OFF -DCGBA_SH4_ARM_LDST_NATIVE=OFF` still
  freezes — *earlier* (frame 696) and *differently* (no BIOS storm, only ~80
  vs 152k boot-region jumps). So native LDST is not the bug (it lets the game
  get *further*); the corruption is in machinery shared by both builds.
- **Per-block codegen looks correct** in the diffable region: the block-diff
  (with the loop-artifact + spin fixes) runs clean through the cutscene. The
  corruption is either accumulated state (a store wrote 0 — the register diff
  compares r0..r15+CPSR, **not memory**, so it would miss a wrong-value store)
  or in code the diff can't reach (it stalls on the cutscene's frame waits).

## Correction: the earlier "decompression divergences" were harness artifacts

Previously the block-diff harness reported the first divergence at BIOS `0xB5C`
(the LZ77 unpack) and we believed the unpacker was mistranslated. **That was a
false positive in the harness, not a codegen bug.** Mechanism:

- The harness runs the dynarec one block from a shared state `S`, takes its
  block-end PC `dpc`, then runs the interpreter `S → dpc` (`interp_run_to_pc`)
  and compares register files.
- gpSP blocks **contain internal backward branches** (a tight loop ends a block
  at the loop's backward branch, whose target is *inside* the block). For such a
  block `dpc` is the backward-branch target, which the interpreter passes through
  **sequentially, early** (the loop-body entry) long before the dyn's block
  actually exits there. `interp_run_to_pc` stopped at that first early pass, so
  it compared the interp at the loop *entry* against the dyn after a *full body
  pass*. Apples-to-oranges → bogus "divergence."
- Proof: forcing the LZ77 routine to one-instruction gpSP blocks (no internal
  loops) made it **match**, and the "divergence" just hopped to the next
  loop-bearing block (`0xB60` → `0xC14` …). The smoking gun was `du20 iu2`: the
  dyn ran 20 insns (full body, exiting at `bne 0xC1C`) while the interp ran 2
  (`0xC14,0xC18` → stopped at the `0xC1C` body entry).

### Harness fixes (ports/fxcg100/sh4/sh4_diff_harness.c)

1. **Loop-artifact retry.** On a register mismatch with `dpc != pc0`, re-run the
   interpreter to `dpc` skipping one occurrence (`skip_initial=1`) = one body
   pass, matching how single-block mode exits the loop. If that matches, it was
   the artifact → not a bug, and the one-body-pass state is the next baseline.
   Real (non-loop) divergences fail the retry and are still reported.
   (`regs_diverge()` compares r0..r15 + CPSR; cycle/halt/sleep are timing
   residue and intentionally ignored — per the cycle-accuracy research.)
2. **VBlank/idle-spin advance.** A self-loop that matches (`dpc == pc0`) is a
   VCOUNT/idle wait (e.g. BIOS `0x238`: `ldrh VCOUNT; cmp #0x9f; bls 0x238`) the
   dyn also spins on; single-block mode can't advance VCOUNT, so the diff hung
   there forever. Now it steps the interpreter freely (`execute_arm`/`update_gba`)
   until the loop exits, then resumes the lockstep.

With both fixes the diff runs cleanly through the frame-620 region (no false
positives) and advances through the per-frame BIOS waits.

## How to reproduce / diagnose

- **Faster casio-emu:** build the `codex/casio-emu-block-dispatch` branch
  (worktree `/private/tmp/cgv-casio-emu-block-dispatch`, `cmake . -B build
  -DUSE_SDL_GUI=ON && make -C build` → `build/calcemu`). Verified byte-identical
  output to the old `build-hle/calcemu`, runs the same workloads faster — use it
  for all diff/playtest runs.
- **Find the freeze frame:** two builds (`HEADLESS_DYNAREC=0` vs `1`),
  `STAT_EVERY=1 FRAMES=900` + new-game input (`START_FRAME=30 START_HOLD=8`,
  `A_FRAME=120 A_PERIOD=120 A_PRESS=6 A_HOLD=900`); compare FBSTAT per frame →
  first hard divergence = frame ~700.
- **Block-diff at the freeze:** `-DCGBA_GPSP_HEADLESS_DIFF_FRAME=699
  -DCGBA_GPSP_HEADLESS_DIFF_BLOCKS=300000`. First `B<n> p<pc> …` / `PC i d` line
  is the mistranslated block / wild jump.
- **External observer (avoids the layout-Heisenbug):** instrument *casio-emu*
  `src/interpreter.c`, or gpSP's `block_lookup_address` resolver (NOT
  `translate_block`) to log guest jumps into the BIOS boot (`0x0`/`0x200`).

## Next steps

The manifestation is pinned (`0x08002248 pop {r0}; bx r0`, r0==0). The open
question is **where the stack 0 comes from**. The register-only block-diff can't
see it (wrong-value *store*, not a register), so:

1. **Trace the stack slot.** Instrument the dynarec to log `[sp]` (and sp, LR)
   when the guest reaches `0x08002248`, and the value + sp at the matching
   `push`/`str` that fills it, in BOTH the JIT and an interp build; diff the two
   to find the block that writes 0 where the interp writes a code address.
2. **Or extend the block-diff to compare memory writes** (a store log per block),
   not just registers — then the corrupting store shows up directly. It also
   needs to cross the cutscene's frame/IntrWait waits (the current spin-advance
   only handles `dpc==pc0` self-loops).
3. Once the corrupting op is found, fix its SH4 codegen and re-verify the full
   new-game → gameplay path, interp vs JIT, frame-by-frame.

Note: native LDST off freezes too, so look at machinery shared by both builds
(stores/PUSH-POP block transfers, conditional/flag handling, or an
IRQ/timing-dependent path that feeds the bad value).

See [[sh4-newgame-decompress-bug]], [[casio-emu-timing-not-cycle-accurate]],
[[dynarec-cycle-accuracy-practice]], [[dispatch-null-overflow-crash]].
