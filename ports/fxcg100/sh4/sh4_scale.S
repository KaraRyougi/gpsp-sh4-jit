/*
 * Hand-written SH-4 big-endian RGB565 upscaling and vertical-blend kernels.
 *
 * Uses SH-4's 1-cycle XTRCT instruction to compute two adjacent RGB565 floor
 * averages in parallel (cgba_avg565x2) instead of scalar 16-bit averages, and
 * provides a fused dual-destination vertical-blend routine for duplicate
 * output rows in the 20 -> 27 fullscreen vertical map.
 *
 * Big-endian SH-4 u32 pixel order:
 *   bits [31:16] = first (left) pixel, bits [15:0] = second (right) pixel.
 *   XTRCT Rm, Rn -> Rn = (Rm << 16) | ((uint32_t)Rn >> 16) = [Rm.lo, Rn.hi].
 *
 * All functions follow the SH-4 C ABI and assume 4-byte aligned pointers and
 * pixel counts that are multiples of 8 (n = 320 or 384).
 */

	.text

/* ============================================================================
 * void cgba_scale_row_240_384_sh4(const uint16_t *src, uint16_t *dst)
 *   r4 = src (240 u16), r5 = dst (384 u16)
 *   24 groups x (10 src px -> 16 dst px):
 *     {a, ab, bc, c, cd, d, de, e} and {f, fh, hi, i, ij, j, jk, k}
 * ========================================================================== */
	.balign 4
	.global _cgba_scale_row_240_384_sh4
	.type   _cgba_scale_row_240_384_sh4, @function
_cgba_scale_row_240_384_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov     #24, r6
	mov.l   .L384_mask, r7

	.balign 4
.L384_loop:
	mov.l   @r4+, r8                    /* w0 = [a, b] */
	mov.l   @r4+, r9                    /* w1 = [c, d] */
	mov.l   @r4+, r10                   /* w2 = [e, f] */

	/* p0 = avg([a, b], [b, c]) = [ab, bc] */
	mov     r9, r0
	xtrct   r8, r0                      /* r0 = [b, c] */
	mov     r8, r1
	and     r0, r1
	xor     r8, r0
	and     r7, r0
	shlr    r0
	add     r1, r0                      /* r0 = [ab, bc] */

	swap.w  r8, r1                      /* r1 = [b, a] */
	mov     r0, r2
	xtrct   r1, r2                      /* r2 = [a, ab] */
	mov.l   r2, @r5
	mov     r9, r1
	xtrct   r0, r1                      /* r1 = [bc, c] */
	mov.l   r1, @(4, r5)

	/* p1 = avg([c, d], [d, e]) = [cd, de] */
	mov     r10, r0
	xtrct   r9, r0                      /* r0 = [d, e] */
	mov     r9, r1
	and     r0, r1
	xor     r9, r0
	and     r7, r0
	shlr    r0
	add     r1, r0                      /* r0 = [cd, de] */

	swap.w  r0, r1                      /* r1 = [de, cd] */
	swap.w  r9, r2                      /* r2 = [d, c] */
	xtrct   r1, r2                      /* r2 = [cd, d] */
	mov.l   r2, @(8, r5)
	mov     r10, r1
	xtrct   r0, r1                      /* r1 = [de, e] */
	mov.l   r1, @(12, r5)

	mov.l   @r4+, r8                    /* w3 = [h, i] */
	mov.l   @r4+, r9                    /* w4 = [j, k] */

	/* p2 = avg([f, h], [h, i]) = [fh, hi] */
	mov     r8, r0
	xtrct   r10, r0                     /* r0 = [f, h] */
	mov     r8, r1
	and     r0, r1
	xor     r8, r0
	and     r7, r0
	shlr    r0
	add     r1, r0                      /* r0 = [fh, hi] */

	mov     r0, r1
	xtrct   r10, r1                     /* r1 = [f, fh] */
	mov.l   r1, @(16, r5)
	swap.w  r8, r1                      /* r1 = [i, h] */
	xtrct   r0, r1                      /* r1 = [hi, i] */
	mov.l   r1, @(20, r5)

	/* p3 = avg([i, j], [j, k]) = [ij, jk] */
	mov     r9, r0
	xtrct   r8, r0                      /* r0 = [i, j] */
	mov     r9, r1
	and     r0, r1
	xor     r9, r0
	and     r7, r0
	shlr    r0
	add     r1, r0                      /* r0 = [ij, jk] */

	swap.w  r0, r1                      /* r1 = [jk, ij] */
	mov     r9, r2
	xtrct   r1, r2                      /* r2 = [ij, j] */
	mov.l   r2, @(24, r5)
	swap.w  r9, r1                      /* r1 = [k, j] */
	xtrct   r0, r1                      /* r1 = [jk, k] */
	dt      r6
	mov.l   r1, @(28, r5)
	bf.s    .L384_loop
	add     #32, r5

	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	.balign 4
