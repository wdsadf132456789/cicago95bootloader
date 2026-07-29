#ifndef CHICAGO95_FS_MENU_H
#define CHICAGO95_FS_MENU_H

#include <stdint.h>

#define FS_MENU_DEFAULT_WIDTH  32
#define FS_MENU_TIMEOUT_TICKS  5000000000ULL  /* ~5 seconds at 3GHz TSC */

/* Supported FAT widths */
static const uint8_t fs_menu_widths[] = { 1, 2, 4, 8, 12, 16, 32, 64, 128 };
#define FS_MENU_NUM_WIDTHS  9

/* Filesystem mode */
typedef enum {
    FS_MODE_NONE    = 0,
    FS_MODE_BRAINFS = 1,
    FS_MODE_ENCFS   = 2
} fs_mode_t;

/* Global filesystem selection state */
typedef struct {
    uint8_t   fat_width;       /* 0 = none, else 1/2/4/8/12/16/32/64/128 */
    fs_mode_t mode;            /* NONE, BRAINFS, or ENCFS */
    uint8_t   drive;           /* boot drive (0x80) */
    uint8_t   mounted;
    uint8_t   selected_at_boot;
} fs_selection_t;

extern fs_selection_t fs_selection;

/* Show the filesystem selection menu on VGA, returns chosen width (0=none) */
uint8_t fs_menu_show(void);

/* Format disk with selected width */
int fs_menu_format(uint8_t drive, uint8_t fat_width);

#endif
