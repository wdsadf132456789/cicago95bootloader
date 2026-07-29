/**
 * Chicago-95 Panic Handler
 *
 * Traps hardware exceptions via IDT fault stubs, dumps the full CPU
 * register state into a circular buffer encrypted with ChaCha20-Poly1305,
 * streams an ASCII-art skull over COM1, unmounts BrainFS securely,
 * and reboots — all in protected long mode with interrupts disabled.
 */

#include "security/panic.h"
#include "boot/security.h"
#include "vga/vga.h"

/* ---- External symbols ---- */
extern void     brainfs_umount_all(void);
extern void     vmm_invalidate_all(void);

/* ChaCha20-Poly1305 (from stage2/security/common/chacha20/chacha20.c) */
extern void sec_chacha20_poly1305_flat_encrypt(const uint8_t key[32],
                                            const uint8_t nonce[12],
                                            const uint8_t *aad, uint32_t aad_len,
                                            const uint8_t *in, uint32_t in_len,
                                            uint8_t *out, uint8_t tag[16]);
extern void sec_chacha20_init(sec_chacha20_ctx_t *ctx, const uint8_t key[32],
                               const uint8_t nonce[12], uint64_t counter);
extern void sec_chacha20_crypt(sec_chacha20_ctx_t *ctx, const uint8_t *in,
                                uint8_t *out, uint32_t len);
extern void sec_random_bytes(uint8_t *buf, uint32_t len);

/* ========================================================================
 * Static state
 * ======================================================================== */

static panic_state_t pstate;

/* ========================================================================
 * Exception name table
 * ======================================================================== */

static const char *exception_names[32] = {
    "Division Error",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "Bound Range",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment",
    "Invalid TSS",
    "Segment Not Present",
    "Stack Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved [15]",
    "x87 FP Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD FP Exception",
    "Virtualization",
    "Reserved [21]",
    "Reserved [22]",
    "Reserved [23]",
    "Reserved [24]",
    "Reserved [25]",
    "Reserved [26]",
    "Reserved [27]",
    "Reserved [28]",
    "Reserved [29]",
    "Security Exception",
    "Reserved [31]"
};

/* ========================================================================
 * COM1 Serial (0x3F8) — 9600 baud, 8N1
 * ======================================================================== */

static inline void panic_outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t panic_inb(uint16_t port) {
    uint8_t val;
    asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

void panic_serial_init(void) {
    /* Disable interrupts on UART */
    panic_outb(0x3F8 + 1, 0x00);
    /* Enable DLAB (set baud rate divisor) */
    panic_outb(0x3F8 + 3, 0x80);
    /* Divisor lo = 0x0C → 9600 baud (115200 / 12) */
    panic_outb(0x3F8 + 0, 0x0C);
    /* Divisor hi = 0x00 */
    panic_outb(0x3F8 + 1, 0x00);
    /* 8 bits, no parity, 1 stop bit */
    panic_outb(0x3F8 + 3, 0x03);
    /* Enable FIFO, clear, 14-byte threshold */
    panic_outb(0x3F8 + 2, 0xC7);
    /* IRQs enabled, RTS/DSR set */
    panic_outb(0x3F8 + 4, 0x0B);
}

static int panic_serial_tx_ready(void) {
    return panic_inb(0x3F8 + 5) & 0x20;
}

void panic_serial_putc(char c) {
    /* Spin until transmitter is empty */
    while (!panic_serial_tx_ready());
    panic_outb(0x3F8, (uint8_t)c);
}

void panic_serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') panic_serial_putc('\r');
        panic_serial_putc(*s++);
    }
}

void panic_serial_write(const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        panic_serial_putc((char)data[i]);
}

/* ========================================================================
 * ASCII-art skull over COM1
 * ======================================================================== */

static const char *skull_art[] = {
    "\n"
    "        _____\n"
    "       /     \\\n"
    "      | () () |\n"
    "      |  ___  |\n"
    "      | |   | |\n"
    "   ___|_|___|_|___\n"
    "  /   P A N I C   \\\n"
    " /  _____________  \\\n"
    "|  |  FATAL FAULT |  |\n"
    "|  |_____________|  |\n"
    "|  /|             |\\\n"
    "| / |  REGISTER   | \\\n"
    "|/  |  DUMP VIA   |  \\\n"
    "|   |  CHACHA20   |   |\n"
    "|   |_____________|   |\n"
    "|  /   |       |  \\  |\n"
    "|_/    |       |   \\_|\n"
    " \\     |       |    /\n"
    "  \\____|_______|___/\n"
    "       |       |\n"
    "       |       |\n"
    "      _|       |_\n"
    "     (_________)\n"
    "\n"
};

