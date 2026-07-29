#include "boot/pre_init.h"
#include "boot/security.h"
#include <stdint.h>

typedef struct {
    uint64_t base;
    uint64_t len;
    uint32_t type;
    uint32_t acpi_attr;
} e820_entry_t;

static preinit_state_t g_preinit;

static inline void pi_outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t pi_inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline uint8_t pi_inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline uint64_t pi_rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static uint32_t pi_pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    uint32_t addr = 0x80000000u
        | ((uint32_t)bus << 16)
        | ((uint32_t)dev << 11)
        | ((uint32_t)func << 8)
        | (reg & 0xFCu);
    pi_outl(0xCF8, addr);
    return pi_inl(0xCFC);
}

void pre_init_cpuid(uint32_t leaf, uint32_t subleaf,
                    uint32_t *eax, uint32_t *ebx,
                    uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf));
}

uint64_t pre_init_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

uint64_t pre_init_tsc_jitter(uint32_t samples) {
    uint64_t acc = 0;
    uint64_t prev = pre_init_rdtsc();
    for (uint32_t i = 0; i < samples; i++) {
        uint64_t cur = pre_init_rdtsc();
        acc ^= (cur - prev);
        prev = cur;
    }
    return acc;
}

uint32_t pre_init_io_entropy(void) {
    uint32_t ent = 0;
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"((uint16_t)0x40)); ent ^= val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"((uint16_t)0x61)); ent ^= (uint32_t)val << 8;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"((uint16_t)0x71)); ent ^= (uint32_t)val << 16;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"((uint16_t)0xCFC)); ent ^= (uint32_t)val << 24;
    return ent;
}

