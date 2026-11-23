/* SH-4A cache-allocating (movca.l) bulk memory copy and fill routines.
 * Shared by DMA and BIOS CpuFastSet/CpuSet on SH-4 targets independently
 * of CGBA_SH4_ASM_4BPP. */

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

