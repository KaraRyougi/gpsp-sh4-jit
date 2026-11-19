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

	.text
	.balign 4

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
 * void cgba_render_4bpp_span_base_pair_sh4(
 *   const u16 *map_ptr,   // r4
 *   u32 todraw,           // r5
 *   u16 *dest_ptr,        // r6
 *   const u8 *tile_base,  // r7
 *   s32 vflip_off,        // @(0, sp_in)
 *   const u32 *pair_lut)  // @(4, sp_in)
 * ========================================================================== */
	.balign 4
	.global _cgba_render_4bpp_span_base_pair_sh4
	.type   _cgba_render_4bpp_span_base_pair_sh4, @function
_cgba_render_4bpp_span_base_pair_sh4:
	tst     r5, r5
	bt      .Lbp_ret_early
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15

	mov     r5, r8                      /* r8  = todraw */
	mov.l   @(20, r15), r9              /* r9  = vflip_off */
	mov.l   @(24, r15), r10             /* r10 = pair_lut */
	mov.w   .Lbp_7fe0, r11              /* r11 = 0x00007FE0 */
	mov.l   @r10, r12                   /* r12 = [bgcolor | bgcolor] */

	mov     r6, r0
	tst     #2, r0                      /* T = 4-byte aligned dest_ptr */
	bf      .Lbp_to_unaligned

	/* ---- Aligned (4k) base loop ---- */
	.balign 4
.Lbp_a_loop:
	mov.w   @r4+, r0
	swap.b  r0, r1
	shll2   r1
	add     r1, r1
	shll2   r1
	and     r11, r1
	tst     #0x08, r0                   /* T = !vflip */
	bt.s    1f
	add     r7, r1
	add     r9, r1
1:	mov.l   @r1, r2                     /* r2 = [b0 b1 b2 b3] */
	tst     r2, r2
	bt.s    .Lbp_a_empty
	mov     r0, r3                      /* r3 = raw_map */

	and     #0xF0, r0                   /* pal * 16 */
	shlr2   r0                          /* pal * 4 */
	shll8   r0                          /* pal * 1024 */
	mov     r10, r5
	add     r0, r5                      /* r5 = &pair_lut[pal][0] */

	mov     r3, r0
	tst     #0x04, r0                   /* T = !hflip */
	bf      .Lbp_a_hf

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
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lbp_ret_early:
	rts
	nop

.Lbp_to_unaligned:
	bra     .Lbp_u_loop
	nop

	.balign 4
.Lbp_7fe0:
	.word   0x7FE0
	.balign 4

.Lbp_a_empty:
	mov.l   r12, @r6
	mov.l   r12, @(4, r6)
	mov.l   r12, @(8, r6)
	dt      r8
	mov.l   r12, @(12, r6)
	bf.s    .Lbp_a_loop
	add     #16, r6
	bra     .Lbp_done
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
	bf.s    .Lbp_a_loop
	add     #16, r6
	bra     .Lbp_done
	nop

	/* ---- Unaligned (4k+2) base loop using XTRCT + MOV.L ---- */
	.balign 4
.Lbp_u_loop:
	mov.w   @r4+, r0
	swap.b  r0, r1
	shll2   r1
	add     r1, r1
	shll2   r1
	and     r11, r1
	tst     #0x08, r0
	bt.s    1f
	add     r7, r1
	add     r9, r1
1:	mov.l   @r1, r2
	tst     r2, r2
	bt.s    .Lbp_u_empty
	mov     r0, r3

	and     #0xF0, r0
	shlr2   r0
	shll8   r0
	mov     r10, r5
	add     r0, r5

	mov     r3, r0
	tst     #0x04, r0
	bf      .Lbp_u_hf

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

.Lbp_u_empty:
	mov     r12, r0
	mov.w   r0, @(14, r6)
	add     #2, r6
	mov.l   r12, @r6
	mov.l   r12, @(4, r6)
	mov.l   r12, @(8, r6)
	dt      r8
	mov.w   r0, @-r6
	bf.s    .Lbp_u_loop
	add     #16, r6
	bra     .Lbp_done
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
	bt      .Lbp_u_done_bra
	bra     .Lbp_u_loop
	add     #14, r6                     /* orig+2+14 = orig+16 */
.Lbp_u_done_bra:
	bra     .Lbp_done
	nop
	.size   _cgba_render_4bpp_span_base_pair_sh4, .-_cgba_render_4bpp_span_base_pair_sh4


