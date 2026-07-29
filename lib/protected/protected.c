/**
 * Chicago-95 Protected Mode Utility Library
 * 32-bit helpers: page table manipulation, GDT, IDT, interrupt handling
 */

#include <stdint.h>

/* ---- GDT helpers ---- */

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) pm_gdtr_t;

typedef struct {
    uint16_t limit_low;
    uint32_t base_low  : 24;
    uint32_t access    : 8;
    uint32_t granularity: 8;
    uint32_t base_high : 8;
} __attribute__((packed)) pm_gdt_entry_t;

void pm_gdt_set_entry(pm_gdt_entry_t *gdt, uint32_t idx,
                      uint32_t base, uint32_t limit,
                      uint8_t access, uint8_t flags) {
    gdt[idx].base_low    = base & 0xFFFFFF;
    gdt[idx].limit_low   = limit & 0xFFFF;
    gdt[idx].access      = access;
    gdt[idx].granularity  = ((limit >> 16) & 0x0F) | ((flags & 0x0F) << 4);
    gdt[idx].base_high   = (base >> 24) & 0xFF;
}

void pm_gdt_load(pm_gdt_entry_t *gdt, uint32_t entries) {
    pm_gdtr_t gdtr;
    gdtr.limit = entries * sizeof(pm_gdt_entry_t) - 1;
    gdtr.base = (uint32_t)gdt;
    asm volatile("lgdt %0" : : "m"(gdtr));
}

/* ---- IDT ---- */

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed)) pm_idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) pm_idtr_t;

static pm_idt_entry_t pm_idt[256];
static pm_idtr_t pm_idtr;

void pm_idt_set_gate(uint8_t num, uint32_t offset, uint16_t selector, uint8_t type_attr) {
    pm_idt[num].offset_low  = offset & 0xFFFF;
    pm_idt[num].offset_high = (offset >> 16) & 0xFFFF;
    pm_idt[num].selector    = selector;
    pm_idt[num].zero        = 0;
    pm_idt[num].type_attr   = type_attr;
}

void pm_idt_load(void) {
    pm_idtr.limit = sizeof(pm_idt) - 1;
    pm_idtr.base = (uint32_t)&pm_idt;
    asm volatile("lidt %0" : : "m"(pm_idtr));
}

/* ---- Page table helpers (32-bit) ---- */

typedef struct {
    uint32_t present   : 1;
    uint32_t rw        : 1;
    uint32_t user      : 1;
    uint32_t write_thr : 1;
    uint32_t cache_dis : 1;
    uint32_t accessed  : 1;
    uint32_t dirty     : 1;
    uint32_t huge      : 1;
    uint32_t global    : 1;
    uint32_t avail     : 3;
    uint32_t frame     : 20;
} __attribute__((packed)) pm_page_entry_t;

typedef pm_page_entry_t pm_page_table_t[1024];

int pm_map_4k(uint32_t *pd, uint32_t phys, uint32_t virt, uint32_t flags) {
    uint32_t pd_idx = (virt >> 22) & 0x3FF;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;

    if (!(pd[pd_idx] & 0x01)) {
        /* Allocate new page table (caller must provide) */
        return -1;
    }

    pm_page_table_t *pt = (pm_page_table_t *)(pd[pd_idx] & 0xFFFFF000);
    (*pt)[pt_idx].frame   = (phys >> 12) & 0xFFFFF;
    (*pt)[pt_idx].present = (flags & 0x01) ? 1 : 0;
    (*pt)[pt_idx].rw      = (flags & 0x02) ? 1 : 0;
    (*pt)[pt_idx].user    = (flags & 0x04) ? 1 : 0;

    /* Invalidate TLB entry */
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

void pm_invalidate_tlb(void) {
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" : : "r"(cr3));
}

/* ---- Interrupt enable/disable ---- */

void pm_cli(void) {
    asm volatile("cli");
}

void pm_sti(void) {
    asm volatile("sti");
}

void pm_halt(void) {
    asm volatile("hlt");
}

/* ---- CR register access ---- */

uint32_t pm_read_cr0(void) {
    uint32_t val;
    asm volatile("mov %%cr0, %0" : "=r"(val));
    return val;
}

void pm_write_cr0(uint32_t val) {
    asm volatile("mov %0, %%cr0" : : "r"(val));
}

uint32_t pm_read_cr2(void) {
    uint32_t val;
    asm volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}

uint32_t pm_read_cr3(void) {
    uint32_t val;
    asm volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

void pm_write_cr3(uint32_t val) {
    asm volatile("mov %0, %%cr3" : : "r"(val));
}

uint32_t pm_read_cr4(void) {
    uint32_t val;
    asm volatile("mov %%cr4, %0" : "=r"(val));
    return val;
}

void pm_write_cr4(uint32_t val) {
    asm volatile("mov %0, %%cr4" : : "r"(val));
}

/* ---- MSR access ---- */

uint64_t pm_read_msr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

void pm_write_msr(uint32_t msr, uint64_t val) {
    uint32_t lo = val & 0xFFFFFFFF;
    uint32_t hi = (val >> 32) & 0xFFFFFFFF;
    asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

/* ---- Spinlock ---- */

typedef struct {
    uint32_t lock;
} pm_spinlock_t;

void pm_spin_lock(pm_spinlock_t *lock) {
    while (__sync_lock_test_and_set(&lock->lock, 1))
        asm volatile("pause");
}

void pm_spin_unlock(pm_spinlock_t *lock) {
    __sync_lock_release(&lock->lock);
}

/* ---- Atomic operations ---- */

uint32_t pm_atomic_exchange(volatile uint32_t *ptr, uint32_t val) {
    asm volatile("xchgl %0, %1" : "=r"(val), "+m"(*ptr) : "0"(val) : "memory");
    return val;
}

uint32_t pm_atomic_compare_exchange(volatile uint32_t *ptr, uint32_t cmp, uint32_t val) {
    asm volatile("lock cmpxchgl %1, %2"
                 : "=a"(cmp)
                 : "r"(val), "m"(*ptr), "0"(cmp)
                 : "memory");
    return cmp;
}

void pm_atomic_add(volatile uint32_t *ptr, uint32_t val) {
    asm volatile("lock addl %1, %0" : "+m"(*ptr) : "r"(val) : "memory");
}
