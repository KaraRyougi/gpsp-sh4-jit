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
	.balign 4
	.global _cgba_render_obj_4bpp_tiles_sh4
	.type   _cgba_render_obj_4bpp_tiles_sh4, @function
_cgba_render_obj_4bpp_tiles_sh4:
.Lobj_loop:
	mov.l   @r4, r2
	tst     r2, r2
	bt      .Lobj_next
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
	mov.w   r0, @(12, r6)               /* dst[6] = p6 */
1:	mov     r2, r0
	tst     #0x1e, r0
	bt.s    2f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(8, r6)                /* dst[4] = p4 */
2:	mov     r2, r0
	tst     #0x1e, r0
	bt.s    3f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(4, r6)                /* dst[2] = p2 */
3:	mov     r2, r0
	tst     #0x1e, r0
	bt      4f
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @r6                     /* dst[0] = p0 */
4:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    5f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(14, r6)               /* dst[7] = p7 */
5:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    6f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(10, r6)               /* dst[5] = p5 */
6:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    7f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(6, r6)                /* dst[3] = p3 */
7:	mov     r1, r0
	tst     #0x1e, r0
	bt      .Lobj_next
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(2, r6)                /* dst[1] = p1 */
.Lobj_next:
	add     #32, r4
	dt      r5
	bf.s    .Lobj_loop
	add     #16, r6
	rts
	nop
	.size   _cgba_render_obj_4bpp_tiles_sh4, .-_cgba_render_obj_4bpp_tiles_sh4

	.balign 4
	.global _cgba_render_obj_4bpp_tiles_hf_sh4
	.type   _cgba_render_obj_4bpp_tiles_hf_sh4, @function
_cgba_render_obj_4bpp_tiles_hf_sh4:
.Lobj_hf_loop:
	mov.l   @r4, r2
	tst     r2, r2
	bt      .Lobj_hf_next
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
	mov.w   r0, @(2, r6)                /* dst[1] = p6 */
1:	mov     r2, r0
	tst     #0x1e, r0
	bt.s    2f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(6, r6)                /* dst[3] = p4 */
2:	mov     r2, r0
	tst     #0x1e, r0
	bt.s    3f
	shlr8   r2
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(10, r6)               /* dst[5] = p2 */
3:	mov     r2, r0
	tst     #0x1e, r0
	bt      4f
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(14, r6)               /* dst[7] = p0 */
4:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    5f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @r6                     /* dst[0] = p7 */
5:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    6f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(4, r6)                /* dst[2] = p5 */
6:	mov     r1, r0
	tst     #0x1e, r0
	bt.s    7f
	shlr8   r1
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(8, r6)                /* dst[4] = p3 */
7:	mov     r1, r0
	tst     #0x1e, r0
	bt      .Lobj_hf_next
	and     #0x1e, r0
	mov.w   @(r0, r7), r0
	mov.w   r0, @(12, r6)               /* dst[6] = p1 */
.Lobj_hf_next:
	add     #-32, r4
	dt      r5
	bf.s    .Lobj_hf_loop
	add     #16, r6
	rts
	nop
	.size   _cgba_render_obj_4bpp_tiles_hf_sh4, .-_cgba_render_obj_4bpp_tiles_hf_sh4

/* ============================================================================
 * Fast 32-byte-multiple strip copy for 4-byte-aligned non-overlapping buffers.
 *   void cgba_copy_32b_sh4(void *dst, const void *src, uint32_t nbytes)
 *     r4 = dst, r5 = src, r6 = nbytes (multiple of 32).
 * 64-byte unrolled dt loop (17.5 insns / 32 B vs newlib memcpy's 21 insns / 32 B)
 * with zero callee-saved register spills and grouped 4-word bursts.
 * ========================================================================== */
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
	tst     #0x08, r0                   /* T = !vflip */
	bf      .Lsb_to_vflip
	add     r7, r1                      /* r1 = tile_ptr (step +4/row) */

	tst     #0x04, r0                   /* T = !hflip */
	and     #0xF0, r0
	shlr2   r0
	shll8   r0
	mov     r10, r5
	bf.s    .Lsb_to_nv_hf
	add     r0, r5                      /* r5 = &pair_lut[pal][0] */

	mov     r13, r9                     /* r9 = k rows */
.Lsb_nv_nhf_row:
	mov.l   @r1+, r2
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
	bf.s    .Lsb_nv_nhf_row
	add     r11, r6

	dt      r8
	bf.s    .Lsb_tile
	add     r14, r6

