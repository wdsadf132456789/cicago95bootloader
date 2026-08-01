/**
 * Chicago-95 Kernel Memory Allocator
 *
 * Three-tier allocation system:
 *   1. PMM: Physical page bitmap allocator (E820-aware)
 *   2. kmalloc: General heap (first-fit linked list with coalescing)
 *   3. slab: Fixed-size object cache (16, 32, 64, 128, 256 byte slabs)
 *   4. fcache: BrainFS cluster file cache (LRU, 256 entries)
 */

#include "kmalloc.h"
#include "kernel.h"
#include "ata.h"

/* ======================================================================== */
/* Physical Page Manager (PMM)                                               */
/* ======================================================================== */

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi;
} __attribute__((packed)) e820_entry_t;

#define E820_MAP       ((e820_entry_t *)0x8000)
#define E820_AVAIL     1
#define E820_MAX       256
#define PMM_MAX_PAGES  (64ULL * 1024 * 1024 * 1024 / PAGE_SIZE)  /* 64GB */

static uint8_t pmm_bitmap[PMM_MAX_PAGES / 8] __attribute__((aligned(16)));
static uint64_t pmm_total_pages = 0;
static uint64_t pmm_used_pages = 0;
static uint64_t pmm_mem_start = 0x100000;
static uint64_t pmm_mem_end = 0;

static inline void bm_set(uint64_t bit) { pmm_bitmap[bit / 8] |= (1 << (bit % 8)); }
static inline void bm_clear(uint64_t bit) { pmm_bitmap[bit / 8] &= ~(1 << (bit % 8)); }
static inline int bm_test(uint64_t bit) { return pmm_bitmap[bit / 8] & (1 << (bit % 8)); }

/* Find N contiguous free pages */
static uint64_t pmm_find_contiguous(uint64_t count) {
    uint64_t run = 0;
    uint64_t start = 0;
    for (uint64_t i = 0; i < pmm_total_pages; i++) {
        if (!bm_test(i)) {
            if (run == 0) start = i;
            run++;
            if (run >= count) return start;
        } else {
            run = 0;
        }
    }
    return (uint64_t)-1;
}

void pmm_init(void) {
    e820_entry_t *e820 = E820_MAP;
    uint32_t count = 0;

    /* Find memory extent */
    while (count < E820_MAX) {
        if (e820[count].base == 0 && e820[count].length == 0) break;
        uint64_t end = e820[count].base + e820[count].length;
        if (end > pmm_mem_end) pmm_mem_end = end;
        count++;
    }

    pmm_total_pages = pmm_mem_end / PAGE_SIZE;
    if (pmm_total_pages > PMM_MAX_PAGES) pmm_total_pages = PMM_MAX_PAGES;

    /* Mark all as used (bitmap bit = 1), then clear available regions */
    __builtin_memset(pmm_bitmap, 0xFF, sizeof(pmm_bitmap));

    for (uint32_t i = 0; i < count; i++) {
        if (e820[i].type != E820_AVAIL) continue;
        uint64_t start = e820[i].base;
        uint64_t len = e820[i].length;

        /* Clamp below pmm_mem_start */
        if (start < pmm_mem_start) {
            if (start + len <= pmm_mem_start) continue;
            len -= pmm_mem_start - start;
            start = pmm_mem_start;
        }

        uint64_t pg_start = start / PAGE_SIZE;
        uint64_t pg_count = len / PAGE_SIZE;
        for (uint64_t j = 0; j < pg_count; j++) {
            if (pg_start + j < pmm_total_pages)
                bm_clear(pg_start + j);
        }
    }

    /* Mark BIOS/kernel reserved regions as used (below pmm_mem_start) */
    for (uint64_t i = 0; i < pmm_mem_start / PAGE_SIZE && i < pmm_total_pages; i++)
        bm_set(i);

    /* Count used pages */
    pmm_used_pages = 0;
    for (uint64_t i = 0; i < pmm_total_pages; i++)
        if (bm_test(i)) pmm_used_pages++;
}

void *pmm_alloc_page(void) {
    for (uint64_t i = 0; i < pmm_total_pages; i++) {
        if (!bm_test(i)) {
            bm_set(i);
            pmm_used_pages++;
            return (void *)(i * PAGE_SIZE);
        }
    }
    return 0;
}

