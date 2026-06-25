    .section .pretext, "ax"
    .global _start
    .global initialize

#ifndef CGBA_RETURN_TO_LOADER
#define CGBA_RETURN_TO_LOADER 0
#endif

#ifndef CGBA_USE_LOADER_STACK
#define CGBA_USE_LOADER_STACK 1
#endif

initialize:
_start:
    mov     r15, r1
    sts     pr, r2
    mov     r14, r3
    mov     r4, r8
    mov     r5, r9

#if !CGBA_USE_LOADER_STACK
    mov.l   .Lstack_top, r15
#endif
    mov.l   r1, @-r15
    mov.l   r2, @-r15
    mov.l   r3, @-r15
    mov.l   r8, @-r15
    mov.l   r9, @-r15

    mov.l   .Ldatald, r0
    mov.l   .Lsdata, r1
    mov.l   .Ledata, r2
1:
    cmp/hs  r2, r1
    bt      2f
    nop
    mov.l   @r0+, r3
    mov.l   r3, @r1
    bra     1b
    add     #4, r1

2:
    mov.l   .Lsbss, r1
    mov.l   .Lebss, r2
    mov     #0, r3
3:
    cmp/hs  r2, r1
    bt      4f
    nop
    mov.l   r3, @r1
    bra     3b
    add     #4, r1

4:
    mov.l   .Lemain, r0
    jsr     @r0
    nop

#if CGBA_RETURN_TO_LOADER
    mov.l   @r15+, r9
    mov.l   @r15+, r8
    mov.l   @r15+, r3
    mov.l   @r15+, r2
    mov.l   @r15+, r1

    mov     r9, r5
    mov     r8, r4
    mov     r3, r14
    lds     r2, pr
    rts
    mov     r1, r15
#else
5:
    bra     5b
    nop
#endif

    .align  4
.Lstack_top:
    .long   __stack_top
.Ldatald:
    .long   _datald
.Lsdata:
    .long   _sdata
.Ledata:
    .long   _edata
.Lsbss:
    .long   _sbss
.Lebss:
    .long   _ebss
.Lemain:
    .long   _emain