.Lsb_done:
	sts     mach, r8
	tst     r8, r8
	bf      .Lsb_span2
	add     #4, r15                     /* pop vflip_off */
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

.Lsb_to_nv_hf:
	bra     .Lsb_nv_hf
	nop
.Lsb_to_vflip:
	bra     .Lsb_vflip
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

	/* !vflip, hflip */
.Lsb_nv_hf:
	mov     r13, r9
.Lsb_nv_hf_row:
	mov.l   @r1+, r2
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
	bf.s    .Lsb_nv_hf_row
	add     r11, r6

	dt      r8
	bt.s    .Lsb_done_bra
	add     r14, r6
	bra     .Lsb_tile
	nop

	/* vflip (!hflip & hflip) */
.Lsb_vflip:
	mov.l   @r15, r2                    /* vflip_off = 28 - 2*start_r*4 */
	add     r7, r1
	add     r2, r1                      /* r1 = tile_ptr for row (7-start_r) */
	tst     #0x04, r0                   /* T = !hflip */
	and     #0xF0, r0
	shlr2   r0
	shll8   r0
	mov     r10, r5
	bf.s    .Lsb_vf_hf
	add     r0, r5

	mov     r13, r9
.Lsb_vf_nhf_row:
	mov.l   @r1, r2
	add     #-4, r1
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
	bf.s    .Lsb_vf_nhf_row
	add     r11, r6

	dt      r8
	bt.s    .Lsb_done_bra
	add     r14, r6
	bra     .Lsb_tile
	nop

.Lsb_vf_hf:
	mov     r13, r9
.Lsb_vf_hf_row:
	mov.l   @r1, r2
	add     #-4, r1
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
	bf.s    .Lsb_vf_hf_row
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
	mov.l   r1, @-r15                   /* @(8, r15) = dst_next_delta */
	mov.l   r2, @-r15                   /* @(4, r15) = k */
	mov.l   r3, @-r15                   /* @(0, r15) = vflip_off */

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
	tst     #0x08, r0                   /* T = !vflip */
	bf      .Lso_to_vflip
	add     r7, r1                      /* r1 = tile_ptr (step +4/row) */

	mov.l   @(4, r15), r9               /* r9 = k */
.Lso_nv_scan:
	mov.l   @r1+, r2
	tst     r2, r2
	bf      .Lso_nv_nz0
	dt      r9
	bf.s    .Lso_nv_scan
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

.Lso_nv_nz0:
	mov     r3, r0
	tst     #0x04, r0                   /* T = !hflip */
	and     #0xF0, r0
	shlr2   r0
	shll8   r0
	mov     r10, r5
	bf.s    .Lso_to_nv_hf
	add     r0, r5                      /* r5 = &pair_lut[pal][0] */

	mov     r13, r0
.Lso_nv_nhf_r0:
	not     r2, r3
	add     r2, r0
	and     r3, r0
	tst     r14, r0
	bf      .Lso_to_nv_nhf_part

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
.Lso_nv_nhf_next:
	dt      r9
	bt.s    .Lso_tile_next
	add     r11, r6
	mov.l   @r1+, r2
	mov     r13, r0
	tst     r2, r2
	bf      .Lso_nv_nhf_r0
	bra     .Lso_nv_nhf_next
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
	add     #12, r15                    /* pop vflip_off, k, dst_next_delta */
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

.Lso_to_nv_nhf_part:
	bra     .Lso_nv_nhf_part
	nop
.Lso_to_nv_hf:
	bra     .Lso_nv_hf_entry
	nop
.Lso_to_vflip:
	bra     .Lso_vflip
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

	/* Out-of-line 2-tile-at-a-time known-empty run skip (keeps hot loop compact) */
.Lso_ke_run:
	dt      r8
	bt.s    .Lso_ke_done
	add     #16, r6
	mov     r4, r0
	tst     #2, r0
	bt      .Lso_ke_aligned
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bf      .Lso_ke_have_r0
	dt      r8
	bt.s    .Lso_ke_done
	add     #16, r6
.Lso_ke_aligned:
	mov     r8, r5
	shlr    r5
	tst     r5, r5
	bt      .Lso_ke_tail1
	extu.w  r12, r2
	swap.w  r2, r1
	or      r1, r2                      /* r2 = [r12 | r12] */
.Lso_ke_pair:
	mov.l   @r4+, r0
	cmp/eq  r2, r0
	bf      .Lso_ke_pmiss
	dt      r5
	bf.s    .Lso_ke_pair
	add     #32, r6
