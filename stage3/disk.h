#ifndef STAGE3_DISK_H
#define STAGE3_DISK_H

#include <stdint.h>
#include "boot/bootinfo.h"

#define MODULE_DIR_LBA   0x5000
#define MODULE_LOAD_ADDR 0x400000ULL
#define MODULE_DIR_MAGIC 0x4C444F4D

typedef struct {
    uint32_t magic;
    uint32_t count;
    uint8_t  _pad[8];
    struct {
        char     name[32];
        uint32_t lba;
        uint32_t sectors;
        uint32_t load_addr;
    } entries[16];
} __attribute__((packed)) module_dir_t;

int  disk_ata_read(uint32_t lba, uint32_t count, void *buf);
int  disk_load_modules(module_dir_t *dir, boot_module_t *mods, uint32_t capacity);

#endif
