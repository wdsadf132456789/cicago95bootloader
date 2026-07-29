/**
 * Chicago-95 Virtual Filesystem
 * Tree-based VFS, fd table, devfs/procfs/tmpfs
 */

#include "vfs.h"
#include "kernel.h"
#include "kmalloc.h"
#include "console.h"
#include "timer.h"
#include "process.h"
#include "keyboard.h"
#include "ata.h"
#include <stdint.h>

/* ---- Global state ---- */

static vfs_node_t *vfs_root = NULL;
static vfs_fd_entry_t global_fd_table[VFS_MAX_FDS * MAX_PROCESSES];
static int global_fd_count = 0;

/* ---- Helper: allocate a VFS node ---- */

static vfs_node_t *vfs_alloc_node(void) {
    vfs_node_t *n = (vfs_node_t *)kmalloc_zero(sizeof(vfs_node_t));
    return n;
}

/* ---- Helper: create a node ---- */

static vfs_node_t *vfs_new_node(const char *name, uint32_t type, vfs_ops_t *ops) {
    vfs_node_t *n = vfs_alloc_node();
    if (!n) return NULL;

    int len = strlen(name);
    if (len >= VFS_NAME_LEN) len = VFS_NAME_LEN - 1;
    memcpy(n->name, name, len);
    n->name[len] = '\0';
    n->type = type;
    n->ops = ops;
    n->parent = NULL;
    n->child_count = 0;
    n->length = 0;
    n->inode = 0;
    n->impl_data = 0;
    return n;
}

/* ---- Helper: find child by name ---- */

static vfs_node_t *vfs_find_child(vfs_node_t *dir, const char *name) {
    if (!dir || dir->type != VFS_DIR) return NULL;
    for (uint32_t i = 0; i < dir->child_count; i++) {
        if (strcmp(dir->children[i]->name, name) == 0)
            return dir->children[i];
    }
    return NULL;
}

/* ---- Helper: attach child to directory ---- */

static int vfs_attach_child(vfs_node_t *dir, vfs_node_t *child) {
    if (!dir || dir->type != VFS_DIR) return -1;
    if (dir->child_count >= VFS_MAX_CHILDREN) return -1;
    child->parent = dir;
    dir->children[dir->child_count++] = child;
    return 0;
}

/* ---- Path token helper ---- */

static const char *path_skip_slashes(const char *p) {
    while (*p == '/') p++;
    return p;
}

/* ---- VFS operations: default (no-op) implementations ---- */

static int default_open(vfs_node_t *n, uint32_t f) { (void)n; (void)f; return 0; }
static int default_close(vfs_node_t *n) { (void)n; return 0; }

static ssize_t default_read(vfs_node_t *n, uint64_t off, void *buf, size_t len) {
    (void)n; (void)off; (void)buf; (void)len;
    return 0;
}

static ssize_t default_write(vfs_node_t *n, uint64_t off, const void *buf, size_t len) {
    (void)n; (void)off; (void)buf; (void)len;
    return 0;
}

static int default_readdir(vfs_node_t *n, uint32_t idx, vfs_node_t *out) {
    (void)n; (void)idx; (void)out;
    return -1;
}

static int default_finddir(vfs_node_t *n, const char *name, vfs_node_t *out) {
    (void)n; (void)name; (void)out;
    return -1;
}

static int default_create(vfs_node_t *n, const char *name, uint32_t type) {
    (void)n; (void)name; (void)type;
    return -1;
}

static int default_mkdir(vfs_node_t *n, const char *name) {
    (void)n; (void)name;
    return -1;
}

static int default_unlink(vfs_node_t *n, const char *name) {
    (void)n; (void)name;
    return -1;
}

static int default_stat(vfs_node_t *n, void *buf) {
    if (!buf) return -1;
    /* Fill a simple stat: type, size, inode */
    uint32_t *stat = (uint32_t *)buf;
    stat[0] = n->type;
    stat[1] = n->length;
    stat[2] = (uint32_t)n->inode;
    return 0;
}

/* =============================================
   DevFS — /dev
   ============================================= */

static ssize_t dev_null_read(vfs_node_t *n, uint64_t off, void *buf, size_t len) {
    (void)n; (void)off;
    memset(buf, 0, len);
    return (ssize_t)len;
}

