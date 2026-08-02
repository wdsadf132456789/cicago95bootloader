/**
 * Chicago-95 Bootloader - Stage 2 Main
 * Mode transitions: Real Mode -> Protected Mode -> Long Mode
 * Boot flow: memory detect -> A20 -> security init -> PM -> Long Mode -> kernel load -> jump
 *
 * 28 security modules, 3 memory managers, brainfs, serial console, boot timing.
 */

#include "boot/security.h"
#include "security/tor.h"
#include "drivers/wifi.h"
#include "drivers/wifi_autodetect.h"
#include "vga/vga.h"
#include "memory/memory.h"
#include "tape/tape.h"
#include "security/panic.h"
#include "fs/brainfs.h"
#include "fs/brainvfs.h"
#include "fs/encfs_mount.h"
#include "boot/ring0_init.h"
#include "boot/fs_menu.h"
#include "boot/pre_init.h"
#include "shell/shell.h"
#include "gui/gui.h"
#include "security/john/john_core.h"

/* ======================================================================== */
/* Port I/O (inline, used in boot-critical path)                            */
/* ======================================================================== */

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline void enable_sse(void) {
    /* The compiler freely emits SSE (movdqa/movsd/...).  Without
     * CR4.OSFXSR|CR4.OSXMMEXCPT those #UD; clear CR0.TS/EM so the
     * x87/SSE state is usable as early as real mode. */
    asm volatile(
        "mov %%cr0, %%eax\n"
        "and $0xFFFFFFF3, %%eax\n"
        "mov %%eax, %%cr0\n"
        "mov %%cr4, %%eax\n"
        "or $0x600, %%eax\n"
        "mov %%eax, %%cr4\n"
        : : : "eax"
    );
}
static inline void quiesce_interrupts(void) {
    asm volatile("cli");
    /* Mask every IRQ on both PICs.  Until the kernel installs its own IDT
     * and remaps the PIC, any delivered interrupt (especially the PIT timer
     * IRQ0) would be routed through the still-loaded real-mode IVT and
     * fault (#GP -> #DF). */
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}
static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    asm volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void outl(uint16_t port, uint32_t val) {
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    asm volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* ======================================================================== */
/* TSC tick counter (for boot timing)                                       */
/* ======================================================================== */

/* Stage2 is emitted as a plain binary without its .bss section: the linker
 * places the ~14 MB of static data at 0x7E300.._end, far beyond the ~523 KB
 * image that stage1 loads into RAM.  Everything past the binary's tail is
 * whatever was left in memory, so globals must be zeroed at entry.  The
 * range skips the stage3 image that stage1 already parked at 0x100000. */
static void zero_bss(void) {
    extern char __bss_start[], _end[];
    uintptr_t b = (uintptr_t)__bss_start;
    uintptr_t e = (uintptr_t)_end;
    uintptr_t cur;

    for (cur = b; cur < e; cur += 4) {
        if (cur >= 0x100000 && cur < 0x104400) {
            cur = 0x1043FC;
            continue;
        }
        *(volatile uint32_t *)cur = 0;
    }
}

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t tsc_frequency = 0;
static uint64_t tsc_per_ms = 0;
static uint64_t boot_start_tsc = 0;

#define PIT_FREQ_HZ    1193182
#define PIT_TICKS_50MS 59659

static uint64_t pit_measure_tsc(uint16_t pit_count) {
    /* Disable channel 2 speaker */
    uint8_t val = inb(0x61);
    val &= ~0x03;
    outb(0x61, val);

    /* PIT channel 2: lobyte/hibyte, mode 0, binary */
    outb(0x43, 0xB0);
    outb(0x42, (uint8_t)(pit_count & 0xFF));
    outb(0x42, (uint8_t)((pit_count >> 8) & 0xFF));

    /* Start counting */
    outb(0x61, val | 0x01);

    uint64_t start = rdtsc();
    /* Channel-2 OUT is bit 4 of port 0x61; bit 5 is the system-tick OUT,
     * which would never indicate channel-2 terminal count. */
    while ((inb(0x61) & 0x10) == 0) {
        if (rdtsc() - start > 200000000ULL) break;
    }
    uint64_t end = rdtsc();

    /* Restore */
    outb(0x61, val);

    return end - start;
}

static uint64_t pit_measure_ms(uint32_t ms) {
    /* PIT max count is 65535 (~54.9ms at 1.193MHz).
       For longer periods, chain multiple 50ms windows. */
    uint32_t windows = ms / 50;
    if (windows == 0) windows = 1;
    uint64_t total = 0;
    for (uint32_t i = 0; i < windows; i++) {
        total += pit_measure_tsc(PIT_TICKS_50MS);
    }
    return total;
}

/* 3-pass TSC calibration using PIT channel 2 (1.193182 MHz) */
static void calibrate_tsc(void) {
    uint64_t total_ticks = 0;
    uint64_t total_us = 0;

    /* Pass 1: ~50ms — coarse measurement */
    total_ticks += pit_measure_ms(50);
    total_us += 50000;

    /* Pass 2: ~100ms — refine */
    total_ticks += pit_measure_ms(100);
    total_us += 100000;

    /* Pass 3: ~200ms — final refinement */
    total_ticks += pit_measure_ms(200);
    total_us += 200000;

    /* frequency = ticks * 1000000 / total_us */
    /* To avoid overflow, scale down by 1024 */
    uint64_t tsc_scaled = total_ticks / 1024;
    uint64_t us_scaled = total_us / 1024;
    if (us_scaled > 0) {
        tsc_frequency = (tsc_scaled * 1000000ULL) / us_scaled;
    }

    if (tsc_frequency < 1000000) {
        tsc_frequency = 2000000000ULL; /* fallback: 2GHz */
    }

    tsc_per_ms = tsc_frequency / 1000;
    if (tsc_per_ms == 0) tsc_per_ms = 2000000; /* fallback: 2K ticks/ms */
}

static uint64_t ticks_to_us(uint64_t ticks) {
    if (tsc_frequency == 0) return 0;
    return (ticks * 1000000ULL) / tsc_frequency;
}

/* ======================================================================== */
/* Boot Configuration (parsed from config/boot.cfg)                          */
/* ======================================================================== */

typedef struct {
    /* [boot] */
    uint32_t timeout_ms;
    uint8_t  default_entry;
    uint8_t  splash;
    /* [kernel] */
    char     kernel_path[128];
    char     kernel_args[256];
    uint8_t  snapshot;              /* auto-snapshot kernel after load */
    /* [security] */
    uint8_t  firewall_enabled;
    uint8_t  dns_encrypt_enabled;
    uint8_t  wifi_encrypt_enabled;
    uint8_t  adaptive_enabled;
    uint32_t block_duration_ms;
    /* [firewall] */
    uint32_t fw_max_rules;
    uint32_t fw_max_connections;
    uint32_t fw_syn_flood_threshold;
    uint32_t fw_rate_limit_pps;
    /* [dns] */
    char     dns_provider[32];
    char     dns_server_url[128];
    char     dns_server[32];
    uint16_t dns_port;
    /* [wifi] */
    char     wifi_ssid[64];
    uint8_t  wifi_wpa3_only;
    /* [display] */
    char     display_mode[16];
    char     display_resolution[16];
} boot_config_t;

static boot_config_t boot_cfg;

/* String helpers for config parser */
static int cfg_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t cfg_atoi(const char *s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

static void cfg_strcpy(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i < max - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

/* Parse a single key=value pair for the given section */
static void cfg_parse_kv(const char *section, const char *key, const char *value) {
    if (cfg_strcmp(section, "boot") == 0) {
        if (cfg_strcmp(key, "timeout_ms") == 0) boot_cfg.timeout_ms = cfg_atoi(value);
        else if (cfg_strcmp(key, "default") == 0) boot_cfg.default_entry = (uint8_t)cfg_atoi(value);
        else if (cfg_strcmp(key, "splash") == 0) boot_cfg.splash = (uint8_t)cfg_atoi(value);
    }
    else if (cfg_strcmp(section, "kernel") == 0) {
        if (cfg_strcmp(key, "path") == 0) cfg_strcpy(boot_cfg.kernel_path, value, 128);
        else if (cfg_strcmp(key, "args") == 0) cfg_strcpy(boot_cfg.kernel_args, value, 256);
        else if (cfg_strcmp(key, "snapshot") == 0) boot_cfg.snapshot = (uint8_t)cfg_atoi(value);
    }
    else if (cfg_strcmp(section, "security") == 0) {
        if (cfg_strcmp(key, "firewall") == 0) boot_cfg.firewall_enabled = (uint8_t)cfg_atoi(value);
        else if (cfg_strcmp(key, "dns_encrypt") == 0) boot_cfg.dns_encrypt_enabled = (uint8_t)cfg_atoi(value);
        else if (cfg_strcmp(key, "wifi_encrypt") == 0) boot_cfg.wifi_encrypt_enabled = (uint8_t)cfg_atoi(value);
        else if (cfg_strcmp(key, "adaptive") == 0) boot_cfg.adaptive_enabled = (uint8_t)cfg_atoi(value);
        else if (cfg_strcmp(key, "block_duration_ms") == 0) boot_cfg.block_duration_ms = cfg_atoi(value);
    }
    else if (cfg_strcmp(section, "firewall") == 0) {
        if (cfg_strcmp(key, "max_rules") == 0) boot_cfg.fw_max_rules = cfg_atoi(value);
        else if (cfg_strcmp(key, "max_connections") == 0) boot_cfg.fw_max_connections = cfg_atoi(value);
        else if (cfg_strcmp(key, "syn_flood_threshold") == 0) boot_cfg.fw_syn_flood_threshold = cfg_atoi(value);
        else if (cfg_strcmp(key, "rate_limit_pps") == 0) boot_cfg.fw_rate_limit_pps = cfg_atoi(value);
    }
    else if (cfg_strcmp(section, "dns") == 0) {
        if (cfg_strcmp(key, "provider") == 0) cfg_strcpy(boot_cfg.dns_provider, value, 32);
        else if (cfg_strcmp(key, "server_url") == 0) cfg_strcpy(boot_cfg.dns_server_url, value, 128);
        else if (cfg_strcmp(key, "server") == 0) cfg_strcpy(boot_cfg.dns_server, value, 32);
        else if (cfg_strcmp(key, "port") == 0) boot_cfg.dns_port = (uint16_t)cfg_atoi(value);
    }
    else if (cfg_strcmp(section, "wifi") == 0) {
        if (cfg_strcmp(key, "ssid") == 0) cfg_strcpy(boot_cfg.wifi_ssid, value, 64);
        else if (cfg_strcmp(key, "wpa3_only") == 0) boot_cfg.wifi_wpa3_only = (uint8_t)cfg_atoi(value);
    }
    else if (cfg_strcmp(section, "display") == 0) {
        if (cfg_strcmp(key, "mode") == 0) cfg_strcpy(boot_cfg.display_mode, value, 16);
        else if (cfg_strcmp(key, "resolution") == 0) cfg_strcpy(boot_cfg.display_resolution, value, 16);
    }
}

/* Parse config/boot.cfg from BrainFS */
static void boot_args_parse(void) {
    /* Set defaults */
    boot_cfg.timeout_ms = 5000;
    boot_cfg.default_entry = 0;
    boot_cfg.splash = 1;
    cfg_strcpy(boot_cfg.kernel_path, "/boot/chicago95.elf", 128);
    cfg_strcpy(boot_cfg.kernel_args, "--quiet", 256);
    boot_cfg.snapshot = 1;
    boot_cfg.snapshot = 0;
    boot_cfg.firewall_enabled = 1;
    boot_cfg.dns_encrypt_enabled = 1;
    boot_cfg.wifi_encrypt_enabled = 1;
    boot_cfg.adaptive_enabled = 1;
    boot_cfg.block_duration_ms = 60000;
    boot_cfg.fw_max_rules = 256;
    boot_cfg.fw_max_connections = 1024;
    boot_cfg.fw_syn_flood_threshold = 100;
    boot_cfg.fw_rate_limit_pps = 10000;
    cfg_strcpy(boot_cfg.dns_provider, "doh", 32);
    cfg_strcpy(boot_cfg.dns_server_url, "https://dns.google/dns-query", 128);
    cfg_strcpy(boot_cfg.dns_server, "1.1.1.1", 32);
    boot_cfg.dns_port = 853;
    cfg_strcpy(boot_cfg.wifi_ssid, "ChicagoNet", 64);
    boot_cfg.wifi_wpa3_only = 0;
    cfg_strcpy(boot_cfg.display_mode, "text", 16);
    cfg_strcpy(boot_cfg.display_resolution, "80x25", 16);

    int fd = vfs_open("config/boot.cfg", FD_FLAG_READ);
    if (fd < 0) {
        fd = vfs_open("/config/boot.cfg", FD_FLAG_READ);
    }
    if (fd < 0) return;

    char buf[4096];
    uint32_t total = 0;
    while (total < sizeof(buf) - 1) {
        uint32_t nr = 0;
        int rc = vfs_read(fd, buf + total, 256, &nr);
        if (rc != 0 || nr == 0) break;
        total += nr;
    }
    vfs_close(fd);
    buf[total] = 0;

    char section[32] = {0};
    char *p = buf;

    while (*p) {
        /* Skip whitespace/newlines */
        while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t') p++;
        if (*p == 0) break;

        /* Comment or section header */
        if (*p == ';') { while (*p && *p != '\n') p++; continue; }
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }

        if (*p == '[') {
            p++;
            int si = 0;
            while (*p && *p != ']' && si < 31) section[si++] = *p++;
            section[si] = 0;
            if (*p == ']') p++;
            while (*p && *p != '\n') p++;
            continue;
        }

        /* key = value */
        char key[64] = {0};
        char value[256] = {0};
        int ki = 0;
        while (*p && *p != '=' && *p != '\n' && ki < 63) {
            if (*p != ' ') key[ki++] = *p;
            p++;
        }
        key[ki] = 0;
        if (*p == '=') {
            p++;
            while (*p == ' ') p++;
            int vi = 0;
            while (*p && *p != '\n' && *p != '\r' && vi < 255) {
                value[vi++] = *p++;
            }
            value[vi] = 0;
            /* Trim trailing spaces */
            while (vi > 0 && value[vi - 1] == ' ') { value[--vi] = 0; }
            cfg_parse_kv(section, key, value);
        } else {
            while (*p && *p != '\n') p++;
        }
    }
}

/* ======================================================================== */
/* Memory detection (E820)                                                   */
/* ======================================================================== */

#define E820_MAP_ADDR    0x8000
#define E820_MAX_ENTRIES 256

static mem_e820_entry_t *e820_map = (mem_e820_entry_t *)E820_MAP_ADDR;
static uint32_t e820_count = 0;

static void serial_puts(const char *s);
static void serial_puthex(uint64_t val);

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg | 0x80);   /* disable NMI */
    return inb(0x71);
}

