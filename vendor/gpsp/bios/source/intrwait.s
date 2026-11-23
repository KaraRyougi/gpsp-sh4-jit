@---------------------------------------------------------------------------------
@ 0x04 - IntrWait
@   Continues to wait in Halt state until one (or more) of the specified
@   interrupt(s) do occur. Forcefully sets IME=1.
@
@   Uses only r0-r3 and r12 (never keeping live state in r4-r7 across
@   HALTCNT/IRQ, matching Nintendo's BIOS so game IRQ handlers that clobber r4
@   in SYS mode such as Harobots / AHQJ do not hang), keeps r12 = 0x04000000
@   (REG_BASE) and r2 = 0x208 across HALTCNT/IRQ (matching Nintendo's BIOS so
@   game IRQ handlers such as Elevator Action Old & New / ANWJ that write
@   IFBIOS via [r12, #-8] succeed), tests the full 16-bit IRQ mask, and
@   returns immediately without halting when discard == 0 and a matching flag
@   is already pending in IFBIOS (0x03FFFFF8).
@
@   In the shipped open_gba_bios.bin this exact 29-instruction routine is
@   patched in-place at 0x0434..0x04A4 (followed by 11 NOPs at 0x04A8..0x04D3
@   to keep downstream BIOS addresses unchanged).
@---------------------------------------------------------------------------------
	.text
	.align	2
	.arm

	.global	swi_IntrWait
	.type	swi_IntrWait, %function
swi_IntrWait:
	mov	r12, #0x04000000
	mov	r2, #0x208
	mov	r1, r1, lsl #16
	mov	r1, r1, lsr #16
	mov	r3, #0
	strh	r3, [r12, r2]		@ REG_IME = 0
	ldrh	r3, [r12, #-8]		@ r3 = IFBIOS (0x03FFFFF8)
	ands	r3, r3, r1
	beq	.Lintrwait_loop
	ldrh	r2, [r12, #-8]
	bic	r2, r2, r3
	strh	r2, [r12, #-8]		@ clear matched IFBIOS bits
	mov	r2, #0x208
	cmp	r0, #0			@ discard == 0?
	beq	.Lintrwait_done

.Lintrwait_loop:
	mov	r3, #1
	strh	r3, [r12, r2]		@ REG_IME = 1
	mov	r3, #0
	strb	r3, [r12, #0x301]	@ HALTCNT = 0 (Halt)
	strh	r3, [r12, r2]		@ REG_IME = 0
	ldrh	r0, [r12, #-8]		@ r0 = IFBIOS
	ands	r3, r0, r1
	beq	.Lintrwait_loop
	bic	r0, r0, r3
	strh	r0, [r12, #-8]		@ clear matched IFBIOS bits
	mov	r0, r3

.Lintrwait_done:
	mov	r3, #1
	strh	r3, [r12, r2]		@ REG_IME = 1
	bx	lr
