/*
	Linker script for gpSP on the fx-CP400 (ClassPad II) under HollyHock-3.

	The image is split in two because of how little RAM is known to be free:

	low    0x8c052800, 330 KiB: the OS "VRAM backup" buffer, the one area a
	       HollyHock add-in may be loaded into. gint, the menu, storage and
	       start-up code live here, with their read-only data, all .data,
	       and the .bss of gint and libc. The launcher loads this part.

	arena  0x8c880000..0x8ce00000, 5.5 MiB inside the OS heap window
	       (0x8c280000..0x8ce80000). The hot emulator code, every other
	       .bss and the large emulator state are linked here. Nothing here
	       is loaded by the launcher: at start-up classpad_loader.c obtains
	       one heap block that contains this range, clears it, and reads the
	       .cgba.core section from the .hh3 file into place. Until that has
	       happened only the low part may run.

	The arena stops 0.5 MiB short of the end of the heap window: the block
	that reserves it ends at 0x8ce40000 (classpad_heap.h), 373 KiB
	past what is linked here, and the OS needs heap of its own for file
	calls: what is left above that and what is free below the arena.

	The program headers are explicit because the HollyHock loader copies
	every PT_LOAD to its address without any check. Arena sections are in
	no segment (:NONE), so the loader never writes to heap memory that has
	not been reserved.
*/

OUTPUT_ARCH(sh4)
OUTPUT_FORMAT(elf32-sh)
ENTRY(_cgba_hhk3_entry)

MEMORY
{
	ram   (rwx): o = 0x8c052800, l = 337920
	arena (rwx): o = 0x8c880000, l = 0x580000
	vbr   (rwx): o = 0x8cffef00, l = 0x1100
	ilram (rwx): o = 0xe5200000, l = 4k
	xyram (rwx): o = 0xe500e000, l = 16k
}

PHDRS
{
	low   PT_LOAD FLAGS(7);
	ilram PT_LOAD FLAGS(7);
	note  PT_NOTE FLAGS(4);
}

