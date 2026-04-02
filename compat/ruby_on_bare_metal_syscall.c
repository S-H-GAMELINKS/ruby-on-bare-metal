/*
 * ruby_on_bare_metal_syscall.c - Linux syscall emulation layer for Ruby on Bare Metal
 *
 * musl libc calls ruby_on_bare_metal_syscall() instead of the syscall instruction.
 * We implement the minimum set needed to run CRuby's miniruby.
 */

#include "../kernel/kernel.h"
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>

/* ---- Internal state ---- */

/* mmap emulation: virtual address space allocator.
 * We have identity-mapped 512MB of RAM. We carve out the upper portion
 * for mmap allocations. The actual memory is already there (RAM). */
#define MMAP_BASE  0x10000000UL  /* 256MB mark */
#define MMAP_LIMIT 0x1F000000UL  /* up to ~496MB */
static size_t mmap_next = MMAP_BASE;

/* File descriptor table: 0=stdin, 1=stdout, 2=stderr */
#define RUBY_ON_BARE_METAL_MAX_FD 64
struct fd_entry {
    int used;
    const char *data;    /* pointer to embedded file data (NULL for stdio) */
    size_t size;         /* file size */
    size_t offset;       /* current read offset */
};
static struct fd_entry fd_table[RUBY_ON_BARE_METAL_MAX_FD] = {
    {1, NULL, 0, 0}, /* stdin */
    {1, NULL, 0, 0}, /* stdout */
    {1, NULL, 0, 0}, /* stderr */
};

static long ruby_on_bare_metal_log_unhandled(long n) {
    serial_puts("syscall: unhandled #");
    /* simple number printing */
    char buf[20];
    int i = 0;
    long num = n;
    if (num < 0) { serial_putc('-'); num = -num; }
    if (num == 0) buf[i++] = '0';
    while (num > 0 && i < 19) { buf[i++] = '0' + (num % 10); num /= 10; }
    for (int j = i - 1; j >= 0; j--) serial_putc(buf[j]);
    serial_putc('\n');
    return -ENOSYS;
}

/* ---- Syscall implementations ---- */

static long sys_read(int fd, void *buf, size_t count) {
    if (fd == 0) {
        /* stdin: read from serial */
        return (long)serial_read((char *)buf, count);
    }
    if (fd >= 3 && fd < RUBY_ON_BARE_METAL_MAX_FD && fd_table[fd].used && fd_table[fd].data) {
        /* Embedded file read */
        size_t remaining = fd_table[fd].size - fd_table[fd].offset;
        if (remaining == 0) return 0; /* EOF */
        size_t to_read = count < remaining ? count : remaining;
        __builtin_memcpy(buf, fd_table[fd].data + fd_table[fd].offset, to_read);
        fd_table[fd].offset += to_read;
        return (long)to_read;
    }
    return 0; /* EOF */
}

static long sys_write(int fd, const void *buf, size_t count) {
    if (fd == 1 || fd == 2) {
        serial_write((const char *)buf, count);
        return (long)count;
    }
    return -EBADF;
}

static long sys_writev(int fd, const void *iov_ptr, int iovcnt) {
    struct { const void *base; size_t len; } *iov = (void *)iov_ptr;
    long total = 0;
    for (int i = 0; i < iovcnt; i++) {
        long r = sys_write(fd, iov[i].base, iov[i].len);
        if (r < 0) return r;
        total += r;
    }
    return total;
}

static long sys_open(const char *path, int flags, int mode) {
    (void)flags; (void)mode;
    /* Check embedded files */
    size_t file_size = 0;
    const char *data = ruby_on_bare_metal_embedded_file_data(path, &file_size);
    if (data) {
        for (int i = 3; i < RUBY_ON_BARE_METAL_MAX_FD; i++) {
            if (!fd_table[i].used) {
                fd_table[i].used = 1;
                fd_table[i].data = data;
                fd_table[i].size = file_size;
                fd_table[i].offset = 0;
                return i;
            }
        }
        return -EMFILE;
    }
    return -ENOENT;
}

static long sys_close(int fd) {
    if (fd >= 0 && fd < RUBY_ON_BARE_METAL_MAX_FD) {
        fd_table[fd].used = 0;
        fd_table[fd].data = NULL;
        fd_table[fd].size = 0;
        fd_table[fd].offset = 0;
        return 0;
    }
    return -EBADF;
}

