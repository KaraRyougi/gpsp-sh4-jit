
This BIOS is an open source replacement for Nintendo's official BIOS.
It was written originally by Normmatt and the VBA/VBA-M team, and its source
code can be found at https://github.com/Nebuleon/ReGBA/tree/master/bios

It is distributed under the GPL2 license (see repo)

The shipped `open_gba_bios.bin` carries one binary patch on top of that
source: SWI 6 (Div) and SWI 7 (DivArm) go through small wrappers at 0x3E00 and
0x3E0C that return ABS(quotient) in r3, as the official BIOS does. The same
wrappers are in `source/core.s`. Every other address in the image is
unchanged, which the SH-4 dynarec's BIOS cycle models depend on.
