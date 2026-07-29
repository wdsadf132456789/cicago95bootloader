/**
 * Chicago-95 Ring-0 Bare-Metal Initialization
 * CPU feature detection, APIC setup, serial input, TSC, MSR configuration
 */

#ifndef RING0_INIT_H
#define RING0_INIT_H

#include <stdint.h>

#define R0_CPUID_SSE        (1 << 0)
#define R0_CPUID_SSE2       (1 << 1)
#define R0_CPUID_SSE3       (1 << 2)
#define R0_CPUID_SSSE3      (1 << 3)
#define R0_CPUID_SSE41      (1 << 4)
#define R0_CPUID_SSE42      (1 << 5)
#define R0_CPUID_AES_NI     (1 << 6)
#define R0_CPUID_AVX        (1 << 7)
#define R0_CPUID_AVX2       (1 << 8)
#define R0_CPUID_PCLMULQDQ  (1 << 9)
#define R0_CPUID_RDRAND     (1 << 10)
#define R0_CPUID_RDSEED     (1 << 11)
#define R0_CPUID_POPCNT     (1 << 12)
#define R0_CPUID_BMI1       (1 << 13)
#define R0_CPUID_BMI2       (1 << 14)
#define R0_CPUID_OSXSAVE    (1 << 15)
#define R0_CPUID_FSGSBASE   (1 << 16)

typedef struct {
    uint32_t cpu_features;
    uint32_t apic_id;
    uint32_t tsc_freq_mhz;
    uint64_t xcr0;
    uint8_t  has_msr;
    uint8_t  has_apic;
    uint8_t  has_x2apic;
    uint32_t max_leaf;
    char     vendor[13];
    char     brand[49];
} ring0_cpu_info_t;

typedef struct {
    ring0_cpu_info_t cpu;
    uint64_t         tsc_per_ms;
    uint32_t         serial_port;
    uint8_t          serial_ready;
    uint8_t          apic_ready;
    uint8_t          ring0_ready;
} ring0_state_t;

/* Initialize the complete ring-0 bare-metal environment */
int ring0_init(uint32_t tsc_freq_hz);

/* CPU feature detection */
void ring0_cpuid_detect(ring0_cpu_info_t *info);

/* APIC initialization (local APIC base detection + enable) */
int  ring0_apic_init(void);
void ring0_apic_eoi(void);
uint32_t ring0_apic_id(void);

/* MSR access */
uint64_t ring0_msr_read(uint32_t msr);
void     ring0_msr_write(uint32_t msr, uint64_t value);

/* Serial console with input */
int  serial_rx_ready(void);
int  serial_getc(void);
int  serial_readline(char *buf, uint32_t maxlen);

/* PS/2 keyboard input */
int  kbd_init(void);
int  kbd_get_scancode(void);
int  kbd_getchar(void);
int  kbd_readline(char *buf, uint32_t maxlen);
int  kbd_available(void);

/* Timer/delay */
void ring0_delay_ms(uint32_t ms);
uint64_t ring0_ticks(void);

/* CPU identification helpers */
static inline void ring0_cpuid(uint32_t leaf, uint32_t sub, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(leaf), "c"(sub));
}

static inline uint64_t ring0_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

extern ring0_state_t ring0_state;

#endif
