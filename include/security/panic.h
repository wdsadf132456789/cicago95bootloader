/**
 * Chicago-95 Panic Handler
 * Hardware exception trap → encrypted register dump → COM1 skull → reboot
 */

#ifndef CHICAGO_PANIC_H
#define CHICAGO_PANIC_H

#include <stdint.h>

/* Panic error codes (mapped to x86 exception vectors 0-31) */
#define PANIC_DIVIDE_ERROR      0
#define PANIC_DEBUG             1
#define PANIC_NMI               2
#define PANIC_BREAKPOINT        3
#define PANIC_OVERFLOW          4
#define PANIC_BOUND_RANGE       5
#define PANIC_INVALID_OPCODE    6
#define PANIC_DEVICE_NOT_AVAIL  7
#define PANIC_DOUBLE_FAULT      8
#define PANIC_COPROC_SEG_OVR    9
#define PANIC_INVALID_TSS       10
#define PANIC_SEG_NOT_PRESENT   11
#define PANIC_STACK_SEG_FAULT   12
#define PANIC_GPF               13
#define PANIC_PAGE_FAULT        14
#define PANIC_X87_FP_EXCP       16
#define PANIC_ALIGN_CHECK       17
#define PANIC_MACHINE_CHECK     18
#define PANIC_SIMD_FP_EXCP      19
#define PANIC_VIRT_EXCP         20
#define PANIC_SECURITY_EXCP     30
#define PANIC_MANUAL            128   /* Software-initiated panic */

/* Panic buffer configuration */
#define PANIC_BUF_SLOTS         16    /* Number of encrypted register dumps */
#define PANIC_BUF_SLOT_SIZE     512   /* Bytes per encrypted slot */
#define PANIC_BUF_TOTAL         (PANIC_BUF_SLOTS * PANIC_BUF_SLOT_SIZE)

/* Maximum exception name length */
#define PANIC_EXCP_NAME_LEN     32

/* Full CPU register state captured at panic time */
typedef struct {
    /* General purpose (pushed by ISR stub) */
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9,  r8;
    uint64_t rdi, rsi, rbp, rdx;
    uint64_t rcx, rbx, rax;

    /* Interrupt frame (pushed by CPU) */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;

    /* Error code (pushed for some exceptions) */
    uint64_t error_code;

    /* Exception vector number */
    uint64_t vector;

    /* CR2 (faulting address for page faults) */
    uint64_t cr2;

    /* CR3 (page table base) */
    uint64_t cr3;

    /* Timestamp */
    uint64_t rdtsc;
} __attribute__((packed)) panic_regs_t;

/* Encrypted panic buffer slot */
typedef struct {
    uint8_t  ciphertext[PANIC_BUF_SLOT_SIZE - 16 - 8]; /* encrypted regs */
    uint8_t  tag[16];        /* Poly1305 auth tag */
    uint8_t  nonce[8];       /* Nonce (counter-based) */
    uint8_t  seq;            /* Slot sequence number */
    uint8_t  valid;          /* 0xFF = slot has valid data */
} __attribute__((packed)) panic_slot_t;

/* Panic subsystem state */
typedef struct {
    uint8_t              armed;        /* 1 = panic handlers installed */
    uint8_t              panicking;    /* 1 = inside panic handler (recursive guard) */
    uint8_t              reboot_pending;
    uint8_t              write_idx;    /* Next slot to write */
    uint32_t             drop_count;   /* Number of panics dropped (buf full) */
    uint32_t             total_panics;
    uint8_t              key[32];      /* ChaCha20 encryption key */
    panic_slot_t         slots[PANIC_BUF_SLOTS];
    panic_regs_t         last_regs;    /* Most recent raw register dump */
    uint8_t              last_excp;    /* Last exception vector */
} __attribute__((packed)) panic_state_t;

/* ========================================================================
 * Panic API
 * ======================================================================== */

/* Initialize panic handler: generates key, installs IDT exception stubs */
int      panic_init(void);

/* Trigger a panic manually (software panic) */
void     panic(uint8_t vector, const char *msg);

/* Trigger panic with full register context (called from ISR stubs) */
void     panic_catch(uint64_t vector, uint64_t error_code, panic_regs_t *regs);

/* Check if panic handler is armed */
int      panic_is_armed(void);

/* Get panic state (read-only) */
const panic_state_t *panic_get_state(void);

/* Read an encrypted slot from the circular buffer */
int      panic_read_slot(uint8_t index, panic_slot_t *out);

/* Unmount BrainFS and secure filesystem state before reboot */
void     panic_secure_filesystem(void);

/* COM1 serial output */
void     panic_serial_init(void);
void     panic_serial_putc(char c);
void     panic_serial_puts(const char *s);
void     panic_serial_write(const uint8_t *data, uint32_t len);

/* ASCII-art skull stream over COM1 */
void     panic_stream_skull(void);

/* Hardware reboot (keyboard controller method) */
void     panic_reboot(void);

#endif /* CHICAGO_PANIC_H */
