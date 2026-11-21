
@---------------------------------------------------------------------------------
@ Setup some nicer names
@---------------------------------------------------------------------------------
numerator .req r0
denominator .req r1
accumulator .req r2
current_bit .req r3

numerator_signed .req r12
denominator_signed .req r3

sign_flip .req r12

result .req r0
remainder .req r1
result_abs .req r3

temp .req r3

@---------------------------------------------------------------------------------
.global swi_DivARM
.type swi_DivARM STT_FUNC
swi_DivARM:
@---------------------------------------------------------------------------------
	mov     temp, numerator
	mov     numerator, denominator
	mov     denominator, temp
	b       swi_Div

@---------------------------------------------------------------------------------
.global swi_Div
.type swi_Div STT_FUNC
swi_Div:
@ See http://www.tofla.iconbar.com/tofla/arm/arm02/index.htm for more information
@---------------------------------------------------------------------------------	
	@ Match GBA BIOS behavior on division by zero (avoid infinite loop):
	@ r0 = (numerator < 0) ? -1 : 1, r1 = numerator, r3 = 1
	cmp denominator, #0
	beq .Ldiv_by_zero

	@ Set if numerator is signed, and abs numerator
	ands numerator_signed, numerator, #0x80000000
	rsbmi numerator, numerator, #0

	// Same with denominator
	ands denominator_signed, denominator, #0x80000000
	rsbmi denominator, denominator, #0

	// Pack signs into r12: bit 31 = sign(numerator), bit 30 = sign(denominator)
	orr sign_flip, numerator_signed, denominator_signed, lsr #1

	mov accumulator, #0
	mov current_bit, #1

	// Align denominator up to numerator without shifting bit 31 out
	// (prevents infinite loop when numerator == 0x80000000, i.e. INT_MIN)
 0:
	cmp denominator, numerator, lsr #1
	movls denominator, denominator, lsl #1
	movls current_bit, current_bit, lsl #1
	bls 0b

	// Basically the grade-school algorithm, for unsigned integers in binary
 1:
	cmp numerator, denominator
	subcs numerator, numerator, denominator
	orrcs accumulator, accumulator, current_bit
	movs current_bit, current_bit, lsr #1
	movcc denominator, denominator, lsr #1
	bcc 1b

	mov remainder, numerator
	mov result_abs, accumulator
	mov result, accumulator

	// Remainder carries sign of numerator
	tst sign_flip, #0x80000000
	rsbmi remainder, remainder, #0

	// Quotient is negative if sign(numerator) != sign(denominator)
	eor sign_flip, sign_flip, sign_flip, lsl #1
	tst sign_flip, #0x80000000
	rsbmi result, result, #0
	
	bx lr

.Ldiv_by_zero:
	mov remainder, numerator
	mov result_abs, #1
	cmp numerator, #0
	mvnlt result, #0
	movge result, #1
	bx lr
