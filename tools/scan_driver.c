/*
 * Chicago-95 Driver Scanner
 *
 * Scans /sys/bus/pci/devices on the build machine and generates
 * a Makefile fragment with ONLY the driver sources needed for
 * the detected hardware.
 *
 * Usage:
 *   gcc -o scan_driver scan_driver.c
 *   ./scan_driver > build/drivers.mk
 *   make -f Makefile DRIVERS_MK=build/drivers.mk
 *
 * Or simply:
 *   make scan
 *
 * The database below maps PCI vendor:device IDs (or wildcards)
 * to the source files required to support them.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#define MAX_DEVICES 512
#define MAX_MATCHES 128
#define SYSFS_PATH  "/sys/bus/pci/devices"

typedef struct {
    uint16_t vendor;
    uint16_t device;
} pci_id_t;

typedef struct {
    pci_id_t  ids[32];
    uint32_t  id_count;
    const char *name;
    const char *src;          /* source file relative to project root */
    int        required;      /* 1 = always include */
} driver_entry_t;

static driver_entry_t driver_db[] = {
    /* ---- Required: always compiled ---- */
    { .ids = {{0,0}}, .id_count = 0, .name = "core",
      .src = "stage2/main.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "security-crypto",
      .src = "stage2/security/common/aes/aes256.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "security-sha256",
      .src = "stage2/security/common/sha256/sha256.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "security-sha512",
      .src = "stage2/security/common/sha512/sha512_stub.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "security-chacha20",
      .src = "stage2/security/common/chacha20/chacha20.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "security-poly1305",
      .src = "stage2/security/common/poly1305/poly1305_stub.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "security-rfc4",
      .src = "stage2/security/common/rfc4/rfc4.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "security-hkdf",
      .src = "stage2/security/common/hkdf/hkdf.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "security-crypto2",
      .src = "stage2/security/common/crypto/crypto.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "security-panic",
      .src = "stage2/security/common/panic/panic.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "security-pgp",
      .src = "stage2/security/common/pgp/pgp.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "security-pgp-mpi",
      .src = "stage2/security/common/pgp/pgp_mpi.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "security-netguard",
      .src = "stage2/security/netguard/netguard.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "fs-brainfs-fat",
      .src = "stage2/fs/brainfs_fat.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "fs-brainfs-core",
      .src = "stage2/fs/brainfs_core.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "fs-brainvfs",
      .src = "stage2/fs/brainvfs.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "fs-encfs",
      .src = "stage2/fs/encfs_mount.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "vga",
      .src = "stage2/vga/vga.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "memory-pmm",
      .src = "stage2/memory/pmm.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "memory-vmm",
      .src = "stage2/memory/vmm.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "menu",
      .src = "stage2/menu/menu.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "tape-ata",
      .src = "stage2/tape/ata.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "tape-floppy",
      .src = "stage2/tape/floppy.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "boot-ring0",
      .src = "stage2/boot/ring0_init.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "boot-preinit",
      .src = "stage2/boot/pre_init.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "shell-main",
      .src = "stage2/shell/shell.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "shell-fish",
      .src = "stage2/shell/fish_shell.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "shell-gnu",
      .src = "stage2/shell/gnu_tools.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "shell-awk",
      .src = "stage2/shell/awk.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "shell-nano",
      .src = "stage2/shell/nano.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "boot-fs-menu",
      .src = "stage2/boot/fs_menu.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "mouse",
      .src = "stage2/drivers/mouse.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "gui",
      .src = "stage2/gui/gui.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "lib-real",
      .src = "lib/real/real.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "lib-protected",
      .src = "lib/protected/protected.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "lib-long",
      .src = "lib/long/long.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "lib-divmod64",
      .src = "lib/divmod64.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "lib-mem",
      .src = "lib/mem.c", .required = 1 },

    /* ---- NIC: always needed (referenced by all security modules) ---- */
    { .ids = {{0x8086, 0x100E}, {0x8086, 0x1502}, {0x8086, 0x1503},
              {0x8086, 0x153A}, {0x8086, 0x1533}},
      .id_count = 5, .name = "nic-e1000",
      .src = "stage2/drivers/nic_e1000.c", .required = 1 },

    /* ---- WIFI: Intel ---- */
    { .ids = {{0x8086, 0x08B1}, {0x8086, 0x095A}, {0x8086, 0x095B},
              {0x8086, 0x24F3}, {0x8086, 0x24FD}, {0x8086, 0x2526},
              {0x8086, 0xA370}, {0x8086, 0x2723}, {0x8086, 0x2725},
              {0x8086, 0x272B}, {0x8086, 0x272C}},
      .id_count = 11, .name = "wifi-intel",
      .src = "stage2/drivers/wifi/intel/wifi_intel.c" },

    /* ---- WIFI: Atheros/Qualcomm ---- */
    { .ids = {{0x168C, 0x002B}, {0x168C, 0x002E}, {0x168C, 0x0030},
              {0x168C, 0x0032}, {0x168C, 0x0034}, {0x168C, 0x0036},
              {0x168C, 0x0037}, {0x168C, 0x003C}, {0x168C, 0x0046},
              {0x168C, 0x003E}, {0x168C, 0x0050}},
      .id_count = 11, .name = "wifi-atheros",
      .src = "stage2/drivers/wifi/atheros/wifi_atheros.c" },

    /* ---- WIFI: Broadcom ---- */
    { .ids = {{0x14E4, 0x4727}, {0x14E4, 0x4357}, {0x14E4, 0x4357},
              {0x14E4, 0x4349}, {0x14E4, 0x4331}, {0x14E4, 0x43A3},
              {0x14E4, 0x43A3}, {0x14E4, 0x43B1}, {0x14E4, 0x43DF},
              {0x14E4, 0x4360}, {0x14E4, 0x43E2}, {0x14E4, 0x4405}},
      .id_count = 12, .name = "wifi-broadcom",
      .src = "stage2/drivers/wifi/broadcom/wifi_broadcom.c" },

    /* ---- WIFI: Realtek ---- */
    { .ids = {{0x10EC, 0x8176}, {0x10EC, 0x8177}, {0x10EC, 0x8723},
              {0x10EC, 0x8812}, {0x10EC, 0x8813}, {0x10EC, 0x8821},
              {0x10EC, 0xB822}, {0x10EC, 0x8179}, {0x10EC, 0x818B}},
      .id_count = 9, .name = "wifi-realtek",
      .src = "stage2/drivers/wifi/realtek/wifi_realtek.c" },

    /* ---- WIFI: Ralink/MediaTek ---- */
    { .ids = {{0x1814, 0x0201}, {0x1814, 0x0301}, {0x1814, 0x0401},
              {0x1814, 0x0601}, {0x1814, 0x3090}, {0x1814, 0x3290},
              {0x1814, 0x539F}, {0x1814, 0x5592},
              {0x14C3, 0x7601}, {0x14C3, 0x7603}, {0x14C3, 0x7610},
              {0x14C3, 0x7612}, {0x14C3, 0x7615}, {0x14C3, 0x7620},
              {0x14C3, 0x7622}, {0x14C3, 0x7663}, {0x14C3, 0x7961}},
      .id_count = 17, .name = "wifi-mediatek",
      .src = "stage2/drivers/wifi/mediatek/wifi_mediatek.c" },

    /* ---- WIFI: Marvell ---- */
    { .ids = {{0x11AB, 0x2A01}, {0x11AB, 0x2A02}, {0x11AB, 0x2A03},
              {0x11AB, 0x2A04}, {0x11AB, 0x2B38}, {0x11AB, 0x2B39}},
      .id_count = 6, .name = "wifi-marvell",
      .src = "stage2/drivers/wifi/marvell/wifi_marvell.c" },

    /* ---- WIFI: Prism/Intersil ---- */
    { .ids = {{0x1260, 0x3877}, {0x1260, 0x3880}, {0x1260, 0x3886},
              {0x1260, 0x3887}, {0x1260, 0x3890}},
      .id_count = 5, .name = "wifi-prism5",
      .src = "stage2/drivers/wifi/prism5/wifi_prism5.c" },

    /* ---- WiFi core + autodetect (always needed if any wifi present) ---- */
    { .ids = {{0,0}}, .id_count = 0, .name = "wifi-core",
      .src = "stage2/drivers/wifi/wifi_core.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "wifi-autodetect",
      .src = "stage2/drivers/wifi/wifi_autodetect.c", .required = 1 },

    /* ---- Firewalls (always included per project design) ---- */
    { .ids = {{0,0}}, .id_count = 0, .name = "fw-packet-filter",
      .src = "stage2/security/firewall/gen2_packet_filter/fw_gen2_packet_filter.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "fw-stateful",
      .src = "stage2/security/firewall/gen2_stateful/fw_gen2_stateful.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "fw-app-layer",
      .src = "stage2/security/firewall/gen2_app_layer/fw_gen2_app_layer.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "fw-adaptive",
      .src = "stage2/security/firewall/gen2_adaptive/fw_gen2_adaptive.c", .required = 1 },

    /* ---- DNS Encrypters (always included) ---- */
    { .ids = {{0,0}}, .id_count = 0, .name = "dns-doh",
      .src = "stage2/security/dns_encrypt/doh/dns_doh.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "dns-dot",
      .src = "stage2/security/dns_encrypt/dot/dns_dot.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "dns-dnscrypt",
      .src = "stage2/security/dns_encrypt/dnscrypt/dns_dnscrypt.c", .required = 1 },

    /* ---- WiFi Encrypters (always included) ---- */
    { .ids = {{0,0}}, .id_count = 0, .name = "wpa2-aes",
      .src = "stage2/security/wifi_encrypt/wpa2_aes/wpa2_aes.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "wpa3-sae",
      .src = "stage2/security/wifi_encrypt/wpa3_sae/wpa3_sae.c", .required = 1 },

    /* ---- MAC Encrypters (always included) ---- */
    { .ids = {{0,0}}, .id_count = 0, .name = "mac-random",
      .src = "stage2/security/mac_encrypt/mac_random/mac_random.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "mac-clone",
      .src = "stage2/security/mac_encrypt/mac_clone/mac_clone.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "mac-mask",
      .src = "stage2/security/mac_encrypt/mac_mask/mac_mask.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "mac-rot",
      .src = "stage2/security/mac_encrypt/mac_rot/mac_rot.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "mac-oui",
      .src = "stage2/security/mac_encrypt/mac_oui/mac_oui.c", .required = 1 },

    /* ---- Anti-IP (always included) ---- */
    { .ids = {{0,0}}, .id_count = 0, .name = "anti-ip",
      .src = "stage2/security/anti_ip/anti_ip.c", .required = 1 },

    /* ---- Disk Encrypters (always included) ---- */
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-uuid-random",
      .src = "stage2/security/disk_encrypt/uuid_random/uuid_random.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-serial-mask",
      .src = "stage2/security/disk_encrypt/serial_mask/serial_mask.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-gpt-header",
      .src = "stage2/security/disk_encrypt/gpt_header/gpt_header.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-mbr-scramble",
      .src = "stage2/security/disk_encrypt/mbr_scramble/mbr_scramble.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-luks-camouflage",
      .src = "stage2/security/disk_encrypt/luks_camouflage/luks_camouflage.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-partname-encrypt",
      .src = "stage2/security/disk_encrypt/partname_encrypt/partname_encrypt.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-label-encrypt",
      .src = "stage2/security/disk_encrypt/label_encrypt/label_encrypt.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-smart-obfuscate",
      .src = "stage2/security/disk_encrypt/smart_obfuscate/smart_obfuscate.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-inquiry-scramble",
      .src = "stage2/security/disk_encrypt/inquiry_scramble/inquiry_scramble.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-fingerprint-rotator",
      .src = "stage2/security/disk_encrypt/fingerprint_rotator/fingerprint_rotator.c", .required = 1 },

    /* ---- Disk Privacy (always included) ---- */
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-priv-uuid-encrypt",
      .src = "stage2/security/disk_privacy/uuid_encrypt/uuid_encrypt.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-priv-uuid-rot",
      .src = "stage2/security/disk_privacy/uuid_rot/uuid_rot.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-priv-uuid-clone",
      .src = "stage2/security/disk_privacy/uuid_clone/uuid_clone.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-priv-serial-mask",
      .src = "stage2/security/disk_privacy/disk_serial_mask/disk_serial_mask.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-priv-model-spoof",
      .src = "stage2/security/disk_privacy/disk_model_spoof/disk_model_spoof.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-priv-label-enc",
      .src = "stage2/security/disk_privacy/disk_label_enc/disk_label_enc.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-priv-mbr-gpt-mask",
      .src = "stage2/security/disk_privacy/disk_mbr_gpt_mask/disk_mbr_gpt_mask.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-priv-lba-scramble",
      .src = "stage2/security/disk_privacy/disk_lba_scramble/disk_lba_scramble.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-priv-size-obfs",
      .src = "stage2/security/disk_privacy/disk_size_obfs/disk_size_obfs.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "disk-priv-fs-hide",
      .src = "stage2/security/disk_privacy/disk_fs_hide/disk_fs_hide.c", .required = 1 },

    /* ---- Tor ---- */
    { .ids = {{0,0}}, .id_count = 0, .name = "tor-core",
      .src = "stage2/security/tor/core/tor_core.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "tor-socks5",
      .src = "stage2/security/tor/socks5/tor_socks5.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "tor-directory",
      .src = "stage2/security/tor/directory/tor_directory.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "tor-hs",
      .src = "stage2/security/tor/hidden_service/tor_hs.c", .required = 1 },
    { .ids = {{0,0}}, .id_count = 0, .name = "tor-bootstrapper",
      .src = "stage2/security/tor/tor_bootstrapper.c", .required = 1 },
};

