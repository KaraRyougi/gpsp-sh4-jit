# SH4 dynarec: new-game freeze (Metroid Fusion)

Status: **OPEN.** The dynarec renders the new-game intro cutscene, then **freezes
~frame 700 on a control-flow divergence** — it branches into code the game never
executes, pops a (legitimately) zero stack word as a code pointer, and `bx 0` →
guest reset → BIOS boot → frozen. Distinct from the cache-overflow crash fixed in
`d3fb4f8`.

> The filename says "decompress" — that was the **original, wrong** hypothesis
> (kept so the memory `[[sh4-newgame-decompress-bug]]` links and git history stay
> intact). The decompression codegen is fine; see "History / superseded" below.

## Symptom

Driving Metroid Fusion past the file-select into a new game (tolerant input:
`START_FRAME=30 START_HOLD=8`, `A_FRAME=120 A_PERIOD=120 A_PRESS=6 A_HOLD=900`):

- The JIT **renders the intro cutscene correctly**: a per-frame framebuffer
  compare against the interpreter build (`STAT_EVERY=1`) is pixel-identical
  (matching FBSTAT black-pixel counts and hashes) from the menu through
  **frame ~699**.
- At **frame ~700** it freezes: the interpreter keeps animating (`black=0`),
  while the JIT jumps to a mostly-black screen (`black=32412`) whose two hashes
  (`9C795F0E`/`87F1677A`) then repeat 700→720. Disassembly of where it lands
  (BIOS `0x200`–`0x258`: `bl 0xB5C`, set DISPCNT, a `r1=0x77` count-down VBlank
  wait at `0x22C`–`0x24C`) shows it re-entered the **BIOS boot/logo sequence**.
  Not a SoftReset SWI (a SWI trace showed none).

## Root cause (as far as pinned): a control-flow divergence

The guest reaches the BIOS boot via a **wild jump to reset (`0x0`)** from Thumb
game code at `0x08002248`:

```
08002248: pop {r0}      ; pops a code pointer off the stack
0800224a: bx  r0        ; r0 == 0  → jumps to reset → BIOS boot → freeze
```

Watchpoints established the precise nature — **it is NOT a value/stack
corruption, it is wrong control flow**:

1. **The popped 0 is legitimate.** A store watchpoint on the popped slot
   `0x03007E20` shows it is written `0` by an *unrelated* prologue
   `push {r5,r6,r7}` at `0x080015DC` — and the **interpreter writes exactly the
   same 0 there** (635 vs 643 instances, all 0). The slot the JIT pops was last
   written by a different function than the one owning the `pop`; the JIT pops a
   **stale** word.
2. **The interpreter never runs this code.** A PC-hook in the interpreter shows
   it **never executes `0x08002248`** (count 0), while the JIT branches there.
   So `0x08002248` is code the game never reaches on the correct path; the JIT
   got there on a wrong path.
3. **The wrong target is off by 2.** The `@@WJ` resolver ring shows the chain
   passing through **`0x08001870` — the middle of the Thumb `bl @0x0800186E`**.
   `0x08001870 = 0x08001872 − 2`, i.e. a valid BL return (`0x08001873` with the
   Thumb bit, which matches the live `lr`) **minus 2**. A code pointer drifted by
   2 and now points mid-instruction.

So: a register/saved code-pointer **branch target drifted** (off by 2, into a
mid-BL/dead-code region), the JIT executed dead code, and a stale `pop {pc}`/`bx`
of a 0 finished the job. This is an **accumulated** divergence — by the time the
wild jump fires, the bad value has propagated through several frames of
IRQ-heavy cutscene.

### Why the existing diff can't catch it

The block-diff harness reseeds the dynarec from the interpreter's state before
every block, so the dynarec block always *starts correct* and produces the
correct end — it structurally **cannot see accumulated drift**. It also compares
**registers only** (r0..r15 + CPSR, not memory) and stalls on the cutscene's
per-frame `IntrWait`/VCOUNT waits before it ever reaches the divergence. Pinning
the **first** wrong branch therefore needs a true-lockstep diff (see Next steps).

### The corrupt branch is an indirect branch on the native LDST path

Further watchpoints narrowed *how* `0x08001870` is reached:

- It is **not a translation-gate re-dispatch.** `@@GATE` instrumentation in
  `generate_translation_gate` never fired for `0x08001870`, and no block ends at
  the BL prefix `0x0800186E` (`MAX_BLOCK_SIZE`=1024 rules out a size split of a
  ~20-instruction region). So it is a genuine **indirect branch to `0x08001871`**
  (a `bx`/`pop {pc}`/computed target), not a fall-through.
