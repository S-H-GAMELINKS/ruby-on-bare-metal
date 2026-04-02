/*
 * ruby_on_bare_metal_pthread.c - Minimal pthread stubs for single-threaded Ruby on Bare Metal
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

/* Opaque types - sized to be at least as large as musl's */
typedef unsigned long pthread_t;
typedef struct { char data[64]; } pthread_attr_t;
typedef struct { char data[48]; } pthread_mutex_t;
typedef struct { char data[48]; } pthread_cond_t;
typedef struct { char data[56]; } pthread_rwlock_t;
typedef struct { char data[8]; }  pthread_mutexattr_t;
typedef struct { char data[8]; }  pthread_condattr_t;
typedef struct { char data[8]; }  pthread_rwlockattr_t;

extern unsigned char stack64_bottom[];

/* Thread identity */
pthread_t pthread_self(void) { return 1; }
int pthread_equal(pthread_t t1, pthread_t t2) { return t1 == t2; }

/* Thread creation (not supported) */
int pthread_create(pthread_t *th, const pthread_attr_t *attr,
                   void *(*start)(void *), void *arg) {
    (void)th; (void)attr; (void)start; (void)arg;
    return ENOSYS;
}
int pthread_join(pthread_t th, void **retval) { (void)th; (void)retval; return 0; }
int pthread_detach(pthread_t th) { (void)th; return 0; }

/* Thread attributes */
int pthread_attr_init(pthread_attr_t *attr) { memset(attr, 0, sizeof(*attr)); return 0; }
int pthread_attr_destroy(pthread_attr_t *attr) { (void)attr; return 0; }
int pthread_attr_setdetachstate(pthread_attr_t *a, int s) { (void)a; (void)s; return 0; }
int pthread_attr_setstacksize(pthread_attr_t *a, size_t s) { (void)a; (void)s; return 0; }
int pthread_attr_setinheritsched(pthread_attr_t *a, int s) { (void)a; (void)s; return 0; }
int pthread_attr_getstack(const pthread_attr_t *attr, void **addr, size_t *size) {
    (void)attr;
    /* POSIX: addr is the lowest address of the stack area, size is total size */
    *addr = stack64_bottom;
    *size = 1048576; /* 1MB, matching entry64.S */
    return 0;
}
int pthread_attr_getguardsize(const pthread_attr_t *attr, size_t *size) {
    (void)attr; *size = 4096; return 0;
}
int pthread_getattr_np(pthread_t th, pthread_attr_t *attr) {
    (void)th; memset(attr, 0, sizeof(*attr)); return 0;
}

/* Mutex */
int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) {
    (void)a; memset(m, 0, sizeof(*m)); return 0;
}
int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutex_trylock(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutexattr_init(pthread_mutexattr_t *a) { memset(a, 0, sizeof(*a)); return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t *a) { (void)a; return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int t) { (void)a; (void)t; return 0; }

/* Condition variable */
int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) {
    (void)a; memset(c, 0, sizeof(*c)); return 0;
}
int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { (void)c; (void)m; return 0; }
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const void *t) {
    (void)c; (void)m; (void)t; return 0;
}
int pthread_cond_signal(pthread_cond_t *c) { (void)c; return 0; }
int pthread_cond_broadcast(pthread_cond_t *c) { (void)c; return 0; }
int pthread_condattr_init(pthread_condattr_t *a) { memset(a, 0, sizeof(*a)); return 0; }
int pthread_condattr_destroy(pthread_condattr_t *a) { (void)a; return 0; }
int pthread_condattr_setclock(pthread_condattr_t *a, int c) { (void)a; (void)c; return 0; }

/* Read-write lock */
int pthread_rwlock_init(pthread_rwlock_t *rw, const pthread_rwlockattr_t *a) {
    (void)a; memset(rw, 0, sizeof(*rw)); return 0;
}
int pthread_rwlock_destroy(pthread_rwlock_t *rw) { (void)rw; return 0; }
int pthread_rwlock_rdlock(pthread_rwlock_t *rw) { (void)rw; return 0; }
int pthread_rwlock_wrlock(pthread_rwlock_t *rw) { (void)rw; return 0; }
int pthread_rwlock_unlock(pthread_rwlock_t *rw) { (void)rw; return 0; }

/* Signal mask */
int pthread_sigmask(int how, const void *set, void *oldset) {
    (void)how; (void)set; (void)oldset; return 0;
}
int pthread_kill(pthread_t th, int sig) { (void)th; (void)sig; return 0; }

/* Misc */
int pthread_setname_np(pthread_t th, const char *name) { (void)th; (void)name; return 0; }
int pthread_atfork(void (*p)(void), void (*pa)(void), void (*c)(void)) {
    (void)p; (void)pa; (void)c; return 0;
}
int pthread_key_create(unsigned *key, void (*dtor)(void *)) {
    (void)dtor;
    static unsigned next_key = 1;
    *key = next_key++;
    return 0;
}
int pthread_setspecific(unsigned key, const void *val) { (void)key; (void)val; return 0; }
void *pthread_getspecific(unsigned key) { (void)key; return NULL; }

/* Cancellation */
int pthread_setcancelstate(int state, int *oldstate) {
    if (oldstate) *oldstate = 0;
    (void)state; return 0;
}
int pthread_setcanceltype(int type, int *oldtype) {
    if (oldtype) *oldtype = 0;
    (void)type; return 0;
}
void pthread_testcancel(void) {}
int __pthread_setcancelstate(int state, int *oldstate) {
    return pthread_setcancelstate(state, oldstate);
}

struct __ptcb { void (*f)(void *); void *x; struct __ptcb *next; };
void _pthread_cleanup_push(struct __ptcb *cb, void (*f)(void *), void *x) {
    (void)cb; (void)f; (void)x;
}
void _pthread_cleanup_pop(struct __ptcb *cb, int run) {
    if (run && cb && cb->f) cb->f(cb->x);
}