void panic_stream_skull(void) {
    const char **line = (const char **)skull_art;
    int lines = sizeof(skull_art) / sizeof(skull_art[0]);

    for (int i = 0; i < lines; i++)
        panic_serial_puts(line[i]);
}

/* ========================================================================
 * Hex / decimal formatting helpers
 * ======================================================================== */

static void panic_put_hex8(char *buf, uint8_t v) {
    static const char hex[] = "0123456789ABCDEF";
    buf[0] = hex[(v >> 4) & 0xF];
    buf[1] = hex[v & 0xF];
    buf[2] = '\0';
}

static void panic_put_hex64(char *buf, uint64_t v) {
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) {
        buf[15 - i] = hex[(v >> (i * 4)) & 0xF];
    }
    buf[16] = '\0';
}

static void panic_put_dec(char *buf, uint32_t v) {
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12];
    int pos = 0;
    while (v > 0) { tmp[pos++] = '0' + (v % 10); v /= 10; }
    for (int i = 0; i < pos; i++) buf[i] = tmp[pos - 1 - i];
    buf[pos] = '\0';
}

/* ========================================================================
 * Register dump → formatted text → ChaCha20-encrypted → circular buffer
 * ======================================================================== */

/* AAD for the authenticated encryption */
static const char panic_aad[] = "CHICAGO95-PANIC-DUMP";

static void panic_encrypt_and_store(panic_regs_t *regs) {
    uint8_t slot_idx = pstate.write_idx;

    /* Format register dump as text for the ciphertext payload */
    char text[384];
    int pos = 0;

    /* Exception header */
    const char *name = (regs->vector < 32) ? exception_names[regs->vector] : "UNKNOWN";
    const char *prefix = "PANIC: ";
    for (int i = 0; prefix[i]; i++) text[pos++] = prefix[i];
    for (int i = 0; name[i]; i++) text[pos++] = name[i];
    text[pos++] = '\n';

    /* Register lines */
    const char *labels[] = {
        "RAX=", "RBX=", "RCX=", "RDX=", "RSI=", "RDI=", "RBP=", "RSP=",
        "R8 =", "R9 =", "R10=", "R11=", "R12=", "R13=", "R14=", "R15=",
        "RIP=", "RFLAGS=", "CS=", "SS=", "CR2=", "CR3=", "ERR=", "TSC="
    };
    uint64_t values[] = {
        regs->rax, regs->rbx, regs->rcx, regs->rdx,
        regs->rsi, regs->rdi, regs->rbp, regs->rsp,
        regs->r8,  regs->r9,  regs->r10, regs->r11,
        regs->r12, regs->r13, regs->r14, regs->r15,
        regs->rip, regs->rflags, regs->cs, regs->ss,
        regs->cr2, regs->cr3, regs->error_code, regs->rdtsc
    };

    for (int r = 0; r < 24; r++) {
        /* Label */
        for (int i = 0; labels[r][i]; i++) text[pos++] = labels[r][i];
        /* Value (16 hex digits) */
        char hex[17];
        panic_put_hex64(hex, values[r]);
        for (int i = 0; i < 16; i++) text[pos++] = hex[i];
        text[pos++] = '\n';
    }

    text[pos++] = '\0';
    uint32_t text_len = pos;

    /* Ensure plaintext fits in the slot */
    uint32_t max_plain = sizeof(pstate.slots[0].ciphertext);
    if (text_len > max_plain) text_len = max_plain;

    /* Build nonce: 8-byte counter stored in slot + 4-byte zero pad */
    uint8_t nonce[12];
    for (int i = 0; i < 12; i++) nonce[i] = 0;
    nonce[0] = slot_idx;
    nonce[1] = pstate.total_panics & 0xFF;
    nonce[2] = (pstate.total_panics >> 8) & 0xFF;

    /* Encrypt with ChaCha20-Poly1305 */
    panic_slot_t *slot = &pstate.slots[slot_idx];

    sec_chacha20_poly1305_flat_encrypt(
        pstate.key,
        nonce,
        (const uint8_t *)panic_aad, sizeof(panic_aad) - 1,
        (const uint8_t *)text, text_len,
        slot->ciphertext,
        slot->tag
    );

    /* Store metadata */
    for (int i = 0; i < 8; i++) slot->nonce[i] = nonce[i];
    slot->seq   = slot_idx;
    slot->valid = 0xFF;

    /* Advance circular write pointer */
    pstate.write_idx = (slot_idx + 1) % PANIC_BUF_SLOTS;
    pstate.total_panics++;

    /* Copy raw regs for post-mortem */
    for (uint32_t i = 0; i < sizeof(panic_regs_t); i++)
        ((uint8_t *)&pstate.last_regs)[i] = ((uint8_t *)regs)[i];
    pstate.last_excp = regs->vector & 0xFF;
}