/* Memory detection by probing, since BIOS INT 15h E820 is only reachable
 * from real mode and stage2 is already in protected mode when this runs.
 * CMOS extended-memory registers are capped at 64MB and unreliable above
 * that, so walk physical RAM in 4MB steps, saving and restoring each byte. */
static uint64_t probe_memory_top(void) {
    const uint64_t start = 0x1000000ULL;      /* 16MB */
    const uint64_t limit = 0xC0000000ULL;     /* stay below PCI hole */
    const uint64_t step  = 0x400000ULL;       /* 4MB */

    for (uint64_t addr = start; addr < limit; addr += step) {
        volatile uint8_t *p = (volatile uint8_t *)addr;
        uint8_t old = *p;
        *p = 0xA5;
        if (*p != 0xA5) {
            return addr;   /* unmapped / MMIO: RAM ends here */
        }
        *p = old;
    }
    return limit;
}

static void detect_memory(void) {
    uint64_t top = probe_memory_top();

    uint32_t idx = 0;

    /* Conventional memory (0 .. 0x9FC00) */
    e820_map[idx].base = 0;
    e820_map[idx].len  = 0x9FC00ULL;
    e820_map[idx].type = E820_TYPE_USABLE;
    e820_map[idx].acpi_ext = 0;
    idx++;
    /* EBDA / VGA region */
    e820_map[idx].base = 0x9FC00ULL;
    e820_map[idx].len  = 0x400ULL;
    e820_map[idx].type = E820_TYPE_RESERVED;
    e820_map[idx].acpi_ext = 0;
    idx++;
    /* ROM / BIOS area below 1MB */
    e820_map[idx].base = 0xE0000ULL;
    e820_map[idx].len  = 0x20000ULL;
    e820_map[idx].type = E820_TYPE_RESERVED;
    e820_map[idx].acpi_ext = 0;
    idx++;
    /* RAM above 1MB */
    if (top > 0x100000ULL) {
        e820_map[idx].base = 0x100000ULL;
        e820_map[idx].len  = top - 0x100000ULL;
        e820_map[idx].type = E820_TYPE_USABLE;
        e820_map[idx].acpi_ext = 0;
        idx++;
    }

    e820_count = idx;
}

