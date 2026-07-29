#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include "process.h"

/* System call numbers */
#define SYS_READ      0
#define SYS_WRITE     1
#define SYS_OPEN      2
#define SYS_CLOSE     3
#define SYS_FORK      4
#define SYS_EXEC      5
#define SYS_EXIT      6
#define SYS_WAIT      7
#define SYS_GETPID    8
#define SYS_GETPPID   9
#define SYS_BRK       10
#define SYS_MMAP      11
#define SYS_MUNMAP    12
#define SYS_SBRK      13
#define SYS_SLEEP     14
#define SYS_YIELD     15
#define SYS_STAT      16
#define SYS_DUP       17
#define SYS_PIPE      18
#define SYS_IOCTL     19
#define SYS_GETCWD    20
#define SYS_CHDIR     21
#define SYS_TIME      22
#define SYS_SHUTDOWN  23
#define SYS_REBOOT    24

#define MAX_SYSCALLS  25

/* User-to-kernel register context pushed by syscall/sysret or int 0x80 */
typedef struct {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;   /* syscall number in / out: return value */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) syscall_frame_t;

/* Standard calling convention: rax=syscall#, rdi,rsi,rdx,r10,r8,r9=args */
typedef int64_t (*syscall_fn_t)(uint64_t a1, uint64_t a2, uint64_t a3,
                                uint64_t a4, uint64_t a5, uint64_t a6);

/* Initialize syscall subsystem (writes MSR_LSTAR etc.) */
void syscall_init(void);

/* Main handler — called from asm entry stub */
void syscall_handler(syscall_frame_t *frame);

/* Validate that a user pointer falls within the process address space */
int syscall_validate_user_ptr(uint64_t addr, uint64_t len);

/* Copy data from user space to kernel buffer */
int syscall_copy_from_user(void *kdst, const void *usrc, uint64_t len);

/* Copy data from kernel buffer to user space */
int syscall_copy_to_user(void *udst, const void *ksrc, uint64_t len);

#endif
