# CG50 upper-RAM probe

This hardware-only diagnostic tests `0x8c6f0000..0x8c800000` in 4 KiB pages.
Each page is written and verified through the uncached P2 alias while interrupts
are masked, then immediately restored and verified before interrupts resume.
Dirty P1 data lines are written back before the test and invalidated after the
restore.

The probe requires an explicit EXE confirmation and refuses non-CG50 hardware,
an unexpected RAM size, or a stack/VBR address overlapping the candidate range.
It writes `RAMPRB00.TXT` (incrementing through `99`) after the test.

A passing result proves that the range survived this bounded transaction. It
does not prove that a future OS version or another runtime path will never use
the range. A production allocation still needs normal gameplay, menu, storage,
suspend/wake, and exit testing on physical hardware.
