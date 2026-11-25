#ifndef KERNEL_BOOT_INFO_H
#define KERNEL_BOOT_INFO_H

#include "../include/boot/bootinfo.h"

extern boot_info_t kernel_boot_info;
extern int kernel_boot_info_valid;

/* Copy the bootloader-provided structure into kernel-owned memory. */
void kernel_boot_info_init(boot_info_t *source);

#endif
