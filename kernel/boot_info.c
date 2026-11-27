#include "boot_info.h"

boot_info_t kernel_boot_info;
int kernel_boot_info_valid = 0;

void kernel_boot_info_init(boot_info_t *source) {
    if (!source || source->magic != BOOT_INFO_MAGIC) return;
    if (source->version != BOOT_INFO_VERSION) return;

    __builtin_memcpy(&kernel_boot_info, source, sizeof(kernel_boot_info));
    kernel_boot_info_valid = 1;
}
