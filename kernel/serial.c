#include "kernel.h"

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t value) {
  __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
  uint8_t ret;
  __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

void serial_init(void) {
  outb(COM1 + 1, 0x00);
  outb(COM1 + 3, 0x80);
  outb(COM1 + 0, 0x03);
  outb(COM1 + 1, 0x00);
  outb(COM1 + 3, 0x03);
  outb(COM1 + 2, 0xC7);
  outb(COM1 + 4, 0x0B);
}

void serial_putc(char c) {
  if (c == '\n') outb(COM1, '\r');
  outb(COM1, (uint8_t)c);
}

void serial_puts(const char *s) {
  while (*s) serial_putc(*s++);
}

void serial_write(const char *buf, size_t len) {
  for (size_t i = 0; i < len; i++) serial_putc(buf[i]);
}

int serial_data_ready(void) {
  return inb(COM1 + 5) & 0x01;
}

int serial_getc(void) {
  while (!serial_data_ready())
    __asm__ volatile ("pause");
  return inb(COM1);
}

/* Read up to `len` bytes from serial. Blocks for first byte,
 * then returns immediately if no more data ready. */
size_t serial_read(char *buf, size_t len) {
  if (len == 0) return 0;
  buf[0] = (char)serial_getc();
  size_t i = 1;
  while (i < len && serial_data_ready()) {
    buf[i++] = (char)inb(COM1);
  }
  return i;
}