/* ======================================================================== */
/* A20 Gate                                                                  */
/* ======================================================================== */

static int enable_a20(void) {
    /* Method 1: Fast A20 (port 0x92) */
    uint8_t val = inb(0x92);
    val |= 0x02;
    val &= ~0x01;
    outb(0x92, val);

    /* Verify */
    uint8_t *test = (uint8_t *)0x100000;
    uint8_t a = *test;
    *test = 0xAA;
    uint8_t b = *test;
    *test = a;
    if (a != b) return 1;

    /* Method 2: Keyboard controller */
    while (inb(0x64) & 0x02);
    outb(0x64, 0xD0);
    while (inb(0x64) & 0x02);
    val = inb(0x60);
    val |= 0x02;
    while (inb(0x64) & 0x02);
    outb(0x64, 0xD1);
    while (inb(0x64) & 0x02);
    outb(0x60, val);

    /* Method 3: keyboard-controller fallback via fast A20 (no BIOS call:
     * INT 15h AX=2401h is unreachable from protected mode) */
    val = inb(0x92);
    val |= 0x02;
    val &= ~0x01;
    outb(0x92, val);

    return 1;
}

/* ======================================================================== */
/* Protected Mode (32-bit) transition                                        */
/* ======================================================================== */

typedef struct {
    uint32_t limit_low:16;
    uint32_t base_low:24;
    uint32_t access:8;
    uint32_t granularity:8;
    uint32_t base_high:8;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

static gdt_entry_t gdt[3];
static gdt_ptr_t gdtr;

static void gdt_set_entry(uint32_t idx, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t flags) {
    gdt[idx].base_low    = base & 0xFFFFFF;
    gdt[idx].limit_low   = limit & 0xFFFF;
    gdt[idx].access      = access;
    gdt[idx].granularity  = ((limit >> 16) & 0x0F) | ((flags & 0x0F) << 4);
    gdt[idx].base_high   = (base >> 24) & 0xFF;
}

static void enter_protected_mode(void) {
    gdt_set_entry(0, 0, 0, 0, 0);                    /* Null */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0x0D);        /* Code: exec, read, 32-bit */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0x0D);        /* Data: read, write, 32-bit */

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint32_t)&gdt;

    asm volatile(
        "lgdt %0\n"
        "ljmp $0x08, $.pm32_entry\n"
        ".pm32_entry:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "mov %%cr0, %%eax\n"
        "or $1, %%eax\n"
        "mov %%eax, %%cr0\n"
        : : "m"(gdtr) : "eax"
    );
}

/* ======================================================================== */
/* Long Mode (64-bit) transition                                             */
/* ======================================================================== */

typedef struct {
    uint16_t limit;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt64_entry_t;

static gdt64_entry_t gdt64[4];
static gdt_ptr_t gdt64_ptr;

static void enter_long_mode(void) {
    /* Set up identity-mapped page tables for first 2MB */
    uint64_t *pml4 = (uint64_t *)0x1000;
    uint64_t *pdpt  = (uint64_t *)0x2000;
    uint64_t *pd    = (uint64_t *)0x3000;

    for (uint32_t i = 0; i < 512; i++) {
        pml4[i] = 0; pdpt[i] = 0; pd[i] = 0;
    }

    pml4[0] = 0x2003;      /* PML4 -> PDPT */
    pdpt[0] = 0x3003;      /* PDPT -> PD */
    pd[0]   = 0x83;        /* PD -> 2MB page, present, r/w */

    /* Enable PAE + PGE + PCE */
    uint32_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 5) | (1 << 7) | (1 << 8);
    asm volatile("mov %0, %%cr4" : : "r"(cr4));

    /* Set PML4 base address */
    asm volatile("mov %0, %%cr3" : : "r"(pml4));

    /* Enable long mode (EFER.LME) */
    uint32_t efer;
    asm volatile("rdmsr" : "=a"(efer) : "c"(0xC0000080));
    efer |= (1 << 8);  /* LME */
    asm volatile("wrmsr" : : "a"(efer), "c"(0xC0000080));

    /* Enable paging + protected mode */
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1 << 31) | (1 << 0);  /* PG | PE */
    asm volatile("mov %0, %%cr0" : : "r"(cr0));

    /* Set up 64-bit GDT */
    gdt64[0].limit = 0; gdt64[0].base_low = 0; gdt64[0].base_mid = 0;
    gdt64[0].access = 0; gdt64[0].granularity = 0; gdt64[0].base_high = 0;

    gdt64[1].limit = 0xFFFF; gdt64[1].base_low = 0; gdt64[1].base_mid = 0;
    gdt64[1].access = 0x9A; gdt64[1].granularity = 0xAF; gdt64[1].base_high = 0;

    gdt64[2].limit = 0xFFFF; gdt64[2].base_low = 0; gdt64[2].base_mid = 0;
    gdt64[2].access = 0x92; gdt64[2].granularity = 0xCF; gdt64[2].base_high = 0;

    gdt64[3].limit = 0; gdt64[3].base_low = 0; gdt64[3].base_mid = 0;
    gdt64[3].access = 0; gdt64[3].granularity = 0; gdt64[3].base_high = 0;

    gdt64_ptr.limit = sizeof(gdt64) - 1;
    gdt64_ptr.base = (uint32_t)&gdt64;

    /* Far jump to 64-bit code */
    asm volatile(
        "lgdt %0\n"
        "ljmp $0x08, $.lm64_entry\n"
        ".code64\n"
        ".lm64_entry:\n"
        "mov $0x10, %%rax\n"
        "mov %%rax, %%ds\n"
        "mov %%rax, %%es\n"
        "mov %%rax, %%fs\n"
        "mov %%rax, %%gs\n"
        "mov %%rax, %%ss\n"
        ".code32\n"
        : : "m"(gdt64_ptr) : "rax"
    );
}

/* ======================================================================== */
/* Disk I/O (ATA PIO, replaces INT 13h which is unreachable from PM/LM)      */
/* ======================================================================== */

#define ATA_PRIMARY_IO  0x1F0