static ssize_t dev_null_write(vfs_node_t *n, uint64_t off, const void *buf, size_t len) {
    (void)n; (void)off; (void)buf;
    return (ssize_t)len; /* /dev/null discards everything */
}

static ssize_t dev_zero_read(vfs_node_t *n, uint64_t off, void *buf, size_t len) {
    (void)n; (void)off;
    memset(buf, 0, len);
    return (ssize_t)len;
}

static ssize_t dev_console_read(vfs_node_t *n, uint64_t off, void *buf, size_t len) {
    (void)n; (void)off;
    char *kb = (char *)buf;
    size_t count = 0;
    while (count < len) {
        int ch = keyboard_getchar();
        if (ch < 0) break;
        if (ch == '\r') ch = '\n';
        kb[count++] = (char)ch;
        /* Echo */
        console_putc((char)ch, 0x07);
        if (ch == '\n') break;
    }
    return (ssize_t)count;
}

static ssize_t dev_console_write(vfs_node_t *n, uint64_t off, const void *buf, size_t len) {
    (void)n; (void)off;
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

static ssize_t dev_tty_read(vfs_node_t *n, uint64_t off, void *buf, size_t len) {
    return dev_console_read(n, off, buf, len);
}

static ssize_t dev_tty_write(vfs_node_t *n, uint64_t off, const void *buf, size_t len) {
    return dev_console_write(n, off, buf, len);
}

/* /dev/sda — ATA disk (raw block device) */
static ssize_t dev_sda_read(vfs_node_t *n, uint64_t off, void *buf, size_t len) {
    (void)n;
    uint32_t lba = (uint32_t)(off / 512);
    uint8_t sectors = (uint8_t)((len + 511) / 512);
    if (sectors == 0) sectors = 1;
    /* Allocate temp buffer for ATA (uint16_t per word) */
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

static ssize_t dev_sda_write(vfs_node_t *n, uint64_t off, const void *buf, size_t len) {
    (void)n;
    uint32_t lba = (uint32_t)(off / 512);
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

/* /dev/nvme0 — stub (NVMe not implemented yet, return error) */
static ssize_t dev_nvme0_read(vfs_node_t *n, uint64_t off, void *buf, size_t len) {
    (void)n; (void)off; (void)buf; (void)len;
    return -1; /* NVMe not available */
}

static ssize_t dev_nvme0_write(vfs_node_t *n, uint64_t off, const void *buf, size_t len) {
    (void)n; (void)off; (void)buf; (void)len;
    return -1;
}

static vfs_ops_t dev_ops = {
    .open    = default_open,
    .close   = default_close,
    .read    = default_read,
    .write   = default_write,
    .readdir = default_readdir,
    .finddir = default_finddir,
    .create  = default_create,
    .mkdir   = default_mkdir,
    .unlink  = default_unlink,
    .stat    = default_stat,
};

void devfs_init(void) {
    vfs_node_t *dev_root = vfs_new_node("dev", VFS_DIR, &dev_ops);
    if (!dev_root) return;

    /* /dev/null */
    vfs_node_t *null_dev = vfs_new_node("null", VFS_CHARDEV, &dev_ops);
    null_dev->major = 1; null_dev->minor = 3;
    vfs_ops_t *null_ops = (vfs_ops_t *)kmalloc(sizeof(vfs_ops_t));
    memcpy(null_ops, &dev_ops, sizeof(vfs_ops_t));
    null_ops->read  = dev_null_read;
    null_ops->write = dev_null_write;
    null_dev->ops = null_ops;
    vfs_attach_child(dev_root, null_dev);

    /* /dev/zero */
    vfs_node_t *zero_dev = vfs_new_node("zero", VFS_CHARDEV, &dev_ops);
    zero_dev->major = 1; zero_dev->minor = 5;
    vfs_ops_t *zero_ops = (vfs_ops_t *)kmalloc(sizeof(vfs_ops_t));
    memcpy(zero_ops, &dev_ops, sizeof(vfs_ops_t));
    zero_ops->read = dev_zero_read;
    zero_dev->ops = zero_ops;
    vfs_attach_child(dev_root, zero_dev);

    /* /dev/console */
    vfs_node_t *cons_dev = vfs_new_node("console", VFS_CHARDEV, &dev_ops);
    cons_dev->major = 5; cons_dev->minor = 1;
    vfs_ops_t *cons_ops = (vfs_ops_t *)kmalloc(sizeof(vfs_ops_t));
    memcpy(cons_ops, &dev_ops, sizeof(vfs_ops_t));
    cons_ops->read  = dev_console_read;
    cons_ops->write = dev_console_write;
    cons_dev->ops = cons_ops;
    vfs_attach_child(dev_root, cons_dev);

    /* /dev/tty */
    vfs_node_t *tty_dev = vfs_new_node("tty", VFS_CHARDEV, &dev_ops);
    tty_dev->major = 5; tty_dev->minor = 0;
    vfs_ops_t *tty_ops = (vfs_ops_t *)kmalloc(sizeof(vfs_ops_t));
    memcpy(tty_ops, &dev_ops, sizeof(vfs_ops_t));
    tty_ops->read  = dev_tty_read;
    tty_ops->write = dev_tty_write;
    tty_dev->ops = tty_ops;
    vfs_attach_child(dev_root, tty_dev);

    /* /dev/sda */
    vfs_node_t *sda_dev = vfs_new_node("sda", VFS_BLOCKDEV, &dev_ops);
    sda_dev->major = 8; sda_dev->minor = 0;
    sda_dev->length = 512 * 2048 * 1024; /* ~1GB placeholder */
    vfs_ops_t *sda_ops = (vfs_ops_t *)kmalloc(sizeof(vfs_ops_t));
    memcpy(sda_ops, &dev_ops, sizeof(vfs_ops_t));
    sda_ops->read  = dev_sda_read;
    sda_ops->write = dev_sda_write;
    sda_dev->ops = sda_ops;
    vfs_attach_child(dev_root, sda_dev);

    /* /dev/nvme0 */
    vfs_node_t *nvme_dev = vfs_new_node("nvme0", VFS_BLOCKDEV, &dev_ops);
    nvme_dev->major = 259; nvme_dev->minor = 0;
    vfs_ops_t *nvme_ops = (vfs_ops_t *)kmalloc(sizeof(vfs_ops_t));
    memcpy(nvme_ops, &dev_ops, sizeof(vfs_ops_t));
    nvme_ops->read  = dev_nvme0_read;
    nvme_ops->write = dev_nvme0_write;
    nvme_dev->ops = nvme_ops;
    vfs_attach_child(dev_root, nvme_dev);

    /* Attach dev root to vfs root */
    dev_root->parent = vfs_root;
    vfs_root->children[vfs_root->child_count++] = dev_root;

    console_puts("[VFS] devfs mounted at /dev\n", 0x0A);
}

/* =============================================
   ProcFS — /proc
   ============================================= */

static ssize_t proc_meminfo_read(vfs_node_t *n, uint64_t off, void *buf, size_t len) {
    (void)n; (void)off;
    char tmp[512];
    uint64_t free_pages = pmm_get_free_pages();
    uint64_t total_pages = pmm_get_total_pages();
    uint64_t used_pages = total_pages - free_pages;

    /* Build the meminfo string */
    int pos = 0;
    const char *hdr = "MemTotal:        ";
    memcpy(tmp + pos, hdr, strlen(hdr)); pos += strlen(hdr);
    /* Write total in pages as decimal */
    char num[32];
    uint64_t val = total_pages;
    int nlen = 0;
    char rev[32];
    if (val == 0) { rev[0] = '0'; nlen = 1; }
    else { while (val > 0) { rev[nlen++] = '0' + (val % 10); val /= 10; } }
    for (int i = 0; i < nlen; i++) num[i] = rev[nlen - 1 - i];
    num[nlen] = '\0';
    memcpy(tmp + pos, num, nlen); pos += nlen;
    const char *sfx = " pages\n";
    memcpy(tmp + pos, sfx, strlen(sfx)); pos += strlen(sfx);

    const char *hdr2 = "MemFree:         ";
    memcpy(tmp + pos, hdr2, strlen(hdr2)); pos += strlen(hdr2);
    val = free_pages; nlen = 0;
    if (val == 0) { rev[0] = '0'; nlen = 1; }
    else { while (val > 0) { rev[nlen++] = '0' + (val % 10); val /= 10; } }
    for (int i = 0; i < nlen; i++) num[i] = rev[nlen - 1 - i];
    num[nlen] = '\0';
    memcpy(tmp + pos, num, nlen); pos += nlen;
    memcpy(tmp + pos, sfx, strlen(sfx)); pos += strlen(sfx);

    const char *hdr3 = "MemUsed:         ";
    memcpy(tmp + pos, hdr3, strlen(hdr3)); pos += strlen(hdr3);
    val = used_pages; nlen = 0;
    if (val == 0) { rev[0] = '0'; nlen = 1; }
    else { while (val > 0) { rev[nlen++] = '0' + (val % 10); val /= 10; } }
    for (int i = 0; i < nlen; i++) num[i] = rev[nlen - 1 - i];
    num[nlen] = '\0';
    memcpy(tmp + pos, num, nlen); pos += nlen;
    memcpy(tmp + pos, sfx, strlen(sfx)); pos += strlen(sfx);

    tmp[pos] = '\0';
    size_t tlen = pos;
    if (off >= tlen) return 0;
    size_t avail = tlen - (size_t)off;
    size_t copy = (len < avail) ? len : avail;
    memcpy(buf, tmp + off, copy);
    return (ssize_t)copy;
}

static ssize_t proc_cpuinfo_read(vfs_node_t *n, uint64_t off, void *buf, size_t len) {
    (void)n; (void)off;
    const char *info =
        "processor       : 0\n"
        "model name      : Chicago-95 x86_64 CPU\n"
        "cpu MHz         : 1000.000\n"
        "bogomips        : 2000.00\n"
        "flags           : sse sse2 x86-64 syscall\n";
    size_t tlen = strlen(info);
    if (off >= tlen) return 0;
    size_t avail = tlen - (size_t)off;
    size_t copy = (len < avail) ? len : avail;
    memcpy(buf, info + off, copy);
    return (ssize_t)copy;
}

static ssize_t proc_self_read(vfs_node_t *n, uint64_t off, void *buf, size_t len) {
    (void)n; (void)off;
    process_t *proc = process_get_current();
    char tmp[128];
    int pos = 0;

    const char *pid_label = "PID: ";
    memcpy(tmp + pos, pid_label, 5); pos += 5;

    char num[16];
    int val = proc ? proc->pid : -1;
    int nlen = 0;
    char rev[16];
    if (val == 0) { rev[0] = '0'; nlen = 1; }
    else {
        int neg = 0;
        if (val < 0) { neg = 1; val = -val; }
        while (val > 0) { rev[nlen++] = '0' + (val % 10); val /= 10; }
        if (neg) rev[nlen++] = '-';
    }
    for (int i = 0; i < nlen; i++) num[i] = rev[nlen - 1 - i];
    num[nlen] = '\0';
    memcpy(tmp + pos, num, nlen); pos += nlen;
    tmp[pos++] = '\n';
    tmp[pos] = '\0';

    size_t tlen = pos;
    if (off >= tlen) return 0;
    size_t avail = tlen - (size_t)off;
    size_t copy = (len < avail) ? len : avail;
    memcpy(buf, tmp + off, copy);
    return (ssize_t)copy;
}

void procfs_init(void) {
    vfs_node_t *proc_root = vfs_new_node("proc", VFS_DIR, &dev_ops);
    if (!proc_root) return;

    /* /proc/meminfo */
    vfs_node_t *meminfo = vfs_new_node("meminfo", VFS_FILE, &dev_ops);
    vfs_ops_t *mem_ops = (vfs_ops_t *)kmalloc(sizeof(vfs_ops_t));
    memcpy(mem_ops, &dev_ops, sizeof(vfs_ops_t));
    mem_ops->read = proc_meminfo_read;
    meminfo->ops = mem_ops;
    vfs_attach_child(proc_root, meminfo);

    /* /proc/cpuinfo */
    vfs_node_t *cpuinfo = vfs_new_node("cpuinfo", VFS_FILE, &dev_ops);
    vfs_ops_t *cpu_ops = (vfs_ops_t *)kmalloc(sizeof(vfs_ops_t));
    memcpy(cpu_ops, &dev_ops, sizeof(vfs_ops_t));
    cpu_ops->read = proc_cpuinfo_read;
    cpuinfo->ops = cpu_ops;
    vfs_attach_child(proc_root, cpuinfo);

    /* /proc/self */
    vfs_node_t *self = vfs_new_node("self", VFS_FILE, &dev_ops);
    vfs_ops_t *self_ops = (vfs_ops_t *)kmalloc(sizeof(vfs_ops_t));
    memcpy(self_ops, &dev_ops, sizeof(vfs_ops_t));
    self_ops->read = proc_self_read;
    self->ops = self_ops;
    vfs_attach_child(proc_root, self);

    /* Attach to root */
    proc_root->parent = vfs_root;
    vfs_root->children[vfs_root->child_count++] = proc_root;

    console_puts("[VFS] procfs mounted at /proc\n", 0x0A);
}

/* =============================================
   TmpFS — /tmp (in-memory)
   ============================================= */

vfs_node_t *tmpfs_create_node(const char *name, uint32_t type) {
    vfs_node_t *n = vfs_new_node(name, type, &dev_ops);
    if (!n) return NULL;
    if (type == VFS_FILE) {
        /* Allocate initial 4KB data buffer */
        uint8_t *data = (uint8_t *)kmalloc(PAGE_SIZE);
        if (data) {
            memset(data, 0, PAGE_SIZE);
            n->impl_data = (uint64_t)data;
            n->length = 0;
        }
    }
    return n;
}

static ssize_t tmpfs_read(vfs_node_t *n, uint64_t off, void *buf, size_t len) {
    if (!n->impl_data) return 0;
    if (off >= n->length) return 0;
    size_t avail = (size_t)n->length - (size_t)off;
    size_t copy = (len < avail) ? len : avail;
    memcpy(buf, (void *)(n->impl_data + off), copy);
    return (ssize_t)copy;
}

static ssize_t tmpfs_write(vfs_node_t *n, uint64_t off, const void *buf, size_t len) {
    uint64_t needed = off + len;
    /* Grow buffer if needed (in PAGE_SIZE increments) */
    if (needed > (uint64_t)n->length) {
        uint64_t new_size = (needed + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint8_t *new_buf = (uint8_t *)kmalloc_aligned((size_t)new_size);
        if (!new_buf) return -1;
        memset(new_buf, 0, (size_t)new_size);
        if (n->impl_data) {
            size_t old_cap = ((n->length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
            if (old_cap == 0) old_cap = PAGE_SIZE;
            size_t copy = (n->length < old_cap) ? n->length : old_cap;
            memcpy(new_buf, (void *)n->impl_data, copy);
            kfree((void *)n->impl_data);
        }
        n->impl_data = (uint64_t)new_buf;
    }
    memcpy((void *)(n->impl_data + off), buf, len);
    if (needed > (uint64_t)n->length)
        n->length = (uint32_t)needed;
    return (ssize_t)len;
}

static int tmpfs_create(vfs_node_t *n, const char *name, uint32_t type) {
    vfs_node_t *child = tmpfs_create_node(name, type);
    if (!child) return -1;
    return vfs_attach_child(n, child);
}

static int tmpfs_mkdir(vfs_node_t *n, const char *name) {
    vfs_node_t *child = tmpfs_create_node(name, VFS_DIR);
    if (!child) return -1;
    return vfs_attach_child(n, child);
}

static int tmpfs_unlink(vfs_node_t *n, const char *name) {
    for (uint32_t i = 0; i < n->child_count; i++) {
        if (strcmp(n->children[i]->name, name) == 0) {
            vfs_node_t *child = n->children[i];
            /* Free data if file */
            if (child->type == VFS_FILE && child->impl_data) {
                kfree((void *)child->impl_data);
            }
            kfree(child);
            /* Shift remaining children */
            for (uint32_t j = i; j < n->child_count - 1; j++)
                n->children[j] = n->children[j + 1];
            n->child_count--;
            return 0;
        }
    }
    return -1;
}

void tmpfs_init(void) {
    vfs_node_t *tmp_root = vfs_new_node("tmp", VFS_DIR, &dev_ops);
    if (!tmp_root) return;

    /* Give tmp root a custom ops with file ops for create/mkdir/unlink */
    vfs_ops_t *tmp_ops = (vfs_ops_t *)kmalloc(sizeof(vfs_ops_t));
    memcpy(tmp_ops, &dev_ops, sizeof(vfs_ops_t));
    tmp_ops->read    = tmpfs_read;
    tmp_ops->write   = tmpfs_write;
    tmp_ops->create  = tmpfs_create;
    tmp_ops->mkdir   = tmpfs_mkdir;
    tmp_ops->unlink  = tmpfs_unlink;
    tmp_root->ops = tmp_ops;

    /* Attach to root */
    tmp_root->parent = vfs_root;
    vfs_root->children[vfs_root->child_count++] = tmp_root;

    console_puts("[VFS] tmpfs mounted at /tmp\n", 0x0A);
}

/* =============================================
   Core VFS API
   ============================================= */

void vfs_init(void) {
    /* Create root node */
    vfs_root = vfs_new_node("/", VFS_DIR, &dev_ops);
    global_fd_count = 0;
    memset(global_fd_table, 0, sizeof(global_fd_table));

    console_puts("[VFS] root filesystem initialized\n", 0x0A);
}

int vfs_mount(const char *path, vfs_node_t *fs_root) {
    if (!vfs_root || !fs_root) return -1;

    /* For now, only support mounting at root subdirectories */
    const char *p = path_skip_slashes(path);
    if (*p == '\0') {
        /* Mount at root — replace children */
        console_puts("[VFS] WARNING: replacing root not supported\n", 0x0C);
        return -1;
    }

    /* Find or create mount point */
    char name[VFS_NAME_LEN];
    int nlen = 0;
    while (*p && *p != '/' && nlen < VFS_NAME_LEN - 1) {
        name[nlen++] = *p++;
    }
    name[nlen] = '\0';

    vfs_node_t *existing = vfs_find_child(vfs_root, name);
    if (existing) {
        /* Replace with mounted fs */
        fs_root->parent = vfs_root;
        /* Find index of existing and replace */
        for (uint32_t i = 0; i < vfs_root->child_count; i++) {
            if (vfs_root->children[i] == existing) {
                vfs_root->children[i] = fs_root;
                return 0;
            }
        }
    }

    /* Attach as new child */
    return vfs_attach_child(vfs_root, fs_root);
}

vfs_node_t *vfs_resolve(const char *path) {
    if (!vfs_root) return NULL;
    const char *p = path_skip_slashes(path);

    vfs_node_t *current = vfs_root;

    /* Handle empty path */
    if (*p == '\0') return vfs_root;

    char name[VFS_NAME_LEN];
    while (*p != '\0') {
        /* Extract component */
        int nlen = 0;
        while (*p && *p != '/' && nlen < VFS_NAME_LEN - 1) {
            name[nlen++] = *p++;
        }
        name[nlen] = '\0';
        p = path_skip_slashes(p);

        if (nlen == 0) break;

        /* Handle "." and ".." */
        if (name[0] == '.' && name[1] == '\0') continue;
        if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
            if (current->parent) current = current->parent;
            continue;
        }

        if (current->type != VFS_DIR) return NULL;

        /* Try finddir first, then linear search */
        vfs_node_t *found = NULL;
        if (current->ops && current->ops->finddir) {
            vfs_node_t tmp;
            memset(&tmp, 0, sizeof(tmp));
            if (current->ops->finddir(current, name, &tmp) == 0) {
                /* finddir filled tmp — but we want the real node from children */
            }
        }

        /* Linear search children */
        found = vfs_find_child(current, name);
        if (!found) return NULL;
        current = found;
    }

    return current;
}

vfs_node_t *vfs_open(const char *path, uint32_t flags) {
    vfs_node_t *node = vfs_resolve(path);
    if (!node) {
        /* If O_CREAT, try to create */
        if (flags & O_CREAT) {
            /* Resolve parent */
            char parent_path[VFS_PATH_LEN];
            char child_name[VFS_NAME_LEN];
            int len = strlen(path);
            int last_slash = -1;
            for (int i = len - 1; i >= 0; i--) {
                if (path[i] == '/') { last_slash = i; break; }
            }

            if (last_slash >= 0) {
                memcpy(parent_path, path, last_slash);
                parent_path[last_slash] = '\0';
                int nlen = len - last_slash - 1;
                if (nlen >= VFS_NAME_LEN) nlen = VFS_NAME_LEN - 1;
                memcpy(child_name, path + last_slash + 1, nlen);
                child_name[nlen] = '\0';
            } else {
                parent_path[0] = '/';
                parent_path[1] = '\0';
                int nlen = len;
                if (nlen >= VFS_NAME_LEN) nlen = VFS_NAME_LEN - 1;
                memcpy(child_name, path, nlen);
                child_name[nlen] = '\0';
            }

            vfs_node_t *parent = vfs_resolve(parent_path);
            if (parent && parent->ops && parent->ops->create) {
                if (parent->ops->create(parent, child_name, VFS_FILE) == 0) {
                    node = vfs_find_child(parent, child_name);
                }
            }
        }
        if (!node) return NULL;
    }

    if (node->ops && node->ops->open) {
        node->ops->open(node, flags);
    }

    node->flags = flags;
    return node;
}

int vfs_close(vfs_node_t *node) {
    if (!node) return -1;
    if (node->ops && node->ops->close) {
        return node->ops->close(node);
    }
    return 0;
}

ssize_t vfs_read(vfs_node_t *node, uint64_t offset, void *buf, size_t len) {
    if (!node || !buf) return -1;

    /* Use tmpfs read for VFS_FILE nodes with impl_data and no custom read */
    if (node->type == VFS_FILE && node->impl_data) {
        if (node->ops->read == default_read || node->ops->read == tmpfs_read) {
            return tmpfs_read(node, offset, buf, len);
        }
    }

    if (node->ops && node->ops->read) {
        return node->ops->read(node, offset, buf, len);
    }
    return -1;
}

ssize_t vfs_write(vfs_node_t *node, uint64_t offset, const void *buf, size_t len) {
    if (!node || !buf) return -1;

    if (node->type == VFS_FILE && node->impl_data) {
        if (node->ops->write == default_write || node->ops->write == tmpfs_write) {
            return tmpfs_write(node, offset, buf, len);
        }
    }

    if (node->ops && node->ops->write) {
        return node->ops->write(node, offset, buf, len);
    }
    return -1;
}

int vfs_mkdir(const char *path) {
    char parent_path[VFS_PATH_LEN];
    char dir_name[VFS_NAME_LEN];
    int len = strlen(path);
    int last_slash = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (path[i] == '/') { last_slash = i; break; }
    }

    if (last_slash >= 0) {
        memcpy(parent_path, path, last_slash);
        parent_path[last_slash] = '\0';
        int nlen = len - last_slash - 1;
        if (nlen >= VFS_NAME_LEN) nlen = VFS_NAME_LEN - 1;
        memcpy(dir_name, path + last_slash + 1, nlen);
        dir_name[nlen] = '\0';
    } else {
        parent_path[0] = '/';
        parent_path[1] = '\0';
        int nlen = len;
        if (nlen >= VFS_NAME_LEN) nlen = VFS_NAME_LEN - 1;
        memcpy(dir_name, path, nlen);
        dir_name[nlen] = '\0';
    }

    vfs_node_t *parent = vfs_resolve(parent_path);
    if (!parent) return -1;
    if (parent->type != VFS_DIR) return -1;

    if (parent->ops && parent->ops->mkdir) {
        return parent->ops->mkdir(parent, dir_name);
    }

    /* Fallback: create directory node manually */
    vfs_node_t *dir = vfs_new_node(dir_name, VFS_DIR, &dev_ops);
    if (!dir) return -1;
    return vfs_attach_child(parent, dir);
}

int vfs_readdir(vfs_node_t *node, uint32_t index, vfs_node_t *out) {
    if (!node || node->type != VFS_DIR) return -1;

    if (node->ops && node->ops->readdir) {
        return node->ops->readdir(node, index, out);
    }

    /* Default: return children[index] */
    if (index >= node->child_count) return -1;
    memcpy(out, node->children[index], sizeof(vfs_node_t));
    return 0;
}

vfs_node_t *vfs_finddir(vfs_node_t *node, const char *name) {
    if (!node || node->type != VFS_DIR) return NULL;

    if (node->ops && node->ops->finddir) {
        vfs_node_t *tmp = vfs_alloc_node();
        if (!tmp) return NULL;
        if (node->ops->finddir(node, name, tmp) == 0) {
            return tmp;
        }
        kfree(tmp);
    }

    return vfs_find_child(node, name);
}

int vfs_stat(vfs_node_t *node, void *statbuf) {
    if (!node) return -1;

    if (node->ops && node->ops->stat) {
        return node->ops->stat(node, statbuf);
    }

    /* Default stat */
    if (!statbuf) return -1;
    uint32_t *stat = (uint32_t *)statbuf;
    stat[0] = node->type;
    stat[1] = node->length;
    stat[2] = (uint32_t)node->inode;
    return 0;
}

int vfs_create(const char *path, uint32_t type) {
    /* Delegate to vfs_open with O_CREAT */
    vfs_node_t *node = vfs_open(path, O_CREAT);
    if (!node) return -1;
    vfs_close(node);
    return 0;
}

int vfs_unlink(const char *path) {
    vfs_node_t *node = vfs_resolve(path);
    if (!node) return -1;
    if (!node->parent) return -1;

    vfs_node_t *parent = node->parent;
    if (parent->ops && parent->ops->unlink) {
        return parent->ops->unlink(parent, node->name);
    }

    /* Fallback: remove from children */
    for (uint32_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == node) {
            if (node->type == VFS_FILE && node->impl_data)
                kfree((void *)node->impl_data);
            kfree(node);
            for (uint32_t j = i; j < parent->child_count - 1; j++)
                parent->children[j] = parent->children[j + 1];
            parent->child_count--;
            return 0;
        }
    }
    return -1;
}

/* =============================================
   File Descriptor Table (global, indexed per-process)
   ============================================= */

int vfs_fd_alloc(vfs_node_t *node, uint32_t flags) {
    for (int i = 0; i < VFS_MAX_FDS * MAX_PROCESSES; i++) {
        if (!global_fd_table[i].in_use) {
            global_fd_table[i].in_use = 1;
            global_fd_table[i].node   = node;
            global_fd_table[i].flags  = flags;
            global_fd_table[i].offset = 0;
            return i;
        }
    }
    return -1;
}

int vfs_fd_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS * MAX_PROCESSES) return -1;
    if (!global_fd_table[fd].in_use) return -1;

    vfs_node_t *node = global_fd_table[fd].node;
    if (node) vfs_close(node);

    global_fd_table[fd].in_use  = 0;
    global_fd_table[fd].node    = NULL;
    global_fd_table[fd].offset  = 0;
    return 0;
}

vfs_node_t *vfs_fd_get(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS * MAX_PROCESSES) return NULL;
    if (!global_fd_table[fd].in_use) return NULL;
    return global_fd_table[fd].node;
}

