#include "kernel.h"

#define HEAP_SIZE (1024 * 1024)
static unsigned char heap[HEAP_SIZE];
static size_t heap_offset;

void memory_init(void) {
  heap_offset = 0;
}

void *ruby_on_bare_metal_alloc(size_t size) {
  size = (size + 7) & ~((size_t)7);
  if (heap_offset + size > HEAP_SIZE) {
    panic("out of heap");
  }
  void *ptr = &heap[heap_offset];
  heap_offset += size;
  return ptr;
}

void ruby_on_bare_metal_free(void *ptr) {
  (void)ptr;
}