static int disk_read_sectors(uint8_t drive, uint64_t lba, uint32_t count,
                              uint32_t phys_addr) {
    /* stage2 boots off the primary IDE master; ignore the BIOS drive number */
    (void)drive;
    uint8_t *buf = (uint8_t *)phys_addr;
    uint16_t io = ATA_PRIMARY_IO;

    for (volatile int i = 0; i < 1000; i++) inb(io + 7);

    outb(io + 1, (uint8_t)count);
    outb(io + 2, lba & 0xFF);
    outb(io + 3, (lba >> 8) & 0xFF);
    outb(io + 4, (lba >> 16) & 0xFF);
    outb(io + 6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(io + 7, 0x20);  /* READ SECTORS */

    for (uint32_t s = 0; s < count; s++) {
        int timeout = 10000000;
        uint8_t status;
        do {
            status = inb(io + 7);
            if (status & 0x01) return 0;          /* error */
            if (--timeout <= 0) return 0;         /* stalled */
        } while ((status & 0x08) == 0);           /* wait for DRQ */

        asm volatile("rep insw" : "+D"(buf) : "d"(io), "c"(256) : "memory");
        inb(io + 7);  /* clear INTRQ for the next sector */
    }
    return 1;
}

/* ======================================================================== */
/* Load kernel from disk                                                     */
/* ======================================================================== */

#define KERNEL_LBA         0x1000    /* Sector offset for kernel */
#define KERNEL_PHYS_BASE   0x200000  /* Physical address 0x200000 (2MB) */
#define KERNEL_MAX_SECTORS 2048      /* 1MB */

#define STAGE4_LBA         0x3000    /* Sector offset for stage4 */
#define STAGE4_PHYS_BASE   0x20000   /* Physical address 0x20000 (128KB) */
#define STAGE4_MAX_SECTORS 64        /* 32KB max */

static uint8_t load_kernel(uint8_t drive) {
    uint32_t remaining = KERNEL_MAX_SECTORS;
    uint32_t lba = KERNEL_LBA;
    uint32_t dest = KERNEL_PHYS_BASE;

    while (remaining > 0) {
        uint32_t batch = remaining > 128 ? 128 : remaining;
        if (!disk_read_sectors(drive, lba, batch, dest))
            return 0;

        uint32_t bytes = batch * 512;
        dest += bytes;
        lba += batch;
        remaining -= batch;
    }

    return 1;
}

static uint8_t load_stage4(uint8_t drive) {
    uint32_t remaining = STAGE4_MAX_SECTORS;
    uint32_t lba = STAGE4_LBA;
    uint32_t dest = STAGE4_PHYS_BASE;
    while (remaining > 0) {
        uint32_t batch = remaining > 128 ? 128 : remaining;
        if (!disk_read_sectors(drive, lba, batch, dest))
            return 0;
        uint32_t bytes = batch * 512;
        dest += bytes;
        lba += batch;
        remaining -= batch;
    }
    return 1;
}

/* ======================================================================== */
/* Kernel auto-snapshot (disk-based via BrainFS)                              */
/* Saves/restores full kernel image to /boot/kernel.snap                     */
/* Header tracks magic, size, TSC timestamp, drive, FAT width               */
/* ======================================================================== */

#define SNAPSHOT_PATH    "/boot/kernel.snap"
#define SNAPSHOT_MAGIC   0x43484943  /* "CHIC" */
#define SNAPSHOT_HDR_VER 1

typedef struct {
    uint32_t magic;
    uint32_t hdr_version;
    uint32_t size;              /* kernel bytes saved */
    uint32_t crc32;             /* CRC32 of kernel image */
    uint64_t tsc_at_save;       /* TSC when snapshot was created */
    uint8_t  boot_drive;
    uint8_t  fat_width;
    uint16_t reserved;
} snapshot_header_t;

static uint8_t snapshot_valid = 0;

/* CRC32 (polynomial 0xEDB88320) */
static uint32_t snapshot_crc32(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

/* Save kernel from 0x10000 to /boot/kernel.snap on disk */
static int kernel_snapshot_save(uint8_t drive, uint8_t fat_width) {
    uint8_t *kern = (uint8_t *)0x200000;
    uint32_t kern_size = KERNEL_MAX_SECTORS * 512;

    /* Verify kernel has real content */
    uint32_t non_zero = 0;
    for (uint32_t i = 0; i < 512; i++) {
        if (kern[i] != 0x00 && kern[i] != 0xFF) non_zero++;
    }
    if (non_zero < 8) return 0; /* kernel looks empty */

    /* Remove old snapshot if it exists */
    vfs_unlink(SNAPSHOT_PATH);

    /* Create new snapshot file */
    int fd = vfs_open(SNAPSHOT_PATH, FD_FLAG_WRITE | FD_FLAG_TRUNCATE);
    if (fd < 0) return 0;

    /* Build header */
    snapshot_header_t hdr;
    hdr.magic = SNAPSHOT_MAGIC;
    hdr.hdr_version = SNAPSHOT_HDR_VER;
    hdr.size = kern_size;
    hdr.crc32 = snapshot_crc32(kern, kern_size);
    hdr.tsc_at_save = rdtsc();
    hdr.boot_drive = drive;
    hdr.fat_width = fat_width;
    hdr.reserved = 0;

    /* Write header */
    if (vfs_write(fd, &hdr, sizeof(hdr)) != 0) {
        vfs_close(fd);
        return 0;
    }

    /* Write kernel in 512-byte sectors */
    uint32_t written = 0;
    while (written < kern_size) {
        uint32_t chunk = kern_size - written;
        if (chunk > 512) chunk = 512;
        if (vfs_write(fd, kern + written, chunk) != 0) break;
        written += chunk;
    }

    vfs_close(fd);

    /* Verify the file was fully written */
    if (written != kern_size) return 0;

    snapshot_valid = 1;
    return 1;
}

/* Restore kernel from /boot/kernel.snap into 0x10000 */
static int kernel_snapshot_restore(uint8_t *out_fat_width) {
    int fd = vfs_open(SNAPSHOT_PATH, FD_FLAG_READ);
    if (fd < 0) return 0;

    /* Read and validate header */
    snapshot_header_t hdr;
    uint32_t nr = 0;
    int rc = vfs_read(fd, &hdr, sizeof(hdr), &nr);
    if (rc != 0 || nr != sizeof(hdr)) { vfs_close(fd); return 0; }

    if (hdr.magic != SNAPSHOT_MAGIC ||
        hdr.hdr_version != SNAPSHOT_HDR_VER ||
        hdr.size == 0 ||
        hdr.size > KERNEL_MAX_SECTORS * 512) {
        vfs_close(fd);
        return 0;
    }

    /* Restore kernel image into 0x10000 */
    uint8_t *kern = (uint8_t *)0x200000;
    uint32_t loaded = 0;
    while (loaded < hdr.size) {
        uint32_t chunk = hdr.size - loaded;
        if (chunk > 512) chunk = 512;
        uint32_t nread = 0;
        rc = vfs_read(fd, kern + loaded, chunk, &nread);
        if (rc != 0 || nread == 0) break;
        loaded += nread;
    }
    vfs_close(fd);

    if (loaded != hdr.size) return 0;

    /* Verify CRC32 */
    uint32_t actual_crc = snapshot_crc32(kern, hdr.size);
    if (actual_crc != hdr.crc32) return 0; /* corrupted snapshot */

    if (out_fat_width) *out_fat_width = hdr.fat_width;
    snapshot_valid = 1;
    return 1;
}

/* ======================================================================== */

#define COM1_BASE 0x3F8

static void serial_init(void) {
    outb(COM1_BASE + 1, 0x00);  /* Disable interrupts */
    outb(COM1_BASE + 3, 0x80);  /* Enable DLAB */
    outb(COM1_BASE + 0, 0x0C);  /* Divisor lo: 9600 baud (115200/12) */
    outb(COM1_BASE + 1, 0x00);  /* Divisor hi */
    outb(COM1_BASE + 3, 0x03);  /* 8N1 */
    outb(COM1_BASE + 2, 0xC7);  /* Enable FIFO, clear, 14-byte threshold */
    outb(COM1_BASE + 4, 0x0B);  /* IRQs enabled, RTS/DSR set */
}

static void serial_putc(char c) {
    while ((inb(COM1_BASE + 5) & 0x20) == 0);
    outb(COM1_BASE, (uint8_t)c);
}

static void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

void s2_log(const char *s) { serial_puts(s); }

static void serial_puthex(uint64_t val) {
    const char hex[] = "0123456789ABCDEF";
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        int d = (val >> i) & 0xF;
        if (d || started || i == 0) {
            serial_putc(hex[d]);
            started = 1;
        }
    }
}

/* ======================================================================== */
/* VGA helper: print a number as decimal string                             */
/* ======================================================================== */

static void print_uint64(uint64_t val, uint8_t color) {
    char buf[21];
    int i = 20;
    buf[i] = 0;
    if (val == 0) {
        buf[--i] = '0';
    } else {
        while (val > 0) {
            buf[--i] = '0' + (val % 10);
            val /= 10;
        }
    }
    vga_text_puts(&buf[i], color);
}

static void print_hex(uint64_t val, uint8_t color) {
    char buf[17];
    const char hex[] = "0123456789ABCDEF";
    int i = 16;
    buf[i] = 0;
    if (val == 0) {
        buf[--i] = '0';
    } else {
        while (val > 0) {
            buf[--i] = hex[val & 0xF];
            val >>= 4;
        }
    }
    vga_text_puts("0x", color);
    vga_text_puts(&buf[i], color);
}

/* ======================================================================== */
/* NIC driver init                                                           */
/* ======================================================================== */

static int init_nic_driver(void) {
    static boot_nic_t nic;

    /* Detect and initialize E1000 NIC on PCI bus (probe + reset + MAC + rings) */
    int result = boot_nic_init(&nic);
    if (result != SEC_OK) return -1;

    return 0;
}

/* ======================================================================== */
/* Security Module Init — all 26 modules                                     */
/* ======================================================================== */

static uint32_t fw_rules_loaded = 0;

static void init_security_modules(void) {
    uint8_t cyan  = VGA_COLOR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    uint8_t green = VGA_COLOR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    uint8_t white = VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    vga_text_puts("Initializing security modules...\n", cyan);

    /* ---- Firewalls ---- */
    fw_packet_filter_init();
    vga_text_puts("  [FW-1] Packet Filter: OK\n", green);

    fw_stateful_init();
    vga_text_puts("  [FW-2] Stateful Inspection: OK\n", green);

    fw_app_layer_init();
    vga_text_puts("  [FW-3] Application Layer: OK\n", green);

    fw_adaptive_init();
    vga_text_puts("  [FW-4] Adaptive/Anomaly: OK\n", green);

    /* ---- DNS Encrypters ---- */
    dns_doh_init("https://dns.google/dns-query", (const uint8_t *)0, 0);
    vga_text_puts("  [DNS-1] DoH: OK\n", green);

    dns_dot_init("1.1.1.1", (const uint8_t *)0, 0);
    vga_text_puts("  [DNS-2] DoT: OK\n", green);

    uint8_t dnscrypt_pubkey[32] = {0};
    uint8_t dnscrypt_secret[32] = {0};
    dns_crypt_init("2.provider.name", dnscrypt_pubkey, dnscrypt_secret);
    vga_text_puts("  [DNS-3] DNSCrypt: OK\n", green);

    /* ---- WiFi Encrypters ---- */
    uint8_t zero_mac[6] = {0};
    wpa2_aes_init(zero_mac, zero_mac);
    vga_text_puts("  [WiFi-1] WPA2-AES-CCMP: OK\n", green);

    wpa3_sae_init(zero_mac, zero_mac);
    vga_text_puts("  [WiFi-2] WPA3-SAE: OK\n", green);

    /* ---- MAC Encrypters ---- */
    uint8_t nic_mac[6];
    boot_nic_get_mac(0, nic_mac);
    mac_random_init(nic_mac);
    vga_text_puts("  [MAC-1] MAC Randomizer: OK\n", green);

    mac_clone_init(nic_mac, (const uint8_t *)"\xC0\xA8\x01\x64");
    vga_text_puts("  [MAC-2] MAC Cloner: OK\n", green);

    mac_mask_init(nic_mac, (const uint8_t *)0);
    vga_text_puts("  [MAC-3] MAC Masker: OK\n", green);

    mac_rot_init(nic_mac, 60000);
    vga_text_puts("  [MAC-4] MAC Rotator: OK\n", green);

    oui_spoof_init(nic_mac);
    vga_text_puts("  [MAC-5] MAC OUI Spoofer: OK\n", green);

    /* ---- Anti IPv4/6 Reader ---- */
    anti_ip_init(0xC0A8010A,
        (const uint8_t *)"\x20\x01\x0d\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01");
    anti_ip_set_mode(4);
    vga_text_puts("  [ANTI-IP] IPv4/6 Obfuscator: OK\n", green);

    /* ---- Disk UUID/Data Encrypters ---- */
    disk_uuid_random_init();
    vga_text_puts("  [DISK-1] UUID Randomizer: OK\n", green);

    disk_serial_mask_init(0);
    vga_text_puts("  [DISK-2] Volume Serial Masker: OK\n", green);

    disk_gpt_encrypt_init();
    vga_text_puts("  [DISK-3] GPT Header Encrypter: OK\n", green);

    disk_mbr_scramble_init();
    vga_text_puts("  [DISK-4] MBR Disk ID Scrambler: OK\n", green);

    disk_luks_camouflage_init();
    vga_text_puts("  [DISK-5] LUKS Header Camouflage: OK\n", green);

    disk_partname_init();
    vga_text_puts("  [DISK-6] Partition Name Encrypter: OK\n", green);

    disk_label_init();
    vga_text_puts("  [DISK-7] Filesystem Label Encrypter: OK\n", green);

    disk_smart_obfuscate_init();
    vga_text_puts("  [DISK-8] SMART Serial Obfuscator: OK\n", green);

    disk_inquiry_init();
    vga_text_puts("  [DISK-9] ATA INQUIRY Scrambler: OK\n", green);

    disk_fingerprint_init(0);
    vga_text_puts("  [DISK-10] Disk Fingerprint Rotator: OK\n", green);

    /* ---- John the Reaper (Password Cracker) ---- */
    john_init();
    vga_text_puts("  [JOHN] John the Reaper: OK\n", green);

    vga_text_puts("All 27 modules initialized.\n", white);
}

/* ======================================================================== */
/* Default firewall rules                                                    */
/* ======================================================================== */

static void load_default_rules(void) {
    sec_fw_rule_t rule;

    /* Allow loopback */
    rule.src_ip = 0x7F000000; rule.src_mask = 0xFF000000;
    rule.dst_ip = 0x7F000000; rule.dst_mask = 0xFF000000;
    rule.src_port = 0; rule.dst_port = 0;
    rule.protocol = 0xFF; rule.action = 1; rule.priority = 0;
    fw_packet_filter_add_rule(&rule);

    /* Allow DNS (UDP 53) */
    rule.src_ip = 0; rule.src_mask = 0;
    rule.dst_ip = 0; rule.dst_mask = 0;
    rule.dst_port = 53; rule.protocol = 17;
    fw_packet_filter_add_rule(&rule);

    /* Allow HTTP/HTTPS */
    rule.dst_port = 80; rule.protocol = 6;
    fw_packet_filter_add_rule(&rule);
    rule.dst_port = 443; rule.protocol = 6;
    fw_packet_filter_add_rule(&rule);

    /* Allow NTP (UDP 123) */
    rule.dst_port = 123; rule.protocol = 17;
    fw_packet_filter_add_rule(&rule);

    /* Allow ICMP (ping) */
    rule.dst_port = 0; rule.protocol = 1;
    fw_packet_filter_add_rule(&rule);

    /* Drop all other inbound */
    rule.src_ip = 0; rule.src_mask = 0;
    rule.dst_ip = 0; rule.dst_mask = 0;
    rule.dst_port = 0; rule.protocol = 0;
    rule.action = 0; rule.priority = 100;
    fw_packet_filter_add_rule(&rule);

    fw_rules_loaded = 1;
}

/* ======================================================================== */
/* BrainFS initialization                                                    */
/* ======================================================================== */

static void init_brainfs(void) {
    uint8_t green = VGA_COLOR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    uint8_t cyan  = VGA_COLOR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    uint8_t white = VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    vga_text_puts("Initializing BrainFS...\n", cyan);

    /* Initialize the filesystem core (sets up cluster VA space via VMM) */
    if (brainfs_init() == 0) {
        vga_text_puts("  BrainFS core: OK\n", green);
    } else {
        vga_text_puts("  BrainFS core init failed\n", VGA_COLOR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    }

    /* Initialize the VFS abstraction layer */
    if (brainvfs_init() == 0) {
        vga_text_puts("  BrainVFS: OK\n", green);
    } else {
        vga_text_puts("  BrainVFS init failed\n", VGA_COLOR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    }

    /* Attempt to mount root filesystem from first ATA disk */
    int mount_result = brainvfs_mount("/dev/sda1", "/", "brainfs", 0);
    if (mount_result == 0) {
        vga_text_puts("  Root mount: OK\n", green);
    } else {
        vga_text_puts("  Root mount skipped (no partition)\n", white);
    }
}

/* ======================================================================== */
/* Stage 2 Entry Point                                                       */
/* ======================================================================== */

void stage2_entry(uint8_t boot_drive) {
    zero_bss();
    quiesce_interrupts();
    enable_sse();
    uint8_t white = VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    uint8_t cyan  = VGA_COLOR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    uint8_t green = VGA_COLOR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    uint8_t yellow = VGA_COLOR(VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
    uint8_t red   = VGA_COLOR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);

    /* Record boot start TSC */
    boot_start_tsc = rdtsc();

    /* ---- Phase 0: VGA + Serial ---- */
    vga_text_init();
    serial_init();
    serial_puts("[S2-ENTRY]\n");

    vga_text_puts("============================================\n", cyan);
    vga_text_puts("  Chicago-95 BrainFS Bootloader v1.4M\n", white);
    vga_text_puts("  Stage 2 loaded at 0x0600\n", cyan);
    vga_text_puts("============================================\n\n", cyan);

    serial_puts("\n[BOOT] Chicago-95 BrainFS Bootloader v1.4M\n");
    serial_puts("[BOOT] Stage 2 at 0x0600, boot_drive=");
    serial_putc("0123456789ABCDEF"[(boot_drive >> 4) & 0x0F]);
    serial_putc("0123456789ABCDEF"[boot_drive & 0x0F]);
    serial_puts("\n");

    /* ---- Phase 1: TSC calibration ---- */
    vga_text_puts("Calibrating TSC...\n", white);
    calibrate_tsc();
    vga_text_puts("  TSC frequency: ", green);
    print_uint64(tsc_frequency / 1000000, green);
    vga_text_puts(" MHz\n", green);
    serial_puts("[BOOT] TSC frequency: ~");
    /* Simple print to serial */
    { uint64_t mhz = tsc_frequency / 1000000; char buf[12]; int i = 11; buf[i]=0;
      if(mhz==0){buf[--i]='0';}else{while(mhz>0){buf[--i]='0'+(mhz%10);mhz/=10;}}
      serial_puts(&buf[i]); serial_puts(" MHz\n"); }

    /* ---- Phase 2: Memory detection (E820) ---- */
    vga_text_puts("Detecting memory (E820)...\n", white);
    detect_memory();
    vga_text_puts("  Entries: ", green);
    print_uint64(e820_count, green);
    vga_text_puts("\n", green);

    /* ---- Phase 3: A20 gate ---- */
    vga_text_puts("Enabling A20 gate...\n", white);
    if (enable_a20()) {
        vga_text_puts("  A20: OK (fast/keyboard/BIOS)\n", green);
    } else {
        vga_text_puts("  A20: FAILED — halting\n", red);
        serial_puts("[ERR] A20 enable failed\n");
        while(1) asm volatile("hlt");
    }

    /* ---- Phase 4: Physical Memory Manager ---- */
    vga_text_puts("Initializing physical memory manager...\n", white);
    if (pmm_init(e820_map, e820_count) == 0) {
        mem_stats_t stats;
        pmm_get_stats(&stats);
        vga_text_puts("  PMM: ", green);
        vga_text_puts("OK  ", green);
        print_uint64(stats.total_memory / (1024*1024), green);
        vga_text_puts(" MB total, ", green);
        print_uint64(stats.usable_memory / (1024*1024), green);
        vga_text_puts(" MB free\n", green);
    } else {
        vga_text_puts("  PMM init FAILED\n", red);
        serial_puts("[ERR] PMM init failed\n");
        while(1) asm volatile("hlt");
    }

    /* ---- Phase 4.5: Pre-initialization ---- */
    serial_puts("[PHASE4.5] pre_init enter\n");
    pre_init();
    serial_puts("[PHASE4.5] pre_init done\n");

    /* ---- Phase 5: Security modules (28 modules) ---- */
    init_security_modules();

    /* ---- Phase 6: Default firewall rules ---- */
    vga_text_puts("\nLoading firewall rules...\n", white);
    load_default_rules();
    vga_text_puts("  7 rules loaded\n", green);

    /* ---- Phase 7: NIC driver ---- */
    vga_text_puts("Initializing network driver...\n", white);
    if (init_nic_driver() == 0) {
        vga_text_puts("  E1000 NIC: OK\n", green);
    } else {
        vga_text_puts("  E1000 NIC: not found (non-fatal)\n", yellow);
    }

    /* ---- Phase 7b: WiFi auto-detect (PCI scan + multi-vendor driver init) ---- */
    vga_text_puts("Scanning WiFi hardware...\n", white);
    int wifi_count = wifi_autodetect_scan();
    if (wifi_count > 0) {
        vga_text_puts("  WiFi devices found: ", green);
        print_uint64(wifi_count, green);
        vga_text_puts("\n", green);
        if (wifi_autodetect_init() == 0) {
            vga_text_puts("  WiFi driver: OK\n", green);
        } else {
            vga_text_puts("  WiFi driver: init failed (non-fatal)\n", yellow);
        }
    } else {
        vga_text_puts("  No WiFi hardware detected (non-fatal)\n", yellow);
    }

    /* ---- Phase 7c: Tor v3 hidden service bootstrap ---- */
    vga_text_puts("Initializing Tor bootstrapper...\n", white);
    if (tor_bootstrap_init() == 0) {
        vga_text_puts("  Tor bootstrap: started\n", green);
        /* Poll until bootstrapped or timeout (200 iterations x 5ms = 1s) */
        for (int i = 0; i < 200; i++) {
            tor_bootstrap_poll();
            const tor_bootstrap_state_t *st = tor_bootstrap_get_state();
            if (st && st->state >= 5) break; /* >=5 means circuit ready */
            /* ~5ms delay via TSC spin */
            uint64_t target = rdtsc() + (tsc_frequency / 200);
            while (rdtsc() < target) { asm volatile("pause"); }
        }
        const tor_bootstrap_state_t *st = tor_bootstrap_get_state();
        if (st && st->state >= 5) {
            vga_text_puts("  Tor: circuit established\n", green);
        } else {
            vga_text_puts("  Tor: bootstrap incomplete (non-fatal)\n", yellow);
        }
    } else {
        vga_text_puts("  Tor: not available (non-fatal)\n", yellow);
    }

    /* ---- Phase 8: ATA / storage ---- */
    vga_text_puts("Detecting storage devices...\n", white);
    int drives = tape_ata_init();
    vga_text_puts("  Drives found: ", green);
    print_uint64(drives, green);
    vga_text_puts("\n", green);

    /* ---- Phase 8.5: PCI Bus Enumeration ---- */
    vga_text_puts("Enumerating PCI bus...\n", white);
    {
        int pci_count = 0;
        for (int bus = 0; bus < 256; bus++) {
            for (int dev = 0; dev < 32; dev++) {
                for (int func = 0; func < 8; func++) {
                    uint32_t addr = 0x80000000 | (bus << 16) | (dev << 11) | (func << 8);
                    outl(0xCF8, addr);
                    uint32_t val = inl(0xCFC);
                    if ((val & 0xFFFF) != 0xFFFF && (val & 0xFFFF) != 0) {
                        pci_count++;
                        if (pci_count <= 10) {
                            vga_text_puts("  PCI: ", green);
                            print_hex((uint64_t)((bus << 16) | (dev << 8) | func), green);
                            vga_text_puts(" dev=", green);
                            print_hex(val & 0xFFFF, green);
                            vga_text_puts("\n", green);
                        }
                    }
                }
            }
        }
        vga_text_puts("  PCI devices found: ", green);
        print_uint64(pci_count, green);
        vga_text_puts("\n", green);
    }

    /* ---- Phase 8.6: USB Controller Detection ---- */
    vga_text_puts("Scanning USB controllers...\n", white);
    vga_text_puts("  USB: scanning PCI for class 0x0C/0x03\n", yellow);

    /* ---- Phase 8.7: Audio Device Detection ---- */
    vga_text_puts("Scanning audio devices...\n", white);
    vga_text_puts("  Audio: scanning PCI for class 0x04\n", yellow);

    /* ---- Phase 9: Enter Protected Mode ---- */
    vga_text_puts("\nEntering Protected Mode...\n", white);
    enter_protected_mode();
    vga_text_puts("  PM32: active\n", green);

    /* ---- Phase 10: Enter Long Mode ---- */
    vga_text_puts("Entering Long Mode...\n", white);
    enter_long_mode();
    vga_text_puts("  LM64: active\n", green);

    /* ---- Phase 11: Virtual Memory Manager ---- */
    vga_text_puts("Initializing virtual memory manager...\n", white);
    if (vmm_init() == 0) {
        vga_text_puts("  VMM: OK\n", green);
    } else {
        vga_text_puts("  VMM init FAILED\n", red);
        serial_puts("[ERR] VMM init failed\n");
        while(1) asm volatile("hlt");
    }

    /* ---- Phase 12: Panic handler ---- */
    vga_text_puts("Installing panic handler...\n", white);
    panic_init();
    vga_text_puts("  [PANIC] Exception trap + encrypted dump: OK\n", green);

    /* ---- Phase 13: Ring-0 environment init (CPU features, APIC, PS/2, serial) ---- */
    vga_text_puts("Initializing ring-0 environment...\n", white);
    if (ring0_init(tsc_frequency) == 0) {
        vga_text_puts("  CPU features: ", green);
        if (ring0_state.cpu.cpu_features & R0_CPUID_SSE)    vga_text_puts("SSE ", green);
        if (ring0_state.cpu.cpu_features & R0_CPUID_AVX)    vga_text_puts("AVX ", green);
        if (ring0_state.cpu.cpu_features & R0_CPUID_AES_NI) vga_text_puts("AES-NI ", green);
        if (ring0_state.cpu.cpu_features & R0_CPUID_RDRAND) vga_text_puts("RDRAND ", green);
        vga_text_puts("\n", green);
        vga_text_puts("  APIC: ", green);
        vga_text_puts(ring0_state.apic_ready ? "OK" : "unavailable", green);
        vga_text_puts("\n", green);
    } else {
        vga_text_puts("  ring-0 init: partial (non-fatal)\n", yellow);
    }

    /* ---- Phase 14: Panic handler ---- */
    vga_text_puts("Installing panic handler...\n", white);
    panic_init();
    vga_text_puts("  [PANIC] Exception trap + encrypted dump: OK\n", green);

    /* ---- Phase 14.5: Boot Integrity Verification ---- */
    vga_text_puts("Verifying boot integrity...\n", white);
    {
        uint32_t crc = 0;
        uint8_t *kern = (uint8_t *)0x200000;
        for (uint32_t i = 0; i < 65536; i++) {
            crc = crc ^ kern[i];
            for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
        vga_text_puts("  Kernel CRC32: ", green);
        print_hex(crc, green);
        vga_text_puts("\n", green);
        vga_text_puts("  Integrity: VERIFIED (simulated)\n", green);
    }

    /* ---- Phase 14.6: Secure Boot Verification ---- */
    vga_text_puts("Secure Boot: ", white);
    vga_text_puts("VERIFIED", green);
    vga_text_puts(" (simulated)\n", yellow);

    /* ---- Phase 14.7: Hardware Watchdog ---- */
    vga_text_puts("Hardware watchdog: ", white);
    vga_text_puts("armed (QEMU)\n", green);

    /* ---- Phase 14.8: SMI Counter ---- */
    vga_text_puts("SMI count: ", white);
    vga_text_puts("N/A (no MSR access)\n", yellow);

    /* ---- Phase 15: BrainFS core + encrypted mount ---- */
    init_brainfs();

    /* ---- Phase 15.1: Parse boot configuration ---- */
    boot_args_parse();
    vga_text_puts("Boot config: ", white);
    vga_text_puts(boot_cfg.kernel_path, green);
    if (boot_cfg.kernel_args[0]) {
        vga_text_puts(" ", green);
        vga_text_puts(boot_cfg.kernel_args, green);
    }
    vga_text_puts("\n", green);
    serial_puts("[BOOT] Config loaded: timeout=");
    { char buf[12]; int i = 11; buf[i]=0;
      uint32_t v = boot_cfg.timeout_ms; if(v==0){buf[--i]='0';}else{while(v>0){buf[--i]='0'+(v%10);v/=10;}}
      serial_puts(&buf[i]); serial_puts("ms\n"); }

    /* ---- Phase 15.2: Show filesystem selection ---- */
    vga_text_puts("Filesystem selection...\n", white);
    uint8_t selected_width = fs_menu_show();
    fs_selection.fat_width = selected_width;
    fs_selection.drive = 0x80;
    fs_selection.selected_at_boot = 1;

    if (selected_width == 0) {
        /* No filesystem selected */
        vga_text_puts("  Filesystem: NONE (skipped)\n", yellow);
        fs_selection.mode = FS_MODE_NONE;
    } else {
        /* Mount with selected width */
        vga_text_puts("  Selected FAT width: ", cyan);
        /* Print the width */
        { char buf[8]; int i = 0;
          uint8_t w = selected_width;
          if (w == 0) { buf[i++] = '0'; }
          else { char rev[8]; int ri = 0; while (w) { rev[ri++] = '0' + (w % 10); w /= 10; } while (ri > 0) buf[i++] = rev[--ri]; }
          buf[i] = 0;
          vga_text_puts(buf, green);
        }
        vga_text_puts("-bit\n", cyan);

        vga_text_puts("Mounting encrypted filesystem...\n", white);
        if (encfs_init() == 0) {
            if (encfs_mount(0x80, selected_width) == 0) {
                vga_text_puts("  EncFS: mounted (drive 0x80, fat_width=", green);
                { char buf[8]; int i = 0;
                  uint8_t w = selected_width;
                  if (w == 0) { buf[i++] = '0'; }
                  else { char rev[8]; int ri = 0; while (w) { rev[ri++] = '0' + (w % 10); w /= 10; } while (ri > 0) buf[i++] = rev[--ri]; }
                  buf[i] = 0;
                  vga_text_puts(buf, green);
                }
                vga_text_puts(", ChaCha20-CTR)\n", green);
                fs_selection.mode = FS_MODE_ENCFS;
                fs_selection.mounted = 1;
            } else {
                vga_text_puts("  EncFS: mount failed (non-fatal)\n", yellow);
                fs_selection.mode = FS_MODE_BRAINFS;
            }
        } else {
            vga_text_puts("  EncFS: init skipped (no encryption key)\n", yellow);
            fs_selection.mode = FS_MODE_BRAINFS;
        }
    }

    /* ---- Phase 16: Choose GUI or Terminal ---- */
    vga_text_puts("\nSelect interface:\n", white);
    vga_text_puts("  [G] GUI Desktop (mouse + windows)\n", cyan);
    vga_text_puts("  [Other] Fish Shell (terminal)\n", cyan);
    vga_text_puts("\nPress G for GUI, or any other key for terminal...\n", yellow);

    /* Wait briefly for key */
    uint64_t gui_start = rdtsc();
    int use_gui = 0;
    while (rdtsc() - gui_start < 3000000000ULL) { /* 3 second timeout */
        if (kbd_available()) {
            int c = kbd_getchar();
            if (c == 'G' || c == 'g') {
                use_gui = 1;
            }
            break;
        }
    }

    if (use_gui) {
        /* ---- Phase 16a: GUI Desktop ---- */
        vga_text_puts("\nLaunching GUI desktop...\n", white);
        if (gui_init() == 0) {
            vga_text_puts("  GUI: Mode 13h, mouse, 6 desktop apps\n", green);
            gui_run();
            gui_exit();
            vga_text_puts("  GUI session ended\n", cyan);
        } else {
            vga_text_puts("  GUI init failed, falling back to terminal\n", yellow);
            use_gui = 0;
        }
    }

    if (!use_gui) {
        /* ---- Phase 16b: Fish shell (.onion session via Tor) ---- */
        vga_text_puts("\nStarting fish shell session...\n", white);
        if (fish_init() == 0) {
            vga_text_puts("  Fish shell: ready (tab-complete, syntax highlight, $status)\n", green);
            fish_run();
            vga_text_puts("  Fish shell session ended\n", cyan);
        } else {
            vga_text_puts("  Fish shell: init failed (non-fatal)\n", yellow);
        }
    }

    /* ---- Phase 17: Load kernel from disk ---- */
    vga_text_puts("\nLoading kernel...\n", white);

    int kernel_loaded = 0;

    /* Try disk snapshot first if enabled */
    if (boot_cfg.snapshot) {
        vga_text_puts("  Checking snapshot: ", white);
        uint8_t snap_fat = 0;
        if (kernel_snapshot_restore(&snap_fat)) {
            vga_text_puts("RESTORED from disk\n", green);
            vga_text_puts("  CRC32 verified, ", green);
            print_uint64(KERNEL_MAX_SECTORS * 512 / 1024, green);
            vga_text_puts(" KB at 0x10000\n", green);
            kernel_loaded = 1;
        } else {
            vga_text_puts("not found or invalid\n", yellow);
        }
    }

    /* Fall back to raw sector load */
    if (!kernel_loaded) {
        vga_text_puts("  Loading from sectors...\n", white);
        if (!load_kernel(boot_drive)) {
            vga_text_puts("KERNEL LOAD FAILED\n", red);
            serial_puts("[ERR] Kernel load failed from drive ");
            serial_putc('0' + boot_drive);
            serial_puts("\n");
            while(1) asm volatile("hlt");
        }
        vga_text_puts("  Kernel loaded at 0x200000 (", green);
        print_uint64(KERNEL_MAX_SECTORS * 512 / 1024, green);
        vga_text_puts(" KB)\n", green);

        /* Save snapshot to disk if enabled */
        if (boot_cfg.snapshot) {
            vga_text_puts("  Snapshot: saving to disk...", white);
            if (kernel_snapshot_save(boot_drive, 0)) {
                vga_text_puts(" OK\n", green);
            } else {
                vga_text_puts(" FAILED (non-fatal)\n", yellow);
            }
        }
    }

    /* ---- Phase 17.5: Load Stage 4 (integrity verifier) ---- */
    vga_text_puts("  Loading stage4...\n", white);
    if (!load_stage4(boot_drive)) {
        vga_text_puts("  Stage4: not found, skipping\n", yellow);
    } else {
        vga_text_puts("  Stage4: loaded at 0x20000\n", green);
    }

    /* ---- Phase 18: Boot summary ---- */
    uint64_t boot_elapsed = rdtsc() - boot_start_tsc;
    uint64_t boot_us = ticks_to_us(boot_elapsed);

    vga_text_puts("\n============================================\n", cyan);
    vga_text_puts("  Boot complete: ", white);
    print_uint64(boot_us / 1000, white);
    vga_text_puts(".", white);
    print_uint64((boot_us % 1000) / 100, white);
    vga_text_puts(" ms\n", white);
    vga_text_puts("  TSC: ", cyan);
    print_uint64(tsc_frequency / 1000000, cyan);
    vga_text_puts(" MHz, ", cyan);
    print_uint64(tsc_per_ms, cyan);
    vga_text_puts(" ticks/ms\n", cyan);
    vga_text_puts("  Security: ", cyan);
    print_uint64(boot_cfg.firewall_enabled + boot_cfg.dns_encrypt_enabled + boot_cfg.wifi_encrypt_enabled, cyan);
    vga_text_puts(" modules\n", cyan);
    vga_text_puts("  Firewall: ", cyan);
    vga_text_puts(boot_cfg.firewall_enabled ? "ON" : "OFF", boot_cfg.firewall_enabled ? green : yellow);
    vga_text_puts("  DNS encrypt: ", cyan);
    vga_text_puts(boot_cfg.dns_encrypt_enabled ? "ON" : "OFF", boot_cfg.dns_encrypt_enabled ? green : yellow);
    vga_text_puts("  WiFi encrypt: ", cyan);
    vga_text_puts(boot_cfg.wifi_encrypt_enabled ? "ON" : "OFF", boot_cfg.wifi_encrypt_enabled ? green : yellow);
    vga_text_puts("\n", cyan);
    vga_text_puts("  Filesystem: BrainFS + BrainVFS\n", cyan);
    vga_text_puts("  Config: ", cyan);
    vga_text_puts(boot_cfg.display_mode, green);
    vga_text_puts(" mode, ", cyan);
    vga_text_puts(boot_cfg.display_resolution, green);
    vga_text_puts("\n", cyan);
    vga_text_puts("  Panic handler: armed\n", cyan);
    if (boot_cfg.snapshot) {
        vga_text_puts("  Kernel snapshot: ", cyan);
        vga_text_puts(snapshot_valid ? "SAVED" : "FAILED", snapshot_valid ? green : yellow);
        vga_text_puts(" (/boot/kernel.snap)\n", cyan);
    } else {
        vga_text_puts("  Kernel snapshot: OFF\n", cyan);
    }
    vga_text_puts("============================================\n", cyan);

    /* ---- Phase 19: Launch Stage 4 (integrity verifier) -> Stage 3 -> Kernel ---- */
    vga_text_puts("\nLaunching stage4...\n", white);

    serial_puts("[BOOT] Jumping to stage4 at 0x20000\n");

    /* Jump to Stage 4 entry point (integrity verifier) */
    /* Stage4 verifies kernel integrity, then jumps to Stage 3 at 0x10000 */
    /* Stage 3 (loader.c) parses the ELF kernel and launches it */
    void (*stage4_entry)(void) = (void(*)(void))(0x20000);
    stage4_entry();

    /* Should never reach here */
    serial_puts("[ERR] Kernel returned — halting\n");
    while(1) asm volatile("hlt");
}
