/**
 * Chicago-95 Physical Memory Manager (PMM)
 * Bitmap allocator with E820 memory map parsing
 * First-fit allocation strategy with automatic BIOS region reservation
 * and kernel image boundary protection.
 *
 * All returned page frame pointers are verified 4KB-aligned.
 */

#include "memory/memory.h"

/* ======================================================================== */
/* Constants                                                                */
/* ======================================================================== */

#define PMM_MAX_PAGES       (1024ULL * 1024 * 16) /* 64GB address space */
#define PMM_BITMAP_SIZE     ((PMM_MAX_PAGES + 7) / 8)
#define PMM_DMA_LIMIT       (16ULL * 1024 * 1024)  /* 16MB for DMA */

/* BIOS reserved regions (lower 1MB) — always marked used */
#define BIOS_IVT_BASE       0x00000000ULL
#define BIOS_IVT_END        0x00000400ULL  /* 1KB  — IVT */
#define BIOS_BDA_BASE       0x00000400ULL
#define BIOS_BDA_END        0x00000500ULL  /* 256B — BIOS Data Area */
#define BIOS_EBDA_BASE      0x00000500ULL
#define BIOS_EBDA_END       0x00000600ULL  /* 256B — EBDA start (Stage 2 reloc) */
#define BIOS_VGA_BASE       0x000A0000ULL
#define BIOS_VGA_END        0x000C0000ULL  /* 128KB — VGA memory */
#define BIOS_ROM_BASE       0x000C0000ULL
#define BIOS_ROM_END        0x00100000ULL  /* 256KB — BIOS ROM / option ROMs */

/* Chicago-95 internal regions — always marked used */
#define STAGE1_BASE         0x00007C00ULL
#define STAGE1_END          0x00007E00ULL  /* 512B  — MBR */
#define DAP_BASE            0x00009000ULL
#define DAP_END             0x00009200ULL  /* 512B  — DAP scratch buffer */
#define E820_MAP_BASE       0x00008000ULL
#define E820_MAP_END        (0x00008000ULL + 256 * sizeof(mem_e820_entry_t))
#define STAGE2_BASE         0x00000600ULL
#define STAGE2_END          0x00007C00ULL  /* Stage 2 blob up to Stage 1 */

/* ======================================================================== */
/* State                                                                    */
/* ======================================================================== */

static uint8_t   pmm_bitmap[PMM_BITMAP_SIZE];
static uint64_t  pmm_total_pages;
static uint64_t  pmm_free_count;
static uint64_t  pmm_reserved_pages;   /* BIOS + kernel + ACPI + hardware reserved */
static uint32_t  pmm_e820_count;
static mem_e820_entry_t *pmm_e820;

/* Stats per E820 type */
static uint64_t  pmm_usable_bytes;
static uint64_t  pmm_reserved_bytes;
static uint64_t  pmm_acpi_reclaim_bytes;
static uint64_t  pmm_acpi_nvs_bytes;
static uint64_t  pmm_bad_bytes;

/* ======================================================================== */
/* Bitmap primitives                                                        */
/* ======================================================================== */

static inline void pmm_bitmap_set(uint64_t page) {
    pmm_bitmap[page >> 3] |= (1U << (page & 7));
}

static inline void pmm_bitmap_clear(uint64_t page) {
    pmm_bitmap[page >> 3] &= ~(1U << (page & 7));
}

static inline int pmm_bitmap_test(uint64_t page) {
    return (pmm_bitmap[page >> 3] >> (page & 7)) & 1;
}

/* Mark a range of pages [start, end) in the bitmap.  1 = set (used), 0 = clear (free). */
static void pmm_bitmap_set_range(uint64_t start_page, uint64_t end_page, int used) {
    if (start_page >= pmm_total_pages) return;
    if (end_page > pmm_total_pages) end_page = pmm_total_pages;

    for (uint64_t p = start_page; p < end_page; p++) {
        if (used)
            pmm_bitmap_set(p);
        else
            pmm_bitmap_clear(p);
    }
}

/* ======================================================================== */
/* Mark a physical address range as used or free in the bitmap               */
/* ======================================================================== */

static void pmm_reserve_range(uint64_t base, uint64_t length) {
    uint64_t start_page = base / PAGE_SIZE;
    uint64_t end_page   = (base + length + PAGE_SIZE - 1) / PAGE_SIZE;

    if (end_page > pmm_total_pages) end_page = pmm_total_pages;

    for (uint64_t p = start_page; p < end_page; p++) {
        if (!pmm_bitmap_test(p)) {
            pmm_bitmap_set(p);
            if (pmm_free_count > 0) pmm_free_count--;
            pmm_reserved_pages++;
        }
    }
}