#define DB_COUNT (sizeof(driver_db) / sizeof(driver_db[0]))

static int pci_ids[MAX_DEVICES][2];
static int pci_count = 0;
static int matched[DB_COUNT];

static int read_sysfs_value(const char *path) {
    char buf[16];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    return (int)strtol(buf, NULL, 16);
}

static void scan_pci(void) {
    DIR *d = opendir(SYSFS_PATH);
    if (!d) {
        fprintf(stderr, "Warning: cannot open %s (not root? no PCI bus?)\n",
                SYSFS_PATH);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) && pci_count < MAX_DEVICES) {
        if (e->d_name[0] == '.') continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s/vendor", SYSFS_PATH, e->d_name);
        int vendor = read_sysfs_value(path);
        snprintf(path, sizeof(path), "%s/%s/device", SYSFS_PATH, e->d_name);
        int device = read_sysfs_value(path);
        if (vendor > 0 && device > 0) {
            pci_ids[pci_count][0] = vendor;
            pci_ids[pci_count][1] = device;
            pci_count++;
        }
    }
    closedir(d);
}

static void match_drivers(void) {
    for (uint32_t d = 0; d < DB_COUNT; d++) {
        matched[d] = driver_db[d].required ? 1 : 0;
    }
    for (int p = 0; p < pci_count; p++) {
        int v = pci_ids[p][0];
        int d = pci_ids[p][1];
        for (uint32_t drv = 0; drv < DB_COUNT; drv++) {
            if (matched[drv]) continue;
            for (uint32_t id = 0; id < driver_db[drv].id_count; id++) {
                if (driver_db[drv].ids[id].vendor == v &&
                    driver_db[drv].ids[id].device == d) {
                    matched[drv] = 1;
                    break;
                }
            }
        }
    }
}

