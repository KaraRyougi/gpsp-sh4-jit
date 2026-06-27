# SH4 dynarec: new-game decompression bug (Metroid Fusion)

Status: **root cause localized, fix in progress.** Distinct from the cache-overflow
crash fixed in `d3fb4f8` (re-dispatch on NULL block lookup).

## Symptom

Driving Metroid Fusion past the file-select to **start a new game**, the SH4 dynarec
reaches the open-source BIOS ("Normmatt") boot splash and hangs at ~5% speed. The
**interpreter** on the identical input proceeds into the intro cutscene
("I'd been assigned to watch over Biologic's research team…") and on toward gameplay.
So the JIT renders boot/title/menus pixel-identically to the interpreter, but cannot
start a game — this is the real "doesn't reach gameplay" blocker.

It is **not** a SoftReset SWI (a SWI-reset trace showed none) and not the cache-overflow
crash. The guest control flow goes *wild* and lands in the BIOS boot.

## Root cause (localized via the block-diff harness)

Running the JIT and interpreter **lockstep, block by block** from frame 620 (just after
the new game starts), the first register divergence is at **block 8170, guest PC
`0x00000B5C`** — the open-source BIOS's **LZ77/decompression routine**:

```
B8170 pB5C r5  i8000 d7FFF      count off by one (0x8000 vs 0x7FFF)
B8170 pB5C r11 i8    d7         "
B8170 pB5C r6  i859A1BD d859A1BE  source pointer +1 (0x0859A1BC vs ...BD)
B8170 pB5C r7  iF    d1E
B8170 pB5C r2  i1    d0 ; r4 i0 d1 ; r12 i0 d8
```

The JIT mistranslates one instruction in this BIOS block, so the unpack loop runs one
iteration off. That corrupts the decompressed cutscene data/code → the guest jumps to a
bogus address → it ends up executing the BIOS boot → "Normmatt" + stall.

### The block (BIOS `0xB5C`, ARM — from `vendor/gpsp/bios/open_gba_bios.bin`)

```
0B5C: push {r4-r8, sb, sl, fp}
0B60: ldr  r5, [r0], #4      ; post-indexed load (writeback r0 += 4)
0B64: cmp  r2, #0
0B68: lsreq r5, r5, #8       ; CONDITIONAL data-proc (eq)
0B6C: beq  0xB90
0B70: tst  r0, #0x0e000000
...                          ; LZ77 unpack with a byte loop (ldrb r7,[r0]; ...)
```

ARM `LDST_NATIVE` is **off**, so loads already go through the correct C helper — the
prime suspects are therefore **ARM conditional execution** (`lsreq`) or a data-proc /
flag in the unpack loop, not the load itself.

## How to reproduce / diagnose

- **Reach the new game (tolerant input — rapid input diverges via approximate timing,
  which is *not* a codegen bug):** `START_FRAME=30 START_HOLD=8`, `A_FRAME=120
  A_PERIOD=120 A_PRESS=6`, run ~620+ frames.
- **Block-diff harness:** `-DCGBA_GPSP_HEADLESS_DIFF_FRAME=620
  -DCGBA_GPSP_HEADLESS_DIFF_BLOCKS=80000`. The first `B<n> p<pc> r<i> i<x> d<y>` line is
  the divergent block. **NB:** for this hunt `sh4_diff_harness.c` was temporarily patched
  to (a) ignore benign cycle and DMA-deferral halt/sleep diffs and (b) process halts
  (`update_gba`) and keep diffing instead of bailing. **Revert those before normal use.**
- **External observer (avoids the layout-Heisenbug):** instrument *casio-emu*
  `src/interpreter.c` (low-PC `BADJUMP` ring) for SH4-side wild jumps, and
  `block_lookup_address` (not `translate_block`) for guest-PC traces.

## Narrowing (done)

The full routine (`0xB5C`–`0xC4C`) is a Huffman/bit-stream unpacker: an outer byte loop
(`0xBA4`–`0xC08`) and an inner 8-bit loop (`0xBB8`–`0xBFC`) with a 2-state accumulator
built from **conditional data-proc** (`cmp r4,#1; strheq/moveq … addne/movne …`) and
`subs`-driven loop counters (`subs r5,#1`, `subs fp,#1`). The JIT runs the loop **one
iteration off** (dyn ran the full inner body where the interp took an early exit), so it
is a control-flow / flag divergence, not a value op.

Ruled out by experiment:
- **Native ARM emitters** (dp / block / multiply): building with `-DCGBA_DIAG_NO_ARM_NATIVE=1`
  (short-circuit all three to the C helpers) leaves the divergence **byte-identical** →
  not a native emitter.
- **The C load helper** `cgba_sh4_arm_ldst`: post-indexed writeback (`ldr r5,[r0],#4`,
  `ldrb`) is handled correctly (`!pre → reg[rn] = base±offset`).

So the bug is in machinery shared by both builds and applied around the conditional run:
**ARM conditional execution / flag handling** in this loop (`generate_cond_emit_far` /
`sh4g_cond_to_T`, or a flag set by `subs`/`tst` that a following condition reads).

## Next steps

1. Instruction-granular pin: force ~1-instruction gpSP blocks so the block-diff resolves
   to the single mistranslated op (the block-level diff stops at the whole `0xB5C` block).
2. Fix the SH4 codegen / flag handling for it.
3. Re-verify the full new-game → gameplay path, interp vs JIT.