- The corrupt pointer is **not on the C-path.** Value + load watchpoints on
  `write_memory32`/`read_memory32` for `0x08001870`..`0x08001873` show the JIT
  stores/loads the *correct* `0x08001873` identically to the interpreter and
  **never** touches the corrupt `0x08001871` through the C helpers.
- It is **not a native single LDST load either.** `sh4g_thumb_ldst_native` only
  ever handles **LDRB (byte loads)** — it can't load a 32-bit pointer; and
  disabling the **ARM** DP/block/MUL natives (`CGBA_DIAG_NO_ARM_NATIVE`) leaves
  the frame-700 freeze **unchanged** (frames 700-752 all the same `9C795F0E`).
- It is **not an IRQ-resume-mid-BL.** Instrumenting `check_and_raise_interrupts`
  for an IRQ taken with PC in `0x08001868`..`0x08001876` fired **zero** times.
- It is **not `bx`/`pop`/gate** (the registers at the resolver hold no
  `0x08001870/71`; `pop`/`ldm{pc}` go through the C path which only sees the
  correct value; `@@GATE`=0).

So the wrong branch target is **computed** (or produced on a path none of the
above watchpoints can see), and the targeted-watchpoint approach is **exhausted**.
The remaining tool is the true-lockstep diff.

**Native LDST off is a *different*, earlier bug** (frame 696, `@@WJ`=0, none of
the `0x0800187x` traffic) — confirming **two distinct bugs**.

## Ruled out

- **Native LDST is not a *uniform* cause, but the frame-700 freeze IS tied to it**
  (see above): disabling it moves the failure to a *different* frame-696 bug, so
  it is not a clean bisect. Treat them as two bugs.
- **Value/stack corruption** of the popped slot (the 0 is written identically by
  both cores) and a **PUSH/POP SP-writeback miscalc** (SP is sane at the jump,
  `0x03007E24` in the IWRAM stack; a wrong r13 would show in the register diff).
- **The BIOS LZ77 unpack at `0xB5C`** — the earlier "first divergence here" was a
  harness artifact (see History below).

## Fixed along the way (real bugs, but NOT this freeze)

Two genuine latent bugs were found and fixed during the hunt. Neither changes the
frame-700 freeze (confirmed by instrumentation / re-test), but both are correct.

1. **S-bit `LDM{pc}^` USER-bank restore** (`cgba_sh4_arm_block`,
   `ports/fxcg100/sh4/sh4_interp_helpers.c`). The helper transferred the
   current/privileged bank, whereas the interpreter (`cpu.cc` `exec_arm_block_mem`
   ~1010/1041) brackets the transfer in USER mode when `s_bit && (store ||
   rn != r15)`; for an exception return that put the popped user `r13`/`r14` in
   the wrong bank and the SPSR re-bank then discarded them. Fixed by
   `set_cpu_mode(MODE_USER)` around the transfer. Regression-neutral here: **zero**
   `LDM{pc}^`-with-r13/r14 opcodes fire near the freeze (BIOS IRQ returns use
   `subs pc,lr,#4`).

2. **Thumb BL split across a block boundary** (`thumb_blh_setup` in
   `vendor/gpsp/sh4/sh4_emit.h`, called from `cpu_threaded.c`). The BL prefix
   (`0xF0-0xF7`) normally emits nothing because the suffix folds into the same
   block via `thumb_bl()`. But if a block ends *exactly* at the prefix
   (`MAX_BLOCK_SIZE` hit there), the suffix is a separate `thumb_blh()` block that
   reads a **stale LR** → wild branch. Fixed by emitting `LR = (PC+4) +
   signext(offset)<<12` at the prefix when `pc + 2 == block_end_pc`. **Defensive:
   it does not fire on this ROM** (`@@GATE`=0; a split needs a ~1024-instruction
   straight-line block ending at a BL prefix, which essentially never happens), so
   it is not the frame-700 freeze either — but it is a correct fix for a real
   latent gpSP gap (the upstream `// I don't think anyone will do that` TODO).

## Diagnostic tooling (durable)

- **`@@WJ` resolver tracer** (`cpu_threaded.c`, gated on `CGBA_GPSP_HEADLESS_TEST`):
  `block_lookup_address_arm/thumb` log guest jumps into the boot/vector region
  (`pc < 0x260`) with a ring of recent block PCs + `lr`/`sp`/`[sp]`. This is what
  caught the wild jump. Instrument the **resolver**, never `translate_block`
  (guest-side instrumentation of `translate_block` shifts block layout and
  dissolves layout-sensitive bugs — a Heisenbug).
