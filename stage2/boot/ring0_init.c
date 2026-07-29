/**
 * Chicago-95 Ring-0 Bare-Metal Initialization
 * CPUID feature detection, APIC setup, serial input, PS/2 keyboard, MSR access
 */

#include "boot/ring0_init.h"
#include "boot/security.h"

ring0_state_t ring0_state;

/* ======================================================================== */
/* Port I/O                                                                 */
/* ======================================================================== */

static inline void r0_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t r0_inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void r0_outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t r0_inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void r0_outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t r0_inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* ======================================================================== */
/* CPUID Detection                                                          */
/* ======================================================================== */

void ring0_cpuid_detect(ring0_cpu_info_t *info) {
    uint32_t eax, ebx, ecx, edx;

    info->cpu_features = 0;

    /* Vendor string */
    ring0_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    info->max_leaf = eax;
    *(uint32_t *)(info->vendor + 0) = ebx;
    *(uint32_t *)(info->vendor + 4) = edx;
    *(uint32_t *)(info->vendor + 8) = ecx;
    info->vendor[12] = 0;

    /* Feature detection - leaf 1 */
    ring0_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    info->apic_id = (ebx >> 24) & 0xFF;
    if (edx & (1 << 9))   info->cpu_features |= R0_CPUID_SSE;
    if (edx & (1 << 26))  info->cpu_features |= R0_CPUID_SSE2;
    if (ecx & (1 << 0))   info->cpu_features |= R0_CPUID_SSE3;
    if (ecx & (1 << 9))   info->cpu_features |= R0_CPUID_SSSE3;
    if (ecx & (1 << 19))  info->cpu_features |= R0_CPUID_SSE41;
    if (ecx & (1 << 20))  info->cpu_features |= R0_CPUID_SSE42;
    if (ecx & (1 << 25))  info->cpu_features |= R0_CPUID_AES_NI;
    if (ecx & (1 << 28))  info->cpu_features |= R0_CPUID_AVX;
    if (ecx & (1 << 1))   info->cpu_features |= R0_CPUID_PCLMULQDQ;
    if (ecx & (1 << 30))  info->cpu_features |= R0_CPUID_RDRAND;
    if (ecx & (1 << 27))  info->cpu_features |= R0_CPUID_OSXSAVE;

    /* AVX2 - leaf 7, subleaf 0 */
    ring0_cpuid(7, 0, &eax, &ebx, &ecx, &edx);
    if (ebx & (1 << 5))   info->cpu_features |= R0_CPUID_AVX2;
    if (ebx & (1 << 3))   info->cpu_features |= R0_CPUID_BMI1;
    if (ebx & (1 << 8))   info->cpu_features |= R0_CPUID_BMI2;
    if (ebx & (1 << 0))   info->cpu_features |= R0_CPUID_FSGSBASE;

    /* Extended features */
    ring0_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    if (ecx & (1 << 23))  info->cpu_features |= R0_CPUID_POPCNT;

    /* Extended leaf for RDSEED */
    ring0_cpuid(7, 0, &eax, &ebx, &ecx, &edx);
    if (ebx & (1 << 18))  info->cpu_features |= R0_CPUID_RDSEED;

    /* TSC frequency from leaf 0x15 (if available) */
    info->tsc_freq_mhz = 0;
    if (info->max_leaf >= 0x15) {
        ring0_cpuid(0x15, 0, &eax, &ebx, &ecx, &edx);
        if (eax && ecx) {
            info->tsc_freq_mhz = ecx / 1000000;
        }
    }

    /* Brand string (leaf 0x80000002-4) */
    ring0_cpuid(0x80000002, 0, (uint32_t *)(info->brand + 0),
                (uint32_t *)(info->brand + 4),
                (uint32_t *)(info->brand + 8),
                (uint32_t *)(info->brand + 12));
    ring0_cpuid(0x80000003, 0, (uint32_t *)(info->brand + 16),
                (uint32_t *)(info->brand + 20),
                (uint32_t *)(info->brand + 24),
                (uint32_t *)(info->brand + 28));
    ring0_cpuid(0x80000004, 0, (uint32_t *)(info->brand + 32),
                (uint32_t *)(info->brand + 36),
                (uint32_t *)(info->brand + 40),
                (uint32_t *)(info->brand + 44));
    info->brand[48] = 0;

    /* XCR0 (OS-level XSAVE state) */
    if (info->cpu_features & R0_CPUID_OSXSAVE) {
        uint32_t xcr0_lo, xcr0_hi;
        __asm__ volatile("xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));
        info->xcr0 = ((uint64_t)xcr0_hi << 32) | xcr0_lo;
    }
}

/* ======================================================================== */
/* MSR Access                                                               */
/* ======================================================================== */

uint64_t ring0_msr_read(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

void ring0_msr_write(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

/* ======================================================================== */
/* APIC                                                                     */
/* ======================================================================== */

#define APIC_BASE_MSR       0x1B
#define APIC_BASE_MASK      0xFFFFF000
#define APIC_REG_APICID     0x020
#define APIC_REG_APICVER    0x030
#define APIC_REG_TPR        0x080
#define APIC_REG_EOI        0x0B0
#define APIC_REG_LVT_TIMER  0x320
#define APIC_REG_LVT_LINT0  0x350
#define APIC_REG_LVT_LINT1  0x360
#define APIC_REG_SPURIOUS   0x0F0
#define APIC_REG_ICR_LOW    0x300

static volatile uint32_t *apic_base = (volatile uint32_t *)0xFEE00000;

static inline uint32_t apic_read(uint32_t reg) {
    return apic_base[reg >> 2];
}

static inline void apic_write(uint32_t reg, uint32_t val) {
    apic_base[reg >> 2] = val;
}

int ring0_apic_init(void) {
    /* Read APIC base from MSR */
    uint64_t base = ring0_msr_read(APIC_BASE_MSR);
    uint32_t apic_phys = (uint32_t)(base & APIC_BASE_MASK);

    /* Map LAPIC base (identity-mapped in early boot) */
    apic_base = (volatile uint32_t *)(uint64_t)apic_phys;

    /* Enable APIC via spurious vector register (bit 8) */
    uint32_t svr = apic_read(APIC_REG_SPURIOUS);
    apic_write(APIC_REG_SPURIOUS, svr | 0x100);

    /* Mask all LVT entries initially */
    apic_write(APIC_REG_LVT_TIMER, 0x10000);
    apic_write(APIC_REG_LVT_LINT0, 0x10000);
    apic_write(APIC_REG_LVT_LINT1, 0x10000);

    /* Set task priority to 0 to accept all interrupts */
    apic_write(APIC_REG_TPR, 0);

    ring0_state.apic_ready = 1;
    return 0;
}

void ring0_apic_eoi(void) {
    apic_write(APIC_REG_EOI, 0);
}

uint32_t ring0_apic_id(void) {
    return apic_read(APIC_REG_APICID) >> 24;
}

/* ======================================================================== */
/* Serial Console Input (COM1)                                              */
/* ======================================================================== */

#define COM1_PORT 0x3F8

void ring0_serial_init(void) {
    /* Disable interrupts */
    r0_outb(COM1_PORT + 1, 0x00);
    /* Enable DLAB, set baud rate divisor=3 (9600 baud) */
    r0_outb(COM1_PORT + 3, 0x80);
    r0_outb(COM1_PORT + 0, 0x03);
    r0_outb(COM1_PORT + 1, 0x00);
    /* 8 bits, no parity, one stop bit */
    r0_outb(COM1_PORT + 3, 0x03);
    /* Enable FIFO, clear, 14-byte threshold */
    r0_outb(COM1_PORT + 2, 0xC7);
    /* IRQs enabled, RTS/DSR set */
    r0_outb(COM1_PORT + 4, 0x0B);
    /* Enable interrupts: data available (bit 0) */
    r0_outb(COM1_PORT + 1, 0x01);

    ring0_state.serial_port = COM1_PORT;
    ring0_state.serial_ready = 1;
}

int serial_rx_ready(void) {
    if (!ring0_state.serial_ready) return 0;
    return (r0_inb(COM1_PORT + 5) & 0x01) != 0;
}

int serial_getc(void) {
    while (!serial_rx_ready()) { __asm__ volatile("hlt"); }
    return r0_inb(COM1_PORT);
}

void serial_putc(char c) {
    while ((r0_inb(COM1_PORT + 5) & 0x20) == 0);
    r0_outb(COM1_PORT, (uint8_t)c);
}

int serial_readline(char *buf, uint32_t maxlen) {
    uint32_t i = 0;
    while (i < maxlen - 1) {
        int c = serial_getc();
        if (c == '\r' || c == '\n') {
            serial_putc('\r');
            serial_putc('\n');
            break;
        }
        if (c == 0x08 || c == 0x7F) { /* backspace */
            if (i > 0) {
                i--;
                serial_putc('\b');
                serial_putc(' ');
                serial_putc('\b');
            }
            continue;
        }
        if (c >= 0x20 && c < 0x7F) {
            buf[i++] = (char)c;
            serial_putc((char)c);
        }
    }
    buf[i] = 0;
    return (int)i;
}

/* ======================================================================== */
/* PS/2 Keyboard Input                                                      */
/* ======================================================================== */

#define KBD_DATA_PORT   0x60
#define KBD_STATUS_PORT 0x64
#define KBD_BUFFER_SIZE 64

static uint8_t  kbd_buffer[KBD_BUFFER_SIZE];
static uint32_t kbd_head = 0;
static uint32_t kbd_tail = 0;
static uint8_t  kbd_shift = 0;
static uint8_t  kbd_ctrl = 0;
static uint8_t  kbd_alt = 0;

/* US QWERTY scancode set 1 -> ASCII */
static const char scancode_to_ascii[128] = {
    0, 0x1B, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',
    0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char scancode_shift[128] = {
    0, 0x1B, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',
    0,'*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static void kbd_push(uint8_t c) {
    uint32_t next = (kbd_head + 1) % KBD_BUFFER_SIZE;
    if (next != kbd_tail) {
        kbd_buffer[kbd_head] = c;
        kbd_head = next;
    }
}

int kbd_init(void) {
    kbd_head = 0;
    kbd_tail = 0;
    kbd_shift = 0;
    kbd_ctrl = 0;
    kbd_alt = 0;

    /* Wait for keyboard controller ready */
    int timeout = 100000;
    while ((r0_inb(KBD_STATUS_PORT) & 0x02) && timeout-- > 0);
    /* Enable keyboard scanning */
    r0_outb(KBD_STATUS_PORT, 0xAE);
    r0_outb(KBD_STATUS_PORT, 0xA8);

    return 0;
}

int kbd_get_scancode(void) {
    if (kbd_tail == kbd_head) return -1;
    uint8_t sc = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    return (int)sc;
}

void kbd_poll(void) {
    /* Read all pending scancodes from keyboard controller */
    while (r0_inb(KBD_STATUS_PORT) & 0x01) {
        uint8_t sc = r0_inb(KBD_DATA_PORT);

        /* Modifier key handling */
        if (sc == 0x2A || sc == 0x36) { kbd_shift = 1; continue; }
        if (sc == 0xAA || sc == 0xB6) { kbd_shift = 0; continue; }
        if (sc == 0x1D) { kbd_ctrl = 1; continue; }
        if (sc == 0x9D) { kbd_ctrl = 0; continue; }
        if (sc == 0x38) { kbd_alt = 1; continue; }
        if (sc == 0xB8) { kbd_alt = 0; continue; }

        /* Ignore key releases (bit 7 set) */
        if (sc & 0x80) continue;

        /* Convert scancode to ASCII */
        char c;
        if (kbd_shift) {
            c = scancode_shift[sc];
        } else {
            c = scancode_to_ascii[sc];
        }

        /* Ctrl+key combos */
        if (kbd_ctrl && c >= 'a' && c <= 'z') {
            c = c - 'a' + 1; /* Ctrl+A=0x01, Ctrl+C=0x03, etc. */
        }

        if (c) kbd_push((uint8_t)c);
    }
}

int kbd_getchar(void) {
    kbd_poll();
    if (kbd_tail == kbd_head) return -1;
    uint8_t c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    return (int)c;
}

int kbd_available(void) {
    kbd_poll();
    return kbd_tail != kbd_head;
}

int kbd_readline(char *buf, uint32_t maxlen) {
    uint32_t i = 0;
    while (i < maxlen - 1) {
        kbd_poll();
        if (kbd_tail == kbd_head) {
            __asm__ volatile("hlt");
            continue;
        }
        int c = kbd_getchar();
        if (c < 0) continue;

        if (c == '\n' || c == '\r') {
            kbd_push('\n');
            break;
        }
        if (c == '\b' || c == 0x7F) {
            if (i > 0) {
                i--;
            }
            continue;
        }
        if (c == 3) { /* Ctrl+C */
            buf[0] = 0;
            return -1;
        }
        if (c >= 0x20 && c < 0x7F) {
            buf[i++] = (char)c;
        }
    }
    buf[i] = 0;
    return (int)i;
}

/* ======================================================================== */
/* Timer/Delay                                                              */
/* ======================================================================== */

void ring0_delay_ms(uint32_t ms) {
    uint64_t target = ring0_rdtsc() + ((uint64_t)ms * ring0_state.tsc_per_ms);
    while (ring0_rdtsc() < target) {
        __asm__ volatile("hlt");
    }
}

uint64_t ring0_ticks(void) {
    return ring0_rdtsc();
}

/* ======================================================================== */
/* Main Ring-0 Init                                                         */
/* ======================================================================== */

int ring0_init(uint32_t tsc_freq_hz) {
    /* Zero state */
    uint8_t *dst = (uint8_t *)&ring0_state;
    for (uint32_t i = 0; i < sizeof(ring0_state_t); i++) dst[i] = 0;

    /* TSC frequency */
    if (tsc_freq_hz == 0) tsc_freq_hz = 2000000000U;
    ring0_state.tsc_per_ms = tsc_freq_hz / 1000;
    ring0_state.cpu.tsc_freq_mhz = tsc_freq_hz / 1000000;

    /* CPUID feature detection */
    ring0_cpuid_detect(&ring0_state.cpu);

    /* Serial input init */
    ring0_serial_init();

    /* PS/2 keyboard init */
    kbd_init();

    /* APIC init */
    ring0_apic_init();

    ring0_state.ring0_ready = 1;
    return 0;
}