.L384_mask:
	.long   0xF7DEF7DE
	.size   _cgba_scale_row_240_384_sh4, .-_cgba_scale_row_240_384_sh4

/* ============================================================================
 * void cgba_scale_row_240_384_sharp_sh4(const uint16_t *src, uint16_t *dst)
 *   24 groups x (10 src px -> 16 dst px):
 *     {a, ab, b, c, cd, d, e, e} and {f, fh, h, i, ij, j, k, k}
 * ========================================================================== */
	.balign 4
	.global _cgba_scale_row_240_384_sharp_sh4
	.type   _cgba_scale_row_240_384_sharp_sh4, @function
_cgba_scale_row_240_384_sharp_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov     #24, r6
	mov.l   .L384s_mask, r7

	.balign 4
.L384s_loop:
	mov.l   @r4+, r8                    /* w0 = [a, b] */
	mov.l   @r4+, r9                    /* w1 = [c, d] */
	mov.l   @r4+, r10                   /* w2 = [e, f] */

	mov     r9, r0
	xtrct   r8, r0                      /* [b, c] */
	mov.l   r0, @(4, r5)
	swap.w  r10, r1                     /* [f, e] */
	mov     r10, r2
	xtrct   r1, r2                      /* [e, e] */
	mov.l   r2, @(12, r5)

	/* p0 = avg([a, c], [b, d]) = [ab, cd] */
	swap.w  r8, r3                      /* [b, a] */
	mov     r9, r0
	xtrct   r3, r0                      /* r0 = [a, c] */
	swap.w  r9, r2                      /* [d, c] */
	mov     r2, r1
	xtrct   r8, r1                      /* r1 = [b, d] */
	mov     r0, r8
	and     r1, r8
	xor     r1, r0
	and     r7, r0
	shlr    r0
	add     r8, r0                      /* r0 = [ab, cd] */

	mov     r0, r1
	xtrct   r3, r1                      /* [a, ab] */
	mov.l   r1, @r5
	xtrct   r0, r2                      /* [cd, d] (r0.lo=cd, r2.hi=d) */
	mov.l   r2, @(8, r5)

	mov.l   @r4+, r8                    /* w3 = [h, i] */
	mov.l   @r4+, r9                    /* w4 = [j, k] */

	mov.l   r8, @(20, r5)               /* [h, i] */
	swap.w  r9, r1                      /* [k, j] */
	xtrct   r9, r1                      /* [k, k] */
	mov.l   r1, @(28, r5)

	/* p1 = avg([f, i], [h, j]) = [fh, ij] */
	swap.w  r8, r3                      /* [i, h] */
	mov     r3, r0
	xtrct   r10, r0                     /* r0 = [f, i] */
	mov     r9, r1
	xtrct   r3, r1                      /* r1 = [h, j] */
	mov     r0, r2
	and     r1, r2
	xor     r1, r0
	and     r7, r0
	shlr    r0
	add     r2, r0                      /* r0 = [fh, ij] */

	mov     r0, r1
	xtrct   r10, r1                     /* [f, fh] */
	mov.l   r1, @(16, r5)
	xtrct   r0, r9                      /* [ij, j] (r0.lo=ij, r9.hi=j) */
	dt      r6
	mov.l   r9, @(24, r5)
	bf.s    .L384s_loop
	add     #32, r5

	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	.balign 4
