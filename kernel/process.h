#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "vfs.h"

/* Page-table entry flag bits */
#define PML4_PRESENT  0x01
#define PML4_RW       0x02
#define PML4_USER     0x04

#define MAX_PROCESSES       64
#define PROCESS_NAME_LEN    32
#define PROCESS_STACK_SIZE  (16 * 1024)  /* 16KB kernel stack */
#define PROCESS_USER_STACK  (64 * 1024)  /* 64KB user stack */
#define PROCESS_HEAP_SIZE   (256 * 1024) /* 256KB initial heap */

typedef enum {
    PROC_UNUSED,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE
} proc_state_t;

typedef enum {
    RING0,
    RING3
} proc_ring_t;

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) process_context_t;

typedef struct {
    int      pid;
    int      ppid;
    proc_state_t state;
    proc_ring_t  ring;
    char     name[PROCESS_NAME_LEN];

    /* Memory management — per-process address space */
    uint64_t cr3;              /* PML4 physical address (0 = kernel shared) */
    uint64_t kernel_stack;     /* Kernel-mode stack (top) */
    uint64_t user_stack;       /* User-mode stack (top) */
    uint64_t user_heap;        /* User-mode heap start */
    uint64_t user_heap_end;    /* User-mode heap current end */
    uint64_t brk;              /* Program break */

    /* Process info */
    uint64_t entry_point;
    uint64_t flags;
    int      exit_code;
    uint64_t wake_time;        /* For sleeping processes */

    process_context_t context;

    uint64_t saved_rsp;          /* Saved kernel RSP for context switch */

    /* Open files (simple) */
    int      fd_table[16];
    int      fd_count;

    /* Current working directory */
    char     cwd[VFS_PATH_LEN];
} process_t;

/* Page table management */
uint64_t vmm_create_address_space(void);
void     vmm_destroy_address_space(uint64_t cr3);
void     vmm_clone_address_space(uint64_t dst_cr3, uint64_t src_cr3);
void     vmm_map_page(uint64_t cr3, uint64_t virt, uint64_t phys, uint64_t flags);
void     vmm_unmap_page(uint64_t cr3, uint64_t virt);
uint64_t vmm_translate(uint64_t cr3, uint64_t virt);
void     vmm_switch_to(uint64_t cr3);

void     process_init(void);
int      process_create(void (*entry)(void), const char *name, proc_ring_t ring);
int      process_fork(void);
void     process_exit(int code);
void     process_yield(void);
void     process_sleep(uint64_t ms);
void     process_wake(int pid);
void     process_scheduler(void);
process_t *process_get_current(void);
process_t *process_get(int pid);

extern process_t processes[MAX_PROCESSES];
extern int current_process;

/* Assembly routines */
extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);
extern void switch_to_user_mode(uint64_t rip, uint64_t rsp, uint64_t rflags);

#endif
