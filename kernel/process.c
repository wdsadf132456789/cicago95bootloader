/**
 * Chicago-95 Process Manager
 * Per-process address spaces, fork, sleep, round-robin scheduler
 */

#include "process.h"
#include "kernel.h"
#include "gdt.h"
#include "kmalloc.h"
#include "timer.h"

process_t processes[MAX_PROCESSES];
int current_process = -1;

static uint64_t kernel_stacks[MAX_PROCESSES][PROCESS_STACK_SIZE / 8] __attribute__((aligned(16)));
static uint32_t scheduler_tick = 0;

/* Trampoline: first code a fresh process executes after context_switch */
static void process_trampoline(void) {
    process_t *proc = process_get_current();
    if (!proc) return;

    if (proc->ring == RING3) {
        /* Switch to user mode via IRETQ */
        switch_to_user_mode(proc->entry_point, proc->user_stack, 0x202);
        /* Should never return */
    } else {
        /* RING0: call entry directly */
        typedef void entry_fn(void);
        entry_fn *fn = (entry_fn *)proc->entry_point;
        fn();
        process_exit(0);
    }
}

/* ---- Page table management (kernel-side VMM) ---- */

#define PML4_PRESENT  0x01
#define PML4_RW       0x02
#define PML4_USER     0x04

static inline void invlpg_addr(uint64_t addr) {
    asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

uint64_t vmm_create_address_space(void) {
    /* Allocate a new PML4 */
    void *pml4_page = pmm_alloc_page();
    if (!pml4_page) return 0;
    __builtin_memset(pml4_page, 0, PAGE_SIZE);

    uint64_t pml4_phys = (uint64_t)pml4_page;

    /* Copy kernel mappings from current PML4 (entries 256-511) */
    uint64_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    uint64_t *src_pml4 = (uint64_t *)current_cr3;
    uint64_t *dst_pml4 = (uint64_t *)pml4_phys;

    for (int i = 256; i < 512; i++) {
        dst_pml4[i] = src_pml4[i];
    }

    return pml4_phys;
}

void vmm_destroy_address_space(uint64_t cr3) {
    if (!cr3) return;

    uint64_t *pml4 = (uint64_t *)cr3;

    /* Free user-space page tables (entries 0-255 only) */
    for (int i = 0; i < 256; i++) {
        if (!(pml4[i] & PML4_PRESENT)) continue;

        uint64_t *pdpt = (uint64_t *)(pml4[i] & ~0xFFFULL);
        for (int j = 0; j < 512; j++) {
            if (!(pdpt[j] & PML4_PRESENT)) continue;
            if (pdpt[j] & 0x80) continue;  /* 1GB huge page */

            uint64_t *pd = (uint64_t *)(pdpt[j] & ~0xFFFULL);
            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & PML4_PRESENT)) continue;
                if (pd[k] & 0x80) continue;  /* 2MB huge page */

                uint64_t *pt = (uint64_t *)(pd[k] & ~0xFFFULL);
                /* Free all 4KB pages in this PT */
                for (int l = 0; l < 512; l++) {
                    if (pt[l] & PML4_PRESENT) {
                        pmm_free_page((void *)(pt[l] & ~0xFFFULL));
                    }
                }
                pmm_free_page(pt);
            }
            pmm_free_page(pd);
        }
        pmm_free_page(pdpt);
    }

    pmm_free_page((void *)cr3);
}