.L384s_mask:
	.long   0xF7DEF7DE
	.size   _cgba_scale_row_240_384_sharp_sh4, .-_cgba_scale_row_240_384_sharp_sh4

/* ============================================================================
 * void cgba_scale_row_240_320_sh4(const uint16_t *src, uint16_t *dst)
 *   40 groups x (6 src px -> 8 dst px):
 *     {a, ab, bc, c, d, de, ef, f}
 * ========================================================================== */
	.balign 4
	.global _cgba_scale_row_240_320_sh4
	.type   _cgba_scale_row_240_320_sh4, @function
_cgba_scale_row_240_320_sh4:
	mov.l   r8, @-r15
	mov.l   r9, @-r15
	mov.l   r10, @-r15
	mov     #40, r6
	mov.l   .L320_mask, r7

	.balign 4
.L320_loop:
	mov.l   @r4+, r8                    /* w0 = [a, b] */
	mov.l   @r4+, r9                    /* w1 = [c, d] */
	mov.l   @r4+, r10                   /* w2 = [e, f] */

	/* p0 = avg([a, b], [b, c]) = [ab, bc] */
	mov     r9, r0
	xtrct   r8, r0                      /* r0 = [b, c] */
	mov     r8, r1
	and     r0, r1
	xor     r8, r0
	and     r7, r0
	shlr    r0
	add     r1, r0                      /* r0 = [ab, bc] */

	swap.w  r8, r1                      /* r1 = [b, a] */
	mov     r0, r2
	xtrct   r1, r2                      /* r2 = [a, ab] */
	mov.l   r2, @r5
	mov     r9, r1
	xtrct   r0, r1                      /* r1 = [bc, c] */
	mov.l   r1, @(4, r5)

	/* p1 = avg([d, e], [e, f]) = [de, ef] */
	mov     r10, r0
	xtrct   r9, r0                      /* r0 = [d, e] */
	mov     r10, r1
	and     r0, r1
	xor     r10, r0
	and     r7, r0
	shlr    r0
	add     r1, r0                      /* r0 = [de, ef] */

	mov     r0, r1
	xtrct   r9, r1                      /* r1 = [d, de] */
	mov.l   r1, @(8, r5)
	swap.w  r10, r1                     /* r1 = [f, e] */
	xtrct   r0, r1                      /* r1 = [ef, f] */
	dt      r6
	mov.l   r1, @(12, r5)
	bf.s    .L320_loop
	add     #16, r5

	mov.l   @r15+, r10
	mov.l   @r15+, r9
	rts
	mov.l   @r15+, r8

	.balign 4
.L320_mask:
	.long   0xF7DEF7DE
	.size   _cgba_scale_row_240_320_sh4, .-_cgba_scale_row_240_320_sh4

/* ============================================================================
 * void cgba_scale_row_avg_sh4(
 *   const uint16_t *a, const uint16_t *b, uint16_t *dst, unsigned n)
 *   r4 = a, r5 = b, r6 = dst, r7 = n (multiple of 8 pixels)
 * ========================================================================== */
	.balign 4
	.global _cgba_scale_row_avg_sh4
	.type   _cgba_scale_row_avg_sh4, @function
_cgba_scale_row_avg_sh4:
	shlr2   r7
	shlr    r7                          /* r7 = n / 8 (4 u32 per iter) */
	tst     r7, r7
	bt      .Lavg_done
	mov.l   .Lavg_mask, r3

	.balign 4