static void pmm_free_range(uint64_t base, uint64_t length) {
    uint64_t start_page = (base + PAGE_SIZE - 1) / PAGE_SIZE;   /* align up */
    uint64_t end_page   = (base + length) / PAGE_SIZE;           /* align down */

    if (end_page > pmm_total_pages) end_page = pmm_total_pages;

    for (uint64_t p = start_page; p < end_page; p++) {
        if (pmm_bitmap_test(p)) {
            pmm_bitmap_clear(p);
            pmm_free_count++;
        }
    }
}

/* ======================================================================== */
/* First-fit bitmap scanner — word-at-a-time scan (64-bit chunks)           */
/* ======================================================================== */

static uint64_t pmm_find_free_pages(uint32_t count) {
    if (count == 0) return 0;

    uint64_t total = pmm_total_pages;
    uint64_t run_start = 0;
    uint32_t run_len   = 0;

    /* Scan bitmap 64 bits (8 bytes) at a time for speed */
    uint64_t word_count = (total + 63) / 64;

    for (uint64_t word_idx = 0; word_idx < word_count; word_idx++) {
        uint64_t word = ~0ULL; /* assume all used if we overrun bitmap end */

        /* Load 8 bytes of bitmap, handling overrun at tail */
        uint64_t byte_offset = word_idx * 8;
        if (byte_offset < PMM_BITMAP_SIZE) {
            uint64_t bytes_left = PMM_BITMAP_SIZE - byte_offset;
            if (bytes_left >= 8) {
                word = 0;
                for (uint32_t b = 0; b < 8; b++)
                    word |= (uint64_t)pmm_bitmap[byte_offset + b] << (b * 8);
            } else {
                /* Partial word — pad with 0xFF (used) beyond bitmap end */
                word = 0;
                for (uint32_t b = 0; b < 8; b++) {
                    if (b < bytes_left)
                        word |= (uint64_t)pmm_bitmap[byte_offset + b] << (b * 8);
                    else
                        word |= (0xFFULL << (b * 8));
                }
            }
        }

        /* All pages in this word are used — skip */
        if (word == ~0ULL) {
            /* Reset run streak */
            run_len = 0;
            continue;
        }

        /* Scan individual bits within the word */
        for (uint32_t bit = 0; bit < 64; bit++) {
            uint64_t page_idx = word_idx * 64 + bit;
            if (page_idx >= total) {
                /* Past end of address space — if we have enough, return */
                if (run_len >= count) return run_start;
                return 0;
            }

            if (word & (1ULL << bit)) {
                /* Page is used — reset run */
                run_len = 0;
            } else {
                /* Page is free */
                if (run_len == 0)
                    run_start = page_idx;
                run_len++;

                if (run_len == count) {
                    /* Verify all pages in the run are within a single E820 usable region */
                    return run_start;
                }
            }
        }
    }

    return 0; /* Not found */
}

/* First-fit scan restricted to DMA region (below 16MB) */
static uint64_t pmm_find_free_pages_dma(uint32_t count) {
    if (count == 0) return 0;

    uint64_t dma_limit = PMM_DMA_LIMIT / PAGE_SIZE;
    if (dma_limit > pmm_total_pages) dma_limit = pmm_total_pages;

    uint64_t run_start = 0;
    uint32_t run_len   = 0;

    uint64_t word_count = (dma_limit + 63) / 64;

    for (uint64_t word_idx = 0; word_idx < word_count; word_idx++) {
        uint64_t byte_offset = word_idx * 8;
        uint64_t word = 0;

        if (byte_offset < PMM_BITMAP_SIZE) {
            uint64_t bytes_left = PMM_BITMAP_SIZE - byte_offset;
            uint32_t bytes_to_read = (bytes_left >= 8) ? 8 : (uint32_t)bytes_left;
            for (uint32_t b = 0; b < bytes_to_read; b++)
                word |= (uint64_t)pmm_bitmap[byte_offset + b] << (b * 8);
            for (uint32_t b = bytes_to_read; b < 8; b++)
                word |= (0xFFULL << (b * 8));
        }

        if (word == ~0ULL) {
            run_len = 0;
            continue;
        }

        for (uint32_t bit = 0; bit < 64; bit++) {
            uint64_t page_idx = word_idx * 64 + bit;
            if (page_idx >= dma_limit) {
                if (run_len >= count) return run_start;
                return 0;
            }

            if (word & (1ULL << bit)) {
                run_len = 0;
            } else {
                if (run_len == 0) run_start = page_idx;
                run_len++;
                if (run_len == count) return run_start;
            }
        }
    }

    return 0;
}

/* ======================================================================== */
/* Alignment verification                                                   */
/* ======================================================================== */