void vmm_clone_address_space(uint64_t dst_cr3, uint64_t src_cr3) {
    if (!dst_cr3 || !src_cr3) return;

    uint64_t *src = (uint64_t *)src_cr3;
    uint64_t *dst = (uint64_t *)dst_cr3;

    /* Only clone user-space (entries 0-255) */
    for (int i = 0; i < 256; i++) {
        if (!(src[i] & PML4_PRESENT)) continue;

        /* Allocate new PDPT */
        void *new_pdpt_page = pmm_alloc_page();
        if (!new_pdpt_page) continue;
        __builtin_memset(new_pdpt_page, 0, PAGE_SIZE);
        dst[i] = src[i] & ~0xFFFULL;
        dst[i] |= PML4_PRESENT | PML4_RW | PML4_USER;

        uint64_t *src_pdpt = (uint64_t *)(src[i] & ~0xFFFULL);
        uint64_t *dst_pdpt = (uint64_t *)new_pdpt_page;

        for (int j = 0; j < 512; j++) {
            if (!(src_pdpt[j] & PML4_PRESENT)) continue;
            if (src_pdpt[j] & 0x80) { dst_pdpt[j] = src_pdpt[j]; continue; }

            void *new_pd_page = pmm_alloc_page();
            if (!new_pd_page) continue;
            __builtin_memset(new_pd_page, 0, PAGE_SIZE);
            dst_pdpt[j] = src_pdpt[j] & ~0xFFFULL;
            dst_pdpt[j] |= PML4_PRESENT | PML4_RW | PML4_USER;

            uint64_t *src_pd = (uint64_t *)(src_pdpt[j] & ~0xFFFULL);
            uint64_t *dst_pd = (uint64_t *)new_pd_page;

            for (int k = 0; k < 512; k++) {
                if (!(src_pd[k] & PML4_PRESENT)) continue;
                if (src_pd[k] & 0x80) { dst_pd[k] = src_pd[k]; continue; }

                void *new_pt_page = pmm_alloc_page();
                if (!new_pt_page) continue;
                __builtin_memset(new_pt_page, 0, PAGE_SIZE);
                dst_pd[k] = src_pd[k] & ~0xFFFULL;
                dst_pd[k] |= PML4_PRESENT | PML4_RW | PML4_USER;

                uint64_t *src_pt = (uint64_t *)(src_pd[k] & ~0xFFFULL);
                uint64_t *dst_pt = (uint64_t *)new_pt_page;

                for (int l = 0; l < 512; l++) {
                    if (!(src_pt[l] & PML4_PRESENT)) continue;

                    /* Copy the physical page */
                    void *new_page = pmm_alloc_page();
                    if (new_page) {
                        __builtin_memcpy(new_page, (void *)(src_pt[l] & ~0xFFFULL), PAGE_SIZE);
                        dst_pt[l] = (uint64_t)new_page | (src_pt[l] & 0xFFFULL);
                    }
                }
            }
        }
    }
}

void vmm_map_page(uint64_t cr3, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!cr3) return;

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t *pml4 = (uint64_t *)cr3;

    /* PML4 -> PDPT */
    if (!(pml4[pml4_idx] & PML4_PRESENT)) {
        void *page = pmm_alloc_page();
        if (!page) return;
        __builtin_memset(page, 0, PAGE_SIZE);
        pml4[pml4_idx] = (uint64_t)page | PML4_PRESENT | PML4_RW | PML4_USER;
    }
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & ~0xFFFULL);

    /* PDPT -> PD */
    if (!(pdpt[pdpt_idx] & PML4_PRESENT)) {
        void *page = pmm_alloc_page();
        if (!page) return;
        __builtin_memset(page, 0, PAGE_SIZE);
        pdpt[pdpt_idx] = (uint64_t)page | PML4_PRESENT | PML4_RW | PML4_USER;
    }
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & ~0xFFFULL);

    /* PD -> PT */
    if (!(pd[pd_idx] & PML4_PRESENT)) {
        void *page = pmm_alloc_page();
        if (!page) return;
        __builtin_memset(page, 0, PAGE_SIZE);
        pd[pd_idx] = (uint64_t)page | PML4_PRESENT | PML4_RW | PML4_USER;
    }
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & ~0xFFFULL);

    /* Map 4KB page */
    pt[pt_idx] = (phys & ~0xFFFULL) | (flags & 0xFFF) | PML4_PRESENT;
    invlpg_addr(virt);
}

void vmm_unmap_page(uint64_t cr3, uint64_t virt) {
    if (!cr3) return;

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t *pml4 = (uint64_t *)cr3;
    if (!(pml4[pml4_idx] & PML4_PRESENT)) return;
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & ~0xFFFULL);
    if (!(pdpt[pdpt_idx] & PML4_PRESENT)) return;
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & ~0xFFFULL);
    if (!(pd[pd_idx] & PML4_PRESENT)) return;
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & ~0xFFFULL);

    pt[pt_idx] = 0;
    invlpg_addr(virt);
}