uint32_t pre_init_crc32(const void *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *p = (const uint8_t *)data;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320u;
            else crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

uint32_t pre_init_random_u32(void) {
    static uint32_t rng_state = 0;
    if (rng_state == 0) {
        for (int i = 0; i < 4; i++)
            rng_state ^= ((uint32_t *)g_preinit.rng.seed)[i];
    }
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

void pre_init_random_bytes(uint8_t *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t v = pre_init_random_u32();
        uint32_t remain = len - i;
        if (remain >= 4) {
            buf[i]     = (uint8_t)(v);
            buf[i + 1] = (uint8_t)(v >> 8);
            buf[i + 2] = (uint8_t)(v >> 16);
            buf[i + 3] = (uint8_t)(v >> 24);
        } else {
            for (uint32_t j = 0; j < remain; j++)
                buf[i + j] = (uint8_t)(v >> (j * 8));
        }
    }
}

static void pi_vga_putuint(uint64_t val) {
    char buf[21];
    int i = 20;
    buf[i] = 0;
    if (val == 0) {
        buf[--i] = '0';
    } else {
        while (val > 0 && i > 0) {
            buf[--i] = '0' + (char)(val % 10);
            val /= 10;
        }
    }
    pre_init_vga_puts(&buf[i]);
}

void pre_init_vga_puts(const char *str) {
    static volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    static int vga_col = 0;
    static int vga_row = 24;
    while (*str) {
        if (*str == '\n') {
            vga_col = 0;
            vga_row++;
            if (vga_row >= 25) vga_row = 0;
            str++;
            continue;
        }
        vga[vga_row * 80 + vga_col] = (uint16_t)(*str) | (0x0Fu << 8);
        vga_col++;
        if (vga_col >= 80) {
            vga_col = 0;
            vga_row++;
            if (vga_row >= 25) vga_row = 0;
        }
        str++;
    }
}

int pre_init_cpu_fingerprint(preinit_cpu_fingerprint_t *fp) {
    uint32_t eax, ebx, ecx, edx;

    pre_init_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    fp->vendor_ebx = ebx;
    fp->vendor_edx = edx;
    fp->vendor_ecx = ecx;
    fp->vendor_string[0]  = (char)(ebx);
    fp->vendor_string[1]  = (char)(ebx >> 8);
    fp->vendor_string[2]  = (char)(ebx >> 16);
    fp->vendor_string[3]  = (char)(ebx >> 24);
    fp->vendor_string[4]  = (char)(edx);
    fp->vendor_string[5]  = (char)(edx >> 8);
    fp->vendor_string[6]  = (char)(edx >> 16);
    fp->vendor_string[7]  = (char)(edx >> 24);
    fp->vendor_string[8]  = (char)(ecx);
    fp->vendor_string[9]  = (char)(ecx >> 8);
    fp->vendor_string[10] = (char)(ecx >> 16);
    fp->vendor_string[11] = (char)(ecx >> 24);
    fp->vendor_string[12] = 0;

    pre_init_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    fp->signature_eax   = eax;
    fp->brandIndex      = ebx & 0xFF;
    fp->clflush_size    = (ebx >> 8) & 0xFF;
    fp->logic_cpus      = (ebx >> 16) & 0xFF;
    fp->initial_apic_id = (ebx >> 24) & 0xFF;
    fp->feature_ecx     = ecx;
    fp->feature_edx     = edx;

    pre_init_cpuid(7, 0, &eax, &ebx, &ecx, &edx);
    fp->ext_feature_ebx = ebx;

    for (uint32_t i = 0; i < 3; i++) {
        pre_init_cpuid(0x80000002 + i, 0, &eax, &ebx, &ecx, &edx);
        uint32_t off = i * 16;
        uint32_t *d = (uint32_t *)&fp->cache_tlb[off / 4];
        d[0] = eax;
        d[1] = ebx;
        d[2] = ecx;
        d[3] = edx;
    }

    uint32_t tlb_idx = 48;
    for (uint32_t leaf = 2; leaf <= 2; leaf++) {
        pre_init_cpuid(leaf, 0, &eax, &ebx, &ecx, &edx);
        if (tlb_idx + 4 <= PREINIT_CPUID_CACHE) {
            fp->cache_tlb[tlb_idx++] = eax;
            fp->cache_tlb[tlb_idx++] = ebx;
            fp->cache_tlb[tlb_idx++] = ecx;
            fp->cache_tlb[tlb_idx++] = edx;
        }
    }
    pre_init_cpuid(0x80000006, 0, &eax, &ebx, &ecx, &edx);
    if (tlb_idx + 4 <= PREINIT_CPUID_CACHE) {
        fp->cache_tlb[tlb_idx++] = eax;
        fp->cache_tlb[tlb_idx++] = ebx;
        fp->cache_tlb[tlb_idx++] = ecx;
        fp->cache_tlb[tlb_idx++] = edx;
    }

    fp->xcr0 = 0;
    pre_init_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    if (ecx & (1u << 27)) {
        __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
        fp->xcr0 = eax;
    }

    return PREINIT_OK;
}

int pre_init_memory_fingerprint(preinit_memory_fingerprint_t *fp) {
    uint32_t count = *(volatile uint32_t *)0x8000;
    e820_entry_t *map = (e820_entry_t *)0x8004;

    fp->e820_entries = count;
    fp->total_ram    = 0;
    fp->usable_ram   = 0;
    fp->first_usable_addr = 0;
    fp->first_hole_addr   = 0;
    fp->memory_sane = 0;

    if (count == 0 || count > 256) return PREINIT_OK;

    int found_usable = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t end = map[i].base + map[i].len;
        if (end > fp->total_ram) fp->total_ram = end;
        if (map[i].type == 1) {
            fp->usable_ram += map[i].len;
            if (!found_usable) {
                fp->first_usable_addr = map[i].base;
                found_usable = 1;
            }
        }
    }

    fp->e820_crc32 = pre_init_crc32((const void *)0x8000, 4 + count * sizeof(e820_entry_t));

    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t j = i + 1; j < count; j++) {
            if (map[i].base < map[j].base) {
                uint64_t end_i = map[i].base + map[i].len;
                if (end_i > map[j].base) {
                    fp->memory_sane = 0;
                    return PREINIT_OK;
                }
            } else if (map[j].base < map[i].base) {
                uint64_t end_j = map[j].base + map[j].len;
                if (end_j > map[i].base) {
                    fp->memory_sane = 0;
                    return PREINIT_OK;
                }
            }
        }
    }

    for (uint32_t i = 0; i + 1 < count; i++) {
        uint64_t end_a = map[i].base + map[i].len;
        if (map[i + 1].base > end_a && map[i + 1].type == 1 && map[i].type == 1) {
            fp->first_hole_addr = end_a;
            break;
        }
    }

    if (fp->usable_ram > 0x100000)
        fp->memory_sane = 1;

    return PREINIT_OK;
}