uint64_t *vfs_fd_offset(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS * MAX_PROCESSES) return NULL;
    if (!global_fd_table[fd].in_use) return NULL;
    return &global_fd_table[fd].offset;
}

int vfs_fd_dup(int oldfd) {
    if (oldfd < 0 || oldfd >= VFS_MAX_FDS * MAX_PROCESSES) return -1;
    if (!global_fd_table[oldfd].in_use) return -1;

    int newfd = vfs_fd_alloc(global_fd_table[oldfd].node, global_fd_table[oldfd].flags);
    if (newfd < 0) return -1;
    global_fd_table[newfd].offset = global_fd_table[oldfd].offset;
    return newfd;
}

void vfs_register_dev(const char *path, vfs_node_t *node) {
    char parent_path[VFS_PATH_LEN];
    char child_name[VFS_NAME_LEN];
    int len = strlen(path);
    int last_slash = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (path[i] == '/') { last_slash = i; break; }
    }

    if (last_slash >= 0) {
        memcpy(parent_path, path, last_slash);
        parent_path[last_slash] = '\0';
        int nlen = len - last_slash - 1;
        if (nlen >= VFS_NAME_LEN) nlen = VFS_NAME_LEN - 1;
        memcpy(child_name, path + last_slash + 1, nlen);
        child_name[nlen] = '\0';
    } else {
        parent_path[0] = '/';
        parent_path[1] = '\0';
        int nlen = len;
        if (nlen >= VFS_NAME_LEN) nlen = VFS_NAME_LEN - 1;
        memcpy(child_name, path, nlen);
        child_name[nlen] = '\0';
    }

    vfs_node_t *parent = vfs_resolve(parent_path);
    if (parent) {
        vfs_attach_child(parent, node);
    }
}
