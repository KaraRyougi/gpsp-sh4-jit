/*
	Linker script for the fx-CG100 gpSP/gint bridge. This is the stock
	fxcg50 gint script with one extra NOLOAD arena for gpSP's large emulator
	state, keeping gint's normal userspace RAM/heap symbols intact.
*/

OUTPUT_ARCH(sh4)
OUTPUT_FORMAT(elf32-sh)
ENTRY(_start)

MEMORY
{
	rom     (rx):   o = 0x00300000, l = 2M
	ram     (rw):   o = 0x08101400, l = 491k
	/*
		The fx-CG100 has an 8 MiB RAM window at 0x8c000000..0x8c7fffff.
		0x8c800000 is already past the end on real hardware and causes a
		cold boot when touched. MPM 1.1+ relocates itself to 0x8c7f0000,
		places URAM in OS AppMemory at 0x8c160000..0x8c1dffff, loads the
		add-in ROM right below 0x8c7f0000 (~0x8c740000 for this binary),
		and reports [0x8c200000, romAddress) to gint via loadinfo (_ld1).

		Production links cap .cgba.highbss at 0x8c720000 (128 KiB below
		MPM 1.1's current ~0x8c740000 load address for future .g3a growth),
		and main.c also verifies the exact _ld1 upper bound at runtime
		(falling back to the legacy-MPM 0x8c655300 ceiling if _ld1 is
		absent). An explicit emulator-only headless/differential-harness
		build can override cgba_highram_limit up to 0x8c780000 at link
		time; never install that build on hardware.
	*/
	highram (rw):   o = 0x8c200000, l = 0x580000
	ilram   (rwx):  o = 0xe5200000, l = 4k
	xyram   (rwx):  o = 0xe500e000, l = 16k
}

