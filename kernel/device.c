/**
 * Chicago-95 Device Framework
 * Static device table, registration, lookup, built-in auto-registration
 */

#include "device.h"
#include "kernel.h"
#include "kmalloc.h"
#include "console.h"
#include "keyboard.h"
#include "timer.h"
#include "ata.h"
#include <stdint.h>

/* ---- Static device table ---- */

static device_t device_table[DEV_MAX];
static int device_count = 0;

/* ---- Console I/O device ops ---- */

static ssize_t console_dev_read(int minor, uint64_t offset, void *buf, size_t len) {
    (void)minor; (void)offset;
    /* Read from keyboard */
    char *kb = (char *)buf;
    size_t count = 0;
    while (count < len) {
        int ch = keyboard_getchar();
        if (ch < 0) break;
        if (ch == '\r') ch = '\n';
        kb[count++] = (char)ch;
        console_putc((char)ch, 0x07);
        if (ch == '\n') break;
    }
    return (ssize_t)count;
}

static ssize_t console_dev_write(int minor, uint64_t offset, const void *buf, size_t len) {
    (void)minor; (void)offset;
    char tmp[257];
    size_t written = 0;
    while (written < len) {
        size_t chunk = len - written;
        if (chunk > 256) chunk = 256;
        memcpy(tmp, (const char *)buf + written, chunk);
        tmp[chunk] = '\0';
        console_puts(tmp, 0x07);
        written += chunk;
    }
    return (ssize_t)len;
}

static int console_dev_open(int minor) { (void)minor; return 0; }
static int console_dev_close(int minor) { (void)minor; return 0; }
static int console_dev_ioctl(int minor, int cmd, uint64_t arg) {
    (void)minor; (void)cmd; (void)arg;
    return -1;
}

static dev_ops_t console_ops = {
    .read  = console_dev_read,
    .write = console_dev_write,
    .ioctl = console_dev_ioctl,
    .open  = console_dev_open,
    .close = console_dev_close,
};

/* ---- Serial device ops ---- */

static ssize_t serial_dev_read(int minor, uint64_t offset, void *buf, size_t len) {
    (void)minor; (void)offset;
    char *kb = (char *)buf;
    size_t count = 0;
    while (count < len) {
        /* Read from serial port 0x3F8 (COM1) */
        if (!(inb(0x3FD) & 0x01)) break;
        kb[count++] = inb(0x3F8);
    }
    return (ssize_t)count;
}

static ssize_t serial_dev_write(int minor, uint64_t offset, const void *buf, size_t len) {
    (void)minor; (void)offset;
    const char *s = (const char *)buf;
    for (size_t i = 0; i < len; i++) {
        /* Wait for transmit buffer empty */
        while (!(inb(0x3FD) & 0x20));
        outb(0x3F8, s[i]);
    }
    return (ssize_t)len;
}

static dev_ops_t serial_ops = {
    .read  = serial_dev_read,
    .write = serial_dev_write,
    .ioctl = NULL,
    .open  = NULL,
    .close = NULL,
};

/* ---- Keyboard device ops ---- */

static ssize_t keyboard_dev_read(int minor, uint64_t offset, void *buf, size_t len) {
    (void)minor; (void)offset;
    char *kb = (char *)buf;
    size_t count = 0;
    while (count < len) {
        int ch = keyboard_getchar();
        if (ch < 0) break;
        kb[count++] = (char)ch;
    }
    return (ssize_t)count;
}

static dev_ops_t keyboard_ops = {
    .read  = keyboard_dev_read,
    .write = NULL,
    .ioctl = NULL,
    .open  = NULL,
    .close = NULL,
};

/* ---- Timer device ops (read returns tick count) ---- */

static ssize_t timer_dev_read(int minor, uint64_t offset, void *buf, size_t len) {
    (void)minor; (void)offset;
    if (len < sizeof(uint64_t)) return -1;
    uint64_t ticks = timer_get_ticks();
    memcpy(buf, &ticks, sizeof(uint64_t));
    return sizeof(uint64_t);
}

static dev_ops_t timer_ops = {
    .read  = timer_dev_read,
    .write = NULL,
    .ioctl = NULL,
    .open  = NULL,
    .close = NULL,
};

/* ---- ATA block device ops ---- */

static ssize_t ata_dev_read(int minor, uint64_t offset, void *buf, size_t len) {
    (void)minor;
    uint32_t lba = (uint32_t)(offset / 512);
    uint8_t sectors = (uint8_t)((len + 511) / 512);
    if (sectors == 0) sectors = 1;
    uint16_t *tmp = (uint16_t *)kmalloc_aligned(sectors * 512);
    if (!tmp) return -1;
    int res = ata_read_sectors(0, lba, sectors, tmp);
    if (res < 0) { kfree(tmp); return -1; }
    size_t copy = len;
    if (copy > (size_t)sectors * 512) copy = (size_t)sectors * 512;
    memcpy(buf, tmp, copy);
    kfree(tmp);
    return (ssize_t)copy;
}

static ssize_t ata_dev_write(int minor, uint64_t offset, const void *buf, size_t len) {
    (void)minor;
    uint32_t lba = (uint32_t)(offset / 512);
    uint8_t sectors = (uint8_t)((len + 511) / 512);
    if (sectors == 0) sectors = 1;
    uint16_t *tmp = (uint16_t *)kmalloc_aligned(sectors * 512);
    if (!tmp) return -1;
    size_t copy = len;
    if (copy > (size_t)sectors * 512) copy = (size_t)sectors * 512;
    memset(tmp, 0, sectors * 512);
    memcpy(tmp, buf, copy);
    int res = ata_write_sectors(0, lba, sectors, tmp);
    kfree(tmp);
    if (res < 0) return -1;
    return (ssize_t)copy;
}

