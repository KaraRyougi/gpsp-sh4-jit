# SH4 dynarec: new-game freeze (Metroid Fusion)

Status: **FIXED for the frame-700 freeze** by
`b665a78 Fix SH4 Thumb BL prefix resume state`. The dynarec now reaches frame
3000 in the paired Metroid harness without crashing. A separate later
dynarec-only blackout remains open between frames 3875 and 3900; see
[sh4-jit-status.md](sh4-jit-status.md#current-metroid-fusion-harness-findings-2026-06-28).

Historical symptom: the dynarec rendered the new-game intro cutscene, then
**froze ~frame 700 on a control-flow divergence** — it branched into code the
game never executes, popped a (legitimately) zero stack word as a code pointer,
and `bx 0` → guest reset → BIOS boot → frozen. Distinct from the cache-overflow
crash fixed in `d3fb4f8`.

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

## Ruled out

- **Native LDST emitters are not the cause.**
  `-DCGBA_SH4_THUMB_LDST_NATIVE=OFF -DCGBA_SH4_ARM_LDST_NATIVE=OFF` still freezes
  — *earlier* (frame 696) and *differently* (no BIOS storm). So native LDST only
  perturbs which value happens to be live; the bug is in shared machinery.
- **Value/stack corruption** of the popped slot (the 0 is written identically by
  both cores) and a **PUSH/POP SP-writeback miscalc** (SP is sane at the jump,
  `0x03007E24` in the IWRAM stack; a wrong r13 would show in the register diff).
- **The BIOS LZ77 unpack at `0xB5C`** — the earlier "first divergence here" was a
  harness artifact (see History below).

## Fixed along the way (a real bug, but NOT this freeze)

A multi-agent audit found that `cgba_sh4_arm_block` transferred the
current/privileged register bank for S-bit block transfers, whereas the gpSP
interpreter (`cpu.cc` `exec_arm_block_mem`, ~1010/1041) brackets the transfer in
USER mode whenever `s_bit && (store || rn != r15)`. For an `LDM{pc}^` exception
return that loaded the popped user `r13`/`r14` into the wrong bank, then the SPSR
re-bank discarded them and reloaded stale USER `sp`/`lr`.

**Fixed** (`ports/fxcg100/sh4/sh4_interp_helpers.c`): `set_cpu_mode(MODE_USER)`
around the transfer, restore the entry mode after, matching the interpreter
exactly. Genuine latent bug; **regression-neutral on this ROM** (instrumentation
showed **zero** `LDM{pc}^`-with-r13/r14 opcodes fire near the freeze — the BIOS
IRQ returns use `subs pc,lr,#4`, not `ldm^`), so it does not change the freeze.

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

Done for this bug: Thumb-BL prefix handling was the bad target source. The SH4
emitter now materializes the prefix LR so exits between the BL halves resume at
the suffix with the right temporary link state. Keep this file as the historical
record for the frame-700 failure.

For the next Metroid issue, continue from the current finding in
[sh4-jit-status.md](sh4-jit-status.md#current-metroid-fusion-harness-findings-2026-06-28):
the dynarec still blacks out between frames 3875 and 3900 while the interpreter
continues rendering.

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
