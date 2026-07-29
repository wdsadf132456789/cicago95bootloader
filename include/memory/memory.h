/**
 * Chicago-95 Physical Memory Manager (PMM) + Virtual Memory Manager (VMM)
 * Bitmap-based PMM for physical page allocation
 * Page table management for VMM with identity + higher-half mappings
 */

#ifndef CHICAGO_MEMORY_H
#define CHICAGO_MEMORY_H

#include <stdint.h>

#define PAGE_SIZE           4096
#define PAGE_SHIFT          12
#define PAGE_MASK           ~(PAGE_SIZE - 1)

/* E820 memory types */
#define E820_TYPE_USABLE        1
#define E820_TYPE_RESERVED      2
#define E820_TYPE_ACPI_RECLAIM  3
#define E820_TYPE_ACPI_NVS      4
#define E820_TYPE_BAD           5

/* Page flags */
#define PAGE_FLAG_PRESENT   (1ULL << 0)
#define PAGE_FLAG_RW        (1ULL << 1)
#define PAGE_FLAG_USER      (1ULL << 2)
#define PAGE_FLAG_WRITE_THR (1ULL << 3)
#define PAGE_FLAG_CACHE_DIS (1ULL << 4)
#define PAGE_FLAG_ACCESSED  (1ULL << 5)
#define PAGE_FLAG_DIRTY     (1ULL << 6)
#define PAGE_FLAG_HUGE      (1ULL << 7)
#define PAGE_FLAG_GLOBAL    (1ULL << 8)
#define PAGE_FLAG_NO_EXEC   (1ULL << 63)

/* Page table entry (64-bit) */
typedef struct {
    uint64_t present    : 1;
    uint64_t rw         : 1;
    uint64_t user       : 1;
    uint64_t write_thr  : 1;
    uint64_t cache_dis  : 1;
    uint64_t accessed   : 1;
    uint64_t dirty      : 1;
    uint64_t huge       : 1;
    uint64_t global     : 1;
    uint64_t avail      : 3;
    uint64_t frame      : 40;
    uint64_t avail2     : 7;
    uint64_t nx         : 1;
} __attribute__((packed)) page_entry_t;

/* Page table pointers (each is 512 entries) */
typedef page_entry_t page_table_t[512];

/* E820 memory map entry */
typedef struct {
    uint64_t base;
    uint64_t len;
    uint32_t type;
    uint32_t acpi_ext;
} __attribute__((packed)) mem_e820_entry_t;

/* PMM statistics */
typedef struct {
    uint64_t total_memory;
    uint64_t usable_memory;
    uint64_t reserved_memory;
    uint64_t used_pages;
    uint64_t free_pages;
    uint64_t total_pages;
    uint32_t e820_count;
} mem_stats_t;

/* ========================================================================
 * PMM: Physical Memory Manager
 * ======================================================================== */
int      pmm_init(mem_e820_entry_t *e820_map, uint32_t e820_count);
void    *pmm_alloc_page(void);
void    *pmm_alloc_pages(uint32_t count);
void     pmm_free_page(void *page);
void     pmm_free_pages(void *pages, uint32_t count);
void    *pmm_alloc_dma(uint32_t size);   /* DMA-safe (low 16MB) */
void     pmm_free_dma(void *pages, uint32_t count);
uint64_t pmm_get_total(void);
uint64_t pmm_get_free(void);
uint64_t pmm_get_used(void);
void     pmm_get_stats(mem_stats_t *stats);

/* ========================================================================
 * VMM: Virtual Memory Manager
 * ======================================================================== */
int      vmm_init(void);
int      vmm_map_page(uint64_t virtual, uint64_t physical, uint64_t flags);
int      vmm_unmap_page(uint64_t virtual);
int      vmm_map_pages(uint64_t virtual, uint64_t physical, uint32_t count, uint64_t flags);
int      vmm_unmap_pages(uint64_t virtual, uint32_t count);
int      vmm_map_range(uint64_t v_start, uint64_t p_start, uint64_t len, uint64_t flags);
uint64_t vmm_get_physical(uint64_t virtual);
int      vmm_set_flags(uint64_t virtual, uint64_t flags);
int      vmm_is_mapped(uint64_t virtual);
page_entry_t *vmm_get_page_entry(uint64_t virtual);

