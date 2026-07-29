/**
 * Chicago-95 Virtual Memory Manager (VMM)
 * Page table management with identity + higher-half kernel mapping
 * PML4 -> PDPT -> PD -> PT hierarchy for 4-level paging
 */

#include "memory/memory.h"

static page_entry_t *vmm_pml4 = (page_entry_t *)0x1000;
static page_entry_t *vmm_kpdpt = (page_entry_t *)0x2000;
static page_entry_t *vmm_kpd   = (page_entry_t *)0x3000;

/* ---- Physical page allocator helpers (uses PMM) ---- */
extern void *pmm_alloc_page(void);
extern void  pmm_free_page(void *page);

/* ---- Page table entry helpers ---- */

static inline int pte_present(page_entry_t *pte) {
    return pte->present;
}

static inline uint64_t pte_frame(page_entry_t *pte) {
    return pte->frame << PAGE_SHIFT;
}

static inline void pte_set_frame(page_entry_t *pte, uint64_t phys) {
    pte->frame = (phys >> PAGE_SHIFT) & 0xFFFFFFFFF;
}

/* ---- Allocate a page table page ---- */

static page_entry_t *vmm_alloc_table(void) {
    page_entry_t *table = (page_entry_t *)pmm_alloc_page();
    if (!table) return (void *)0;
    for (int i = 0; i < 512; i++) {
        table[i].present = 0;
        table[i].frame = 0;
    }
    return table;
}

/* ---- Navigation (walk the page table hierarchy) ---- */

page_entry_t *vmm_get_pml4(void) {
    return vmm_pml4;
}

page_entry_t *vmm_get_pdpt(page_entry_t *pml4, uint64_t virtual, int create) {
    uint32_t idx = (virtual >> 39) & 0x1FF;
    if (pml4[idx].present) {
        return (page_entry_t *)((uint64_t)pml4[idx].frame << PAGE_SHIFT);
    }
    if (!create) return (void *)0;
    page_entry_t *pdpt = vmm_alloc_table();
    if (!pdpt) return (void *)0;
    pml4[idx].present = 1;
    pml4[idx].rw = 1;
    pml4[idx].user = 0;
    pte_set_frame(&pml4[idx], (uint64_t)pdpt);
    return pdpt;
}

page_entry_t *vmm_get_pd(page_entry_t *pdpt, uint64_t virtual, int create) {
    uint32_t idx = (virtual >> 30) & 0x1FF;
    if (pdpt[idx].present) {
        if (pdpt[idx].huge) return (page_entry_t *)0;
        return (page_entry_t *)((uint64_t)pdpt[idx].frame << PAGE_SHIFT);
    }
    if (!create) return (void *)0;
    page_entry_t *pd = vmm_alloc_table();
    if (!pd) return (void *)0;
    pdpt[idx].present = 1;
    pdpt[idx].rw = 1;
    pdpt[idx].user = 0;
    pte_set_frame(&pdpt[idx], (uint64_t)pd);
    return pd;
}

page_entry_t *vmm_get_pt(page_entry_t *pd, uint64_t virtual, int create) {
    uint32_t idx = (virtual >> 21) & 0x1FF;
    if (pd[idx].present) {
        if (pd[idx].huge) return (page_entry_t *)0;
        return (page_entry_t *)((uint64_t)pd[idx].frame << PAGE_SHIFT);
    }
    if (!create) return (void *)0;
    page_entry_t *pt = vmm_alloc_table();
    if (!pt) return (void *)0;
    pd[idx].present = 1;
    pd[idx].rw = 1;
    pd[idx].user = 0;
    pte_set_frame(&pd[idx], (uint64_t)pt);
    return pt;
}

/* ---- Init ---- */

