#include "kernel.h"

void panic(const char *msg) {
  serial_puts("panic: ");
  serial_puts(msg);
  serial_puts("\n");
  for (;;) {
    __asm__ volatile ("cli; hlt");
  }
}
