#include <stdint.h>
#include "console.h"
#include "disk.h"

static void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static int ata_wait_bsy(void) {
    for (int i = 0; i < 100000; i++)
        if (!(inb(0x1F7) & 0x80)) return 1;
    return 0;
}

static int ata_wait_drq(void) {
    for (int i = 0; i < 100000; i++) {
        uint8_t st = inb(0x1F7);
        if (st & 0x01) return 0;
        if (st & 0x08) return 1;
    }
    return 0;
}

int disk_ata_read(uint32_t lba, uint32_t count, void *buf) {
    if (!ata_wait_bsy()) return 0;
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, count > 0 ? (uint8_t)count : 0);
    outb(0x1F3, (uint8_t)lba);
    outb(0x1F4, (uint8_t)(lba >> 8));
    outb(0x1F5, (uint8_t)(lba >> 16));
    outb(0x1F7, 0x20);
    uint16_t *ptr = (uint16_t *)buf;
    for (uint32_t s = 0; s < count; s++) {
        if (!ata_wait_drq()) return 0;
        for (int i = 0; i < 256; i++)
            ptr[s * 256 + i] = inw(0x1F0);
    }
    return 1;
}

int disk_load_modules(module_dir_t *dir) {
    cons_color("  Loading modules...\n", COL_DEFAULT);
    uint32_t loaded = 0;
    uint32_t next_load = MODULE_LOAD_ADDR;
    for (uint32_t i = 0; i < dir->count && i < 16; i++) {
        uint32_t lba = dir->entries[i].lba;
        uint32_t sectors = dir->entries[i].sectors;
        if (lba == 0 || sectors == 0) continue;
        cons_color("    ", COL_DEFAULT);
        cons_color(dir->entries[i].name, COL_LABEL);
        cons_color(" [LBA ", COL_DEFAULT);
        cons_dec32(lba);
        cons_color(" + ", COL_DEFAULT);
        cons_dec32(sectors);
        cons_color(" sectors]...", COL_DEFAULT);
        dir->entries[i].load_addr = next_load;
        if (disk_ata_read(lba, sectors, (void *)(uint64_t)next_load)) {
            cons_color(" loaded at ", COL_OK);
            cons_hex32(next_load);
            cons_puts("\n");
            next_load += sectors * 512;
            loaded++;
        } else {
            cons_color(" FAILED\n", COL_ERR);
        }
    }
    return loaded;
}