uint64_t vmm_translate(uint64_t cr3, uint64_t virt) {
    if (!cr3) return virt;  /* Identity map if no cr3 */

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t *pml4 = (uint64_t *)cr3;
    if (!(pml4[pml4_idx] & PML4_PRESENT)) return 0;
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & ~0xFFFULL);
    if (!(pdpt[pdpt_idx] & PML4_PRESENT)) return 0;
    if (pdpt[pdpt_idx] & 0x80) return (pdpt[pdpt_idx] & ~0x1FFFFFFFULL) | (virt & 0x3FFFFFFF);
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & ~0xFFFULL);
    if (!(pd[pd_idx] & PML4_PRESENT)) return 0;
    if (pd[pd_idx] & 0x80) return (pd[pd_idx] & ~0x1FFFFFULL) | (virt & 0x1FFFFF);
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & ~0xFFFULL);
    if (!(pt[pt_idx] & PML4_PRESENT)) return 0;
    return (pt[pt_idx] & ~0xFFFULL) | (virt & 0xFFF);
}

void vmm_switch_to(uint64_t cr3) {
    asm volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

/* ---- Process management ---- */

void process_init(void) {
    __builtin_memset(processes, 0, sizeof(processes));
    current_process = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processes[i].pid = -1;
        processes[i].state = PROC_UNUSED;
        processes[i].saved_rsp = 0;
        for (int j = 0; j < 16; j++)
            processes[i].fd_table[j] = -1;
    }
}

static void idle_process(void) {
    while (1) hlt();
}

int process_create(void (*entry)(void), const char *name, proc_ring_t ring) {
    int pid = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_UNUSED) { pid = i; break; }
    }
    if (pid < 0) return -1;

    process_t *proc = &processes[pid];
    __builtin_memset(proc, 0, sizeof(process_t));

    proc->pid = pid;
    proc->ppid = current_process;
    proc->state = PROC_READY;
    proc->ring = ring;

    int len = 0;
    while (name[len] && len < PROCESS_NAME_LEN - 1) {
        proc->name[len] = name[len];
        len++;
    }
    proc->name[len] = '\0';

    proc->kernel_stack = (uint64_t)&kernel_stacks[pid][PROCESS_STACK_SIZE / 8];
    proc->user_stack = proc->kernel_stack - PROCESS_USER_STACK;
    proc->user_heap = proc->user_stack - PROCESS_HEAP_SIZE;
    proc->user_heap_end = proc->user_heap;
    proc->brk = proc->user_heap;
    proc->entry_point = (uint64_t)entry;

    /* Create per-process address space */
    proc->cr3 = vmm_create_address_space();

    /* Map user stack pages */
    if (proc->cr3) {
        for (uint64_t off = 0; off < PROCESS_USER_STACK; off += PAGE_SIZE) {
            void *page = pmm_alloc_page();
            if (!page) break;
            __builtin_memset(page, 0, PAGE_SIZE);
            vmm_map_page(proc->cr3, proc->user_stack - PROCESS_USER_STACK + off,
                         (uint64_t)page, PML4_RW | PML4_USER);
        }
        /* Map user heap pages */
        for (uint64_t off = 0; off < PROCESS_HEAP_SIZE; off += PAGE_SIZE) {
            void *page = pmm_alloc_page();
            if (!page) break;
            __builtin_memset(page, 0, PAGE_SIZE);
            vmm_map_page(proc->cr3, proc->user_heap + off,
                         (uint64_t)page, PML4_RW | PML4_USER);
        }
    }

    /* Set up initial context */
    __builtin_memset(&proc->context, 0, sizeof(process_context_t));
    proc->context.rip = (uint64_t)entry;
    proc->context.rflags = 0x202;

    if (ring == RING0) {
        proc->context.cs = KERNEL_CS;
        proc->context.ss = KERNEL_DS;
        proc->context.rsp = proc->kernel_stack;
    } else {
        proc->context.cs = USER_CS;
        proc->context.ss = USER_DS;
        proc->context.rsp = proc->user_stack;
    }

    /* Set up initial kernel stack for context_switch:
     * context_switch pops r15, r14, r13, r12, rbx, rbp, then ret.
     * The return address must be process_trampoline so a fresh
     * process enters user mode (or ring0 entry) on first schedule. */
    uint64_t *stack = (uint64_t *)proc->kernel_stack;
    *(--stack) = 0;             /* r15 = 0 */
    *(--stack) = 0;             /* r14 = 0 */
    *(--stack) = 0;             /* r13 = 0 */
    *(--stack) = 0;             /* r12 = 0 */
    *(--stack) = 0;             /* rbx = 0 */
    *(--stack) = 0;             /* rbp = 0 */
    *(--stack) = (uint64_t)process_trampoline;  /* return address */

    proc->saved_rsp = (uint64_t)stack;

    return pid;
}