/* TLB management */
void     vmm_invalidate_page(uint64_t virtual);
void     vmm_invalidate_all(void);
void     vmm_flush_tlb(void);

/* Page table manipulation */
page_entry_t *vmm_get_pml4(void);
page_entry_t *vmm_get_pdpt(page_entry_t *pml4, uint64_t virtual, int create);
page_entry_t *vmm_get_pd(page_entry_t *pdpt, uint64_t virtual, int create);
page_entry_t *vmm_get_pt(page_entry_t *pd, uint64_t virtual, int create);

/* Context */
void     vmm_load_cr3(uint64_t pml4_phys);
uint64_t vmm_read_cr3(void);

/* Kernel address space helpers */
#define KERNEL_HIGHER_HALF  0xFFFF800000000000ULL
#define KERNEL_PHYS_OFFSET  0xFFFF800000000000ULL
#define KERNEL_BASE         0xFFFFFFFF80000000ULL

uint64_t vmm_kernel_virt_to_phys(uint64_t virt);
uint64_t vmm_kernel_phys_to_virt(uint64_t phys);

/* ========================================================================
 * Cluster VA Space — dedicated virtual address range for BrainFS clusters
 * Bypasses standard page faults: clusters are mapped fault-on-demand,
 * resolved lazily via vmm_cluster_resolve(). All cluster I/O executes
 * entirely in protected long mode without dropping to real mode.
 *
 * VA layout (each cluster gets a fixed PAGE_SIZE-aligned slot):
 *   CLUSTER_VA_BASE + (cluster * PAGE_SIZE)  ->  physical page
 *
 * On allocation: page table entry is PRESENT but PHYS=0 (trap page)
 * On first access: fault handler calls vmm_cluster_resolve() to
 *                  back the page with the real physical frame
 * ======================================================================== */
#define CLUSTER_VA_BASE         0x0000100000000000ULL
#define CLUSTER_VA_LIMIT        0x00007FFFFFFFFFFFULL
#define CLUSTER_VA_PER_CLUSTER  PAGE_SIZE

/* Init the cluster VA space (must be called after VMM + PMM init) */
int      vmm_cluster_space_init(void);

/* Get the virtual address backing a cluster number */
uint64_t vmm_cluster_virt(uint32_t cluster);

/* Get the physical address backing a cluster (0 if not mapped) */
uint64_t vmm_cluster_phys(uint32_t cluster);

/* Map a physical page to a cluster's virtual slot (non-faulting) */
int      vmm_cluster_map(uint32_t cluster, uint64_t physical);

/* Unmap a cluster's virtual slot */
int      vmm_cluster_unmap(uint32_t cluster);

/* Resolve a fault-on-demand page: backs the reserved entry with a real frame
 * Called from the page fault handler.  Returns the physical frame address. */
uint64_t vmm_cluster_resolve(uint32_t cluster);

/* Bulk resolve: pre-map a range of consecutive clusters */
int      vmm_cluster_resolve_range(uint32_t start_cluster, uint32_t count);

/* Check if a virtual address falls in the cluster VA space */
int      vmm_is_cluster_addr(uint64_t virtual);

/* Get cluster number from a cluster VA address */
uint32_t vmm_addr_to_cluster(uint64_t virtual);

/* Translate a physical address used by BrainFS to a safe virtual address */
uint64_t vmm_translate_phys_for_fs(uint64_t physical);

/* Translate a virtual address back to physical for DMA / disk I/O */
uint64_t vmm_translate_virt_for_fs(uint64_t virtual);

#endif /* CHICAGO_MEMORY_H */