int vmm_init(void) {
    page_entry_t *first_pt = vmm_alloc_table();
    if (!first_pt) return -1;

    vmm_kpd[0].huge = 0;
    vmm_kpd[0].present = 1;
    vmm_kpd[0].rw = 1;
    pte_set_frame(&vmm_kpd[0], (uint64_t)first_pt);

    for (int i = 0; i < 512; i++) {
        first_pt[i].present = 1;
        first_pt[i].rw = 1;
        first_pt[i].user = 0;
        first_pt[i].global = 1;
        pte_set_frame(&first_pt[i], (uint64_t)(i * PAGE_SIZE));
    }

    for (uint64_t addr = 0; addr < 0x100000000ULL; addr += 0x200000) {
        uint32_t pdpt_idx = (addr >> 39) & 0x1FF;
        uint32_t pd_idx   = (addr >> 30) & 0x1FF;

        if (pdpt_idx == 0 && pd_idx == 0) continue;

        page_entry_t *pdpt = vmm_get_pdpt(vmm_pml4, addr, 1);
        if (!pdpt) return -1;

        if (pdpt[pdpt_idx].present && !pdpt[pdpt_idx].huge) {
            page_entry_t *pd = (page_entry_t *)((uint64_t)pdpt[pdpt_idx].frame << PAGE_SHIFT);
            if (pd[pd_idx].huge) continue;
            page_entry_t *pt = vmm_alloc_table();
            if (!pt) return -1;
            for (int i = 0; i < 512; i++) {
                pt[i].present = 1;
                pt[i].rw = 1;
                pt[i].user = 0;
                pt[i].global = 1;
                pte_set_frame(&pt[i], addr + (uint64_t)i * PAGE_SIZE);
            }
            pd[pd_idx].huge = 0;
            pd[pd_idx].present = 1;
            pd[pd_idx].rw = 1;
            pte_set_frame(&pd[pd_idx], (uint64_t)pt);
        } else if (pdpt[pdpt_idx].huge) {
            continue;
        } else {
            page_entry_t *pd = vmm_alloc_table();
            if (!pd) return -1;
            pdpt[pdpt_idx].present = 1;
            pdpt[pdpt_idx].rw = 1;
            pte_set_frame(&pdpt[pdpt_idx], (uint64_t)pd);
            pd[pd_idx].present = 1;
            pd[pd_idx].rw = 1;
            pd[pd_idx].huge = 1;
            pd[pd_idx].global = 1;
            pte_set_frame(&pd[pd_idx], addr);
        }
    }

    uint64_t kernel_virt = KERNEL_BASE;
    uint64_t kernel_phys = 0;
    page_entry_t *kpdpt = vmm_get_pdpt(vmm_pml4, kernel_virt, 1);
    if (!kpdpt) return -1;
    uint32_t kpdpt_idx = (kernel_virt >> 39) & 0x1FF;

    page_entry_t *kpd = (page_entry_t *)((uint64_t)kpdpt[kpdpt_idx].frame << PAGE_SHIFT);
    if (!kpdpt[kpdpt_idx].present) {
        kpd = vmm_alloc_table();
        if (!kpd) return -1;
        kpdpt[kpdpt_idx].present = 1;
        kpdpt[kpdpt_idx].rw = 1;
        pte_set_frame(&kpdpt[kpdpt_idx], (uint64_t)kpd);
    }

    uint32_t kpd_idx = (kernel_virt >> 30) & 0x1FF;
    page_entry_t *kpt = vmm_alloc_table();
    if (!kpt) return -1;
    for (int i = 0; i < 512; i++) {
        kpt[i].present = 1;
        kpt[i].rw = 1;
        kpt[i].user = 0;
        kpt[i].global = 1;
        pte_set_frame(&kpt[i], kernel_phys + (uint64_t)i * PAGE_SIZE);
    }
    kpd[kpd_idx].present = 1;
    kpd[kpd_idx].rw = 1;
    kpd[kpd_idx].huge = 0;
    pte_set_frame(&kpd[kpd_idx], (uint64_t)kpt);

    vmm_flush_tlb();
    return 0;
}

/* ---- Map / Unmap ---- */

int vmm_map_page(uint64_t virtual, uint64_t physical, uint64_t flags) {
    page_entry_t *pdpt = vmm_get_pdpt(vmm_pml4, virtual, 1);
    if (!pdpt) return -1;

    page_entry_t *pd = vmm_get_pd(pdpt, virtual, 1);
    if (!pd) return -1;

    page_entry_t *pt = vmm_get_pt(pd, virtual, 1);
    if (!pt) return -1;

    uint32_t pt_idx = (virtual >> 12) & 0x1FF;

    if (pt[pt_idx].present) return -2;

    pt[pt_idx].present = (flags & PAGE_FLAG_PRESENT) ? 1 : 0;
    pt[pt_idx].rw      = (flags & PAGE_FLAG_RW) ? 1 : 0;
    pt[pt_idx].user    = (flags & PAGE_FLAG_USER) ? 1 : 0;
    pt[pt_idx].write_thr = (flags & PAGE_FLAG_WRITE_THR) ? 1 : 0;
    pt[pt_idx].cache_dis = (flags & PAGE_FLAG_CACHE_DIS) ? 1 : 0;
    pt[pt_idx].global  = (flags & PAGE_FLAG_GLOBAL) ? 1 : 0;
    pt[pt_idx].nx      = (flags & PAGE_FLAG_NO_EXEC) ? 1 : 0;
    pte_set_frame(&pt[pt_idx], physical);

    vmm_invalidate_page(virtual);
    return 0;
}

