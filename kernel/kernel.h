#pragma once

#include <stddef.h>
#include <stdint.h>

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);
void serial_write(const char *buf, size_t len);
int serial_data_ready(void);
int serial_getc(void);
size_t serial_read(char *buf, size_t len);

void panic(const char *msg);

void memory_init(void);
void *ruby_on_bare_metal_alloc(size_t size);
void ruby_on_bare_metal_free(void *ptr);

void timer_init(void);
long ruby_on_bare_metal_clock_millis(void);

int ruby_on_bare_metal_file_exists(const char *path);
const char *ruby_on_bare_metal_embedded_file_data(const char *path, size_t *size);