/* ========================================================================
 * Stream register dump over COM1 (plaintext, before encryption)
 * ======================================================================== */

static void panic_stream_regs(panic_regs_t *regs) {
    panic_serial_puts("\n===== CPU REGISTER DUMP =====\n");

    const char *labels[] = {
        "RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "RBP", "RSP",
        "R8 ", "R9 ", "R10", "R11", "R12", "R13", "R14", "R15",
        "RIP", "RFLG", "CS  ", "SS  ", "CR2 ", "CR3 ", "ERR ", "TSC "
    };
    uint64_t values[] = {
        regs->rax, regs->rbx, regs->rcx, regs->rdx,
        regs->rsi, regs->rdi, regs->rbp, regs->rsp,
        regs->r8,  regs->r9,  regs->r10, regs->r11,
        regs->r12, regs->r13, regs->r14, regs->r15,
        regs->rip, regs->rflags, regs->cs, regs->ss,
        regs->cr2, regs->cr3, regs->error_code, regs->rdtsc
    };

    char line[64];
    for (int r = 0; r < 24; r++) {
        int p = 0;
        /* Label */
        for (int i = 0; i < 4; i++) line[p++] = labels[r][i];
        line[p++] = ' ';
        line[p++] = '0';
        line[p++] = 'x';
        /* Hex value */
        char hex[17];
        panic_put_hex64(hex, values[r]);
        for (int i = 0; i < 16; i++) line[p++] = hex[i];
        line[p++] = '\n';
        line[p] = '\0';
        panic_serial_puts(line);
    }

    /* Exception name */
    panic_serial_puts("FAULT: ");
    if (regs->vector < 32)
        panic_serial_puts(exception_names[regs->vector]);
    else
        panic_serial_puts("UNKNOWN");
    panic_serial_puts("\n");

    panic_serial_puts("=============================\n");
}

/* ========================================================================
 * BrainFS secure unmount
 * ======================================================================== */

/* Forward declaration — the real implementation lives in brainfs_core.c.
 * We provide a weak stub here in case BrainFS is not linked. */
__attribute__((weak))
void brainfs_umount_all(void) {
    /* Walk all mounted drives and umount them cleanly.
     * This zeroes the FAT table, closes all open files, and
     * unmaps all cluster VA slots from the VMM. */
    extern int brainfs_umount(uint8_t drive);
    for (uint8_t d = 0; d < 4; d++)
        brainfs_umount(d);
}

void panic_secure_filesystem(void) {
    /* Best-effort unmount of every BrainFS mount point.
     * After this, no cluster VA slots remain mapped and no
     * open file handles reference encrypted data. */
    brainfs_umount_all();

    /* Invalidate TLB to ensure no stale cluster mappings survive */
    vmm_invalidate_all();
}

/* ========================================================================
 * Hardware reboot (keyboard controller / fast A20 reset)
 * ======================================================================== */

static inline void panic_outb_fast(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void panic_reboot(void) {
    /* Method 1: Keyboard controller reset */
    uint8_t good = 0x02;
    while (good & 0x02)
        good = panic_inb(0x64);
    panic_outb_fast(0x64, 0xFE);

    /* Method 2: Triple fault via IDT */
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) idtr = {0, 0};
    asm volatile("lidt %0" : : "m"(idtr));
    asm volatile("int $3");

    /* Method 3: Port 0x92 (Fast A20) reset */
    panic_outb_fast(0x92, 0xFF);

    /* Method 4: ACPI reset (if available) */
    panic_outb_fast(0x604, 0x2000);   /* QEMU/KVM */
    panic_outb_fast(0xB004, 0x2000);  /* Bochs */

    /* Should never reach here */
    while (1) asm volatile("hlt");
}