int vmm_unmap_page(uint64_t virtual) {
    page_entry_t *pdpt = vmm_get_pdpt(vmm_pml4, virtual, 0);
    if (!pdpt) return -1;

    page_entry_t *pd = vmm_get_pd(pdpt, virtual, 0);
    if (!pd) return -1;

    page_entry_t *pt = vmm_get_pt(pd, virtual, 0);
    if (!pt) return -1;

    uint32_t pt_idx = (virtual >> 12) & 0x1FF;
    pt[pt_idx].present = 0;
    pt[pt_idx].frame = 0;

    vmm_invalidate_page(virtual);
    return 0;
}

int vmm_map_pages(uint64_t virtual, uint64_t physical, uint32_t count, uint64_t flags) {
    for (uint32_t i = 0; i < count; i++) {
        int ret = vmm_map_page(virtual + i * PAGE_SIZE, physical + i * PAGE_SIZE, flags);
        if (ret != 0) return ret;
    }
    return 0;
}

int vmm_unmap_pages(uint64_t virtual, uint32_t count) {
    for (uint32_t i = 0; i < count; i++)
        vmm_unmap_page(virtual + i * PAGE_SIZE);
    return 0;
}

int vmm_map_range(uint64_t v_start, uint64_t p_start, uint64_t len, uint64_t flags) {
    uint32_t count = (len + PAGE_SIZE - 1) / PAGE_SIZE;
    return vmm_map_pages(v_start, p_start, count, flags);
}

/* ---- Query ---- */

uint64_t vmm_get_physical(uint64_t virtual) {
    page_entry_t *pdpt = vmm_get_pdpt(vmm_pml4, virtual, 0);
    if (!pdpt) return 0;

    uint32_t pdpt_idx = (virtual >> 39) & 0x1FF;

    if (pdpt[pdpt_idx].huge)
        return (pdpt[pdpt_idx].frame << PAGE_SHIFT) | (virtual & 0x3FFFFFFF);

    page_entry_t *pd = vmm_get_pd(pdpt, virtual, 0);
    if (!pd) return 0;

    uint32_t pd_idx = (virtual >> 30) & 0x1FF;

    if (pd[pd_idx].huge)
        return (pd[pd_idx].frame << PAGE_SHIFT) | (virtual & 0x1FFFFF);

    page_entry_t *pt = vmm_get_pt(pd, virtual, 0);
    if (!pt) return 0;

    uint32_t pt_idx = (virtual >> 12) & 0x1FF;
    return (pt[pt_idx].frame << PAGE_SHIFT) | (virtual & 0xFFF);
}

int vmm_is_mapped(uint64_t virtual) {
    page_entry_t *entry = vmm_get_page_entry(virtual);
    return entry && entry->present;
}

page_entry_t *vmm_get_page_entry(uint64_t virtual) {
    page_entry_t *pdpt = vmm_get_pdpt(vmm_pml4, virtual, 0);
    if (!pdpt) return (void *)0;

    page_entry_t *pd = vmm_get_pd(pdpt, virtual, 0);
    if (!pd) return (void *)0;

    page_entry_t *pt = vmm_get_pt(pd, virtual, 0);
    if (!pt) return (void *)0;

    uint32_t pt_idx = (virtual >> 12) & 0x1FF;
    return &pt[pt_idx];
}

int vmm_set_flags(uint64_t virtual, uint64_t flags) {
    page_entry_t *entry = vmm_get_page_entry(virtual);
    if (!entry || !entry->present) return -1;

    entry->rw        = (flags & PAGE_FLAG_RW) ? 1 : 0;
    entry->user      = (flags & PAGE_FLAG_USER) ? 1 : 0;
    entry->write_thr = (flags & PAGE_FLAG_WRITE_THR) ? 1 : 0;
    entry->cache_dis = (flags & PAGE_FLAG_CACHE_DIS) ? 1 : 0;
    entry->nx        = (flags & PAGE_FLAG_NO_EXEC) ? 1 : 0;

    vmm_invalidate_page(virtual);
    return 0;
}

/* ---- TLB ---- */

void vmm_invalidate_page(uint64_t virtual) {
    asm volatile("invlpg (%0)" : : "r"(virtual) : "memory");
}

void vmm_invalidate_all(void) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" : : "r"(cr3));
}

void vmm_flush_tlb(void) {
    vmm_invalidate_all();
}

/* ---- CR3 ---- */

void vmm_load_cr3(uint64_t pml4_phys) {
    asm volatile("mov %0, %%cr3" : : "r"(pml4_phys));
}