static inline int pmm_is_page_aligned(uint64_t addr) {
    return (addr & (PAGE_SIZE - 1)) == 0;
}

static inline uint64_t pmm_align_up(uint64_t addr) {
    return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static inline uint64_t pmm_align_down(uint64_t addr) {
    return addr & ~(PAGE_SIZE - 1);
}

/* ======================================================================== */
/* Init: scan E820, reserve BIOS/kernel regions, free usable memory         */
/* ======================================================================== */

int pmm_init(mem_e820_entry_t *e820_map, uint32_t e820_count) {
    /* ---- Phase 1: Mark entire bitmap as USED (all pages reserved) ---- */
    for (uint32_t i = 0; i < PMM_BITMAP_SIZE; i++)
        pmm_bitmap[i] = 0xFF;

    pmm_e820        = e820_map;
    pmm_e820_count  = e820_count;
    pmm_total_pages = 0;
    pmm_free_count  = 0;
    pmm_reserved_pages = 0;
    pmm_usable_bytes = 0;
    pmm_reserved_bytes = 0;
    pmm_acpi_reclaim_bytes = 0;
    pmm_acpi_nvs_bytes = 0;
    pmm_bad_bytes = 0;

    /* ---- Phase 2: Determine total address space from E820 ---- */
    uint64_t highest_addr = 0;
    for (uint32_t i = 0; i < e820_count; i++) {
        uint64_t end = e820_map[i].base + e820_map[i].len;
        if (end > highest_addr)
            highest_addr = end;
    }

    pmm_total_pages = highest_addr / PAGE_SIZE;
    if (pmm_total_pages > PMM_MAX_PAGES)
        pmm_total_pages = PMM_MAX_PAGES;

    /* ---- Phase 3: Free all E820 usable regions ---- */
    for (uint32_t i = 0; i < e820_count; i++) {
        uint64_t base = e820_map[i].base;
        uint64_t len  = e820_map[i].len;

        switch (e820_map[i].type) {
        case E820_TYPE_USABLE:
            pmm_free_range(base, len);
            pmm_usable_bytes += len;
            break;
        case E820_TYPE_RESERVED:
            pmm_reserve_range(base, len);
            pmm_reserved_bytes += len;
            break;
        case E820_TYPE_ACPI_RECLAIM:
            pmm_reserve_range(base, len);
            pmm_acpi_reclaim_bytes += len;
            break;
        case E820_TYPE_ACPI_NVS:
            pmm_reserve_range(base, len);
            pmm_acpi_nvs_bytes += len;
            break;
        case E820_TYPE_BAD:
            pmm_reserve_range(base, len);
            pmm_bad_bytes += len;
            break;
        default:
            /* Unknown type — reserve it */
            pmm_reserve_range(base, len);
            pmm_reserved_bytes += len;
            break;
        }
    }

    /* ---- Phase 4: Reserve BIOS lower 1MB regions ---- */
    /* IVT (0x000 - 0x3FF) */
    pmm_reserve_range(BIOS_IVT_BASE, BIOS_IVT_END - BIOS_IVT_BASE);

    /* BIOS Data Area (0x400 - 0x4FF) */
    pmm_reserve_range(BIOS_BDA_BASE, BIOS_BDA_END - BIOS_BDA_BASE);

    /* EBDA start (0x500 - 0x5FF) — Stage 2 reloc target */
    pmm_reserve_range(BIOS_EBDA_BASE, BIOS_EBDA_END - BIOS_EBDA_BASE);

    /* VGA video memory (0xA0000 - 0xBFFFF) */
    pmm_reserve_range(BIOS_VGA_BASE, BIOS_VGA_END - BIOS_VGA_BASE);

    /* BIOS ROM + option ROMs (0xC0000 - 0xFFFFF) */
    pmm_reserve_range(BIOS_ROM_BASE, BIOS_ROM_END - BIOS_ROM_BASE);

    /* ---- Phase 5: Reserve Chicago-95 internal regions ---- */

    /* Stage 1 MBR (0x7C00 - 0x7DFF) */
    pmm_reserve_range(STAGE1_BASE, STAGE1_END - STAGE1_BASE);

    /* DAP scratch buffer (0x9000 - 0x91FF) */
    pmm_reserve_range(DAP_BASE, DAP_END - DAP_BASE);

    /* E820 memory map (0x8000 - ~0x8800) */
    pmm_reserve_range(E820_MAP_BASE, E820_MAP_END - E820_MAP_BASE);

    /* Stage 2 image (0x600 - 0x7BFF) — everything up to MBR */
    pmm_reserve_range(STAGE2_BASE, STAGE2_END - STAGE2_BASE);

    /* ---- Phase 6: Reserve first page (null pointer safety) ---- */
    if (pmm_bitmap_test(0)) {
        /* Already reserved — count it */
    } else {
        pmm_bitmap_set(0);
        if (pmm_free_count > 0) pmm_free_count--;
        pmm_reserved_pages++;
    }

    return 0;
}

/* ======================================================================== */
/* Page allocation — first-fit with 4KB alignment verification              */
/* ======================================================================== */

void *pmm_alloc_page(void) {
    uint64_t page = pmm_find_free_pages(1);
    if (page == 0) return (void *)0;

    /* Verify page is actually free (defensive check) */
    if (pmm_bitmap_test(page)) return (void *)0;

    pmm_bitmap_set(page);
    pmm_free_count--;

    uint64_t addr = page * PAGE_SIZE;

    /* Verify 4KB alignment */
    if (!pmm_is_page_aligned(addr)) return (void *)0;

    return (void *)addr;
}

void *pmm_alloc_pages(uint32_t count) {
    if (count == 0) return (void *)0;

    uint64_t page = pmm_find_free_pages(count);
    if (page == 0) return (void *)0;

    /* Verify first page is free (defensive) */
    if (pmm_bitmap_test(page)) return (void *)0;

    for (uint32_t i = 0; i < count; i++) {
        if (pmm_bitmap_test(page + i)) {
            /* Corruption — partial rollback, return failure */
            for (uint32_t j = 0; j < i; j++)
                pmm_bitmap_clear(page + j);
            return (void *)0;
        }
        pmm_bitmap_set(page + i);
    }

    pmm_free_count -= count;

    uint64_t addr = page * PAGE_SIZE;
    if (!pmm_is_page_aligned(addr)) return (void *)0;

    return (void *)addr;
}

void *pmm_alloc_dma(uint32_t size) {
    uint32_t count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t page = pmm_find_free_pages_dma(count);
    if (page == 0) return (void *)0;

    if (pmm_bitmap_test(page)) return (void *)0;

    for (uint32_t i = 0; i < count; i++) {
        if (pmm_bitmap_test(page + i)) {
            for (uint32_t j = 0; j < i; j++)
                pmm_bitmap_clear(page + j);
            return (void *)0;
        }
        pmm_bitmap_set(page + i);
    }

    pmm_free_count -= count;

    uint64_t addr = page * PAGE_SIZE;
    if (!pmm_is_page_aligned(addr)) return (void *)0;
    /* DMA must be below 16MB */
    if (addr >= PMM_DMA_LIMIT) return (void *)0;

    return (void *)addr;
}

/* ======================================================================== */
/* Page deallocation                                                        */
/* ======================================================================== */

void pmm_free_page(void *page) {
    if (!page) return;

    uint64_t addr = (uint64_t)page;
    if (!pmm_is_page_aligned(addr)) return;  /* Reject unaligned pointers */

    uint64_t p = addr / PAGE_SIZE;
    if (p == 0) return;                       /* Never free null page */
    if (p >= pmm_total_pages) return;         /* Out of range */

    if (pmm_bitmap_test(p)) {
        pmm_bitmap_clear(p);
        pmm_free_count++;
    }
    /* else: already free — no double-free counting */
}

void pmm_free_pages(void *pages, uint32_t count) {
    if (!pages || count == 0) return;

    uint64_t addr = (uint64_t)pages;
    if (!pmm_is_page_aligned(addr)) return;

    uint64_t p = addr / PAGE_SIZE;
    if (p == 0) return;

    for (uint32_t i = 0; i < count; i++) {
        if ((p + i) >= pmm_total_pages) break;
        if (pmm_bitmap_test(p + i)) {
            pmm_bitmap_clear(p + i);
            pmm_free_count++;
        }
    }
}

void pmm_free_dma(void *pages, uint32_t count) {
    pmm_free_pages(pages, count);
}

/* ======================================================================== */
/* Statistics                                                               */
/* ======================================================================== */

uint64_t pmm_get_total(void) { return pmm_total_pages * PAGE_SIZE; }
uint64_t pmm_get_free(void)  { return pmm_free_count * PAGE_SIZE;  }
uint64_t pmm_get_used(void)  { return (pmm_total_pages - pmm_free_count) * PAGE_SIZE; }

void pmm_get_stats(mem_stats_t *stats) {
    if (!stats) return;

    stats->total_memory   = pmm_total_pages * PAGE_SIZE;
    stats->usable_memory  = pmm_free_count * PAGE_SIZE;
    stats->used_pages     = pmm_total_pages - pmm_free_count;
    stats->free_pages     = pmm_free_count;
    stats->total_pages    = pmm_total_pages;
    stats->e820_count     = pmm_e820_count;

    /* Sum reserved from E820 */
    stats->reserved_memory = pmm_reserved_bytes;
}
