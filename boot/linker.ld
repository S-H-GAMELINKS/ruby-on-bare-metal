ENTRY(_start)

SECTIONS
{
  . = 1M;

  .multiboot : {
    *(.multiboot)
  }

  .text : {
    *(.text.boot)
    *(.text*)
  }

  .rodata : {
    *(.rodata*)
  }

  .data : {
    *(.data*)
  }

  . = ALIGN(4096);
  .bss : {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;
  }

  . = 2M;
  .kernel64 : {
    *(.kernel64)
  }
}