uint64_t vmm_read_cr3(void) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/* ---- Kernel address helpers ---- */

uint64_t vmm_kernel_virt_to_phys(uint64_t virt) {
    return virt - KERNEL_BASE + KERNEL_PHYS_OFFSET;
}

uint64_t vmm_kernel_phys_to_virt(uint64_t phys) {
    return phys - KERNEL_PHYS_OFFSET + KERNEL_BASE;
}

/* ========================================================================
 * Cluster VA Space
 *
 * Dedicated virtual region CLUSTER_VA_BASE..CLUSTER_VA_LIMIT for BrainFS.
 * Each cluster number maps to exactly one PAGE_SIZE-aligned virtual slot.
 *
 * Allocation strategy:
 *   - On vmm_cluster_map(): a PRESENT + RW page entry is created pointing
 *     at the physical frame.  No page fault will ever occur.
 *   - On vmm_cluster_space_init(): a thin PDPT/PD layer is created but
 *     NO page tables are allocated.  When the kernel first touches a
 *     cluster VA, the entry is absent -> page fault -> the resolve path
 *     backs it.  This avoids allocating page tables for clusters that
 *     are never touched.
 *   - vmm_cluster_resolve() is the lazy path: it checks if the cluster
 *     already has a frame, and if not allocates one and maps it.  The
 *     calling code (BrainFS) never sees a page fault -- it is resolved
 *     inside the fault handler and execution resumes transparently.
 * ======================================================================== */

static int     cluster_space_initialized = 0;

/* Per-cluster physical frame tracking (for resolve + unmap) */
#define CLUSTER_MAP_MAX     (1 << 20)   /* up to 1M clusters tracked */
static uint64_t cluster_phys_map[CLUSTER_MAP_MAX]; /* phys frame per cluster, 0 = unmapped */
static uint8_t  cluster_present[CLUSTER_MAP_MAX];   /* 1 = page table entry exists */

int vmm_cluster_space_init(void) {
    for (uint32_t i = 0; i < CLUSTER_MAP_MAX; i++) {
        cluster_phys_map[i] = 0;
        cluster_present[i]  = 0;
    }
    cluster_space_initialized = 1;
    return 0;
}

/* ---- Helpers ---- */

uint64_t vmm_cluster_virt(uint32_t cluster) {
    return CLUSTER_VA_BASE + (uint64_t)cluster * CLUSTER_VA_PER_CLUSTER;
}

uint64_t vmm_cluster_phys(uint32_t cluster) {
    if (cluster >= CLUSTER_MAP_MAX) return 0;
    return cluster_phys_map[cluster];
}

int vmm_is_cluster_addr(uint64_t virtual) {
    return virtual >= CLUSTER_VA_BASE && virtual < CLUSTER_VA_LIMIT;
}

uint32_t vmm_addr_to_cluster(uint64_t virtual) {
    if (!vmm_is_cluster_addr(virtual)) return 0;
    return (uint32_t)((virtual - CLUSTER_VA_BASE) / CLUSTER_VA_PER_CLUSTER);
}

/* ---- Internal: walk/create the intermediate page table layers for a cluster VA
 * We only create the PML4 -> PDPT -> PD chain.  The PT is NOT allocated here;
 * the page entry will either be absent (for lazy resolve) or present (for
 * direct map).  This avoids page table allocation overhead for the lazy path. */

static int cluster_ensure_pd(uint64_t virt, page_entry_t **out_pd, uint32_t *out_pd_idx) {
    page_entry_t *pdpt = vmm_get_pdpt(vmm_pml4, virt, 1);
    if (!pdpt) return -1;
    page_entry_t *pd = vmm_get_pd(pdpt, virt, 1);
    if (!pd) return -1;
    *out_pd = pd;
    *out_pd_idx = (virt >> 21) & 0x1FF;
    return 0;
}

/* ---- Map a physical page directly into a cluster slot (no fault possible) ---- */