static long sys_fstat(int fd, void *buf) {
    if (fd < 0 || fd >= RUBY_ON_BARE_METAL_MAX_FD) return -EBADF;
    struct stat *st = (struct stat *)buf;
    __builtin_memset(st, 0, sizeof(*st));
    if (fd <= 2) {
        st->st_mode = 0020666; /* character device (tty) */
        st->st_rdev = 0x0501;
    } else if (fd_table[fd].data) {
        st->st_mode = 0100444; /* regular file */
        st->st_size = fd_table[fd].size;
    } else {
        st->st_mode = 0100444;
        st->st_size = 0;
    }
    return 0;
}

static long sys_stat(const char *path, void *buf) {
    struct stat *st = (struct stat *)buf;
    __builtin_memset(st, 0, sizeof(*st));
    if (ruby_on_bare_metal_file_exists(path)) {
        size_t size = 0;
        ruby_on_bare_metal_embedded_file_data(path, &size);
        st->st_mode = 0100444;
        st->st_size = size;
        return 0;
    }
    return -ENOENT;
}

static long sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    (void)addr; (void)flags; (void)fd; (void)offset;

    /* Align to page boundary */
    size_t aligned = (mmap_next + 4095) & ~((size_t)4095);
    if (aligned + length > MMAP_LIMIT) {
        return -ENOMEM;
    }
    void *ptr = (void *)aligned;
    /* Only zero-fill if readable/writable (skip PROT_NONE reservations) */
    if (prot & 0x3) { /* PROT_READ|PROT_WRITE */
        __builtin_memset(ptr, 0, length);
    }
    mmap_next = aligned + length;

    return (long)ptr;
}

static long sys_munmap(void *addr, size_t length) {
    (void)addr; (void)length;
    /* No-op: bump allocator can't free */
    return 0;
}

static long sys_mprotect(void *addr, size_t len, int prot) {
    (void)addr; (void)len; (void)prot;
    return 0; /* No-op: everything is RWX in ring 0 */
}

static long sys_madvise(void *addr, size_t length, int advice) {
    (void)addr; (void)length; (void)advice;
    return 0;
}

static long sys_mremap(void *old_addr, size_t old_size, size_t new_size, int flags) {
    (void)flags;
    /* Check for invalid old_addr (e.g., failed mmap result treated as address) */
    if ((unsigned long)old_addr > 0xfffffffffffff000UL) {
        /* old_addr is an error value, just allocate new memory */
        return sys_mmap(NULL, new_size, 3, 0, -1, 0);
    }
    if (new_size <= old_size) return (long)old_addr;
    /* Allocate new region and copy */
    long new_addr = sys_mmap(NULL, new_size, 3, 0, -1, 0);
    if (new_addr < 0) return new_addr;
    __builtin_memcpy((void *)new_addr, old_addr, old_size);
    return new_addr;
}

/* brk: always fail to force musl to use mmap path.
 * Return -1 (invalid address) so musl's expand_heap sees brk as unavailable. */
static long sys_brk(void *addr) {
    (void)addr;
    return -1;
}

static long sys_clock_gettime(int clk_id, struct timespec *tp) {
    (void)clk_id;
    if (tp) {
        long ms = ruby_on_bare_metal_clock_millis();
        tp->tv_sec = ms / 1000;
        tp->tv_nsec = (ms % 1000) * 1000000L;
    }
    return 0;
}

static long sys_clock_getres(int clk_id, struct timespec *res) {
    (void)clk_id;
    if (res) {
        res->tv_sec = 0;
        res->tv_nsec = 1000000; /* 1ms resolution */
    }
    return 0;
}

static long sys_gettimeofday(void *tv, void *tz) {
    (void)tz;
    if (tv) {
        struct { long tv_sec; long tv_usec; } *t = tv;
        long ms = ruby_on_bare_metal_clock_millis();
        t->tv_sec = ms / 1000;
        t->tv_usec = (ms % 1000) * 1000;
    }
    return 0;
}

