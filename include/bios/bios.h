/**
 * Chicago-95 BIOS Interrupt Wrappers
 * Protected-mode safe wrappers for real BIOS INT calls (via V86 or real-mode trampoline)
 * Also includes direct PM-mode I/O port access
 */

#ifndef CHICAGO_BIOS_H
#define CHICAGO_BIOS_H

#include <stdint.h>

/* BIOS interrupt vector */
typedef struct {
    uint16_t offset;
    uint16_t segment;
} __attribute__((packed)) bios_ivt_entry_t;

/* DAP (Disk Address Packet) for INT 13h extensions */
typedef struct {
    uint8_t  size;
    uint8_t  reserved;
    uint16_t count;
    uint16_t offset;
    uint16_t segment;
    uint64_t lba;
} __attribute__((packed)) bios_dap_t;

/* E820 entry */
typedef struct {
    uint64_t base;
    uint64_t len;
    uint32_t type;
    uint32_t acpi_ext;
} __attribute__((packed)) bios_e820_entry_t;

/* VBE mode info block */
typedef struct {
    uint16_t attributes;
    uint8_t  window_a;
    uint8_t  window_b;
    uint16_t granularity;
    uint16_t window_size;
    uint16_t segment_a;
    uint16_t segment_b;
    uint32_t win_func_ptr;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t  char_width;
    uint8_t  char_height;
    uint8_t  planes;
    uint8_t  bpp;
    uint8_t  banks;
    uint8_t  memory_model;
    uint8_t  bank_size;
    uint8_t  image_pages;
    uint8_t  reserved0;
    uint8_t  red_mask;
    uint8_t  red_position;
    uint8_t  green_mask;
    uint8_t  green_position;
    uint8_t  blue_mask;
    uint8_t  blue_position;
    uint8_t  rsvd_mask;
    uint8_t  rsvd_position;
    uint8_t  direct_color_modes;
    uint32_t phys_base;
    uint32_t off_screen_mem;
    uint16_t off_screen_size;
    uint8_t  reserved1[206];
} __attribute__((packed)) bios_vbe_mode_info_t;

/* ========================================================================
 * Disk I/O (INT 13h)
 * ======================================================================== */
int      bios_disk_read(uint8_t drive, uint64_t lba, uint32_t count,
                        uint16_t segment, uint16_t offset);
int      bios_disk_write(uint8_t drive, uint64_t lba, uint32_t count,
                         uint16_t segment, uint16_t offset);
int      bios_disk_get_size(uint8_t drive, uint64_t *total_sectors, uint16_t *sector_size);
int      bios_disk_reset(uint8_t drive);
int      bios_disk_extensions_present(uint8_t drive);

/* ========================================================================
 * Memory (INT 15h)
 * ======================================================================== */
int      bios_e820_get_map(bios_e820_entry_t *map, uint32_t max_entries, uint32_t *count);
uint64_t bios_get_memory_size(void);
int      bios_a20_enable(void);
int      bios_a20_disable(void);
int      bios_a20_is_enabled(void);

/* ========================================================================
 * Video (INT 10h)
 * ======================================================================== */
int      bios_vbe_set_mode(uint16_t mode);
int      bios_vbe_get_mode(uint16_t *mode, bios_vbe_mode_info_t *info);
int      bios_vbe_get_modes(uint16_t *modes, uint16_t max);
int      bios_set_cursor(uint8_t row, uint8_t col);
int      bios_cursor_visible(int visible);
int      bios_putchar(char c, uint8_t page);
int      bios_puts(const char *s, uint8_t page);
int      bios_scroll_up(uint8_t lines, uint8_t page);
int      bios_set_color(uint8_t attr, uint8_t page);

/* ========================================================================
 * PCI (INT 1Ah or direct I/O)
 * ======================================================================== */
uint32_t bios_pci_config_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg);
void     bios_pci_config_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t val);
uint16_t bios_pci_find_device(uint16_t vendor, uint16_t device);

/* ========================================================================
 * CMOS / RTC
 * ======================================================================== */
uint8_t  bios_cmos_read(uint8_t reg);
void     bios_cmos_write(uint8_t reg, uint8_t val);
uint8_t  bios_rtc_second(void);
uint8_t  bios_rtc_minute(void);
uint8_t  bios_rtc_hour(void);
uint32_t bios_rtc_timestamp(void);

/* ========================================================================
 * Timer / Misc
 * ======================================================================== */
void     bios_delay_ms(uint32_t ms);
uint32_t bios_get_tick_count(void);
void     bios_reboot(void);
void     bios_shutdown(void);

/* ========================================================================
 * Port I/O (direct PM-mode access)
 * ======================================================================== */
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    asm volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void outl(uint16_t port, uint32_t val) {
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    asm volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif /* CHICAGO_BIOS_H */