int vmm_cluster_map(uint32_t cluster, uint64_t physical) {
    if (cluster >= CLUSTER_MAP_MAX) return -1;

    uint64_t virt = vmm_cluster_virt(cluster);

    page_entry_t *pd;
    uint32_t pd_idx;
    if (cluster_ensure_pd(virt, &pd, &pd_idx) < 0) return -1;

    if (pd[pd_idx].huge && pd[pd_idx].present) {
        page_entry_t *new_pt = vmm_alloc_table();
        if (!new_pt) return -1;

        uint64_t old_phys_base = pd[pd_idx].frame << 21;
        for (int i = 0; i < 512; i++) {
            new_pt[i].present = 1;
            new_pt[i].rw      = 1;
            new_pt[i].user    = 0;
            new_pt[i].global  = 1;
            pte_set_frame(&new_pt[i], old_phys_base + (uint64_t)i * PAGE_SIZE);
        }
        pd[pd_idx].huge = 0;
        pte_set_frame(&pd[pd_idx], (uint64_t)new_pt);
    }

    if (!pd[pd_idx].present) {
        page_entry_t *pt = vmm_alloc_table();
        if (!pt) return -1;
        pd[pd_idx].present = 1;
        pd[pd_idx].rw      = 1;
        pd[pd_idx].user    = 0;
        pte_set_frame(&pd[pd_idx], (uint64_t)pt);
    }

    page_entry_t *pt = (page_entry_t *)((uint64_t)pd[pd_idx].frame << PAGE_SHIFT);
    uint32_t pt_idx = (virt >> 12) & 0x1FF;

    pt[pt_idx].present  = 1;
    pt[pt_idx].rw       = 1;
    pt[pt_idx].user     = 0;
    pt[pt_idx].global   = 1;
    pt[pt_idx].cache_dis = 0;
    pt[pt_idx].nx       = 0;
    pte_set_frame(&pt[pt_idx], physical);

    cluster_phys_map[cluster] = physical;
    cluster_present[cluster]  = 1;

    vmm_invalidate_page(virt);
    return 0;
}

/* ---- Unmap a cluster slot ---- */

int vmm_cluster_unmap(uint32_t cluster) {
    if (cluster >= CLUSTER_MAP_MAX || !cluster_present[cluster]) return -1;

    uint64_t virt = vmm_cluster_virt(cluster);

    page_entry_t *pdpt = vmm_get_pdpt(vmm_pml4, virt, 0);
    if (!pdpt) return -1;
    page_entry_t *pd = vmm_get_pd(pdpt, virt, 0);
    if (!pd) return -1;
    if (pd[(virt >> 21) & 0x1FF].huge) return -1;
    page_entry_t *pt = vmm_get_pt(pd, virt, 0);
    if (!pt) return -1;

    uint32_t pt_idx = (virt >> 12) & 0x1FF;
    pt[pt_idx].present = 0;
    pt[pt_idx].frame   = 0;

    cluster_phys_map[cluster] = 0;
    cluster_present[cluster]  = 0;

    vmm_invalidate_page(virt);
    return 0;
}

/* ---- Resolve a fault-on-demand page (lazy backing) ----
 *
 * Called by the page fault handler when a cluster VA is accessed but
 * the page table entry is not present.  If the cluster has a physical
 * frame allocated, it maps it.  If not, it allocates a fresh zero page
 * and maps that.
 *
 * Returns the physical frame address that was mapped (0 on error).
 */

uint64_t vmm_cluster_resolve(uint32_t cluster) {
    if (cluster >= CLUSTER_MAP_MAX) return 0;

    uint64_t existing_phys = cluster_phys_map[cluster];

    if (existing_phys) {
        uint64_t virt = vmm_cluster_virt(cluster);
        page_entry_t *entry = vmm_get_page_entry(virt);
        if (entry && !entry->present) {
            entry->present = 1;
            entry->rw      = 1;
            entry->global  = 1;
            vmm_invalidate_page(virt);
        }
        return existing_phys;
    }

    void *page = pmm_alloc_page();
    if (!page) return 0;
    uint64_t phys = (uint64_t)page;

    uint8_t *p = (uint8_t *)phys;
    for (uint32_t i = 0; i < PAGE_SIZE; i++) p[i] = 0;

    if (vmm_cluster_map(cluster, phys) < 0) {
        pmm_free_page(page);
        return 0;
    }

    return phys;
}

/* ---- Bulk resolve a range of consecutive clusters ---- */

int vmm_cluster_resolve_range(uint32_t start_cluster, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (vmm_cluster_resolve(start_cluster + i) == 0) return -1;
    }
    return 0;
}

/* ---- Translation hooks for BrainFS ----
 *
 * BrainFS operates with sector addresses and cluster numbers.
 * These helpers translate between raw physical addresses and the
 * cluster VA space, so all cluster I/O goes through the VMM.
 */

uint64_t vmm_translate_phys_for_fs(uint64_t physical) {
    if (physical < 0x100000000ULL) {
        return physical;
    }
    return vmm_kernel_phys_to_virt(physical);
}

uint64_t vmm_translate_virt_for_fs(uint64_t virtual) {
    if (vmm_is_cluster_addr(virtual)) {
        uint32_t cluster = vmm_addr_to_cluster(virtual);
        return cluster_phys_map[cluster];
    }
    return vmm_get_physical(virtual);
}
