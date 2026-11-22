
This BIOS is an open source replacement for Nintendo's official BIOS.
It was written originally by Normmatt and the VBA/VBA-M team, and its source
code can be found at https://github.com/Nebuleon/ReGBA/tree/master/bios

It is distributed under the GPL2 license (see repo)

The shipped `open_gba_bios.bin` carries three small binary patches on top of that
source while leaving every other address in the image unchanged (which the SH-4
dynarec's BIOS cycle models depend on):
1. SWI 6 (Div) and SWI 7 (DivArm) go through wrappers at 0x3E00 and 0x3E0C that
   return ABS(quotient) in r3, as the official BIOS does (`source/core.s`).
2. `swi_Div` at 0x17A8 branches to a fixed routine at 0x3E20 (`source/divide.s`)
   that avoids infinite loops on division by zero and `INT_MIN` (0x80000000)
   and preserves the numerator's sign on the remainder in r1.
3. `swi_IntrWait` at 0x0434..0x04D3 is replaced in-place with an assembly
   routine that uses only `r0-r3, r12` (never keeping live state in `r4-r7`
   across `HALTCNT`/IRQ, matching Nintendo's BIOS so game IRQ handlers that
   clobber `r4` in SYS mode such as Harobots / `AHQJ` do not hang), tests the
   full 16-bit IRQ mask (fixing `bool CheckInterrupts` truncating bits 8..13),
   and returns immediately without halting when `discard == 0` and a matching
   flag is already pending in `IFBIOS`.
