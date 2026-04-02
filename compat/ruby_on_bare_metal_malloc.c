/*
 * ruby_on_bare_metal_malloc.c - Complete memory allocation override for Ruby on Bare Metal
 *
 * Overrides ALL of musl's memory-related functions to prevent musl
 * from using its mmap-based internal malloc.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

/* ---- Bump allocator with 128MB heap ---- */

#define HEAP_SIZE (128UL * 1024 * 1024)
static unsigned char heap[HEAP_SIZE] __attribute__((aligned(4096)));
static size_t heap_used = 0;

struct block_hdr {
    size_t size;
    size_t magic; /* 0xA110CA7E = allocated */
};

#define HDR_SIZE   sizeof(struct block_hdr)
#define ALIGN(x,a) (((x) + (a) - 1) & ~((size_t)(a) - 1))
#define MAGIC      0xA110CA7EULL

static void *bump_alloc(size_t size, size_t alignment) {
    if (size == 0) size = 1;
    if (alignment < 16) alignment = 16;

    size_t hdr_space = HDR_SIZE;
    /* Ensure returned pointer is aligned */
    size_t start = ALIGN(heap_used + hdr_space, alignment);
    size_t hdr_pos = start - hdr_space;
    size_t end = start + ALIGN(size, 16);

    if (end > HEAP_SIZE) {
        return NULL;
    }

    struct block_hdr *hdr = (struct block_hdr *)(heap + hdr_pos);
    hdr->size = ALIGN(size, 16);
    hdr->magic = MAGIC;
    heap_used = end;

    void *ret = (void *)(heap + start);
    return ret;
}

/* ---- Standard malloc API ---- */

void *malloc(size_t size) {
    return bump_alloc(size, 16);
}

void free(void *ptr) {
    /* Bump allocator: no-op free (CRuby GC handles object reuse) */
    (void)ptr;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    if (nmemb && total / nmemb != size) return NULL; /* overflow */
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }

    struct block_hdr *hdr = (struct block_hdr *)ptr - 1;
    size_t old_size = (hdr->magic == MAGIC) ? hdr->size : 0;

    if (size <= old_size) return ptr;

    void *new_ptr = malloc(size);
    if (new_ptr && old_size > 0) {
        memcpy(new_ptr, ptr, old_size);
    }
    return new_ptr;
}

/* ---- Aligned allocation ---- */

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    void *p = bump_alloc(size, alignment);
    if (!p) return ENOMEM;
    *memptr = p;
    return 0;
}

void *memalign(size_t alignment, size_t size) {
    return bump_alloc(size, alignment);
}

void *aligned_alloc(size_t alignment, size_t size) {
    return bump_alloc(size, alignment);
}

/* ---- Query functions ---- */

size_t malloc_usable_size(void *ptr) {
    if (!ptr) return 0;
    struct block_hdr *hdr = (struct block_hdr *)ptr - 1;
    if (hdr->magic == MAGIC) return hdr->size;
    return 0;
}

int malloc_trim(size_t pad) {
    (void)pad;
    return 0;
}

/* ---- mmap/munmap C-level overrides ---- */
/* These override musl's mmap wrapper functions, preventing musl's
 * internal malloc from ever issuing mmap syscalls. */

extern long ruby_on_bare_metal_syscall(long n, long a1, long a2, long a3, long a4, long a5, long a6);

void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset) {
    /* Route directly to our syscall handler */
    long ret = ruby_on_bare_metal_syscall(9, (long)addr, (long)length, (long)prot,
                              (long)flags, (long)fd, offset);
    if (ret < 0 && ret > -4096L) {
        errno = (int)(-ret);
        return (void *)-1L; /* MAP_FAILED */
    }
    return (void *)ret;
}

int munmap(void *addr, size_t length) {
    (void)addr; (void)length;
    return 0;
}

int mprotect(void *addr, size_t len, int prot) {
    (void)addr; (void)len; (void)prot;
    return 0;
}

void *mremap(void *old_addr, size_t old_size, size_t new_size, int flags, ...) {
    (void)flags;
    if ((unsigned long)old_addr > 0xfffffffffffff000UL) {
        /* Invalid address (error from previous failed mmap) */
        return mmap(NULL, new_size, 3, 0x22, -1, 0);
    }
    if (new_size <= old_size) return old_addr;
    void *new_addr = mmap(NULL, new_size, 3, 0x22, -1, 0);
    if (new_addr == (void *)-1L) return (void *)-1L;
    memcpy(new_addr, old_addr, old_size);
    return new_addr;
}

int madvise(void *addr, size_t length, int advice) {
    (void)addr; (void)length; (void)advice;
    return 0;
}

/* ---- musl internal overrides ---- */
/* These names are used by musl's internal code */

void *__mmap(void *addr, size_t length, int prot, int flags, int fd, long offset) {
    return mmap(addr, length, prot, flags, fd, offset);
}

int __munmap(void *addr, size_t length) {
    return munmap(addr, length);
}

int __mprotect(void *addr, size_t len, int prot) {
    return mprotect(addr, len, prot);
}

void *__mremap(void *old_addr, size_t old_size, size_t new_size, int flags, ...) {
    return mremap(old_addr, old_size, new_size, flags);
}

int __madvise(void *addr, size_t length, int advice) {
    return madvise(addr, length, advice);
}

/* Override musl's heap expander - return memory from bump allocator */
void *__expand_heap(size_t *pn) {
    size_t n = *pn;
    void *p = bump_alloc(n, 4096);
    if (!p) {
        errno = ENOMEM;
        return 0;
    }
    return p;
}

/* Custom memcpy/memset that catches bad addresses */
/* These are removed from musl's libc_ruby_on_bare_metal.a, so we provide them */

/* musl's __libc_malloc aliases */
void *__libc_malloc(size_t size) { return malloc(size); }
void *__libc_malloc_impl(size_t size) { return malloc(size); }

/* pthread stubs are in ruby_on_bare_metal_pthread.c */
void __libc_free(void *ptr) { free(ptr); }
void *__libc_calloc(size_t n, size_t s) { return calloc(n, s); }
void *__libc_realloc(void *p, size_t s) { return realloc(p, s); }