static long sys_getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags;
    /* Simple pseudo-random: use clock + address mixing */
    unsigned char *p = (unsigned char *)buf;
    unsigned long seed = (unsigned long)ruby_on_bare_metal_clock_millis() ^ (unsigned long)buf;
    for (size_t i = 0; i < buflen; i++) {
        seed = seed * 6364136223846793005UL + 1442695040888963407UL;
        p[i] = (unsigned char)(seed >> 33);
    }
    return (long)buflen;
}

static long sys_dup(int oldfd) {
    if (oldfd < 0 || oldfd >= RUBY_ON_BARE_METAL_MAX_FD) return -EBADF;
    for (int i = 3; i < RUBY_ON_BARE_METAL_MAX_FD; i++) {
        if (!fd_table[i].used) {
            fd_table[i] = fd_table[oldfd];
            return i;
        }
    }
    return -EMFILE;
}

static long sys_dup2(int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= RUBY_ON_BARE_METAL_MAX_FD) return -EBADF;
    if (newfd < 0 || newfd >= RUBY_ON_BARE_METAL_MAX_FD) return -EBADF;
    fd_table[newfd] = fd_table[oldfd];
    return newfd;
}

static long sys_dup3(int oldfd, int newfd, int flags) {
    (void)flags;
    return sys_dup2(oldfd, newfd);
}

static long sys_fcntl(int fd, int cmd, long arg) {
    (void)fd; (void)arg;
    if (cmd == 1) return 0;   /* F_GETFD */
    if (cmd == 2) return 0;   /* F_SETFD */
    if (cmd == 3) return 0x02; /* F_GETFL -> O_RDWR */
    if (cmd == 4) return 0;   /* F_SETFL */
    return 0;
}

static long sys_ioctl(int fd, unsigned long request, long arg) {
    (void)fd;
    /* TIOCGWINSZ = 0x5413 */
    if (request == 0x5413) {
        struct { unsigned short rows, cols, xpixel, ypixel; } *ws = (void *)arg;
        if (ws) { ws->rows = 25; ws->cols = 80; ws->xpixel = 0; ws->ypixel = 0; }
        return 0;
    }
    /* TCGETS = 0x5401 - terminal attributes */
    if (request == 0x5401) return 0;
    return -ENOTTY;
}

static long sys_arch_prctl(int code, unsigned long addr) {
    /* ARCH_SET_FS = 0x1002, ARCH_SET_GS = 0x1001 */
    if (code == 0x1002) {
        /* Set FS base via MSR 0xC0000100 */
        uint32_t lo = (uint32_t)(addr & 0xFFFFFFFF);
        uint32_t hi = (uint32_t)(addr >> 32);
        __asm__ volatile ("wrmsr" : : "c"(0xC0000100), "a"(lo), "d"(hi));
        return 0;
    }
    if (code == 0x1001) {
        /* Set GS base via MSR 0xC0000101 */
        uint32_t lo = (uint32_t)(addr & 0xFFFFFFFF);
        uint32_t hi = (uint32_t)(addr >> 32);
        __asm__ volatile ("wrmsr" : : "c"(0xC0000101), "a"(lo), "d"(hi));
        return 0;
    }
    return -EINVAL;
}

static long sys_getpid(void) { return 1; }
static long sys_getuid(void) { return 0; }
static long sys_getgid(void) { return 0; }
static long sys_geteuid(void) { return 0; }
static long sys_getegid(void) { return 0; }

/* ---- Main dispatcher ---- */

static int syscall_trace = 0;

