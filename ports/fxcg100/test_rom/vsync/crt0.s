@ Start-up for the vsync test ROM: cartridge header, stacks, .data/.bss,
@ the VBlank interrupt handler, then main() in Thumb.
@
@ The Nintendo logo area is left empty: emulators do not check it, and the
@ ROM is not meant for real hardware.

.syntax unified
.section .crt0, "ax"
.arm
.global _start
_start:
  b reset
  .space 0xA0 - (. - _start), 0
  .ascii "VSYNC TEST"
  .byte 0, 0                @ title, 12 bytes
  .ascii "CVSY"             @ game code
  .ascii "01"               @ maker code
  .byte 0x96                @ fixed value
  .byte 0x00                @ unit code
  .byte 0x00                @ device type
  .space 7, 0               @ reserved
  .byte 0x00                @ version
  .byte 0x00                @ header complement, patched by build.sh
  .space 2, 0               @ reserved

reset:
  mov r0, #0x12             @ IRQ mode
  msr cpsr_c, r0
  ldr sp, =0x03007FA0
  mov r0, #0x1F             @ system mode
  msr cpsr_c, r0
  ldr sp, =0x03007F00

  ldr r0, =__data_lma
  ldr r1, =__data_start
  ldr r2, =__data_end
1:
  cmp r1, r2
  ldrlo r3, [r0], #4
  strlo r3, [r1], #4
  blo 1b

  ldr r1, =__bss_start
  ldr r2, =__bss_end
  mov r3, #0
2:
  cmp r1, r2
  strlo r3, [r1], #4
  blo 2b

  ldr r0, =0x03007FFC       @ the BIOS calls the handler stored here
  ldr r1, =irq_handler
  str r1, [r0]

  ldr r0, =main
  mov lr, pc
  bx r0
3:
  b 3b

@ Acknowledge whatever interrupted, both in IF and in the BIOS copy that
@ VBlankIntrWait (SWI 5) waits on.
irq_handler:
  mov r0, #0x04000000
  add r0, r0, #0x200
  ldrh r1, [r0, #2]
  strh r1, [r0, #2]
  ldr r2, =0x03007FF8
  ldrh r3, [r2]
  orr r3, r3, r1
  strh r3, [r2]
  bx lr

.ltorg
