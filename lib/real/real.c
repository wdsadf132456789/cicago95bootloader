/**
 * Chicago-95 Real Mode Utility Library
 * INT call wrappers, memory access, BIOS service helpers
 * Used by Stage 1 and Stage 2 pre-PM code
 */

#include <stdint.h>

/* ---- BIOS call (software interrupt) ---- */

typedef struct {
    uint16_t ax, bx, cx, dx, si, di, es, flags;
} bios_regs_t;

/* Call BIOS INT with register setup - stub for 32-bit compilation */
uint16_t bios_int(uint8_t int_num, bios_regs_t *regs) {
    (void)int_num; (void)regs;
    return 0;
}

/* ---- Disk I/O (INT 13h) ---- */

typedef struct {
    uint8_t  size;
    uint8_t  reserved;
    uint16_t count;
    uint16_t offset;
    uint16_t segment;
    uint64_t lba;
} __attribute__((packed)) real_dap_t;

int real_disk_read(uint8_t drive, uint64_t lba, uint32_t count,
                   uint16_t segment, uint16_t offset) {
    real_dap_t dap;
    dap.size = 0x10;
    dap.reserved = 0;
    dap.count = count;
    dap.offset = offset;
    dap.segment = segment;
    dap.lba = lba;

    uint16_t result;
    asm volatile(
        "int $0x13\n"
        : "=a"(result)
        : "a"(0x4200), "d"(drive), "S"(&dap)
    );
    return (result & 0xFF00) ? 0 : 1;
}

int real_disk_write(uint8_t drive, uint64_t lba, uint32_t count,
                    uint16_t segment, uint16_t offset) {
    real_dap_t dap;
    dap.size = 0x10;
    dap.reserved = 0;
    dap.count = count;
    dap.offset = offset;
    dap.segment = segment;
    dap.lba = lba;

    uint16_t result;
    asm volatile(
        "int $0x13\n"
        : "=a"(result)
        : "a"(0x4300), "d"(drive), "S"(&dap)
    );
    return (result & 0xFF00) ? 0 : 1;
}

int real_disk_reset(uint8_t drive) {
    uint16_t result;
    asm volatile(
        "int $0x13\n"
        : "=a"(result)
        : "a"(0x0000), "d"(drive)
    );
    return (result & 0xFF00) ? 0 : 1;
}

/* ---- Memory copy (stub for 32-bit build) ---- */

void real_memcpy(uint16_t dst_seg, uint16_t dst_off,
                 uint16_t src_seg, uint16_t src_off,
                 uint16_t len) {
    (void)dst_seg; (void)dst_off; (void)src_seg; (void)src_off; (void)len;
}

/* ---- Memory set (stub for 32-bit build) ---- */

void real_memset(uint16_t seg, uint16_t off, uint8_t val, uint16_t len) {
    (void)seg; (void)off; (void)val; (void)len;
}

/* ---- Port I/O ---- */

void real_outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

uint8_t real_inb(uint16_t port) {
    uint8_t val;
    asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* ---- String I/O for BIOS calls ---- */

void real_print_char(char c) {
    asm volatile(
        "int $0x10\n"
        :
        : "a"(0x0E00 | c), "b"(0x0007)
    );
}

void real_print_string(const char *s) {
    while (*s) {
        real_print_char(*s);
        s++;
    }
}

/* ---- Delay ---- */

void real_delay_ms(uint16_t ms) {
    asm volatile(
        "int $0x15\n"
        :
        : "a"(0x8600), "c"(ms), "d"(0)
    );
}