long ruby_on_bare_metal_syscall(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;

    if (syscall_trace) {
        serial_puts("sys:");
        char buf[20];
        int i = 0;
        long num = n;
        if (num < 0) { serial_putc('-'); num = -num; }
        if (num == 0) buf[i++] = '0';
        while (num > 0 && i < 19) { buf[i++] = '0' + (num % 10); num /= 10; }
        for (int j = i - 1; j >= 0; j--) serial_putc(buf[j]);
        serial_putc('\n');
    }

    switch (n) {
    case __NR_read:           return sys_read((int)a1, (void *)a2, (size_t)a3);
    case __NR_write:          return sys_write((int)a1, (const void *)a2, (size_t)a3);
    case __NR_open:           return sys_open((const char *)a1, (int)a2, (int)a3);
    case __NR_close:          return sys_close((int)a1);
    case __NR_stat:           return sys_stat((const char *)a1, (void *)a2);
    case __NR_fstat:          return sys_fstat((int)a1, (void *)a2);
    case __NR_lstat:          return sys_stat((const char *)a1, (void *)a2);
    case __NR_poll: {
        struct { int fd; short events; short revents; } *fds = (void *)a1;
        int nfds = (int)a2;
        for (int i = 0; i < nfds; i++) {
            fds[i].revents = 0;
            if (fds[i].fd == 0 && (fds[i].events & 1)) { /* POLLIN */
                if (serial_data_ready()) fds[i].revents |= 1;
            }
            if (fds[i].fd == 1 || fds[i].fd == 2) {
                if (fds[i].events & 4) fds[i].revents |= 4; /* POLLOUT */
            }
        }
        return 1;
    }
    case __NR_lseek:          return 0;
    case __NR_mmap:           return sys_mmap((void *)a1, (size_t)a2, (int)a3, (int)a4, (int)a5, (off_t)a6);
    case __NR_mprotect:       return sys_mprotect((void *)a1, (size_t)a2, (int)a3);
    case __NR_munmap:         return sys_munmap((void *)a1, (size_t)a2);
    case __NR_brk:            return sys_brk((void *)a1);
    case __NR_rt_sigaction:   return 0; /* stub */
    case __NR_rt_sigprocmask: return 0; /* stub */
    case 131: /* sigaltstack */ return 0;
    case __NR_ioctl:          return sys_ioctl((int)a1, (unsigned long)a2, a3);
    case __NR_pread64:        return 0;
    case __NR_pwrite64:       return sys_write((int)a1, (const void *)a2, (size_t)a3);
    case __NR_writev:         return sys_writev((int)a1, (const void *)a2, (int)a3);
    case __NR_access:         return ruby_on_bare_metal_file_exists((const char *)a1) ? 0 : -ENOENT;
    case __NR_pipe: {
        /* Create a dummy pipe using fd slots */
        int *fds = (int *)a1;
        int r = -1, w = -1;
        for (int i = 3; i < RUBY_ON_BARE_METAL_MAX_FD && (r < 0 || w < 0); i++) {
            if (!fd_table[i].used) {
                if (r < 0) { r = i; fd_table[i].used = 1; }
                else { w = i; fd_table[i].used = 1; }
            }
        }
        if (r < 0 || w < 0) return -EMFILE;
        fds[0] = r; fds[1] = w;
        return 0;
    }
    case __NR_pipe2: {
        int *fds = (int *)a1;
        int r = -1, w = -1;
        for (int i = 3; i < RUBY_ON_BARE_METAL_MAX_FD && (r < 0 || w < 0); i++) {
            if (!fd_table[i].used) {
                if (r < 0) { r = i; fd_table[i].used = 1; }
                else { w = i; fd_table[i].used = 1; }
            }
        }
        if (r < 0 || w < 0) return -EMFILE;
        fds[0] = r; fds[1] = w;
        return 0;
    }
    case __NR_sched_yield:    return 0;
    case __NR_mremap:         return sys_mremap((void *)a1, (size_t)a2, (size_t)a3, (int)a4);
    case __NR_madvise:        return sys_madvise((void *)a1, (size_t)a2, (int)a3);
    case __NR_dup:            return sys_dup((int)a1);
    case __NR_dup2:           return sys_dup2((int)a1, (int)a2);
    case __NR_dup3:           return sys_dup3((int)a1, (int)a2, (int)a3);
    case __NR_nanosleep:      return 0; /* instant return */
    case __NR_getpid:         return sys_getpid();
    case __NR_clone:          return -ENOSYS;
    case __NR_fork:           return -ENOSYS;
    case __NR_vfork:          return -ENOSYS;
    case __NR_execve:         return -ENOSYS;
    case __NR_exit:           serial_puts("exit called\n"); for (;;) { __asm__ volatile("hlt"); }
    case __NR_wait4:          return -ECHILD;
    case __NR_kill:           return 0;
    case __NR_fcntl:          return sys_fcntl((int)a1, (int)a2, a3);
    case __NR_ftruncate:      return 0;
    case __NR_getdents:       return 0;
    case __NR_getcwd:         { const char *cwd = "/"; size_t len = (size_t)a2; if (len >= 2) { __builtin_memcpy((void *)a1, cwd, 2); } return (long)a1; }
    case __NR_chdir:          return 0;
    case __NR_rename:         return -EROFS;
    case __NR_mkdir:          return -EROFS;
    case __NR_rmdir:          return -EROFS;
    case __NR_unlink:         return -EROFS;
    case __NR_symlink:        return -EROFS;
    case __NR_readlink:       return -EINVAL;
    case __NR_chmod:          return 0;
    case __NR_fchmod:         return 0;
    case __NR_chown:          return 0;
    case __NR_fchown:         return 0;
    case __NR_umask:          return 022;
    case __NR_gettimeofday:   return sys_gettimeofday((void *)a1, (void *)a2);
    case __NR_getrlimit: {
        struct { unsigned long cur; unsigned long max; } *rl = (void *)a2;
        if (rl) {
            rl->cur = 1048576; /* 1MB stack */
            rl->max = 1048576;
        }
        return 0;
    }
    case __NR_getrusage:      return -ENOSYS;
    case __NR_times:          return -ENOSYS;
    case __NR_getuid:         return sys_getuid();
    case __NR_getgid:         return sys_getgid();
    case __NR_geteuid:        return sys_geteuid();
    case __NR_getegid:        return sys_getegid();
    case __NR_getpgrp:        return 1;
    case __NR_getppid:        return 0;
    case __NR_setsid:         return 1;
    case __NR_setpgid:        return 0;
    case __NR_prctl:          return 0;
    case __NR_arch_prctl:     return sys_arch_prctl((int)a1, (unsigned long)a2);
    case __NR_futex:          return 0;
    case 186: /* gettid */    return 1;
    case __NR_set_tid_address: return sys_getpid();
    case __NR_set_robust_list: return 0;
    case __NR_clock_gettime:  return sys_clock_gettime((int)a1, (struct timespec *)a2);
    case __NR_clock_getres:   return sys_clock_getres((int)a1, (struct timespec *)a2);
    case __NR_exit_group:     serial_puts("exit_group called\n"); for (;;) { __asm__ volatile("hlt"); }
    case __NR_epoll_create1:  return -ENOSYS;
    case 270: /* pselect6 */ {
        /* a1=nfds, a2=readfds, a3=writefds, a4=exceptfds, a5=timeout, a6=sigmask */
        int nfds = (int)a1;
        unsigned long *readfds = (unsigned long *)a2;
        unsigned long *writefds = (unsigned long *)a3;
        int ready = 0;
        /* Check if stdin (fd 0) is in readfds and has data */
        if (readfds && nfds > 0) {
            if (readfds[0] & 1) { /* fd 0 */
                if (serial_data_ready()) {
                    readfds[0] = 1; /* only stdin ready */
                    ready++;
                } else {
                    readfds[0] = 0;
                }
            }
        }
        /* stdout/stderr always writable */
        if (writefds && nfds > 2) {
            writefds[0] &= 0x6; /* fds 1,2 */
            if (writefds[0]) ready++;
        }
        return ready;
    }
    case 271: /* ppoll */ {
        struct { int fd; short events; short revents; } *fds = (void *)a1;
        int nfds = (int)a2;
        int ready = 0;
        for (int i = 0; i < nfds; i++) {
            fds[i].revents = 0;
            if (fds[i].fd == 0 && (fds[i].events & 1)) {
                if (serial_data_ready()) { fds[i].revents |= 1; ready++; }
            }
            if ((fds[i].fd == 1 || fds[i].fd == 2) && (fds[i].events & 4)) {
                fds[i].revents |= 4; ready++;
            }
        }
        return ready;
    }
    case __NR_eventfd2:       return -ENOSYS;
    case __NR_openat:         return sys_open((const char *)a2, (int)a3, (int)a4);
    case __NR_newfstatat:     return sys_stat((const char *)a2, (void *)a3);
    case __NR_getrandom:      return sys_getrandom((void *)a1, (size_t)a2, (unsigned int)a3);
    case __NR_statx:          return -ENOSYS;
    default:                  return ruby_on_bare_metal_log_unhandled(n);
    }
}