int pre_init_rng_seed(preinit_rng_state_t *rng) {
    uint32_t eax, ebx, ecx, edx;

    for (uint32_t i = 0; i < 64; i++) rng->seed[i] = 0;

    uint64_t jitter = pre_init_tsc_jitter(256);
    rng->tsc_jitter_entropy = jitter;
    uint64_t *seed64 = (uint64_t *)rng->seed;
    for (int i = 0; i < 8; i++) {
        seed64[i] ^= jitter;
        jitter = pre_init_tsc_jitter(32);
        seed64[i] ^= jitter;
    }

    uint32_t io_ent = 0;
    for (int i = 0; i < 64; i++) {
        io_ent ^= pre_init_io_entropy();
        uint32_t *seed32 = (uint32_t *)rng->seed;
        seed32[i % 16] ^= io_ent;
    }
    rng->io_entropy = io_ent;

    pre_init_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    rng->rdrand_available = (ecx & (1u << 30)) ? 1 : 0;
    if (rng->rdrand_available) {
        uint32_t *seed32 = (uint32_t *)rng->seed;
        for (int i = 0; i < 8; i++) {
            uint32_t rnd;
            __asm__ volatile("rdrand %0" : "=r"(rnd));
            seed32[i] ^= rnd;
        }
    }

    pre_init_cpuid(7, 0, &eax, &ebx, &ecx, &edx);
    rng->rdseed_available = (ebx & (1u << 18)) ? 1 : 0;
    if (rng->rdseed_available) {
        uint32_t *seed32 = (uint32_t *)rng->seed;
        for (int i = 8; i < 16; i++) {
            uint32_t rnd;
            __asm__ volatile("rdseed %0" : "=r"(rnd));
            seed32[i] ^= rnd;
        }
    }

    rng->seed_crc32 = pre_init_crc32(rng->seed, 64);
    rng->seed_len = 64;
    rng->initialized = 1;

    sec_random_bytes(rng->seed, 64);

    return PREINIT_OK;
}

int pre_init_boot_integrity(preinit_integrity_t *integrity) {
    integrity->stage1_crc32 = pre_init_crc32((const void *)(uintptr_t)0x7C00, 512);
    integrity->stage1_size  = 512;

    integrity->stage2_crc32 = pre_init_crc32((const void *)(uintptr_t)0x0600, 128 * 1024);
    integrity->stage2_size  = 128 * 1024;

    integrity->stage3_crc32 = pre_init_crc32((const void *)(uintptr_t)0x10000, 0x8000);
    integrity->stage3_size  = 0x8000;

    integrity->kernel_crc32 = 0;

    integrity->integrity_ok   = 0;
    integrity->secure_boot_ok = 0;

    if (integrity->stage1_crc32 != 0 && integrity->stage2_crc32 != 0)
        integrity->integrity_ok = 1;

    return PREINIT_OK;
}

int pre_init_anti_tamper(preinit_tamper_state_t *tamper) {
    static const uint32_t expected_stage1_crc = 0;
    static const uint32_t expected_stage2_crc = 0;

    tamper->tamper_detected  = 0;
    tamper->tamper_count      = 0;
    tamper->hardware_changed  = 0;
    tamper->memory_changed    = 0;
    tamper->expected_stage1_crc = expected_stage1_crc;
    tamper->expected_stage2_crc = expected_stage2_crc;

    if (expected_stage1_crc != 0 &&
        g_preinit.integrity.stage1_crc32 != expected_stage1_crc) {
        tamper->tamper_detected = 1;
        tamper->tamper_count++;
    }
    if (expected_stage2_crc != 0 &&
        g_preinit.integrity.stage2_crc32 != expected_stage2_crc) {
        tamper->tamper_detected = 1;
        tamper->tamper_count++;
    }

    uint64_t *bcp = (uint64_t *)0x7FF0;
    tamper->boot_counter = *bcp;
    (*bcp)++;
    tamper->boot_counter = *bcp;

    return PREINIT_OK;
}

int pre_init_dma_protect(preinit_dma_state_t *dma) {
    dma->region_count   = 0;
    dma->iommu_detected = 0;
    dma->iommu_enabled  = 0;
    dma->blocked_count  = 0;

    for (uint8_t dev = 0; dev < 32; dev++) {
        uint32_t vendev = pi_pci_read32(0, dev, 0, 0x00);
        if ((vendev & 0xFFFF) == 0xFFFF) continue;

        uint32_t class_reg = pi_pci_read32(0, dev, 0, 0x08);
        uint8_t class    = (class_reg >> 24) & 0xFF;
        uint8_t subclass = (class_reg >> 16) & 0xFF;
        uint8_t prog_if  = (class_reg >> 8) & 0xFF;

        if (class == 0x08 && subclass == 0x04 && prog_if == 0x00) {
            dma->iommu_detected = 1;
            dma->iommu_enabled  = 1;
        }

        for (uint8_t bar_off = 0; bar_off < 6; bar_off++) {
            uint8_t reg = 0x10 + bar_off * 4;
            uint32_t bar = pi_pci_read32(0, dev, 0, reg);
            if (bar == 0 || bar == 0xFFFFFFFF) continue;
            if ((bar & 1) == 0 && dma->region_count < PREINIT_MAX_DMA_REGIONS) {
                preinit_dma_region_t *r = &dma->regions[dma->region_count];
                r->base   = (uint64_t)(bar & 0xFFFFFFF0u);
                r->length = 0;
                r->active = 1;
                r->type   = 0;
                dma->region_count++;
            }
        }

        uint32_t hdr_type_reg = pi_pci_read32(0, dev, 0, 0x0C);
        if (!(hdr_type_reg & 0x00800000)) continue;

        for (uint8_t func = 1; func < 8; func++) {
            uint32_t v2 = pi_pci_read32(0, dev, func, 0x00);
            if ((v2 & 0xFFFF) == 0xFFFF) continue;

            for (uint8_t bar_off = 0; bar_off < 6; bar_off++) {
                uint8_t reg = 0x10 + bar_off * 4;
                uint32_t bar = pi_pci_read32(0, dev, func, reg);
                if (bar == 0 || bar == 0xFFFFFFFF) continue;
                if ((bar & 1) == 0 && dma->region_count < PREINIT_MAX_DMA_REGIONS) {
                    preinit_dma_region_t *r = &dma->regions[dma->region_count];
                    r->base   = (uint64_t)(bar & 0xFFFFFFF0u);
                    r->length = 0;
                    r->active = 1;
                    r->type   = 0;
                    dma->region_count++;
                }
            }
        }
    }

    return PREINIT_OK;
}