.Lso_ke_tail1:
	mov     r8, r0
	tst     #1, r0
	bt      .Lso_ke_done
	mov.w   @r4+, r0
	cmp/eq  r12, r0
	bf.s    .Lso_ke_have_r0
	mov     #1, r8
	bra     .Lso_done
	add     #16, r6
.Lso_ke_pmiss:
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
	bra     .Lso_tile_have_r0
	mov     r1, r0
1:	add     #16, r6
	bra     .Lso_tile_have_r0
	add     #-1, r8
.Lso_ke_have_r0:
	bra     .Lso_tile_have_r0
	nop
.Lso_ke_done:
	bra     .Lso_done
	nop

	/* !vflip !hflip partial row (pair_lut in r5, raw in r2) */
.Lso_nv_nhf_part:
	extu.b  r2, r0
	tst     #0x0F, r0
	shlr8   r2
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
1:	bra     .Lso_nv_nhf_next
	nop

	/* !vflip hflip row loop (r5 = &pair_lut[pal][0], r2 = first nonzero word) */
.Lso_nv_hf_entry:
	mov     r13, r0
.Lso_nv_hf_r0:
	not     r2, r3
	add     r2, r0
	and     r3, r0
	tst     r14, r0
	bf      .Lso_nv_hf_part

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
.Lso_nv_hf_next:
	dt      r9
	bt.s    .Lso_hf_tile_done
	add     r11, r6
	mov.l   @r1+, r2
	mov     r13, r0
	tst     r2, r2
	bf      .Lso_nv_hf_r0
	bra     .Lso_nv_hf_next
	nop
.Lso_hf_tile_done:
	bra     .Lso_tile_next
	nop

.Lso_nv_hf_part:
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
1:	bra     .Lso_nv_hf_next
	nop

	/* vflip overlay tile (!hflip & hflip) */
.Lso_vflip:
	mov.l   @r15, r2                    /* vflip_off = 28 - 2*start_r*4 */
	add     r7, r1
	add     r2, r1                      /* r1 = tile_ptr for row (7-start_r) */
	mov.l   @(4, r15), r9               /* r9 = k */
.Lso_vf_scan:
	mov.l   @r1, r2
	add     #-4, r1
	tst     r2, r2
	bf      .Lso_vf_nz0
	dt      r9
	bf.s    .Lso_vf_scan
	add     r11, r6
	mov     r3, r12                     /* all k rows empty */
	bra     .Lso_tile_next
	nop

.Lso_vf_nz0:
	mov     r3, r0
	tst     #0x04, r0                   /* T = !hflip */
	and     #0xF0, r0
	shlr2   r0
	shll8   r0
	mov     r10, r5
	bf.s    .Lso_vf_hf_entry
	add     r0, r5

	mov     r13, r0
.Lso_vf_nhf_r0:
	not     r2, r3
	add     r2, r0
	and     r3, r0
	tst     r14, r0
	bf      .Lso_vf_nhf_part

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
.Lso_vf_nhf_next:
	dt      r9
	bt.s    .Lso_vf_tile_done
	add     r11, r6
	mov.l   @r1, r2
	add     #-4, r1
	mov     r13, r0
	tst     r2, r2
	bf      .Lso_vf_nhf_r0
	bra     .Lso_vf_nhf_next
	nop
.Lso_vf_tile_done:
	bra     .Lso_tile_next
	nop

.Lso_vf_nhf_part:
	extu.b  r2, r0
	tst     #0x0F, r0
	shlr8   r2
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
1:	bra     .Lso_vf_nhf_next
	nop

.Lso_vf_hf_entry:
	mov     r13, r0
.Lso_vf_hf_r0:
	not     r2, r3
	add     r2, r0
	and     r3, r0
	tst     r14, r0
	bf      .Lso_vf_hf_part

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
.Lso_vf_hf_next:
	dt      r9
	bt.s    .Lso_vf_tile_done
	add     r11, r6
	mov.l   @r1, r2
	add     #-4, r1
	mov     r13, r0
	tst     r2, r2
	bf      .Lso_vf_hf_r0
	bra     .Lso_vf_hf_next
	nop

.Lso_vf_hf_part:
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
1:	bra     .Lso_vf_hf_next
	nop
	.size   _cgba_render_4bpp_strip_over_sh4, .-_cgba_render_4bpp_strip_over_sh4
