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
		cold boot when touched. Local CG100/MPM tooling uses 0x8c200000 as
		a scratch/upload window, so keep gpSP's temporary high arena there
		until the core is moved to explicit heap/NOR-backed allocation.
	*/
	highram (rw):   o = 0x8c200000, l = 5M
	ilram   (rwx):  o = 0xe5200000, l = 4k
	xyram   (rwx):  o = 0xe500e000, l = 16k
}

SECTIONS
{
	_brom = ORIGIN(rom);
	_srom = SIZEOF(.text) + SIZEOF(.rodata)
	      + SIZEOF(.gint.drivers) + SIZEOF(.gint.blocks);

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
		*(.ilram)
		*(.gint.mapped)
		. = ALIGN(16);
	} > ilram AT> rom

	. = ORIGIN(xyram);
	.xyram ALIGN(4) : ALIGN(4) {
		_lxyram = LOADADDR(.xyram);
		_rxyram = . ;
		*(.xram .yram .xyram)
		. = ALIGN(16);
	} > xyram AT> rom

	_silram = SIZEOF(.ilram);
	_sxyram = SIZEOF(.xyram);

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

	ASSERT(_cgba_highbss_end <= 0x8c780000,
		"fx-CG100 gpSP high RAM arena exceeds conservative physical RAM bound")

	/DISCARD/ : {
		*(.gint.rodata.sh3 .gint.data.sh3 .gint.bss.sh3)
		*(.jcr)
		*(.eh_frame_hdr)
		*(.eh_frame)
		*(.comment)
	}
}