/* ========================================================================
 * ISR stubs — x86_64 exception vectors 0-31
 *
 * Each stub pushes the vector number (and error code where the CPU
 * doesn't), then jumps to the common handler which builds panic_regs_t
 * and calls panic_catch().
 *
 * Error-code exceptions (8,10-14,17,21,29,30): CPU pushes error code.
 * No-error exceptions (0-7,9,15,16,18-20): stub pushes dummy 0.
 * ======================================================================== */

#define ISR_STUB(vect, has_err) \
    __attribute__((naked)) void isr_##vect(void); \
    __attribute__((naked)) void isr_##vect(void) { \
        asm volatile( \
            ".code64\n" \
            /* If no error code, push a dummy one */ \
            has_err \
            "pushq $" #vect "\n" \
            /* Save all general-purpose registers */ \
            "pushq %%rax\n" \
            "pushq %%rbx\n" \
            "pushq %%rcx\n" \
            "pushq %%rdx\n" \
            "pushq %%rsi\n" \
            "pushq %%rdi\n" \
            "pushq %%rbp\n" \
            "pushq %%r8\n" \
            "pushq %%r9\n" \
            "pushq %%r10\n" \
            "pushq %%r11\n" \
            "pushq %%r12\n" \
            "pushq %%r13\n" \
            "pushq %%r14\n" \
            "pushq %%r15\n" \
            /* RSP points to panic_regs_t, pass as argument */ \
            "movq %%rsp, %%rdi\n" \
            "call panic_catch\n" \
            /* Should not return, but just in case */ \
            "cli\n" \
            "hlt\n" \
            ".code32\n" \
            : : : "memory" \
        ); \
    }

/* No error code pushed by CPU */
#define ERR_NONE  "pushq $0\n"
/* Error code already pushed by CPU */
#define ERR_CODE  ""

/* Vectors 0-7: no error code */
ISR_STUB(0,  ERR_NONE)
ISR_STUB(1,  ERR_NONE)
ISR_STUB(2,  ERR_NONE)
ISR_STUB(3,  ERR_NONE)
ISR_STUB(4,  ERR_NONE)
ISR_STUB(5,  ERR_NONE)
ISR_STUB(6,  ERR_NONE)
ISR_STUB(7,  ERR_NONE)

/* Vector 8: error code (double fault) */
ISR_STUB(8,  ERR_CODE)

/* Vector 9: no error code (coprocessor segment) */
ISR_STUB(9,  ERR_NONE)

/* Vectors 10-14: error code */
ISR_STUB(10, ERR_CODE)
ISR_STUB(11, ERR_CODE)
ISR_STUB(12, ERR_CODE)
ISR_STUB(13, ERR_CODE)
ISR_STUB(14, ERR_CODE)

/* Vector 15: reserved (no error code) */
ISR_STUB(15, ERR_NONE)

/* Vector 16: no error code (x87 FP) */
ISR_STUB(16, ERR_NONE)

/* Vector 17: error code (alignment check) */
ISR_STUB(17, ERR_CODE)

/* Vector 18: no error code (machine check) */
ISR_STUB(18, ERR_NONE)

/* Vector 19: no error code (SIMD FP) */
ISR_STUB(19, ERR_NONE)

/* Vectors 20-31 */
ISR_STUB(20, ERR_NONE)
ISR_STUB(21, ERR_CODE)
ISR_STUB(22, ERR_NONE)
ISR_STUB(23, ERR_NONE)
ISR_STUB(24, ERR_NONE)
ISR_STUB(25, ERR_NONE)
ISR_STUB(26, ERR_NONE)
ISR_STUB(27, ERR_NONE)
ISR_STUB(28, ERR_NONE)
ISR_STUB(29, ERR_CODE)
ISR_STUB(30, ERR_CODE)
ISR_STUB(31, ERR_NONE)

/* Table of ISR entry points for IDT loading */
typedef void (*isr_fn)(void);
static const isr_fn isr_table[32] = {
    isr_0,  isr_1,  isr_2,  isr_3,  isr_4,  isr_5,  isr_6,  isr_7,
    isr_8,  isr_9,  isr_10, isr_11, isr_12, isr_13, isr_14, isr_15,
    isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23,
    isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31
};

/* ========================================================================
 * IDT setup (64-bit gate descriptors)
 * ======================================================================== */

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) idt64_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt64_ptr_t;

static idt64_entry_t idt64[32];
static idt64_ptr_t  idtr64;