void *pmm_alloc_pages(size_t count) {
    if (count == 0) return 0;
    if (count == 1) return pmm_alloc_page();

    uint64_t start = pmm_find_contiguous(count);
    if (start == (uint64_t)-1) return 0;

    for (uint64_t i = 0; i < count; i++) {
        bm_set(start + i);
        pmm_used_pages++;
    }
    return (void *)(start * PAGE_SIZE);
}

void pmm_free_page(void *addr) {
    uint64_t pg = (uint64_t)addr / PAGE_SIZE;
    if (pg < pmm_total_pages && bm_test(pg)) {
        bm_clear(pg);
        pmm_used_pages--;
    }
}

void pmm_free_pages(void *addr, size_t count) {
    uint64_t pg = (uint64_t)addr / PAGE_SIZE;
    for (size_t i = 0; i < count; i++) {
        if (pg + i < pmm_total_pages && bm_test(pg + i)) {
            bm_clear(pg + i);
            pmm_used_pages--;
        }
    }
}

uint64_t pmm_get_free_pages(void) { return pmm_total_pages - pmm_used_pages; }
uint64_t pmm_get_total_pages(void) { return pmm_total_pages; }

/* ======================================================================== */
/* Kernel Heap (first-fit linked list with block splitting & coalescing)     */
/* ======================================================================== */

#define HEAP_MAGIC 0xDEADBEEFCAFE0000ULL

typedef struct block_header {
    uint64_t magic;             /* Corruption detection */
    size_t   size;              /* Usable size (excluding header) */
    int      free;              /* 1 = free, 0 = allocated */
    struct block_header *next;
    struct block_header *prev;
} __attribute__((aligned(16))) block_header_t;

static block_header_t *heap_head = 0;
static size_t heap_used = 0;
static size_t heap_total = 0;

void kmalloc_init(void) {
    heap_head = 0;
    heap_used = 0;
    heap_total = 0;
}

static block_header_t *heap_grow(size_t size) {
    size_t pages = (size + sizeof(block_header_t) + PAGE_SIZE - 1) / PAGE_SIZE;
    void *mem = pmm_alloc_pages(pages);
    if (!mem) return 0;

    block_header_t *block = (block_header_t *)mem;
    block->magic = HEAP_MAGIC;
    block->size = pages * PAGE_SIZE - sizeof(block_header_t);
    block->free = 1;
    block->next = heap_head;
    block->prev = 0;
    if (heap_head) heap_head->prev = block;
    heap_head = block;
    heap_total += pages * PAGE_SIZE;

    return block;
}

void *kmalloc(size_t size) {
    if (size == 0) return 0;
    size = (size + 15) & ~15;  /* 16-byte alignment */

    /* First-fit search */
    block_header_t *cur = heap_head;
    while (cur) {
        if (cur->magic != HEAP_MAGIC) break;  /* corruption */
        if (cur->free && cur->size >= size) {
            /* Split if remainder is large enough for a block */
            if (cur->size >= size + sizeof(block_header_t) + 32) {
                block_header_t *split = (block_header_t *)((uint8_t *)cur + sizeof(block_header_t) + size);
                split->magic = HEAP_MAGIC;
                split->size = cur->size - size - sizeof(block_header_t);
                split->free = 1;
                split->next = cur->next;
                split->prev = cur;
                if (cur->next) cur->next->prev = split;
                cur->next = split;
                cur->size = size;
            }
            cur->free = 0;
            heap_used += size;
            return (void *)((uint8_t *)cur + sizeof(block_header_t));
        }
        cur = cur->next;
    }

    /* No fit found — grow heap */
    block_header_t *block = heap_grow(size);
    if (!block) return 0;
    block->free = 0;
    heap_used += size;
    return (void *)((uint8_t *)block + sizeof(block_header_t));
}

