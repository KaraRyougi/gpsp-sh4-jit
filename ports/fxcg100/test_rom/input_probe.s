.syntax unified
.arm
.global _start
.section .text

_start:
  b reset
  .space 0xA0 - (. - _start), 0
  .ascii "CGBA INPUT  "
  .ascii "CGBA"
  .byte 0x00, 0x00
  .byte 0x96
  .space 0xC0 - (. - _start), 0

reset:
  ldr r0, =0x04000000
  ldr r1, =0x0403
  strh r1, [r0]
  ldr r6, =0x04000130
  mov r7, #10

poll:
  ldrh r0, [r6]
  mvn r0, r0
  ldr r1, =0x03ff
  ands r0, r0, r1
  beq draw

  mov r1, #0

find_key:
  mov r2, #1
  lsl r2, r2, r1
  tst r0, r2
  bne found_key
  add r1, r1, #1
  cmp r1, #10
  blt find_key
  b draw

found_key:
  mov r7, r1

draw:
  adr r2, color_table
  add r2, r2, r7, lsl #1
  ldrh r4, [r2]
  ldr r0, =0x06000000
  ldr r2, =38400

fill:
  strh r4, [r0], #2
  subs r2, r2, #1
  bne fill
  b poll

.align 2
color_table:
  .hword 0x001f  @ A: red
  .hword 0x7c00  @ B: blue
  .hword 0x021f  @ SELECT: orange
  .hword 0x7fff  @ START: white
  .hword 0x03e0  @ RIGHT: green
  .hword 0x7fe0  @ LEFT: cyan
  .hword 0x03ff  @ UP: yellow
  .hword 0x7c1f  @ DOWN: magenta
  .hword 0x421f  @ R: violet
  .hword 0x7e10  @ L: teal
  .hword 0x03e0  @ NONE: green

  .ltorg
  .space 0x8000 - (. - _start), 0xff
