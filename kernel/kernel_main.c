#include "kernel.h"
#include <stddef.h>

void ruby_on_bare_metal_cruby_demo(void);

/* Override musl's __vdsosym to disable vDSO */
void *__vdsosym(const char *vername, const char *name) {
    (void)vername; (void)name;
    return 0;
}

/* TLS setup for musl libc.
 * On x86_64, TLS variables live BEFORE the thread pointer (TP).
 * FS register points to TP. TLS vars accessed as FS:negative_offset.
 * musl's pthread struct is at TP, with self-pointer as first field.
 */
static unsigned char tls_mem[8192] __attribute__((aligned(4096)));

static void init_musl_tls(void) {
    /* TP goes at midpoint, leaving room for TLS data before it.
     * On x86_64, TLS vars are at negative offsets from TP.
     * .tbss is 28 bytes. We place TP at +4096 for plenty of room. */
    void *tp = tls_mem + 4096;
    void **self = (void **)tp;
    *self = tp;  /* musl pthread self-pointer */

    /* Set FS base directly via MSR (FSBASE = 0xC0000100) */
    uint64_t addr = (uint64_t)tp;
    uint32_t lo = (uint32_t)(addr & 0xFFFFFFFF);
    uint32_t hi = (uint32_t)(addr >> 32);
    __asm__ volatile ("wrmsr" : : "c"(0xC0000100), "a"(lo), "d"(hi));
}

void kernel_main(void) {
  serial_init();
  serial_puts("boot ok\n");

  timer_init();
  serial_puts("timer ok\n");

  init_musl_tls();
  serial_puts("tls ok\n");

#ifndef RUBY_ON_BARE_METAL_UEFI
  memory_init();
  serial_puts("memory ok\n");
#endif

  if (ruby_on_bare_metal_file_exists("/hello.rb")) {
    serial_puts("embedded script ok\n");
  } else {
    panic("embedded script missing");
  }

  serial_puts("cruby init start\n");
  ruby_on_bare_metal_cruby_demo();
  serial_puts("cruby init ok\n");

  for (;;) {
    __asm__ volatile ("hlt");
  }
}
