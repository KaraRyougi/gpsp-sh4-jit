.syntax unified
.arm
.global _start
.section .text

_start:
  b reset
  .space 0xA0 - (. - _start), 0
  .ascii "CGBA MODE3  "
  .ascii "CGBA"
  .byte 0x00, 0x00
  .byte 0x96
  .space 0xC0 - (. - _start), 0

reset:
  ldr r0, =0x04000000
  ldr r1, =0x0403
  strh r1, [r0]
  ldr r0, =0x06000000
  ldr r1, =0x001f
  ldr r2, =38400

fill:
  strh r1, [r0], #2
  subs r2, r2, #1
  bne fill

hang:
  b hang

  .ltorg
  .space 0x8000 - (. - _start), 0xff
