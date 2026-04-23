#pragma once

#ifdef RUBY_ON_BARE_METAL_UEFI
#define serial_init       uefi_console_init
#define serial_putc       uefi_console_putc
#define serial_puts       uefi_console_puts
#define serial_write      uefi_console_write
#define serial_getc       uefi_console_getc
#define serial_read       uefi_console_read
#define serial_data_ready uefi_console_data_ready
#endif