SECTIONS
{
	_brom = ORIGIN(ram);
	_srom = SIZEOF(.text) + SIZEOF(.rodata)
	      + SIZEOF(.gint.drivers) + SIZEOF(.gint.blocks);

	.note.hollyhock : {
		KEEP(*(.note.hollyhock))
	} > ram :low :note

	.text : {
		*(.text.entry)

		_bctors = . ;
		KEEP(*(.ctors .ctors.*))
		_ectors = . ;

		_bdtors = . ;
		KEEP(*(.dtors .dtors.*))
		_edtors = . ;

		. = ALIGN(0x10);
		_gint_exch_start = . ;
		*(.gint.exch)
		_gint_exch_end = . ;

		. = ALIGN(0x10);
		_gint_tlbh_start = . ;
		*(.gint.tlbh)
		_gint_tlbh_end = . ;

		/* Everything except the gameplay working set, which goes to the
		   arena below. Same object list as the fx-CG50 hot text. */
		*(EXCLUDE_FILE (*cpu_threaded.c.obj *cpu.cc.obj
			*gba_memory.c.obj *video.cc.obj *sh4_interp_helpers.c.obj
			*sh4_stub.S.obj) .text)
		*(EXCLUDE_FILE (*cpu_threaded.c.obj *cpu.cc.obj
			*gba_memory.c.obj *video.cc.obj *sh4_interp_helpers.c.obj
			*sh4_stub.S.obj) .text.*)
		*(.hh3* .eram*)
	} > ram :low

	.gint.blocks ALIGN(4) : ALIGN(4) {
		KEEP(*(.gint.blocks));
	} > ram :low

	.gint.drivers ALIGN(4) : ALIGN(4) {
		_gint_drivers = . ;
		KEEP(*(SORT_BY_NAME(.gint.drivers.*)));
		_gint_drivers_end = . ;
	} > ram :low

	.rodata ALIGN(4) : ALIGN(4) SUBALIGN(4) {
		*(.rodata.4)
		*(.rodata .rodata.*)
	} > ram :low

	.data ALIGN(4) : ALIGN(4) {
		_ldata = LOADADDR(.data);
		_rdata = . ;

		*(.data .data.*)
		*(.gint.mapped)
		*(.gint.mappedrel)

		. = ALIGN(16);
	} > ram :low

	.data.4 ALIGN(4) : ALIGN(4) SUBALIGN(4) {
		*(.data.4)
		. = ALIGN(16);
	} > ram :low

	_sdata = SIZEOF(.data) + SIZEOF(.data.4);

	/*
		Only the .bss of gint and the C library stays low: it is in use
		before main() runs, i.e. before the arena exists. The launcher
		zeroes it (it is the tail of the PT_LOAD).

		gint and fxlibc are LTO archives: their code reaches the linker as
		"*.ltrans.o" objects, not as archive members, so both spellings
		are listed. Our own objects are never LTO.
	*/
	.bss : {
		_rbss = . ;
		*.ltrans.o(.bss .bss.* COMMON)
		*libgint-cp.a:*(.bss .bss.* COMMON)
		*libc.a:*(.bss .bss.* COMMON)
		*libm.a:*(.bss .bss.* COMMON)
		*libopenlibm.a:*(.bss .bss.* COMMON)
		*libgcc.a:*(.bss .bss.* COMMON)
		. = ALIGN(16);
	} > ram :low

	_sbss = SIZEOF(.bss);
	ASSERT(_sbss > 0,
		"gint's .bss was not placed in the low image; check the library patterns above")

	.gint.bss : {
		*(.gint.bss)
		. = ALIGN(16);
		_euram = . ;
	} > ram :low

	_sgbss = SIZEOF(.gint.bss);

	. = ORIGIN(ilram);
	.ilram ALIGN(4) : ALIGN(4) {
		_lilram = ABSOLUTE(LOADADDR(.ilram) + ORIGIN(ram));
		_rilram = . ;
		*(.ilram.dispatch .ilram.dispatch.*)
		*(.ilram)
		. = ALIGN(16);
	} > ilram :ilram

	_silram = SIZEOF(.ilram);
	ASSERT(_silram > 0 && _silram <= 4k,
		"the ILRAM segment must hold the SH4 dispatch stubs (build with CGBA_DYNAREC=ON) and fit 4 KiB")

	/* Nothing may be linked into X/Y RAM: it has no program header here. */
	_lxyram = ABSOLUTE(0);
	_rxyram = ORIGIN(xyram);
	_sxyram = ABSOLUTE(0);

	_lgmapped = ABSOLUTE(0);
	_sgmapped = ABSOLUTE(0);
	_lreloc = ABSOLUTE(0);
	_sreloc = ABSOLUTE(0);
	_gint_region_vbr = ORIGIN(vbr);

	/*
		---- arena: reserved and filled at run time ----
	*/
	_cgba_arena_start = ORIGIN(arena);
	_cgba_arena_end = ORIGIN(arena) + LENGTH(arena);

	/* Hot emulator code. Kept in the file, but in no segment. */
	.cgba.core ORIGIN(arena) : {
		_cgba_core_start = . ;
		KEEP(*(.cgba.core.stamp))
		. = ALIGN(4);
		*cpu_threaded.c.obj(.text .text.*)
		*cpu.cc.obj(.text .text.*)
		*gba_memory.c.obj(.text .text.*)
		*video.cc.obj(.text .text.*)
		*sh4_interp_helpers.c.obj(.text .text.*)
		*sh4_stub.S.obj(.text .text.*)
		. = ALIGN(32);
		_cgba_core_end = . ;
	} > arena :NONE

	.cgba.bss (NOLOAD) : {
		*(.bss .bss.* COMMON)
		. = ALIGN(32);
	} > arena :NONE

	.cgba.highbss (NOLOAD) : {
		_cgba_highbss_start = . ;
		*(.cgba.highbss .cgba.highbss.*)
		. = ALIGN(32);
		_cgba_highbss_end = . ;
	} > arena :NONE

	/* Catch anything that expects on-chip X/Y RAM rather than drop it. */
	.xyram (NOLOAD) : {
		*(.xram .yram .xyram .xyram.fastmem .xyram.fastmem.*)
	} > xyram :NONE
	ASSERT(SIZEOF(.xyram) == 0,
		"X/Y RAM sections are not supported by the fx-CP400 image")

	/DISCARD/ : {
		*(.gint.rodata.sh3 .gint.data.sh3 .gint.bss.sh3)
		*(.jcr)
		*(.eh_frame_hdr)
		*(.eh_frame)
		*(.comment)
	}
}
