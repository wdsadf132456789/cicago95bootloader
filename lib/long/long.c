/**
 * Chicago-95 Long Mode Utility Library
 * 64-bit helpers: 4-level page tables, SYSCALL, MSR, SMP, atomics
 *
 * All functions use native x86_64 instructions. Compiled as 64-bit code.
 */

#include <stdint.h>

/* ---- Page table structures ---- */

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
} __attribute__((packed)) lm_page_entry_t;

typedef lm_page_entry_t lm_page_table_t[512];

/* Page table level addresses (fixed physical locations) */
#define LM_PML4_ADDR    0x1000
#define LM_PDPT_ADDR    0x2000
#define LM_PD_ADDR      0x3000

/* ---- Page table helpers ---- */

static inline lm_page_entry_t *lm_get_entry(lm_page_table_t *table, uint64_t index) {
    return &(*table)[index & 0x1FF];
}

int lm_map_2mb(uint64_t *pdpt, uint64_t phys, uint64_t virt, uint64_t flags) {
    if (!pdpt) return -1;

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;

    /* Get PML4 */
    lm_page_entry_t *pml4 = (lm_page_entry_t *)pdpt;

    /* Ensure PML4[entry] -> PDPT */
    if (!pml4[pml4_idx].present) {
        uint64_t new_pdpt = 0;
        asm volatile("mov %%cr3, %0" : "=r"(new_pdpt));
        /* Allocate a page for PDPT from PMM - simplified: use fixed address */
        new_pdpt = LM_PDPT_ADDR;
        pml4[pml4_idx].frame = new_pdpt >> 12;
        pml4[pml4_idx].present = 1;
        pml4[pml4_idx].rw = (flags >> 1) & 1;
        pml4[pml4_idx].user = (flags >> 2) & 1;
    }

    lm_page_entry_t *pdpt_table = (lm_page_entry_t *)(pml4[pml4_idx].frame << 12);

    /* Ensure PDPT[entry] -> PD */
    if (!pdpt_table[pdpt_idx].present) {
        uint64_t new_pd = LM_PD_ADDR;
        pdpt_table[pdpt_idx].frame = new_pd >> 12;
        pdpt_table[pdpt_idx].present = 1;
        pdpt_table[pdpt_idx].rw = (flags >> 1) & 1;
        pdpt_table[pdpt_idx].user = (flags >> 2) & 1;
    }

    lm_page_entry_t *pd = (lm_page_entry_t *)(pdpt_table[pdpt_idx].frame << 12);

    /* Map 2MB huge page */
    pd[pd_idx].frame = phys >> 12;
    pd[pd_idx].present = 1;
    pd[pd_idx].rw = (flags >> 1) & 1;
    pd[pd_idx].user = (flags >> 2) & 1;
    pd[pd_idx].huge = 1;
    pd[pd_idx].global = (flags >> 8) & 1;

    return 0;
}

int lm_map_4kb(uint64_t *pdpt, uint64_t phys, uint64_t virt, uint64_t flags) {
    if (!pdpt) return -1;

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    lm_page_entry_t *pml4 = (lm_page_entry_t *)pdpt;

    /* Navigate PML4 -> PDPT */
    if (!pml4[pml4_idx].present) return -1;
    lm_page_entry_t *pdpt_table = (lm_page_entry_t *)(pml4[pml4_idx].frame << 12);

    /* Navigate PDPT -> PD */
    if (!pdpt_table[pdpt_idx].present) return -1;
    lm_page_entry_t *pd = (lm_page_entry_t *)(pdpt_table[pdpt_idx].frame << 12);

    /* If PD entry is a 2MB huge page, split it first */
    if (pd[pd_idx].huge && pd[pd_idx].present) {
        /* For simplicity, don't split — just return error */
        return -2;
    }

    /* Allocate page table if needed */
    if (!pd[pd_idx].present) {
        /* Need to allocate a 4KB page for the PT */
        /* Use a fixed high-address scratch area */
        static uint64_t pt_phys = 0x800000; /* scratch at 8MB */
        uint64_t new_pt = pt_phys;
        pt_phys += 0x1000;

        pd[pd_idx].frame = new_pt >> 12;
        pd[pd_idx].present = 1;
        pd[pd_idx].rw = (flags >> 1) & 1;
        pd[pd_idx].user = (flags >> 2) & 1;

        /* Zero the new page table */
        lm_page_entry_t *pt = (lm_page_entry_t *)new_pt;
        for (int i = 0; i < 512; i++) pt[i].present = 0;
    }

    lm_page_entry_t *pt = (lm_page_entry_t *)(pd[pd_idx].frame << 12);

    /* Map 4KB page */
    pt[pt_idx].frame = phys >> 12;
    pt[pt_idx].present = 1;
    pt[pt_idx].rw = (flags >> 1) & 1;
    pt[pt_idx].user = (flags >> 2) & 1;
    pt[pt_idx].nx = (flags >> 63) & 1;

    return 0;
}

