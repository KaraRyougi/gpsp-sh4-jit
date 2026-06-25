OUTPUT_ARCH(sh4)
OUTPUT_FORMAT(elf32-sh)
ENTRY(_start)

MEMORY
{
  rom (rx)  : ORIGIN = 0x00300000, LENGTH = 2M
  ram (rw)  : ORIGIN = 0x08101400, LENGTH = 491K
}

SECTIONS
{
  .text :
  {
    KEEP(*(.pretext))
    KEEP(*(.ctors .ctors.*))
    KEEP(*(.dtors .dtors.*))
    *(.text .text.*)
    *(.rodata .rodata.*)
    *(.gcc_except_table .gcc_except_table.*)
  } > rom

  .data : ALIGN(4)
  {
    _sdata = .;
    *(.data .data.*)
    *(.sdata .sdata.*)
    . = ALIGN(4);
    _edata = .;
  } > ram AT > rom
  _datald = LOADADDR(.data);

  .bss (NOLOAD) : ALIGN(4)
  {
    _sbss = .;
    *(.bss .bss.*)
    *(.sbss .sbss.*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > ram

  __stack_top = ORIGIN(ram) + LENGTH(ram);
  __heap_start = ALIGN(_ebss, 8);
  __heap_end = __stack_top - 0x4000;
  ___heap_start = __heap_start;
  ___heap_end = __heap_end;

  PROVIDE(_emain = emain);

  ASSERT(__heap_start <= __heap_end, "fx-CG100 static RAM overflow")

  /DISCARD/ :
  {
    *(.comment)
    *(.note .note.*)
    *(.eh_frame .eh_frame.*)
  }
}