int pre_init_smi_counter(preinit_smi_state_t *smi) {
    smi->available     = 0;
    smi->initial_count = 0;
    smi->final_count   = 0;
    smi->delta         = 0;
    smi->log_count     = 0;

    uint32_t eax_lo, edx_hi;
    __asm__ volatile("rdmsr" : "=a"(eax_lo), "=d"(edx_hi) : "c"((uint32_t)0x34));
    uint64_t val = ((uint64_t)edx_hi << 32) | eax_lo;

    if (val != 0 && val != 0xFFFFFFFFFFFFFFFFull) {
        smi->available     = 1;
        smi->initial_count = val;
    }

    return PREINIT_OK;
}

int pre_init(void) {
    sec_memzero(&g_preinit, sizeof(g_preinit));

    pre_init_vga_puts("Pre-initialization...\n");

    g_preinit.magic   = PREINIT_MAGIC;
    g_preinit.version = PREINIT_VERSION;
    g_preinit.preinit_start_tsc = pre_init_rdtsc();

    int result;

    result = pre_init_cpu_fingerprint(&g_preinit.cpu);
    pre_init_vga_puts("[PRE-01] CPU fingerprint: ");
    pre_init_vga_puts(result == PREINIT_OK ? "OK\n" : "FAIL\n");

    result = pre_init_memory_fingerprint(&g_preinit.mem);
    pre_init_vga_puts("[PRE-02] Memory fingerprint: ");
    pre_init_vga_puts(result == PREINIT_OK ? "OK\n" : "FAIL\n");

    result = pre_init_rng_seed(&g_preinit.rng);
    pre_init_vga_puts("[PRE-03] RNG seed: ");
    pre_init_vga_puts(result == PREINIT_OK ? "OK\n" : "FAIL\n");

    result = pre_init_boot_integrity(&g_preinit.integrity);
    pre_init_vga_puts("[PRE-04] Boot integrity: ");
    pre_init_vga_puts(result == PREINIT_OK ? "OK\n" : "FAIL\n");

    result = pre_init_anti_tamper(&g_preinit.tamper);
    pre_init_vga_puts("[PRE-05] Anti-tamper: ");
    pre_init_vga_puts(result == PREINIT_OK ? "OK\n" : "FAIL\n");

    result = pre_init_dma_protect(&g_preinit.dma);
    pre_init_vga_puts("[PRE-06] DMA protection: ");
    pre_init_vga_puts(result == PREINIT_OK ? "OK\n" : "FAIL\n");

    result = pre_init_smi_counter(&g_preinit.smi);
    pre_init_vga_puts("[PRE-07] SMI counter: ");
    pre_init_vga_puts(result == PREINIT_OK ? "OK\n" : "FAIL\n");

    g_preinit.preinit_end_tsc = pre_init_rdtsc();
    g_preinit.preinit_time_us = (g_preinit.preinit_end_tsc - g_preinit.preinit_start_tsc) / 2000;

    pre_init_vga_puts("Pre-init complete (");
    pi_vga_putuint(g_preinit.preinit_time_us);
    pre_init_vga_puts(" us)\n");

    g_preinit.initialized = 1;
    return PREINIT_OK;
}

const preinit_state_t *pre_init_get_state(void) {
    return &g_preinit;
}

void pre_init_get_stats(uint64_t *time_us, uint32_t *tamper_count, uint8_t *integrity_ok) {
    if (time_us) *time_us = g_preinit.preinit_time_us;
    if (tamper_count) *tamper_count = g_preinit.tamper.tamper_count;
    if (integrity_ok) *integrity_ok = g_preinit.integrity.integrity_ok;
}
