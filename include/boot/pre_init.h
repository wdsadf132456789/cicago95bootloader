/**
 * Chicago-95 Bootloader Pre-Initialization
 * Runs between PMM init and security module init.
 * Hardware fingerprinting, RNG seeding, boot integrity,
 * anti-tamper, crypto init, DMA protection, SMI counter.
 */

#ifndef BOOT_PRE_INIT_H
#define BOOT_PRE_INIT_H

#include <stdint.h>

/* ========================================================================
 * Pre-init constants
 * ======================================================================== */
#define PREINIT_OK              0
#define PREINIT_ERR_NOMEM      -1
#define PREINIT_ERR_HARDWARE   -2
#define PREINIT_ERR_TAMPER     -3
#define PREINIT_ERR_INTEGRITY  -4
#define PREINIT_ERR_RNG        -5
#define PREINIT_ERR_DMA        -6

#define PREINIT_MAGIC          0x43484943  /* "CHIC" */
#define PREINIT_VERSION        1
#define PREINIT_MAX_DMA_REGIONS 16
#define PREINIT_CPUID_CACHE    64
#define PREINIT_SMI_MAX_LOG    32

/* ========================================================================
 * CPU fingerprint (CPUID-derived)
 * ======================================================================== */
typedef struct {
    uint32_t vendor_ebx;
    uint32_t vendor_edx;
    uint32_t vendor_ecx;
    char     vendor_string[13];
    uint32_t brandIndex;
    uint32_t clflush_size;
    uint32_t logic_cpus;
    uint32_t initial_apic_id;
    uint32_t feature_ecx;       /* SSE3, SSSE3, AES-NI, etc */
    uint32_t feature_edx;       /* TSC, MSR, APIC, FXSAVE, SSE, SSE2 */
    uint32_t ext_feature_ebx;   /* AVX, BMI, etc */
    uint32_t signature_eax;     /* stepping/model/family */
    uint32_t cache_tlb[PREINIT_CPUID_CACHE];
    uint32_t xcr0;              /* XSAVE feature mask */
} preinit_cpu_fingerprint_t;

/* ========================================================================
 * Memory layout fingerprint
 * ======================================================================== */
typedef struct {
    uint64_t total_ram;
    uint64_t usable_ram;
    uint32_t e820_entries;
    uint32_t e820_crc32;
    uint64_t first_usable_addr;
    uint64_t first_hole_addr;
    uint8_t  memory_sane;       /* 1 if E820 data passes sanity checks */
} preinit_memory_fingerprint_t;

/* ========================================================================
 * RNG seed state
 * ======================================================================== */
typedef struct {
    uint8_t  seed[64];
    uint8_t  seed_len;
    uint8_t  rdrand_available;
    uint8_t  rdseed_available;
    uint64_t tsc_jitter_entropy;
    uint32_t io_entropy;
    uint32_t seed_crc32;
    uint8_t  initialized;
} preinit_rng_state_t;

/* ========================================================================
 * Boot integrity measurement
 * ======================================================================== */
typedef struct {
    uint32_t stage1_crc32;
    uint32_t stage2_crc32;
    uint32_t stage3_crc32;
    uint32_t kernel_crc32;
    uint64_t stage1_size;
    uint64_t stage2_size;
    uint64_t stage3_size;
    uint8_t  integrity_ok;
    uint8_t  secure_boot_ok;
} preinit_integrity_t;

/* ========================================================================
 * Anti-tamper state
 * ======================================================================== */
typedef struct {
    uint32_t expected_stage1_crc;
    uint32_t expected_stage2_crc;
    uint8_t  tamper_detected;
    uint8_t  tamper_count;
    uint64_t boot_counter;
    uint8_t  hardware_changed;  /* CPU fingerprint differs from stored */
    uint8_t  memory_changed;    /* Memory layout differs from stored */
} preinit_tamper_state_t;

/* ========================================================================
 * DMA protection region
 * ======================================================================== */
typedef struct {
    uint64_t base;
    uint64_t length;
    uint8_t  active;
    uint8_t  type;              /* 0=MMIO 1=PIO 2=DMA 3=reserved */
} preinit_dma_region_t;

/* ========================================================================
 * DMA protection state
 * ======================================================================== */
typedef struct {
    preinit_dma_region_t regions[PREINIT_MAX_DMA_REGIONS];
    uint32_t region_count;
    uint8_t  iommu_detected;
    uint8_t  iommu_enabled;
    uint32_t blocked_count;
} preinit_dma_state_t;

/* ========================================================================
 * SMI counter
 * ======================================================================== */
typedef struct {
    uint64_t initial_count;
    uint64_t final_count;
    uint64_t delta;
    uint8_t  available;         /* 0 = no SMI counter MSRs */
    uint64_t log[PREINIT_SMI_MAX_LOG];
    uint32_t log_count;
} preinit_smi_state_t;

/* ========================================================================
 * Master pre-init state
 * ======================================================================== */
typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  initialized;
    uint64_t preinit_start_tsc;
    uint64_t preinit_end_tsc;
    uint64_t preinit_time_us;
    preinit_cpu_fingerprint_t      cpu;
    preinit_memory_fingerprint_t   mem;
    preinit_rng_state_t            rng;
    preinit_integrity_t            integrity;
    preinit_tamper_state_t         tamper;
    preinit_dma_state_t            dma;
    preinit_smi_state_t            smi;
} preinit_state_t;

/* ========================================================================
 * Pre-init API
 * ======================================================================== */

/* Main pre-init entry point (called from stage2_entry before security init) */
int  pre_init(void);

/* Individual pre-init stages */
int  pre_init_cpu_fingerprint(preinit_cpu_fingerprint_t *fp);
int  pre_init_memory_fingerprint(preinit_memory_fingerprint_t *fp);
int  pre_init_rng_seed(preinit_rng_state_t *rng);
int  pre_init_boot_integrity(preinit_integrity_t *integrity);
int  pre_init_anti_tamper(preinit_tamper_state_t *tamper);
int  pre_init_dma_protect(preinit_dma_state_t *dma);
int  pre_init_smi_counter(preinit_smi_state_t *smi);

/* Utility functions */
uint32_t pre_init_crc32(const void *data, uint32_t len);
uint64_t pre_init_rdtsc(void);
uint32_t pre_init_random_u32(void);
void     pre_init_random_bytes(uint8_t *buf, uint32_t len);
uint32_t pre_init_io_entropy(void);
uint64_t pre_init_tsc_jitter(uint32_t samples);

/* CPUID wrapper */
void pre_init_cpuid(uint32_t leaf, uint32_t subleaf,
                    uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx);

/* State access */
const preinit_state_t *pre_init_get_state(void);
void pre_init_get_stats(uint64_t *time_us, uint32_t *tamper_count, uint8_t *integrity_ok);

/* VGA output helper */
void pre_init_vga_puts(const char *str);

#endif /* BOOT_PRE_INIT_H */