static dev_ops_t ata_ops = {
    .read  = ata_dev_read,
    .write = ata_dev_write,
    .ioctl = NULL,
    .open  = NULL,
    .close = NULL,
};

/* ---- NVMe block device ops (stub) ---- */

static ssize_t nvme_dev_read(int minor, uint64_t offset, void *buf, size_t len) {
    (void)minor; (void)offset; (void)buf; (void)len;
    return -1;
}

static ssize_t nvme_dev_write(int minor, uint64_t offset, const void *buf, size_t len) {
    (void)minor; (void)offset; (void)buf; (void)len;
    return -1;
}

static dev_ops_t nvme_ops = {
    .read  = nvme_dev_read,
    .write = nvme_dev_write,
    .ioctl = NULL,
    .open  = NULL,
    .close = NULL,
};

/* ---- Public API ---- */

int device_register(const char *name, uint32_t type, uint32_t major,
                    uint32_t minor, dev_ops_t *ops) {
    if (device_count >= DEV_MAX) return -1;

    /* Check for duplicate */
    for (int i = 0; i < device_count; i++) {
        if (strcmp(device_table[i].name, name) == 0) return -1;
    }

    device_t *dev = &device_table[device_count];
    int nlen = strlen(name);
    if (nlen >= DEV_NAME_LEN) nlen = DEV_NAME_LEN - 1;
    memcpy(dev->name, name, nlen);
    dev->name[nlen] = '\0';
    dev->type  = type;
    dev->major = major;
    dev->minor = minor;
    dev->ops   = ops;
    dev->impl_data = 0;
    dev->in_use = 1;

    device_count++;
    return 0;
}

device_t *device_find_by_name(const char *name) {
    for (int i = 0; i < device_count; i++) {
        if (device_table[i].in_use && strcmp(device_table[i].name, name) == 0) {
            return &device_table[i];
        }
    }
    return NULL;
}

device_t *device_find_by_major_minor(uint32_t major, uint32_t minor) {
    for (int i = 0; i < device_count; i++) {
        if (device_table[i].in_use &&
            device_table[i].major == major &&
            device_table[i].minor == minor) {
            return &device_table[i];
        }
    }
    return NULL;
}

void device_list(void) {
    console_puts("=== Registered Devices ===\n", 0x0B);
    console_puts("Name            Type  Major  Minor\n", 0x07);
    console_puts("─────────────────────────────────\n", 0x08);

    for (int i = 0; i < device_count; i++) {
        if (!device_table[i].in_use) continue;

        char line[128];
        const char *type_str;
        switch (device_table[i].type) {
            case DEV_CHAR:  type_str = "CHAR "; break;
            case DEV_BLOCK: type_str = "BLOCK"; break;
            case DEV_NET:   type_str = "NET  "; break;
            default:        type_str = "?????"; break;
        }

        /* Manual formatting since no sprintf */
        int pos = 0;
        int nlen = strlen(device_table[i].name);
        memcpy(line + pos, device_table[i].name, nlen);
        pos += nlen;
        /* Pad to 16 chars */
        while (pos < 16) line[pos++] = ' ';
        memcpy(line + pos, type_str, 5); pos += 5;
        line[pos++] = ' ';

        /* Major */
        char num[12];
        uint32_t val = device_table[i].major;
        int nlen2 = 0;
        char rev[12];
        if (val == 0) { rev[0] = '0'; nlen2 = 1; }
        else { while (val > 0) { rev[nlen2++] = '0' + (val % 10); val /= 10; } }
        for (int j = 0; j < nlen2; j++) num[j] = rev[nlen2 - 1 - j];
        num[nlen2] = '\0';
        memcpy(line + pos, num, nlen2); pos += nlen2;
        while (pos < 28) line[pos++] = ' ';

        /* Minor */
        val = device_table[i].minor;
        nlen2 = 0;
        if (val == 0) { rev[0] = '0'; nlen2 = 1; }
        else { while (val > 0) { rev[nlen2++] = '0' + (val % 10); val /= 10; } }
        for (int j = 0; j < nlen2; j++) num[j] = rev[nlen2 - 1 - j];
        num[nlen2] = '\0';
        memcpy(line + pos, num, nlen2); pos += nlen2;
        line[pos++] = '\n';
        line[pos] = '\0';

        console_puts(line, 0x07);
    }
    console_puts("═════════════════════════════════\n", 0x08);
}

void device_init(void) {
    memset(device_table, 0, sizeof(device_table));
    device_count = 0;

    /* Auto-register built-in devices */
    device_register("console", DEV_CHAR,  5, 1, &console_ops);
    device_register("tty",      DEV_CHAR,  5, 0, &console_ops);
    device_register("serial0",  DEV_CHAR,  4, 0, &serial_ops);
    device_register("keyboard", DEV_CHAR,  1, 1, &keyboard_ops);
    device_register("timer",    DEV_CHAR,  1, 8, &timer_ops);
    device_register("sda",      DEV_BLOCK, 8, 0, &ata_ops);
    device_register("nvme0",    DEV_BLOCK, 259, 0, &nvme_ops);

    console_puts("[DEV] device framework initialized, 7 built-in devices\n", 0x0A);
}