.Lavg_loop:
	mov.l   @r4+, r0
	mov.l   @r5+, r1
	mov     r0, r2
	and     r1, r2
	xor     r1, r0
	and     r3, r0
	shlr    r0
	add     r2, r0
	mov.l   r0, @r6

	mov.l   @r4+, r0
	mov.l   @r5+, r1
	mov     r0, r2
	and     r1, r2
	xor     r1, r0
	and     r3, r0
	shlr    r0
	add     r2, r0
	mov.l   r0, @(4, r6)

	mov.l   @r4+, r0
	mov.l   @r5+, r1
	mov     r0, r2
	and     r1, r2
	xor     r1, r0
	and     r3, r0
	shlr    r0
	add     r2, r0
	mov.l   r0, @(8, r6)

	mov.l   @r4+, r0
	mov.l   @r5+, r1
	mov     r0, r2
	and     r1, r2
	xor     r1, r0
	and     r3, r0
	shlr    r0
	add     r2, r0
	dt      r7
	mov.l   r0, @(12, r6)
	bf.s    .Lavg_loop
	add     #16, r6

.Lavg_done:
	rts
	nop

	.balign 4
.Lavg_mask:
	.long   0xF7DEF7DE
	.size   _cgba_scale_row_avg_sh4, .-_cgba_scale_row_avg_sh4

/* ============================================================================
 * void cgba_scale_row_avg_dup2_sh4(
 *   const uint16_t *a, const uint16_t *b,
 *   uint16_t *dst0, uint16_t *dst1, unsigned n)
 *   r4 = a, r5 = b, r6 = dst0, r7 = dst1, @(0, sp) = n (multiple of 8 pixels)
 * ========================================================================== */
	.balign 4
	.global _cgba_scale_row_avg_dup2_sh4
	.type   _cgba_scale_row_avg_dup2_sh4, @function
_cgba_scale_row_avg_dup2_sh4:
	mov.l   @r15, r0                    /* n */
	shlr2   r0
	shlr    r0                          /* n / 8 */
	tst     r0, r0
	bt      .Lavg2_ret
	mov.l   r8, @-r15
	mov     r0, r8
	mov.l   .Lavg2_mask, r3

	.balign 4
.Lavg2_loop:
	mov.l   @r4+, r0
	mov.l   @r5+, r1
	mov     r0, r2
	and     r1, r2
	xor     r1, r0
	and     r3, r0
	shlr    r0
	add     r2, r0
	mov.l   r0, @r6
	mov.l   r0, @r7

	mov.l   @r4+, r0
	mov.l   @r5+, r1
	mov     r0, r2
	and     r1, r2
	xor     r1, r0
	and     r3, r0
	shlr    r0
	add     r2, r0
	mov.l   r0, @(4, r6)
	mov.l   r0, @(4, r7)

	mov.l   @r4+, r0
	mov.l   @r5+, r1
	mov     r0, r2
	and     r1, r2
	xor     r1, r0
	and     r3, r0
	shlr    r0
	add     r2, r0
	mov.l   r0, @(8, r6)
	mov.l   r0, @(8, r7)

	mov.l   @r4+, r0
	mov.l   @r5+, r1
	mov     r0, r2
	and     r1, r2
	xor     r1, r0
	and     r3, r0
	shlr    r0
	add     r2, r0
	mov.l   r0, @(12, r6)
	mov.l   r0, @(12, r7)
	add     #16, r6
	dt      r8
	bf.s    .Lavg2_loop
	add     #16, r7

	rts
	mov.l   @r15+, r8

.Lavg2_ret:
	rts
	nop

	.balign 4
.Lavg2_mask:
	.long   0xF7DEF7DE
	.size   _cgba_scale_row_avg_dup2_sh4, .-_cgba_scale_row_avg_dup2_sh4