static void panic_idt_set_gate(uint32_t vec, uint64_t handler) {
    idt64[vec].offset_low  = handler & 0xFFFF;
    idt64[vec].offset_mid  = (handler >> 16) & 0xFFFF;
    idt64[vec].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt64[vec].selector    = 0x08;      /* Kernel code selector */
    idt64[vec].ist         = 0;
    idt64[vec].type_attr   = 0x8E;      /* Present, DPL=0, 64-bit interrupt gate */
    idt64[vec].reserved    = 0;
}

static void panic_idt_install(void) {
    for (int i = 0; i < 32; i++)
        panic_idt_set_gate(i, (uint64_t)isr_table[i]);

    idtr64.limit = sizeof(idt64) - 1;
    idtr64.base  = (uint64_t)&idt64;

    asm volatile("lidt %0" : : "m"(idtr64));
}

/* ========================================================================
 * Core panic entry point (called from ISR stubs)
 * ======================================================================== */

void panic_catch(uint64_t vector, uint64_t error_code, panic_regs_t *regs) {
    /* Recursive guard: if we're already panicking, just reboot */
    if (pstate.panicking) {
        panic_reboot();
        return;
    }
    pstate.panicking = 1;

    /* Disable all interrupts */
    asm volatile("cli");

    /* Capture faulting address and page table base */
    uint32_t cr2_lo, cr3_lo;
    asm volatile("mov %%cr2, %0" : "=a"(cr2_lo));
    asm volatile("mov %%cr3, %0" : "=a"(cr3_lo));
    uint64_t cr2 = cr2_lo;
    uint64_t cr3 = cr3_lo;

    /* Capture TSC */
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t tsc = ((uint64_t)hi << 32) | lo;

    /* Fill in the register dump */
    regs->vector     = vector;
    regs->error_code = error_code;
    regs->cr2        = cr2;
    regs->cr3        = cr3;
    regs->rdtsc      = tsc;

    /* ---- Phase 1: Stream skull + registers over COM1 ---- */
    panic_serial_init();
    panic_serial_puts("\n\n");
    panic_stream_skull();
    panic_stream_regs(regs);

    /* ---- Phase 2: Secure the filesystem ---- */
    panic_secure_filesystem();

    /* ---- Phase 3: Encrypt + store register dump ---- */
    panic_encrypt_and_store(regs);

    /* ---- Phase 4: Display on VGA (if available) ---- */
    uint8_t *vga = (uint8_t *)0xB8000;
    /* Red background, white text */
    for (uint32_t i = 0; i < 80 * 25 * 2; i += 2) {
        vga[i + 1] = 0x4F;  /* white on red */
    }

    /* Print fault name at top of screen */
    const char *fault_name = (vector < 32) ? exception_names[vector] : "???";
    const char *prefix = "PANIC: ";
    uint32_t col = 0;
    for (int i = 0; prefix[i]; i++) {
        vga[(0 * 80 + col) * 2] = prefix[i];
        vga[(0 * 80 + col) * 2 + 1] = 0x4F;
        col++;
    }
    for (int i = 0; fault_name[i]; i++) {
        vga[(0 * 80 + col) * 2] = fault_name[i];
        vga[(0 * 80 + col) * 2 + 1] = 0x4F;
        col++;
    }

    /* Print key registers on screen */
    uint32_t row = 2;
    const char *short_labels[] = {"RIP", "RSP", "RBP", "CR2", "ERR"};
    uint64_t short_vals[] = {regs->rip, regs->rsp, regs->rbp, cr2, error_code};
    for (int r = 0; r < 5; r++) {
        uint32_t c = 1;
        for (int i = 0; short_labels[r][i]; i++) {
            vga[(row * 80 + c) * 2] = short_labels[r][i];
            vga[(row * 80 + c) * 2 + 1] = 0x4F;
            c++;
        }
        vga[(row * 80 + c) * 2] = '=';
        vga[(row * 80 + c) * 2 + 1] = 0x4F;
        c++;
        char hex[17];
        panic_put_hex64(hex, short_vals[r]);
        for (int i = 0; i < 16; i++) {
            vga[(row * 80 + c) * 2] = hex[i];
            vga[(row * 80 + c) * 2 + 1] = 0x4F;
            c++;
        }
        row++;
    }

    /* Print "BrainFS unmounted" + "Rebooting..." */
    row++;
    const char *msg1 = "BrainFS: unmounted, cluster VA space cleared.";
    uint32_t c = 1;
    for (int i = 0; msg1[i]; i++) {
        vga[(row * 80 + c) * 2] = msg1[i];
        vga[(row * 80 + c) * 2 + 1] = 0x4F;
        c++;
    }
    row++;
    c = 1;
    const char *msg2 = "Panic data encrypted with ChaCha20-Poly1305 → circular buffer.";
    for (int i = 0; msg2[i]; i++) {
        vga[(row * 80 + c) * 2] = msg2[i];
        vga[(row * 80 + c) * 2 + 1] = 0x4F;
        c++;
    }
    row++;
    c = 1;
    const char *msg3 = "Rebooting...";
    for (int i = 0; msg3[i]; i++) {
        vga[(row * 80 + c) * 2] = msg3[i];
        vga[(row * 80 + c) * 2 + 1] = 0x4F;
        c++;
    }

    /* ---- Phase 5: Final serial flush + reboot ---- */
    panic_serial_puts("\nBrainFS unmounted, cluster VA space cleared.\n");
    panic_serial_puts("Panic dump encrypted with ChaCha20-Poly1305.\n");
    panic_serial_puts("Stored in circular buffer slot ");
    {
        char n[4];
        panic_put_dec(n, (pstate.write_idx + PANIC_BUF_SLOTS - 1) % PANIC_BUF_SLOTS);
        panic_serial_puts(n);
    }
    panic_serial_puts(".\n");
    panic_serial_puts("Rebooting...\n");

    /* Small delay so the message is visible on serial */
    for (volatile uint32_t i = 0; i < 5000000; i++)
        asm volatile("nop");

    panic_reboot();
}

