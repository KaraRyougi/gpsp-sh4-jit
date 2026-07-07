/*
 * fx-CG50 / fx-CG20 (Prizm) syscall trampoline for Bfile_GetBlockAddress,
 * which the CG100 reaches by a firmware address but the CG50 exposes only as
 * OS syscall 0x1DAA (SimonLothar). gint does not wrap it, so invoke it through
 * the OS syscall gate at 0x80020070: r0 = syscall number, args in r4-r7,
 * result in r0.
 *
 *   int cgba_cg50_bfile_get_block_address(int fd, int offset, void **addr)
 *
 * MUST be called in OS mode -- i.e. via gint_world_switch(GINT_CALL(...)) --
 * exactly like gint's own BFile_* wrappers. This is a tail-jump trampoline:
 * the gate returns (PC = PR) straight to our caller, so r4-r6 pass through
 * untouched and the syscall's result in r0 is returned as-is.
 */
	.section .text
	.align 2
	.global _cgba_cg50_bfile_get_block_address
_cgba_cg50_bfile_get_block_address:
	mov.l	.Lgate, r2
	mov.l	.Lnum, r0
	jmp	@r2
	nop
	.align 2
.Lgate:	.long	0x80020070
.Lnum:	.long	0x00001DAA
