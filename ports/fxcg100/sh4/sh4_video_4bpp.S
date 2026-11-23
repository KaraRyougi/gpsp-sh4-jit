/*
 * Hand-written SH-4 big-endian 4-bpp FULLCOLOR text background span renderers.
 *
 * Replaces GCC's generic per-pixel shld/eswap32 codegen in
 * cgba_render_4bpp_fullcolor_span<isbase> (which accounted for ~42% of all
 * instructions during continuous Pokemon Emerald overworld scrolling).
 *
 * Big-endian SH-4 layout (MSB_FIRST=1):
 *   *map_ptr (u16):  bits [15:8] = tile_id[7:0],
 *                    bits [7:4]  = subpalette (0..15) -> (raw_map & 0xF0) << 1 = byte offset,
 *                    bit  3      = vflip (0x08),
 *                    bit  2      = hflip (0x04),
 *                    bits [1:0]  = tile_id[9:8].
 *   *tile_ptr (u32): [b0, b1, b2, b3] where b0=(p1<<4)|p0 .. b3=(p7<<4)|p6.
 *   t_even = raw << 1: bytes LSB->MSB hold (p6*2, p4*2, p2*2, p0*2) in bits [4:1].
 *   t_odd  = raw >> 3: bytes LSB->MSB hold (p7*2, p5*2, p3*2, p1*2) in bits [4:1].
 *
 * Pure 16-bit SH-4 instructions only (MOV.W R0, @(disp4, Rn) uses R0).
 * Both functions are leaf routines obeying the standard SH-4 C ABI and handle
 * arbitrary 2-byte alignment of dest_ptr.
 */

	.balign 4

/* ============================================================================
 * Shared GAS helper macros (zero runtime overhead; single source of truth).
 * ============================================================================ */

/* Branchless SWAR RGB565 per-channel saturation on expanded pfe (0x07E0F81F
 * layout, overflow bits in \ovf = 0x08010020 at bits 27/16/5).
 * Clamps any overflowing channel to all-1s (G=0x07E00000, R=0x0000F800, B=0x1F)
 * in \pfe; caller then masks with 0x07E0F81F.
 * Clobbers \t1 and \t2 (\t1 may alias \ovf if caller no longer needs \ovf). */
.macro CGBA_SWAR_SAT_PFE pfe, ovf, t1, t2
	.ifnc \ovf,\t1
	mov     \ovf, \t1
	.endif
	and     \pfe, \t1                   /* ovf = pfe & 0x08010020 */
	mov     \t1, \t2
	shlr2   \t2
	shlr2   \t2
	shlr    \t2                         /* ovf >> 5 */
	sub     \t2, \t1                    /* R/B full 5-bit fill; G top 5 bits */
	shlr    \t2                         /* ovf >> 6 (G bit 21) */
	or      \t2, \t1
	or      \t1, \pfe
.endm

/* Unpack 4 packed 4bpp bytes in \wreg ([b0|b1|b2|b3], big-endian) into 8
 * pre-shifted nibble bytes (pix << 1) at @(\dst) via the 512B _cgba_nib2_lut
 * in \lut. Clobbers \wreg and r0. */
.macro CGBA_UNPACK_4BPP_TILE_LUT wreg, dst, lut
	extu.b  \wreg, r0
	add     r0, r0
	mov.w   @(r0, \lut), r0
	shlr8   \wreg
	mov.w   r0, @(6, \dst)

	extu.b  \wreg, r0
	add     r0, r0
	mov.w   @(r0, \lut), r0
	shlr8   \wreg
	mov.w   r0, @(4, \dst)

	extu.b  \wreg, r0
	add     r0, r0
	mov.w   @(r0, \lut), r0
	shlr8   \wreg
	mov.w   r0, @(2, \dst)

	extu.b  \wreg, r0
	add     r0, r0
	mov.w   @(r0, \lut), r0
	mov.w   r0, @\dst
.endm

/* 8-pixel paired 32-bit mov.l LUT blit for an 8bpp tile row (4-byte aligned \dst).
 * \w0 = [p0 p1 p2 p3], \w1 = [p4 p5 p6 p7] (clobbered), \pal = 256-entry u16 LUT.
 * Clobbers r0, r1, \w0, \w1. */
.macro CGBA_8BPP_BLIT8_XTRCT w0, w1, dst, pal, hflip
.if \hflip
	extu.b  \w0, r0                     /* p3 -> dst[4] */
	shll    r0
	mov.w   @(r0, \pal), r1
	shlr8   \w0
	extu.b  \w0, r0                     /* p2 -> dst[5] */
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w0
	swap.w  r0, r0
	xtrct   r1, r0
	mov.l   r0, @(8, \dst)
	extu.b  \w0, r0                     /* p1 -> dst[6] */
	shll    r0
	mov.w   @(r0, \pal), r1
	shlr8   \w0
	mov     \w0, r0                     /* p0 -> dst[7] */
	shll    r0
	mov.w   @(r0, \pal), r0
	swap.w  r0, r0
	xtrct   r1, r0
	mov.l   r0, @(12, \dst)
	extu.b  \w1, r0                     /* p7 -> dst[0] */
	shll    r0
	mov.w   @(r0, \pal), r1
	shlr8   \w1
	extu.b  \w1, r0                     /* p6 -> dst[1] */
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w1
	swap.w  r0, r0
	xtrct   r1, r0
	mov.l   r0, @\dst
	extu.b  \w1, r0                     /* p5 -> dst[2] */
	shll    r0
	mov.w   @(r0, \pal), r1
	shlr8   \w1
	mov     \w1, r0                     /* p4 -> dst[3] */
	shll    r0
	mov.w   @(r0, \pal), r0
	swap.w  r0, r0
	xtrct   r1, r0
	mov.l   r0, @(4, \dst)
.else
	extu.b  \w0, r0                     /* p3 */
	shll    r0
	mov.w   @(r0, \pal), r1
	shlr8   \w0
	extu.b  \w0, r0                     /* p2 */
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w0
	swap.w  r1, r1
	xtrct   r0, r1
	mov.l   r1, @(4, \dst)
	extu.b  \w0, r0                     /* p1 */
	shll    r0
	mov.w   @(r0, \pal), r1
	shlr8   \w0
	mov     \w0, r0                     /* p0 */
	shll    r0
	mov.w   @(r0, \pal), r0
	swap.w  r1, r1
	xtrct   r0, r1
	mov.l   r1, @\dst
	extu.b  \w1, r0                     /* p7 */
	shll    r0
	mov.w   @(r0, \pal), r1
	shlr8   \w1
	extu.b  \w1, r0                     /* p6 */
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w1
	swap.w  r1, r1
	xtrct   r0, r1
	mov.l   r1, @(12, \dst)
	extu.b  \w1, r0                     /* p5 */
	shll    r0
	mov.w   @(r0, \pal), r1
	shlr8   \w1
	mov     \w1, r0                     /* p4 */
	shll    r0
	mov.w   @(r0, \pal), r0
	swap.w  r1, r1
	xtrct   r0, r1
	mov.l   r1, @(8, \dst)
.endif
.endm

/* 8-pixel 16-bit mov.w LUT blit for an unaligned (4k+2) 8bpp tile row. */
.macro CGBA_8BPP_BLIT8_W16 w0, w1, dst, pal, hflip
.if \hflip
	extu.b  \w0, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w0
	mov.w   r0, @(8, \dst)
	extu.b  \w0, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w0
	mov.w   r0, @(10, \dst)
	extu.b  \w0, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w0
	mov.w   r0, @(12, \dst)
	mov     \w0, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	mov.w   r0, @(14, \dst)
	extu.b  \w1, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w1
	mov.w   r0, @\dst
	extu.b  \w1, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w1
	mov.w   r0, @(2, \dst)
	extu.b  \w1, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w1
	mov.w   r0, @(4, \dst)
	mov     \w1, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	mov.w   r0, @(6, \dst)
.else
	extu.b  \w0, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w0
	mov.w   r0, @(6, \dst)
	extu.b  \w0, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w0
	mov.w   r0, @(4, \dst)
	extu.b  \w0, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w0
	mov.w   r0, @(2, \dst)
	mov     \w0, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	mov.w   r0, @\dst
	extu.b  \w1, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w1
	mov.w   r0, @(14, \dst)
	extu.b  \w1, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w1
	mov.w   r0, @(12, \dst)
	extu.b  \w1, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	shlr8   \w1
	mov.w   r0, @(10, \dst)
	mov     \w1, r0
	shll    r0
	mov.w   @(r0, \pal), r0
	mov.w   r0, @(8, \dst)
.endif
.endm

/* ============================================================================
 * void cgba_render_4bpp_span_over_sh4(
 *   const u16 *map_ptr,   // r4
 *   u32 todraw,           // r5
 *   u16 *dest_ptr,        // r6
 *   const u8 *tile_base,  // r7
 *   s32 vflip_off,        // @(0, sp_in)
 *   const u16 *paltbl,    // @(4, sp_in)
 *   u32 skip_zero)        // @(8, sp_in)
 * ========================================================================== */
	.section .text._cgba_render_4bpp_span_over_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_4bpp_span_over_sh4
	.type   _cgba_render_4bpp_span_over_sh4, @function
_cgba_render_4bpp_span_over_sh4:
	tst     r5, r5
	bt      .Lover_ret_early
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	mov     r5, r8                      /* r8  = todraw */
	mov.l   @(28, r15), r9              /* r9  = vflip_off */
	mov.l   @(32, r15), r10             /* r10 = paltbl */
	mov.l   @(36, r15), r12             /* r12 = skip_zero (0 or 1) */
	mov.w   .Lover_7fe0, r11            /* r11 = 0x00007FE0 */
	mov.l   .Lover_eeeef, r13           /* r13 = 0xEEEEEEEF */
	mov.l   .Lover_88888, r14           /* r14 = 0x88888888 */

	.balign 4
.Lover_loop:
	mov.w   @r4+, r0                    /* r0 = raw_map (sign-ext) */
	mov     r0, r1
	or      r12, r1                     /* raw_map | skip_zero */
	tst     r1, r1
	bt.s    .Lover_next
	swap.b  r0, r1                      /* r1[9:0] = tile_id */

	shll2   r1
	add     r1, r1
	shll2   r1
	and     r11, r1                     /* r1 = (tile_id & 0x3FF) << 5 */
	tst     #0x08, r0                   /* T = !vflip */
	bt.s    .Lover_novf
	add     r7, r1                      /* r1 = tile_base + tile_id*32 */
	add     r9, r1                      /* vflip: + vflip_off */
.Lover_novf:
	mov.l   @r1, r2                     /* r2 = raw [b0 b1 b2 b3] */
	tst     r2, r2
	bt.s    .Lover_next
	mov     r0, r3                      /* r3 = raw_map */

	/* subpal = paltbl + ((raw_map & 0xF0) << 1) bytes */
	and     #0xF0, r0
	add     r0, r0
	mov     r10, r5
	add     r0, r5

	/* Opaque check: ((raw + 0xEEEEEEEF) & ~raw & 0x88888888) == 0 */
	mov     r13, r0
	not     r2, r1
	add     r2, r0
	and     r1, r0
	tst     r14, r0                     /* T = opaque */
	bf.s    .Lover_partial_tramp
	mov     r3, r0                      /* r0 = raw_map */

	/* Opaque row: r1 = raw >> 3 (t_odd), r2 = raw << 1 (t_even) */
	mov     r2, r1
	shlr2   r1
	shlr    r1                          /* clobbers T; must precede tst */
	tst     #0x04, r0                   /* T = !hflip */
	bf.s    .Lover_op_hf
	add     r2, r2

	/* Opaque !hflip */
	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r2
	mov.w   r0, @(12, r6)               /* dest[6] = p6 */

	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r2
	mov.w   r0, @(8, r6)                /* dest[4] = p4 */

	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r2
	mov.w   r0, @(4, r6)                /* dest[2] = p2 */

	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r3
	mov     r1, r0
	mov.w   r3, @r6                     /* dest[0] = p0 */

	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r1
	mov.w   r0, @(14, r6)               /* dest[7] = p7 */

	mov     r1, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r1
	mov.w   r0, @(10, r6)               /* dest[5] = p5 */

	mov     r1, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r1
	mov.w   r0, @(6, r6)                /* dest[3] = p3 */

	mov     r1, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	dt      r8
	mov.w   r0, @(2, r6)                /* dest[1] = p1 */
	bf.s    .Lover_loop
	add     #16, r6
	bra     .Lover_done
	nop

.Lover_next:
	dt      r8
	bf.s    .Lover_loop
	add     #16, r6

.Lover_done:
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lover_ret_early:
	rts
	nop

.Lover_partial_tramp:
	bra     .Lover_partial
	nop

	.balign 4
.Lover_eeeef:
	.long   0xEEEEEEEF
.Lover_88888:
	.long   0x88888888
.Lover_7fe0:
	.word   0x7FE0
	.balign 4

	/* Opaque hflip */
.Lover_op_hf:
	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r2
	mov.w   r0, @(2, r6)                /* dest[1] = p6 */

	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r2
	mov.w   r0, @(6, r6)                /* dest[3] = p4 */

	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r2
	mov.w   r0, @(10, r6)               /* dest[5] = p2 */

	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(14, r6)               /* dest[7] = p0 */

	mov     r1, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r3
	shlr8   r1
	mov.w   r3, @r6                     /* dest[0] = p7 */

	mov     r1, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r1
	mov.w   r0, @(4, r6)                /* dest[2] = p5 */

	mov     r1, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r1
	mov.w   r0, @(8, r6)                /* dest[4] = p3 */

	mov     r1, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	dt      r8
	mov.w   r0, @(12, r6)               /* dest[6] = p1 */
	bf.s    .Lover_loop_bra
	add     #16, r6
	bra     .Lover_done
	nop

.Lover_loop_bra:
	bra     .Lover_loop
	nop

	/* Partially transparent overlay row (<4% of tiles) */
.Lover_partial:
	mov     r2, r1
	shlr2   r1
	shlr    r1                          /* clobbers T; must precede tst */
	tst     #0x04, r0                   /* T = !hflip */
	bf.s    .Lover_part_hf
	add     r2, r2

	/* Partial !hflip */
	mov     r2, r0
	tst     #0x1e, r0
	bt.s    1f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(12, r6)
1:	mov     r2, r0
	tst     #0x1e, r0
	bt.s    2f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(8, r6)
2:	mov     r2, r0
	tst     #0x1e, r0
	bt.s    3f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(4, r6)
3:	mov     r2, r0
	tst     #0x1e, r0
	bt      4f
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @r6
4:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    5f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(14, r6)
5:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    6f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(10, r6)
6:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    7f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(6, r6)
7:	mov     r1, r0
	tst     #0x1e, r0
	bt      8f
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(2, r6)
8:	dt      r8
	bt      .Lover_done_bra
	bra     .Lover_loop
	add     #16, r6

	/* Partial hflip */
.Lover_part_hf:
	mov     r2, r0
	tst     #0x1e, r0
	bt.s    1f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(2, r6)
1:	mov     r2, r0
	tst     #0x1e, r0
	bt.s    2f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(6, r6)
2:	mov     r2, r0
	tst     #0x1e, r0
	bt.s    3f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(10, r6)
3:	mov     r2, r0
	tst     #0x1e, r0
	bt      4f
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(14, r6)
4:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    5f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @r6
5:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    6f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(4, r6)
6:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    7f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(8, r6)
7:	mov     r1, r0
	tst     #0x1e, r0
	bt      8f
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(12, r6)
8:	dt      r8
	bt      .Lover_done_bra
	bra     .Lover_loop
	add     #16, r6
.Lover_done_bra:
	bra     .Lover_done
	nop
	.size   _cgba_render_4bpp_span_over_sh4, .-_cgba_render_4bpp_span_over_sh4


/* ============================================================================
 * void cgba_render_4bpp_span_base_sh4(
 *   const u16 *map_ptr,   // r4
 *   u32 todraw,           // r5
 *   u16 *dest_ptr,        // r6
 *   const u8 *tile_base,  // r7
 *   s32 vflip_off,        // @(0, sp_in)
 *   const u16 *paltbl)    // @(4, sp_in) -- backdrop_shadow_palette
 * ========================================================================== */
	.section .text._cgba_render_4bpp_span_base_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_4bpp_span_base_sh4
	.type   _cgba_render_4bpp_span_base_sh4, @function
_cgba_render_4bpp_span_base_sh4:
	tst     r5, r5
	bt      .Lbase_ret_early
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15

	mov     r5, r8                      /* r8  = todraw */
	mov.l   @(20, r15), r9              /* r9  = vflip_off */
	mov.l   @(24, r15), r10             /* r10 = paltbl */
	mov.w   .Lbase_7fe0, r11            /* r11 = 0x00007FE0 */
	mov.w   @r10, r12                   /* r12 = bgcolor (paltbl[0]) */

	.balign 4
.Lbase_loop:
	mov.w   @r4+, r0                    /* r0 = raw_map */
	swap.b  r0, r1                      /* r1[9:0] = tile_id */
	shll2   r1
	add     r1, r1
	shll2   r1
	and     r11, r1                     /* r1 = (tile_id & 0x3FF) << 5 */
	tst     #0x08, r0                   /* T = !vflip */
	bt.s    .Lbase_novf
	add     r7, r1
	add     r9, r1
.Lbase_novf:
	mov.l   @r1, r2                     /* r2 = raw [b0 b1 b2 b3] */
	tst     r2, r2
	bt.s    .Lbase_empty
	mov     r0, r3                      /* r3 = raw_map */

	/* subpal = paltbl + ((raw_map & 0xF0) << 1) bytes */
	and     #0xF0, r0
	add     r0, r0
	mov     r10, r5
	add     r0, r5

	mov     r2, r1
	shlr2   r1
	mov     r3, r0
	shlr    r1                          /* r1 = raw >> 3 (t_odd) */
	tst     #0x04, r0                   /* T = !hflip */
	bf.s    .Lbase_hf
	add     r2, r2                      /* r2 = raw << 1 (t_even) */

	/* Base !hflip */
	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r2
	mov.w   r0, @(12, r6)               /* dest[6] = p6 */

	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r2
	mov.w   r0, @(8, r6)                /* dest[4] = p4 */

	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r2
	mov.w   r0, @(4, r6)                /* dest[2] = p2 */

	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r3
	mov     r1, r0
	mov.w   r3, @r6                     /* dest[0] = p0 */

	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r1
	mov.w   r0, @(14, r6)               /* dest[7] = p7 */

	mov     r1, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r1
	mov.w   r0, @(10, r6)               /* dest[5] = p5 */

	mov     r1, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r1
	mov.w   r0, @(6, r6)                /* dest[3] = p3 */

	mov     r1, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	dt      r8
	mov.w   r0, @(2, r6)                /* dest[1] = p1 */
	bf.s    .Lbase_loop
	add     #16, r6

.Lbase_done:
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lbase_ret_early:
	rts
	nop

	/* Empty base tile row: fill 8 pixels with bgcolor (r12) */
.Lbase_empty:
	mov     r12, r0
	mov.w   r0, @r6
	mov.w   r0, @(2, r6)
	mov.w   r0, @(4, r6)
	mov.w   r0, @(6, r6)
	mov.w   r0, @(8, r6)
	mov.w   r0, @(10, r6)
	mov.w   r0, @(12, r6)
	dt      r8
	mov.w   r0, @(14, r6)
	bf.s    .Lbase_loop
	add     #16, r6
	bra     .Lbase_done
	nop

	/* Base hflip */
.Lbase_hf:
	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r2
	mov.w   r0, @(2, r6)                /* dest[1] = p6 */

	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r2
	mov.w   r0, @(6, r6)                /* dest[3] = p4 */

	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r2
	mov.w   r0, @(10, r6)               /* dest[5] = p2 */

	mov     r2, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(14, r6)               /* dest[7] = p0 */

	mov     r1, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r3
	shlr8   r1
	mov.w   r3, @r6                     /* dest[0] = p7 */

	mov     r1, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r1
	mov.w   r0, @(4, r6)                /* dest[2] = p5 */

	mov     r1, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	shlr8   r1
	mov.w   r0, @(8, r6)                /* dest[4] = p3 */

	mov     r1, r0
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	dt      r8
	mov.w   r0, @(12, r6)               /* dest[6] = p1 */
	bt      .Lbase_done_bra
	bra     .Lbase_loop
	add     #16, r6
.Lbase_done_bra:
	bra     .Lbase_done
	nop

	.balign 4
.Lbase_7fe0:
	.word   0x7FE0
	.size   _cgba_render_4bpp_span_base_sh4, .-_cgba_render_4bpp_span_base_sh4


/* ============================================================================
 * 2-pixel-at-a-time pair-LUT renderers using 32-bit MOV.L (aligned) and
 * XTRCT + MOV.L (unaligned) stores.
 *
 * pair_lut[pal][b] = (subpal[b & 0xF] << 16) | subpal[b >> 4] (BE: [p_even|p_odd])
 * where subpal uses backdrop_shadow_palette (subpal[0] == palette[0]).
 * ========================================================================== */

/* ============================================================================
 * void cgba_render_4bpp_plan_base_sh4(
 *   const cgba_mode0_bg_plan *p, // r4
 *   u32 vcount,                  // r5
 *   u16 *dest_ptr,               // r6
 *   const u32 *pair_lut)         // r7
 *
 * Fast 4-arg Mode-0 aligned base-layer entry (p->flags & 1, p->tile_hoff == 0).
 * Also: void cgba_render_4bpp_span_base_pair_sh4 / span2_base_pair_sh4(
 *   const u16 *map_ptr,   // r4
 *   u32 todraw,           // r5 (todraw1 | (todraw2 << 16))
 *   u16 *dest_ptr,        // r6
 *   const u8 *tile_base,  // r7
 *   s32 vflip_off,        // @(0, sp_in)
 *   const u32 *pair_lut,  // @(4, sp_in)
 *   const u16 *map2)      // @(8, sp_in) (only when todraw2 != 0)
 * ========================================================================== */
	.section .text._cgba_render_4bpp_plan_base_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_4bpp_plan_base_sh4
	.type   _cgba_render_4bpp_plan_base_sh4, @function
_cgba_render_4bpp_plan_base_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	mov     r7, r10                     /* r10 = pair_lut */
	mov     r4, r1                      /* r1  = p */
	mov.w   @(24, r1), r0               /* r0  = p->vofs */
	add     r5, r0
	mov.w   .Lbp_01ff, r2
	and     r0, r2                      /* r2  = voffset (0..511) */

	mov     r2, r0
	and     #7, r0
	shll2   r0                          /* r0  = vert_pix */
	mov.l   @r1, r7                     /* r7  = p->tile_charbase */
	add     r0, r7                      /* r7  = tile_base */
	add     r0, r0                      /* r0  = vert_pix << 1 */
	mov     #28, r9
	sub     r0, r9                      /* r9  = vflip_off */

	mov     r2, r0
	shlr8   r0                          /* vhalf (0 or 1) */
	shll2   r0
	add     #4, r0
	mov.l   @(r0, r1), r3               /* r3  = p->sb_ptr[vhalf] */
	mov     r2, r0
	and     #0xF8, r0
	shll2   r0
	add     r0, r0                      /* (voffset & 0xF8) << 3 */
	add     r0, r3                      /* r3  = map_row_b */

	mov.w   @(26, r1), r0               /* first_byte_off */
	mov     r3, r4
	add     r0, r4                      /* r4  = map_ptr */
	mov.w   @(28, r1), r0               /* map2_byte_off */
	add     r3, r0                      /* r0  = map2 */
	lds     r0, macl                    /* macl = map2 */
	mov.l   @(12, r1), r5               /* counts_packed */
	extu.w  r5, r8                      /* r8  = todraw1 */
	shlr16  r5                          /* r5  = todraw2 */
	lds     r5, mach                    /* mach = todraw2 */

	mov.w   .Lbp_7fe0, r11              /* r11 = 0x00007FE0 */
	mov.l   .Lbp_10000, r12             /* r12 = sentinel prev1 */
	mov     #5, r13                     /* r13 = 5 (tile_id << 5) */
	bra     .Lbp_plan_join
	mov     #6, r14                     /* r14 = 6 (pal*16 << 6 = pal*1024) */
	.size   _cgba_render_4bpp_plan_base_sh4, .-_cgba_render_4bpp_plan_base_sh4

	.balign 4
	.global _cgba_render_4bpp_span_base_pair_sh4
	.global _cgba_render_4bpp_span2_base_pair_sh4
	.type   _cgba_render_4bpp_span_base_pair_sh4, @function
	.type   _cgba_render_4bpp_span2_base_pair_sh4, @function
_cgba_render_4bpp_span2_base_pair_sh4:
_cgba_render_4bpp_span_base_pair_sh4:
	tst     r5, r5
	bt      .Lbp_ret_early
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	extu.w  r5, r8                      /* r8  = todraw1 */
	shlr16  r5                          /* r5  = todraw2 (0 for 1-span callers) */
	lds     r5, mach                    /* mach = todraw2 */
	tst     r5, r5
	bt      1f
	mov.l   @(36, r15), r0              /* r0  = map2 */
	lds     r0, macl                    /* macl = map2 */
1:	mov.l   @(28, r15), r9              /* r9  = vflip_off */
	mov.l   @(32, r15), r10             /* r10 = pair_lut */
	mov.w   .Lbp_7fe0, r11              /* r11 = 0x00007FE0 */
	mov.l   .Lbp_10000, r12             /* r12 = sentinel prev1 */
	mov     #5, r13                     /* r13 = 5 (tile_id << 5) */
	mov     #6, r14                     /* r14 = 6 (pal*16 << 6 = pal*1024) */

.Lbp_plan_join:
	mov     r6, r0
	tst     #2, r0                      /* T = 4-byte aligned dest_ptr */
	bf      .Lbp_to_unaligned

	/* ---- Aligned (4k) base loop ---- */
	.balign 4
.Lbp_a_loop:
	mov.w   @r4+, r0
	cmp/eq  r12, r0                     /* same raw_map as prev1? */
	bt      .Lbp_a_rep1
.Lbp_a_have_r0:
	mov     r0, r12                     /* prev1 = cur */
	swap.b  r0, r1
	shld    r13, r1                     /* r1 = tile_id << 5 */
	and     r11, r1
	tst     #0x08, r0                   /* T = !vflip */
	bt.s    1f
	add     r7, r1
	add     r9, r1
1:	mov.l   @r1, r2                     /* r2 = [b0 b1 b2 b3] */

	tst     #0x04, r0                   /* T = !hflip (preserved below) */
	and     #0xF0, r0                   /* pal * 16 */
	shld    r14, r0                     /* pal * 1024 (T unchanged) */
	mov     r10, r5
	bf.s    .Lbp_a_hf
	add     r0, r5                      /* r5 = &pair_lut[pal][0] */

	/* Aligned base !hflip: 4 lookups + 4 longword stores */
	extu.b  r2, r0                      /* b3 (p6, p7) */
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	mov.l   r3, @(12, r6)

	extu.b  r2, r0                      /* b2 (p4, p5) */
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	mov.l   r3, @(8, r6)

	extu.b  r2, r0                      /* b1 (p2, p3) */
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	mov.l   r3, @(4, r6)

	extu.b  r2, r0                      /* b0 (p0, p1) */
	shll2   r0
	mov.l   @(r0, r5), r3
	dt      r8
	mov.l   r3, @r6
	bf.s    .Lbp_a_loop
	add     #16, r6

.Lbp_done:
	sts     mach, r8                    /* r8 = todraw2 */
	tst     r8, r8
	bf      .Lbp_span2
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lbp_span2:
	sts     macl, r4                    /* r4 = map2 */
	mov     #0, r0
	lds     r0, mach                    /* todraw2 = 0 */
	mov     r6, r0
	tst     #2, r0
	bf      .Lbp_to_unaligned
	bra     .Lbp_a_loop
	nop

.Lbp_ret_early:
	rts
	nop

.Lbp_to_unaligned:
	bra     .Lbp_u_loop
	nop

	.balign 4
.Lbp_10000:
	.long   0x00010000
.Lbp_7fe0:
	.word   0x7FE0
.Lbp_01ff:
	.word   0x01FF
	.balign 4

	/* Consecutive identical base tile run (aligned): load once, store in regs;
	 * runs of >=2 tiles enter out-of-line 2-tile-at-a-time pair loop. */
.Lbp_a_rep1:
	mov     r6, r1
	add     #-16, r1
	mov.l   @r1+, r2
	mov.l   @r1+, r3
	mov.l   @r1+, r5
	mov.l   @r1, r1
	mov.l   r2, @r6
	mov.l   r3, @(4, r6)
	mov.l   r5, @(8, r6)
	dt      r8
	mov.l   r1, @(12, r6)
	bt.s    .Lbp_done
	add     #16, r6
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bt      .Lbp_a_rep1_long
	bra     .Lbp_a_have_r0
	nop

	/* Aligned base hflip */
.Lbp_a_hf:
	extu.b  r2, r0                      /* b3 -> [p7|p6] */
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	swap.w  r3, r3
	mov.l   r3, @r6

	extu.b  r2, r0                      /* b2 -> [p5|p4] */
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	swap.w  r3, r3
	mov.l   r3, @(4, r6)

	extu.b  r2, r0                      /* b1 -> [p3|p2] */
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	swap.w  r3, r3
	mov.l   r3, @(8, r6)

	extu.b  r2, r0                      /* b0 -> [p1|p0] */
	shll2   r0
	mov.l   @(r0, r5), r3
	dt      r8
	swap.w  r3, r3
	mov.l   r3, @(12, r6)
	bt.s    .Lbp_a_done_bra
	add     #16, r6
	bra     .Lbp_a_loop
	nop
.Lbp_a_done_bra:
	bra     .Lbp_done
	nop

	/* Out-of-line 2-tile-at-a-time aligned base rep1 loop (borrows r13, r14) */
	.balign 4
.Lbp_a_rep1_long:
	mov.l   r2, @r6
	mov.l   r3, @(4, r6)
	mov.l   r5, @(8, r6)
	dt      r8
	mov.l   r1, @(12, r6)
	bt.s    .Lbp_a_done_bra
	add     #16, r6
	mov     r4, r0
	tst     #2, r0
	bt      .Lbp_a_rep1_aligned
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bf      .Lbp_a_have_r0_bra
	mov.l   r2, @r6
	mov.l   r3, @(4, r6)
	mov.l   r5, @(8, r6)
	dt      r8
	mov.l   r1, @(12, r6)
	bt.s    .Lbp_a_done_bra
	add     #16, r6
.Lbp_a_rep1_aligned:
	mov     r8, r13
	shlr    r13                         /* r13 = pairs remaining */
	tst     r13, r13
	bt      .Lbp_a_rep1_tail
	extu.w  r12, r14
	swap.w  r14, r0
	or      r0, r14                     /* r14 = [r12 | r12] */
.Lbp_a_rep1_pair:
	mov.l   @r4+, r0
	cmp/eq  r14, r0
	bf      .Lbp_a_rep1_pmiss
	mov.l   r2, @r6
	mov.l   r3, @(4, r6)
	mov.l   r5, @(8, r6)
	mov.l   r1, @(12, r6)
	mov.l   r2, @(16, r6)
	mov.l   r3, @(20, r6)
	mov.l   r5, @(24, r6)
	mov.l   r1, @(28, r6)
	dt      r13
	bf.s    .Lbp_a_rep1_pair
	add     #32, r6
.Lbp_a_rep1_tail:
	mov     #5, r13
	mov     #6, r14
	mov     r8, r0
	tst     #1, r0
	bt      .Lbp_a_done_bra
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bf.s    .Lbp_a_have_r0_bra
	mov     #1, r8
	mov.l   r2, @r6
	mov.l   r3, @(4, r6)
	mov.l   r5, @(8, r6)
	mov.l   r1, @(12, r6)
	bra     .Lbp_done
	add     #16, r6
.Lbp_a_rep1_pmiss:
	shll    r13
	mov     #1, r14
	and     r8, r14
	mov     r13, r8
	add     r14, r8
	swap.w  r0, r13
	exts.w  r13, r13
	cmp/eq  r12, r13
	bt.s    1f
	exts.w  r0, r0
	mov     r13, r0
	add     #-2, r4
	mov     #5, r13
	bra     .Lbp_a_have_r0
	mov     #6, r14
1:	mov.l   r2, @r6
	mov.l   r3, @(4, r6)
	mov.l   r5, @(8, r6)
	mov.l   r1, @(12, r6)
	add     #16, r6
	add     #-1, r8
	mov     #5, r13
	bra     .Lbp_a_have_r0
	mov     #6, r14
.Lbp_a_have_r0_bra:
	bra     .Lbp_a_have_r0
	nop

	/* ---- Unaligned (4k+2) base loop using XTRCT + MOV.L ---- */
	.balign 4
.Lbp_u_loop:
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bt      .Lbp_u_rep1
.Lbp_u_have_r0:
	mov     r0, r12
	swap.b  r0, r1
	shld    r13, r1                     /* r1 = tile_id << 5 */
	and     r11, r1
	tst     #0x08, r0
	bt.s    1f
	add     r7, r1
	add     r9, r1
1:	mov.l   @r1, r2

	tst     #0x04, r0                   /* T = !hflip (preserved below) */
	and     #0xF0, r0
	shld    r14, r0                     /* pal * 1024 (T unchanged) */
	mov     r10, r5
	bf.s    .Lbp_u_hf
	add     r0, r5

	/* Unaligned base !hflip (r6 = 4k+2) */
	extu.b  r2, r0                      /* b3 -> [p6|p7] */
	shll2   r0
	mov.l   @(r0, r5), r1
	shlr8   r2
	mov     r1, r0
	mov.w   r0, @(14, r6)               /* dest[7] = p7 */
	add     #2, r6                      /* r6 = 4k+4 */

	extu.b  r2, r0                      /* b2 -> [p4|p5] */
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	xtrct   r3, r1                      /* [p5|p6] */
	mov.l   r1, @(8, r6)                /* dest[5..6] */

	extu.b  r2, r0                      /* b1 -> [p2|p3] */
	shll2   r0
	mov.l   @(r0, r5), r1
	shlr8   r2
	xtrct   r1, r3                      /* [p3|p4] */
	mov.l   r3, @(4, r6)                /* dest[3..4] */

	extu.b  r2, r0                      /* b0 -> [p0|p1] */
	shll2   r0
	mov.l   @(r0, r5), r3
	xtrct   r3, r1                      /* [p1|p2] */
	mov.l   r1, @r6                     /* dest[1..2] */
	swap.w  r3, r3                      /* low 16 = p0 */
	dt      r8
	mov.w   r3, @-r6                    /* r6 = orig, dest[0] = p0 */
	bf.s    .Lbp_u_loop
	add     #16, r6
	bra     .Lbp_done
	nop

	/* Consecutive identical base tile run (unaligned 4k+2): load once, store in regs */
.Lbp_u_rep1:
	mov     r6, r1
	add     #-16, r1
	mov.w   @r1+, r0                    /* p0 */
	mov.l   @r1+, r2                    /* [p1|p2] */
	mov.l   @r1+, r3                    /* [p3|p4] */
	mov.l   @r1+, r5                    /* [p5|p6] */
	mov.w   @r1, r1                     /* p7 */
	shll16  r0
	extu.w  r1, r1
	or      r0, r1                      /* r1 = [p0|p7] */
.Lbp_u_rep1_loop:
	mov     r1, r0
	mov.w   r0, @(14, r6)               /* dest[7] = p7 */
	swap.w  r1, r0
	mov.w   r0, @r6                     /* dest[0] = p0 */
	add     #2, r6
	mov.l   r2, @r6
	mov.l   r3, @(4, r6)
	dt      r8
	mov.l   r5, @(8, r6)
	bt.s    .Lbp_u_done_bra
	add     #14, r6
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bt      .Lbp_u_rep1_loop
	bra     .Lbp_u_have_r0
	nop

	/* Unaligned base hflip (r6 = 4k+2) */
.Lbp_u_hf:
	extu.b  r2, r0                      /* b3 -> [p6|p7] */
	shll2   r0
	mov.l   @(r0, r5), r1
	shlr8   r2
	mov.w   r1, @r6                     /* dest[0] = p7 */
	add     #2, r6                      /* r6 = 4k+4 */

	extu.b  r2, r0                      /* b2 -> [p4|p5] */
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	xtrct   r3, r1                      /* [p5|p6] */
	swap.w  r1, r1                      /* [p6|p5] */
	mov.l   r1, @r6                     /* dest[1..2] */

	extu.b  r2, r0                      /* b1 -> [p2|p3] */
	shll2   r0
	mov.l   @(r0, r5), r1
	shlr8   r2
	xtrct   r1, r3                      /* [p3|p4] */
	swap.w  r3, r3                      /* [p4|p3] */
	mov.l   r3, @(4, r6)                /* dest[3..4] */

	extu.b  r2, r0                      /* b0 -> [p0|p1] */
	shll2   r0
	mov.l   @(r0, r5), r3
	xtrct   r3, r1                      /* [p1|p2] */
	swap.w  r1, r1                      /* [p2|p1] */
	mov.l   r1, @(8, r6)                /* dest[5..6] */
	swap.w  r3, r0                      /* low 16 = p0 */
	dt      r8
	mov.w   r0, @(12, r6)               /* dest[7] = p0 */
	bt.s    .Lbp_u_done_bra
	add     #14, r6                     /* orig+2+14 = orig+16 */
	bra     .Lbp_u_loop
	nop
.Lbp_u_done_bra:
	bra     .Lbp_done
	nop
	.size   _cgba_render_4bpp_span_base_pair_sh4, .-_cgba_render_4bpp_span_base_pair_sh4


/* ============================================================================
 * void cgba_render_4bpp_plan_over_sh4(
 *   const cgba_mode0_bg_plan *p, // r4
 *   u32 vcount,                  // r5
 *   u16 *dest_ptr,               // r6 (must be 4-byte aligned)
 *   const u32 *pair_lut)         // r7
 *
 * Fast 4-arg Mode-0 aligned overlay entry (p->flags & 1, p->tile_hoff == 0).
 * Checks row_nonzero empty-row skip before saving any callee-saved registers.
 *
 * Also: void cgba_render_4bpp_span_over_pair_sh4 / span2_over_pair_sh4(
 *   const u16 *map_ptr,   // r4
 *   u32 todraw,           // r5 (todraw1 | (todraw2 << 16))
 *   u16 *dest_ptr,        // r6
 *   const u8 *tile_base,  // r7
 *   s32 vflip_off,        // @(0, sp_in)
 *   const u32 *pair_lut,  // @(4, sp_in)
 *   const u16 *paltbl,    // @(8, sp_in)
 *   const u16 *map2)      // @(12, sp_in) (only when todraw2 != 0)
 * ========================================================================== */
	.section .text._cgba_render_4bpp_plan_over_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_4bpp_plan_over_sh4
	.type   _cgba_render_4bpp_plan_over_sh4, @function
_cgba_render_4bpp_plan_over_sh4:
	mov.w   @(24, r4), r0               /* p->vofs */
	add     r5, r0
	mov.w   .Lop_01ff, r2
	and     r0, r2                      /* r2 = voffset (0..511) */
	mov     r2, r3
	shlr8   r3                          /* vhalf (0 or 1) */
	shll2   r3                          /* r3 = vhalf * 4 */
	mov     #30, r0
	mov.b   @(r0, r4), r0               /* p->flags */
	tst     #2, r0                      /* has_zero_row? */
	bt      .Lop_plan_no_skip
	mov     r3, r0
	add     #16, r0
	mov.l   @(r0, r4), r1               /* p->row_nonzero[vhalf] */
	mov     r2, r0
	extu.b  r0, r0                      /* voffset & 255 */
	shlr2   r0
	shlr    r0                          /* vtile_row (0..31) */
	neg     r0, r0                      /* -vtile_row for right shift */
	shld    r0, r1
	shlr    r1
	bt      .Lop_plan_no_skip
	mov     r2, r0
	and     #7, r0
	shll2   r0                          /* vert_pix */
	mov.l   @r4, r1                     /* p->tile_charbase */
	mov.l   @(r0, r1), r1               /* *(u32*)(tile_charbase + vert_pix) */
	tst     r1, r1
	bf      .Lop_plan_no_skip
	rts
	nop

.Lop_plan_no_skip:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	mov     r7, r10                     /* r10 = pair_lut */
	mov     r4, r1                      /* r1  = p */
	mov     r2, r0
	and     #7, r0
	shll2   r0                          /* vert_pix */
	mov.l   @r1, r7                     /* tile_charbase */
	add     r0, r7                      /* r7  = tile_base */
	add     r0, r0                      /* vert_pix << 1 */
	mov     #28, r9
	sub     r0, r9                      /* r9  = vflip_off */

	mov     r3, r0                      /* vhalf * 4 */
	add     #4, r0
	mov.l   @(r0, r1), r3               /* p->sb_ptr[vhalf] */
	mov     r2, r0
	and     #0xF8, r0
	shll2   r0
	add     r0, r0                      /* (voffset & 0xF8) << 3 */
	add     r0, r3                      /* r3  = map_row_b */

	mov.w   @(26, r1), r0               /* first_byte_off */
	mov     r3, r4
	add     r0, r4                      /* r4  = map_ptr */
	mov.w   @(28, r1), r0               /* map2_byte_off */
	add     r3, r0                      /* r0  = map2 */
	lds     r0, mach                    /* mach = map2 */
	mov.l   @(12, r1), r5               /* counts_packed */
	extu.w  r5, r8                      /* r8  = todraw1 */
	shlr16  r5                          /* r5  = todraw2 */
	lds     r5, macl                    /* macl = todraw2 */

	mov.w   .Lop_7fe0, r11              /* r11 = 0x00007FE0 */
	mov.l   .Lop_eeeef, r13             /* r13 = 0xEEEEEEEF */
	mov.l   .Lop_88888, r14             /* r14 = 0x88888888 */
	bra     .Lop_a_loop
	mov     #5, r12                     /* r12 = 5 (tile_id << 5) */
	.size   _cgba_render_4bpp_plan_over_sh4, .-_cgba_render_4bpp_plan_over_sh4

	.balign 4
	.global _cgba_render_4bpp_span_over_pair_sh4
	.global _cgba_render_4bpp_span2_over_pair_sh4
	.type   _cgba_render_4bpp_span_over_pair_sh4, @function
	.type   _cgba_render_4bpp_span2_over_pair_sh4, @function
_cgba_render_4bpp_span2_over_pair_sh4:
_cgba_render_4bpp_span_over_pair_sh4:
	tst     r5, r5
	bf      1f
	rts
	nop
1:	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	extu.w  r5, r8                      /* r8  = todraw1 */
	shlr16  r5                          /* r5  = todraw2 (0 for 1-span callers) */
	lds     r5, macl                    /* macl = todraw2 */
	tst     r5, r5
	bt      1f
	mov.l   @(40, r15), r0              /* r0  = map2 */
	lds     r0, mach                    /* mach = map2 */
1:	mov.l   @(28, r15), r9              /* r9  = vflip_off */
	mov.l   @(32, r15), r10             /* r10 = pair_lut */
	/* @(36, r15) = paltbl (loaded only on rare unaligned partial tiles) */
	mov.w   .Lop_7fe0, r11              /* r11 = 0x00007FE0 */
	mov     #5, r12                     /* r12 = 5 (tile_id << 5) */
	mov.l   .Lop_eeeef, r13             /* r13 = 0xEEEEEEEF */
	mov.l   .Lop_88888, r14             /* r14 = 0x88888888 */

	mov     r6, r0
	tst     #2, r0                      /* T = 4-byte aligned dest_ptr */
	bf      .Lop_to_unaligned

	/* ---- Aligned (4k) overlay loop ---- */
	.balign 4
.Lop_a_loop:
	mov.w   @r4+, r0
.Lop_a_have_r0:
	swap.b  r0, r1
	shld    r12, r1                     /* r1 = tile_id << 5 */
	and     r11, r1
	tst     #0x08, r0
	bt.s    1f
	add     r7, r1
	add     r9, r1
1:	mov.l   @r1, r2
	tst     r2, r2
	bt.s    .Lop_a_empty_run
	mov     r0, r3

	/* Opaque check */
	mov     r13, r0
	not     r2, r1
	add     r2, r0
	and     r1, r0
	tst     r14, r0
	bf.s    .Lop_to_partial
	mov     r3, r0                      /* r0 = raw_map */

	tst     #0x04, r0                   /* T = !hflip (preserved below) */
	and     #0xF0, r0
	shlr2   r0
	shll8   r0
	mov     r10, r5
	bf.s    .Lop_a_hf
	add     r0, r5                      /* r5 = &pair_lut[pal][0] */

	/* Aligned opaque !hflip */
	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	mov.l   r3, @(12, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	mov.l   r3, @(8, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	mov.l   r3, @(4, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	dt      r8
	mov.l   r3, @r6
	bf.s    .Lop_a_loop
	add     #16, r6
	bra     .Lop_done
	nop

	/* Empty-run skip (aligned) */
.Lop_a_empty_run:
	dt      r8
	bt.s    .Lop_done
	add     #16, r6
	mov.w   @r4+, r0
	cmp/eq  r3, r0
	bf      .Lop_a_have_r0
	bra     .Lop_a_empty_pair_entry
	nop

.Lop_done:
	sts     macl, r8                    /* r8 = todraw2 */
	tst     r8, r8
	bf      .Lop_span2
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lop_to_unaligned:
	bra     .Lop_u_loop
	nop

.Lop_to_partial:
	bra     .Lop_part_a
	mov     r10, r5

.Lop_span2:
	sts     mach, r4                    /* r4 = map2 */
	mov     #0, r0
	lds     r0, macl                    /* todraw2 = 0 */
	mov     r6, r0
	tst     #2, r0
	bf      .Lop_to_unaligned
	bra     .Lop_a_loop
	nop

	.balign 4
.Lop_eeeef:
	.long   0xEEEEEEEF
.Lop_88888:
	.long   0x88888888
.Lop_7fe0:
	.word   0x7FE0
.Lop_01ff:
	.word   0x01FF
	.balign 4

	/* Aligned opaque hflip */
.Lop_a_hf:
	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	swap.w  r3, r3
	mov.l   r3, @r6

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	swap.w  r3, r3
	mov.l   r3, @(4, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	swap.w  r3, r3
	mov.l   r3, @(8, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	dt      r8
	swap.w  r3, r3
	mov.l   r3, @(12, r6)
	bt.s    .Lop_a_done_bra
	add     #16, r6
	bra     .Lop_a_loop
	nop
.Lop_a_done_bra:
	bra     .Lop_done
	nop

	/* Out-of-line 4-tile-at-a-time aligned empty-run skip (keeps hot branches short) */
	.balign 4
.Lop_a_empty_pair_entry:
	add     #16, r6
	dt      r8
	bt      .Lop_a_done_bra
	mov     r4, r0
	tst     #2, r0
	bt      .Lop_a_empty_aligned
	mov.w   @r4+, r0
	cmp/eq  r3, r0
	bf      .Lop_a_have_r0_bra
	add     #16, r6
	dt      r8
	bt      .Lop_a_done_bra
.Lop_a_empty_aligned:
	mov     r8, r5
	shlr    r5                          /* r5 = number of 2-tile pairs */
	tst     r5, r5
	bt      .Lop_a_empty_tail1
	extu.w  r3, r2
	swap.w  r2, r1
	or      r1, r2                      /* r2 = [r3 | r3] */
	shlr    r5                          /* r5 = number of 4-tile quads */
	tst     r5, r5
	bt      .Lop_a_empty_tail2
.Lop_a_empty_quad:
	mov.l   @r4+, r0
	cmp/eq  r2, r0
	bf      .Lop_a_empty_qmiss0
	mov.l   @r4+, r0
	cmp/eq  r2, r0
	bf      .Lop_a_empty_qmiss1
	dt      r5
	bf.s    .Lop_a_empty_quad
	add     #64, r6
.Lop_a_empty_tail2:
	mov     r8, r0
	tst     #2, r0
	bt      .Lop_a_empty_tail1
	mov.l   @r4+, r0
	cmp/eq  r2, r0
	bf      .Lop_a_empty_t2miss
	add     #32, r6
.Lop_a_empty_tail1:
	mov     r8, r0
	tst     #1, r0
	bt      .Lop_a_done_bra
	mov.w   @r4+, r0
	cmp/eq  r3, r0
	bf      1f
	bra     .Lop_done
	add     #16, r6
1:	bra     .Lop_a_have_r0
	mov     #1, r8
.Lop_a_empty_qmiss1:
	add     #32, r6
	shll2   r5
	bra     1f
	add     #-2, r5
.Lop_a_empty_t2miss:
	bra     1f
	mov     #0, r5
.Lop_a_empty_qmiss0:
	shll2   r5
1:	mov     #3, r1
	and     r8, r1
	mov     r5, r8
	add     r1, r8
	swap.w  r0, r1
	exts.w  r1, r1
	cmp/eq  r3, r1
	bt.s    2f
	exts.w  r0, r0
	add     #-2, r4
	bra     .Lop_a_have_r0
	mov     r1, r0
2:	add     #16, r6
	bra     .Lop_a_have_r0
	add     #-1, r8
.Lop_a_have_r0_bra:
	bra     .Lop_a_have_r0
	nop

	/* ---- Unaligned (4k+2) overlay loop ---- */
	.balign 4
.Lop_u_loop:
	mov.w   @r4+, r0
.Lop_u_have_r0:
	swap.b  r0, r1
	shld    r12, r1                     /* r1 = tile_id << 5 */
	and     r11, r1
	tst     #0x08, r0
	bt.s    1f
	add     r7, r1
	add     r9, r1
1:	mov.l   @r1, r2
	tst     r2, r2
	bt.s    .Lop_u_empty_run
	mov     r0, r3

	/* Opaque check */
	mov     r13, r0
	not     r2, r1
	add     r2, r0
	and     r1, r0
	tst     r14, r0
	bf.s    .Lop_partial_u_tramp
	mov     r3, r0                      /* r0 = raw_map */

	tst     #0x04, r0                   /* T = !hflip (preserved below) */
	and     #0xF0, r0
	shlr2   r0
	shll8   r0
	mov     r10, r5
	bf.s    .Lop_u_hf
	add     r0, r5

	/* Unaligned opaque !hflip */
	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r1
	shlr8   r2
	mov     r1, r0
	mov.w   r0, @(14, r6)
	add     #2, r6

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	xtrct   r3, r1
	mov.l   r1, @(8, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r1
	shlr8   r2
	xtrct   r1, r3
	mov.l   r3, @(4, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	xtrct   r3, r1
	mov.l   r1, @r6
	swap.w  r3, r3
	dt      r8
	mov.w   r3, @-r6
	bf.s    .Lop_u_loop
	add     #16, r6
	bra     .Lop_done
	nop

	/* Empty-run skip (unaligned) */
.Lop_u_empty_run:
	dt      r8
	bt.s    .Lop_u_done_bra
	add     #16, r6
	mov.w   @r4+, r0
	cmp/eq  r3, r0
	bf      .Lop_u_have_r0
.Lop_u_empty_loop:
	dt      r8
	bt.s    .Lop_u_done_bra
	add     #16, r6
	mov.w   @r4+, r0
	cmp/eq  r3, r0
	bt      .Lop_u_empty_loop
	bra     .Lop_u_have_r0
	nop

.Lop_partial_u_tramp:
	bra     .Lop_partial_u
	nop

	/* Unaligned opaque hflip */
.Lop_u_hf:
	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r1
	shlr8   r2
	mov.w   r1, @r6
	add     #2, r6

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	xtrct   r3, r1
	swap.w  r1, r1
	mov.l   r1, @r6

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r1
	shlr8   r2
	xtrct   r1, r3
	swap.w  r3, r3
	mov.l   r3, @(4, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	xtrct   r3, r1
	swap.w  r1, r1
	mov.l   r1, @(8, r6)
	swap.w  r3, r0
	dt      r8
	mov.w   r0, @(12, r6)
	bt.s    .Lop_u_done_bra
	add     #14, r6
	bra     .Lop_u_loop
	nop
.Lop_u_done_bra:
	bra     .Lop_done
	nop

	/* Aligned (4k) partial row: pair-LUT 2-pixel-at-a-time fast path!
	 * In: r0 = r3 = raw_map, r2 = raw, r5 = r10 = pair_lut. */
.Lop_part_a:
	tst     #0x04, r0                   /* T = !hflip (preserved below) */
	and     #0xF0, r0
	shlr2   r0
	shll8   r0
	bf.s    .Lop_part_a_hf
	add     r0, r5                      /* r5 = &pair_lut[subpal][0] */

	/* Aligned partial !hflip: bytes b0..b3 -> offsets 12, 8, 4, 0 */
	extu.b  r2, r0
	tst     #0x0F, r0
	shlr8   r2                          /* T unchanged */
	bt      2f
	tst     #0xF0, r0
	bt.s    3f
	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.l   r0, @(12, r6)
2:	tst     r0, r0
	bt      1f
	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.w   r0, @(14, r6)
3:	mov.w   @(r0, r5), r0
	mov.w   r0, @(12, r6)
1:
	extu.b  r2, r0
	tst     #0x0F, r0
	shlr8   r2
	bt      2f
	tst     #0xF0, r0
	bt.s    3f
	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.l   r0, @(8, r6)
2:	tst     r0, r0
	bt      1f
	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.w   r0, @(10, r6)
3:	mov.w   @(r0, r5), r0
	mov.w   r0, @(8, r6)
1:
	extu.b  r2, r0
	tst     #0x0F, r0
	shlr8   r2
	bt      2f
	tst     #0xF0, r0
	bt.s    3f
	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.l   r0, @(4, r6)
2:	tst     r0, r0
	bt      1f
	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.w   r0, @(6, r6)
3:	mov.w   @(r0, r5), r0
	mov.w   r0, @(4, r6)
1:
	extu.b  r2, r0
	tst     #0x0F, r0
	bt      2f
	tst     #0xF0, r0
	bt.s    3f
	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.l   r0, @r6
2:	tst     r0, r0
	bt      1f
	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.w   r0, @(2, r6)
3:	mov.w   @(r0, r5), r0
	mov.w   r0, @r6
1:
	dt      r8
	bt.s    .Lop_part_a_done
	add     #16, r6
	bra     .Lop_a_loop
	nop
.Lop_part_a_done:
	bra     .Lop_done
	nop

	/* Aligned partial hflip: bytes b0..b3 -> offsets 0, 4, 8, 12 */
.Lop_part_a_hf:
	extu.b  r2, r0
	tst     #0x0F, r0
	shlr8   r2
	bt      2f
	tst     #0xF0, r0
	bt.s    3f
	shll2   r0
	mov.l   @(r0, r5), r0
	swap.w  r0, r0
	bra     1f
	mov.l   r0, @r6
2:	tst     r0, r0
	bt      1f
	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.w   r0, @r6
3:	mov.w   @(r0, r5), r0
	mov.w   r0, @(2, r6)
1:
	extu.b  r2, r0
	tst     #0x0F, r0
	shlr8   r2
	bt      2f
	tst     #0xF0, r0
	bt.s    3f
	shll2   r0
	mov.l   @(r0, r5), r0
	swap.w  r0, r0
	bra     1f
	mov.l   r0, @(4, r6)
2:	tst     r0, r0
	bt      1f
	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.w   r0, @(4, r6)
3:	mov.w   @(r0, r5), r0
	mov.w   r0, @(6, r6)
1:
	extu.b  r2, r0
	tst     #0x0F, r0
	shlr8   r2
	bt      2f
	tst     #0xF0, r0
	bt.s    3f
	shll2   r0
	mov.l   @(r0, r5), r0
	swap.w  r0, r0
	bra     1f
	mov.l   r0, @(8, r6)
2:	tst     r0, r0
	bt      1f
	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.w   r0, @(8, r6)
3:	mov.w   @(r0, r5), r0
	mov.w   r0, @(10, r6)
1:
	extu.b  r2, r0
	tst     #0x0F, r0
	bt      2f
	tst     #0xF0, r0
	bt.s    3f
	shll2   r0
	mov.l   @(r0, r5), r0
	swap.w  r0, r0
	bra     1f
	mov.l   r0, @(12, r6)
2:	tst     r0, r0
	bt      1f
	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.w   r0, @(12, r6)
3:	mov.w   @(r0, r5), r0
	mov.w   r0, @(14, r6)
1:
	dt      r8
	bt.s    .Lop_part_a_done
	add     #16, r6
	bra     .Lop_a_loop
	nop

	/* Unaligned (4k+2) partial row */
.Lop_partial_u:
	mov.l   @(36, r15), r5              /* r5 = paltbl */
	and     #0xF0, r0
	add     r0, r0
	add     r0, r5                      /* r5 = subpal */
	mov     r2, r1
	shlr2   r1
	mov     r3, r0                      /* r0 = raw_map */
	shlr    r1                          /* clobbers T; precedes tst */
	tst     #0x04, r0                   /* T = !hflip */
	bf.s    .Lop_part_hf
	add     r2, r2

	/* Partial !hflip */
	mov     r2, r0
	tst     #0x1e, r0
	bt.s    1f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(12, r6)
1:	mov     r2, r0
	tst     #0x1e, r0
	bt.s    2f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(8, r6)
2:	mov     r2, r0
	tst     #0x1e, r0
	bt.s    3f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(4, r6)
3:	mov     r2, r0
	tst     #0x1e, r0
	bt      4f
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @r6
4:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    5f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(14, r6)
5:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    6f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(10, r6)
6:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    7f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(6, r6)
7:	mov     r1, r0
	tst     #0x1e, r0
	bt      8f
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(2, r6)
8:	bra     .Lop_part_ret
	nop

	/* Partial hflip */
.Lop_part_hf:
	mov     r2, r0
	tst     #0x1e, r0
	bt.s    1f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(2, r6)
1:	mov     r2, r0
	tst     #0x1e, r0
	bt.s    2f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(6, r6)
2:	mov     r2, r0
	tst     #0x1e, r0
	bt.s    3f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(10, r6)
3:	mov     r2, r0
	tst     #0x1e, r0
	bt      4f
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(14, r6)
4:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    5f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @r6
5:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    6f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(4, r6)
6:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    7f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(8, r6)
7:	mov     r1, r0
	tst     #0x1e, r0
	bt      8f
	and     #0x1e, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @(12, r6)
8:
.Lop_part_ret:
	dt      r8
	bt.s    .Lop_part_done
	add     #16, r6
	bra     .Lop_u_loop
	nop
.Lop_part_done:
	bra     .Lop_done
	nop
	.size   _cgba_render_4bpp_span_over_pair_sh4, .-_cgba_render_4bpp_span_over_pair_sh4

/* ============================================================================
 * 4bpp FULLCOLOR OBJ full-tile row renderers (non-flipped & hflipped).
 *
 * void cgba_render_obj_4bpp_tiles_sh4(
 *   const u8 *tile_ptr,   // r4 (&vram[0x10000 + off], 4-aligned, non-wrapping)
 *   u32 num_tiles,        // r5 (> 0)
 *   u16 *dst_ptr,         // r6
 *   const u16 *subpal)    // r7 (&palette_ram_converted[0x100 + pal], 32 bytes)
 *
 * Zero callee-saved registers, zero stack traffic, and 32-byte subpal stays
 * pinned in a single D-cache line.
 * ========================================================================== */
/* Shared 8-pixel 4bpp OBJ tile row unpacker (hf=0 normal, hf=1 hflip).
 * Expects r4=tile_row_ptr, r6=dst, r7=subpal; skips transparent pixels (0)
 * and branches to \next_lbl when the entire 8-pixel word is zero or done. */
.macro CGBA_OBJ_4BPP_ROW hf, next_lbl
	mov.l   @r4, r2
	tst     r2, r2
	bt      \next_lbl
	mov     r2, r1
	shlr2   r1
	shlr    r1
	add     r2, r2

	mov     r2, r0
	tst     #0x1e, r0
	bt.s    1f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	.if \hf
	mov.w   r0, @(2, r6)
	.else
	mov.w   r0, @(12, r6)
	.endif
1:	mov     r2, r0
	tst     #0x1e, r0
	bt.s    2f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	.if \hf
	mov.w   r0, @(6, r6)
	.else
	mov.w   r0, @(8, r6)
	.endif
2:	mov     r2, r0
	tst     #0x1e, r0
	bt.s    3f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	.if \hf
	mov.w   r0, @(10, r6)
	.else
	mov.w   r0, @(4, r6)
	.endif
3:	mov     r2, r0
	tst     #0x1e, r0
	bt      4f
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	.if \hf
	mov.w   r0, @(14, r6)
	.else
	mov.w   r0, @r6
	.endif
4:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    5f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	.if \hf
	mov.w   r0, @r6
	.else
	mov.w   r0, @(14, r6)
	.endif
5:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    6f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	.if \hf
	mov.w   r0, @(4, r6)
	.else
	mov.w   r0, @(10, r6)
	.endif
6:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    7f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	.if \hf
	mov.w   r0, @(8, r6)
	.else
	mov.w   r0, @(6, r6)
	.endif
7:	mov     r1, r0
	tst     #0x1e, r0
	bt      \next_lbl
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	.if \hf
	mov.w   r0, @(12, r6)
	.else
	mov.w   r0, @(2, r6)
	.endif
.endm

	.section .text._cgba_render_obj_4bpp_tiles_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_obj_4bpp_tiles_sh4
	.type   _cgba_render_obj_4bpp_tiles_sh4, @function
_cgba_render_obj_4bpp_tiles_sh4:
.Lobj_loop:
	CGBA_OBJ_4BPP_ROW 0, .Lobj_next
.Lobj_next:
	add     #32, r4
	dt      r5
	bf.s    .Lobj_loop
	add     #16, r6
	rts
	nop
	.size   _cgba_render_obj_4bpp_tiles_sh4, .-_cgba_render_obj_4bpp_tiles_sh4

	.section .text._cgba_render_obj_4bpp_tiles_hf_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_obj_4bpp_tiles_hf_sh4
	.type   _cgba_render_obj_4bpp_tiles_hf_sh4, @function
_cgba_render_obj_4bpp_tiles_hf_sh4:
.Lobj_hf_loop:
	CGBA_OBJ_4BPP_ROW 1, .Lobj_hf_next
.Lobj_hf_next:
	add     #-32, r4
	dt      r5
	bf.s    .Lobj_hf_loop
	add     #16, r6
	rts
	nop
	.size   _cgba_render_obj_4bpp_tiles_hf_sh4, .-_cgba_render_obj_4bpp_tiles_hf_sh4

/* ============================================================================
 * Multi-row 4bpp FULLCOLOR OBJ tile strip renderers (1..8 rows in one tile row).
 *   void cgba_render_obj_4bpp_tiles_strip[_hf]_sh4(
 *     const u8 *tile_ptr,    // r4: top (or bottom if vflip) tile row
 *     u32 k_and_ntiles,      // r5: (k << 16) | ntiles  (k>=1, ntiles>=1)
 *     u16 *dst_top,          // r6: top-left pixel
 *     const u16 *subpal,     // r7: palptr + pal_off
 *     s32 row_step)          // @(0, sp_in): +4 (!vflip) or -4 (vflip)
 * ============================================================================ */
.macro CGBA_OBJ_4BPP_TILES_STRIP_KERNEL sym, pfx, hflip
	.section .text.\sym,"ax",@progbits
	.balign 4
	.global \sym
	.type   \sym, @function
\sym:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15

	mov.l   @(20, r15), r11             /* r11 = row_step (+4 or -4) */
	extu.w  r5, r12                     /* r12 = ntiles */
	shlr16  r5
	mov     r5, r9                      /* r9  = k rows */
	mov     r4, r8                      /* r8  = cur_row_tile_ptr */
	mov.w   \pfx\()_480, r10
	mov     r12, r0
	shll2   r0
	shll2   r0                          /* ntiles * 16 */
	sub     r0, r10                     /* r10 = 480 - ntiles * 16 */

	.balign 4
\pfx\()_row:
	mov     r8, r4
	mov     r12, r5
\pfx\()_loop:
	CGBA_OBJ_4BPP_ROW \hflip, \pfx\()_next
\pfx\()_next:
.if \hflip
	add     #-32, r4
.else
	add     #32, r4
.endif
	dt      r5
	bf.s    \pfx\()_loop
	add     #16, r6

	add     r11, r8
	dt      r9
	bf.s    \pfx\()_row
	add     r10, r6

	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	.balign 2
\pfx\()_480:
	.word   480
	.size   \sym, .-\sym
.endm

	CGBA_OBJ_4BPP_TILES_STRIP_KERNEL _cgba_render_obj_4bpp_tiles_strip_sh4,    .Lobjs,  0
	CGBA_OBJ_4BPP_TILES_STRIP_KERNEL _cgba_render_obj_4bpp_tiles_strip_hf_sh4, .Lobjsh, 1
	.purgem CGBA_OBJ_4BPP_TILES_STRIP_KERNEL

/* ============================================================================
 * Fast 32-byte-multiple strip copy for 4-byte-aligned non-overlapping buffers.
 *   void cgba_copy_32b_sh4(void *dst, const void *src, uint32_t nbytes)
 *     r4 = dst, r5 = src, r6 = nbytes (multiple of 32).
 * 64-byte unrolled dt loop (17.5 insns / 32 B vs newlib memcpy's 21 insns / 32 B)
 * with zero callee-saved register spills and grouped 4-word bursts.
 * ========================================================================== */
	.section .text._cgba_copy_32b_sh4,"ax",@progbits
	.balign 4
	.global _cgba_copy_32b_sh4
	.type   _cgba_copy_32b_sh4, @function
_cgba_copy_32b_sh4:
	mov     r6, r7
	shlr2   r6
	shlr2   r6
	shlr2   r6                          /* r6 = nbytes / 64 */
	tst     r6, r6
	bt      .Lcp32_tail
	.balign 4
.Lcp64_loop:
	mov.l   @r5+, r0
	mov.l   @r5+, r1
	mov.l   @r5+, r2
	mov.l   @r5+, r3
	mov.l   r0, @r4
	mov.l   r1, @(4, r4)
	mov.l   r2, @(8, r4)
	mov.l   r3, @(12, r4)
	mov.l   @r5+, r0
	mov.l   @r5+, r1
	mov.l   @r5+, r2
	mov.l   @r5+, r3
	mov.l   r0, @(16, r4)
	mov.l   r1, @(20, r4)
	mov.l   r2, @(24, r4)
	mov.l   r3, @(28, r4)
	mov.l   @r5+, r0
	mov.l   @r5+, r1
	mov.l   @r5+, r2
	mov.l   @r5+, r3
	mov.l   r0, @(32, r4)
	mov.l   r1, @(36, r4)
	mov.l   r2, @(40, r4)
	mov.l   r3, @(44, r4)
	mov.l   @r5+, r0
	mov.l   @r5+, r1
	mov.l   @r5+, r2
	mov.l   @r5+, r3
	mov.l   r0, @(48, r4)
	mov.l   r1, @(52, r4)
	mov.l   r2, @(56, r4)
	mov.l   r3, @(60, r4)
	dt      r6
	bf.s    .Lcp64_loop
	add     #64, r4
.Lcp32_tail:
	mov     r7, r0
	tst     #32, r0
	bt      .Lcp32_done
	mov.l   @r5+, r0
	mov.l   @r5+, r1
	mov.l   @r5+, r2
	mov.l   @r5+, r3
	mov.l   r0, @r4
	mov.l   r1, @(4, r4)
	mov.l   r2, @(8, r4)
	mov.l   r3, @(12, r4)
	mov.l   @r5+, r0
	mov.l   @r5+, r1
	mov.l   @r5+, r2
	mov.l   @r5+, r3
	mov.l   r0, @(16, r4)
	mov.l   r1, @(20, r4)
	mov.l   r2, @(24, r4)
	mov.l   r3, @(28, r4)
.Lcp32_done:
	rts
	nop
	.size   _cgba_copy_32b_sh4, .-_cgba_copy_32b_sh4

/* ============================================================================
 * Cache-allocating (movca.l) bulk copy for non-overlapping 4-byte-aligned
 * DMA / CpuFastSet transfers.
 *   void cgba_dma_copy_movca_sh4(void *dst, const void *src, uint32_t nbytes)
 *     r4 = dst (4B aligned), r5 = src (4B aligned), r6 = nbytes.
 * Aligns dst up to a 32-byte cache-line boundary, then streams 64-byte /
 * 32-byte full cache lines using movca.l to eliminate SDRAM read-for-ownership
 * misses on the destination buffer, then copies any remaining tail words/bytes.
 * Zero callee-saved register spills.
 * ========================================================================== */
	.section .text._cgba_dma_copy_movca_sh4,"ax",@progbits
	.balign 4
	.global _cgba_dma_copy_movca_sh4
	.type   _cgba_dma_copy_movca_sh4, @function
_cgba_dma_copy_movca_sh4:
	mov     r4, r0
	and     #31, r0
	tst     r0, r0
	bt      .Lmc_aligned
	mov     #32, r1
	sub     r0, r1                      /* r1 = bytes to 32B boundary (4..28) */
	cmp/hs  r1, r6                      /* T = (nbytes >= head) */
	bf      .Lmc_tail_words
	sub     r1, r6
	shlr2   r1                          /* r1 = head words (1..7) */
	.balign 4
.Lmc_head_loop:
	mov.l   @r5+, r0
	mov.l   r0, @r4
	dt      r1
	bf.s    .Lmc_head_loop
	add     #4, r4
.Lmc_aligned:
	mov     r6, r7
	shlr2   r7
	shlr2   r7
	shlr2   r7                          /* r7 = remaining / 64 */
	tst     r7, r7
	bt      .Lmc_rem32
	.balign 4
.Lmc64_loop:
	mov.l   @r5+, r0
	mov.l   @r5+, r1
	mov.l   @r5+, r2
	mov.l   @r5+, r3
	movca.l r0, @r4
	mov.l   r1, @(4, r4)
	mov.l   r2, @(8, r4)
	mov.l   r3, @(12, r4)
	mov.l   @r5+, r0
	mov.l   @r5+, r1
	mov.l   @r5+, r2
	mov.l   @r5+, r3
	mov.l   r0, @(16, r4)
	mov.l   r1, @(20, r4)
	mov.l   r2, @(24, r4)
	mov.l   r3, @(28, r4)
	add     #32, r4
	mov.l   @r5+, r0
	mov.l   @r5+, r1
	mov.l   @r5+, r2
	mov.l   @r5+, r3
	movca.l r0, @r4
	mov.l   r1, @(4, r4)
	mov.l   r2, @(8, r4)
	mov.l   r3, @(12, r4)
	mov.l   @r5+, r0
	mov.l   @r5+, r1
	mov.l   @r5+, r2
	mov.l   @r5+, r3
	mov.l   r0, @(16, r4)
	mov.l   r1, @(20, r4)
	mov.l   r2, @(24, r4)
	mov.l   r3, @(28, r4)
	dt      r7
	bf.s    .Lmc64_loop
	add     #32, r4
.Lmc_rem32:
	mov     r6, r0
	tst     #32, r0
	bt      .Lmc_tail_words
	mov.l   @r5+, r0
	mov.l   @r5+, r1
	mov.l   @r5+, r2
	mov.l   @r5+, r3
	movca.l r0, @r4
	mov.l   r1, @(4, r4)
	mov.l   r2, @(8, r4)
	mov.l   r3, @(12, r4)
	mov.l   @r5+, r0
	mov.l   @r5+, r1
	mov.l   @r5+, r2
	mov.l   @r5+, r3
	mov.l   r0, @(16, r4)
	mov.l   r1, @(20, r4)
	mov.l   r2, @(24, r4)
	mov.l   r3, @(28, r4)
	add     #32, r4
.Lmc_tail_words:
	mov     #31, r0
	and     r0, r6                      /* r6 = remaining tail bytes (0..31) */
	mov     r6, r1
	shlr2   r1
	tst     r1, r1
	bt      .Lmc_tail_bytes
	.balign 4
.Lmc_tw_loop:
	mov.l   @r5+, r0
	mov.l   r0, @r4
	dt      r1
	bf.s    .Lmc_tw_loop
	add     #4, r4
.Lmc_tail_bytes:
	mov     #3, r0
	and     r0, r6
	tst     r6, r6
	bt      .Lmc_done
	.balign 4
.Lmc_tb_loop:
	mov.b   @r5+, r0
	mov.b   r0, @r4
	dt      r6
	bf.s    .Lmc_tb_loop
	add     #1, r4
.Lmc_done:
	rts
	nop
	.size   _cgba_dma_copy_movca_sh4, .-_cgba_dma_copy_movca_sh4

/* ============================================================================
 * Cache-allocating (movca.l) 32-bit word fill for 4-byte-aligned buffers.
 *   void cgba_dma_fill32_movca_sh4(void *dst, uint32_t val, uint32_t nbytes)
 *     r4 = dst (4B aligned), r5 = raw 32-bit pattern, r6 = nbytes (4B multiple).
 * ========================================================================== */
	.section .text._cgba_dma_fill32_movca_sh4,"ax",@progbits
	.balign 4
	.global _cgba_dma_fill32_movca_sh4
	.type   _cgba_dma_fill32_movca_sh4, @function
_cgba_dma_fill32_movca_sh4:
	mov     r4, r0
	and     #31, r0
	tst     r0, r0
	bt      .Lmf_aligned
	mov     #32, r1
	sub     r0, r1
	cmp/hs  r1, r6
	bf      .Lmf_tail_words
	sub     r1, r6
	shlr2   r1
	.balign 4
.Lmf_head_loop:
	mov.l   r5, @r4
	dt      r1
	bf.s    .Lmf_head_loop
	add     #4, r4
.Lmf_aligned:
	mov     r6, r7
	shlr2   r7
	shlr2   r7
	shlr    r7                          /* r7 = remaining / 32 */
	tst     r7, r7
	bt      .Lmf_tail_words
	mov     r5, r0
	.balign 4
.Lmf32_loop:
	movca.l r0, @r4
	mov.l   r0, @(4, r4)
	mov.l   r0, @(8, r4)
	mov.l   r0, @(12, r4)
	mov.l   r0, @(16, r4)
	mov.l   r0, @(20, r4)
	mov.l   r0, @(24, r4)
	mov.l   r0, @(28, r4)
	dt      r7
	bf.s    .Lmf32_loop
	add     #32, r4
.Lmf_tail_words:
	mov     #31, r0
	and     r0, r6
	shlr2   r6
	tst     r6, r6
	bt      .Lmf_done
	.balign 4
.Lmf_tw_loop:
	mov.l   r5, @r4
	dt      r6
	bf.s    .Lmf_tw_loop
	add     #4, r4
.Lmf_done:
	rts
	nop
	.size   _cgba_dma_fill32_movca_sh4, .-_cgba_dma_fill32_movca_sh4

/* ============================================================================
 * Tile-major k-row 4bpp Mode-0 strip renderers (1 <= k <= 8, start_r+k <= 8,
 * pitch = 240 u16 = 480 bytes, 4-byte aligned dst_top).
 *
 * void cgba_render_4bpp_strip_base_sh4(
 *   const u16 *map1,          // r4
 *   u32 counts_packed,        // r5: todraw1 | (todraw2 << 16), todraw1 >= 1
 *   u16 *dst_top,             // r6 (4-byte aligned)
 *   const u8 *tile_charbase,  // r7 (32-byte aligned)
 *   u32 start_r_and_k,        // @(0, sp_in): (start_r << 8) | k
 *   const u32 *pair_lut,      // @(4, sp_in)
 *   const u16 *map2)          // @(8, sp_in) (only when todraw2 != 0)
 * ========================================================================== */
	.section .text._cgba_render_4bpp_strip_base_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_4bpp_strip_base_sh4
	.type   _cgba_render_4bpp_strip_base_sh4, @function
_cgba_render_4bpp_strip_base_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	extu.w  r5, r8                      /* r8  = todraw1 */
	shlr16  r5                          /* r5  = todraw2 */
	lds     r5, mach                    /* mach = todraw2 */
	tst     r5, r5
	bt      1f
	mov.l   @(36, r15), r0              /* map2 */
	lds     r0, macl                    /* macl = map2 */
1:	mov.l   @(32, r15), r10             /* r10 = pair_lut */
	mov.l   @(28, r15), r0              /* (start_r << 8) | k */
	extu.b  r0, r13                     /* r13 = k (1..8) */
	shlr8   r0
	shll2   r0                          /* r0  = start_r * 4 */
	add     r0, r7                      /* r7  = tile_charbase + start_r*4 */
	add     r0, r0
	mov     #28, r1
	sub     r0, r1
	mov     #4, r0
	mov.l   r0, @-r15                   /* @(4, r15) = row_step (+4 or -4) */
	mov.l   r1, @-r15                   /* @(0, r15) = vflip_off = 28 - 2*start_r*4 */

	mov.w   .Lsb_480, r11               /* r11 = 480 (row pitch in bytes) */
	mov     r13, r0
	shll8   r0
	add     r0, r0                      /* k * 512 */
	mov     r13, r1
	shll2   r1
	shll2   r1
	add     r1, r1                      /* k * 32 */
	sub     r1, r0                      /* k * 480 */
	mov     #16, r14
	sub     r0, r14                     /* r14 = 16 - k * 480 */
	mov.l   .Lsb_10000, r12             /* r12 = sentinel prev1 */

	.balign 4
.Lsb_tile:
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bt      .Lsb_rep1
	mov     r0, r12
	swap.b  r0, r1
	mov     #5, r2
	shld    r2, r1
	mov.w   .Lsb_7fe0, r2
	and     r2, r1                      /* r1 = tile_id << 5 */
	add     r7, r1                      /* r1 = tile_ptr (top row) */
	tst     #0x08, r0                   /* T = !vflip */
	mov     #4, r2
	bt      1f
	mov.l   @(0, r15), r2               /* vflip_off */
	add     r2, r1
	mov     #-4, r2
1:	mov.l   r2, @(4, r15)               /* row_step = +4 or -4 */

	tst     #0x04, r0                   /* T = !hflip */
	and     #0xF0, r0
	shlr2   r0
	shll8   r0
	mov     r10, r5
	bf.s    .Lsb_to_hf
	add     r0, r5                      /* r5 = &pair_lut[pal][0] */

	mov     r13, r9                     /* r9 = k rows */
.Lsb_nhf_row:
	mov.l   @r1, r2
	mov.l   @(4, r15), r0
	add     r0, r1
	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	mov.l   r3, @(12, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	mov.l   r3, @(8, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	mov.l   r3, @(4, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	dt      r9
	mov.l   r3, @r6
	bf.s    .Lsb_nhf_row
	add     r11, r6

	dt      r8
	bf.s    .Lsb_tile
	add     r14, r6

.Lsb_done:
	sts     mach, r8
	tst     r8, r8
	bf      .Lsb_span2
	add     #8, r15                     /* pop vflip_off + row_step */
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lsb_span2:
	sts     macl, r4
	mov     #0, r0
	bra     .Lsb_tile
	lds     r0, mach

.Lsb_to_hf:
	bra     .Lsb_hf
	nop

	.balign 4
.Lsb_10000:
	.long   0x00010000
.Lsb_480:
	.word   480
.Lsb_7fe0:
	.word   0x7FE0
	.balign 4

	/* Identical consecutive tile(s) across all k rows:
	 * for runs of m >= 2 tiles, load each row once from left neighbor and
	 * fill all m tiles horizontally (2-tile unrolled inner loop!). */
.Lsb_rep1:
	mov     #1, r0
	cmp/eq  r0, r8
	bt      .Lsb_rep1_single
	mov.w   @r4, r0
	cmp/eq  r12, r0
	bf      .Lsb_rep1_single

	/* Count consecutive matching tiles m (2 <= m <= r8) */
	mov     #1, r2
	mov     r8, r3
	add     #-1, r3
.Lsb_rep_cnt:
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bf      .Lsb_rep_cnt_end
	add     #1, r2
	dt      r3
	bf      .Lsb_rep_cnt
	bra     .Lsb_rep_run_go
	nop
.Lsb_rep_cnt_end:
	add     #-2, r4
.Lsb_rep_run_go:
	sub     r2, r8                      /* r8 = remaining tiles after run */
	mov.l   r4, @-r15                   /* save map_ptr */
	mov.l   r7, @-r15                   /* save tile_charbase */
	mov     r2, r7                      /* r7 = m (>= 2) */
	mov     r13, r9                     /* r9 = k rows */
.Lsb_rep_row:
	mov     r6, r1
	add     #-16, r1
	mov.l   @r1+, r2
	mov.l   @r1+, r3
	mov.l   @r1+, r5
	mov.l   @r1+, r0                    /* r1 == r6 */
	mov     r7, r4
	shlr    r4                          /* r4 = pairs (m >> 1 >= 1) */
.Lsb_rep_pair:
	mov.l   r2, @r1
	mov.l   r3, @(4, r1)
	mov.l   r5, @(8, r1)
	mov.l   r0, @(12, r1)
	mov.l   r2, @(16, r1)
	mov.l   r3, @(20, r1)
	mov.l   r5, @(24, r1)
	mov.l   r0, @(28, r1)
	dt      r4
	bf.s    .Lsb_rep_pair
	add     #32, r1
	mov     r7, r4
	shlr    r4                          /* T = m & 1 */
	bf      1f
	mov.l   r2, @r1
	mov.l   r3, @(4, r1)
	mov.l   r5, @(8, r1)
	mov.l   r0, @(12, r1)
1:	dt      r9
	bf.s    .Lsb_rep_row
	add     r11, r6
	add     r14, r6                     /* dst_top + (tx + 1) * 16 */
	add     #-1, r7                     /* m - 1 */
	shll2   r7
	shll2   r7                          /* (m - 1) * 16 */
	add     r7, r6                      /* dst_top + (tx + m) * 16 */
	mov.l   @r15+, r7
	mov.l   @r15+, r4
	tst     r8, r8
	bt      .Lsb_done_bra
	bra     .Lsb_tile
	nop

.Lsb_rep1_single:
	mov     r13, r9
	mov     r6, r1
	add     #-16, r1
.Lsb_rep1_row:
	mov.l   @r1, r2
	mov.l   @(4, r1), r3
	mov.l   @(8, r1), r5
	mov.l   @(12, r1), r0
	add     r11, r1
	mov.l   r2, @r6
	mov.l   r3, @(4, r6)
	mov.l   r5, @(8, r6)
	dt      r9
	mov.l   r0, @(12, r6)
	bf.s    .Lsb_rep1_row
	add     r11, r6
	dt      r8
	bt.s    .Lsb_done_bra
	add     r14, r6
	bra     .Lsb_tile
	nop
.Lsb_done_bra:
	bra     .Lsb_done
	nop

	/* hflip (!vflip & vflip via row_step in @(4, r15)) */
.Lsb_hf:
	mov     r13, r9
.Lsb_hf_row:
	mov.l   @r1, r2
	mov.l   @(4, r15), r0
	add     r0, r1
	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	swap.w  r3, r3
	mov.l   r3, @r6

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	swap.w  r3, r3
	mov.l   r3, @(4, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	swap.w  r3, r3
	mov.l   r3, @(8, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	dt      r9
	swap.w  r3, r3
	mov.l   r3, @(12, r6)
	bf.s    .Lsb_hf_row
	add     r11, r6

	dt      r8
	bt.s    .Lsb_done_bra
	add     r14, r6
	bra     .Lsb_tile
	nop
	.size   _cgba_render_4bpp_strip_base_sh4, .-_cgba_render_4bpp_strip_base_sh4

/* ============================================================================
 * void cgba_render_4bpp_strip_over_sh4(
 *   const u16 *map1,          // r4
 *   u32 counts_packed,        // r5: todraw1 | (todraw2 << 16), todraw1 >= 1
 *   u16 *dst_top,             // r6 (4-byte aligned)
 *   const u8 *tile_charbase,  // r7 (32-byte aligned)
 *   u32 start_r_and_k,        // @(0, sp_in): (start_r << 8) | k
 *   const u32 *pair_lut,      // @(4, sp_in)
 *   const u16 *map2)          // @(8, sp_in) (only when todraw2 != 0)
 * ========================================================================== */
	.balign 4
.macro SO_PART_BYTE_NHF disp_lo, disp_hi, do_shift
	extu.b  r2, r0
	tst     r0, r0
	.if \do_shift
	bt.s    1f
	shlr8   r2
	.else
	bt      1f
	.endif
	tst     #0x0F, r0
	bt      2f
	tst     #0xF0, r0
	bt.s    3f
	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.l   r0, @(\disp_lo, r6)
2:	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.w   r0, @(\disp_hi, r6)
3:	mov.w   @(r0, r5), r0
	mov.w   r0, @(\disp_lo, r6)
1:
.endm

.macro SO_PART_BYTE_HF disp_lo, disp_hi, do_shift
	extu.b  r2, r0
	tst     r0, r0
	.if \do_shift
	bt.s    1f
	shlr8   r2
	.else
	bt      1f
	.endif
	tst     #0x0F, r0
	bt      2f
	tst     #0xF0, r0
	bt.s    3f
	shll2   r0
	mov.l   @(r0, r5), r0
	swap.w  r0, r0
	bra     1f
	mov.l   r0, @(\disp_lo, r6)
2:	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.w   r0, @(\disp_lo, r6)
3:	mov.w   @(r0, r5), r0
	mov.w   r0, @(\disp_hi, r6)
1:
.endm

/* Shared 2-tile-at-a-time known-empty run skip for strip_over[_u16]_sh4. */
.macro CGBA_STRIP_OVER_KE_RUN pfx
\pfx\()_ke_run:
	dt      r8
	bt.s    \pfx\()_ke_done
	add     #16, r6
	mov     r4, r0
	tst     #2, r0
	bt      \pfx\()_ke_aligned
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bf      \pfx\()_ke_have_r0
	dt      r8
	bt.s    \pfx\()_ke_done
	add     #16, r6
\pfx\()_ke_aligned:
	mov     r8, r5
	shlr    r5
	tst     r5, r5
	bt      \pfx\()_ke_tail1
	extu.w  r12, r2
	swap.w  r2, r1
	or      r1, r2                      /* r2 = [r12 | r12] */
\pfx\()_ke_pair:
	mov.l   @r4+, r0
	cmp/eq  r2, r0
	bf      \pfx\()_ke_pmiss
	dt      r5
	bf.s    \pfx\()_ke_pair
	add     #32, r6
\pfx\()_ke_tail1:
	mov     r8, r0
	tst     #1, r0
	bt      \pfx\()_ke_done
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bf.s    \pfx\()_ke_have_r0
	mov     #1, r8
	bra     \pfx\()_done
	add     #16, r6
\pfx\()_ke_pmiss:
	shll    r5
	mov     #1, r1
	and     r8, r1
	mov     r5, r8
	add     r1, r8
	swap.w  r0, r1
	exts.w  r1, r1
	cmp/eq  r12, r1
	bt.s    1f
	exts.w  r0, r0
	add     #-2, r4
	bra     \pfx\()_tile_have_r0
	mov     r1, r0
1:	add     #16, r6
	bra     \pfx\()_tile_have_r0
	add     #-1, r8
\pfx\()_ke_have_r0:
	bra     \pfx\()_tile_have_r0
	nop
\pfx\()_ke_done:
	bra     \pfx\()_done
	nop
.endm

	.section .text._cgba_render_4bpp_strip_over_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_4bpp_strip_over_sh4
	.type   _cgba_render_4bpp_strip_over_sh4, @function
_cgba_render_4bpp_strip_over_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	extu.w  r5, r8                      /* r8  = todraw1 */
	shlr16  r5                          /* r5  = todraw2 */
	lds     r5, macl                    /* macl = todraw2 */
	tst     r5, r5
	bt      1f
	mov.l   @(36, r15), r0              /* map2 */
	lds     r0, mach                    /* mach = map2 */
1:	mov.l   @(32, r15), r10             /* r10 = pair_lut */
	mov.l   @(28, r15), r0              /* (start_r << 8) | k */
	extu.b  r0, r2                      /* r2  = k (1..8) */
	shlr8   r0
	shll2   r0                          /* r0  = start_r * 4 */
	add     r0, r7                      /* r7  = tile_charbase + start_r*4 */
	add     r0, r0
	mov     #28, r3
	sub     r0, r3                      /* r3  = vflip_off = 28 - 2*start_r*4 */

	/* dst_next_delta = 16 - k * 480 */
	mov     r2, r0
	shll8   r0
	add     r0, r0                      /* k * 512 */
	mov     r2, r1
	shll2   r1
	shll2   r1
	add     r1, r1                      /* k * 32 */
	sub     r1, r0                      /* k * 480 */
	mov     #16, r1
	sub     r0, r1                      /* r1  = 16 - k * 480 */
	mov     #4, r0
	mov.l   r0, @-r15                   /* @(12, r15) = row_step (+4 or -4) */
	mov.l   r1, @-r15                   /* @(8,  r15) = dst_next_delta */
	mov.l   r2, @-r15                   /* @(4,  r15) = k */
	mov.l   r3, @-r15                   /* @(0,  r15) = vflip_off */

	mov.w   .Lso_480, r11               /* r11 = 480 */
	mov.l   .Lso_eeeef, r13             /* r13 = 0xEEEEEEEF (also empty sentinel!) */
	mov.l   .Lso_88888, r14             /* r14 = 0x88888888 */
	mov     r13, r12                    /* r12 = last known all-empty raw_map */

	.balign 4
.Lso_tile:
	mov.w   @r4+, r0
	cmp/eq  r12, r0                     /* matches known all-k-rows-empty map entry? */
	bt      .Lso_known_empty
.Lso_tile_have_r0:
	mov     r0, r3                      /* r3 = raw_map */
	swap.b  r0, r1
	mov     #5, r2
	shld    r2, r1
	mov.w   .Lso_7fe0, r2
	and     r2, r1                      /* r1 = tile_id << 5 */
	add     r7, r1                      /* r1 = tile_ptr (top row) */
	tst     #0x08, r0                   /* T = !vflip */
	mov     #4, r5
	bt      1f
	mov.l   @(0, r15), r2               /* vflip_off */
	add     r2, r1
	mov     #-4, r5
1:	mov.l   r5, @(12, r15)              /* row_step = +4 or -4 */

	mov.l   @(4, r15), r9               /* r9 = k */
.Lso_scan:
	mov.l   @r1, r2
	add     r5, r1
	tst     r2, r2
	bf      .Lso_nz0
	dt      r9
	bf.s    .Lso_scan
	add     r11, r6

	/* All k rows empty: cache raw_map in r12 and advance to next tile */
	mov     r3, r12
	bra     .Lso_tile_next
	nop

.Lso_known_empty:
	dt      r8
	bt.s    .Lso_done
	add     #16, r6
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bf      .Lso_tile_have_r0
	bra     .Lso_ke_run
	nop

.Lso_nz0:
	mov     r3, r0
	tst     #0x04, r0                   /* T = !hflip */
	and     #0xF0, r0
	shlr2   r0
	shll8   r0
	mov     r10, r5
	bf.s    .Lso_to_hf
	add     r0, r5                      /* r5 = &pair_lut[pal][0] */

	mov     r13, r0
.Lso_nhf_r0:
	not     r2, r3
	add     r2, r0
	and     r3, r0
	tst     r14, r0
	bf      .Lso_nhf_part

	/* Aligned opaque !hflip row */
	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	mov.l   r3, @(12, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	mov.l   r3, @(8, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	mov.l   r3, @(4, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	mov.l   r3, @r6
.Lso_nhf_next:
	dt      r9
	bt.s    .Lso_tile_next
	add     r11, r6
	mov.l   @r1, r2
	mov.l   @(12, r15), r0
	add     r0, r1
	mov     r13, r0
	tst     r2, r2
	bf      .Lso_nhf_r0
	bra     .Lso_nhf_next
	nop

.Lso_tile_next:
	mov.l   @(8, r15), r0               /* 16 - k*480 */
	dt      r8
	bf.s    .Lso_tile
	add     r0, r6

.Lso_done:
	sts     macl, r8
	tst     r8, r8
	bf      .Lso_span2
	add     #16, r15                    /* pop vflip_off, k, dst_next_delta, row_step */
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lso_span2:
	sts     mach, r4
	mov     #0, r0
	bra     .Lso_tile
	lds     r0, macl

.Lso_to_hf:
	bra     .Lso_hf_entry
	nop

	.balign 4
.Lso_eeeef:
	.long   0xEEEEEEEF
.Lso_88888:
	.long   0x88888888
.Lso_480:
	.word   480
.Lso_7fe0:
	.word   0x7FE0
	.balign 4

	/* !hflip partial row (direct disp8 target from .Lso_nhf_r0) */
.Lso_nhf_part:
	SO_PART_BYTE_NHF 12, 14, 1
	SO_PART_BYTE_NHF  8, 10, 1
	SO_PART_BYTE_NHF  4,  6, 1
	SO_PART_BYTE_NHF  0,  2, 0
	bra     .Lso_nhf_next
	nop

	CGBA_STRIP_OVER_KE_RUN .Lso

	/* hflip row loop (r5 = &pair_lut[pal][0], r2 = first nonzero word) */
.Lso_hf_entry:
	mov     r13, r0
.Lso_hf_r0:
	not     r2, r3
	add     r2, r0
	and     r3, r0
	tst     r14, r0
	bf      .Lso_hf_part

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	swap.w  r3, r3
	mov.l   r3, @r6

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	swap.w  r3, r3
	mov.l   r3, @(4, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	swap.w  r3, r3
	mov.l   r3, @(8, r6)

	extu.b  r2, r0
	shll2   r0
	mov.l   @(r0, r5), r3
	swap.w  r3, r3
	mov.l   r3, @(12, r6)
.Lso_hf_next:
	dt      r9
	bt.s    .Lso_hf_tile_done
	add     r11, r6
	mov.l   @r1, r2
	mov.l   @(12, r15), r0
	add     r0, r1
	mov     r13, r0
	tst     r2, r2
	bf      .Lso_hf_r0
	bra     .Lso_hf_next
	nop
.Lso_hf_tile_done:
	bra     .Lso_tile_next
	nop

.Lso_hf_part:
	SO_PART_BYTE_HF  0,  2, 1
	SO_PART_BYTE_HF  4,  6, 1
	SO_PART_BYTE_HF  8, 10, 1
	SO_PART_BYTE_HF 12, 14, 0
	bra     .Lso_hf_next
	nop
	.size   _cgba_render_4bpp_strip_over_sh4, .-_cgba_render_4bpp_strip_over_sh4

/* ============================================================================
 * _cgba_render_4bpp_strip_blend_sh4  /  _cgba_render_4bpp_strip_blend_sat_sh4
 *
 * Tile-major 4bpp alpha-blended strip renderers for direct-to-RGB565 Mode 0/1
 * blending (RGB565 BLND_MSK = 0x07E0F81F; sat=0 for eva+evb<=16, sat=1 for
 * eva+evb>16 with channel clamping). Works for any 2-byte aligned dst_top.
 *
 * Signature:
 *   void cgba_render_4bpp_strip_blend[_sat]_sh4(
 *     const u16 *map1,          // r4
 *     u32 counts_packed,        // r5: todraw1 | (todraw2 << 16)
 *     u16 *dst_top,             // r6: 2-byte aligned destination
 *     const u8 *tile_charbase,  // r7
 *     u32 start_r_k_evb,        // @(0, sp_in): (evb << 16) | (start_r << 8) | k
 *     const u32 *term_a,        // @(4, sp_in): 256-entry pre-scaled BG term_a
 *     const u16 *map2)          // @(8, sp_in): second map segment if todraw2 != 0
 * ========================================================================== */
.macro CGBA_BLEND_BYTE_CALL pfx, disp_lo, disp_hi, do_shift
	extu.b  r2, r0
	tst     r0, r0
	bt      2f
	and     #0x0F, r0
	tst     r0, r0
	bt      1f
	shll2   r0
	mov.l   @(r0, r5), r3
	bsr     \pfx\()_blend_pix
	mov.w   @(\disp_lo, r6), r0
	mov.w   r0, @(\disp_lo, r6)
1:	extu.b  r2, r0
	shlr2   r0
	shlr2   r0
	tst     r0, r0
	bt      2f
	shll2   r0
	mov.l   @(r0, r5), r3
	bsr     \pfx\()_blend_pix
	mov.w   @(\disp_hi, r6), r0
	mov.w   r0, @(\disp_hi, r6)
2:
	.if \do_shift
	shlr8   r2
	.endif
.endm

/* Solid-color 2-pixel aligned blend pair (out-of-line miss helper keeps all
 * 4 solid loops compact in I-cache and inside 8-bit branch range). */
.macro CGBA_SOLID_PAIR_P pfx, disp
	mov.l   @(\disp, r6), r0
	cmp/eq  r5, r0
	bt      1f
	bsr     \pfx\()_solid_pair_miss
	mov     r0, r5
1:	mov.l   r2, @(\disp, r6)
.endm

/* Solid-color 16-bit edge pixel for odd tile_hoff (shares (r5, r2) pair cache). */
.macro CGBA_SOLID_EDGE_PIX_P pfx, disp
	mov.w   @(\disp, r6), r0
	extu.w  r0, r0
	swap.w  r0, r12
	or      r12, r0
	cmp/eq  r5, r0
	bt      1f
	bsr     \pfx\()_solid_edge_miss
	mov     r0, r5
1:	mov     r2, r0
	mov.w   r0, @(\disp, r6)
.endm

.macro CGBA_SAT_R0_P pfx
	mov.l   @(48, r15), r12
	tst     r12, r0
	bt      9f
	mov.l   r1, @-r15
	CGBA_SWAR_SAT_PFE r0, r12, r12, r1
	mov.l   @r15+, r1
9:
.endm

.macro CGBA_STRIP_BLEND_KERNEL sym, pfx, sat
	.section .text.\sym,"ax",@progbits
	.balign 4
	.global \sym
	.type   \sym, @function
\sym:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15
	sts.l   pr, @-r15

	extu.w  r5, r8                      /* r8  = todraw1 */
	shlr16  r5                          /* r5  = todraw2 */
	mov.l   @(40, r15), r1              /* r1  = map2 */
	mov.l   @(36, r15), r10             /* r10 = term_a */
	mov.l   @(32, r15), r0              /* r0  = (evb << 16) | (start_r << 8) | k */
	mov     r0, r14
	shlr16  r14                         /* r14 = evb (0..16) */
	extu.b  r0, r2                      /* r2  = k (1..8) */
	shlr8   r0
	extu.b  r0, r0
	shll2   r0                          /* r0  = start_r * 4 */
	add     r0, r7                      /* r7  = tile_charbase + start_r * 4 */
	add     r0, r0
	mov     #28, r3
	sub     r0, r3                      /* r3  = vflip_off = 28 - 2 * start_r * 4 */

	/* dst_next_delta = 16 - k * 480 */
	mov     r2, r0
	shll8   r0
	add     r0, r0                      /* k * 512 */
	mov     r2, r12
	shll2   r12
	shll2   r12
	add     r12, r12                    /* k * 32 */
	sub     r12, r0                     /* k * 480 */
	mov     #16, r12
	sub     r0, r12                     /* r12 = 16 - k * 480 */

	/* Local stack frame (48 bytes; 52 bytes when sat=1 with @(48,r15)=OVF_ALL):
	 *   @(44, r15) = pure_cand, @(40, r15) = pure_solid_ta, @(36, r15) = pure_solid_raw,
	 *   @(32, r15) = solid_word, @(28, r15) = saved_raw_map, @(24, r15) = row_step,
	 *   @(20, r15) = last_empty_raw, @(16, r15) = map2, @(12, r15) = todraw2,
	 *   @(8, r15) = dst_next_delta, @(4, r15) = k, @(0, r15) = vflip_off */
	.if \sat
	mov.l   \pfx\()_ovf_all, r0
	mov.l   r0, @-r15                   /* @(48, r15) = 0x08010020 */
	.endif
	mov.l   \pfx\()_blnd_msk, r13       /* r13 = 0x07E0F81F (also impossible 16-bit sentinel) */
	mov     #0, r0
	mov.l   r13, @-r15                  /* @(44, r15) = pure_cand */
	mov.l   r0, @-r15                   /* @(40, r15) = pure_solid_ta */
	mov.l   r13, @-r15                  /* @(36, r15) = pure_solid_raw */
	mov.l   r0, @-r15                   /* @(32, r15) = solid_word */
	mov.l   r0, @-r15                   /* @(28, r15) = saved_raw_map */
	mov     #4, r0
	mov.l   r0, @-r15                   /* @(24, r15) = row_step */
	mov.l   r13, @-r15                  /* @(20, r15) = last_empty_raw */
	mov.l   r1, @-r15                   /* @(16, r15) = map2 */
	mov.l   r5, @-r15                   /* @(12, r15) = todraw2 */
	mov.l   r12, @-r15                  /* @(8,  r15) = dst_next_delta */
	mov.l   r2, @-r15                   /* @(4,  r15) = k */
	mov.l   r3, @-r15                   /* @(0,  r15) = vflip_off */

	mov.w   \pfx\()_480, r11            /* r11 = 480 */

	.balign 4
\pfx\()_tile:
	mov.w   @r4+, r0
\pfx\()_tile_have_r0:
	mov.l   @(20, r15), r3
	cmp/eq  r3, r0
	bt      \pfx\()_known_empty
	mov.l   @(36, r15), r3
	cmp/eq  r3, r0
	bt      \pfx\()_known_solid
	mov     r0, r3                      /* r3 = raw_map */
	swap.b  r0, r1
	mov     #5, r2
	shld    r2, r1
	mov.w   \pfx\()_7fe0, r2
	and     r2, r1                      /* r1 = tile_id << 5 */
	add     r7, r1                      /* r1 = tile_charbase + start_r*4 + tile_id*32 */
	tst     #0x08, r0                   /* T = !vflip */
	bf      \pfx\()_setup_vflip
	mov     #4, r5
	bra     \pfx\()_scan_start
	mov.l   r5, @(24, r15)

\pfx\()_setup_vflip:
	mov.l   @(0, r15), r2
	add     r2, r1
	mov     #-4, r5
	mov.l   r5, @(24, r15)

\pfx\()_scan_start:
	mov.l   @(4, r15), r9               /* r9 = k */
\pfx\()_empty_scan:
	mov.l   @r1, r2
	add     r5, r1
	tst     r2, r2
	bf      \pfx\()_nz_found
	dt      r9
	bf.s    \pfx\()_empty_scan
	add     r11, r6

	mov.l   r3, @(20, r15)
	bra     \pfx\()_tile_next
	nop

\pfx\()_known_empty:
	add     #16, r6
	dt      r8
	bf      \pfx\()_tile
	bra     \pfx\()_seg_done
	nop

\pfx\()_known_solid:
	mov.l   @(40, r15), r3              /* r3 = pure_solid_ta */
	mov.l   @(4, r15), r9               /* r9 = k */
	mov     #0, r5
	mov     r3, r2
	shlr2   r2
	shlr2   r2
	and     r13, r2
	swap.w  r2, r12
	or      r12, r2                     /* r2 = (out_black << 16) | out_black */
	mov     r6, r0
	tst     #2, r0
	bf      1f
	bra     \pfx\()_psa_loop
	nop
1:	bra     \pfx\()_psu_loop
	nop

\pfx\()_tile_next:
	mov.l   @(8, r15), r0               /* dst_next_delta = 16 - k * 480 */
	add     r0, r6
	dt      r8
	bf      \pfx\()_tile
\pfx\()_seg_done:
	mov.l   @(12, r15), r8              /* todraw2 */
	tst     r8, r8
	bt      \pfx\()_ret
	mov     #0, r0
	mov.l   r0, @(12, r15)
	bra     \pfx\()_tile
	mov.l   @(16, r15), r4              /* r4 = map2 */

\pfx\()_ret:
	.if \sat
	add     #52, r15
	.else
	add     #48, r15
	.endif
	lds.l   @r15+, pr
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	.balign 4
	.if \sat
\pfx\()_ovf_all:
	.long   0x08010020
	.endif
\pfx\()_blnd_msk:
	.long   0x07E0F81F
\pfx\()_480:
	.word   480
\pfx\()_7fe0:
	.word   0x7FE0

	.balign 4
\pfx\()_nz_found:
	mov.l   r3, @(28, r15)              /* save raw_map for solid/general transitions */
	mov     r3, r0
	and     #0xF0, r0
	shll2   r0                          /* pal * 64 bytes */
	mov     r10, r5
	add     r0, r5                      /* r5 = &term_a[pal * 16] */
	mov     r2, r0
	shll2   r0
	shll2   r0
	xor     r2, r0
	shlr2   r0
	shlr2   r0
	tst     r0, r0
	bt      \pfx\()_solid_enter
	mov     r3, r0
	tst     #0x04, r0                   /* T = !hflip */
	bf      1f
	bra     \pfx\()_nhf_have_r2
	nop
1:	bra     \pfx\()_hf_have_r2
	nop

	.balign 4
\pfx\()_solid_enter:
	mov.l   r2, @(32, r15)              /* save solid_word */
	mov.l   @(4, r15), r0               /* k */
	cmp/eq  r0, r9                      /* row 0 solid (no leading empty rows)? */
	bt      1f
	bra     2f
	mov.l   r13, @(44, r15)             /* pure_cand = sentinel */
1:	mov.l   r3, @(44, r15)              /* pure_cand = raw_map */
2:	mov     r2, r0
	and     #0x0F, r0
	shll2   r0
	mov.l   @(r0, r5), r3               /* r3 = term_a[pix] */
	mov     #0, r5
	mov     r3, r2
	shlr2   r2
	shlr2   r2
	and     r13, r2
	swap.w  r2, r12
	or      r12, r2                     /* r2 = (out_black << 16) | out_black */
	mov     r6, r0
	tst     #2, r0
	bt      \pfx\()_sa_loop
	bra     \pfx\()_su_loop
	nop

	.balign 4
\pfx\()_sa_loop:
	CGBA_SOLID_PAIR_P \pfx,  0
	CGBA_SOLID_PAIR_P \pfx,  4
	CGBA_SOLID_PAIR_P \pfx,  8
	CGBA_SOLID_PAIR_P \pfx, 12
\pfx\()_sa_next:
	add     r11, r6
	dt      r9
	bf      \pfx\()_sa_fetch
	mov.l   @(44, r15), r0
	cmp/eq  r13, r0
	bt      1f
	mov.l   r0, @(36, r15)
	bra     \pfx\()_psa_tile_done
	mov.l   r3, @(40, r15)
1:	bra     \pfx\()_tile_next
	nop
\pfx\()_sa_fetch:
	mov.l   @r1, r12
	mov.l   @(24, r15), r0
	add     r0, r1
	mov.l   @(32, r15), r0
	cmp/eq  r0, r12
	bt      \pfx\()_sa_loop
	tst     r12, r12
	bf      1f
	bra     \pfx\()_sa_next
	mov.l   r13, @(44, r15)
1:	mov     r12, r2
	mov.l   @(28, r15), r3
	bra     \pfx\()_nz_found
	nop

	.balign 4
\pfx\()_su_loop:
	CGBA_SOLID_EDGE_PIX_P \pfx,  0
	add     #2, r6
	CGBA_SOLID_PAIR_P     \pfx,  0
	CGBA_SOLID_PAIR_P     \pfx,  4
	CGBA_SOLID_PAIR_P     \pfx,  8
	CGBA_SOLID_EDGE_PIX_P \pfx, 12
	add     #-2, r6
\pfx\()_su_next:
	add     r11, r6
	dt      r9
	bf      \pfx\()_su_fetch
	mov.l   @(44, r15), r0
	cmp/eq  r13, r0
	bt      1f
	mov.l   r0, @(36, r15)
	bra     \pfx\()_psu_tile_done
	mov.l   r3, @(40, r15)
1:	bra     \pfx\()_tile_next
	nop
\pfx\()_su_fetch:
	mov.l   @r1, r12
	mov.l   @(24, r15), r0
	add     r0, r1
	mov.l   @(32, r15), r0
	cmp/eq  r0, r12
	bt      \pfx\()_su_loop
	tst     r12, r12
	bf      1f
	bra     \pfx\()_su_next
	mov.l   r13, @(44, r15)
1:	mov     r12, r2
	mov.l   @(28, r15), r3
	bra     \pfx\()_nz_found
	nop

	.balign 4
\pfx\()_psa_loop:
	CGBA_SOLID_PAIR_P \pfx,  0
	CGBA_SOLID_PAIR_P \pfx,  4
	CGBA_SOLID_PAIR_P \pfx,  8
	CGBA_SOLID_PAIR_P \pfx, 12
	dt      r9
	bf.s    \pfx\()_psa_loop
	add     r11, r6
\pfx\()_psa_tile_done:
	mov.l   @(8, r15), r0
	add     r0, r6
	dt      r8
	bt      \pfx\()_seg_done_p
	mov.w   @r4+, r0
	mov.l   @(36, r15), r12
	cmp/eq  r12, r0
	bt.s    \pfx\()_psa_loop
	mov.l   @(4, r15), r9
	bra     \pfx\()_tile_have_r0
	nop

	.balign 4
\pfx\()_psu_loop:
	CGBA_SOLID_EDGE_PIX_P \pfx,  0
	add     #2, r6
	CGBA_SOLID_PAIR_P     \pfx,  0
	CGBA_SOLID_PAIR_P     \pfx,  4
	CGBA_SOLID_PAIR_P     \pfx,  8
	CGBA_SOLID_EDGE_PIX_P \pfx, 12
	add     #-2, r6
	dt      r9
	bf.s    \pfx\()_psu_loop
	add     r11, r6
\pfx\()_psu_tile_done:
	mov.l   @(8, r15), r0
	add     r0, r6
	dt      r8
	bt      \pfx\()_seg_done_p
	mov.w   @r4+, r0
	mov.l   @(36, r15), r12
	cmp/eq  r12, r0
	bt.s    \pfx\()_psu_loop
	mov.l   @(4, r15), r9
	bra     \pfx\()_tile_have_r0
	nop
\pfx\()_seg_done_p:
	bra     \pfx\()_seg_done
	nop

	/* Out-of-line pair & edge-pixel miss helper (shared by all 4 solid loops). */
	.balign 4
\pfx\()_solid_pair_miss:
	swap.w  r0, r12
	cmp/eq  r12, r0
	bf      1f
\pfx\()_solid_edge_miss:
	and     r13, r0
	mul.l   r14, r0
	sts     macl, r0
	add     r3, r0
	shlr2   r0
	shlr2   r0
	.if \sat
	CGBA_SAT_R0_P \pfx
	.endif
	and     r13, r0
	swap.w  r0, r12
	or      r12, r0
	rts
	mov     r0, r2
1:	extu.w  r0, r0
	swap.w  r0, r12
	or      r12, r0
	and     r13, r0
	mul.l   r14, r0
	sts     macl, r0
	add     r3, r0
	shlr2   r0
	shlr2   r0
	.if \sat
	CGBA_SAT_R0_P \pfx
	.endif
	and     r13, r0
	swap.w  r0, r12
	or      r12, r0
	extu.w  r0, r2                      /* r2 = 0x0000_out1 */
	mov     r5, r0
	shlr16  r0
	swap.w  r0, r12
	or      r12, r0
	and     r13, r0
	mul.l   r14, r0
	sts     macl, r0
	add     r3, r0
	shlr2   r0
	shlr2   r0
	.if \sat
	CGBA_SAT_R0_P \pfx
	.endif
	and     r13, r0
	swap.w  r0, r12
	or      r12, r0
	shll16  r0
	rts
	or      r0, r2                      /* r2 = (out0 << 16) | out1 */

	/* Shared single-pixel blend helper called from CGBA_BLEND_BYTE_CALL (16 sites).
	 * In: r0 = dst RGB565 (from delay slot), r3 = term_a[pix], r13 = BLND_MSK, r14 = evb.
	 * Out: r0 = blended RGB565. Clobbers r12, MACL, T. */
	.balign 4
\pfx\()_blend_pix:
	extu.w  r0, r0
	swap.w  r0, r12
	or      r12, r0
	and     r13, r0
	mul.l   r14, r0
	sts     macl, r0
	add     r3, r0
	shlr2   r0
	shlr2   r0
	.if \sat
	CGBA_SAT_R0_P \pfx
	.endif
	and     r13, r0
	swap.w  r0, r12
	rts
	or      r12, r0

	.balign 4
\pfx\()_nhf_have_r2:
	CGBA_BLEND_BYTE_CALL \pfx, 12, 14, 1
	CGBA_BLEND_BYTE_CALL \pfx,  8, 10, 1
	CGBA_BLEND_BYTE_CALL \pfx,  4,  6, 1
	CGBA_BLEND_BYTE_CALL \pfx,  0,  2, 0
\pfx\()_nhf_next:
	add     r11, r6
	dt      r9
	bf      \pfx\()_nhf_fetch
	bra     \pfx\()_tile_next
	nop
\pfx\()_nhf_fetch:
	mov.l   @r1, r2
	mov.l   @(24, r15), r0
	add     r0, r1
	tst     r2, r2
	bt      \pfx\()_nhf_next
	mov     r2, r0
	shll2   r0
	shll2   r0
	xor     r2, r0
	shlr2   r0
	shlr2   r0
	tst     r0, r0
	bf      1f
	bra     \pfx\()_solid_enter
	mov.l   @(28, r15), r3
1:	bra     \pfx\()_nhf_have_r2
	nop

	.balign 4
\pfx\()_hf_have_r2:
	CGBA_BLEND_BYTE_CALL \pfx,  2,  0, 1
	CGBA_BLEND_BYTE_CALL \pfx,  6,  4, 1
	CGBA_BLEND_BYTE_CALL \pfx, 10,  8, 1
	CGBA_BLEND_BYTE_CALL \pfx, 14, 12, 0
\pfx\()_hf_next:
	add     r11, r6
	dt      r9
	bf      \pfx\()_hf_fetch
	bra     \pfx\()_tile_next
	nop
\pfx\()_hf_fetch:
	mov.l   @r1, r2
	mov.l   @(24, r15), r0
	add     r0, r1
	tst     r2, r2
	bt      \pfx\()_hf_next
	mov     r2, r0
	shll2   r0
	shll2   r0
	xor     r2, r0
	shlr2   r0
	shlr2   r0
	tst     r0, r0
	bf      1f
	bra     \pfx\()_solid_enter
	mov.l   @(28, r15), r3
1:	bra     \pfx\()_hf_have_r2
	nop
	.size   \sym, .-\sym
.endm

	CGBA_STRIP_BLEND_KERNEL _cgba_render_4bpp_strip_blend_sh4, .Lsbl, 0
	CGBA_STRIP_BLEND_KERNEL _cgba_render_4bpp_strip_blend_sat_sh4, .Lsbs, 1

/* ============================================================================
 * _cgba_blend_solid_rect_sh4
 *
 * Fast solid-color RGB565 alpha-blend over a width x height rectangle in a
 * 240-pixel-pitch framebuffer (eva + evb <= 16, non-saturating).
 *
 * Signature:
 *   void cgba_blend_solid_rect_sh4(
 *     u16 *dst_top,        // r4: top-left pixel
 *     u32 wh_packed,       // r5: (height << 16) | width  (both >= 1)
 *     u32 term_a_val,      // r6: pre-scaled 1st-target term_a
 *     u32 evb)             // r7: 0..16
 * ========================================================================== */
	.section .text._cgba_blend_solid_rect_sh4,"ax",@progbits
	.balign 4
	.global _cgba_blend_solid_rect_sh4
	.type   _cgba_blend_solid_rect_sh4, @function
_cgba_blend_solid_rect_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15

	extu.w  r5, r8                      /* r8  = width */
	shlr16  r5                          /* r5  = height */
	mov.w   .Lbsr_480, r10
	sub     r8, r10
	sub     r8, r10                     /* r10 = 480 - width * 2 */
	mov.l   .Lbsr_msk, r3               /* r3  = 0x07E0F81F */

	/* Preload 1-entry cache with 0x0000 -> blended_black */
	mov     #0, r1
	mov     r6, r2
	shlr2   r2
	shlr2   r2
	and     r3, r2
	swap.w  r2, r11
	or      r11, r2
	mov     r8, r9

	.balign 4
.Lbsr_pix:
	mov.w   @r4, r0
	cmp/eq  r1, r0
	bt      1f
	mov     r0, r1
	extu.w  r0, r0
	swap.w  r0, r11
	or      r11, r0
	and     r3, r0
	mul.l   r7, r0
	sts     macl, r0
	add     r6, r0
	shlr2   r0
	shlr2   r0
	and     r3, r0
	swap.w  r0, r11
	or      r11, r0
	mov     r0, r2
1:	mov.w   r2, @r4
	dt      r9
	bf.s    .Lbsr_pix
	add     #2, r4

	add     r10, r4
	dt      r5
	bf.s    .Lbsr_pix
	mov     r8, r9

	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	.balign 4
.Lbsr_msk:
	.long   0x07E0F81F
.Lbsr_480:
	.word   480
	.size   _cgba_blend_solid_rect_sh4, .-_cgba_blend_solid_rect_sh4

/* ============================================================================
 * _cgba_render_obj_4bpp_affine_scale_sh4
 *   Fast 4bpp FULLCOLOR non-mosaic affine OBJ scanline (dy == 0, no VRAM wrap).
 *   r4 = dst_ptr (scanline + d_start)
 *   r5 = (cnt << 16) | obj_dimw  (cnt >= 1, obj_dimw in {8,16,32,64})
 *   r6 = source_x (24.8 fixed point)
 *   r7 = dx (24.8 fixed point)
 *   stack[0] = row_ptr (&vram[0x10000 + row_base])
 *   stack[4] = subpal  (palptr + palette)
 * ============================================================================ */
	.section .text._cgba_render_obj_4bpp_affine_scale_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_obj_4bpp_affine_scale_sh4
	.type   _cgba_render_obj_4bpp_affine_scale_sh4, @function
_cgba_render_obj_4bpp_affine_scale_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	mov.l   @(28, r15), r1              /* r1  = row_ptr */
	mov.l   @(32, r15), r11             /* r11 = subpal */
	extu.w  r5, r8                      /* r8  = obj_dimw (8,16,32,64) */
	shlr16  r5
	mov     r5, r9                      /* r9  = cnt */
	mov     #-8, r12                    /* r12 = -8 (shad >> 8) */

	/* Phase 1: skip leading out-of-bounds pixels before touching VRAM. */
	.balign 4
.Lafs_skip:
	mov     r6, r0
	shad    r12, r0
	cmp/hs  r8, r0
	bf      .Lafs_unpack
	dt      r9
	bt      .Lafs_done_no_frame
	add     #2, r4
	bra     .Lafs_skip
	add     r7, r6

	/* Phase 2: unpack dimw/8 tiles into 64-byte stack linear[x] = pixval<<1. */
.Lafs_unpack:
	mov     r0, r13                     /* save first in-bounds pixel_x */
	add     #-64, r15
	mov     r15, r10                    /* r10 = &linear[0] */
	mov     r10, r3                     /* write cursor */
	mov     r8, r2
	shlr2   r2
	shlr    r2                          /* r2 = num_tiles (1,2,4,8) */
	mov.l   .Lafs_nib2_ptr, r12         /* r12 = 512B _cgba_nib2_lut (reloaded to -8 below) */
	mov     #0, r5                      /* r5 = any non-zero tile flag */

	.balign 4
.Lafs_tile:
	mov.l   @r1, r14                    /* 4 bytes = 8 4bpp pixels */
	tst     r14, r14
	bt      .Lafs_ztile
	mov     #1, r5

	CGBA_UNPACK_4BPP_TILE_LUT r14, r3, r12

	add     #32, r1
	dt      r2
	bf.s    .Lafs_tile
	add     #8, r3
	bra     .Lafs_draw_start
	mov     r13, r0

.Lafs_ztile:
	mov.l   r14, @(0, r3)
	mov.l   r14, @(4, r3)
	add     #32, r1
	dt      r2
	bf.s    .Lafs_tile
	add     #8, r3
	tst     r5, r5
	bt      .Lafs_done
	mov     r13, r0

	/* Phase 3: inner pixel loop — 10 insns transparent, 12 insns opaque. */
	.balign 4
.Lafs_draw_start:
	mov     #-8, r12                    /* restore r12 = -8 (shad >> 8) */
.Lafs_draw:
	mov.b   @(r0, r10), r0              /* linear[px] = pixval << 1 (0..30) */
	tst     r0, r0
	bt      1f
	mov.w   @(r0, r11), r0
	mov.w   r0, @r4
1:	dt      r9
	bt      .Lafs_done
	add     r7, r6
	mov     r6, r0
	shad    r12, r0
	cmp/hs  r8, r0
	bf.s    .Lafs_draw
	add     #2, r4

.Lafs_done:
	add     #64, r15
.Lafs_done_no_frame:
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	.balign 4
.Lafs_nib2_ptr:
	.long   _cgba_nib2_lut
	.size   _cgba_render_obj_4bpp_affine_scale_sh4, .-_cgba_render_obj_4bpp_affine_scale_sh4

/* ============================================================================
 * _cgba_render_obj_4bpp_affine_scale_strip_sh4
 *   Multi-row pre-clipped 4bpp FULLCOLOR non-mosaic axis-aligned affine OBJ
 *   strip renderer (dy == 0, dmx == 0, no VRAM wrap). Caches unpacked 4bpp
 *   tile row across repeated source py rows (zoomed-in sprites) and omits
 *   per-pixel bounds checks via caller horizontal/vertical clipping.
 *
 *   r4 -> cgba_obj_aff_scale_strip_params:
 *     @(0)  dst_ptr         (top-left clipped screen pixel)
 *     @(4)  vram_tx         (&vram[0x10000 + bt + min_tx*32])
 *     @(8)  subpal          (palptr + pal_off)
 *     @(12) sx0             (pre-biased by -(min_tx << 11), always in-bounds)
 *     @(16) dx
 *     @(20) sy0             (always in-bounds py for all nrows)
 *     @(24) dmy
 *     @(28) span_cnt        (>= 1)
 *     @(32) nrows           (>= 1)
 *     @(36) ntiles          (1..8)
 *     @(40) tile_pitch_sh   (5, 6, 7, 8, or 10)
 * ============================================================================ */
	.section .text._cgba_render_obj_4bpp_affine_scale_strip_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_obj_4bpp_affine_scale_strip_sh4
	.type   _cgba_render_obj_4bpp_affine_scale_strip_sh4, @function
_cgba_render_obj_4bpp_affine_scale_strip_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15
	add     #-64, r15

	mov     r4, r14                     /* r14 = params * */
	mov     r15, r10                    /* r10 = &linear[0] (64 B) */
	mov.l   @(0, r14), r5               /* r5  = dst_row */
	mov.l   @(8, r14), r11              /* r11 = subpal */
	mov.l   @(20, r14), r9              /* r9  = sy */
	mov.l   @(28, r14), r0
	lds     r0, macl                    /* macl = span_cnt */
	mov.l   @(32, r14), r13             /* r13 = nrows */
	mov     #-8, r12                    /* r12 = -8 (shad >> 8) */
	mov     #-1, r8                     /* r8  = last_py = -1 */

	.balign 4
.Lafss_row:
	mov     r9, r0
	shad    r12, r0                     /* r0 = py = sy >> 8 */
	mov.l   @(24, r14), r1              /* dmy */
	cmp/eq  r8, r0
	bt.s    .Lafss_same_py
	add     r1, r9                      /* sy += dmy */

	/* Unpack ntiles of row py into linear[0..ntiles*8-1] via 512B 2-px LUT. */
	mov     r0, r8                      /* last_py = py */
	mov     r0, r1
	and     #7, r0
	shll2   r0                          /* (py & 7) << 2 */
	shlr2   r1
	shlr    r1                          /* py >> 3 */
	mov.l   @(40, r14), r2              /* tile_pitch_sh */
	shld    r2, r1                      /* (py >> 3) << tile_pitch_sh */
	mov.l   @(4, r14), r3               /* vram_tx */
	add     r0, r1
	add     r3, r1                      /* r1 = row_ptr */
	mov     r10, r3                     /* r3 = write cursor in linear[] */
	mov.l   @(36, r14), r2              /* r2 = ntiles (1..8) */
	mov.l   .Lafss_nib2_ptr, r7         /* r7 = 512B nibble-pair unpack LUT */
	mov     #0, r4                      /* r4 = any_nz */

	.balign 4
.Lafss_tile:
	mov.l   @r1, r6
	tst     r6, r6
	bt      .Lafss_ztile
	mov     #1, r4

	CGBA_UNPACK_4BPP_TILE_LUT r6, r3, r7

	add     #32, r1
	dt      r2
	bf.s    .Lafss_tile
	add     #8, r3
	bra     .Lafss_unpack_done
	lds     r4, mach

.Lafss_ztile:
	mov.l   r6, @(0, r3)
	mov.l   r6, @(4, r3)
	add     #32, r1
	dt      r2
	bf.s    .Lafss_tile
	add     #8, r3
	lds     r4, mach
	tst     r4, r4
	bt      .Lafss_next_row

.Lafss_unpack_done:
	mov.l   @(12, r14), r6              /* sx0 */
	mov.l   @(16, r14), r7              /* dx */
	sts     macl, r2                    /* span_cnt */
	mov     r5, r4                      /* dst_row */
	mov     r2, r3
	shlr    r3                          /* pairs = span_cnt >> 1 */
	tst     r3, r3
	bt      .Lafss_pix_odd

	.balign 4
.Lafss_pix2:
	mov     r6, r0
	shad    r12, r0                     /* px = sx >> 8 (guaranteed in-bounds) */
	mov.b   @(r0, r10), r0              /* linear[px] = pixval << 1 */
	tst     r0, r0
	bt.s    1f
	add     r7, r6                      /* sx += dx */
	mov.w   @(r0, r11), r0
	mov.w   r0, @r4
1:	mov     r6, r0
	shad    r12, r0
	mov.b   @(r0, r10), r0
	tst     r0, r0
	bt.s    2f
	add     r7, r6
	mov.w   @(r0, r11), r0
	mov.w   r0, @(2, r4)
2:	dt      r3
	bf.s    .Lafss_pix2
	add     #4, r4

	mov     r2, r0
	tst     #1, r0
	bt      .Lafss_next_row
.Lafss_pix_odd:
	mov     r6, r0
	shad    r12, r0
	mov.b   @(r0, r10), r0
	tst     r0, r0
	bt      .Lafss_next_row
	mov.w   @(r0, r11), r0
	mov.w   r0, @r4

.Lafss_next_row:
	mov.w   .Lafss_480, r0
	dt      r13
	bt      .Lafss_done
	bra     .Lafss_row
	add     r0, r5

.Lafss_same_py:
	sts     mach, r0
	tst     r0, r0
	bt      .Lafss_next_row
	bra     .Lafss_unpack_done
	nop

.Lafss_done:
	add     #64, r15
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	.balign 4
.Lafss_nib2_ptr:
	.long   _cgba_nib2_lut
.Lafss_480:
	.word   480
	.size   _cgba_render_obj_4bpp_affine_scale_strip_sh4, .-_cgba_render_obj_4bpp_affine_scale_strip_sh4

	.section .rodata._cgba_nib2_lut,"a",@progbits
	.balign 4
	.global _cgba_nib2_lut
	.type   _cgba_nib2_lut, @object
_cgba_nib2_lut:
	.set _nib_i, 0
	.rept 256
	.word (((_nib_i & 0x0F) << 9) | ((_nib_i & 0xF0) >> 3))
	.set _nib_i, _nib_i + 1
	.endr
	.size   _cgba_nib2_lut, 512

/* ============================================================================
 * _cgba_render_obj_4bpp_affine_rot_sh4
 *   Fast 4bpp FULLCOLOR non-mosaic affine OBJ scanline (dy != 0, no VRAM wrap).
 *   r4 = const cgba_aff_rot_params *p:
 *     +0  dst_ptr
 *     +4  base_vram (&vram[0x10000 + base_tile])
 *     +8  subpal    (palptr + palette)
 *     +12 source_x
 *     +16 source_y
 *     +20 dx
 *     +24 dy
 *     +28 cnt (>= 1)
 *     +32 dimw (8..64)
 *     +36 dimh (8..64)
 *     +40 row_shift (pitch_shift - 3: 2,3,4,5,7)
 * ============================================================================ */
	.section .text._cgba_render_obj_4bpp_affine_rot_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_obj_4bpp_affine_rot_sh4
	.type   _cgba_render_obj_4bpp_affine_rot_sh4, @function
_cgba_render_obj_4bpp_affine_rot_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	mov     r4, r0
	mov.l   @(0, r0), r4                /* r4  = dst_ptr (pre-clipped in-bounds) */
	mov.l   @(4, r0), r5                /* r5  = base_vram */
	mov.l   @(8, r0), r11               /* r11 = subpal */
	mov.l   @(12, r0), r6               /* r6  = source_x (0 <= x < dimw<<8) */
	mov.l   @(16, r0), r7               /* r7  = source_y (0 <= y < dimh<<8) */
	mov.l   @(20, r0), r8               /* r8  = dx */
	mov.l   @(24, r0), r9               /* r9  = dy */
	mov.l   @(28, r0), r10              /* r10 = cnt (>= 1) */
	mov.l   @(40, r0), r14              /* r14 = row_shift */
	mov     #-1, r12                    /* r12 = cached last_py (sentinel) */

	.balign 4
.Lafr_draw:
	mov     r7, r0
	shlr8   r0                          /* r0 = pixel_y (0..63) */
	cmp/eq  r12, r0
	bt      .Lafr_same_y
	mov     r0, r12                     /* cache last_py */
	mov     r0, r1
	and     #0xF8, r0
	shld    r14, r0                     /* (py >> 3) * tile_pitch */
	mov     r0, r13
	mov     r1, r0
	and     #0x07, r0
	shll2   r0                          /* (py & 7) * 4 */
	add     r0, r13                     /* r13 = cached y_off */

.Lafr_same_y:
	mov     r6, r0
	shlr8   r0                          /* r0 = pixel_x (0..63) */
	mov     r0, r2
	and     #0xF8, r0
	shll2   r0                          /* (px >> 3) * 32 */
	add     r13, r0                     /* + y_off */
	mov     r0, r3
	mov     r2, r0
	shlr    r0                          /* T = px & 1; r0 = px >> 1 */
	and     #0x03, r0                   /* (px >> 1) & 3 (T unchanged) */
	add     r3, r0                      /* total byte offset (T unchanged) */
	mov.b   @(r0, r5), r0               /* pixpair (T unchanged) */
	bf      1f
	shlr2   r0
	shlr2   r0
1:	and     #0x0F, r0
	tst     r0, r0
	bt      2f
	shll    r0
	mov.w   @(r0, r11), r0
	mov.w   r0, @r4
2:	add     #2, r4
	add     r8, r6
	dt      r10
	bf.s    .Lafr_draw
	add     r9, r7

	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8
	.size   _cgba_render_obj_4bpp_affine_rot_sh4, .-_cgba_render_obj_4bpp_affine_rot_sh4

/* ============================================================================
 * void cgba_sparse_override_band_sh4(const cgba_sparse_band_ctx *ctx)
 *
 * Scans full 8-pixel tiles in map1[0..td1-1] and map2[0..td2-1] of a Mode 0
 * blended 4bpp layer, skipping tiles equal to dom_raw and rows equal to
 * dom_word, and re-blending only non-dominant pixels by sampling the lower
 * 4bpp BGs directly in SH-4 assembly (or calling ctx->sample_obj_fn on rows
 * that contain lower-priority OBJs).
 *
 * cgba_sparse_bg_desc (64 bytes):
 *   +0:  const u8 *sb_row0[8]
 *   +32: u32 hofs
 *   +36: u32 hmask
 *   +40: const u8 *charbase
 *   +44: u8 vy7[8]
 *
 * cgba_sparse_band_ctx (88 bytes):
 *   +0:  const u16 *map1
 *   +4:  const u16 *map2
 *   +8:  u32 td1
 *   +12: u32 td2
 *   +16: u32 sx0
 *   +20: u32 start_r
 *   +24: u32 k
 *   +28: u16 *dst_top
 *   +32: const u8 *charbase
 *   +36: const u32 *term_a
 *   +40: const u16 *pal_conv
 *   +44: const cgba_sparse_bg_desc *low_bg
 *   +48: u32 n_low_bg
 *   +52: u32 dom_raw
 *   +56: u32 dom_sub
 *   +60: u32 dom_word
 *   +64: u32 evb           (ctx_hi +0)
 *   +68: u32 saturate      (ctx_hi +4)
 *   +72: u8 row_has_obj[8] (ctx_hi +8)
 *   +80: sample_obj_fn     (ctx_hi +16)
 *   +84: obj_ctx           (ctx_hi +20)
 * ========================================================================== */
	.section .text._cgba_sparse_override_band_sh4,"ax",@progbits
	.balign 4
	.global _cgba_sparse_override_band_sh4
	.type   _cgba_sparse_override_band_sh4, @function
_cgba_sparse_override_band_sh4:
	sts.l   pr, @-r15
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15
	add     #-52, r15

	/* Stack frame (52 bytes):
	 *   @(0,  r15) = row_ptr
	 *   @(4,  r15) = row_step (+4 or -4)
	 *   @(8,  r15) = ta (term_a + sub*4)
	 *   @(12, r15) = dst_row
	 *   @(16, r15) = same_sub (1 or 0)
	 *   @(20, r15) = hflip (1 or 0)
	 *   @(24, r15) = r_idx (0..k-1)
	 *   @(28, r15) = seg2_pending (1 or 0)
	 *   @(32, r15) = saved_diff
	 *   @(36, r15) = saved_rw
	 *   @(40, r15) = px_base
	 *   @(44, r15) = ctx_hi (ctx + 64)
	 *   @(48, r15) = cur_pix
	 */
	mov     r4, r14                     /* r14 = ctx */
	mov     r4, r0
	add     #64, r0
	mov.l   r0, @(44, r15)              /* ctx_hi */
	mov.l   .Lspo_blnd_msk, r13         /* r13 = 0x07E0F81F */
	mov.l   @(52, r14), r11             /* r11 = dom_raw */
	mov.l   @(60, r14), r12             /* r12 = dom_word */
	mov.l   @(16, r14), r10             /* r10 = sx0 */

	mov.l   @(8, r14), r9               /* r9 = td1 */
	tst     r9, r9
	bt      .Lspo_start_seg2
	mov.l   @(0, r14), r8               /* r8 = map1 */
	mov     #1, r0
	mov.l   r0, @(28, r15)              /* seg2_pending = 1 */
	bra     .Lspo_tile
	nop

.Lspo_start_seg2:
	mov     #0, r0
	mov.l   r0, @(28, r15)              /* seg2_pending = 0 */
	mov.l   @(12, r14), r9              /* r9 = td2 */
	tst     r9, r9
	bf      1f
	bra     .Lspo_done
	nop
1:
	mov.l   @(4, r14), r8               /* r8 = map2 */

	.balign 4
.Lspo_tile:
	mov.w   @r8+, r0
	extu.w  r0, r0
	cmp/eq  r11, r0                     /* raw_map == dom_raw? */
	bf      1f
	bra     .Lspo_tile_next
	nop
1:
	/* Decode big-endian raw_map in r0:
	 *   r0[15:8] = tid[7:0], r0[7:0] = [pal:4][vflip:1][hflip:1][tid:2] */
	mov     r0, r2                      /* r2 = raw_map */
	and     #0xF0, r0                   /* r0 = sub (0..240) */
	mov.l   @(56, r14), r1              /* dom_sub */
	cmp/eq  r1, r0
	movt    r1                          /* r1 = same_sub (1 or 0) */
	mov.l   r1, @(16, r15)
	shll2   r0                          /* sub * 4 bytes */
	mov.l   @(36, r14), r1              /* term_a */
	add     r1, r0
	mov.l   r0, @(8, r15)               /* ta = term_a + sub */

	/* hflip = (raw_map & 4) != 0 */
	mov     r2, r0
	tst     #0x04, r0
	movt    r0                          /* T=1 => !hflip */
	xor     #1, r0                      /* r0 = hflip (1 or 0) */
	mov.l   r0, @(20, r15)

	/* tile_ptr = charbase + (tid << 5) */
	swap.b  r2, r1
	mov     #5, r0
	shld    r0, r1
	mov.w   .Lspo_7fe0_a, r0
	and     r0, r1                      /* r1 = tid << 5 */
	mov.l   @(32, r14), r0              /* charbase */
	add     r0, r1                      /* r1 = tile_base */

	/* vflip = (raw_map & 8) != 0 */
	mov     r2, r0
	tst     #0x08, r0                   /* T=1 => !vflip */
	mov.l   @(20, r14), r0              /* start_r (T unchanged) */
	bt      1f
	neg     r0, r0
	add     #7, r0                      /* 7 - start_r */
	mov     #-4, r2
	bra     2f
	shll2   r0
1:
	mov     #4, r2
	shll2   r0
2:
	add     r0, r1                      /* row_ptr */
	mov.l   r1, @(0, r15)
	mov.l   r2, @(4, r15)               /* row_step */

	mov.l   @(28, r14), r0              /* dst_top */
	mov.l   r0, @(12, r15)              /* dst_row */
	mov     #0, r0
	mov.l   r0, @(24, r15)              /* r_idx = 0 */

	/* Fast path for same_sub == 1 && hflip == 0 (99%+ of sparse rain tiles). */
	mov.l   @(16, r15), r0              /* same_sub */
	tst     r0, r0
	bt      .Lspo_row_loop_g
	mov.l   @(20, r15), r0              /* hflip */
	tst     r0, r0
	bf      .Lspo_row_loop_g

	.balign 4
.Lspo_frow_loop:
	mov.l   @(0, r15), r1               /* row_ptr */
	mov.l   @r1, r3                     /* r3 = rw */
	mov.l   @(4, r15), r0               /* row_step */
	add     r0, r1
	mov.l   r1, @(0, r15)
	mov     r3, r2
	xor     r12, r2                     /* diff = rw ^ dom_word */
	tst     r2, r2
	bt      .Lspo_fnext_row

	mov     r10, r6
	add     #6, r6                      /* px_base = sx + 6 */
	extu.w  r2, r0
	tst     r0, r0
	bf      .Lspo_fbyte_loop
	shlr16  r2
	shlr16  r3
	add     #-4, r6

.Lspo_fbyte_loop:
	mov     r2, r0
	tst     #0xFF, r0
	bt      .Lspo_fnext_byte

	tst     #0x0F, r0                   /* low nibble changed? */
	bt      .Lspo_fhi_check
	/* Check if ONLY this low nibble differs in the rest of r2. */
	mov     r2, r1
	shlr2   r1
	shlr2   r1
	tst     r1, r1
	bf      .Lspo_fmulti
	/* Single-pixel fast path (low nibble only): zero stack spills! */
	mov     r3, r0
	and     #0x0F, r0
	mov.l   r0, @(48, r15)
	bsr     .Lspo_emit_pix
	mov     r6, r4
	bra     .Lspo_fnext_row
	nop

.Lspo_fhi_check:
	/* Low nibble == 0, byte != 0 => high nibble != 0. Check upper bytes. */
	mov     r2, r1
	shlr8   r1
	tst     r1, r1
	bf      .Lspo_fmulti_hi
	/* Single-pixel fast path (high nibble only): zero stack spills! */
	mov     r3, r0
	shlr2   r0
	shlr2   r0
	and     #0x0F, r0
	mov.l   r0, @(48, r15)
	mov     r6, r4
	bsr     .Lspo_emit_pix
	add     #1, r4
	bra     .Lspo_fnext_row
	nop

.Lspo_fmulti:
	mov.l   r2, @(32, r15)
	mov.l   r3, @(36, r15)
	mov.l   r6, @(40, r15)
	mov     r3, r0
	and     #0x0F, r0
	mov.l   r0, @(48, r15)
	bsr     .Lspo_emit_pix
	mov     r6, r4
	mov.l   @(32, r15), r0
	tst     #0xF0, r0
	bt      .Lspo_freload
	bra     .Lspo_femit_hi
	nop

.Lspo_fmulti_hi:
	mov.l   r2, @(32, r15)
	mov.l   r3, @(36, r15)
	mov.l   r6, @(40, r15)
.Lspo_femit_hi:
	mov.l   @(36, r15), r0
	shlr2   r0
	shlr2   r0
	and     #0x0F, r0
	mov.l   r0, @(48, r15)
	mov.l   @(40, r15), r4
	bsr     .Lspo_emit_pix
	add     #1, r4
.Lspo_freload:
	mov.l   @(32, r15), r2
	mov.l   @(36, r15), r3
	mov.l   @(40, r15), r6

.Lspo_fnext_byte:
	shlr8   r2
	tst     r2, r2
	bt      .Lspo_fnext_row
	shlr8   r3
	bra     .Lspo_fbyte_loop
	add     #-2, r6

.Lspo_fnext_row:
	mov.l   @(24, r15), r0              /* r_idx */
	add     #1, r0
	mov.l   @(24, r14), r1              /* k */
	cmp/eq  r1, r0
	bt      .Lspo_tile_next_bra
	mov.l   r0, @(24, r15)
	mov.l   @(12, r15), r1              /* dst_row */
	mov.w   .Lspo_480_a, r0
	add     r0, r1
	bra     .Lspo_frow_loop
	mov.l   r1, @(12, r15)
.Lspo_tile_next_bra:
	bra     .Lspo_tile_next
	nop

	.balign 4
.Lspo_480_a:
	.word   480

	/* General row loop (hflip == 1 or same_sub == 0). */
	.balign 4
.Lspo_row_loop_g:
	mov.l   @(0, r15), r1               /* row_ptr */
	mov.l   @r1, r3                     /* r3 = rw */
	mov.l   @(4, r15), r0               /* row_step */
	add     r0, r1
	mov.l   r1, @(0, r15)               /* row_ptr += row_step */

	mov.l   @(16, r15), r0              /* same_sub */
	tst     r0, r0
	bt      1f
	mov     r3, r2
	xor     r12, r2                     /* diff = rw ^ dom_word */
	tst     r2, r2
	bt      .Lspo_next_row
	bra     2f
	nop
1:
	mov     #-1, r2                     /* !same_sub: all 8 pixels differ */
2:
	/* r2 = diff (!= 0), r3 = rw. Set px_base in r6. */
	mov.l   @(20, r15), r1              /* hflip */
	mov     r10, r6                     /* sx */
	tst     r1, r1
	bf      3f
	add     #6, r6                      /* !hflip: px_base = sx + 6 */
	extu.w  r2, r0
	tst     r0, r0
	bf      .Lspo_byte_loop
	shlr16  r2
	shlr16  r3
	bra     .Lspo_byte_loop
	add     #-4, r6
3:
	add     #1, r6                      /* hflip: px_base = sx + 1 */
	extu.w  r2, r0
	tst     r0, r0
	bf      .Lspo_byte_loop
	shlr16  r2
	shlr16  r3
	add     #4, r6

	.balign 4
.Lspo_byte_loop:
	mov     r2, r0
	tst     #0xFF, r0
	bt      .Lspo_next_byte
	mov.l   r2, @(32, r15)              /* saved_diff */
	mov.l   r3, @(36, r15)              /* saved_rw */
	mov.l   r6, @(40, r15)              /* px_base */

	tst     #0x0F, r0                   /* low nibble changed? */
	bt      .Lspo_hi_nib
	mov     r3, r0
	and     #0x0F, r0
	mov.l   r0, @(48, r15)              /* cur_pix */
	bsr     .Lspo_emit_pix
	mov     r6, r4                      /* delay slot: r4 = px */
	mov.l   @(32, r15), r0              /* reload diff for high nibble test */

.Lspo_hi_nib:
	tst     #0xF0, r0                   /* high nibble changed? */
	bt      .Lspo_reload_byte
	mov.l   @(36, r15), r0              /* saved_rw */
	shlr2   r0
	shlr2   r0
	and     #0x0F, r0
	mov.l   r0, @(48, r15)              /* cur_pix */
	mov.l   @(40, r15), r4              /* px_base */
	mov.l   @(20, r15), r0              /* hflip */
	tst     r0, r0
	bf      1f
	bsr     .Lspo_emit_pix
	add     #1, r4                      /* !hflip: px_base + 1 */
	bra     .Lspo_reload_byte
	nop
1:
	bsr     .Lspo_emit_pix
	add     #-1, r4                     /* hflip: px_base - 1 */

.Lspo_reload_byte:
	mov.l   @(32, r15), r2
	mov.l   @(36, r15), r3
	mov.l   @(40, r15), r6

.Lspo_next_byte:
	shlr8   r2
	tst     r2, r2
	bt      .Lspo_next_row
	shlr8   r3
	mov.l   @(20, r15), r0              /* hflip */
	tst     r0, r0
	bf      1f
	bra     .Lspo_byte_loop
	add     #-2, r6
1:
	bra     .Lspo_byte_loop
	add     #2, r6

.Lspo_next_row:
	mov.l   @(24, r15), r0              /* r_idx */
	add     #1, r0
	mov.l   @(24, r14), r1              /* k */
	cmp/eq  r1, r0
	bt      .Lspo_tile_next
	mov.l   r0, @(24, r15)
	mov.l   @(12, r15), r1              /* dst_row */
	mov.w   .Lspo_480, r0
	add     r0, r1
	bra     .Lspo_row_loop_g
	mov.l   r1, @(12, r15)

.Lspo_tile_next:
	add     #8, r10                     /* sx += 8 */
	dt      r9
	bt      1f
	bra     .Lspo_tile
	nop
1:
	mov.l   @(28, r15), r0              /* seg2_pending */
	tst     r0, r0
	bt      .Lspo_done
	bra     .Lspo_start_seg2
	nop

.Lspo_done:
	add     #52, r15
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	mov.l   @r15+, r8
	lds.l   @r15+, pr
	rts
	nop

	.balign 4
.Lspo_blnd_msk:
	.long   0x07E0F81F
.Lspo_7fe0_a:
	.word   0x7FE0
.Lspo_480:
	.word   480

	/* --------------------------------------------------------------------
	 * Internal helper: sample lower pixel at (r4 = px, r_idx = @(24, r15))
	 * and blend with cur_pix = @(48, r15), storing to dst_row[px].
	 * Preserves: r8..r14, r15. Clobbers: r0..r7, T, macl.
	 * ------------------------------------------------------------------ */
	.balign 4
.Lspo_emit_pix:
	mov.l   @(24, r15), r7              /* r7 = r_idx */
	mov.l   @(44, r15), r3              /* r3 = ctx_hi */
	mov     r7, r0
	add     #8, r0
	mov.b   @(r0, r3), r0               /* row_has_obj[r_idx] */
	tst     r0, r0
	bt      .Lspo_bg_only

	/* Rare path (~12%): row has lower OBJ(s), call C sampler.
	 * Returns 0..0xFFFF if an OBJ bbox covered px (exact lower pixel),
	 * or 0xFFFFFFFF if no lower OBJ bbox covers px (fall through to asm BG). */
	sts.l   pr, @-r15
	mov.l   r4, @-r15
	mov     r4, r5                      /* arg1 = px */
	mov.l   @(20, r3), r4               /* arg0 = obj_ctx */
	mov     r7, r6                      /* arg2 = r_idx */
	mov.l   @(16, r3), r0               /* sample_obj_fn */
	jsr     @r0
	nop
	mov.l   @r15+, r4                   /* restore px */
	lds.l   @r15+, pr
	cmp/pz  r0                          /* >= 0 => valid u16 pixel */
	bf      1f
	bra     .Lspo_have_raw_dst
	nop
1:	mov.l   @(24, r15), r7              /* reload r_idx for .Lspo_bg_only */

	.balign 4
.Lspo_bg_only:
	mov.l   @(48, r14), r6              /* r6 = n_low_bg */
	tst     r6, r6
	bt      .Lspo_backdrop
	mov.l   @(44, r14), r5              /* r5 = &low_bg[0] */

.Lspo_bg_loop:
	mov     r7, r0
	shll2   r0
	mov.l   @(r0, r5), r2               /* r2 = sb_row0[r_idx] (offset 0) */

	mov.l   @(32, r5), r0               /* hofs */
	mov.l   @(36, r5), r1               /* hmask */
	add     r4, r0                      /* px + hofs */
	and     r1, r0                      /* r0 = vx */
	mov     r0, r3                      /* r3 = vx */
	and     #0xF8, r0
	shlr2   r0                          /* (vx & 0xF8) >> 2 */
	mov     r3, r1
	shlr8   r1
	tst     r1, r1
	bt      1f
	mov.w   .Lspo_2048, r1
	add     r1, r0
1:
	mov.w   @(r0, r2), r2               /* big-endian raw_me */

	swap.b  r2, r1
	mov     #5, r0
	shld    r0, r1
	mov.w   .Lspo_7fe0_b, r0
	and     r0, r1                      /* tid << 5 */
	mov.l   @(40, r5), r0               /* charbase */
	add     r0, r1                      /* tile_ptr */

	mov     r2, r0
	tst     #0x08, r0                   /* T = !vflip */
	mov     r7, r0
	add     #44, r0
	mov.b   @(r0, r5), r0               /* vy7 (T unchanged) */
	bt      2f
	neg     r0, r0
	add     #7, r0                      /* 7 - vy7 */
2:
	shll2   r0
	add     r0, r1                      /* tile_row_ptr */

	mov     r2, r0
	tst     #0x04, r0                   /* T = !hflip */
	mov     r3, r0                      /* vx (T unchanged) */
	and     #7, r0                      /* vx & 7 (T unchanged) */
	bt      3f
	neg     r0, r0
	add     #7, r0                      /* 7 - (vx & 7) */
3:
	mov     r0, r3                      /* r3 = tc */
	shlr    r0                          /* tc >> 1 */
	mov.b   @(r0, r1), r0               /* byte */
	mov     #1, r1
	tst     r1, r3                      /* T = !(tc & 1) */
	bt.s    4f
	extu.b  r0, r0
	shlr2   r0
	bra     5f
	shlr2   r0
4:
	and     #0x0F, r0
5:
	tst     r0, r0
	bf      .Lspo_bg_hit
	dt      r6
	bf.s    .Lspo_bg_loop
	add     #64, r5

.Lspo_backdrop:
	mov.l   @(40, r14), r1              /* pal_conv */
	bra     .Lspo_have_raw_dst
	mov.w   @r1, r0

.Lspo_bg_hit:
	mov     r0, r1                      /* bg_pix */
	mov     r2, r0                      /* raw_me */
	and     #0xF0, r0                   /* subpal */
	or      r1, r0                      /* cidx */
	add     r0, r0                      /* cidx * 2 */
	mov.l   @(40, r14), r1              /* pal_conv */
	mov.w   @(r0, r1), r0               /* pal_conv[cidx] */

.Lspo_have_raw_dst:
	mov.l   @(48, r15), r1              /* cur_pix (0..15) */
	tst     r1, r1
	bt      .Lspo_store_raw

	extu.w  r0, r0
	swap.w  r0, r2
	or      r2, r0
	and     r13, r0                     /* p2e */
	mov.l   @(44, r15), r3              /* ctx_hi */
	mov.l   @(0, r3), r2                /* evb */
	mul.l   r2, r0
	mov     r1, r0
	shll2   r0                          /* cur_pix * 4 */
	mov.l   @(8, r15), r2               /* ta */
	mov.l   @(r0, r2), r1               /* ta[cur_pix] */
	sts     macl, r0
	add     r1, r0
	shlr2   r0
	shlr2   r0                          /* pfe */
	mov.l   @(4, r3), r2                /* saturate */
	tst     r2, r2
	bt      .Lspo_no_sat
	mov.l   .Lspo_ovf_all, r2
	tst     r2, r0
	bt      .Lspo_no_sat
	CGBA_SWAR_SAT_PFE r0, r2, r2, r1
.Lspo_no_sat:
	and     r13, r0
	swap.w  r0, r2
	or      r2, r0
.Lspo_store_raw:
	mov.l   @(12, r15), r2              /* dst_row */
	mov     r4, r1
	add     r1, r1                      /* px * 2 */
	add     r1, r2
	rts
	mov.w   r0, @r2

	.balign 4
.Lspo_ovf_all:
	.long   0x08010020
.Lspo_7fe0_b:
	.word   0x7FE0
.Lspo_2048:
	.word   2048
	.size   _cgba_sparse_override_band_sh4, .-_cgba_sparse_override_band_sh4


/* ============================================================================
 * Fast same-layout 4bpp tilemap segment mergers for Mode 0 opaque compositing.
 *
 * int cgba_mode0_merge2_seg_sh4(
 *   u16 *out, const u16 *s0, const u16 *s1, u32 n, const u8 *cls_tbl)
 *   r4=out, r5=s0, r6=s1, r7=n, @(0, sp)=cls_tbl
 *
 * int cgba_mode0_merge3_seg_sh4(
 *   u16 *out, const u16 *s0, const u16 *s1, const u16 *s2,
 *   u32 n, const u8 *cls_tbl)
 *   r4=out, r5=s0, r6=s1, r7=s2, @(0, sp)=n, @(4, sp)=cls_tbl
 *
 * Returns 1 when every slot in [0..n) resolved cleanly via OPAQUE(2)/EMPTY(1)
 * classes in cls_tbl; returns 0 on UNKNOWN(0) or MIXED(3) so the C caller can
 * lazily classify unknown tiles or check MIXED-over-EMPTY.
 * ========================================================================== */
	.section .text._cgba_mode0_merge2_seg_sh4,"ax",@progbits
	.balign 4
	.global _cgba_mode0_merge2_seg_sh4
	.type   _cgba_mode0_merge2_seg_sh4, @function
_cgba_mode0_merge2_seg_sh4:
	tst     r7, r7
	bt      .Lm2_ok
	mov.l   @r15, r3                    /* r3 = cls_tbl */
	mov.w   .Lm2_3ff, r2                /* r2 = 0x03FF */

	.balign 4
.Lm2_loop:
	mov.w   @r6+, r1                    /* r1 = s1[i] */
	swap.b  r1, r0
	and     r2, r0
	mov.b   @(r0, r3), r0               /* r0 = c1 in {0,1,2,3} */
	cmp/eq  #2, r0                      /* T = (c1 == OPAQUE) */
	bt.s    .Lm2_opaque
	cmp/eq  #1, r0                      /* delay: T = (c1 == EMPTY) */
	bf      .Lm2_fail                   /* c1 == UNKNOWN or MIXED */
	mov.w   @r5+, r1                    /* c1 == EMPTY: r1 = s0[i] */
	mov.w   r1, @r4
	dt      r7
	bf.s    .Lm2_loop
	add     #2, r4
.Lm2_ok:
	rts
	mov     #1, r0

	.balign 4
.Lm2_opaque:
	mov.w   r1, @r4
	add     #2, r5
	dt      r7
	bf.s    .Lm2_loop
	add     #2, r4
	rts
	mov     #1, r0

.Lm2_fail:
	rts
	mov     #0, r0

	.balign 2
.Lm2_3ff:
	.word   0x03FF
	.size   _cgba_mode0_merge2_seg_sh4, .-_cgba_mode0_merge2_seg_sh4

	.section .text._cgba_mode0_merge3_seg_sh4,"ax",@progbits
	.balign 4
	.global _cgba_mode0_merge3_seg_sh4
	.type   _cgba_mode0_merge3_seg_sh4, @function
_cgba_mode0_merge3_seg_sh4:
	mov.l   @r15, r0                    /* r0 = n */
	tst     r0, r0
	bt      .Lm3_ok_early
	mov.l   @(4, r15), r3               /* r3 = cls_tbl */
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov     r0, r8                      /* r8 = n */
	mov.w   .Lm3_3ff, r9                /* r9 = 0x03FF */

	.balign 4
.Lm3_loop:
	mov.w   @r7+, r2                    /* r2 = s2[i] (top) */
	swap.b  r2, r0
	and     r9, r0
	mov.b   @(r0, r3), r0               /* r0 = c2 */
	cmp/eq  #2, r0                      /* T = (c2 == OPAQUE) */
	bt.s    .Lm3_c2_op
	cmp/eq  #1, r0                      /* delay: T = (c2 == EMPTY) */
	bf      .Lm3_fail                   /* c2 == UNKNOWN or MIXED */

	mov.w   @r6+, r1                    /* r1 = s1[i] (mid) */
	swap.b  r1, r0
	and     r9, r0
	mov.b   @(r0, r3), r0               /* r0 = c1 */
	cmp/eq  #2, r0                      /* T = (c1 == OPAQUE) */
	bt.s    .Lm3_c1_op
	cmp/eq  #1, r0                      /* delay: T = (c1 == EMPTY) */
	bf      .Lm3_fail                   /* c1 == UNKNOWN or MIXED */

	mov.w   @r5+, r1                    /* r1 = s0[i] (bot) */
	mov.w   r1, @r4
	dt      r8
	bf.s    .Lm3_loop
	add     #2, r4
	bra     .Lm3_ok
	nop

	.balign 4
.Lm3_c1_op:
	mov.w   r1, @r4
	add     #2, r5
	dt      r8
	bf.s    .Lm3_loop
	add     #2, r4
	bra     .Lm3_ok
	nop

	.balign 4
.Lm3_c2_op:
	mov.w   r2, @r4
	add     #2, r6
	add     #2, r5
	dt      r8
	bf.s    .Lm3_loop
	add     #2, r4
.Lm3_ok:
	mov     #1, r0
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lm3_fail:
	mov     #0, r0
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lm3_ok_early:
	rts
	mov     #1, r0

	.balign 2
.Lm3_3ff:
	.word   0x03FF
	.size   _cgba_mode0_merge3_seg_sh4, .-_cgba_mode0_merge3_seg_sh4

/* ============================================================================
 * _cgba_render_affine_bg_scale_sh4
 *   Fast FULLCOLOR non-mosaic affine BG scanline (dy == 0).
 *   r4 = const cgba_aff_bg_params *p:
 *     +0  dst_ptr       (u16 *)
 *     +4  map_base      (const u8 *)
 *     +8  tile_base     (const u8 *)
 *     +12 pal           (const u16 *, pal[0] = backdrop)
 *     +16 source_x      (s32)
 *     +20 source_y      (s32)
 *     +24 dx            (s32)
 *     +28 dy            (s32, == 0)
 *     +32 cnt           (u32, >= 1)
 *     +36 width_height  (u32, 128/256/512/1024)
 *     +40 map_pitch     (u32, 4/5/6/7)
 *     +44 flags         (u32: bit0 = isbase, bit1 = wrap)
 * ========================================================================== */
	.section .text._cgba_render_affine_bg_scale_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_affine_bg_scale_sh4
	.type   _cgba_render_affine_bg_scale_sh4, @function
_cgba_render_affine_bg_scale_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	mov     r4, r0
	mov.l   @(0, r0), r4                /* r4  = dst_ptr */
	mov.l   @(4, r0), r8                /* r8  = map_base */
	mov.l   @(8, r0), r9                /* r9  = tile_base */
	mov.l   @(12, r0), r10              /* r10 = pal */
	mov.l   @(16, r0), r5               /* r5  = source_x */
	mov.l   @(20, r0), r1               /* r1  = source_y */
	mov.l   @(24, r0), r6               /* r6  = dx */
	mov.l   @(32, r0), r7               /* r7  = cnt */
	mov.l   @(36, r0), r11              /* r11 = width_height */
	mov.l   @(40, r0), r2               /* r2  = map_pitch */
	mov.l   @(44, r0), r3               /* r3  = flags */

	mov     #-8, r12                    /* r12 = -8 (shad >> 8) */
	shad    r12, r1                     /* r1  = py = source_y >> 8 */

	mov     r3, r0
	tst     #2, r0                      /* T = !wrap */
	bt      .Labgs_nowrap

	/* ---- wrap == true ---- */
	add     #-1, r11                    /* r11 = mask = width_height - 1 */
	and     r11, r1                     /* py &= mask */
	mov     r1, r0
	shlr2   r0
	shlr    r0
	shld    r2, r0                      /* (py >> 3) << map_pitch */
	add     r0, r8                      /* r8  = map_row */
	mov     r1, r0
	and     #7, r0
	shll2   r0
	add     r0, r0                      /* (py & 7) << 3 */
	add     r0, r9                      /* r9  = tile_row_base */
	mov     #-1, r13                    /* r13 = cur_tx sentinel */

	mov     r3, r0
	tst     #1, r0                      /* T = !isbase */
	bt.s    .Labgs_wrap_over
	mov     #7, r3                      /* r3  = 7 mask */

	.balign 4
.Labgs_wb_loop:
	mov     r5, r0
	shlr8   r0
	and     r11, r0                     /* r0  = pix_x */
	mov     r0, r2
	and     r3, r2                      /* r2  = pix_x & 7 */
	shlr2   r0
	shlr    r0                          /* r0  = tx */
	cmp/eq  r13, r0
	bt.s    1f
	add     r6, r5                      /* delay: source_x += dx */
	mov     r0, r13
	mov.b   @(r0, r8), r1               /* map_row[tx] */
	extu.b  r1, r1
	shll2   r1
	shll2   r1
	shll2   r1                          /* tile_idx << 6 */
	mov     r9, r14
	add     r1, r14                     /* r14 = tile_row_base + tile_idx*64 */
1:	mov     r2, r0
	mov.b   @(r0, r14), r0              /* pval */
	extu.b  r0, r0
	shll    r0
	mov.w   @(r0, r10), r0              /* pal[pval] (pal[0] for transparent) */
	mov.w   r0, @r4
	dt      r7
	bf.s    .Labgs_wb_loop
	add     #2, r4
	bra     .Labgs_done
	nop

	.balign 4
.Labgs_wrap_over:
.Labgs_wo_loop:
	mov     r5, r0
	shlr8   r0
	and     r11, r0                     /* r0  = pix_x */
	mov     r0, r2
	and     r3, r2                      /* r2  = pix_x & 7 */
	shlr2   r0
	shlr    r0                          /* r0  = tx */
	cmp/eq  r13, r0
	bt.s    1f
	add     r6, r5                      /* delay: source_x += dx */
	mov     r0, r13
	mov.b   @(r0, r8), r1
	extu.b  r1, r1
	shll2   r1
	shll2   r1
	shll2   r1
	mov     r9, r14
	add     r1, r14
1:	mov     r2, r0
	mov.b   @(r0, r14), r0
	extu.b  r0, r0
	tst     r0, r0
	bt      2f
	shll    r0
	mov.w   @(r0, r10), r0
	mov.w   r0, @r4
2:	dt      r7
	bf.s    .Labgs_wo_loop
	add     #2, r4
	bra     .Labgs_done
	nop

	/* ---- wrap == false ---- */
	.balign 4
.Labgs_nowrap:
	cmp/hs  r11, r1                     /* T = ((u32)py >= width_height) */
	bt      .Labgs_oob_y
	mov     r1, r0
	shlr2   r0
	shlr    r0
	shld    r2, r0                      /* (py >> 3) << map_pitch */
	add     r0, r8                      /* r8  = map_row */
	mov     r1, r0
	and     #7, r0
	shll2   r0
	add     r0, r0                      /* (py & 7) << 3 */
	add     r0, r9                      /* r9  = tile_row_base */
	mov     #-1, r13                    /* cur_tx sentinel */

	mov     r3, r0
	tst     #1, r0                      /* T = !isbase */
	bt.s    .Labgs_nwo_skip
	mov     #7, r3                      /* r3  = 7 mask */

	/* nowrap + isbase: leading OOB fills pal[0] */
	mov.w   @r10, r14                   /* r14 = pal[0] (backdrop) */
	.balign 4
.Labgs_nwb_skip:
	mov     r5, r0
	shad    r12, r0
	cmp/hs  r11, r0
	bf      .Labgs_nwb_draw
	mov.w   r14, @r4
	add     #2, r4
	dt      r7
	bf.s    .Labgs_nwb_skip
	add     r6, r5
	bra     .Labgs_done
	nop

	.balign 4
.Labgs_nwb_draw:
	/* r0 = valid pix_x (< width_height) */
	mov     r0, r2
	and     r3, r2                      /* r2  = pix_x & 7 */
	shlr2   r0
	shlr    r0                          /* r0  = tx */
	cmp/eq  r13, r0
	bt.s    1f
	add     r6, r5                      /* delay: source_x += dx */
	mov     r0, r13
	mov.b   @(r0, r8), r1
	extu.b  r1, r1
	shll2   r1
	shll2   r1
	shll2   r1
	mov     r9, r14
	add     r1, r14
1:	mov     r2, r0
	mov.b   @(r0, r14), r0
	extu.b  r0, r0
	shll    r0
	mov.w   @(r0, r10), r0
	mov.w   r0, @r4
	add     #2, r4
	dt      r7
	bt      .Labgs_done
	mov     r5, r0
	shad    r12, r0
	cmp/hs  r11, r0
	bf      .Labgs_nwb_draw

	/* Trailing OOB fill for isbase (r7 >= 1) */
.Labgs_fill_bd:
	mov.w   @r10, r0
.Labgs_fill_bd_loop:
	mov.w   r0, @r4
	dt      r7
	bf.s    .Labgs_fill_bd_loop
	add     #2, r4
	bra     .Labgs_done
	nop

.Labgs_oob_y:
	mov     r3, r0
	tst     #1, r0
	bf      .Labgs_fill_bd
	bra     .Labgs_done
	nop

	/* nowrap + !isbase: leading OOB skips */
	.balign 4
.Labgs_nwo_skip:
	mov     r5, r0
	shad    r12, r0
	cmp/hs  r11, r0
	bf      .Labgs_nwo_draw
	add     #2, r4
	dt      r7
	bf.s    .Labgs_nwo_skip
	add     r6, r5
	bra     .Labgs_done
	nop

	.balign 4
.Labgs_nwo_draw:
	mov     r0, r2
	and     r3, r2
	shlr2   r0
	shlr    r0
	cmp/eq  r13, r0
	bt.s    1f
	add     r6, r5
	mov     r0, r13
	mov.b   @(r0, r8), r1
	extu.b  r1, r1
	shll2   r1
	shll2   r1
	shll2   r1
	mov     r9, r14
	add     r1, r14
1:	mov     r2, r0
	mov.b   @(r0, r14), r0
	extu.b  r0, r0
	tst     r0, r0
	bt      2f
	shll    r0
	mov.w   @(r0, r10), r0
	mov.w   r0, @r4
2:	add     #2, r4
	dt      r7
	bt      .Labgs_done
	mov     r5, r0
	shad    r12, r0
	cmp/hs  r11, r0
	bf      .Labgs_nwo_draw

.Labgs_done:
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8
	.size   _cgba_render_affine_bg_scale_sh4, .-_cgba_render_affine_bg_scale_sh4

/* ============================================================================
 * _cgba_render_affine_bg_rot_u32_sh4
 *   2D DDA affine BG renderer for u32 INDXCOLOR and base STCKCOLOR (dy != 0,
 *   no mosaic). Accelerates Mode 1/2 affine layers under active BLDCNT.
 *   p->dst_ptr      = u32 *dst
 *   p->map_base     = const u8 *map_base
 *   p->tile_base    = const u8 *tile_base
 *   p->pal          = (const u16 *)(uintptr_t)px_comb_eff
 *   p->source_x..dy = 24.8 fixed point
 *   p->cnt          = pixel count (>= 1)
 *   p->width_height = 128/256/512/1024
 *   p->map_pitch    = map_size + 4 (4..7)
 *   p->flags        = bit0=isbase, bit1=wrap, bits16..31=bg_comb
 * ========================================================================== */
	.section .text._cgba_render_affine_bg_rot_u32_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_affine_bg_rot_u32_sh4
	.type   _cgba_render_affine_bg_rot_u32_sh4, @function
_cgba_render_affine_bg_rot_u32_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	mov     r4, r0
	mov.l   @(0, r0), r4                /* r4  = u32 *dst */
	mov.l   @(4, r0), r5                /* r5  = map_base */
	mov.l   @(8, r0), r11               /* r11 = tile_base */
	mov.l   @(12, r0), r12              /* r12 = px_comb_eff */
	mov.l   @(16, r0), r6               /* r6  = sx */
	mov.l   @(20, r0), r7               /* r7  = sy */
	mov.l   @(24, r0), r8               /* r8  = dx */
	mov.l   @(28, r0), r9               /* r9  = dy */
	mov.l   @(32, r0), r10              /* r10 = cnt */
	mov.l   @(36, r0), r13              /* r13 = width_height */
	mov.l   @(40, r0), r14              /* r14 = map_pitch (4..7) */
	mov.l   @(44, r0), r2               /* r2  = flags | (bg_comb << 16) */

	add     #-3, r14                    /* r14 = map_size + 1 (1..4) */
	mov     r2, r0
	shlr16  r0
	mov.l   r0, @-r15                   /* @(0, sp) = bg_comb */

	mov     r2, r0
	tst     #2, r0                      /* T = !wrap */
	bt      .Labgru_nowrap

	/* ---- wrap == true ---- */
	add     #-1, r13                    /* r13 = mask = width_height - 1 */
	mov     r2, r0
	tst     #1, r0                      /* T = !isbase */
	bt      .Labgru_wo_loop

	.balign 4
.Labgru_wb_loop:
	mov     r7, r0
	shlr8   r0
	and     r13, r0                     /* py */
	mov     r0, r3
	and     #7, r0
	sub     r0, r3                      /* py & ~7 */
	shll2   r0
	add     r0, r0
	mov     r0, r1                      /* (py & 7) << 3 */
	mov     r6, r0
	shlr8   r0
	and     r13, r0                     /* px */
	mov     r0, r2
	and     #7, r0
	add     r0, r1                      /* intra-tile offset */
	shld    r14, r3                     /* (py >> 3) << map_pitch */
	shlr2   r2
	shlr    r2
	mov     r2, r0
	add     r3, r0
	mov.b   @(r0, r5), r0
	add     r8, r6
	extu.b  r0, r0
	shll8   r0
	shlr2   r0
	add     r1, r0
	mov.b   @(r0, r11), r0
	add     r9, r7
	tst     r0, r0
	bt.s    1f
	dt      r10                         /* T = (--cnt == 0), preserved below */
	extu.b  r0, r0
	bra     2f
	or      r12, r0
1:	mov.l   @r15, r0                    /* bg_comb */
2:	mov.l   r0, @r4
	bf.s    .Labgru_wb_loop
	add     #4, r4
	bra     .Labgru_done
	nop

	.balign 4
.Labgru_wo_loop:
	mov     r7, r0
	shlr8   r0
	and     r13, r0                     /* py */
	mov     r0, r3
	and     #7, r0
	sub     r0, r3
	shll2   r0
	add     r0, r0
	mov     r0, r1
	mov     r6, r0
	shlr8   r0
	and     r13, r0                     /* px */
	mov     r0, r2
	and     #7, r0
	add     r0, r1
	shld    r14, r3
	shlr2   r2
	shlr    r2
	mov     r2, r0
	add     r3, r0
	mov.b   @(r0, r5), r0
	add     r8, r6
	extu.b  r0, r0
	shll8   r0
	shlr2   r0
	add     r1, r0
	mov.b   @(r0, r11), r0
	add     r9, r7
	tst     r0, r0
	bt.s    1f
	dt      r10
	extu.b  r0, r0
	or      r12, r0
	mov.l   r0, @r4
1:	bf.s    .Labgru_wo_loop
	add     #4, r4
	bra     .Labgru_done
	nop

	/* ---- wrap == false ---- */
	.balign 4
.Labgru_nowrap:
	mov     r2, r0
	tst     #1, r0                      /* T = !isbase */
	bf      .Labgru_nwb_skip
	bra     .Labgru_nwo_skip
	nop

	/* nowrap + isbase: leading OOB fills bg_comb */
	.balign 4
.Labgru_nwb_skip:
	mov     r6, r2
	shlr8   r2
	mov     r7, r0
	shlr8   r0
	mov     r2, r1
	or      r0, r1
	cmp/hs  r13, r1
	bf      .Labgru_nwb_check
	mov.l   @r15, r0
	mov.l   r0, @r4
	add     #4, r4
	dt      r10
	bt      .Labgru_done_mid
	add     r8, r6
	bra     .Labgru_nwb_skip
	add     r9, r7

.Labgru_done_mid:
	bra     .Labgru_done
	nop

.Labgru_nwb_check:
	mov     r10, r1
	add     #-1, r1
	mul.l   r8, r1
	sts     macl, r3
	add     r6, r3
	shlr8   r3
	mul.l   r9, r1
	sts     macl, r1
	add     r7, r1
	shlr8   r1
	or      r3, r1
	cmp/hs  r13, r1
	bt      .Labgru_nwb_slow
	mov     #-3, r13

	.balign 4
.Labgru_nwb_fast:
	mov     r7, r0
	shlr8   r0
	mov     r0, r3
	and     #7, r0
	sub     r0, r3
	shll2   r0
	add     r0, r0
	mov     r0, r1
	mov     r6, r0
	shlr8   r0
	mov     r0, r2
	and     #7, r0
	add     r0, r1
	shld    r14, r3
	shld    r13, r2
	mov     r2, r0
	add     r3, r0
	mov.b   @(r0, r5), r0
	add     r8, r6
	extu.b  r0, r0
	shll8   r0
	shlr2   r0
	add     r1, r0
	mov.b   @(r0, r11), r0
	add     r9, r7
	tst     r0, r0
	bt.s    1f
	dt      r10
	extu.b  r0, r0
	bra     2f
	or      r12, r0
1:	mov.l   @r15, r0
2:	mov.l   r0, @r4
	bf.s    .Labgru_nwb_fast
	add     #4, r4
	bra     .Labgru_done
	nop

	.balign 4
.Labgru_nwb_slow:
	mov     r0, r3
	and     #7, r0
	sub     r0, r3
	shll2   r0
	add     r0, r0
	mov     r0, r1
	mov     r2, r0
	and     #7, r0
	add     r0, r1
	shld    r14, r3
	shlr2   r2
	shlr    r2
	mov     r2, r0
	add     r3, r0
	mov.b   @(r0, r5), r0
	add     r8, r6
	extu.b  r0, r0
	shll8   r0
	shlr2   r0
	add     r1, r0
	mov.b   @(r0, r11), r0
	add     r9, r7
	tst     r0, r0
	bt      1f
	extu.b  r0, r0
	bra     2f
	or      r12, r0
1:	mov.l   @r15, r0
2:	mov.l   r0, @r4
	add     #4, r4
	dt      r10
	bt      .Labgru_done_mid
	mov     r6, r2
	shlr8   r2
	mov     r7, r0
	shlr8   r0
	mov     r2, r1
	or      r0, r1
	cmp/hs  r13, r1
	bf      .Labgru_nwb_slow

.Labgru_fill_bd:
	mov.l   @r15, r0
.Labgru_fill_bd_loop:
	mov.l   r0, @r4
	dt      r10
	bf.s    .Labgru_fill_bd_loop
	add     #4, r4
	bra     .Labgru_done
	nop

	/* nowrap + !isbase: leading OOB skips */
	.balign 4
.Labgru_nwo_skip:
	mov     r6, r2
	shlr8   r2
	mov     r7, r0
	shlr8   r0
	mov     r2, r1
	or      r0, r1
	cmp/hs  r13, r1
	bf      .Labgru_nwo_check
	add     #4, r4
	dt      r10
	bt      .Labgru_done
	add     r8, r6
	bra     .Labgru_nwo_skip
	add     r9, r7

.Labgru_nwo_check:
	mov     r10, r1
	add     #-1, r1
	mul.l   r8, r1
	sts     macl, r3
	add     r6, r3
	shlr8   r3
	mul.l   r9, r1
	sts     macl, r1
	add     r7, r1
	shlr8   r1
	or      r3, r1
	cmp/hs  r13, r1
	bt      .Labgru_nwo_slow
	mov     #-3, r13

	.balign 4
.Labgru_nwo_fast:
	mov     r7, r0
	shlr8   r0
	mov     r0, r3
	and     #7, r0
	sub     r0, r3
	shll2   r0
	add     r0, r0
	mov     r0, r1
	mov     r6, r0
	shlr8   r0
	mov     r0, r2
	and     #7, r0
	add     r0, r1
	shld    r14, r3
	shld    r13, r2
	mov     r2, r0
	add     r3, r0
	mov.b   @(r0, r5), r0
	add     r8, r6
	extu.b  r0, r0
	shll8   r0
	shlr2   r0
	add     r1, r0
	mov.b   @(r0, r11), r0
	add     r9, r7
	tst     r0, r0
	bt.s    1f
	dt      r10
	extu.b  r0, r0
	or      r12, r0
	mov.l   r0, @r4
1:	bf.s    .Labgru_nwo_fast
	add     #4, r4
	bra     .Labgru_done
	nop

	.balign 4
.Labgru_nwo_slow:
	mov     r0, r3
	and     #7, r0
	sub     r0, r3
	shll2   r0
	add     r0, r0
	mov     r0, r1
	mov     r2, r0
	and     #7, r0
	add     r0, r1
	shld    r14, r3
	shlr2   r2
	shlr    r2
	mov     r2, r0
	add     r3, r0
	mov.b   @(r0, r5), r0
	add     r8, r6
	extu.b  r0, r0
	shll8   r0
	shlr2   r0
	add     r1, r0
	mov.b   @(r0, r11), r0
	add     r9, r7
	tst     r0, r0
	bt      2f
	extu.b  r0, r0
	or      r12, r0
	mov.l   r0, @r4
2:	add     #4, r4
	dt      r10
	bt      .Labgru_done
	mov     r6, r2
	shlr8   r2
	mov     r7, r0
	shlr8   r0
	mov     r2, r1
	or      r0, r1
	cmp/hs  r13, r1
	bf      .Labgru_nwo_slow

.Labgru_done:
	add     #4, r15
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8
	.size   _cgba_render_affine_bg_rot_u32_sh4, .-_cgba_render_affine_bg_rot_u32_sh4

/* ============================================================================
 * _cgba_render_obj_4bpp_tiles_blend_sh4
 *   Direct RGB565 alpha-blending 4bpp OBJ tile row renderer (no VRAM wrap).
 *   void cgba_render_obj_4bpp_tiles_blend_sh4(
 *     const u8 *tile_ptr, u32 num_tiles, u16 *dst_ptr,
 *     const u32 *sub_term_a, u32 evb_and_flags)
 *     r4 = tile_ptr
 *     r5 = num_tiles (>= 1)
 *     r6 = dst_ptr
 *     r7 = sub_term_a (16 u32 entries: expanded(pal[i]) * eva)
 *     @(0, sp) = evb_and_flags: bits 0..4 = evb (0..16), bit 8 = hflip, bit 9 = sat
 * ========================================================================== */
	.section .text._cgba_render_obj_4bpp_tiles_blend_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_obj_4bpp_tiles_blend_sh4
	.type   _cgba_render_obj_4bpp_tiles_blend_sh4, @function
_cgba_render_obj_4bpp_tiles_blend_sh4:
	mov.l   @r15, r0                    /* evb_and_flags */
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15
	sts.l   pr, @-r15

	mov     r0, r1
	and     #0x1F, r0
	mov     r0, r11                     /* r11 = evb (0..16) */
	mov.l   .Lobjb_blnd_msk, r12        /* r12 = 0x07E0F81F */
	mov     #0, r13                     /* r13 = 0 (no saturate) */
	shlr8   r1                          /* bit 0 = hflip, bit 1 = sat */
	mov     r1, r0
	tst     #2, r0
	bt      1f
	mov.l   .Lobjb_ovf_all, r13         /* r13 = 0x08010020 (saturate mask) */
1:	mov     r1, r0
	tst     #1, r0                      /* T = !hflip */
	bt      .Lobjb_norm_loop

.macro CGBA_OBJB_PIX reg, shift_next, disp, next_lbl
	mov     \reg, r0
	and     #0x3C, r0
	tst     r0, r0
.if \shift_next
	bt.s    \next_lbl
	shlr8   \reg
.else
	bt      \next_lbl
.endif
.if \disp == 0
	bsr     .Lobjb_blend_pix
	mov     r6, r1
.else
	mov     r6, r1
	bsr     .Lobjb_blend_pix
	add     #\disp, r1
.endif
.endm

.macro CGBA_OBJB_TILE_LOOP pfx, hflip
	.balign 4
\pfx\()_loop:
	mov.l   @r4, r9                     /* [b0 | b1 | b2 | b3] */
	tst     r9, r9
	bt      \pfx\()_next
	mov     r9, r10
	shlr2   r10                         /* odd nibbles << 2 */
	add     r9, r9
	add     r9, r9                      /* even nibbles << 2 */
.if \hflip
	CGBA_OBJB_PIX r9,  1,  2, 1f
1:	CGBA_OBJB_PIX r9,  1,  6, 2f
2:	CGBA_OBJB_PIX r9,  1, 10, 3f
3:	CGBA_OBJB_PIX r9,  0, 14, 4f
4:	CGBA_OBJB_PIX r10, 1,  0, 5f
5:	CGBA_OBJB_PIX r10, 1,  4, 6f
6:	CGBA_OBJB_PIX r10, 1,  8, 7f
7:	CGBA_OBJB_PIX r10, 0, 12, \pfx\()_next
.else
	CGBA_OBJB_PIX r9,  1, 12, 1f
1:	CGBA_OBJB_PIX r9,  1,  8, 2f
2:	CGBA_OBJB_PIX r9,  1,  4, 3f
3:	CGBA_OBJB_PIX r9,  0,  0, 4f
4:	CGBA_OBJB_PIX r10, 1, 14, 5f
5:	CGBA_OBJB_PIX r10, 1, 10, 6f
6:	CGBA_OBJB_PIX r10, 1,  6, 7f
7:	CGBA_OBJB_PIX r10, 0,  2, \pfx\()_next
.endif
\pfx\()_next:
.if \hflip
	add     #-32, r4
.else
	add     #32, r4
.endif
	dt      r5
	bf.s    \pfx\()_loop
	add     #16, r6
.endm

	/* ---- hflip == true ---- */
	CGBA_OBJB_TILE_LOOP .Lobjb_hf, 1
	bra     .Lobjb_done
	nop

	/* ---- hflip == false ---- */
	CGBA_OBJB_TILE_LOOP .Lobjb_norm, 0
	.purgem CGBA_OBJB_PIX
	.purgem CGBA_OBJB_TILE_LOOP

.Lobjb_done:
	lds.l   @r15+, pr
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	/* Internal helper: blend non-zero nibble (r0 = pix<<2) onto u16 *@r1 */
	.balign 4
.Lobjb_blend_pix:
	mov.l   @(r0, r7), r3               /* term_a[pix] */
	mov.w   @r1, r0
	extu.w  r0, r0
	swap.w  r0, r8
	or      r8, r0
	and     r12, r0                     /* p2e */
	mul.l   r11, r0
	sts     macl, r0
	add     r3, r0
	shlr2   r0
	shlr2   r0                          /* pfe */
	tst     r13, r0
	bf      .Lobjb_sat
.Lobjb_sat_ret:
	and     r12, r0
	swap.w  r0, r8
	or      r8, r0
	rts
	mov.w   r0, @r1

.Lobjb_sat:
	CGBA_SWAR_SAT_PFE r0, r13, r8, r3
	and     r12, r0
	swap.w  r0, r8
	or      r8, r0
	rts
	mov.w   r0, @r1

	.balign 4
.Lobjb_blnd_msk: .long 0x07E0F81F
.Lobjb_ovf_all:  .long 0x08010020
	.size   _cgba_render_obj_4bpp_tiles_blend_sh4, .-_cgba_render_obj_4bpp_tiles_blend_sh4

/* ============================================================================
 * _cgba_merge_brightness_sh4
 *   Fast SH-4 RGB565 merge_brightness for INDXCOLOR scanline spans.
 *   void cgba_merge_brightness_sh4(
 *     u16 *srcdst, u32 count, const u16 *pal_conv, u32 pa, u32 inv_evy)
 *     r4 = srcdst, r5 = count, r6 = pal_conv, r7 = pa, @(0, sp) = inv_evy
 * ========================================================================== */
	.section .text._cgba_merge_brightness_sh4,"ax",@progbits
	.balign 4
	.global _cgba_merge_brightness_sh4
	.type   _cgba_merge_brightness_sh4, @function
_cgba_merge_brightness_sh4:
	tst     r5, r5
	bt      .Lmbri_early
	mov.l   @r15, r0                    /* r0  = inv_evy = 16 - brightness */
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15

	mov     r0, r8                      /* r8  = inv_evy */
	mov     #-1, r9                     /* r9  = prev_spix sentinel */
	mov     #0, r10                     /* r10 = prev_out */
	mov.l   .Lmbri_blnd_msk, r11        /* r11 = 0x07E0F81F */
	mov.w   .Lmbri_1ff, r12             /* r12 = 0x01FF */
	mov.w   .Lmbri_200, r13             /* r13 = 0x0200 */

	.balign 4
.Lmbri_loop:
	mov.w   @r4, r1
	extu.w  r1, r1
	cmp/eq  r9, r1
	bt      .Lmbri_hit
	mov     r1, r9
	mov     r1, r0
	and     r12, r0
	shll    r0
	mov.w   @(r0, r6), r0               /* pixcol */
	tst     r13, r1                     /* T = !(spix & 0x200) */
	bt.s    .Lmbri_store
	extu.w  r0, r0
	swap.w  r0, r2
	or      r2, r0
	and     r11, r0                     /* epixel */
	mul.l   r8, r0                      /* epixel * inv_evy */
	sts     macl, r0
	shlr2   r0
	shlr2   r0
	and     r11, r0                     /* pb */
	add     r7, r0                      /* pa + pb */
	and     r11, r0
	swap.w  r0, r2
	or      r2, r0
.Lmbri_store:
	mov     r0, r10
.Lmbri_hit:
	mov.w   r10, @r4
	dt      r5
	bf.s    .Lmbri_loop
	add     #2, r4

	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lmbri_early:
	rts
	nop

	.balign 4
.Lmbri_blnd_msk: .long 0x07E0F81F
.Lmbri_1ff:      .word 0x01FF
.Lmbri_200:      .word 0x0200
	.size   _cgba_merge_brightness_sh4, .-_cgba_merge_brightness_sh4

/* ============================================================================
 * _cgba_merge_blend_sh4
 *   Fast SH-4 RGB565 merge_blend for STCKCOLOR scanline spans.
 *   void cgba_merge_blend_sh4(
 *     u16 *dst, const u32 *src, u32 count, const u16 *pal_conv,
 *     const cgba_merge_blend_params *p)
 *     r4 = dst, r5 = src, r6 = count, r7 = pal_conv, @(0, sp) = p:
 *       +0  blend_a        (0..16)
 *       +4  blend_b        (0..16)
 *       +8  top_blend_bits ((st_objs ? 0x800 : 0) | (BLEND_ONLY ? 0x200 : 0))
 *       +12 sat_mask       ((a+b > 16) ? 0x08010020 : 0)
 *       +16 bright_mask    ((DARK|BRIGHT) ? 0x200 : 0)
 *       +20 bright_pa      (BRIGHT ? ((BLND_MSK*brightf)>>4)&BLND_MSK : 0)
 *       +24 bright_inv     (16 - brightf)
 * ========================================================================== */
	.section .text._cgba_merge_blend_sh4,"ax",@progbits
	.balign 4
	.global _cgba_merge_blend_sh4
	.type   _cgba_merge_blend_sh4, @function
_cgba_merge_blend_sh4:
	tst     r6, r6
	bt      .Lmbl_early
	mov.l   @r15, r0                    /* r0  = p */
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	mov     r0, r8                      /* r8  = p */
	mov     #-1, r9                     /* r9  = prev_pair sentinel */
	mov     #0, r10                     /* r10 = prev_out */
	mov.l   .Lmbl_blnd_msk, r11         /* r11 = 0x07E0F81F */
	mov.w   .Lmbl_1ff, r12              /* r12 = 0x01FF */
	mov.l   @(8, r8), r13               /* r13 = top_blend_bits */
	mov.l   .Lmbl_tgt2_bit, r14         /* r14 = 0x04000000 */

	.balign 4
.Lmbl_loop:
	mov.l   @r5+, r1                    /* r1  = pixpair */
	cmp/eq  r9, r1
	bt      .Lmbl_hit
	mov     r1, r9
	tst     r14, r1                     /* bottom is 2nd target? */
	bt      .Lmbl_no_alpha
	tst     r13, r1                     /* top is ST-obj or 1st target? */
	bt      .Lmbl_no_alpha

	/* ---- Alpha blend top (p1) + bottom (p2) ---- */
	mov     r1, r0
	and     r12, r0
	shll    r0
	mov.w   @(r0, r7), r0               /* p1 */
	extu.w  r0, r0
	swap.w  r0, r2
	or      r2, r0
	and     r11, r0                     /* p1e */
	mov.l   @(0, r8), r2                /* blend_a */
	mul.l   r2, r0
	sts     macl, r3                    /* p1e * blend_a */

	mov     r1, r0
	shlr16  r0
	and     r12, r0
	shll    r0
	mov.w   @(r0, r7), r0               /* p2 */
	extu.w  r0, r0
	swap.w  r0, r2
	or      r2, r0
	and     r11, r0                     /* p2e */
	mov.l   @(4, r8), r2                /* blend_b */
	mul.l   r2, r0
	sts     macl, r0                    /* p2e * blend_b */
	add     r3, r0
	shlr2   r0
	shlr2   r0                          /* pfe */

	mov.l   @(12, r8), r2               /* sat_mask (0 when a+b <= 16) */
	tst     r2, r0
	bt      .Lmbl_sat_ret
	CGBA_SWAR_SAT_PFE r0, r2, r2, r3
.Lmbl_sat_ret:
	and     r11, r0
	swap.w  r0, r2
	or      r2, r0
	mov     r0, r10
	bra     .Lmbl_hit
	nop

	.balign 4
.Lmbl_no_alpha:
	mov     r1, r0
	and     r12, r0
	shll    r0
	mov.w   @(r0, r7), r0               /* pal[pixpair & 0x1FF] */
	mov.l   @(16, r8), r2               /* bright_mask (0 or 0x200) */
	tst     r2, r1
	bt.s    .Lmbl_store
	extu.w  r0, r0
	swap.w  r0, r2
	or      r2, r0
	and     r11, r0                     /* epixel */
	mov.l   @(24, r8), r2               /* bright_inv = 16 - brightf */
	mul.l   r2, r0
	sts     macl, r0
	shlr2   r0
	shlr2   r0
	and     r11, r0                     /* pb */
	mov.l   @(20, r8), r2               /* bright_pa */
	add     r2, r0
	and     r11, r0
	swap.w  r0, r2
	or      r2, r0
.Lmbl_store:
	mov     r0, r10
.Lmbl_hit:
	mov.w   r10, @r4
	dt      r6
	bf.s    .Lmbl_loop
	add     #2, r4

	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lmbl_early:
	rts
	nop

	.balign 4
.Lmbl_blnd_msk: .long 0x07E0F81F
.Lmbl_tgt2_bit: .long 0x04000000
.Lmbl_1ff:      .word 0x01FF
	.size   _cgba_merge_blend_sh4, .-_cgba_merge_blend_sh4

/* ============================================================================
 * _cgba_render_8bpp_strip_base_sh4
 *   Tile-major SH-4 big-endian 8-bpp FULLCOLOR base strip renderer (k = 1..8
 *   rows per tile).  Accepts any 256-entry RGB565 LUT (either pal_conv for
 *   normal 8-bpp base or cgba_bg_blend_bd_pal for backdrop-folded 8-bpp
 *   alpha-blend base).  Uses 32-bit mov.l paired stores when dst_top is
 *   4-byte aligned (even HOFS), and 16-bit mov.w stores otherwise.
 *
 *   void cgba_render_8bpp_strip_base_sh4(
 *     const u16 *map1, u32 counts_packed, u16 *dst_top,
 *     const u8 *tile_charbase, u32 start_r_and_k,
 *     const u16 *pal256, const u16 *map2)
 * ========================================================================== */
	.section .text._cgba_render_8bpp_strip_base_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_8bpp_strip_base_sh4
	.type   _cgba_render_8bpp_strip_base_sh4, @function
_cgba_render_8bpp_strip_base_sh4:
	tst     r5, r5
	bf      1f
	rts
	nop
1:	mov.l   @(0, r15), r0               /* r0 = (start_r << 8) | k */
	mov.l   @(4, r15), r1               /* r1 = pal256 */
	mov.l   @(8, r15), r2               /* r2 = map2 */
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15
	add     #-12, r15                   /* +0=k, +4=todraw2, +8=map2 */

	mov     r1, r10                     /* r10 = pal256 */
	mov.l   r2, @(8, r15)               /* save map2 */
	mov     r0, r3
	extu.b  r3, r3                      /* r3 = k (1..8) */
	mov.l   r3, @(0, r15)
	shlr8   r0
	extu.b  r0, r0                      /* r0 = start_r (0..7) */
	mov     r0, r1
	shll2   r1
	shll    r1                          /* r1 = start_r << 3 */
	add     r1, r7                      /* r7 = tb_norm = tile_charbase + start_r*8 */
	shll    r1                          /* r1 = start_r << 4 */
	mov     #56, r9
	sub     r1, r9                      /* r9 = vflip_off = 56 - start_r*16 */
	mov.w   .L8b_stride_a, r13          /* r13 = 480 */

	mov     r5, r1
	shlr16  r1                          /* r1 = todraw2 */
	mov.l   r1, @(4, r15)
	extu.w  r5, r5                      /* r5 = todraw1 */
	tst     r5, r5
	bf      1f
	mov.l   @(4, r15), r5
	mov     #0, r0
	mov.l   r0, @(4, r15)
	mov.l   @(8, r15), r4
	tst     r5, r5
	bf      1f
	bra     .L8b_done
	nop
1:
	mov     r6, r0
	tst     #2, r0                      /* T = 4-byte aligned dst_top */
	bt      .L8ba_tile_loop
	bra     .L8bu_tile_loop
	nop

	/* ---- Aligned (4k) 8-bpp base tile loop: 2 pixels per mov.l ---- */
	.balign 4
.L8ba_tile_loop:
	mov.w   @r4+, r0
	swap.b  r0, r1
	shll2   r1
	shll2   r1
	shll2   r1
	mov.w   .L8b_tile_mask_a, r2
	extu.w  r2, r2
	and     r2, r1                      /* r1 = (tid & 0x3FF) << 6 */
	tst     #0x08, r0                   /* T = !vflip */
	mov     #8, r8
	bt.s    1f
	add     r7, r1
	add     r9, r1
	mov     #-8, r8
1:	mov     r1, r11                     /* r11 = r_tile */
	mov     r6, r12                     /* r12 = r_dst */
	mov.l   @r15, r14                   /* r14 = k */
	tst     #0x04, r0                   /* T = !hflip */
	bt      .L8ba_row_nf
	bra     .L8ba_row_hf
	nop

	.balign 4
.L8ba_row_nf:
	mov.l   @r11, r2                    /* [p0 p1 p2 p3] */
	mov.l   @(4, r11), r3               /* [p4 p5 p6 p7] */
	mov     r2, r0
	or      r3, r0
	tst     r0, r0
	bt      .L8ba_zero_nf
	CGBA_8BPP_BLIT8_XTRCT r2, r3, r12, r10, 0
.L8ba_nf_next:
	add     r8, r11
	dt      r14
	bf.s    .L8ba_row_nf
	add     r13, r12
	dt      r5
	bf.s    .L8ba_tile_loop
	add     #16, r6
	bra     .L8ba_span2
	nop

.L8ba_zero_nf:
	mov.w   @r10, r0
	swap.w  r0, r1
	xtrct   r0, r1
	mov.l   r1, @r12
	mov.l   r1, @(4, r12)
	mov.l   r1, @(8, r12)
	bra     .L8ba_nf_next
	mov.l   r1, @(12, r12)

	.balign 4
.L8ba_row_hf:
	mov.l   @r11, r2                    /* [p0 p1 p2 p3] */
	mov.l   @(4, r11), r3               /* [p4 p5 p6 p7] */
	mov     r2, r0
	or      r3, r0
	tst     r0, r0
	bt      .L8ba_zero_hf
	CGBA_8BPP_BLIT8_XTRCT r2, r3, r12, r10, 1
.L8ba_hf_next:
	add     r8, r11
	dt      r14
	bf.s    .L8ba_row_hf
	add     r13, r12
	dt      r5
	bt.s    .L8ba_span2
	add     #16, r6
	bra     .L8ba_tile_loop
	nop

.L8ba_zero_hf:
	mov.w   @r10, r0
	swap.w  r0, r1
	xtrct   r0, r1
	mov.l   r1, @r12
	mov.l   r1, @(4, r12)
	mov.l   r1, @(8, r12)
	bra     .L8ba_hf_next
	mov.l   r1, @(12, r12)

.L8ba_span2:
	mov.l   @(4, r15), r5
	tst     r5, r5
	bt      .L8b_done_tramp
	mov     #0, r0
	mov.l   r0, @(4, r15)
	mov.l   @(8, r15), r4
	bra     .L8ba_tile_loop
	nop
.L8b_done_tramp:
	bra     .L8b_done
	nop

	.balign 4
.L8b_stride_a:    .word 480
.L8b_tile_mask_a: .word 0xFFC0

	/* ---- Unaligned (4k+2) 8-bpp base tile loop: 16-bit mov.w stores ---- */
	.balign 4
.L8bu_tile_loop:
	mov.w   @r4+, r0
	swap.b  r0, r1
	shll2   r1
	shll2   r1
	shll2   r1
	mov.w   .L8b_tile_mask, r2
	extu.w  r2, r2
	and     r2, r1
	tst     #0x08, r0
	mov     #8, r8
	bt.s    1f
	add     r7, r1
	add     r9, r1
	mov     #-8, r8
1:	mov     r1, r11
	mov     r6, r12
	mov.l   @r15, r14
	tst     #0x04, r0
	bt      .L8bu_row_nf
	bra     .L8bu_row_hf
	nop

	.balign 4
.L8bu_row_nf:
	mov.l   @r11, r2
	mov.l   @(4, r11), r3
	mov     r2, r0
	or      r3, r0
	tst     r0, r0
	bt      .L8bu_zero_nf
	CGBA_8BPP_BLIT8_W16 r2, r3, r12, r10, 0
.L8bu_nf_next:
	add     r8, r11
	dt      r14
	bf.s    .L8bu_row_nf
	add     r13, r12
	dt      r5
	bf.s    .L8bu_tile_loop
	add     #16, r6
	bra     .L8bu_span2
	nop

.L8bu_zero_nf:
	mov.w   @r10, r0
	mov.w   r0, @r12
	mov.w   r0, @(2, r12)
	mov.w   r0, @(4, r12)
	mov.w   r0, @(6, r12)
	mov.w   r0, @(8, r12)
	mov.w   r0, @(10, r12)
	mov.w   r0, @(12, r12)
	bra     .L8bu_nf_next
	mov.w   r0, @(14, r12)

	.balign 4
.L8bu_row_hf:
	mov.l   @r11, r2
	mov.l   @(4, r11), r3
	mov     r2, r0
	or      r3, r0
	tst     r0, r0
	bt      .L8bu_zero_hf
	CGBA_8BPP_BLIT8_W16 r2, r3, r12, r10, 1
.L8bu_hf_next:
	add     r8, r11
	dt      r14
	bf.s    .L8bu_row_hf
	add     r13, r12
	dt      r5
	bt.s    .L8bu_span2
	add     #16, r6
	bra     .L8bu_tile_loop
	nop

.L8bu_zero_hf:
	mov.w   @r10, r0
	mov.w   r0, @r12
	mov.w   r0, @(2, r12)
	mov.w   r0, @(4, r12)
	mov.w   r0, @(6, r12)
	mov.w   r0, @(8, r12)
	mov.w   r0, @(10, r12)
	mov.w   r0, @(12, r12)
	bra     .L8bu_hf_next
	mov.w   r0, @(14, r12)

.L8bu_span2:
	mov.l   @(4, r15), r5
	tst     r5, r5
	bt      .L8b_done
	mov     #0, r0
	mov.l   r0, @(4, r15)
	mov.l   @(8, r15), r4
	bra     .L8bu_tile_loop
	nop

.L8b_done:
	add     #12, r15
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	.balign 4
.L8b_stride:    .word 480
.L8b_tile_mask: .word 0xFFC0
	.size   _cgba_render_8bpp_strip_base_sh4, .-_cgba_render_8bpp_strip_base_sh4

/* ============================================================================
 * _cgba_render_8bpp_strip_over_sh4
 *   Tile-major SH-4 big-endian 8-bpp FULLCOLOR overlay strip renderer
 *   (k = 1..8 rows per tile, index 0 transparent).  Includes a 1-entry
 *   empty-tile raw-map cache, whole-row zero skip, half-row zero skip,
 *   and cmp/str branchless 4-pixel opaque runs.  Uses 16-bit mov.w stores
 *   so aligned (4k) and unaligned (4k+2) destinations share one code path.
 *
 *   void cgba_render_8bpp_strip_over_sh4(
 *     const u16 *map1, u32 counts_packed, u16 *dst_top,
 *     const u8 *tile_charbase, u32 start_r_and_k,
 *     const u16 *pal256, const u16 *map2)
 * ========================================================================== */
.macro CGBA_8BPP_OVER4 reg, o0, o1, o2, o3, lbl
	tst     \reg, \reg
	bt      \lbl\()_done
	mov     #0, r1
	cmp/str r1, \reg
	bt      \lbl\()_part
	extu.b  \reg, r0
	shll    r0
	mov.w   @(r0, r10), r0
	shlr8   \reg
	mov.w   r0, @(\o3, r12)
	extu.b  \reg, r0
	shll    r0
	mov.w   @(r0, r10), r0
	shlr8   \reg
	mov.w   r0, @(\o2, r12)
	extu.b  \reg, r0
	shll    r0
	mov.w   @(r0, r10), r0
	shlr8   \reg
	mov.w   r0, @(\o1, r12)
	mov     \reg, r0
	shll    r0
	mov.w   @(r0, r10), r0
	bra     \lbl\()_done
	mov.w   r0, @(\o0, r12)
\lbl\()_part:
	extu.b  \reg, r0
	tst     r0, r0
	bt.s    1f
	shlr8   \reg
	shll    r0
	mov.w   @(r0, r10), r0
	mov.w   r0, @(\o3, r12)
1:	extu.b  \reg, r0
	tst     r0, r0
	bt.s    2f
	shlr8   \reg
	shll    r0
	mov.w   @(r0, r10), r0
	mov.w   r0, @(\o2, r12)
2:	extu.b  \reg, r0
	tst     r0, r0
	bt.s    3f
	shlr8   \reg
	shll    r0
	mov.w   @(r0, r10), r0
	mov.w   r0, @(\o1, r12)
3:	tst     \reg, \reg
	bt      \lbl\()_done
	mov     \reg, r0
	shll    r0
	mov.w   @(r0, r10), r0
	mov.w   r0, @(\o0, r12)
\lbl\()_done:
.endm

	.section .text._cgba_render_8bpp_strip_over_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_8bpp_strip_over_sh4
	.type   _cgba_render_8bpp_strip_over_sh4, @function
_cgba_render_8bpp_strip_over_sh4:
	tst     r5, r5
	bf      1f
	rts
	nop
1:	mov.l   @(0, r15), r0               /* r0 = (start_r << 8) | k */
	mov.l   @(4, r15), r1               /* r1 = pal256 */
	mov.l   @(8, r15), r2               /* r2 = map2 */
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15
	add     #-16, r15                   /* +0=k, +4=todraw2, +8=map2, +12=empty_raw */

	mov     r1, r10                     /* r10 = pal256 */
	mov.l   r2, @(8, r15)
	mov     #1, r2
	shll16  r2
	mov.l   r2, @(12, r15)              /* empty_raw sentinel (0x10000) */
	mov     r0, r3
	extu.b  r3, r3                      /* r3 = k (1..8) */
	mov.l   r3, @(0, r15)
	shlr8   r0
	extu.b  r0, r0                      /* r0 = start_r (0..7) */
	mov     r0, r1
	shll2   r1
	shll    r1                          /* r1 = start_r << 3 */
	add     r1, r7                      /* r7 = tb_norm = tile_charbase + start_r*8 */
	shll    r1                          /* r1 = start_r << 4 */
	mov     #56, r9
	sub     r1, r9                      /* r9 = vflip_off = 56 - start_r*16 */
	mov.w   .L8o_stride, r13            /* r13 = 480 */

	mov     r5, r1
	shlr16  r1                          /* r1 = todraw2 */
	mov.l   r1, @(4, r15)
	extu.w  r5, r5                      /* r5 = todraw1 */
	tst     r5, r5
	bf      .L8o_tile_loop
	mov.l   @(4, r15), r5
	mov     #0, r0
	mov.l   r0, @(4, r15)
	mov.l   @(8, r15), r4
	tst     r5, r5
	bf      .L8o_tile_loop
	bra     .L8o_done
	nop

	.balign 4
.L8o_tile_loop:
	mov.w   @r4+, r0
	mov.l   @(12, r15), r1
	cmp/eq  r1, r0
	bt      .L8o_tile_skip
	swap.b  r0, r1
	shll2   r1
	shll2   r1
	shll2   r1
	mov.w   .L8o_tile_mask, r2
	extu.w  r2, r2
	and     r2, r1                      /* r1 = (tid & 0x3FF) << 6 */
	tst     #0x08, r0                   /* T = !vflip */
	mov     #8, r8
	bt.s    1f
	add     r7, r1
	add     r9, r1
	mov     #-8, r8
1:	mov     r1, r11                     /* r11 = r_tile */
	mov     r6, r12                     /* r12 = r_dst */
	mov.l   @r15, r14                   /* r14 = k */
.L8o_lead_zero:
	mov.l   @r11, r2
	mov.l   @(4, r11), r3
	mov     r2, r1
	or      r3, r1
	tst     r1, r1
	bf      .L8o_first_nz
	add     r8, r11
	dt      r14
	bf.s    .L8o_lead_zero
	add     r13, r12
	mov.l   r0, @(12, r15)              /* all k rows zero -> cache raw map entry */
.L8o_tile_skip:
	dt      r5
	bf.s    .L8o_tile_loop
	add     #16, r6
	bra     .L8o_span2
	nop

.L8o_first_nz:
	tst     #0x04, r0                   /* T = !hflip */
	bt      .L8o_nf_have
	bra     .L8o_hf_have
	nop

	.balign 4
.L8o_stride:    .word 480
.L8o_tile_mask: .word 0xFFC0

	.balign 4
.L8o_nf_loop:
	mov.l   @r11, r2
	mov.l   @(4, r11), r3
	mov     r2, r0
	or      r3, r0
	tst     r0, r0
	bt      .L8o_nf_next
.L8o_nf_have:
	CGBA_8BPP_OVER4 r2, 0, 2, 4, 6, .L8o_nf_lo
	CGBA_8BPP_OVER4 r3, 8, 10, 12, 14, .L8o_nf_hi
.L8o_nf_next:
	add     r8, r11
	dt      r14
	bf.s    .L8o_nf_loop
	add     r13, r12
	dt      r5
	bt.s    .L8o_span2
	add     #16, r6
	bra     .L8o_tile_loop
	nop

	.balign 4
.L8o_hf_loop:
	mov.l   @r11, r2
	mov.l   @(4, r11), r3
	mov     r2, r0
	or      r3, r0
	tst     r0, r0
	bt      .L8o_hf_next
.L8o_hf_have:
	CGBA_8BPP_OVER4 r2, 14, 12, 10, 8, .L8o_hf_lo
	CGBA_8BPP_OVER4 r3, 6, 4, 2, 0, .L8o_hf_hi
.L8o_hf_next:
	add     r8, r11
	dt      r14
	bf.s    .L8o_hf_loop
	add     r13, r12
	dt      r5
	bt.s    .L8o_span2
	add     #16, r6
	bra     .L8o_tile_loop
	nop

.L8o_span2:
	mov.l   @(4, r15), r5
	tst     r5, r5
	bt      .L8o_done
	mov     #0, r0
	mov.l   r0, @(4, r15)
	mov.l   @(8, r15), r4
	bra     .L8o_tile_loop
	nop

.L8o_done:
	add     #16, r15
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8
	.size   _cgba_render_8bpp_strip_over_sh4, .-_cgba_render_8bpp_strip_over_sh4

/* ============================================================================
 * _cgba_render_8bpp_strip_blend_sh4
 *   Tile-major SH-4 big-endian 8-bpp direct RGB565 alpha-blend strip renderer
 *   with whole-row backdrop LUT fast path (bd_pal256) and per-pixel backdrop
 *   shortcut (skips mul.l whenever dst == backdrop).
 *
 *   void cgba_render_8bpp_strip_blend_sh4(
 *     const u16 *map1, u32 counts_packed, u16 *dst_top,
 *     const u8 *tile_charbase, u32 rke_sat,
 *     const u32 *term_a256, const u16 *map2, const u16 *bd_pal256)
 *   rke_sat = k | (start_r << 8) | (evb << 16) | (sat << 24)
 * ========================================================================== */
	.section .text._cgba_render_8bpp_strip_blend_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_8bpp_strip_blend_sh4
	.type   _cgba_render_8bpp_strip_blend_sh4, @function
_cgba_render_8bpp_strip_blend_sh4:
	tst     r5, r5
	bf      1f
	rts
	nop
1:	mov.l   @(0, r15), r0               /* r0 = rke_sat */
	mov.l   @(4, r15), r1               /* r1 = term_a256 */
	mov.l   @(8, r15), r2               /* r2 = map2 */
	mov.l   @(12, r15), r3              /* r3 = bd_pal256 */
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15
	sts.l   pr, @-r15
	add     #-32, r15                   /* +0=k,+4=vflip,+8=tstep,+12=td2,+16=map2,+20=tb_norm,+24=bd_pal,+28=bd_pair */

	mov     r1, r10                     /* r10 = term_a256 */
	mov.l   r2, @(16, r15)              /* save map2 */
	mov.l   r3, @(24, r15)              /* save bd_pal256 */
	mov.w   @r3, r3                     /* r3[15:0] = bd_pal256[0] = backdrop */
	swap.w  r3, r14
	xtrct   r3, r14                     /* r14 = (bd << 16) | bd */
	mov.l   r14, @(28, r15)             /* save bd_pair */

	mov     r0, r3
	extu.b  r3, r3                      /* r3 = k */
	mov.l   r3, @(0, r15)
	shlr8   r0
	mov     r0, r1
	extu.b  r1, r1                      /* r1 = start_r */
	shll2   r1
	shll    r1                          /* r1 = start_r << 3 */
	add     r1, r7                      /* r7 = tb_norm */
	mov.l   r7, @(20, r15)
	shll    r1                          /* r1 = start_r << 4 */
	mov     #56, r2
	sub     r1, r2                      /* r2 = vflip_off = 56 - start_r*16 */
	mov.l   r2, @(4, r15)
	shlr8   r0
	mov     r0, r11
	extu.b  r11, r11                    /* r11 = evb (0..16) */
	shlr8   r0
	tst     r0, r0                      /* sat != 0? */
	mov     #0, r13
	bt      1f
	mov.l   .L8bl_ovf_all, r13          /* r13 = 0x08010020 */
1:	mov.l   .L8bl_blnd_msk, r12         /* r12 = 0x07E0F81F */

	mov     r5, r1
	shlr16  r1                          /* r1 = todraw2 */
	mov.l   r1, @(12, r15)
	extu.w  r5, r5                      /* r5 = todraw1 */
	tst     r5, r5
	bf      .L8bl_tile_loop
	mov.l   @(12, r15), r5
	mov     #0, r0
	mov.l   r0, @(12, r15)
	mov.l   @(16, r15), r4
	tst     r5, r5
	bf      .L8bl_tile_loop
	bra     .L8bl_done
	nop

	.balign 4
.L8bl_tile_loop:
	mov.w   @r4+, r0                    /* raw_map */
	swap.b  r0, r1
	shll2   r1
	shll2   r1
	shll2   r1
	mov.w   .L8bl_tile_mask, r2
	extu.w  r2, r2
	and     r2, r1                      /* r1 = (tid & 0x3FF) << 6 */
	mov.l   @(20, r15), r7              /* r7 = tb_norm */
	add     r1, r7
	tst     #0x08, r0                   /* T = !vflip */
	mov     #8, r2
	bt      1f
	mov.l   @(4, r15), r3               /* vflip_off */
	add     r3, r7
	mov     #-8, r2
1:	mov.l   r2, @(8, r15)               /* save tstep */
	mov     r6, r8                      /* r8 = r_dst */
	mov.l   @(0, r15), r9               /* r9 = k */
	tst     #0x04, r0                   /* T = !hflip */
	bt      .L8bl_row_nf
	bra     .L8bl_row_hf
	nop

	.balign 4
.L8bl_blnd_msk:  .long 0x07E0F81F
.L8bl_ovf_all:   .long 0x08010020
.L8bl_tile_mask: .word 0xFFC0

/* Emit one 4-pixel 8bpp blend half-word check ([b0 b1 b2 b3] in r14). */
.macro CGBA_8BPP_BLEND_HALF off_src, d0, d1, d2, d3, next_lbl
.if \off_src == 0
	mov.l   @r7, r14
.else
	mov.l   @(\off_src, r7), r14
.endif
	tst     r14, r14
	bt      \next_lbl
	extu.b  r14, r0
	tst     r0, r0
	bt.s    1f
	shlr8   r14
	shll2   r0
.if \d0 == 0
	bsr     .L8bl_blend_pix
	mov     r8, r1
.else
	mov     r8, r1
	bsr     .L8bl_blend_pix
	add     #\d0, r1
.endif
1:	extu.b  r14, r0
	tst     r0, r0
	bt.s    2f
	shlr8   r14
	shll2   r0
	mov     r8, r1
	bsr     .L8bl_blend_pix
	add     #\d1, r1
2:	extu.b  r14, r0
	tst     r0, r0
	bt.s    3f
	shlr8   r14
	shll2   r0
	mov     r8, r1
	bsr     .L8bl_blend_pix
	add     #\d2, r1
3:	tst     r14, r14
	bt      \next_lbl
	mov     r14, r0
	shll2   r0
.if \d3 == 0
	bsr     .L8bl_blend_pix
	mov     r8, r1
.else
	mov     r8, r1
	bsr     .L8bl_blend_pix
	add     #\d3, r1
.endif
.endm

.macro CGBA_8BPP_BLEND_ROW_LOOP pfx, hflip, stride_lbl
	.balign 4
\pfx\()_row:
	mov     r8, r0
	tst     #2, r0
	bf      \pfx\()_slow
	mov.l   @(28, r15), r2              /* bd_pair */
	mov.l   @r8, r0
	cmp/eq  r2, r0
	bf      \pfx\()_slow
	mov.l   @(4, r8), r0
	cmp/eq  r2, r0
	bf      \pfx\()_slow
	mov.l   @(8, r8), r0
	cmp/eq  r2, r0
	bf      \pfx\()_slow
	mov.l   @(12, r8), r0
	cmp/eq  r2, r0
	bf      \pfx\()_slow
	/* Whole 8-pixel dst row == backdrop: 2-pixel mov.l LUT via bd_pal256! */
	mov.l   @r7, r2
	mov.l   @(4, r7), r3
	mov     r2, r0
	or      r3, r0
	tst     r0, r0
	bt      \pfx\()_bd_skip
	mov.l   @(24, r15), r14             /* r14 = bd_pal256 */
	CGBA_8BPP_BLIT8_XTRCT r2, r3, r8, r14, \hflip
\pfx\()_bd_skip:
	bra     \pfx\()_next
	nop

	.balign 4
\pfx\()_slow:
.if \hflip
	CGBA_8BPP_BLEND_HALF 0, 8, 10, 12, 14, \pfx\()_hi
\pfx\()_hi:
	CGBA_8BPP_BLEND_HALF 4, 0,  2,  4,  6, \pfx\()_next
.else
	CGBA_8BPP_BLEND_HALF 0,  6,  4,  2, 0, \pfx\()_hi
\pfx\()_hi:
	CGBA_8BPP_BLEND_HALF 4, 14, 12, 10, 8, \pfx\()_next
.endif
\pfx\()_next:
	mov.l   @(8, r15), r0               /* tstep */
	add     r0, r7
	mov.w   \stride_lbl, r0
	dt      r9
	bt.s    7f
	add     r0, r8
	bra     \pfx\()_row
	nop
7:	dt      r5
	bt.s    8f
	add     #16, r6
	bra     .L8bl_tile_loop
	nop
8:	bra     .L8bl_span2
	nop
.endm

.L8bl_row_nf:
	CGBA_8BPP_BLEND_ROW_LOOP .L8bl_nf, 0, .L8bl_stride_nf
	.balign 2
.L8bl_stride_nf: .word 480

	/* ---- hflip 8-bpp blend row loop ---- */
.L8bl_row_hf:
	CGBA_8BPP_BLEND_ROW_LOOP .L8bl_hf, 1, .L8bl_stride
	.purgem CGBA_8BPP_BLEND_HALF
	.purgem CGBA_8BPP_BLEND_ROW_LOOP

.L8bl_span2:
	mov.l   @(12, r15), r5
	tst     r5, r5
	bt      .L8bl_done
	mov     #0, r0
	mov.l   r0, @(12, r15)
	mov.l   @(16, r15), r4
	bra     .L8bl_tile_loop
	nop

.L8bl_done:
	add     #32, r15
	lds.l   @r15+, pr
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	/* Internal helper: blend non-zero 8-bpp pixel (r0 = pix<<2) onto *@r1.
	 * Fast-paths dst == backdrop via bd_pal256. Preserves r4..r14. */
	.balign 4
.L8bl_blend_pix:
	mov     r0, r3                      /* r3 = pix << 2 */
	mov.l   @(28, r15), r2              /* bd_pair */
	mov.w   @r1, r0                     /* sign-ext dst */
	exts.w  r2, r2                      /* sign-ext bd */
	cmp/eq  r2, r0
	bt      .L8bl_pix_bd
	extu.w  r0, r0
	swap.w  r0, r2
	or      r2, r0
	and     r12, r0                     /* p2e */
	mul.l   r11, r0
	mov     r3, r0
	mov.l   @(r0, r10), r3              /* term_a[pix] */
	sts     macl, r0
	add     r3, r0
	shlr2   r0
	shlr2   r0                          /* pfe */
	tst     r13, r0
	bf      .L8bl_sat
.L8bl_sat_ret:
	and     r12, r0
	swap.w  r0, r2
	or      r2, r0
	rts
	mov.w   r0, @r1

.L8bl_pix_bd:
	mov.l   @(24, r15), r2              /* bd_pal256 */
	mov     r3, r0
	shlr    r0                          /* pix << 1 */
	mov.w   @(r0, r2), r0               /* bd_pal256[pix] */
	rts
	mov.w   r0, @r1

.L8bl_sat:
	CGBA_SWAR_SAT_PFE r0, r13, r2, r3
	and     r12, r0
	swap.w  r0, r2
	or      r2, r0
	rts
	mov.w   r0, @r1

	.balign 2
.L8bl_stride:    .word 480
	.size   _cgba_render_8bpp_strip_blend_sh4, .-_cgba_render_8bpp_strip_blend_sh4


/* ============================================================================
 * 8-bpp FULLCOLOR rotated affine background kernels (all 4 map sizes 128..1024).
 *
 * Caller pre-clips non-wrap spans to exact in-bounds [lo, hi] and pre-shifts
 * coordinates to unsigned 32-bit wrapped space:
 *   ux     = (u32)(source_x + (cnt - 1) * dx) << (17 - map_size)
 *   uy     = (u32)(source_y + (cnt - 1) * dy) << (17 - map_size)
 *   step_x = (u32)(-dx) << (17 - map_size)
 *   step_y = (u32)(-dy) << (17 - map_size)
 *
 * Walks backwards from dst_end = dst + cnt down to dst using @-r4.
 * Keeps texel_mask (r5), tile_mask (r6), sub_y_shift (r13 = pix_shift + 3),
 * and cur_tile_ptr (r3) live in registers across pixels; map_base/tile_base
 * are only read from stack on 8x8 tile misses.
 * ============================================================================ */
	.section .text._cgba_render_affine_rot_base_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_affine_rot_base_sh4
	.type   _cgba_render_affine_rot_base_sh4, @function
_cgba_render_affine_rot_base_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	mov     r4, r0
	mov.l   @(0, r0), r4                /* r4  = dst_end */
	mov.l   @(8, r0), r2                /* tile_base */
	mov.l   r2, @-r15                   /* @(4, sp) = tile_base */
	mov.l   @(4, r0), r2                /* map_base */
	mov.l   r2, @-r15                   /* @(0, sp) = map_base */
	mov.l   @(12, r0), r7               /* r7  = pal */
	mov.l   @(16, r0), r8               /* r8  = ux */
	mov.l   @(20, r0), r9               /* r9  = uy */
	mov.l   @(24, r0), r10              /* r10 = step_x */
	mov.l   @(28, r0), r11              /* r11 = step_y */
	mov.l   @(32, r0), r14              /* r14 = cnt */
	mov.l   @(36, r0), r12              /* r12 = pix_shift (-(25 - map_size)) */
	mov.l   @(40, r0), r2               /* row_shift */
	lds     r2, macl                    /* macl = row_shift */

	mov     r12, r13
	add     #3, r13                     /* r13 = sub_y_shift = -(22 - map_size) */
	mov     #3, r0
	sub     r12, r0                     /* 28 - map_size (25..28) */
	mov     #-1, r6
	shld    r0, r6                      /* r6  = tile_mask */

	/* Same-tile cached loop (large steps |dx|>=2048 || |dy|>=2048). */
	.balign 4
.Lafbg_b_tile_miss:
	mov     r8, r0
	shld    r12, r0
	mov     r0, r1
	and     #7, r0
	mov     r0, r2
	shlr2   r1
	shlr    r1

	mov     r9, r0
	shld    r12, r0
	mov     r0, r3
	and     #7, r0
	xor     r0, r3
	shll2   r0
	add     r0, r0
	or      r0, r2
	mov     r3, r0
	sts     macl, r3
	shld    r3, r0
	or      r1, r0

	mov.l   @(0, r15), r1
	mov.b   @(r0, r1), r0               /* tile_id */
	extu.b  r0, r0
	shll8   r0
	shlr2   r0                          /* tile_id << 6 */
	mov.l   @(4, r15), r3
	add     r0, r3                      /* r3 = cur_tile_ptr */
	mov     r2, r0
	mov.b   @(r0, r3), r0
	extu.b  r0, r0
	add     r0, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @-r4
	dt      r14
	bt      .Lafbg_b_done

	.balign 4
.Lafbg_b_tile_next:
	mov     r8, r2
	add     r10, r8
	xor     r8, r2
	mov     r9, r0
	add     r11, r9
	xor     r9, r0
	or      r0, r2
	tst     r6, r2                      /* same 8x8 tile? */
	bf      .Lafbg_b_tile_miss
	mov     r8, r0
	shld    r12, r0
	and     #7, r0
	mov     r0, r2
	mov     r9, r0
	shld    r13, r0
	and     #0x38, r0
	or      r2, r0
	mov.b   @(r0, r3), r0
	extu.b  r0, r0
	add     r0, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @-r4
	dt      r14
	bf      .Lafbg_b_tile_next

.Lafbg_b_done:
	add     #8, r15
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8
	.size   _cgba_render_affine_rot_base_sh4, .-_cgba_render_affine_rot_base_sh4

/* ============================================================================
 * Fast addc-carry 8-bpp rotated affine BG base-layer renderer (SH-4).
 *
 * Mode 0 (|dx| < 256 && |dy| < 256, abs_step < 260):
 *   Uses 32-bit fractional carry (clrt; addc) for 7-insn same-texel hits and
 *   incremental cur_off (19..21 insns on same-tile texel change).
 *
 * Mode 1 (|dx| < 2048 && |dy| < 2048, abs_step >= 260):
 *   Uses 3.29 directional local-tile coordinates with clrt; addc for 20-insn
 *   same-tile hits and direct tx/ty +/-1 stepping on tile boundary crossings.
 * ============================================================================ */
	.section .text._cgba_render_affine_rot_base_addc_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_affine_rot_base_addc_sh4
	.type   _cgba_render_affine_rot_base_addc_sh4, @function
_cgba_render_affine_rot_base_addc_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	mov     r4, r0
	mov.l   @(56, r0), r1               /* mask_y_sh (mode0) / qflip (mode1) */
	mov.l   @(60, r0), r2               /* row_shift */
	tst     r5, r5                      /* mode (r5: 0 or 1) */
	bt      .Lafbg_a_setup
	bra     .Lafbg_t_setup
	nop

	/* Mode 0 setup: stack @(0)=map_base, @(4)=tile_base (-1 for step_x<0) */
.Lafbg_a_setup:
	mov.l   @(52, r0), r12              /* r12 = t_mask */
	lds     r2, macl                    /* macl = row_shift (map_size + 4) */
	mov     r1, r2                      /* r2  = init_cur_off (0..63) */

	mov.l   @(48, r0), r13              /* r13 = step_y8 (+/-8) */
	mov     #1, r1
	cmp/pz  r13
	bt      1f
	mov     #-1, r1
1:	lds     r1, mach                    /* mach = step_ty (+/-1) */

	mov.l   @(44, r0), r1               /* step_x (+1 or -1) */
	mov.l   @(8, r0), r3                /* tile_base */
	cmp/pz  r1
	bt      .Lafbg_ap_init

	add     #-1, r3                     /* tile_base - 1 for step_x == -1 */
	mov.l   r3, @-r15                   /* @(4, sp) = tile_base - 1 */
	mov.l   @(4, r0), r3
	mov.l   r3, @-r15                   /* @(0, sp) = map_base */
	mov.l   @(0, r0), r4                /* r4  = dst_end */
	mov.l   @(12, r0), r7               /* r7  = pal_signed */
	mov.l   @(16, r0), r8               /* r8  = f_x */
	mov.l   @(20, r0), r9               /* r9  = f_y */
	mov.l   @(24, r0), r10              /* r10 = step_fx */
	mov.l   @(28, r0), r11              /* r11 = step_fy */
	mov.l   @(32, r0), r14              /* r14 = cnt */
	mov.l   @(36, r0), r5               /* r5  = tx */
	bra     .Lafbg_am_miss
	mov.l   @(40, r0), r6               /* r6  = ty */

.Lafbg_ap_init:
	mov.l   r3, @-r15                   /* @(4, sp) = tile_base */
	mov.l   @(4, r0), r3
	mov.l   r3, @-r15                   /* @(0, sp) = map_base */
	mov.l   @(0, r0), r4                /* r4  = dst_end */
	mov.l   @(12, r0), r7               /* r7  = pal_signed */
	mov.l   @(16, r0), r8               /* r8  = f_x */
	mov.l   @(20, r0), r9               /* r9  = f_y */
	mov.l   @(24, r0), r10              /* r10 = step_fx */
	mov.l   @(28, r0), r11              /* r11 = step_fy */
	mov.l   @(32, r0), r14              /* r14 = cnt */
	mov.l   @(36, r0), r5               /* r5  = tx */
	bra     .Lafbg_ap_miss
	mov.l   @(40, r0), r6               /* r6  = ty */

	/* ---- Mode 0: step_x == -1 (r3 = cur_tile_ptr - 1, r12 = t_mask) ---- */
	.balign 4
.Lafbg_am_step_xy:
	mov     r2, r0
	tst     #7, r0
	bt      .Lafbg_am_miss_x_then_y
	add     #-1, r2
	add     r13, r2
	mov     r2, r0
	tst     #0x40, r0
	bf      .Lafbg_am_miss_y
	bra     .Lafbg_am_load
	add     #1, r0

	/* Entry invariant: T == 0 (only reached via bf after dt r14). */
	.balign 4
.Lafbg_am_next:
	addc    r10, r8
	bt      .Lafbg_am_step_x
	addc    r11, r9
	bt      .Lafbg_am_step_y
	dt      r14
	bf.s    .Lafbg_am_next
	mov.w   r1, @-r4
	bra     .Lafbg_a_done
	nop

	.balign 4
.Lafbg_am_step_x:
	clrt
	addc    r11, r9
	bt      .Lafbg_am_step_xy
	mov     r2, r0
	tst     #7, r0
	bt      .Lafbg_am_miss_x
	add     #-1, r2                     /* r0 == old r2 == new r2 + 1 */
	/* Entry invariant: r0 == r2 + 1, r3 == cur_tile_ptr - 1. */
.Lafbg_am_load:
	mov.b   @(r0, r3), r0
	add     r0, r0
	mov.w   @(r0, r7), r1
	dt      r14
	bf.s    .Lafbg_am_next
	mov.w   r1, @-r4
.Lafbg_a_done:
	add     #8, r15
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	.balign 4
.Lafbg_am_step_y:
	add     r13, r2
	mov     r2, r0
	tst     #0x40, r0
	bf      .Lafbg_am_miss_y
	add     #1, r0
	mov.b   @(r0, r3), r0
	add     r0, r0
	mov.w   @(r0, r7), r1
	dt      r14
	bf.s    .Lafbg_am_next
	mov.w   r1, @-r4
	bra     .Lafbg_a_done
	nop

	.balign 4
.Lafbg_am_miss_x_then_y:
	add     #7, r2
	add     #-1, r5
	add     r13, r2
	mov     r2, r0
	tst     #0x40, r0
	bt      .Lafbg_am_miss
	/* fallthrough to .Lafbg_am_miss_y */

.Lafbg_am_miss_y:
	mov     r2, r0
	and     #0x3F, r0
	mov     r0, r2
	sts     mach, r0
	bra     .Lafbg_am_miss
	add     r0, r6

.Lafbg_am_miss_x:
	add     #7, r2
	add     #-1, r5

	.balign 4
.Lafbg_am_miss:
	and     r12, r5
	and     r12, r6
	sts     macl, r3
	mov     r6, r0
	shld    r3, r0
	or      r5, r0
	mov.l   @(0, r15), r1               /* map_base */
	mov.b   @(r0, r1), r0
	extu.b  r0, r0
	shll8   r0
	shlr2   r0
	mov.l   @(4, r15), r3               /* tile_base - 1 */
	add     r0, r3
	mov     r2, r0
	bra     .Lafbg_am_load
	add     #1, r0

	/* ---- Mode 0: step_x == +1 (r3 = cur_tile_ptr, r12 = t_mask) ---- */
	.balign 4
.Lafbg_ap_step_xy:
	add     #1, r2
	mov     r2, r0
	tst     #7, r0
	bt      .Lafbg_ap_miss_x_then_y
	add     r13, r2
	mov     r2, r0
	tst     #0x40, r0
	bf      .Lafbg_ap_miss_y
	bra     .Lafbg_ap_load
	nop

	/* Entry invariant: T == 0 (only reached via bf after dt r14). */
	.balign 4
.Lafbg_ap_next:
	addc    r10, r8
	bt      .Lafbg_ap_step_x
	addc    r11, r9
	bt      .Lafbg_ap_step_y
	dt      r14
	bf.s    .Lafbg_ap_next
	mov.w   r1, @-r4
	bra     .Lafbg_a_done
	nop

	.balign 4
.Lafbg_ap_step_x:
	clrt
	addc    r11, r9
	bt      .Lafbg_ap_step_xy
	add     #1, r2
	mov     r2, r0
	tst     #7, r0
	bt      .Lafbg_ap_miss_x
	/* Entry invariant: r0 == r2, r3 == cur_tile_ptr. */
.Lafbg_ap_load:
	mov.b   @(r0, r3), r0
	add     r0, r0
	mov.w   @(r0, r7), r1
	dt      r14
	bf.s    .Lafbg_ap_next
	mov.w   r1, @-r4
	bra     .Lafbg_a_done
	nop

	.balign 4
.Lafbg_ap_step_y:
	add     r13, r2
	mov     r2, r0
	tst     #0x40, r0
	bf      .Lafbg_ap_miss_y
	mov.b   @(r0, r3), r0
	add     r0, r0
	mov.w   @(r0, r7), r1
	dt      r14
	bf.s    .Lafbg_ap_next
	mov.w   r1, @-r4
	bra     .Lafbg_a_done
	nop

	.balign 4
.Lafbg_ap_miss_x_then_y:
	add     #-8, r2
	add     #1, r5
	add     r13, r2
	mov     r2, r0
	tst     #0x40, r0
	bt      .Lafbg_ap_miss
	/* fallthrough to .Lafbg_ap_miss_y */

.Lafbg_ap_miss_y:
	mov     r2, r0
	and     #0x3F, r0
	mov     r0, r2
	sts     mach, r0
	bra     .Lafbg_ap_miss
	add     r0, r6

.Lafbg_ap_miss_x:
	add     #-8, r2
	add     #1, r5

	.balign 4
.Lafbg_ap_miss:
	and     r12, r5
	and     r12, r6
	sts     macl, r3
	mov     r6, r0
	shld    r3, r0
	or      r5, r0
	mov.l   @(0, r15), r1               /* map_base */
	mov.b   @(r0, r1), r0
	extu.b  r0, r0
	shll8   r0
	shlr2   r0
	mov.l   @(4, r15), r3               /* tile_base */
	add     r0, r3
	bra     .Lafbg_ap_load
	mov     r2, r0

	/* Mode 1 setup: stack @(0)=map_base, @(4)=tile_base, @(8)=t_mask, @(12)=row_shift */
	.balign 4
.Lafbg_t_setup:
	mov.l   r2, @-r15                   /* @(12, sp) = row_shift (map_size + 4) */
	mov.l   @(52, r0), r2
	mov.l   r2, @-r15                   /* @(8, sp)  = t_mask */
	mov.l   @(8, r0), r2
	mov.l   r2, @-r15                   /* @(4, sp)  = tile_base */
	mov.l   @(4, r0), r2
	mov.l   r2, @-r15                   /* @(0, sp)  = map_base */
	mov     r1, r5                      /* r5  = qflip */
	mov.l   @(44, r0), r2
	lds     r2, mach                    /* mach = step_tx (+/-1) */
	mov.l   @(48, r0), r2
	lds     r2, macl                    /* macl = step_ty (+/-1) */

	mov.l   @(0, r0), r4                /* r4  = dst_end */
	mov.l   @(12, r0), r7               /* r7  = pal_signed */
	mov.l   @(16, r0), r8               /* r8  = lx_dir (3.29) */
	mov.l   @(20, r0), r9               /* r9  = ly_dir (3.29) */
	mov.l   @(24, r0), r10              /* r10 = step_lx */
	mov.l   @(28, r0), r11              /* r11 = step_ly */
	mov.l   @(32, r0), r14              /* r14 = cnt */
	mov.l   @(36, r0), r1               /* r1  = tx */
	mov.l   @(40, r0), r6               /* r6  = ty */
	mov     #-29, r12
	bra     .Lafbg_t_miss
	mov     #-26, r13

	.balign 4
.Lafbg_t_miss_x:
	clrt
	addc    r11, r9
	bf      1f
	sts     macl, r0
	add     r0, r6
1:	sts     mach, r0
	bra     .Lafbg_t_miss
	add     r0, r1

	.balign 4
.Lafbg_t_miss_y:
	sts     macl, r0
	add     r0, r6

	.balign 4
.Lafbg_t_miss:
	mov.l   @(8, r15), r0               /* t_mask */
	and     r0, r1
	and     r0, r6
	mov.l   @(12, r15), r2              /* row_shift */
	mov     r6, r0
	shld    r2, r0                      /* ty << (map_size + 4) */
	or      r1, r0                      /* mapoff */
	mov.l   @(0, r15), r2               /* map_base */
	mov.b   @(r0, r2), r0               /* tile_id */
	extu.b  r0, r0
	shll8   r0
	shlr2   r0                          /* tile_id << 6 */
	mov.l   @(4, r15), r3               /* tile_base */
	bra     .Lafbg_t_draw
	add     r0, r3                      /* r3 = cur_tile_ptr */

	/* Entry invariant: T == 0 (only reached via bf after dt r14). */
	.balign 4
.Lafbg_t_next:
	addc    r10, r8
	bt      .Lafbg_t_miss_x
	addc    r11, r9
	bt      .Lafbg_t_miss_y
.Lafbg_t_draw:
	mov     r8, r0
	shld    r12, r0                     /* lx_dir >> 29 (0..7) */
	mov     r0, r2
	mov     r9, r0
	shld    r13, r0                     /* ly_dir >> 26 (0..56) */
	and     #0x38, r0
	or      r2, r0
	xor     r5, r0                      /* apply qflip */
	mov.b   @(r0, r3), r0
	add     r0, r0
	mov.w   @(r0, r7), r0
	dt      r14
	bf.s    .Lafbg_t_next
	mov.w   r0, @-r4
	bra     .Lafbg_a_done
	add     #8, r15
	.size   _cgba_render_affine_rot_base_addc_sh4, .-_cgba_render_affine_rot_base_addc_sh4

/* ============================================================================
 * _cgba_render_affine_rot_base_p2_sh4
 *   2-pixel-at-a-time direct-addc 8-bpp FULLCOLOR rotated affine BG base kernel
 *   for |dx| < 128 && |dy| < 128 (all 4 step sign combinations).
 *   Zero outer-loop / multiply overhead: advances (f_x, f_y) by 2*step per
 *   iteration and writes 2 pixels per mov.l @-r4 using u32 pal_dup + xtrct.
 *   Because |dx|<128 and |dy|<128, each axis carries at most once per 2-pixel
 *   pair, and cmp/hs (step>>1), f identifies which pixel carried.
 *
 *   r4 = const cgba_aff_addc_params *p:
 *     +0  dst_end   +4  map_base   +8  tile_base  +12 pal_dup (u32* signed)
 *     +16 f_x       +20 f_y        +24 step_fx (adx<<25)  +28 step_fy (ady<<25)
 *     +32 cnt       +36 tx         +40 ty         +44 step_x (+1/-1)
 *     +48 step_y8   +52 t_mask     +56 cur_off    +60 row_shift
 * ============================================================================ */
	.section .text._cgba_render_affine_rot_base_p2_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_affine_rot_base_p2_sh4
	.type   _cgba_render_affine_rot_base_p2_sh4, @function
_cgba_render_affine_rot_base_p2_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15
	sts.l   pr, @-r15

	mov     r4, r0
	mov.l   @(52, r0), r1
	mov.l   r1, @-r15                   /* @(16, sp) = t_mask */
	mov     #0, r1
	mov.l   r1, @-r15                   /* @(12, sp) = tail_flag */
	mov.l   @(44, r0), r1
	mov.l   r1, @-r15                   /* @(8, sp)  = step_x (+1 or -1) */
	mov.l   @(8, r0), r1
	mov.l   r1, @-r15                   /* @(4, sp)  = tile_base */
	mov.l   @(4, r0), r1
	mov.l   r1, @-r15                   /* @(0, sp)  = map_base */

	mov.l   @(60, r0), r1
	lds     r1, macl                    /* macl = row_shift */
	mov.l   @(56, r0), r2               /* r2   = cur_off (0..63) */
	mov.l   @(0, r0), r4                /* r4   = dst_end */
	mov.l   @(36, r0), r5               /* r5   = tx */
	mov.l   @(40, r0), r6               /* r6   = ty */
	mov.l   @(12, r0), r7               /* r7   = pal_dup */
	mov.l   @(16, r0), r8               /* r8   = f_x */
	mov.l   @(20, r0), r9               /* r9   = f_y */
	mov.l   @(24, r0), r10              /* r10  = 2*adx << 24 */
	mov.l   @(28, r0), r11              /* r11  = 2*ady << 24 */
	mov     r11, r12
	shlr    r12                         /* r12  = ady << 24 (half step_fy) */
	mov.l   @(48, r0), r13              /* r13  = step_y8 (+8 or -8) */
	mov.l   @(32, r0), r14              /* r14  = cnt (>= 1) */

	/* Initial tile load + pixel 0 color */
	sts     macl, r3
	mov     r6, r0
	shld    r3, r0
	or      r5, r0
	mov.l   @(0, r15), r1               /* map_base */
	mov.b   @(r0, r1), r0
	extu.b  r0, r0
	shll8   r0
	shlr2   r0
	mov.l   @(4, r15), r3               /* tile_base */
	add     r0, r3                      /* r3 = cur_tile_ptr */
	mov     r2, r0
	mov.b   @(r0, r3), r0
	shll2   r0
	mov.l   @(r0, r7), r1               /* r1 = [c0 | c0] */

	mov     #1, r0
	cmp/eq  r0, r14
	bt      .Lp2_cnt1
	mov     r4, r0
	tst     #2, r0                      /* T = 4-byte aligned */
	bt      .Lp2_head_aligned
	/* Unaligned dst_end (4k+2): write pixel 0 to align r4 */
	mov.w   r1, @-r4
	bra     .Lp2_start_pairs
	add     #-1, r14

.Lp2_cnt1:
	bra     .Lp2_done
	mov.w   r1, @-r4

.Lp2_head_aligned:
	/* Aligned dst_end (4k), cnt >= 2: emit [px1 | px0] as one mov.l */
	lds     r1, mach                    /* mach = [c0 | c0] */
	mov     r10, r0
	shlr    r0
	clrt
	addc    r0, r8
	bf      1f
	bsr     .Lp2_step_x_one
	nop
1:	mov     r11, r0
	shlr    r0
	clrt
	addc    r0, r9
	bf      2f
	bsr     .Lp2_step_y_one
	nop
2:	mov     r2, r0
	mov.b   @(r0, r3), r0
	shll2   r0
	mov.l   @(r0, r7), r1               /* r1 = [c1 | c1] */
	sts     mach, r0
	xtrct   r1, r0                      /* r0 = [c1 | c0] */
	mov.l   r0, @-r4
	add     #-2, r14
	tst     r14, r14
	bt      .Lp2_done

.Lp2_start_pairs:
	mov     r14, r0
	shlr    r14                         /* r14 = pair_count */
	and     #1, r0
	mov.l   r0, @(12, r15)              /* tail_flag */
	tst     r14, r14
	bt      .Lp2_to_tail
	clrt

	/* ---- Hot 2-pixel pair loop (T == 0 on entry) ---- */
	.balign 4
.Lp2_next:
	addc    r10, r8
	bt      .Lp2_step_x
	addc    r11, r9
	bt      .Lp2_step_y
	dt      r14
	bf.s    .Lp2_next
	mov.l   r1, @-r4
.Lp2_check_tail:
	mov.l   @(12, r15), r0
	tst     r0, r0
	bf      .Lp2_to_tail
.Lp2_done:
	add     #20, r15
	lds.l   @r15+, pr
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lp2_to_tail:
	bra     .Lp2_tail_pix
	nop

	.balign 4
.Lp2_step_y:
	add     r13, r2
	mov     r2, r0
	tst     #0x40, r0
	bf      .Lp2_miss_y
	lds     r1, mach
.Lp2_y_load:
	mov.b   @(r0, r3), r0
	shll2   r0
	mov.l   @(r0, r7), r1
	cmp/hs  r12, r9                     /* T = pixel 0 carried in Y */
	bt      1f
	sts     mach, r0
	xtrct   r1, r0                      /* [new | old] */
	dt      r14
	bf.s    .Lp2_next
	mov.l   r0, @-r4
	bra     .Lp2_check_tail
	nop
1:	dt      r14
	bf.s    .Lp2_next
	mov.l   r1, @-r4
	bra     .Lp2_check_tail
	nop

	.balign 4
.Lp2_step_x:
	clrt
	addc    r11, r9
	bt      .Lp2_to_xy
	lds     r1, mach
	mov     r2, r0
	and     #7, r0
	mov.l   @(8, r15), r1               /* step_x (+1 or -1) */
	add     r1, r0
	tst     #8, r0                      /* crossed X tile edge? */
	bf      .Lp2_miss_x
	add     r1, r2
	mov     r2, r0
.Lp2_x_load:
	mov.b   @(r0, r3), r0
	shll2   r0
	mov.l   @(r0, r7), r1
	mov     r10, r0
	shlr    r0
	cmp/hs  r0, r8                      /* T = pixel 0 carried in X */
	bt      1f
	sts     mach, r0
	xtrct   r1, r0                      /* [new | old] */
	dt      r14
	bf.s    .Lp2_next
	mov.l   r0, @-r4
	bra     .Lp2_check_tail
	nop
1:	dt      r14
	bf.s    .Lp2_next
	mov.l   r1, @-r4
	bra     .Lp2_check_tail
	nop

.Lp2_to_xy:
	bra     .Lp2_step_xy
	nop

	.balign 4
.Lp2_miss_y:
	lds     r1, mach
	mov     r2, r0
	and     #0x3F, r0
	mov     r0, r2
	mov     #1, r0
	cmp/pz  r13
	bt      1f
	mov     #-1, r0
1:	add     r0, r6
	mov.l   @(16, r15), r0              /* t_mask */
	and     r0, r6
	sts     macl, r3
	mov     r6, r0
	shld    r3, r0
	or      r5, r0
	mov.l   @(0, r15), r1
	mov.b   @(r0, r1), r0
	extu.b  r0, r0
	shll8   r0
	shlr2   r0
	mov.l   @(4, r15), r3
	add     r0, r3
	bra     .Lp2_y_load
	mov     r2, r0

	.balign 4
.Lp2_miss_x:
	mov     #7, r0
	xor     r0, r2
	add     r1, r5
	mov.l   @(16, r15), r0              /* t_mask */
	and     r0, r5
	sts     macl, r3
	mov     r6, r0
	shld    r3, r0
	or      r5, r0
	mov.l   @(0, r15), r1
	mov.b   @(r0, r1), r0
	extu.b  r0, r0
	shll8   r0
	shlr2   r0
	mov.l   @(4, r15), r3
	add     r0, r3
	bra     .Lp2_x_load
	mov     r2, r0

	/* ---- Both X and Y carried in this 2-pixel pair ---- */
	.balign 4
.Lp2_step_xy:
	lds     r1, mach                    /* default px0 = old */
	mov     r10, r0
	shlr    r0
	cmp/hs  r0, r8                      /* T = x0 (pixel 0 stepped X) */
	bt      .Lp2_xy_x0
	/* x0 == 0: check y0 */
	mov     r11, r0
	shlr    r0
	cmp/hs  r0, r9                      /* T = y0 (pixel 0 stepped Y) */
	bt      .Lp2_xy_y0_x1
	/* (x0=0, y0=0): px0 = old (mach), px1 steps X and Y */
	bsr     .Lp2_step_x_one
	nop
	bsr     .Lp2_step_y_one
	nop
.Lp2_xy_finish_pair:
	mov     r2, r0
	mov.b   @(r0, r3), r0
	shll2   r0
	mov.l   @(r0, r7), r1               /* r1 = [c_xy | c_xy] */
	sts     mach, r0                    /* r0 = [px0 | px0] */
	xtrct   r1, r0                      /* r0 = [c_xy | px0] */
	dt      r14
	bt.s    .Lp2_xy_done_pair
	mov.l   r0, @-r4
	bra     .Lp2_next
	nop
.Lp2_xy_done_pair:
	bra     .Lp2_check_tail
	nop

.Lp2_xy_y0_x1:
	/* (x0=0, y0=1): px0 steps Y, px1 steps X */
	bsr     .Lp2_step_y_one
	nop
	mov     r2, r0
	mov.b   @(r0, r3), r0
	shll2   r0
	mov.l   @(r0, r7), r1
	lds     r1, mach                    /* mach = [c_y | c_y] */
	bsr     .Lp2_step_x_one
	nop
	bra     .Lp2_xy_finish_pair
	nop

.Lp2_xy_x0:
	/* x0 == 1: check y0 */
	mov     r11, r0
	shlr    r0
	cmp/hs  r0, r9                      /* T = y0 */
	bf      .Lp2_xy_x0_y1
	/* (x0=1, y0=1): px0 steps X and Y, px1 repeats */
	bsr     .Lp2_step_x_one
	nop
	bsr     .Lp2_step_y_one
	nop
	mov     r2, r0
	mov.b   @(r0, r3), r0
	shll2   r0
	mov.l   @(r0, r7), r1               /* r1 = [c_xy | c_xy] */
	dt      r14
	bt.s    .Lp2_xy_done_pair
	mov.l   r1, @-r4
	bra     .Lp2_next
	nop

.Lp2_xy_x0_y1:
	/* (x0=1, y0=0): px0 steps X, px1 steps Y */
	bsr     .Lp2_step_x_one
	nop
	mov     r2, r0
	mov.b   @(r0, r3), r0
	shll2   r0
	mov.l   @(r0, r7), r1
	lds     r1, mach                    /* mach = [c_x | c_x] */
	bsr     .Lp2_step_y_one
	nop
	bra     .Lp2_xy_finish_pair
	nop

	/* ---- Odd final pixel ---- */
	.balign 4
.Lp2_tail_pix:
	mov     r10, r0
	shlr    r0
	clrt
	addc    r0, r8
	bf      1f
	bsr     .Lp2_step_x_one
	nop
1:	mov     r11, r0
	shlr    r0
	clrt
	addc    r0, r9
	bf      2f
	bsr     .Lp2_step_y_one
	nop
2:	mov     r2, r0
	mov.b   @(r0, r3), r0
	shll2   r0
	mov.l   @(r0, r7), r1
	bra     .Lp2_done
	mov.w   r1, @-r4

	/* ---- Internal helpers (preserve all regs except r0, r1, r2/r3/r5/r6) ---- */
	.balign 4
.Lp2_step_x_one:
	mov     r2, r0
	and     #7, r0
	mov.l   @(8, r15), r1
	add     r1, r0
	tst     #8, r0
	bf      1f
	rts
	add     r1, r2
1:	mov     #7, r0
	xor     r0, r2
	add     r1, r5
	mov.l   @(16, r15), r0              /* t_mask */
	and     r0, r5
	bra     .Lp2_reload_tile_sub
	nop

	.balign 4
.Lp2_step_y_one:
	add     r13, r2
	mov     r2, r0
	tst     #0x40, r0
	bf      1f
	rts
	nop
1:	and     #0x3F, r0
	mov     r0, r2
	mov     #1, r0
	cmp/pz  r13
	bt      2f
	mov     #-1, r0
2:	add     r0, r6
	mov.l   @(16, r15), r0              /* t_mask */
	and     r0, r6
.Lp2_reload_tile_sub:
	sts     macl, r3
	mov     r6, r0
	shld    r3, r0
	or      r5, r0
	mov.l   @(0, r15), r1
	mov.b   @(r0, r1), r0
	extu.b  r0, r0
	shll8   r0
	shlr2   r0
	mov.l   @(4, r15), r3
	rts
	add     r0, r3
	.size   _cgba_render_affine_rot_base_p2_sh4, .-_cgba_render_affine_rot_base_p2_sh4

/* ============================================================================
 * Unaligned (2-byte aligned dst_top = 4k+2) tile-major k-row 4bpp strip
 * over-renderer for odd tile_hoff (1, 3, 5, 7) scrolling text BGs.
 * Same signature as _cgba_render_4bpp_strip_over_sh4:
 *   void cgba_render_4bpp_strip_over_u_sh4(
 *     const u16 *map1, u32 counts_packed, u16 *dst_top,
 *     const u8 *tile_charbase, u32 rk, const u32 *pair_lut, const u16 *map2)
 * ============================================================================ */
.macro SOU_OPAQUE_XTRCT_NHF
	extu.b  r2, r0                      /* b3 -> [p6|p7] */
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	mov     r3, r0
	mov.w   r0, @(14, r6)               /* dest[7] = p7 */
	add     #2, r6                      /* r6 = 4k+4 (4-aligned) */

	extu.b  r2, r0                      /* b2 -> [p4|p5] */
	shll2   r0
	mov.l   @(r0, r5), r0
	shlr8   r2
	xtrct   r0, r3                      /* r3 = [p5|p6] */
	mov.l   r3, @(8, r6)                /* dest[5..6] */
	mov     r0, r3                      /* r3 = [p4|p5] */

	extu.b  r2, r0                      /* b1 -> [p2|p3] */
	shll2   r0
	mov.l   @(r0, r5), r0
	shlr8   r2
	xtrct   r0, r3                      /* r3 = [p3|p4] */
	mov.l   r3, @(4, r6)                /* dest[3..4] */
	mov     r0, r3                      /* r3 = [p2|p3] */

	extu.b  r2, r0                      /* b0 -> [p0|p1] */
	shll2   r0
	mov.l   @(r0, r5), r0
	xtrct   r0, r3                      /* r3 = [p1|p2] */
	mov.l   r3, @r6                     /* dest[1..2] */
	swap.w  r0, r0                      /* low 16 = p0 */
	mov.w   r0, @-r6                    /* r6 = 4k+2, dest[0] = p0 */
.endm

.macro SOU_OPAQUE_XTRCT_HF
	extu.b  r2, r0                      /* b3 -> [p6|p7] */
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	mov.w   r3, @r6                     /* dest[0] = p7 */
	add     #2, r6                      /* r6 = 4k+4 (4-aligned) */

	extu.b  r2, r0                      /* b2 -> [p4|p5] */
	shll2   r0
	mov.l   @(r0, r5), r0
	shlr8   r2
	xtrct   r0, r3                      /* r3 = [p5|p6] */
	swap.w  r3, r3                      /* r3 = [p6|p5] */
	mov.l   r3, @r6                     /* dest[1..2] */
	mov     r0, r3                      /* r3 = [p4|p5] */

	extu.b  r2, r0                      /* b1 -> [p2|p3] */
	shll2   r0
	mov.l   @(r0, r5), r0
	shlr8   r2
	xtrct   r0, r3                      /* r3 = [p3|p4] */
	swap.w  r3, r3                      /* r3 = [p4|p3] */
	mov.l   r3, @(4, r6)                /* dest[3..4] */
	mov     r0, r3                      /* r3 = [p2|p3] */

	extu.b  r2, r0                      /* b0 -> [p0|p1] */
	shll2   r0
	mov.l   @(r0, r5), r0
	xtrct   r0, r3                      /* r3 = [p1|p2] */
	swap.w  r3, r3                      /* r3 = [p2|p1] */
	mov.l   r3, @(8, r6)                /* dest[5..6] */
	swap.w  r0, r0                      /* low 16 = p0 */
	mov.w   r0, @(12, r6)               /* dest[7] = p0 */
	add     #-2, r6                     /* restore r6 = 4k+2 */
.endm

.macro SOU_PART_BYTE disp_lo, disp_hi, do_shift
	extu.b  r2, r0
	tst     r0, r0
	.if \do_shift
	bt.s    1f
	shlr8   r2
	.else
	bt      1f
	.endif
	tst     #0x0F, r0
	bt      2f
	tst     #0xF0, r0
	bt.s    3f
	shll2   r0
	mov.l   @(r0, r5), r0
	mov.w   r0, @(\disp_hi, r6)
	swap.w  r0, r0
	bra     1f
	mov.w   r0, @(\disp_lo, r6)
2:	shll2   r0
	mov.l   @(r0, r5), r0
	bra     1f
	mov.w   r0, @(\disp_hi, r6)
3:	mov.w   @(r0, r5), r0
	mov.w   r0, @(\disp_lo, r6)
1:
.endm

	.section .text._cgba_render_4bpp_strip_over_u16_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_4bpp_strip_over_u16_sh4
	.type   _cgba_render_4bpp_strip_over_u16_sh4, @function
_cgba_render_4bpp_strip_over_u16_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	extu.w  r5, r8                      /* r8  = todraw1 */
	shlr16  r5                          /* r5  = todraw2 */
	lds     r5, macl                    /* macl = todraw2 */
	tst     r5, r5
	bt      1f
	mov.l   @(36, r15), r0              /* map2 */
	lds     r0, mach                    /* mach = map2 */
1:	mov.l   @(32, r15), r10             /* r10 = pair_lut */
	mov.l   @(28, r15), r0              /* (start_r << 8) | k */
	extu.b  r0, r2                      /* r2  = k (1..8) */
	shlr8   r0
	shll2   r0                          /* r0  = start_r * 4 */
	add     r0, r7                      /* r7  = tile_charbase + start_r*4 */
	add     r0, r0
	mov     #28, r3
	sub     r0, r3                      /* r3  = vflip_off */

	mov     r2, r0
	shll8   r0
	add     r0, r0                      /* k * 512 */
	mov     r2, r1
	shll2   r1
	shll2   r1
	add     r1, r1                      /* k * 32 */
	sub     r1, r0                      /* k * 480 */
	mov     #16, r1
	sub     r0, r1                      /* r1  = 16 - k * 480 */
	mov     #4, r0
	mov.l   r0, @-r15                   /* @(12, r15) = row_step (+4 or -4) */
	mov.l   r1, @-r15                   /* @(8,  r15) = dst_next_delta */
	mov.l   r2, @-r15                   /* @(4,  r15) = k */
	mov.l   r3, @-r15                   /* @(0,  r15) = vflip_off */

	mov.w   .Lsou_480, r11              /* r11 = 480 */
	mov.l   .Lsou_eeeef, r13            /* r13 = 0xEEEEEEEF */
	mov.l   .Lsou_88888, r14            /* r14 = 0x88888888 */
	mov     r13, r12                    /* r12 = last known all-empty raw_map */

	.balign 4
.Lsou_tile:
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bt      .Lsou_known_empty
.Lsou_tile_have_r0:
	mov     r0, r3                      /* r3 = raw_map */
	swap.b  r0, r1
	mov     #5, r2
	shld    r2, r1
	mov.w   .Lsou_7fe0, r2
	and     r2, r1                      /* r1 = tile_id << 5 */
	add     r7, r1                      /* r1 = tile_ptr (top row) */
	tst     #0x08, r0                   /* T = !vflip */
	mov     #4, r5
	bt      1f
	mov.l   @(0, r15), r2               /* vflip_off */
	add     r2, r1
	mov     #-4, r5
1:	mov.l   r5, @(12, r15)              /* row_step = +4 or -4 */

	mov.l   @(4, r15), r9               /* r9 = k */
.Lsou_scan:
	mov.l   @r1, r2
	add     r5, r1
	tst     r2, r2
	bf      .Lsou_nz0
	dt      r9
	bf.s    .Lsou_scan
	add     r11, r6

	mov     r3, r12
	bra     .Lsou_tile_next
	nop

.Lsou_known_empty:
	dt      r8
	bt.s    .Lsou_done
	add     #16, r6
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bf      .Lsou_tile_have_r0
	bra     .Lsou_ke_run
	nop

.Lsou_nz0:
	mov     r3, r0
	tst     #0x04, r0                   /* T = !hflip */
	and     #0xF0, r0
	shlr2   r0
	shll8   r0
	mov     r10, r5
	bf.s    .Lsou_to_hf
	add     r0, r5                      /* r5 = &pair_lut[pal][0] */

	mov     r13, r0
.Lsou_nhf_r0:
	not     r2, r3
	add     r2, r0
	and     r3, r0
	tst     r14, r0
	bf      .Lsou_nhf_part

	SOU_OPAQUE_XTRCT_NHF
.Lsou_nhf_next:
	dt      r9
	bt.s    .Lsou_tile_next
	add     r11, r6
	mov.l   @r1, r2
	mov.l   @(12, r15), r0
	add     r0, r1
	mov     r13, r0
	tst     r2, r2
	bf      .Lsou_nhf_r0
	bra     .Lsou_nhf_next
	nop

.Lsou_tile_next:
	mov.l   @(8, r15), r0               /* 16 - k*480 */
	dt      r8
	bf.s    .Lsou_tile
	add     r0, r6

.Lsou_done:
	sts     macl, r8
	tst     r8, r8
	bf      .Lsou_span2
	add     #16, r15                    /* pop vflip_off, k, dst_next_delta, row_step */
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lsou_span2:
	sts     mach, r4
	mov     #0, r0
	bra     .Lsou_tile
	lds     r0, macl

.Lsou_to_hf:
	bra     .Lsou_hf_entry
	nop

	.balign 4
.Lsou_eeeef:
	.long   0xEEEEEEEF
.Lsou_88888:
	.long   0x88888888
.Lsou_480:
	.word   480
.Lsou_7fe0:
	.word   0x7FE0
	.balign 4

.Lsou_nhf_part:
	SOU_PART_BYTE 12, 14, 1
	SOU_PART_BYTE  8, 10, 1
	SOU_PART_BYTE  4,  6, 1
	SOU_PART_BYTE  0,  2, 0
	bra     .Lsou_nhf_next
	nop

	CGBA_STRIP_OVER_KE_RUN .Lsou

.Lsou_hf_entry:
	mov     r13, r0
.Lsou_hf_r0:
	not     r2, r3
	add     r2, r0
	and     r3, r0
	tst     r14, r0
	bf      .Lsou_hf_part
	SOU_OPAQUE_XTRCT_HF
.Lsou_hf_next:
	dt      r9
	bt.s    .Lsou_hf_tile_done
	add     r11, r6
	mov.l   @r1, r2
	mov.l   @(12, r15), r0
	add     r0, r1
	mov     r13, r0
	tst     r2, r2
	bf      .Lsou_hf_r0
	bra     .Lsou_hf_next
	nop
.Lsou_hf_tile_done:
	bra     .Lsou_tile_next
	nop

.Lsou_hf_part:
	SOU_PART_BYTE  2,  0, 1
	SOU_PART_BYTE  6,  4, 1
	SOU_PART_BYTE 10,  8, 1
	SOU_PART_BYTE 14, 12, 0
	bra     .Lsou_hf_next
	nop
	.size   _cgba_render_4bpp_strip_over_u16_sh4, .-_cgba_render_4bpp_strip_over_u16_sh4

	.section .text._cgba_render_affine_rot_over_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_affine_rot_over_sh4
	.type   _cgba_render_affine_rot_over_sh4, @function
_cgba_render_affine_rot_over_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	mov     r4, r0
	mov.l   @(48, r0), r2               /* tile_kind */
	mov.l   r2, @-r15                   /* @(0, sp) = tile_kind */
	mov.l   @(0, r0), r4
	mov.l   @(4, r0), r5
	mov.l   @(8, r0), r6
	mov.l   @(12, r0), r7               /* r7 = signed pal (+128) */
	mov.l   @(16, r0), r8
	mov.l   @(20, r0), r9
	mov.l   @(24, r0), r10
	mov.l   @(28, r0), r11
	mov.l   @(32, r0), r14
	mov.l   @(36, r0), r12              /* r12 = pix_shift */
	mov.l   @(40, r0), r13              /* row_shift */
	lds     r13, macl                   /* macl = row_shift */
	mov.l   @(44, r0), r0               /* flags: bit0=same_texel */
	mov     r12, r13
	add     #3, r13                     /* r13 = sub_y_shift = -(22 - map_size) */
	mov     #3, r2
	sub     r12, r2                     /* 28 - map_size (25..28) */
	mov     #-1, r1
	shld    r2, r1                      /* r1 = tile_mask */
	lds     r1, mach
	tst     #1, r0
	bt      .Lafbg_o_t0_miss
	bra     .Lafbg_o_same_miss
	nop

	/* Shared tile-classified empty & solid fast loops (12 insns/px). */
.macro AFBG_OVER_EMPTY_SOLID pfx
	.balign 4
\pfx\()_empty_step:
	sts     mach, r1                    /* r1 = tile_mask */
	dt      r14
	bt      \pfx\()_done_bra
\pfx\()_empty_next:
	mov     r8, r2
	add     r10, r8
	xor     r8, r2
	mov     r9, r0
	add     r11, r9
	xor     r9, r0
	or      r0, r2
	tst     r1, r2                      /* same empty 8x8 tile? */
	bf      \pfx\()_miss
	dt      r14
	bf.s    \pfx\()_empty_next
	add     #-2, r4
	bra     .Lafbg_o_done
	nop

	.balign 4
\pfx\()_solid_enter:
	mov.b   @r3, r0
	add     r0, r0
	mov.w   @(r0, r7), r3               /* r3 = solid RGB565 color (signed pal) */
	mov.w   r3, @r4
	dt      r14
	bt      \pfx\()_done_bra
\pfx\()_solid_next:
	mov     r8, r2
	add     r10, r8
	xor     r8, r2
	mov     r9, r0
	add     r11, r9
	xor     r9, r0
	or      r0, r2
	tst     r1, r2                      /* same solid 8x8 tile? */
	bf      \pfx\()_miss
	mov.w   r3, @-r4
	dt      r14
	bf      \pfx\()_solid_next
\pfx\()_done_bra:
	bra     .Lafbg_o_done
	nop
.endm

	/* Tile-classified overlay loop using per-CBB tile_kind[256]:
	 *   kind == 0 (empty):  12 insns/pixel, zero memory reads.
	 *   kind == 1 (solid):  12 insns/pixel, direct mov.w r3, @-r4.
	 *   kind == 3 (opaque): no zero-check per texel, signed pal lookup.
	 *   kind == 2 (mixed):  cur_tile_ptr cached in r3, signed pal lookup. */
	AFBG_OVER_EMPTY_SOLID .Lafbg_o_t0

	.balign 4
.Lafbg_o_t0_miss:
	mov     r8, r0
	shld    r12, r0
	mov     r0, r1
	and     #7, r0
	mov     r0, r2
	shlr2   r1
	shlr    r1

	mov     r9, r0
	shld    r12, r0
	mov     r0, r3
	and     #7, r0
	xor     r0, r3
	shll2   r0
	add     r0, r0
	or      r0, r2
	mov     r3, r0
	sts     macl, r3
	shld    r3, r0
	or      r1, r0

	mov.b   @(r0, r5), r0               /* tile_id */
	extu.b  r0, r0
	mov     r0, r1
	mov.l   @(0, r15), r3               /* tile_kind */
	mov.b   @(r0, r3), r0               /* 0=empty, 1=solid, 2=mixed, 3=opaque */
	tst     r0, r0
	bt.s    .Lafbg_o_t0_empty_step
	add     #-2, r4
	shll8   r1
	shlr2   r1                          /* tile_id << 6 */
	mov     r6, r3
	add     r1, r3                      /* r3 = cur_tile_ptr */
	sts     mach, r1                    /* r1 = tile_mask */
	cmp/eq  #1, r0
	bt      .Lafbg_o_t0_solid_enter
	cmp/eq  #3, r0
	bt      .Lafbg_o_t0_opaq_enter

	/* Mixed textured tile (tile_kind == 2). */
	mov     r2, r0
	mov.b   @(r0, r3), r0
	tst     r0, r0
	bt      1f
	add     r0, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @r4
1:	dt      r14
	bf      .Lafbg_o_t0_tile_next
	bra     .Lafbg_o_done
	nop

	.balign 4
.Lafbg_o_t0_tile_next:
	mov     r8, r2
	add     r10, r8
	xor     r8, r2
	mov     r9, r0
	add     r11, r9
	xor     r9, r0
	or      r0, r2
	tst     r1, r2                      /* same 8x8 tile? */
	bf      .Lafbg_o_t0_miss
	mov     r8, r0
	shld    r12, r0
	and     #7, r0
	mov     r0, r2
	mov     r9, r0
	shld    r13, r0
	and     #0x38, r0
	or      r2, r0
	mov.b   @(r0, r3), r0
	tst     r0, r0
	bt.s    2f
	add     #-2, r4
	add     r0, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @r4
2:	dt      r14
	bf      .Lafbg_o_t0_tile_next
	bra     .Lafbg_o_done
	nop

	/* Opaque textured tile (tile_kind == 3): zero transparent pixels, no tst/bt. */
	.balign 4
.Lafbg_o_t0_opaq_enter:
	mov     r2, r0
	mov.b   @(r0, r3), r0
	add     r0, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @r4
	dt      r14
	bf      .Lafbg_o_t0_opaq_next
	bra     .Lafbg_o_done
	nop
.Lafbg_o_t0_opaq_next:
	mov     r8, r2
	add     r10, r8
	xor     r8, r2
	mov     r9, r0
	add     r11, r9
	xor     r9, r0
	or      r0, r2
	tst     r1, r2                      /* same opaque 8x8 tile? */
	bf      .Lafbg_o_t0_miss
	mov     r8, r0
	shld    r12, r0
	and     #7, r0
	mov     r0, r2
	mov     r9, r0
	shld    r13, r0
	and     #0x38, r0
	or      r2, r0
	mov.b   @(r0, r3), r0
	add     r0, r0
	mov.w   @(r0, r7), r0
	dt      r14
	bf.s    .Lafbg_o_t0_opaq_next
	mov.w   r0, @-r4
	bra     .Lafbg_o_done
	nop

	.balign 4
.Lafbg_o_done:
	add     #4, r15
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	/* Small-step same-texel cached loop with tile_kind empty/solid fast paths. */
	.balign 4
.Lafbg_o_same_miss:
	mov     r8, r0
	shld    r12, r0
	mov     r0, r1
	and     #7, r0
	mov     r0, r2
	shlr2   r1
	shlr    r1

	mov     r9, r0
	shld    r12, r0
	mov     r0, r3
	and     #7, r0
	xor     r0, r3
	shll2   r0
	add     r0, r0
	or      r0, r2
	mov     r3, r0
	sts     macl, r3
	shld    r3, r0
	or      r1, r0

	mov.b   @(r0, r5), r0               /* tile_id */
	extu.b  r0, r0
	mov     r0, r1
	mov.l   @(0, r15), r3               /* tile_kind */
	mov.b   @(r0, r3), r0               /* 0=empty, 1=solid, 2/3=textured */
	tst     r0, r0
	bt.s    .Lafbg_o_same_empty_step
	add     #-2, r4
	shll8   r1
	shlr2   r1                          /* tile_id << 6 */
	mov     r6, r3
	add     r1, r3                      /* r3 = cur_tile_ptr */
	sts     mach, r1                    /* r1 = tile_mask */
	cmp/eq  #1, r0
	bt      .Lafbg_o_same_solid_enter

	mov     r2, r0
	mov.b   @(r0, r3), r0
	tst     r0, r0
	bt      .Lafbg_o_same_trans
	add     r0, r0
	mov.w   @(r0, r7), r1
	extu.w  r1, r1                      /* r1 >= 0: opaque cached color */
	mov.w   r1, @r4
	dt      r14
	bf      .Lafbg_o_same_next
	bra     .Lafbg_o_done
	nop
.Lafbg_o_same_trans:
	mov     #-1, r1                     /* r1 < 0: transparent */
	dt      r14
	bt      .Lafbg_o_same_done_bra

	.balign 4
.Lafbg_o_same_next:
	mov     r8, r2
	add     r10, r8
	xor     r8, r2
	mov     r9, r0
	add     r11, r9
	xor     r9, r0
	or      r0, r2
	shld    r12, r2
	tst     r2, r2
	bf      .Lafbg_o_check_tile
	cmp/pz  r1
	bf.s    2f
	add     #-2, r4
	mov.w   r1, @r4
2:	dt      r14
	bf      .Lafbg_o_same_next
	bra     .Lafbg_o_done
	nop

	.balign 4
.Lafbg_o_check_tile:
	shlr2   r2
	shlr    r2
	tst     r2, r2
	bf      .Lafbg_o_same_miss
	mov     r8, r0
	shld    r12, r0
	and     #7, r0
	mov     r0, r2
	mov     r9, r0
	shld    r13, r0
	and     #0x38, r0
	or      r2, r0
	mov.b   @(r0, r3), r0
	tst     r0, r0
	bt.s    .Lafbg_o_same_trans
	add     #-2, r4
	add     r0, r0
	mov.w   @(r0, r7), r1
	extu.w  r1, r1
	mov.w   r1, @r4
	dt      r14
	bf      .Lafbg_o_same_next
	bra     .Lafbg_o_done
	nop

	AFBG_OVER_EMPTY_SOLID .Lafbg_o_same
	.purgem AFBG_OVER_EMPTY_SOLID
	.size   _cgba_render_affine_rot_over_sh4, .-_cgba_render_affine_rot_over_sh4

/* ============================================================================
 * Single-scanline 4-bpp direct-RGB565 additive-saturating alpha-blend span
 * renderer for evb == 16 (any eva in 0..16).
 *
 * Inlines the common non-saturating path (~10 insns/pixel for solid rows,
 * ~11 insns/pixel for mixed rows) and branches out-of-line only on rare
 * per-channel overflow.  Keeps a 1-entry empty-tile raw-map cache.
 *
 * void cgba_render_4bpp_span_blend_evb16_sh4(
 *   const u16 *map_ptr,      // r4
 *   u32 todraw,              // r5
 *   u16 *dest_ptr,           // r6
 *   const u8 *tile_base,     // r7
 *   s32 vflip_off,           // @(0, sp_in)
 *   const u32 *term_a_s4)    // @(4, sp_in)
 * ============================================================================ */
.macro SBE16_PIX disp
	mov.l   @(r0, r5), r2               /* r2 = term_a_s4[pix] */
	mov.w   @(\disp, r6), r0            /* r0 = dst_pix */
	extu.w  r0, r0
	swap.w  r0, r3
	or      r3, r0
	and     r13, r0                     /* p2e */
	add     r2, r0                      /* pfe = term_a_s4 + p2e */
	tst     r14, r0
	bt.s    1f
	swap.w  r0, r3
	bsr     .Lsbe16_sat
	nop
1:	or      r3, r0
	mov.w   r0, @(\disp, r6)
.endm

.macro SBE16_BYTE disp_lo, disp_hi
	mov     r12, r0
	and     #0x0F, r0
	tst     r0, r0
	bt      2f
	shll2   r0
	SBE16_PIX \disp_lo
2:	mov     r12, r0
	shlr2   r0
	and     #0x3C, r0
	tst     r0, r0
	bt      3f
	SBE16_PIX \disp_hi
3:
.endm

.macro SBE16_SOLID_PIX disp
	mov.w   @(\disp, r6), r0            /* r0 = dst_pix */
	extu.w  r0, r0
	swap.w  r0, r3
	or      r3, r0
	and     r13, r0                     /* p2e */
	add     r2, r0                      /* pfe = term_a_s4[pix] + p2e */
	tst     r14, r0
	bt.s    1f
	swap.w  r0, r3
	bsr     .Lsbe16_sat
	nop
1:	or      r3, r0
	mov.w   r0, @(\disp, r6)
.endm

	.section .text._cgba_render_4bpp_span_blend_evb16_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_4bpp_span_blend_evb16_sh4
	.type   _cgba_render_4bpp_span_blend_evb16_sh4, @function
_cgba_render_4bpp_span_blend_evb16_sh4:
	tst     r5, r5
	bt      .Lsbe16_ret_early
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15
	sts.l   pr, @-r15

	mov     r5, r8                      /* r8  = todraw */
	mov.l   @(32, r15), r9              /* r9  = vflip_off */
	mov.l   @(36, r15), r10             /* r10 = term_a_s4 */
	mov.w   .Lsbe16_7fe0, r11           /* r11 = 0x00007FE0 */
	mov.l   .Lsbe16_blnd_msk, r13       /* r13 = 0x07E0F81F */
	mov.l   .Lsbe16_ovf_msk, r14        /* r14 = 0x08010020 */
	lds     r13, mach                   /* mach = last_empty_raw sentinel */
	bra     .Lsbe16_loop
	nop

.Lsbe16_ret_early:
	rts
	nop

	.balign 4
.Lsbe16_blnd_msk:
	.long   0x07E0F81F
.Lsbe16_ovf_msk:
	.long   0x08010020
.Lsbe16_7fe0:
	.word   0x7FE0

	.balign 4
.Lsbe16_done:
	lds.l   @r15+, pr
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	.balign 4
.Lsbe16_loop:
	mov.w   @r4+, r0
	sts     mach, r1
	cmp/eq  r1, r0
	bt      .Lsbe16_skip
	swap.b  r0, r1
	shll2   r1
	add     r1, r1
	shll2   r1
	and     r11, r1                     /* (tile_id & 0x3FF) << 5 */
	tst     #0x08, r0                   /* T = !vflip */
	bt.s    1f
	add     r7, r1
	add     r9, r1
1:	mov.l   @r1, r12                    /* r12 = raw [b0 b1 b2 b3] */
	tst     r12, r12
	bt.s    .Lsbe16_mark_empty
	mov     r0, r3                      /* r3 = raw_map */

	and     #0xF0, r0
	shll2   r0                          /* subpal * 64 */
	mov     r10, r5
	add     r0, r5                      /* r5 = &term_a_s4[subpal * 16] */

	/* Solid-row check: (((raw << 4) ^ raw) >> 4) == 0 */
	mov     r12, r0
	shll2   r0
	shll2   r0
	xor     r12, r0
	shlr2   r0
	shlr2   r0
	tst     r0, r0
	bt      .Lsbe16_to_solid

	mov     r3, r0
	tst     #0x04, r0                   /* T = !hflip */
	bt      .Lsbe16_nohf
	bra     .Lsbe16_hf
	nop

.Lsbe16_to_solid:
	bra     .Lsbe16_solid
	nop

.Lsbe16_mark_empty:
	lds     r3, mach
.Lsbe16_skip:
	dt      r8
	bf.s    .Lsbe16_loop
	add     #16, r6
	bra     .Lsbe16_done
	nop

.Lsbe16_nohf:
	SBE16_BYTE 12, 14                   /* b3: p6, p7 */
	shlr8   r12
	SBE16_BYTE 8, 10                    /* b2: p4, p5 */
	shlr8   r12
	SBE16_BYTE 4, 6                     /* b1: p2, p3 */
	shlr8   r12
	SBE16_BYTE 0, 2                     /* b0: p0, p1 */

.Lsbe16_next:
	dt      r8
	bt      .Lsbe16_done_far
	bra     .Lsbe16_loop
	add     #16, r6
.Lsbe16_done_far:
	bra     .Lsbe16_done
	nop

	.balign 4
.Lsbe16_hf:
	SBE16_BYTE 2, 0                     /* b3: p6->2, p7->0 */
	shlr8   r12
	SBE16_BYTE 6, 4                     /* b2: p4->6, p5->4 */
	shlr8   r12
	SBE16_BYTE 10, 8                    /* b1: p2->10, p3->8 */
	shlr8   r12
	SBE16_BYTE 14, 12                   /* b0: p0->14, p1->12 */
	bra     .Lsbe16_next
	nop

	/* Solid-row path: r2 = term_a_s4[pix] loaded once for all 8 pixels. */
	.balign 4
.Lsbe16_solid:
	mov     r12, r0
	and     #0x0F, r0
	shll2   r0
	mov.l   @(r0, r5), r2               /* r2 = term_a_s4[pix] */
	SBE16_SOLID_PIX 0
	SBE16_SOLID_PIX 2
	SBE16_SOLID_PIX 4
	SBE16_SOLID_PIX 6
	SBE16_SOLID_PIX 8
	SBE16_SOLID_PIX 10
	SBE16_SOLID_PIX 12
	SBE16_SOLID_PIX 14
	bra     .Lsbe16_next
	nop

	/* Rare per-channel saturation helper:
	 *   in:  r0 = pfe (with at least one bit of r14 set), r13 = BLND_MSK, r14 = OVF_MSK
	 *   out: r0 = clamped expanded pfe, r3 = swap.w(r0) (preserves r2, r4..r14) */
	.balign 4
.Lsbe16_sat:
	CGBA_SWAR_SAT_PFE r0, r14, r3, r1
	and     r13, r0
	rts
	swap.w  r0, r3
	.size   _cgba_render_4bpp_span_blend_evb16_sh4, .-_cgba_render_4bpp_span_blend_evb16_sh4

/* ============================================================================
 * Unpack a 4-bpp text BG tile span into pre-shifted nibble bytes (pix << 1,
 * values 0..30) while verifying every non-empty tile uses expected_pal_be
 * (or establishing expected_pal_be on the first non-empty tile when < 0).
 *
 * Returns:
 *   >= 0: confirmed subpalette (0..15), or 16 if all tiles were empty
 *   -1  : mixed subpalettes detected (caller falls back to non-fused path)
 *
 * s32 cgba_unpack_4bpp_nibs_sh4(
 *   const u16 *map_ptr,        // r4
 *   u32 todraw,                // r5
 *   u8 *dst_nibs,              // r6 (4-byte aligned, todraw * 8 bytes)
 *   const u8 *tile_base,       // r7
 *   s32 vflip_off,             // @(0, sp_in)
 *   const u16 *unpack_lut,     // @(4, sp_in) 256-entry (p0<<1, p1<<1) pairs
 *   s32 expected_subpal)       // @(8, sp_in) -1 (unassigned) or 0..15
 * ============================================================================ */
	.section .text._cgba_unpack_4bpp_nibs_sh4,"ax",@progbits
	.balign 4
	.global _cgba_unpack_4bpp_nibs_sh4
	.type   _cgba_unpack_4bpp_nibs_sh4, @function
_cgba_unpack_4bpp_nibs_sh4:
	tst     r5, r5
	bt      .Lun4_ret_empty
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	mov     r5, r8                      /* r8  = todraw */
	mov.l   @(28, r15), r9              /* r9  = vflip_off */
	mov.l   @(32, r15), r10             /* r10 = unpack_lut */
	mov.l   @(36, r15), r13             /* r13 = expected_subpal (-1 or 0..15) */
	cmp/pz  r13
	bf      1f
	shll2   r13
	shll2   r13                         /* r13 = expected_pal_be (subpal << 4) */
1:	mov.w   .Lun4_7fe0, r11             /* r11 = 0x7FE0 */
	mov.l   .Lun4_sentinel, r14         /* r14 = prev_raw (never matches u16) */

	.balign 4
.Lun4_loop:
	mov.w   @r4+, r0
	cmp/eq  r14, r0
	bt      .Lun4_rep
	mov     r0, r14                     /* prev_raw = raw */
	swap.b  r0, r1
	shll2   r1
	add     r1, r1
	shll2   r1
	and     r11, r1                     /* (tile_id & 0x3FF) << 5 */
	tst     #0x08, r0                   /* T = !vflip */
	bt.s    2f
	add     r7, r1
	add     r9, r1
2:	mov.l   @r1, r12                    /* r12 = [b0 b1 b2 b3] */
	tst     r12, r12
	bt      .Lun4_zero

	mov     r0, r3                      /* r3 = raw */
	and     #0xF0, r0                   /* r0 = pal_be (subpal << 4) */
	cmp/eq  r13, r0
	bt      3f
	cmp/pz  r13
	bt      .Lun4_mixed
	mov     r0, r13                     /* lock subpalette on first non-empty tile */
3:	mov     r3, r0
	tst     #0x04, r0                   /* T = !hflip */
	bf      .Lun4_hf

	extu.b  r12, r0                     /* b3 */
	add     r0, r0
	mov.w   @(r0, r10), r0
	shlr8   r12
	mov.w   r0, @(6, r6)
	extu.b  r12, r0                     /* b2 */
	add     r0, r0
	mov.w   @(r0, r10), r0
	shlr8   r12
	mov.w   r0, @(4, r6)
	extu.b  r12, r0                     /* b1 */
	add     r0, r0
	mov.w   @(r0, r10), r0
	shlr8   r12
	mov.w   r0, @(2, r6)
	extu.b  r12, r0                     /* b0 */
	add     r0, r0
	mov.w   @(r0, r10), r0
	dt      r8
	mov.w   r0, @r6
	bf.s    .Lun4_loop
	add     #8, r6

.Lun4_done:
	cmp/pz  r13
	bf      4f
	mov     r13, r0
	shlr2   r0
	shlr2   r0                          /* return subpal (0..15) */
	bra     .Lun4_epilogue
	nop
4:	mov     #16, r0                     /* all tiles empty */
.Lun4_epilogue:
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lun4_ret_empty:
	mov.l   @(8, r15), r0
	cmp/pz  r0
	bt      1f
	mov     #16, r0
1:	rts
	nop

	.balign 4
.Lun4_sentinel:
	.long   0x00010000
.Lun4_7fe0:
	.word   0x7FE0

	.balign 4
.Lun4_mixed:
	bra     .Lun4_epilogue
	mov     #-1, r0

.Lun4_zero:
	mov     #0, r0
	mov.l   r0, @r6
	dt      r8
	mov.l   r0, @(4, r6)
	bf.s    .Lun4_loop
	add     #8, r6
	bra     .Lun4_done
	nop

.Lun4_rep:
	mov     r6, r1
	add     #-8, r1
	mov.l   @r1+, r2
	mov.l   @r1, r3
	mov.l   r2, @r6
	dt      r8
	mov.l   r3, @(4, r6)
	bf.s    .Lun4_loop
	add     #8, r6
	bra     .Lun4_done
	nop

.Lun4_hf:
	extu.b  r12, r0                     /* b3 -> p7,p6 at [0],[1] */
	add     r0, r0
	mov.w   @(r0, r10), r0
	swap.b  r0, r0
	shlr8   r12
	mov.w   r0, @r6
	extu.b  r12, r0                     /* b2 -> p5,p4 at [2],[3] */
	add     r0, r0
	mov.w   @(r0, r10), r0
	swap.b  r0, r0
	shlr8   r12
	mov.w   r0, @(2, r6)
	extu.b  r12, r0                     /* b1 -> p3,p2 at [4],[5] */
	add     r0, r0
	mov.w   @(r0, r10), r0
	swap.b  r0, r0
	shlr8   r12
	mov.w   r0, @(4, r6)
	extu.b  r12, r0                     /* b0 -> p1,p0 at [6],[7] */
	add     r0, r0
	mov.w   @(r0, r10), r0
	swap.b  r0, r0
	dt      r8
	mov.w   r0, @(6, r6)
	bf.s    .Lun4_loop
	add     #8, r6
	bra     .Lun4_done
	nop
	.size   _cgba_unpack_4bpp_nibs_sh4, .-_cgba_unpack_4bpp_nibs_sh4

/* ============================================================================
 * Fused 4-bpp-over-4-bpp direct 16x16 LUT blend span renderer.
 *
 * Each byte of base_nibs[] holds (base_pix << 1) in 0..30.
 * lut[256] holds RGB565 output for ((cand_pix << 4) | base_pix), so byte
 * offset is (cand_pix << 5) + (base_pix << 1).
 *
 * Empty cand tiles and solid cand tiles both run a 3.0-insn/pixel shared loop;
 * mixed cand tiles run a branchless 8.5-insn/pixel loop.
 * Stops early and returns tiles completed if a non-empty tile uses a different
 * subpalette than expected_pal_be (caller switches LUT and resumes).
 *
 * struct cgba_fused_blend_params {
 *   const u16 *map_ptr;       // +0
 *   const u8  *base_nibs;     // +4
 *   u16       *dest_ptr;      // +8
 *   const u16 *lut;           // +12
 *   const u8  *tile_base;     // +16
 *   s32        vflip_off;     // +20
 *   u32        todraw;        // +24
 *   u32        expected_pal;  // +28  (sub_cand << 4)
 * };
 * u32 cgba_render_4bpp_span_fused_blend_sh4(const cgba_fused_blend_params *p);
 * ============================================================================ */
.macro FBL_NOHF_BYTE disp_lo, disp_hi
	mov.b   @r1+, r0
	mov     r0, r2
	and     #0x0F, r0
	shll2   r0
	shll2   r0
	shll    r0                          /* p0 << 5 */
	mov.b   @r5+, r3                    /* base0 << 1 */
	add     r3, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(\disp_lo, r6)
	mov     r2, r0
	and     #0xF0, r0                   /* p1 << 4 */
	shll    r0                          /* p1 << 5 */
	mov.b   @r5+, r3                    /* base1 << 1 */
	add     r3, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(\disp_hi, r6)
.endm

.macro FBL_HF_BYTE boff, disp_lo, disp_hi
	mov.b   @(\boff, r1), r0
	mov     r0, r2
	and     #0xF0, r0                   /* p1 << 4 */
	shll    r0                          /* p1 << 5 */
	mov.b   @r5+, r3                    /* base0 << 1 */
	add     r3, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(\disp_lo, r6)
	mov     r2, r0
	and     #0x0F, r0
	shll2   r0
	shll2   r0
	shll    r0                          /* p0 << 5 */
	mov.b   @r5+, r3                    /* base1 << 1 */
	add     r3, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(\disp_hi, r6)
.endm

	.section .text._cgba_render_4bpp_span_fused_blend_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_4bpp_span_fused_blend_sh4
	.type   _cgba_render_4bpp_span_fused_blend_sh4, @function
_cgba_render_4bpp_span_fused_blend_sh4:
	mov.l   @(24, r4), r0               /* todraw */
	tst     r0, r0
	bt      .Lfbl_ret_zero
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	mov     r4, r0
	mov.l   @(0,  r0), r4               /* r4  = map_ptr */
	mov.l   @(4,  r0), r5               /* r5  = base_nibs */
	mov.l   @(8,  r0), r6               /* r6  = dest_ptr */
	mov.l   @(12, r0), r7               /* r7  = lut */
	mov.l   @(16, r0), r9               /* r9  = tile_base */
	mov.l   @(20, r0), r10              /* r10 = vflip_off */
	mov.l   @(24, r0), r8               /* r8  = todraw (remaining) */
	mov.l   @(28, r0), r13              /* r13 = expected_pal_be */
	mov     r8, r14                     /* r14 = orig_todraw */
	mov.w   .Lfbl_7fe0, r11             /* r11 = 0x7FE0 */
	bra     .Lfbl_loop
	nop

.Lfbl_ret_zero:
	rts
	mov     #0, r0

	.balign 4
.Lfbl_7fe0:
	.word   0x7FE0

	.balign 4
.Lfbl_pal_change:
	mov     r14, r0
	sub     r8, r0                      /* tiles completed before mismatch */
	bra     .Lfbl_epilogue
	nop

.Lfbl_done:
	mov     r14, r0
.Lfbl_epilogue:
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	.balign 4
.Lfbl_loop:
	mov.w   @r4+, r0
	swap.b  r0, r1
	shll2   r1
	add     r1, r1
	shll2   r1
	and     r11, r1                     /* (tile_id & 0x3FF) << 5 */
	tst     #0x08, r0                   /* T = !vflip */
	bt.s    1f
	add     r9, r1
	add     r10, r1
1:	mov.l   @r1, r12                    /* r12 = [b0 b1 b2 b3] */
	tst     r12, r12
	bt.s    .Lfbl_row_copy
	mov     r7, r3                      /* empty: r3 = lut row 0 */

	mov     r0, r2                      /* r2 = raw_map */
	and     #0xF0, r0
	cmp/eq  r13, r0
	bf      .Lfbl_pal_change

	/* Solid-row check: (((raw << 4) ^ raw) >> 4) == 0 */
	mov     r12, r0
	shll2   r0
	shll2   r0
	xor     r12, r0
	shlr2   r0
	shlr2   r0
	tst     r0, r0
	bt      .Lfbl_solid

	mov     r2, r0
	tst     #0x04, r0                   /* T = !hflip */
	bt      .Lfbl_nohf
	bra     .Lfbl_hf
	nop

	.balign 4
.Lfbl_solid:
	mov     r12, r0
	and     #0x0F, r0
	shll2   r0
	shll2   r0
	shll    r0                          /* pix << 5 */
	mov     r7, r3
	add     r0, r3                      /* r3 = &lut[pix * 16] */
.Lfbl_row_copy:
	mov.b   @r5+, r0
	mov.w   @(r0, r3), r0
	mov.w   r0, @r6
	mov.b   @r5+, r0
	mov.w   @(r0, r3), r0
	mov.w   r0, @(2, r6)
	mov.b   @r5+, r0
	mov.w   @(r0, r3), r0
	mov.w   r0, @(4, r6)
	mov.b   @r5+, r0
	mov.w   @(r0, r3), r0
	mov.w   r0, @(6, r6)
	mov.b   @r5+, r0
	mov.w   @(r0, r3), r0
	mov.w   r0, @(8, r6)
	mov.b   @r5+, r0
	mov.w   @(r0, r3), r0
	mov.w   r0, @(10, r6)
	mov.b   @r5+, r0
	mov.w   @(r0, r3), r0
	mov.w   r0, @(12, r6)
	mov.b   @r5+, r0
	mov.w   @(r0, r3), r0
	dt      r8
	mov.w   r0, @(14, r6)
	bt.s    .Lfbl_done_far
	add     #16, r6
	bra     .Lfbl_loop
	nop
.Lfbl_done_far:
	bra     .Lfbl_done
	nop

	.balign 4
.Lfbl_nohf:
	FBL_NOHF_BYTE 0, 2
	FBL_NOHF_BYTE 4, 6
	FBL_NOHF_BYTE 8, 10
	FBL_NOHF_BYTE 12, 14

.Lfbl_next:
	dt      r8
	bt      .Lfbl_done_far
	bra     .Lfbl_loop
	add     #16, r6

	.balign 4
.Lfbl_hf:
	FBL_HF_BYTE 3, 0, 2
	FBL_HF_BYTE 2, 4, 6
	FBL_HF_BYTE 1, 8, 10
	FBL_HF_BYTE 0, 12, 14
	bra     .Lfbl_next
	nop
	.size   _cgba_render_4bpp_span_fused_blend_sh4, .-_cgba_render_4bpp_span_fused_blend_sh4

/* ============================================================================
 * Unaligned (2-byte aligned dst_top = 4k+2) tile-major k-row 4bpp strip
 * base-renderer for odd tile_hoff (1, 3, 5, 7) scrolling text BGs.
 * Uses xtrct to write 3 aligned mov.l + 2 mov.w per 8-pixel row and fully
 * 4-byte-aligned mov.l bursts for consecutive identical tile runs.
 *
 * void cgba_render_4bpp_strip_base_u_sh4(
 *   const u16 *map1,          // r4
 *   u32 counts_packed,        // r5: todraw1 | (todraw2 << 16), todraw1 >= 1
 *   u16 *dst_top,             // r6 (2-byte aligned = 4k+2)
 *   const u8 *tile_charbase,  // r7 (32-byte aligned)
 *   u32 start_r_and_k,        // @(0, sp_in): (start_r << 8) | k
 *   const u32 *pair_lut,      // @(4, sp_in)
 *   const u16 *map2)          // @(8, sp_in) (only when todraw2 != 0)
 * ============================================================================ */
	.section .text._cgba_render_4bpp_strip_base_u16_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_4bpp_strip_base_u16_sh4
	.type   _cgba_render_4bpp_strip_base_u16_sh4, @function
_cgba_render_4bpp_strip_base_u16_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	extu.w  r5, r8                      /* r8  = todraw1 */
	shlr16  r5                          /* r5  = todraw2 */
	lds     r5, mach                    /* mach = todraw2 */
	tst     r5, r5
	bt      1f
	mov.l   @(36, r15), r0              /* map2 */
	lds     r0, macl                    /* macl = map2 */
1:	mov.l   @(32, r15), r10             /* r10 = pair_lut */
	mov.l   @(28, r15), r0              /* (start_r << 8) | k */
	extu.b  r0, r13                     /* r13 = k (1..8) */
	shlr8   r0
	shll2   r0                          /* r0  = start_r * 4 */
	add     r0, r7                      /* r7  = tile_charbase + start_r*4 */
	add     r0, r0
	mov     #28, r3
	sub     r0, r3                      /* r3  = vflip_off = 28 - 2*start_r*4 */

	mov     r13, r0
	shll8   r0
	add     r0, r0                      /* k * 512 */
	mov     r13, r1
	shll2   r1
	shll2   r1
	add     r1, r1                      /* k * 32 */
	sub     r1, r0                      /* k * 480 */
	mov     #16, r1
	sub     r0, r1                      /* r1  = dst_next_delta = 16 - k*480 */
	mov     #4, r0
	mov.l   r0, @-r15                   /* @(8, r15) = row_step (+4 or -4) */
	mov.l   r1, @-r15                   /* @(4, r15) = dst_next_delta */
	mov.l   r3, @-r15                   /* @(0, r15) = vflip_off */

	mov.w   .Lsbu_480, r11              /* r11 = 480 */
	mov.l   .Lsbu_10000, r12            /* r12 = sentinel prev1 */

	.balign 4
.Lsbu_tile:
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bt      .Lsbu_to_rep1
	mov     r0, r12
	swap.b  r0, r1
	mov     #5, r2
	shld    r2, r1
	mov.w   .Lsbu_7fe0, r2
	and     r2, r1                      /* r1 = tile_id << 5 */
	add     r7, r1                      /* r1 = tile_ptr (top row) */
	tst     #0x08, r0                   /* T = !vflip */
	mov     #4, r2
	bt      1f
	mov.l   @(0, r15), r2               /* vflip_off */
	add     r2, r1
	mov     #-4, r2
1:	mov.l   r2, @(8, r15)               /* row_step = +4 or -4 */

	tst     #0x04, r0                   /* T = !hflip */
	and     #0xF0, r0
	shlr2   r0
	shll8   r0
	mov     r10, r5
	bf.s    .Lsbu_to_hf
	add     r0, r5                      /* r5 = &pair_lut[pal][0] */

	mov     r13, r9                     /* r9 = k rows */
	add     #2, r6                      /* r6 = dst + 2 (4-byte aligned!) */
	add     #2, r11                     /* r11 = 482 */
.Lsbu_nhf_row:
	mov.l   @r1, r2
	mov.l   @(8, r15), r0
	add     r0, r1
	extu.b  r2, r0                      /* b3 -> [p6|p7] */
	shll2   r0
	mov.l   @(r0, r5), r14
	shlr8   r2
	mov     r14, r0
	mov.w   r0, @(12, r6)               /* dest[7] = p7 */

	extu.b  r2, r0                      /* b2 -> [p4|p5] */
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	xtrct   r3, r14                     /* [p5|p6] */
	mov.l   r14, @(8, r6)               /* dest[5..6] */

	extu.b  r2, r0                      /* b1 -> [p2|p3] */
	shll2   r0
	mov.l   @(r0, r5), r14
	shlr8   r2
	xtrct   r14, r3                     /* [p3|p4] */
	mov.l   r3, @(4, r6)                /* dest[3..4] */

	extu.b  r2, r0                      /* b0 -> [p0|p1] */
	shll2   r0
	mov.l   @(r0, r5), r3
	xtrct   r3, r14                     /* [p1|p2] */
	mov.l   r14, @r6                    /* dest[1..2] */
	swap.w  r3, r3                      /* low 16 = p0 */
	dt      r9
	mov.w   r3, @-r6                    /* r6 = row_i, dest[0] = p0 */
	bf.s    .Lsbu_nhf_row
	add     r11, r6                     /* r6 = row_{i+1} + 2 */
.Lsbu_nhf_end:
	add     #-2, r6                     /* restore r6 = dst_top + k*480 */
	add     #-2, r11                    /* restore r11 = 480 */
	mov.l   @(4, r15), r0
	dt      r8
	bf.s    .Lsbu_tile
	add     r0, r6

.Lsbu_done:
	sts     mach, r8
	tst     r8, r8
	bf      .Lsbu_span2
	add     #12, r15                    /* pop vflip_off + dst_next_delta + row_step */
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lsbu_span2:
	sts     macl, r4
	mov     #0, r0
	bra     .Lsbu_tile
	lds     r0, mach

.Lsbu_to_rep1:
	bra     .Lsbu_rep1
	nop
.Lsbu_to_hf:
	bra     .Lsbu_hf
	nop

	.balign 4
.Lsbu_10000:
	.long   0x00010000
.Lsbu_480:
	.word   480
.Lsbu_7fe0:
	.word   0x7FE0
	.balign 4

	/* Identical consecutive tile(s) across all k rows (r6 = 4k+2):
	 * for m >= 2 tiles, write leading p0, fill m-1 tiles with 4-byte-aligned
	 * [p1|p2],[p3|p4],[p5|p6],[p7|p0_next] bursts, then write trailing p7. */
.Lsbu_rep1:
	mov     #1, r0
	cmp/eq  r0, r8
	bt      .Lsbu_rep1_single
	mov.w   @r4, r0
	cmp/eq  r12, r0
	bf      .Lsbu_rep1_single

	/* Count consecutive matching tiles m (2 <= m <= r8) */
	mov     #1, r2
	mov     r8, r3
	add     #-1, r3
.Lsbu_rep_cnt:
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bf      .Lsbu_rep_cnt_end
	add     #1, r2
	dt      r3
	bf      .Lsbu_rep_cnt
	bra     .Lsbu_rep_run_go
	nop
.Lsbu_rep_cnt_end:
	add     #-2, r4
.Lsbu_rep_run_go:
	sub     r2, r8                      /* r8 = remaining tiles after run */
	mov.l   r4, @-r15                   /* @(4, r15) = saved map_ptr */
	mov.l   r7, @-r15                   /* @(0, r15) = saved tile_charbase */
	mov     r2, r7                      /* r7 = m (>= 2) */
	mov     r13, r9                     /* r9 = k rows */
.Lsbu_rep_row:
	mov     r6, r1
	add     #-16, r1                    /* r1 = left neighbor (4k-14) */
	mov.w   @r1+, r0                    /* p0; r1 = 4k-12 */
	mov.l   @r1+, r2                    /* [p1|p2] */
	mov.l   @r1+, r3                    /* [p3|p4] */
	mov.l   @r1+, r5                    /* [p5|p6] */
	mov.w   @r1+, r14                   /* p7; r1 == r6 (4k+2) */
	mov.w   r0, @r1                     /* dest[0] = p0 */
	add     #2, r1                      /* r1 = 4k+4 (4-aligned) */
	shll16  r14
	extu.w  r0, r0
	or      r0, r14                     /* r14 = [p7|p0] */
	mov     r7, r4
	add     #-1, r4                     /* r4 = m - 1 (>= 1) */
.Lsbu_rep_tile:
	mov.l   r2, @r1
	mov.l   r3, @(4, r1)
	mov.l   r5, @(8, r1)
	mov.l   r14, @(12, r1)              /* [p7_cur | p0_next] */
	dt      r4
	bf.s    .Lsbu_rep_tile
	add     #16, r1
	mov.l   r2, @r1
	mov.l   r3, @(4, r1)
	mov.l   r5, @(8, r1)
	swap.w  r14, r0                     /* low 16 = p7 */
	dt      r9
	mov.w   r0, @(12, r1)
	bf.s    .Lsbu_rep_row
	add     r11, r6

	mov.l   @(12, r15), r0              /* dst_next_delta = 16 - k*480 */
	add     r0, r6
	add     #-1, r7                     /* m - 1 */
	shll2   r7
	shll2   r7                          /* (m - 1) * 16 */
	add     r7, r6                      /* dst_top + (tx + m) * 16 */
	mov.l   @r15+, r7
	mov.l   @r15+, r4
	tst     r8, r8
	bt      .Lsbu_done_bra
	bra     .Lsbu_tile
	nop

.Lsbu_rep1_single:
	mov     r13, r9
	mov     r6, r1
	add     #-16, r1
.Lsbu_rep1_row:
	mov.w   @r1+, r0                    /* p0 */
	mov.l   @r1+, r2                    /* [p1|p2] */
	mov.l   @r1+, r3                    /* [p3|p4] */
	mov.l   @r1+, r5                    /* [p5|p6] */
	mov.w   r0, @r6
	mov.w   @r1, r0                     /* p7 */
	add     #2, r6                      /* 4-aligned */
	mov.l   r2, @r6
	mov.l   r3, @(4, r6)
	mov.l   r5, @(8, r6)
	mov.w   r0, @(12, r6)
	add     #-14, r1
	add     r11, r1                     /* r1 = prev + 480 */
	add     #-2, r6
	dt      r9
	bf.s    .Lsbu_rep1_row
	add     r11, r6

	mov.l   @(4, r15), r0
	dt      r8
	bt.s    .Lsbu_done_bra
	add     r0, r6
	bra     .Lsbu_tile
	nop
.Lsbu_done_bra:
	bra     .Lsbu_done
	nop

	/* hflip (r6 = 4k+2, unified !vflip/vflip via @(8, r15) row_step) */
.Lsbu_hf:
	mov     r13, r9
.Lsbu_hf_row:
	mov.l   @r1, r2
	mov.l   @(8, r15), r0
	add     r0, r1
	extu.b  r2, r0                      /* b3 -> [p6|p7] */
	shll2   r0
	mov.l   @(r0, r5), r14
	shlr8   r2
	mov.w   r14, @r6                    /* dest[0] = p7 */
	add     #2, r6                      /* r6 = 4k+4 */

	extu.b  r2, r0                      /* b2 -> [p4|p5] */
	shll2   r0
	mov.l   @(r0, r5), r3
	shlr8   r2
	xtrct   r3, r14                     /* [p5|p6] */
	swap.w  r14, r14                    /* [p6|p5] */
	mov.l   r14, @r6                    /* dest[1..2] */

	extu.b  r2, r0                      /* b1 -> [p2|p3] */
	shll2   r0
	mov.l   @(r0, r5), r14
	shlr8   r2
	xtrct   r14, r3                     /* [p3|p4] */
	swap.w  r3, r3                      /* [p4|p3] */
	mov.l   r3, @(4, r6)                /* dest[3..4] */

	extu.b  r2, r0                      /* b0 -> [p0|p1] */
	shll2   r0
	mov.l   @(r0, r5), r3
	xtrct   r3, r14                     /* [p1|p2] */
	swap.w  r14, r14                    /* [p2|p1] */
	mov.l   r14, @(8, r6)               /* dest[5..6] */
	swap.w  r3, r0                      /* low 16 = p0 */
	dt      r9
	mov.w   r0, @(12, r6)               /* dest[7] = p0 */
	add     #-2, r6
	bf.s    .Lsbu_hf_row
	add     r11, r6

	mov.l   @(4, r15), r0
	dt      r8
	bt.s    .Lsbu_done_bra
	add     r0, r6
	bra     .Lsbu_tile
	nop
	.size   _cgba_render_4bpp_strip_base_u16_sh4, .-_cgba_render_4bpp_strip_base_u16_sh4

/* ============================================================================
 * _cgba_render_4bpp_part_strip_sh4
 *
 * Partial-tile k-row 4bpp FULLCOLOR strip renderer for scrolling text BG
 * left/right edge columns (1..7 px wide, 1..8 rows tall).
 *
 * Signature:
 *   void cgba_render_4bpp_part_strip_sh4(
 *     u16 *dst_top,            // r4
 *     u32 k_start_w,           // r5: (start << 16) | (width << 8) | k
 *     u32 raw_map_isbase,      // r6: (isbase << 16) | raw_map (BE u16)
 *     const u8 *tile_charbase, // r7
 *     u32 start_r,             // @(0, sp_in)
 *     const u16 *paltbl)       // @(4, sp_in) (backdrop_shadow if isbase)
 * ========================================================================== */
	.section .text._cgba_render_4bpp_part_strip_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_4bpp_part_strip_sh4
	.type   _cgba_render_4bpp_part_strip_sh4, @function
_cgba_render_4bpp_part_strip_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15

	extu.b  r5, r8                      /* r8  = k (1..8) */
	shlr8   r5
	extu.b  r5, r10                     /* r10 = width (1..7) */
	shlr8   r5                          /* r5  = start (0..7) */
	shll2   r5
	mov     r5, r12                     /* r12 = start * 4 */

	/* tile_id = swap.b(raw_map) & 0x3FF -> (tile_id << 5) */
	swap.b  r6, r1
	mov     #5, r2
	shld    r2, r1
	mov.w   .Lps_7fe0, r2
	and     r2, r1
	add     r7, r1                      /* r1 = tile_charbase + tile_id*32 */

	mov.l   @(24, r15), r0              /* start_r (0..7) */
	mov     r0, r2
	mov     r6, r0
	tst     #0x08, r0                   /* T = !vflip */
	bt      1f
	mov     #7, r3
	sub     r2, r3                      /* 7 - start_r */
	shll2   r3
	add     r3, r1
	bra     2f
	mov     #-4, r7                     /* row_step = -4 */
1:	shll2   r2
	add     r2, r1
	mov     #4, r7                      /* row_step = +4 */
2:
	mov.l   @(28, r15), r5              /* paltbl */
	mov.w   @r5, r13                    /* bg = paltbl[0] (for isbase) */
	mov     r6, r0
	and     #0xF0, r0
	add     r0, r0                      /* pal * 32 bytes */
	add     r0, r5                      /* r5 = subpal */

	mov.w   .Lps_480, r11               /* 480 */
	mov     r10, r0
	add     r0, r0                      /* width * 2 */
	sub     r0, r11                     /* r11 = 480 - width * 2 */

	mov     r6, r0
	shlr16  r0                          /* r0 = isbase (0 or 1) */
	tst     r0, r0
	bf      .Lps_is_base
	mov.w   .Lps_480, r13               /* overlay: r13 = 480 for zero-row skip */
	mov     r6, r0
	tst     #0x04, r0                   /* T = !hflip */
	bf      .Lps_over_hf_init

	/* ---- Overlay, !hflip ---- */
	neg     r12, r12                    /* r12 = -(start * 4) */
	.balign 4
.Lps_over_nhf_row:
	mov.l   @r1, r2
	tst     r2, r2
	bt.s    .Lps_over_nhf_z
	add     r7, r1
	swap.b  r2, r2
	swap.w  r2, r2
	swap.b  r2, r2
	shld    r12, r2                     /* eswap32(raw) >> (start*4) */
	mov     r10, r9
.Lps_over_nhf_px:
	mov     r2, r0
	tst     #0x0F, r0
	bt.s    1f
	shlr2   r2
	and     #0x0F, r0
	add     r0, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @r4
1:	shlr2   r2
	dt      r9
	bf.s    .Lps_over_nhf_px
	add     #2, r4
	dt      r8
	bf.s    .Lps_over_nhf_row
	add     r11, r4
	bra     .Lps_done
	nop
.Lps_over_nhf_z:
	dt      r8
	bf.s    .Lps_over_nhf_row
	add     r13, r4

.Lps_done:
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	.balign 2
.Lps_7fe0:
	.word   0x7FE0
.Lps_480:
	.word   480
	.balign 4

	/* ---- Overlay, hflip ---- */
.Lps_over_hf_init:
	mov     #-27, r3
.Lps_over_hf_row:
	mov.l   @r1, r2
	tst     r2, r2
	bt.s    .Lps_over_hf_z
	add     r7, r1
	swap.b  r2, r2
	swap.w  r2, r2
	swap.b  r2, r2
	shld    r12, r2                     /* eswap32(raw) << (start*4) */
	mov     r10, r9
.Lps_over_hf_px:
	mov     r2, r0
	shld    r3, r0
	tst     #0x1E, r0
	bt.s    1f
	shll2   r2
	and     #0x1E, r0
	mov.w   @(r0, r5), r0
	mov.w   r0, @r4
1:	shll2   r2
	dt      r9
	bf.s    .Lps_over_hf_px
	add     #2, r4
	dt      r8
	bf.s    .Lps_over_hf_row
	add     r11, r4
	bra     .Lps_done
	nop
.Lps_over_hf_z:
	dt      r8
	bf.s    .Lps_over_hf_row
	add     r13, r4
	bra     .Lps_done
	nop

	/* ---- Base (r13 = bg, subpal[0] == bg) ---- */
.Lps_is_base:
	mov     r6, r0
	tst     #0x04, r0                   /* T = !hflip */
	bf      .Lps_base_hf_init

	/* ---- Base, !hflip ---- */
	neg     r12, r12                    /* r12 = -(start * 4) */
	.balign 4
.Lps_base_nhf_row:
	mov.l   @r1, r2
	tst     r2, r2
	bt.s    .Lps_base_nhf_z
	add     r7, r1
	swap.b  r2, r2
	swap.w  r2, r2
	swap.b  r2, r2
	shld    r12, r2
	mov     r10, r9
.Lps_base_nhf_px:
	mov     r2, r0
	add     r0, r0
	and     #0x1E, r0
	mov.w   @(r0, r5), r0
	shlr2   r2
	shlr2   r2
	mov.w   r0, @r4
	dt      r9
	bf.s    .Lps_base_nhf_px
	add     #2, r4
	dt      r8
	bf.s    .Lps_base_nhf_row
	add     r11, r4
	bra     .Lps_done
	nop
.Lps_base_nhf_z:
	mov     r10, r9
1:	mov.w   r13, @r4
	dt      r9
	bf.s    1b
	add     #2, r4
	dt      r8
	bf.s    .Lps_base_nhf_row
	add     r11, r4
	bra     .Lps_done
	nop

	/* ---- Base, hflip ---- */
.Lps_base_hf_init:
	mov     #-27, r3
	.balign 4
.Lps_base_hf_row:
	mov.l   @r1, r2
	tst     r2, r2
	bt.s    .Lps_base_hf_z
	add     r7, r1
	swap.b  r2, r2
	swap.w  r2, r2
	swap.b  r2, r2
	shld    r12, r2
	mov     r10, r9
.Lps_base_hf_px:
	mov     r2, r0
	shld    r3, r0
	and     #0x1E, r0
	mov.w   @(r0, r5), r0
	shll2   r2
	shll2   r2
	mov.w   r0, @r4
	dt      r9
	bf.s    .Lps_base_hf_px
	add     #2, r4
	dt      r8
	bf.s    .Lps_base_hf_row
	add     r11, r4
	bra     .Lps_done
	nop
.Lps_base_hf_z:
	mov     r10, r9
1:	mov.w   r13, @r4
	dt      r9
	bf.s    1b
	add     #2, r4
	dt      r8
	bf.s    .Lps_base_hf_row
	add     r11, r4
	bra     .Lps_done
	nop
	.size   _cgba_render_4bpp_part_strip_sh4, .-_cgba_render_4bpp_part_strip_sh4

/* ============================================================================
 * void cgba_render_mode4_blit_base_sh4(
 *   const u8 *src,        // r4: 8-bpp Mode 4 pixels (1 byte/pixel)
 *   u32 count,            // r5: pixel count
 *   u16 *dst,             // r6: RGB565 scanline destination
 *   const u16 *pal_signed // r7: 256-entry signed RGB565 palette (+128)
 * );
 * Unconditional 8-bpp signed-palette lookup with paired 32-bit mov.l stores
 * (handles arbitrary 2-byte dst alignment; zero callee-saved spills).
 * ============================================================================ */
	.section .text._cgba_render_mode4_blit_base_sh4,"ax",@progbits
	.global _cgba_render_mode4_blit_base_sh4
	.type   _cgba_render_mode4_blit_base_sh4, @function
	.balign 4
_cgba_render_mode4_blit_base_sh4:
	tst     r5, r5
	bt      .Lm4b_done
	/* Align dst (r6) to 4 bytes if starting at odd u16 address. */
	mov     r6, r0
	tst     #2, r0
	bt      .Lm4b_aligned
	mov.b   @r4+, r0
	add     r0, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @r6
	add     #2, r6
	add     #-1, r5
.Lm4b_aligned:
	mov     r5, r3
	shlr2   r3
	tst     r3, r3
	bt      .Lm4b_tail
	.balign 4
.Lm4b_loop4:
	mov.b   @r4+, r0
	add     r0, r0
	mov.w   @(r0, r7), r2               /* r2[15:0] = p0 */
	mov.b   @r4+, r0
	add     r0, r0
	mov.w   @(r0, r7), r1               /* r1[15:0] = p1 */
	swap.w  r1, r1                      /* r1[31:16] = p1 */
	xtrct   r2, r1                      /* r1 = (p0 << 16) | p1 */
	mov.l   r1, @r6

	mov.b   @r4+, r0
	add     r0, r0
	mov.w   @(r0, r7), r2               /* r2[15:0] = p2 */
	mov.b   @r4+, r0
	add     r0, r0
	mov.w   @(r0, r7), r1               /* r1[15:0] = p3 */
	swap.w  r1, r1                      /* r1[31:16] = p3 */
	xtrct   r2, r1                      /* r1 = (p2 << 16) | p3 */
	mov.l   r1, @(4, r6)

	dt      r3
	bf.s    .Lm4b_loop4
	add     #8, r6
.Lm4b_tail:
	mov     r5, r0
	and     #3, r0
	tst     r0, r0
	bt      .Lm4b_done
	mov     r0, r3
.Lm4b_loop1:
	mov.b   @r4+, r0
	add     r0, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @r6
	dt      r3
	bf.s    .Lm4b_loop1
	add     #2, r6
.Lm4b_done:
	rts
	nop
	.size   _cgba_render_mode4_blit_base_sh4, .-_cgba_render_mode4_blit_base_sh4

/* ============================================================================
 * void cgba_render_mode4_blit_over_sh4(
 *   const u8 *src,        // r4: 8-bpp Mode 4 pixels (1 byte/pixel)
 *   u32 count,            // r5: pixel count
 *   u16 *dst,             // r6: RGB565 scanline destination
 *   const u16 *pal        // r7: 256-entry RGB565 palette_ram_converted
 * );
 * Transparent-zero 8-bpp palette lookup (preserves dst when src[i] == 0).
 * ============================================================================ */
	.section .text._cgba_render_mode4_blit_over_sh4,"ax",@progbits
	.global _cgba_render_mode4_blit_over_sh4
	.type   _cgba_render_mode4_blit_over_sh4, @function
	.balign 4
_cgba_render_mode4_blit_over_sh4:
	tst     r5, r5
	bt      .Lm4o_done
	.balign 4
.Lm4o_loop:
	mov.b   @r4+, r0
	tst     r0, r0
	bt.s    .Lm4o_next
	extu.b  r0, r0
	add     r0, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @r6
.Lm4o_next:
	dt      r5
	bf.s    .Lm4o_loop
	add     #2, r6
.Lm4o_done:
	rts
	nop
	.size   _cgba_render_mode4_blit_over_sh4, .-_cgba_render_mode4_blit_over_sh4

/* ============================================================================
 * void cgba_blend_mode4_st4bpp_sh4(
 *   u16 *dst,             // r4: RGB565 scanline destination
 *   const u16 *bg,        // r5: RGB565 BG2 background source
 *   u16 *st,              // r6: semi-transparent OBJ index buffer (cleared to 0)
 *   u32 count,            // r7: pixel count
 *   const u32 *ta,        // @(0,r15): 16-entry precomputed term_a table
 *   u32 evb_sat           // @(4,r15): evb (0..16) | (can_sat ? 0x80 : 0)
 * );
 * Fast paired-zero-skipping 4-bpp semi-transparent RGB565 alpha blender.
 * Clears every non-zero entry in st[0..count-1] back to 0 in place so callers
 * can reuse a persistent zeroed buffer without memset().
 * ============================================================================ */
	.section .text._cgba_blend_mode4_st4bpp_sh4,"ax",@progbits
	.global _cgba_blend_mode4_st4bpp_sh4
	.type   _cgba_blend_mode4_st4bpp_sh4, @function
	.balign 4
_cgba_blend_mode4_st4bpp_sh4:
	tst     r7, r7
	bf      0f
	rts
	nop
0:	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	sts.l   pr, @-r15

	mov.l   @(24, r15), r10             /* r10 = ta */
	mov.l   @(28, r15), r0              /* r0  = evb | (can_sat ? 0x80 : 0) */
	mov.l   .Lbs_blnd_msk, r8           /* r8  = 0x07E0F81F */
	mov     #0, r9
	tst     #0x80, r0
	bt      1f
	mov.l   .Lbs_ovf_all, r9            /* r9  = 0x08010020 */
1:	and     #0x7F, r0
	mov     r0, r11                     /* r11 = evb (0..16) */

	/* Align st (r6) to 4 bytes if starting at odd pixel */
	mov     r6, r0
	tst     #2, r0
	bt      .Lbs_aligned
	mov.w   @r6, r0
	extu.w  r0, r0
	tst     r0, r0
	bt      .Lbs_lead_next
	mov     #0, r1
	mov.w   r1, @r6
	mov.w   @r5, r2
	bsr     .Lbs_blend_one
	extu.w  r2, r2
	mov.w   r0, @r4
.Lbs_lead_next:
	add     #2, r6
	add     #2, r4
	add     #2, r5
	add     #-1, r7

.Lbs_aligned:
	mov     r7, r3
	shlr    r3
	tst     r3, r3
	bt      .Lbs_tail
/* Shared single-pixel blend core for _cgba_blend_mode4_st4bpp_sh4:
 *   in:  r0 = st_val (low4 = 1..15), r2 = zero-ext RGB565 bg pixel,
 *        r8 = BLND_MSK, r9 = OVF_MSK (0 or 0x08010020), r10 = ta, r11 = evb
 *   out: r0 = blended RGB565; clobbers r1, r2, MACL, T. */
.macro M4_ST4_BLEND_PIX inline_sat
	and     #0x0F, r0
	shll2   r0
	mov.l   @(r0, r10), r0
	mov     r2, r1
	shll16  r1
	or      r1, r2
	and     r8, r2
	mul.l   r11, r2
	sts     macl, r2
	add     r2, r0
	shlr2   r0
	shlr2   r0
	tst     r9, r0
	bt      9f
.if \inline_sat
	CGBA_SWAR_SAT_PFE r0, r9, r2, r1
.else
	bsr     .Lbs_sat
	nop
.endif
9:	and     r8, r0
	mov     r0, r2
	shlr16  r2
	or      r2, r0
.endm

	.balign 4
.Lbs_pair:
	mov.l   @r6, r12                    /* big-endian: hi16=st[0], lo16=st[1] */
	tst     r12, r12
	bt.s    .Lbs_pair_next
	mov     #0, r0
	mov.l   r0, @r6                     /* clear both halfwords back to 0 */
	swap.w  r12, r0                     /* low16 = st[0] */
	extu.w  r0, r0
	tst     r0, r0
	bt      .Lbs_p1
	mov.w   @r5, r2
	extu.w  r2, r2
	M4_ST4_BLEND_PIX 0
	mov.w   r0, @r4
.Lbs_p1:
	extu.w  r12, r0                     /* low16 = st[1] */
	tst     r0, r0
	bt      .Lbs_pair_next
	mov.w   @(2, r5), r0
	extu.w  r0, r2
	mov     r12, r0
	M4_ST4_BLEND_PIX 0
	mov.w   r0, @(2, r4)
.Lbs_pair_next:
	add     #4, r6
	add     #4, r4
	dt      r3
	bf.s    .Lbs_pair
	add     #4, r5

.Lbs_tail:
	mov     r7, r0
	tst     #1, r0
	bt      .Lbs_done
	mov.w   @r6, r0
	extu.w  r0, r0
	tst     r0, r0
	bt      .Lbs_done
	mov     #0, r1
	mov.w   r1, @r6
	mov.w   @r5, r2
	bsr     .Lbs_blend_one
	extu.w  r2, r2
	mov.w   r0, @r4

.Lbs_done:
	lds.l   @r15+, pr
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	.balign 4
/* Leaf helper for odd lead/tail pixel: r0=st_val, r2=bg_pix -> r0=blended. */
.Lbs_blend_one:
	M4_ST4_BLEND_PIX 1
	rts
	nop
	.purgem M4_ST4_BLEND_PIX

	/* Branchless SWAR RGB565 per-channel saturation (ovf bits at 27, 16, 5). */
	.balign 4
.Lbs_sat:
	CGBA_SWAR_SAT_PFE r0, r9, r2, r1
	rts
	nop

	.balign 4
.Lbs_blnd_msk:
	.long   0x07E0F81F
.Lbs_ovf_all:
	.long   0x08010020
	.size   _cgba_blend_mode4_st4bpp_sh4, .-_cgba_blend_mode4_st4bpp_sh4

/* ============================================================================
 * 8-bpp rotated/scaled affine BG direct alpha-blend overlay kernel:
 *   void cgba_render_affine_rot_blend_sh4(const cgba_aff_rot_blend_params *p)
 *
 * Blends non-zero texels directly onto existing 16-bit RGB565 pixels in dst,
 * walking backward from dst_end.  Uses per-CBB tile_kind[256] (0=empty,
 * 1=solid, 2/3=textured) to skip empty tiles in 12 insns/px with zero memory
 * traffic and hoist solid-tile term_a loads out of the pixel loop.
 *
 * Struct layout (cgba_aff_rot_blend_params):
 *   +0  dst_end    (u16 *)
 *   +4  map_base   (const u8 *)
 *   +8  tile_base  (const u8 *)
 *   +12 term_a     (const u32 *: term_a_s4 if flags&1, else p1e*eva)
 *   +16 ux         (u32)
 *   +20 uy         (u32)
 *   +24 step_x     (u32)
 *   +28 step_y     (u32)
 *   +32 cnt        (u32)
 *   +36 pix_shift  (s32: -(25 - map_size))
 *   +40 row_shift  (s32: map_size + 4)
 *   +44 evb        (u32: 0..16)
 *   +48 tile_kind  (const u8 *)
 *   +52 flags      (u32: bit0=(evb==16), bit1=can_saturate)
 * ============================================================================ */
	.section .text._cgba_render_affine_rot_blend_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_affine_rot_blend_sh4
	.type   _cgba_render_affine_rot_blend_sh4, @function
_cgba_render_affine_rot_blend_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15
	sts.l   pr, @-r15
	add     #-24, r15

	/* Stack layout:
	 *   @(0,  r15) = tile_kind
	 *   @(4,  r15) = tile_shift = pix_shift - 3 = -(28 - map_size)
	 *   @(8,  r15) = BLND_MSK   = 0x07E0F81F
	 *   @(12, r15) = sat_mask   = 0x08010020 (if evb==16 or saturate) else 0
	 *   @(16, r15) = row_shift  = map_size + 4
	 *   @(20, r15) = evb */
	mov     r4, r3                      /* r3 = params ptr */
	mov.l   @(48, r3), r1               /* tile_kind */
	mov.l   r1, @(0, r15)
	mov.l   @(36, r3), r12              /* r12 = pix_shift */
	mov     r12, r1
	add     #-3, r1                     /* tile_shift */
	mov.l   r1, @(4, r15)
	mov.l   .Lafbl_blnd_msk, r1
	mov.l   r1, @(8, r15)
	mov.l   @(40, r3), r1               /* row_shift */
	mov.l   r1, @(16, r15)
	lds     r1, macl
	mov.l   @(44, r3), r1               /* evb */
	mov.l   r1, @(20, r15)
	mov.l   @(52, r3), r0               /* r0 = flags */
	tst     #3, r0                      /* sat_mask nonzero if bit0 (evb16) or bit1 (sat) */
	bt.s    1f
	mov     #0, r1
	mov.l   .Lafbl_ovf_msk, r1
1:	mov.l   r1, @(12, r15)

	mov     r12, r13
	add     #3, r13                     /* r13 = sub_y_shift = -(22 - map_size) */
	mov     #3, r1
	sub     r12, r1                     /* 28 - map_size */
	mov     #-1, r2
	shld    r1, r2                      /* r2 = tile_mask */
	lds     r2, mach

	mov.l   @(0, r3), r4                /* dst_end */
	mov.l   @(4, r3), r5                /* map_base */
	mov.l   @(8, r3), r6                /* tile_base */
	mov.l   @(12, r3), r7               /* term_a / term_a_s4 */
	mov.l   @(16, r3), r8               /* ux */
	mov.l   @(20, r3), r9               /* uy */
	mov.l   @(24, r3), r10              /* step_x */
	mov.l   @(28, r3), r11              /* step_y */
	mov.l   @(32, r3), r14              /* cnt */

	tst     #1, r0                      /* flags bit0: evb == 16? */
	bf      .Lafbl16_miss
	bra     .Lafblg_miss
	nop

	.balign 4
.Lafbl_blnd_msk:
	.long   0x07E0F81F
.Lafbl_ovf_msk:
	.long   0x08010020

	.balign 4
.Lafbl_done:
	add     #24, r15
	lds.l   @r15+, pr
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	/* Rare per-channel saturation helper:
	 *   in:  r0 = pfe (with overflow bit set), @(8,r15)=BLND_MSK, @(12,r15)=OVF_MSK
	 *   out: r0 = clamped expanded pfe, r2 = swap.w(r0), r1 = mach (tile_mask)
	 *   preserves: r3..r14, mach, macl */
	.balign 4
.Lafbl_sat:
	mov.l   @(12, r15), r2              /* OVF_MSK = 0x08010020 */
	CGBA_SWAR_SAT_PFE r0, r2, r2, r1
	mov.l   @(8, r15), r1               /* BLND_MSK */
	and     r1, r0
	sts     mach, r1                    /* restore r1 = tile_mask */
	rts
	swap.w  r0, r2

/* Parameterized tile-classified affine blend loops:
 *   evb16 == 1: zero multiplies (pfe = term_a_s4[pix] + p2e)
 *   evb16 == 0: general 0 <= evb < 16 (pfe = (term_a[pix] + p2e * evb) >> 4)
 * Supports tile_kind 0 (empty), 1 (solid), 2 (mixed), and 3 (opaque textured). */
.macro AFBL_BLEND_PIXEL evb16, ta_reg, tmp_reg
	mov.w   @r4, r0
	extu.w  r0, r0
	swap.w  r0, \tmp_reg
	or      \tmp_reg, r0
	mov.l   @(8, r15), \tmp_reg         /* BLND_MSK */
	and     \tmp_reg, r0
.if \evb16
	add     \ta_reg, r0
	mov.l   @(12, r15), \tmp_reg        /* OVF_MSK */
	tst     \tmp_reg, r0
	bt.s    1f
	swap.w  r0, r2
	bsr     .Lafbl_sat
	nop
1:	or      r2, r0
.else
	mov.l   @(20, r15), \tmp_reg        /* evb */
	mul.l   \tmp_reg, r0
	sts     macl, r0
	add     \ta_reg, r0
	shlr2   r0
	shlr2   r0
	mov.l   @(12, r15), \tmp_reg        /* sat_mask */
	tst     \tmp_reg, r0
	bt      1f
	bsr     .Lafbl_sat
	nop
	bra     2f
	nop
1:	mov.l   @(8, r15), \tmp_reg         /* BLND_MSK */
	and     \tmp_reg, r0
	swap.w  r0, r2
2:	or      r2, r0
.endif
.endm

.macro AFBL_MODE_LOOPS pfx, evb16
	.balign 4
\pfx\()_empty_step:
	sts     mach, r1                    /* r1 = tile_mask */
	add     #-2, r4
	dt      r14
	bt      \pfx\()_done_br
\pfx\()_empty_next:
	mov     r8, r2
	add     r10, r8
	xor     r8, r2
	mov     r9, r0
	add     r11, r9
	xor     r9, r0
	or      r0, r2
	tst     r1, r2                      /* same empty 8x8 tile? */
	bf      \pfx\()_miss
	dt      r14
	bf.s    \pfx\()_empty_next
	add     #-2, r4
\pfx\()_done_br:
	bra     .Lafbl_done
	nop

	.balign 4
\pfx\()_solid_enter:
	mov.b   @r3, r0
	extu.b  r0, r0
	shll2   r0
	bra     \pfx\()_solid_blend
	mov.l   @(r0, r7), r3               /* r3 = solid term_a */
	.balign 4
\pfx\()_solid_next:
	mov     r8, r2
	add     r10, r8
	xor     r8, r2
	mov     r9, r0
	add     r11, r9
	xor     r9, r0
	or      r0, r2
	tst     r1, r2                      /* same solid 8x8 tile? */
	bf      \pfx\()_miss
\pfx\()_solid_blend:
	add     #-2, r4
	AFBL_BLEND_PIXEL \evb16, r3, r2
	dt      r14
	bf.s    \pfx\()_solid_next
	mov.w   r0, @r4
	bra     .Lafbl_done
	nop

	.balign 4
\pfx\()_miss:
	mov.l   @(4, r15), r2               /* tile_shift */
	mov     r8, r0
	shld    r2, r0                      /* tx */
	mov     r0, r1
	mov     r9, r0
	shld    r2, r0                      /* ty */
.if \evb16
	sts     macl, r2                    /* row_shift */
.else
	mov.l   @(16, r15), r2              /* row_shift (mul.l clobbers macl) */
.endif
	shld    r2, r0                      /* ty << (map_size + 4) */
	or      r1, r0                      /* map_idx */
	mov.b   @(r0, r5), r0               /* tile_id */
	extu.b  r0, r0
	mov     r0, r1
	mov.l   @(0, r15), r3               /* tile_kind */
	mov.b   @(r0, r3), r0               /* 0=empty, 1=solid, 2=mixed, 3=opaque */
	tst     r0, r0
	bt.s    \pfx\()_empty_step
	shll8   r1
	shlr2   r1                          /* tile_id << 6 */
	mov     r6, r3
	add     r1, r3                      /* r3 = cur_tile_ptr */
	sts     mach, r1                    /* r1 = tile_mask */
	cmp/eq  #1, r0
	bt      \pfx\()_solid_enter
	cmp/eq  #3, r0
	bt      \pfx\()_opaq_draw
	bra     \pfx\()_mix_draw
	nop

	.balign 4
\pfx\()_mix_next:
	mov     r8, r2
	add     r10, r8
	xor     r8, r2
	mov     r9, r0
	add     r11, r9
	xor     r9, r0
	or      r0, r2
	tst     r1, r2                      /* same 8x8 tile? */
	bf      \pfx\()_miss
\pfx\()_mix_draw:
	mov     r8, r0
	shld    r12, r0
	and     #7, r0
	mov     r0, r2
	mov     r9, r0
	shld    r13, r0
	and     #0x38, r0
	or      r2, r0
	mov.b   @(r0, r3), r0
	tst     r0, r0
	bt.s    3f
	add     #-2, r4
	extu.b  r0, r0
	shll2   r0
	mov.l   @(r0, r7), r2               /* term_a[pix] */
	AFBL_BLEND_PIXEL \evb16, r2, r1
	mov.w   r0, @r4
	sts     mach, r1
3:	dt      r14
	bf      \pfx\()_mix_next
	bra     .Lafbl_done
	nop

	.balign 4
\pfx\()_opaq_next:
	mov     r8, r2
	add     r10, r8
	xor     r8, r2
	mov     r9, r0
	add     r11, r9
	xor     r9, r0
	or      r0, r2
	tst     r1, r2                      /* same opaque 8x8 tile? */
	bf      \pfx\()_miss
\pfx\()_opaq_draw:
	mov     r8, r0
	shld    r12, r0
	and     #7, r0
	mov     r0, r2
	mov     r9, r0
	shld    r13, r0
	and     #0x38, r0
	or      r2, r0
	mov.b   @(r0, r3), r0
	add     #-2, r4
	extu.b  r0, r0
	shll2   r0
	mov.l   @(r0, r7), r2               /* term_a[pix] */
	AFBL_BLEND_PIXEL \evb16, r2, r1
	mov.w   r0, @r4
	sts     mach, r1
	dt      r14
	bf      \pfx\()_opaq_next
	bra     .Lafbl_done
	nop
.endm

	/* ---- Fast path: evb == 16 ---- */
	AFBL_MODE_LOOPS .Lafbl16, 1
	/* ---- General path: 0 <= evb < 16 ---- */
	AFBL_MODE_LOOPS .Lafblg, 0
	.purgem AFBL_BLEND_PIXEL
	.purgem AFBL_MODE_LOOPS
	.size   _cgba_render_affine_rot_blend_sh4, .-_cgba_render_affine_rot_blend_sh4

/* ============================================================================
 * 8-bpp rotated/scaled affine BG direct alpha-blend kernel for evb == 16
 * (BOTH wrap == false and wrap == true) and |dx| < 2048 && |dy| < 2048:
 *   void cgba_render_affine_rot_blend_evb16_329_sh4(
 *     const cgba_aff_t329_blend_params *p)
 *
 * Uses 3.29 unsigned intra-tile stepping (`addc`) so carries fire ONLY on
 * 8-pixel tile boundaries, handling all 4 step quadrants via qflip (0,7,56,63),
 * all 4 map sizes (16..128 tiles), and both wrapped & pre-clipped non-wrap spans.
 *   - no_sat == 1 path (when palette bounds prove no RGB565 channel can
 *     overflow): blends in 3 instructions (mov.w / add / mov.w) with r5 = qflip,
 *     7-insn empty tiles, 10-insn solid tiles, 20-insn opaque textured tiles,
 *     and 22-insn mixed tiles.
 *   - no_sat == 0 path: 16-bit SWAR carry-detect addition (r5 = 0x00010820)
 *     with cold .Lb3_sat16 clamping on overflow.
 *
 * Struct layout (cgba_aff_t329_blend_params, 64 bytes):
 *   +0  dst_end        (u16 *)
 *   +4  map_base       (const u8 *: base of affine tilemap)
 *   +8  tile_base      (const u8 *)
 *   +12 signed_term_a  (const u16 *, pointing to +128; RGB565 term_a_s4)
 *   +16 lx_dir         (u32: 3.29 unsigned intra-tile x)
 *   +20 ly_dir         (u32: 3.29 unsigned intra-tile y)
 *   +24 step_lx        (u32: |dx| << 21)
 *   +28 step_ly        (u32: |dy| << 21)
 *   +32 cnt            (u32: > 0)
 *   +36 step_map_x     (s32: +1 or -1)
 *   +40 step_map_y     (s32: +(16<<map_size) or -(16<<map_size))
 *   +44 qflip          (u32: 0, 7, 56, or 63)
 *   +48 tile_kind      (const u8 *: 0=empty, 1=solid, 2=mixed, 3=opaque)
 *   +52 no_sat         (u32: 1 if proven no saturation, 0 otherwise)
 *   +56 map_off        (u32: (ty << (map_size+4)) | tx, within [0, map_mask])
 *   +60 t_mask         (u32: (16 << map_size) - 1, i.e. 0x0F/0x1F/0x3F/0x7F)
 * ============================================================================ */
	.section .text._cgba_render_affine_rot_blend_evb16_329_sh4,"ax",@progbits
	.balign 4
	.global _cgba_render_affine_rot_blend_evb16_329_sh4
	.type   _cgba_render_affine_rot_blend_evb16_329_sh4, @function
_cgba_render_affine_rot_blend_evb16_329_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15
	sts.l   pr, @-r15

	mov     r4, r3
	mov.l   @(60, r3), r1               /* t_mask = dim - 1 */
	mov     r1, r2
	add     #1, r2                      /* dim = 16, 32, 64, or 128 */
	mulu.w  r2, r2
	sts     macl, r2
	add     #-1, r2                     /* map_mask = dim*dim - 1 */
	mov.l   r2, @-r15                   /* @(20, r15) = map_mask */
	mov.l   r1, @-r15                   /* @(16, r15) = t_mask */
	mov.l   @(4, r3), r1                /* map_base */
	mov.l   r1, @-r15                   /* @(12, r15) = map_base */
	mov.l   @(44, r3), r5               /* r5 = qflip */
	mov.l   r5, @-r15                   /* @(8, r15) = qflip */
	mov.l   @(8, r3), r1                /* tile_base */
	mov.l   r1, @-r15                   /* @(4, r15) = tile_base */
	mov.l   @(48, r3), r1               /* tile_kind */
	mov.l   r1, @-r15                   /* @(0, r15) = tile_kind */

	mov.l   @(36, r3), r1               /* step_map_x (+1 or -1) */
	lds     r1, mach                    /* mach = step_map_x */
	mov.l   @(40, r3), r1               /* step_map_y (+dim or -dim) */
	lds     r1, macl                    /* macl = step_map_y */

	mov.l   @(52, r3), r0               /* no_sat */
	mov.l   @(0, r3), r4                /* r4  = dst_end */
	mov.l   @(56, r3), r6               /* r6  = map_off */
	mov.l   @(12, r3), r7               /* r7  = signed_term_a (+128) */
	mov.l   @(16, r3), r8               /* r8  = lx_dir (3.29) */
	mov.l   @(20, r3), r9               /* r9  = ly_dir (3.29) */
	mov.l   @(24, r3), r10              /* r10 = step_lx */
	mov.l   @(28, r3), r11              /* r11 = step_ly */
	mov.l   @(32, r3), r14              /* r14 = cnt */
	mov     #-29, r12
	mov     #-26, r13

	tst     r0, r0
	bt      .Lb3_to_sat
	bra     .Lb3_ns_miss
	nop
.Lb3_to_sat:
	mov.l   .Lb3_carry_msk, r5          /* r5 = 0x00010820 */
	bra     .Lb3_s_miss
	nop

	.balign 4
.Lb3_carry_msk:
	.long   0x00010820

	.balign 4
.Lb3_done:
	add     #24, r15
	lds.l   @r15+, pr
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	/* Cold per-channel saturation helper for 16-bit SWAR add:
	 *   in:  r0 = a + b, r2 = a (0..0xFFFF), r4 = &dst_pix
	 *   out: writes exact saturated RGB565 to @r4; preserves r2..r14, mach, macl */
	.balign 4
.Lb3_sat16:
	mov.l   r3, @-r15
	mov.l   r5, @-r15
	sub     r2, r0                      /* r0 = b (original dst_pix) */
	mov.l   .Lb3_blnd_msk, r5           /* 0x07E0F81F */
	swap.w  r0, r1
	or      r1, r0
	and     r5, r0                      /* p2e */
	mov     r2, r3
	swap.w  r3, r1
	or      r1, r3
	and     r5, r3                      /* p1e */
	add     r3, r0                      /* pfe = p1e + p2e */
	mov.l   .Lb3_ovf_msk, r3            /* 0x08010020 */
	CGBA_SWAR_SAT_PFE r0, r3, r3, r1
	and     r5, r0
	swap.w  r0, r1
	or      r1, r0
	mov.w   r0, @r4
	mov.l   @r15+, r5
	rts
	mov.l   @r15+, r3

	.balign 4
.Lb3_blnd_msk:
	.long   0x07E0F81F
.Lb3_ovf_msk:
	.long   0x08010020

	/* Shared tile-miss dispatch + empty-tile fast loop for both no_sat paths.
	 * Wraps tx modulo (t_mask+1) without borrowing/carrying into ty, and wraps
	 * ty modulo (t_mask+1) via map_mask — exact for both wrap and non-wrap! */
.macro AFBL329_MISS pfx, is_sat
	.balign 4
.Lb3_\pfx\()_miss_x:
	clrt
	addc    r11, r9
	bf      1f
	sts     macl, r0
	add     r0, r6
	mov.l   @(20, r15), r0              /* map_mask */
	and     r0, r6
1:	sts     mach, r0
	mov     r6, r1
	add     r0, r1
	xor     r6, r1
	mov.l   @(16, r15), r0              /* t_mask */
	and     r0, r1
	bra     .Lb3_\pfx\()_miss
	xor     r1, r6

	.balign 4
.Lb3_\pfx\()_miss_y:
	sts     macl, r0
	add     r0, r6
	mov.l   @(20, r15), r0              /* map_mask */
	and     r0, r6

	.balign 4
.Lb3_\pfx\()_miss:
	mov.l   @(12, r15), r2              /* map_base */
	mov     r6, r0
	mov.b   @(r0, r2), r0               /* tile_id */
	extu.b  r0, r0
	mov     r0, r1
	mov.l   @(0, r15), r2               /* tile_kind */
	mov.b   @(r0, r2), r0               /* 0=empty, 1=solid, 2=mixed, 3=opaque */
	tst     r0, r0
	bt.s    .Lb3_\pfx\()_empty_first
	shll8   r1
	shlr2   r1                          /* tile_id << 6 */
	mov.l   @(4, r15), r3               /* tile_base */
	add     r1, r3                      /* r3 = cur_tile_ptr */
	cmp/eq  #2, r0
	bt      .Lb3_\pfx\()_mix_draw
	cmp/eq  #3, r0
	bt      .Lb3_\pfx\()_opaq_draw
	/* tile_kind == 1: solid non-zero tile */
	mov.b   @r3, r0
	add     r0, r0
.if \is_sat
	mov.w   @(r0, r7), r2
	bra     .Lb3_\pfx\()_solid_draw
	extu.w  r2, r2
.else
	bra     .Lb3_\pfx\()_solid_draw
	mov.w   @(r0, r7), r2
.endif
.endm

.macro AFBL329_EMPTY pfx
	/* Empty tile (tile_kind == 0): 7 insns per pixel, 0 memory reads */
	.balign 4
.Lb3_\pfx\()_empty_first:
	add     #-2, r4
	dt      r14
	bt      .Lb3_\pfx\()_done_br
.Lb3_\pfx\()_empty_next:
	addc    r10, r8
	bt      .Lb3_\pfx\()_miss_x
	addc    r11, r9
	bt      .Lb3_\pfx\()_miss_y
	dt      r14
	bf.s    .Lb3_\pfx\()_empty_next
	add     #-2, r4
.Lb3_\pfx\()_done_br:
	bra     .Lb3_done
	nop
.endm

	/* ==================== no_sat == 1 fast path (r5 = qflip) ==================== */
	AFBL329_MISS ns, 0

	/* Mixed tile (tile_kind == 2): 22 insns blended, 17 insns transparent */
	.balign 4
.Lb3_ns_mix_next:
	addc    r10, r8
	bt      .Lb3_ns_miss_x
	addc    r11, r9
	bt      .Lb3_ns_miss_y
.Lb3_ns_mix_draw:
	mov     r9, r0
	shld    r13, r0
	mov     r8, r1
	and     #0x38, r0
	shld    r12, r1
	or      r1, r0
	xor     r5, r0
	mov.b   @(r0, r3), r0
	tst     r0, r0
	bt.s    .Lb3_ns_mix_trans
	add     #-2, r4
	add     r0, r0
	mov.w   @(r0, r7), r2
	mov.w   @r4, r0
	add     r2, r0
	dt      r14
	bf.s    .Lb3_ns_mix_next
	mov.w   r0, @r4
	bra     .Lb3_done
	nop
.Lb3_ns_mix_trans:
	dt      r14
	bf      .Lb3_ns_mix_next
	bra     .Lb3_done
	nop

	/* Opaque textured tile (tile_kind == 3): 20 insns per pixel */
	.balign 4
.Lb3_ns_opaq_next:
	addc    r10, r8
	bt      .Lb3_ns_miss_x
	addc    r11, r9
	bt      .Lb3_ns_miss_y
.Lb3_ns_opaq_draw:
	mov     r9, r0
	shld    r13, r0
	mov     r8, r1
	and     #0x38, r0
	shld    r12, r1
	or      r1, r0
	xor     r5, r0
	mov.b   @(r0, r3), r0
	add     #-2, r4
	add     r0, r0
	mov.w   @(r0, r7), r2
	mov.w   @r4, r0
	add     r2, r0
	dt      r14
	bf.s    .Lb3_ns_opaq_next
	mov.w   r0, @r4
	bra     .Lb3_done
	nop

	AFBL329_EMPTY ns

	/* Solid non-zero tile (tile_kind == 1): 10 insns per pixel */
	.balign 4
.Lb3_ns_solid_next:
	addc    r10, r8
	bt      .Lb3_ns_miss_x
	addc    r11, r9
	bt      .Lb3_ns_miss_y
.Lb3_ns_solid_draw:
	add     #-2, r4
	mov.w   @r4, r0
	add     r2, r0
	dt      r14
	bf.s    .Lb3_ns_solid_next
	mov.w   r0, @r4
	bra     .Lb3_done
	nop

	/* ==================== no_sat == 0 path (r5 = 0x00010820) ==================== */
	AFBL329_MISS s, 1

	.balign 4
.Lb3_s_mix_next:
	addc    r10, r8
	bt      .Lb3_s_miss_x
	addc    r11, r9
	bt      .Lb3_s_miss_y
.Lb3_s_mix_draw:
	mov     r9, r0
	shld    r13, r0
	mov     r8, r1
	and     #0x38, r0
	shld    r12, r1
	or      r1, r0
	mov.l   @(8, r15), r1               /* qflip */
	xor     r1, r0
	mov.b   @(r0, r3), r0
	tst     r0, r0
	bt.s    .Lb3_s_mix_trans
	add     #-2, r4
	add     r0, r0
	mov.w   @(r0, r7), r2
	extu.w  r2, r2
	mov.w   @r4, r0
	extu.w  r0, r0
	mov     r0, r1
	add     r2, r0
	xor     r2, r1
	xor     r0, r1
	tst     r5, r1
	bt.s    .Lb3_s_mix_trans
	mov.w   r0, @r4
	bsr     .Lb3_sat16
	nop
.Lb3_s_mix_trans:
	dt      r14
	bf      .Lb3_s_mix_next
	bra     .Lb3_done
	nop

	.balign 4
.Lb3_s_opaq_next:
	addc    r10, r8
	bt      .Lb3_s_miss_x
	addc    r11, r9
	bt      .Lb3_s_miss_y
.Lb3_s_opaq_draw:
	mov     r9, r0
	shld    r13, r0
	mov     r8, r1
	and     #0x38, r0
	shld    r12, r1
	or      r1, r0
	mov.l   @(8, r15), r1               /* qflip */
	xor     r1, r0
	mov.b   @(r0, r3), r0
	add     #-2, r4
	add     r0, r0
	mov.w   @(r0, r7), r2
	extu.w  r2, r2
	mov.w   @r4, r0
	extu.w  r0, r0
	mov     r0, r1
	add     r2, r0
	xor     r2, r1
	xor     r0, r1
	tst     r5, r1
	bt.s    1f
	mov.w   r0, @r4
	bsr     .Lb3_sat16
	nop
1:	dt      r14
	bf      .Lb3_s_opaq_next
	bra     .Lb3_done
	nop

	AFBL329_EMPTY s

	.balign 4
.Lb3_s_solid_next:
	addc    r10, r8
	bt      .Lb3_s_miss_x
	addc    r11, r9
	bt      .Lb3_s_miss_y
.Lb3_s_solid_draw:
	add     #-2, r4
	mov.w   @r4, r0
	extu.w  r0, r0
	mov     r0, r1
	add     r2, r0
	xor     r2, r1
	xor     r0, r1
	tst     r5, r1
	bt.s    1f
	mov.w   r0, @r4
	bsr     .Lb3_sat16
	nop
1:	dt      r14
	bf      .Lb3_s_solid_next
	bra     .Lb3_done
	nop

	.purgem AFBL329_MISS
	.purgem AFBL329_EMPTY
	.size   _cgba_render_affine_rot_blend_evb16_329_sh4, .-_cgba_render_affine_rot_blend_evb16_329_sh4