static void print_report(void) {
    printf("# Chicago-95 Driver Scanner Report\n");
    printf("# Generated: scan of %d PCI devices found on this machine\n",
           pci_count);
    printf("#\n");
    printf("# Detected PCI devices:\n");
    for (int i = 0; i < pci_count; i++) {
        printf("#   %04X:%04X\n", pci_ids[i][0], pci_ids[i][1]);
    }
    printf("#\n");
    printf("# Selected drivers:\n");
    for (uint32_t d = 0; d < DB_COUNT; d++) {
        if (matched[d]) {
            printf("#   [%s] %s\n", driver_db[d].required ? "REQ" : "HW ", driver_db[d].name);
        } else {
            printf("#   [SKIP] %s (not needed)\n", driver_db[d].name);
        }
    }
    printf("#\n");
    printf("STAGE2_OPT_SRCS = \\\n");
    int first = 1;
    for (uint32_t d = 0; d < DB_COUNT; d++) {
        if (matched[d]) {
            printf("%s%s \\\n", first ? "" : "\t", driver_db[d].src);
            first = 0;
        }
    }
    printf("\n");
    printf("STAGE2_DRIVER_NAMES =");
    for (uint32_t d = 0; d < DB_COUNT; d++) {
        if (matched[d]) {
            printf(" \\\n\t%s", driver_db[d].name);
        }
    }
    printf("\n");
}

int main(void) {
    scan_pci();
    match_drivers();
    print_report();
    return 0;
}