void *kmalloc_aligned(size_t size) {
    /* Allocate extra page + header to guarantee alignment */
    void *raw = kmalloc(size + PAGE_SIZE + sizeof(block_header_t));
    if (!raw) return 0;
    uint64_t addr = ((uint64_t)raw + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    return (void *)addr;
}

void *kmalloc_zero(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        uint8_t *p = (uint8_t *)ptr;
        for (size_t i = 0; i < size; i++) p[i] = 0;
    }
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;
    block_header_t *block = (block_header_t *)((uint8_t *)ptr - sizeof(block_header_t));
    if (block->magic != HEAP_MAGIC) return;  /* corruption check */
    if (block->free) return;  /* double free */

    block->free = 1;
    heap_used -= block->size;

    /* Coalesce adjacent free blocks (forward + backward) */
    block_header_t *cur = heap_head;
    while (cur) {
        if (cur->magic != HEAP_MAGIC) break;
        if (cur->free && cur->next && cur->next->free) {
            cur->size += sizeof(block_header_t) + cur->next->size;
            cur->next = cur->next->next;
            if (cur->next) cur->next->prev = cur;
        } else {
            cur = cur->next;
        }
    }
}

size_t kheap_used(void) { return heap_used; }
size_t kheap_total(void) { return heap_total; }

/* ======================================================================== */
/* Slab Allocator (fixed-size object pools)                                  */
/* ======================================================================== */

#define SLAB_SIZES     5
#define SLAB_PAGE_OBJECTS 64

static const size_t slab_sizes[SLAB_SIZES] = { 16, 32, 64, 128, 256 };

typedef struct slab_page {
    struct slab_page *next;
    uint32_t slab_class;        /* Index into slab_sizes[] */
    uint32_t total;
    uint32_t used;
    uint64_t bitmap[SLAB_PAGE_OBJECTS / 64];  /* Free bitmap */
    uint8_t  objects[];         /* Object storage */
} __attribute__((aligned(16))) slab_page_t;

static slab_page_t *slab_pages[SLAB_SIZES] = {0};

void slab_init(void) {
    for (int i = 0; i < SLAB_SIZES; i++) slab_pages[i] = 0;
}

static slab_page_t *slab_new_page(int class_idx) {
    slab_page_t *page = (slab_page_t *)pmm_alloc_page();
    if (!page) return 0;

    page->next = slab_pages[class_idx];
    page->slab_class = class_idx;
    page->total = (PAGE_SIZE - sizeof(slab_page_t)) / slab_sizes[class_idx];
    if (page->total > SLAB_PAGE_OBJECTS) page->total = SLAB_PAGE_OBJECTS;
    page->used = 0;
    __builtin_memset(page->bitmap, 0, sizeof(page->bitmap));

    slab_pages[class_idx] = page;
    return page;
}

void *slab_alloc(size_t size) {
    /* Find smallest slab class that fits */
    int class_idx = -1;
    for (int i = 0; i < SLAB_SIZES; i++) {
        if (slab_sizes[i] >= size) { class_idx = i; break; }
    }
    if (class_idx < 0) return kmalloc(size);  /* Too large for slab */

    /* Find a page with free objects */
    slab_page_t *page = slab_pages[class_idx];
    while (page) {
        if (page->used < page->total) break;
        page = page->next;
    }
    if (!page) page = slab_new_page(class_idx);
    if (!page) return 0;

    /* Find first free bit in bitmap */
    for (uint32_t w = 0; w < (page->total + 63) / 64; w++) {
        if (page->bitmap[w] != ~0ULL) {
            for (int b = 0; b < 64; b++) {
                uint64_t mask = 1ULL << b;
                uint32_t idx = w * 64 + b;
                if (idx >= page->total) break;
                if (!(page->bitmap[w] & mask)) {
                    page->bitmap[w] |= mask;
                    page->used++;
                    return &page->objects[idx * slab_sizes[class_idx]];
                }
            }
        }
    }
    return 0;
}

void slab_free(void *ptr) {
    if (!ptr) return;

    /* Find which slab page this pointer belongs to */
    for (int c = 0; c < SLAB_SIZES; c++) {
        slab_page_t *page = slab_pages[c];
        while (page) {
            uint64_t start = (uint64_t)&page->objects[0];
            uint64_t end = start + page->total * slab_sizes[c];
            if ((uint64_t)ptr >= start && (uint64_t)ptr < end) {
                uint32_t idx = ((uint64_t)ptr - start) / slab_sizes[c];
                uint32_t w = idx / 64;
                uint32_t b = idx % 64;
                page->bitmap[w] &= ~(1ULL << b);
                page->used--;
                return;
            }
            page = page->next;
        }
    }
    /* Not in any slab — fall through to heap free */
    kfree(ptr);
}

