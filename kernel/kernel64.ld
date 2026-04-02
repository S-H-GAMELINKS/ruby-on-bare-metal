ENTRY(_start64)

SECTIONS
{
  . = 0x200000;

  .text : {
    *(.text.entry)
    *(.text*)
  }

  .rodata : {
    *(.rodata*)
  }

  .data : {
    *(.data*)
  }

  .bss : {
    . = ALIGN(8);
    __bss64_start = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(8);
    __bss64_end = .;
  }
}
