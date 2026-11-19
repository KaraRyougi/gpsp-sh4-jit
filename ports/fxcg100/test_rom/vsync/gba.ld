OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY
{
	rom   (rx)  : ORIGIN = 0x08000000, LENGTH = 32M
	iwram (rwx) : ORIGIN = 0x03000000, LENGTH = 32K
}

SECTIONS
{
	.text : {
		KEEP(*(.crt0))
		*(.text .text.*)
		*(.glue_7 .glue_7t .vfp11_veneer .v4_bx)
		*(.rodata .rodata.*)
		. = ALIGN(4);
	} > rom

	.data : {
		__data_start = .;
		*(.data .data.*)
		. = ALIGN(4);
		__data_end = .;
	} > iwram AT > rom
	__data_lma = LOADADDR(.data);

	.bss (NOLOAD) : {
		__bss_start = .;
		*(.bss .bss.* COMMON)
		. = ALIGN(4);
		__bss_end = .;
	} > iwram

	/DISCARD/ : { *(.ARM.exidx* .ARM.extab* .comment .note*) }
}