void lm_invalidate_page(uint64_t virt) {
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void lm_flush_tlb(void) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

uint64_t lm_read_cr3(void) {
    uint64_t val;
    asm volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

void lm_write_cr3(uint64_t val) {
    asm volatile("mov %0, %%cr3" : : "r"(val) : "memory");
}

uint64_t lm_read_cr4(void) {
    uint64_t val;
    asm volatile("mov %%cr4, %0" : "=r"(val));
    return val;
}

void lm_write_cr4(uint64_t val) {
    asm volatile("mov %0, %%cr4" : : "r"(val) : "memory");
}

/* ---- MSR access ---- */

uint64_t lm_read_msr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

void lm_write_msr(uint32_t msr, uint64_t val) {
    asm volatile("wrmsr" : : "a"((uint32_t)val), "d"((uint32_t)(val >> 32)), "c"(msr));
}

/* ---- Interrupt control ---- */

void lm_cli(void) { asm volatile("cli" ::: "memory"); }
void lm_sti(void) { asm volatile("sti" ::: "memory"); }
void lm_halt(void) { asm volatile("hlt" ::: "memory"); }
void lm_cli_halt(void) { asm volatile("cli\nhlt" ::: "memory"); }

/* ---- Spinlocks (test-and-set) ---- */

typedef struct {
    volatile uint32_t lock;
} lm_spinlock_t;

void lm_spin_lock(lm_spinlock_t *lock) {
    while (__sync_lock_test_and_set(&lock->lock, 1))
        __asm__ volatile("pause");
}

void lm_spin_unlock(lm_spinlock_t *lock) {
    __sync_lock_release(&lock->lock);
}

int lm_spin_trylock(lm_spinlock_t *lock) {
    return __sync_lock_test_and_set(&lock->lock, 1) == 0;
}

/* ---- Atomic operations ---- */

uint64_t lm_atomic_exchange(volatile uint64_t *ptr, uint64_t val) {
    return __sync_lock_test_and_set(ptr, val);
}

uint64_t lm_atomic_compare_exchange(volatile uint64_t *ptr, uint64_t cmp, uint64_t val) {
    return __sync_val_compare_and_swap(ptr, cmp, val);
}

void lm_atomic_add(volatile uint64_t *ptr, uint64_t val) {
    __sync_fetch_and_add(ptr, val);
}

void lm_atomic_sub(volatile uint64_t *ptr, uint64_t val) {
    __sync_fetch_and_sub(ptr, val);
}

uint64_t lm_atomic_or(volatile uint64_t *ptr, uint64_t val) {
    return __sync_fetch_and_or(ptr, val);
}

uint64_t lm_atomic_and(volatile uint64_t *ptr, uint64_t val) {
    return __sync_fetch_and_and(ptr, val);
}

/* ---- Timing ---- */

uint64_t lm_rdtsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void lm_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    asm volatile("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf)
    );
}

/* ---- SYSCALL setup (MSR-based) ---- */

typedef struct {
    uint64_t rip;
    uint64_t cs;
} __attribute__((packed)) lm_star_star_data_t;

#define MSR_STAR        0xC0000081
#define MSR_LSTAR       0xC0000082
#define MSR_SFMASK      0xC0000084

void lm_setup_syscall(uint64_t syscall_handler) {
    /* STAR: CS/SS for syscall/sysret */
    uint64_t star = 0;
    star |= ((uint64_t)0x08 << 32);  /* syscall CS = 0x08 (kernel code) */
    star |= ((uint64_t)0x10 << 48);  /* syscall SS = 0x10 (kernel data) */
    star |= ((uint64_t)0x1B << 16);  /* sysret CS = 0x1B (user code) */
    star |= ((uint64_t)0x23 << 48);  /* sysret SS = 0x23 (user data) */
    lm_write_msr(MSR_STAR, star);

    /* LSTAR: syscall entry point */
    lm_write_msr(MSR_LSTAR, syscall_handler);

    /* SFMASK: mask interrupts on syscall (clear IF) */
    lm_write_msr(MSR_SFMASK, 0x200);

    /* Enable SYSCALL/SYSRET via EFER.SCE */
    uint64_t efer = lm_read_msr(0xC0000080);
    efer |= (1 << 0);  /* SCE bit */
    lm_write_msr(0xC0000080, efer);
}

/* ---- SMP (IPI) ---- */

#define APIC_ICR_LOW   0x030
#define APIC_ICR_HIGH  0x310
#define APIC_ICR_INIT  (5 << 8)
#define APIC_ICR_SIPI  (6 << 8)

void lm_smp_send_init(uint32_t apic_id, uint32_t apic_base) {
    volatile uint32_t *apic = (volatile uint32_t *)(uint64_t)apic_base;
    apic[APIC_ICR_HIGH >> 2] = (uint32_t)apic_id << 24;
    apic[APIC_ICR_LOW >> 2] = APIC_ICR_INIT;

    /* Wait for delivery */
    for (volatile int i = 0; i < 10000; i++) asm volatile("pause");
}

void lm_smp_send_sipi(uint32_t apic_id, uint32_t vector, uint32_t apic_base) {
    volatile uint32_t *apic = (volatile uint32_t *)(uint64_t)apic_base;
    apic[APIC_ICR_HIGH >> 2] = (uint32_t)apic_id << 24;
    apic[APIC_ICR_LOW >> 2] = APIC_ICR_SIPI | vector;

    for (volatile int i = 0; i < 10000; i++) asm volatile("pause");
}

uint32_t lm_smp_get_apic_id(uint32_t apic_base) {
    volatile uint32_t *apic = (volatile uint32_t *)(uint64_t)apic_base;
    return (apic[0x20 >> 2] >> 24) & 0xFF;
}

/* ---- Delay ---- */

void lm_delay_us(uint64_t us) {
    uint64_t start = lm_rdtsc();
    /* Approximate: assume ~1GHz TSC for safety, scale up */
    uint64_t cycles = us * 2000;
    while ((lm_rdtsc() - start) < cycles)
        asm volatile("pause");
}

void lm_delay_ms(uint64_t ms) {
    lm_delay_us(ms * 1000);
}