/* ============================================================================
 * void cgba_render_4bpp_span_over_pair_sh4(
 *   const u16 *map_ptr,   // r4
 *   u32 todraw,           // r5
 *   u16 *dest_ptr,        // r6
 *   const u8 *tile_base,  // r7
 *   s32 vflip_off,        // @(0, sp_in)
 *   const u32 *pair_lut,  // @(4, sp_in)
 *   const u16 *paltbl)    // @(8, sp_in)
 *
 * Includes empty-run skip (repeated identical raw map entries after an empty
 * tile row skip in 6 insns with zero VRAM tile-data loads), span-hoisted
 * 4-byte alignment dispatch (4 MOV.L aligned, XTRCT + 3 MOV.L + 2 MOV.W
 * unaligned), and shared partial-transparency fallback.
 * ========================================================================== */
	.balign 4
	.global _cgba_render_4bpp_span_over_pair_sh4
	.type   _cgba_render_4bpp_span_over_pair_sh4, @function
_cgba_render_4bpp_span_over_pair_sh4:
	tst     r5, r5
	bt      .Lop_ret_early
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov.l   r11, @-r15
	mov.l   r12, @-r15
	mov.l   r13, @-r15
	mov.l   r14, @-r15

	mov     r5, r8                      /* r8  = todraw */
	mov.l   @(28, r15), r9              /* r9  = vflip_off */
	mov.l   @(32, r15), r10             /* r10 = pair_lut */
	mov.l   @(36, r15), r12             /* r12 = paltbl */
	mov.w   .Lop_7fe0, r11              /* r11 = 0x00007FE0 */
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
	shll2   r1
	add     r1, r1
	shll2   r1
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

	and     #0xF0, r0
	shlr2   r0
	shll8   r0
	mov     r10, r5
	add     r0, r5                      /* r5 = &pair_lut[pal][0] */

	mov     r3, r0
	tst     #0x04, r0
	bf      .Lop_a_hf

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

	/* Empty-run skip (aligned): step over repeated identical raw map entries */
.Lop_a_empty_run:
	dt      r8
	bt      .Lop_done
	mov.w   @r4+, r0
	cmp/eq  r3, r0
	bt.s    .Lop_a_empty_run
	add     #16, r6
	bra     .Lop_a_have_r0
	nop

.Lop_done:
	mov.l   @r15+, r14
	mov.l   @r15+, r13
	mov.l   @r15+, r12
	mov.l   @r15+, r11
	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

.Lop_ret_early:
	rts
	nop

.Lop_to_unaligned:
	bra     .Lop_u_loop
	nop

.Lop_to_partial:
	bra     .Lop_partial
	nop

	.balign 4
.Lop_eeeef:
	.long   0xEEEEEEEF
.Lop_88888:
	.long   0x88888888
.Lop_7fe0:
	.word   0x7FE0
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
	bt      .Lop_a_done_bra
	bra     .Lop_a_loop
	add     #16, r6
.Lop_a_done_bra:
	bra     .Lop_done
	nop

	/* ---- Unaligned (4k+2) overlay loop ---- */
	.balign 4
.Lop_u_loop:
	mov.w   @r4+, r0
.Lop_u_have_r0:
	swap.b  r0, r1
	shll2   r1
	add     r1, r1
	shll2   r1
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

	and     #0xF0, r0
	shlr2   r0
	shll8   r0
	mov     r10, r5
	add     r0, r5

	mov     r3, r0
	tst     #0x04, r0
	bf      .Lop_u_hf

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
	bt      .Lop_u_done_bra
	mov.w   @r4+, r0
	cmp/eq  r3, r0
	bt.s    .Lop_u_empty_run
	add     #16, r6
	bra     .Lop_u_have_r0
	nop

.Lop_partial_u_tramp:
	bra     .Lop_partial
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
	bt      .Lop_u_done_bra
	bra     .Lop_u_loop
	add     #14, r6
.Lop_u_done_bra:
	bra     .Lop_done
	nop

	/* Shared partially transparent overlay row (<4% of tiles) */
	/* In: r0 = r3 = raw_map, r2 = raw, r12 = paltbl */
.Lop_partial:
	mov     r12, r5                     /* r5 = paltbl */
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
	bt      .Lop_part_done
	mov     r6, r0
	tst     #2, r0
	bt.s    1f
	add     #16, r6
	bra     .Lop_u_loop
	nop
1:	bra     .Lop_a_loop
	nop
.Lop_part_done:
	bra     .Lop_done
	nop
	.size   _cgba_render_4bpp_span_over_pair_sh4, .-_cgba_render_4bpp_span_over_pair_sh4
