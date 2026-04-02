#include "kernel.h"

/*
 * Simple tick counter using x86 RDTSC.
 * We estimate TSC frequency at boot time using PIT channel 2.
 */

static uint64_t tsc_freq_khz;  /* TSC ticks per millisecond */
static uint64_t boot_tsc;

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void timer_init(void) {
    /* Calibrate TSC using PIT channel 2 (speaker gate) */
    /* PIT frequency = 1193182 Hz, we measure ~50ms */
    uint16_t pit_count = 59659; /* ~50ms at 1193182 Hz */

    /* Set PIT channel 2 to one-shot mode */
    outb(0x61, (inb(0x61) & 0xFD) | 0x01); /* gate on, speaker off */
    outb(0x43, 0xB0); /* channel 2, lobyte/hibyte, mode 0 */
    outb(0x42, (uint8_t)(pit_count & 0xFF));
    outb(0x42, (uint8_t)(pit_count >> 8));

    uint64_t start_tsc = rdtsc();

    /* Wait for PIT to count down */
    while (!(inb(0x61) & 0x20))
        ;

    uint64_t end_tsc = rdtsc();
    uint64_t elapsed = end_tsc - start_tsc;

    /* TSC ticks in ~50ms -> ticks per ms */
    tsc_freq_khz = elapsed / 50;
    if (tsc_freq_khz == 0) tsc_freq_khz = 1; /* safety */

    boot_tsc = rdtsc();
}

long ruby_on_bare_metal_clock_millis(void) {
    if (tsc_freq_khz == 0) return 0;
    uint64_t now = rdtsc();
    return (long)((now - boot_tsc) / tsc_freq_khz);
}