int process_fork(void) {
    if (current_process < 0) return -1;
    process_t *parent = &processes[current_process];

    int pid = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_UNUSED) { pid = i; break; }
    }
    if (pid < 0) return -1;

    process_t *child = &processes[pid];
    __builtin_memcpy(child, parent, sizeof(process_t));
    child->pid = pid;
    child->ppid = current_process;
    child->state = PROC_READY;
    child->exit_code = 0;

    /* Clone address space */
    if (parent->cr3) {
        child->cr3 = vmm_create_address_space();
        if (child->cr3) {
            vmm_clone_address_space(child->cr3, parent->cr3);
        }
    }

    /* Child returns 0 from fork */
    child->context.rax = 0;

    return pid;
}

void process_exit(int code) {
    if (current_process < 0) return;
    process_t *proc = &processes[current_process];
    if (proc->pid == 0) return;  /* Can't kill init */

    /* Reclaim address space */
    if (proc->cr3) {
        vmm_destroy_address_space(proc->cr3);
        proc->cr3 = 0;
    }

    proc->state = PROC_ZOMBIE;
    proc->exit_code = code;
    process_yield();
}

void process_sleep(uint64_t ms) {
    if (current_process < 0) return;
    process_t *proc = &processes[current_process];
    proc->wake_time = timer_get_ticks() + ms;
    proc->state = PROC_BLOCKED;
    process_yield();
}

void process_wake(int pid) {
    if (pid < 0 || pid >= MAX_PROCESSES) return;
    if (processes[pid].state == PROC_BLOCKED) {
        processes[pid].state = PROC_READY;
    }
}

void process_yield(void) {
    /* Yield to next ready process (does NOT return until we're scheduled again) */
    process_scheduler();
}

process_t *process_get_current(void) {
    if (current_process < 0 || current_process >= MAX_PROCESSES) return 0;
    return &processes[current_process];
}

process_t *process_get(int pid) {
    if (pid < 0 || pid >= MAX_PROCESSES) return 0;
    if (processes[pid].state == PROC_UNUSED) return 0;
    return &processes[pid];
}

void process_scheduler(void) {
    scheduler_tick++;
    int start = current_process;
    if (start < 0) start = 0;

    /* Wake sleeping processes whose wake_time has passed */
    uint64_t now = timer_get_ticks();
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_BLOCKED && processes[i].wake_time > 0 && now >= processes[i].wake_time) {
            processes[i].state = PROC_READY;
            processes[i].wake_time = 0;
        }
    }

    /* Find next READY process */
    int next = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        int idx = (start + 1 + i) % MAX_PROCESSES;
        if (processes[idx].state == PROC_READY) {
            next = idx;
            break;
        }
    }

    if (next < 0) return;  /* No process to switch to */

    if (next == current_process) return;  /* Already running this one */

    /* Mark current as READY */
    if (current_process >= 0 && current_process < MAX_PROCESSES) {
        if (processes[current_process].state == PROC_RUNNING)
            processes[current_process].state = PROC_READY;
    }

    int prev = current_process;
    current_process = next;
    processes[current_process].state = PROC_RUNNING;

    /* Switch address space */
    if (processes[current_process].cr3) {
        vmm_switch_to(processes[current_process].cr3);
    }

    /* Set kernel stack top in TSS for ring0 stack on interrupts */
    tss_set_rsp0(processes[current_process].kernel_stack);

    /* Context switch */
    uint64_t *old_rsp_ptr = &processes[prev].saved_rsp;
    uint64_t new_rsp = processes[current_process].saved_rsp;

    if (prev < 0) {
        /* No previous process (first schedule) — just load the new RSP */
        asm volatile("mov %0, %%rsp" : : "r"(new_rsp) : "memory");
        /* Pop callee-saved and ret to process_trampoline */
        asm volatile(
            "pop %%r15\n"
            "pop %%r14\n"
            "pop %%r13\n"
            "pop %%r12\n"
            "pop %%rbx\n"
            "pop %%rbp\n"
            "ret\n"
            : : : "memory"
        );
    } else {
        context_switch(old_rsp_ptr, new_rsp);
    }
}