/* ======================================================================== */
/* File Cache (BrainFS cluster caching, 256-entry LRU)                       */
/* ======================================================================== */

#define FCACHE_ENTRIES  256
#define CLUSTER_SIZE    4096

typedef struct {
    uint32_t cluster;           /* BrainFS cluster number */
    uint32_t age;               /* LRU counter */
    uint8_t  dirty;             /* Modified flag */
    uint8_t  valid;             /* Entry in use */
    uint8_t  data[CLUSTER_SIZE];
} __attribute__((aligned(16))) fcache_entry_t;

static fcache_entry_t fcache[FCACHE_ENTRIES];
static uint32_t fcache_clock = 0;
static uint32_t fcache_hits = 0;
static uint32_t fcache_misses = 0;

void fcache_init(void) {
    for (int i = 0; i < FCACHE_ENTRIES; i++) {
        fcache[i].valid = 0;
        fcache[i].cluster = 0;
        fcache[i].age = 0;
        fcache[i].dirty = 0;
    }
    fcache_clock = 0;
}

void *fcache_get(uint32_t cluster) {
    /* Search existing entries */
    for (int i = 0; i < FCACHE_ENTRIES; i++) {
        if (fcache[i].valid && fcache[i].cluster == cluster) {
            fcache[i].age = ++fcache_clock;
            fcache_hits++;
            return fcache[i].data;
        }
    }

    /* Not found — evict LRU */
    fcache_misses++;
    int lru_idx = 0;
    uint32_t lru_age = ~0U;
    for (int i = 0; i < FCACHE_ENTRIES; i++) {
        if (!fcache[i].valid) { lru_idx = i; break; }
        if (fcache[i].age < lru_age) {
            lru_age = fcache[i].age;
            lru_idx = i;
        }
    }

    /* Write back dirty entry before eviction */
    if (fcache[lru_idx].valid && fcache[lru_idx].dirty) {
        uint64_t old_lba = (uint64_t)fcache[lru_idx].cluster * (CLUSTER_SIZE / 512);
        ata_write_sectors(0, (uint32_t)old_lba, CLUSTER_SIZE / 512, (uint16_t *)fcache[lru_idx].data);
    }
    fcache[lru_idx].valid = 1;
    fcache[lru_idx].cluster = cluster;
    fcache[lru_idx].age = ++fcache_clock;
    fcache[lru_idx].dirty = 0;

    /* Read cluster data from disk (cluster -> sector mapping: cluster * (CLUSTER_SIZE / 512)) */
    uint64_t lba = (uint64_t)cluster * (CLUSTER_SIZE / 512);
    int res = ata_read_sectors(0, (uint32_t)lba, CLUSTER_SIZE / 512, (uint16_t *)fcache[lru_idx].data);
    if (res < 0) {
        __builtin_memset(fcache[lru_idx].data, 0, CLUSTER_SIZE);
    }

    return fcache[lru_idx].data;
}

void fcache_invalidate(uint32_t cluster) {
    for (int i = 0; i < FCACHE_ENTRIES; i++) {
        if (fcache[i].valid && fcache[i].cluster == cluster) {
            if (fcache[i].dirty) {
                uint64_t lba = (uint64_t)cluster * (CLUSTER_SIZE / 512);
                ata_write_sectors(0, (uint32_t)lba, CLUSTER_SIZE / 512, (uint16_t *)fcache[i].data);
            }
            fcache[i].valid = 0;
            return;
        }
    }
}

void fcache_flush(void) {
    for (int i = 0; i < FCACHE_ENTRIES; i++) {
        if (fcache[i].valid && fcache[i].dirty) {
            uint64_t lba = (uint64_t)fcache[i].cluster * (CLUSTER_SIZE / 512);
            ata_write_sectors(0, (uint32_t)lba, CLUSTER_SIZE / 512, (uint16_t *)fcache[i].data);
            fcache[i].dirty = 0;
        }
    }
}

void fcache_invalidate_all(void) {
    fcache_flush();
    for (int i = 0; i < FCACHE_ENTRIES; i++)
        fcache[i].valid = 0;
}

uint32_t fcache_count(void) {
    uint32_t c = 0;
    for (int i = 0; i < FCACHE_ENTRIES; i++)
        if (fcache[i].valid) c++;
    return c;
}