SECTIONS
{
	_brom = ORIGIN(rom);

	.text : {
		*(.text.entry)

		_bctors = . ;
		KEEP(*(.ctors .ctors.*))
		_ectors = . ;

		_bdtors = . ;
		KEEP(*(.dtors .dtors.*))
		_edtors = . ;

		_gint_exch_start = . ;
		KEEP(*(.gint.exch))
		_gint_exch_end = . ;
		_gint_exch_size = ABSOLUTE(. - _gint_exch_start);

		_gint_tlbh_start = . ;
		KEEP(*(.gint.tlbh))
		_gint_tlbh_end = . ;
		_gint_tlbh_size = ABSOLUTE(. - _gint_tlbh_start);

		/*
			Co-locate the hottest per-frame emulation loops into the lowest
			1-2 64 KiB P0 pages so the SH7305's 4-entry hardware ITLB does not
			thrash bouncing across the ~650 KiB add-in .text image.
		*/
		*(.text.cgba.hot .text.cgba.hot.*)
		*sh4_stub.S.obj(.text .text.*)
		*gpsp/main.c.obj(.text .text.*)
		*sh4_mem_ops.S.obj(.text .text.*)
		*sh4_video_4bpp.S.obj(.text .text.*)
		*sh4_scale.S.obj(.text .text.*)
		*sh4_interp_helpers.c.obj(.text .text.*)
		*gba_memory.c.obj(.text .text.*)
		*video.cc.obj(.text .text.*)
		*cpu_threaded.c.obj(.text .text.*)
		*(.text .text.*)
	} > rom

	.gint.blocks : {
		KEEP(*(.gint.blocks));
	} > rom

	.gint.drivers : {
		_gint_drivers = . ;
		KEEP(*(SORT_BY_NAME(.gint.drivers.*)));
		_gint_drivers_end = . ;
	} > rom

	.rodata : SUBALIGN(4) {
		*(.rodata.4)
		*(.rodata .rodata.*)
	} > rom

	. = ORIGIN(ram);

	.bss (NOLOAD) : {
		_rbss = . ;
		*(.bss .bss.* COMMON)
		. = ALIGN(16);
	} > ram :NONE

	_sbss = SIZEOF(.bss);

	.data ALIGN(4) : ALIGN(4) {
		_ldata = LOADADDR(.data);
		_rdata = . ;
		*(.data .data.*)
		*(.gint.mappedrel)
		. = ALIGN(16);
	} > ram AT> rom

	.data.4 : SUBALIGN(4) {
		*(.data.4)
		. = ALIGN(16);
	} > ram AT> rom

	_sdata = SIZEOF(.data) + SIZEOF(.data.4);

	. = ORIGIN(ilram);
	.ilram ALIGN(4) : ALIGN(4) {
		_lilram = LOADADDR(.ilram);
		_rilram = . ;
		*(.ilram.dispatch .ilram.dispatch.*)
		*(.ilram)
		*(.gint.mapped)
		. = ALIGN(16);
	} > ilram AT> rom

	/* Remaining zero-wait-state ILRAM (up to 4 KiB) holds the hottest
	 * runtime-emitted fastmem routines (saved across OS world switches). */
	_cgba_ilram_fastmem_start = (_rilram + SIZEOF(.ilram) + 31) & ~31;
	_cgba_ilram_fastmem_end = ORIGIN(ilram) + LENGTH(ilram);

	. = ORIGIN(xyram);
	.xyram.fastmem (NOLOAD) : ALIGN(32) {
		_cgba_fastmem_start = . ;
		KEEP(*(.xyram.fastmem .xyram.fastmem.*))
		_cgba_fastmem_end = . ;
	} > xyram :NONE

	.xyram ALIGN(4) : ALIGN(4) {
		_lxyram = LOADADDR(.xyram);
		_rxyram = . ;
		*(.xram .yram .xyram)
		. = ALIGN(16);
	} > xyram AT> rom

	_silram = SIZEOF(.ilram);
	_sxyram = SIZEOF(.xyram);
	/*
		gint's TLB miss handler (_gint_tlbh) refuses to map any ROM address
		at or above 0x00300000 + _srom. Include .data, .ilram, and .xyram's
		load images in _srom; otherwise gint_load_onchip_sections() faults
		on return from the first OS world switch once .ilram crosses a 64 KiB
		TLB page boundary past .rodata.
	*/
	_srom = LOADADDR(.xyram) + SIZEOF(.xyram) - ORIGIN(rom);
	ASSERT(_brom + _srom >= _lilram + _silram,
		"fx-CG100 ROM span does not cover the .ilram load image")
	ASSERT(_silram <= 4k,
		"SH4 dispatch stubs exceed the 4 KiB ILRAM budget")
	ASSERT(SIZEOF(.xyram.fastmem) <= 8k,
		"SH4 resident fastmem exceeds its 8 KiB XYRAM budget")

	_lgmapped = ABSOLUTE(0);
	_sgmapped = ABSOLUTE(0);
	_lreloc = ABSOLUTE(0);
	_sreloc = ABSOLUTE(0);

	.gint.bss (NOLOAD) : {
		*(.gint.bss)
		. = ALIGN(16);
		_euram = . ;
	} > ram :NONE

	_sgbss = SIZEOF(.gint.bss);

	.cgba.highbss (NOLOAD) : {
		_cgba_highbss_start = . ;
		*(.cgba.highbss .cgba.highbss.*)
		. = ALIGN(32);
		_cgba_highbss_end = . ;
	} > highram :NONE

	PROVIDE(cgba_highram_limit = 0x8c720000);
	ASSERT(_cgba_highbss_end <= cgba_highram_limit,
		"fx-CG100 gpSP high RAM arena exceeds the MPM 1.1+ 0x8c720000 ceiling")

	/DISCARD/ : {
		*(.gint.rodata.sh3 .gint.data.sh3 .gint.bss.sh3)
		*(.jcr)
		*(.eh_frame_hdr)
		*(.eh_frame)
		*(.comment)
	}
}