- **Block-diff harness** (`ports/fxcg100/sh4/sh4_diff_harness.c`), now with two
  correctness fixes so it stops emitting false positives:
  1. **Loop-artifact retry.** A block ending in a backward branch has its `dpc`
     *inside* the block; `interp_run_to_pc` used to stop at the loop-body entry
     (reached early, sequentially) instead of the dyn's branch exit, mis-comparing
     the interp at the loop entry vs the dyn after a full body pass (tell:
     `du`≫`iu`). Now, on a mismatch with `dpc != pc0`, it retries the interp
     skipping one `dpc` occurrence (= one body pass); if that matches it was the
     artifact. `regs_diverge()` compares r0..r15+CPSR only (cycle/halt/sleep are
     timing residue, intentionally ignored).
  2. **VCOUNT/idle-spin advance.** A matching self-loop (`dpc == pc0`, e.g. BIOS
     `0x238 ldrh VCOUNT; cmp #0x9f; bls 0x238`) can't advance VCOUNT in
     single-block mode and hung the diff; it now steps the interpreter freely
     until the loop exits.
- **Faster casio-emu:** the `codex/casio-emu-block-dispatch` branch (worktree
  `/private/tmp/cgv-casio-emu-block-dispatch`, `cmake . -B build -DUSE_SDL_GUI=ON
  && make -C build` → `build/calcemu`). Verified byte-identical output to the old
  `build-hle/calcemu`, much faster — use it for all diff/playtest runs.

### Reproduce / locate the freeze

- Build interp (`-DCGBA_GPSP_HEADLESS_DYNAREC=0`) and JIT (`=1`) with
  `STAT_EVERY=1 FRAMES=900` + the input config above; compare FBSTAT per frame →
  first hard divergence = frame ~700.
- Run the JIT alone; `@@WJ` lines near frame 700 show the jump to `0x0` with the
  recent-PC ring (the chain ends `… 0x08001846 0x08001870 0x0800223E → 0x0`).

## Next steps

The targeted-watchpoint approach is exhausted (the wrong branch target is
computed / invisible to every C-path and value watch tried). The decisive tool is
now a real differential:

1. **Build a true-lockstep diff** (host-side / inside casio-emu, since two full
   memory snapshots won't fit the add-in): run the dynarec and interpreter with
   **independent register files + memory**, no per-block reseed, comparing full
   register/PC state after each block and advancing through IRQ/`IntrWait` waits
   with each core's own `update_gba`. The first PC/register divergence is the
   cause. Distinguish benign IRQ-timing skew (both at valid PCs, re-converges)
   from the real bug (the JIT branches to a PC the interpreter never reaches and
   does not re-converge — `@@IRQ`=0 near the BL suggests the critical divergence
   is *not* IRQ-timing, so the lockstep should find a clean codegen divergence).
2. Once the first wrong branch is pinned, examine the SH4 codegen for the op that
   computes the bad target (a Thumb data-proc / shift / hi-reg move producing a
   code pointer off by 2). Fix it.
3. Separately, the **native-LDST-off frame-696 freeze** (`@@WJ`=0, a different
   failure) is a *second* bug — pin it the same way.
4. Re-verify new-game → gameplay, interp vs JIT, frame-by-frame past 700/1000.

## History / superseded

The original hypothesis was a mistranslated **BIOS LZ77 unpack at `0xB5C`**,
reported by the block-diff as the "first divergence". **That was a harness
artifact, not a codegen bug:** gpSP blocks contain internal backward branches, so
for a loop block `dpc` is the backward-branch target, which the interpreter
passes through *sequentially, early* (the loop-body entry) long before the dyn's
block exits there. `interp_run_to_pc` stopped at that first early pass and
compared the interp at the loop entry vs the dyn after a full body pass —
apples-to-oranges. Forcing the routine to one-instruction blocks made it match,
and the "divergence" just hopped to the next loop-bearing block (`0xB60` →
`0xC14` …); the smoking gun was `du20 iu2`. This is why the harness now has the
loop-artifact retry above.

See [[sh4-newgame-decompress-bug]], [[sh4-block-diff-loop-artifact]],
[[casio-emu-timing-not-cycle-accurate]], [[dynarec-cycle-accuracy-practice]],
[[dispatch-null-overflow-crash]].
