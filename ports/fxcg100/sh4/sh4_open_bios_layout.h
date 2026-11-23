/* Canonical address layout of the bundled vendor/gpsp/bios/open_gba_bios.bin.
 *
 * All SH-4 BIOS HLE fast paths (IRQ wrapper, SWI dispatcher, parked/resumable
 * SWI engines) depend on these fixed offsets when !cgba_using_external_bios.
 * Verified against the shipped binary in tests/sh4_bios_swi_hle_test.c. */
#ifndef CGBA_SH4_OPEN_BIOS_LAYOUT_H
#define CGBA_SH4_OPEN_BIOS_LAYOUT_H

/* Exception vectors & dispatcher return points */
#define CGBA_BIOS_SWI_VEC        0x00000008u
#define CGBA_BIOS_IRQ_VEC        0x00000018u
#define CGBA_BIOS_IRQ_RET        0x00000030u
#define CGBA_BIOS_SWI_DISPATCH_B 0x00000070u
#define CGBA_BIOS_SWI_RET        0x00000094u
#define CGBA_BIOS_SWI_TRAMP      0x00003EB0u

/* SWI 0x02 Halt */
#define CGBA_BIOS_HALT_ENTRY     0x000003D8u
#define CGBA_HALT_RESUME         0x000003E4u

/* SWI 0x04 / 0x05 IntrWait / VBlankIntrWait */
#define CGBA_BIOS_INTRWAIT_ENTRY 0x00000434u
#define CGBA_BIOS_INTRWAIT_HALT  0x00000478u
#define CGBA_INTRWAIT_RESUME     0x00000480u
#define CGBA_BIOS_INTRWAIT_BX    0x000004A4u

/* SWI 0x0B CpuSet */
#define CGBA_BIOS_CPUSET_ENTRY   0x00000614u
#define CGBA_CS_HCOPY_LOOP       0x00000668u
#define CGBA_CS_WCOPY_LOOP       0x000006ACu
#define CGBA_CS_HFILL_LOOP       0x000006E4u
#define CGBA_CS_WFILL_LOOP       0x00000708u

/* SWI 0x0C CpuFastSet */
#define CGBA_BIOS_FASTSET_ENTRY  0x00000720u
#define CGBA_FS_FILL_LOOP        0x00000770u
#define CGBA_FS_COPY_LOOP        0x000007A4u

/* SWI 0x0E BgAffineSet / SWI 0x0F ObjAffineSet */
#define CGBA_BIOS_BGAFFINE_ENTRY 0x000007E4u
#define CGBA_BA_LOOP             0x000007FCu
#define CGBA_BIOS_OBJAFFINE_ENTRY 0x000008E0u
#define CGBA_OA_LOOP             0x00000904u

/* SWI 0x11 / 0x12 LZ77UnCompWram / LZ77UnCompVram */
#define CGBA_BIOS_LZ8_ENTRY      0x00000A44u
#define CGBA_LZ8_LOOP            0x00000A74u
#define CGBA_LZ16_LOOP           0x00000BA4u

/* SWI 0x14 / 0x15 RLUnCompWram / RLUnCompVram */
#define CGBA_BIOS_RL8_ENTRY      0x00000F18u
#define CGBA_RL8_LOOP            0x00000F48u
#define CGBA_BIOS_RL16_ENTRY     0x00000FBCu
#define CGBA_RL16_LOOP           0x00001000u

/* Sine table used by BgAffineSet / ObjAffineSet */
#define CGBA_BIOS_SIN_TABLE      0x00002150u

#endif /* CGBA_SH4_OPEN_BIOS_LAYOUT_H */