/* ========================================================================
 * Public API
 * ======================================================================== */

int panic_init(void) {
    /* Generate encryption key */
    sec_random_bytes(pstate.key, 32);

    /* Clear buffer */
    for (uint32_t i = 0; i < sizeof(panic_state_t); i++)
        ((uint8_t *)&pstate)[i] = 0;

    /* Restore key (cleared by the zeroing above) */
    sec_random_bytes(pstate.key, 32);

    pstate.armed       = 1;
    pstate.panicking   = 0;
    pstate.write_idx   = 0;
    pstate.drop_count  = 0;
    pstate.total_panics = 0;

    /* Install IDT exception handlers */
    panic_idt_install();

    return 0;
}

void panic(uint8_t vector, const char *msg) {
    /* Software-initiated panic: build a minimal regs struct */
    panic_regs_t regs;
    for (uint32_t i = 0; i < sizeof(panic_regs_t); i++)
        ((uint8_t *)&regs)[i] = 0;

    /* Capture the caller's EIP approximately */
    uint32_t ret_addr;
    asm volatile("mov (%%ebp), %%eax; mov 4(%%eax), %0" : "=r"(ret_addr) : : "eax");
    regs.rip = ret_addr;

    /* Capture ESP */
    uint32_t esp_val;
    asm volatile("mov %%esp, %0" : "=r"(esp_val));
    regs.rsp = esp_val;

    /* Capture EFLAGS */
    uint32_t eflags;
    asm volatile("lahf; seto %%al; movl %%eax, %0" : "=r"(eflags) : : "eax");
    regs.rflags = eflags;

    /* CR2/CR3 */
    uint32_t cr2_lo, cr3_lo;
    asm volatile("mov %%cr2, %0" : "=a"(cr2_lo));
    asm volatile("mov %%cr3, %0" : "=a"(cr3_lo));
    regs.cr2 = cr2_lo;
    regs.cr3 = cr3_lo;

    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    regs.rdtsc = ((uint64_t)hi << 32) | lo;

    regs.vector = vector;
    regs.error_code = 0;

    /* Stream the message */
    panic_serial_init();
    if (msg) {
        panic_serial_puts("PANIC MSG: ");
        panic_serial_puts(msg);
        panic_serial_puts("\n");
    }

    panic_catch(vector, 0, &regs);
}

int panic_is_armed(void) {
    return pstate.armed;
}

const panic_state_t *panic_get_state(void) {
    return &pstate;
}

int panic_read_slot(uint8_t index, panic_slot_t *out) {
    if (index >= PANIC_BUF_SLOTS) return -1;
    panic_slot_t *slot = &pstate.slots[index];
    if (slot->valid != 0xFF) return -1;

    /* Copy slot */
    for (uint32_t i = 0; i < sizeof(panic_slot_t); i++)
        ((uint8_t *)out)[i] = ((uint8_t *)slot)[i];

    return 0;
}
