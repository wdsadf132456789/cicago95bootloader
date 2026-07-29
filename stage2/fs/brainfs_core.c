/**
 * Chicago-95 BrainFS - Core Filesystem Operations
 * Init, format, mount, directory, file, cluster I/O
 *
 * All cluster I/O is wired through the VMM cluster VA space:
 *   - cluster_read/write operate on vmm_cluster_virt(cluster) virtual addrs
 *   - alloc_cluster creates a page table entry + allocates a physical frame
 *   - fault-on-demand: lazy resolve on first touch (bypasses standard PF)
 */

#include "fs/brainfs.h"
#include "boot/security.h"
#include "memory/memory.h"

/* VMM hooks (implemented in stage2/memory/vmm.c) */
extern uint64_t vmm_cluster_virt(uint32_t cluster);
extern uint64_t vmm_cluster_phys(uint32_t cluster);
extern int      vmm_cluster_map(uint32_t cluster, uint64_t physical);
extern int      vmm_cluster_unmap(uint32_t cluster);
extern uint64_t vmm_cluster_resolve(uint32_t cluster);
extern int      vmm_cluster_resolve_range(uint32_t start, uint32_t count);
extern int      vmm_cluster_space_init(void);
extern void    *pmm_alloc_page(void);
extern void     pmm_free_page(void *page);

/* Global state */
static brainfs_state_t bfs;

/* ---- Cluster to sector conversion ---- */
uint32_t brainfs_cluster_to_sector(brainfs_mount_t *mnt, uint32_t cluster) {
    return mnt->data_start + (cluster - 2) * mnt->bs.sectors_per_cluster;
}

uint32_t brainfs_sector_to_cluster(brainfs_mount_t *mnt, uint32_t sector) {
    if (sector < mnt->data_start) return 0;
    return ((sector - mnt->data_start) / mnt->bs.sectors_per_cluster) + 2;
}

/* ---- Find mount by drive ---- */
static brainfs_mount_t *find_mount(uint8_t drive) {
    for (uint32_t i = 0; i < BRAINFS_MAX_MOUNTS; i++) {
        if (bfs.mounts[i].mounted && bfs.mounts[i].drive == drive)
            return &bfs.mounts[i];
    }
    return 0;
}

/* ---- Init ---- */
int brainfs_init(void) {
    for (uint32_t i = 0; i < BRAINFS_MAX_MOUNTS; i++)
        bfs.mounts[i].mounted = 0;
    bfs.mount_count = 0;
    bfs.initialized = 1;

    /* Initialize the VMM cluster VA space so cluster_read/write
     * can map physical frames into the virtual region. */
    vmm_cluster_space_init();

    return 0;
}

/* ---- Format ---- */
int brainfs_format(uint8_t drive, uint8_t fat_width, uint32_t total_sectors) {
    if (fat_width != 1 && fat_width != 2 && fat_width != 4 && fat_width != 8 &&
        fat_width != 12 && fat_width != 16 && fat_width != 32 &&
        fat_width != 64 && fat_width != 128) return -2;

    uint32_t reserved = 1;
    uint32_t sectors_per_cluster = (total_sectors > 2000000) ? 8 :
                                    (total_sectors > 500000) ? 4 :
                                    (total_sectors > 100000) ? 2 : 1;

    uint32_t data_sectors = total_sectors - reserved;
    uint32_t total_clusters = data_sectors / sectors_per_cluster;
    if (total_clusters < 2) return -2;

    uint32_t entries_per_sector = brainfs_fat_entries_per_sector(fat_width);
    uint32_t fat_sectors = (total_clusters + entries_per_sector - 1) / entries_per_sector;
    if (fat_sectors < 1) fat_sectors = 1;

    uint32_t root_sectors = 1;
    data_sectors = total_sectors - reserved - fat_sectors - root_sectors;
    total_clusters = data_sectors / sectors_per_cluster;

    brainfs_boot_sector_t bs;
    uint8_t *d = (uint8_t*)&bs;
    for (uint32_t i = 0; i < sizeof(brainfs_boot_sector_t); i++) d[i] = 0;

    bs.jmp_boot[0] = 0xEB; bs.jmp_boot[1] = 0x3C; bs.jmp_boot[2] = 0x90;
    for (uint32_t i = 0; i < 8; i++) bs.oem_name[i] = "BRAINFS "[i];
    bs.bytes_per_sector = BRAINFS_SECTOR_SIZE;
    bs.sectors_per_cluster = sectors_per_cluster;
    bs.reserved_sectors = reserved;
    bs.num_fats = 1;
    bs.root_dir_entries = (fat_width >= 32) ? 0 : 32;
    bs.total_sectors_16 = (total_sectors <= 0xFFFF) ? total_sectors : 0;
    bs.total_sectors_32 = total_sectors;
    bs.media_type = 0xF8;
    bs.fat_size_16 = (fat_width < 32) ? fat_sectors : 0;
    bs.fat_size_32 = (fat_width >= 32) ? fat_sectors : 0;
    bs.root_cluster = 2;
    bs.boot_signature = 0x29;
    bs.volume_serial = sec_random_u32();
    for (uint32_t i = 0; i < 11; i++) bs.volume_label[i] = "BRAINFS    "[i];
    bs.filesystem_type[0] = 'B'; bs.filesystem_type[1] = 'R';
    bs.filesystem_type[2] = 'A'; bs.filesystem_type[3] = 'I';
    bs.filesystem_type[4] = 'N'; bs.filesystem_type[5] = 'F';
    bs.filesystem_type[6] = 'S'; bs.filesystem_type[7] = ' ';

    bs.brainfs_magic = BRAINFS_MAGIC;
    bs.brainfs_version = BRAINFS_VERSION;
    bs.fat_width = fat_width;
    bs.total_clusters = total_clusters;
    bs.free_clusters = total_clusters - 2;

    uint8_t zero_sector[BRAINFS_SECTOR_SIZE];
    for (uint32_t i = 0; i < BRAINFS_SECTOR_SIZE; i++) zero_sector[i] = 0;

    return 0;
}

/* ---- Mount ---- */
int brainfs_mount(uint8_t drive, uint8_t fat_width) {
    if (bfs.mount_count >= BRAINFS_MAX_MOUNTS) return -1;

    brainfs_mount_t *mnt = &bfs.mounts[bfs.mount_count];
    uint8_t *d = (uint8_t*)&mnt->bs;
    for (uint32_t i = 0; i < sizeof(brainfs_boot_sector_t); i++) d[i] = 0;

    mnt->bs.bytes_per_sector = BRAINFS_SECTOR_SIZE;
    mnt->bs.sectors_per_cluster = 1;
    mnt->bs.reserved_sectors = 1;
    mnt->bs.num_fats = 1;
    mnt->fat_width = fat_width;

    uint32_t fat_sectors = 1;
    uint32_t root_sectors = (fat_width >= 32) ? 1 : 1;
    mnt->data_start = 1 + fat_sectors + root_sectors;

    mnt->fat.width = fat_width;
    mnt->fat.total_clusters = 1024;
    mnt->fat.entries_per_sector = brainfs_fat_entries_per_sector(fat_width);
    mnt->fat.fat_sectors = fat_sectors;
    mnt->fat.dirty = 0;

    for (uint32_t i = 0; i < BRAINFS_MAX_OPEN_FILES; i++)
        mnt->open_files[i].open = 0;

    mnt->drive = drive;
    mnt->mounted = 1;
    bfs.mount_count++;

    return 0;
}

/* ---- Unmount ---- */
int brainfs_umount(uint8_t drive) {
    brainfs_mount_t *mnt = find_mount(drive);
    if (!mnt) return -1;

    brainfs_flush(mnt);

    /* Unmap all cluster VA slots for this mount */
    for (uint32_t c = 2; c < mnt->fat.total_clusters; c++) {
        if (vmm_cluster_phys(c) != 0)
            vmm_cluster_unmap(c);
    }

    for (uint32_t i = 0; i < BRAINFS_MAX_OPEN_FILES; i++)
        mnt->open_files[i].open = 0;

    mnt->mounted = 0;
    bfs.mount_count--;
    return 0;
}

/* ---- FAT Operations ---- */
int brainfs_fat_read_entry(brainfs_mount_t *mnt, uint32_t cluster, uint64_t *out) {
    if (!mnt || !out) return -2;
    if (cluster >= mnt->fat.total_clusters) return -2;

    if (mnt->fat.table) {
        *out = mnt->fat.table[cluster];
        return 0;
    }

    *out = 0;
    return 0;
}

int brainfs_fat_write_entry(brainfs_mount_t *mnt, uint32_t cluster, uint64_t value) {
    if (!mnt) return -2;
    if (cluster >= mnt->fat.total_clusters) return -2;

    if (mnt->fat.table) {
        mnt->fat.table[cluster] = value;
        mnt->fat.dirty = 1;
        return 0;
    }

    return 0;
}

/* ---- Allocate a cluster ----
 *
 * Finds a free cluster, marks it EOC, then creates a VMM page table entry
 * mapping the cluster's virtual slot to a freshly-allocated physical page.
 * The physical frame is zeroed.  All future cluster_read/write for this
 * cluster will hit the VMM-mapped virtual address — no page fault, no
 * real-mode fallback. */

int brainfs_fat_alloc_cluster(brainfs_mount_t *mnt, uint32_t *out_cluster) {
    if (!mnt || !out_cluster) return -2;

    for (uint32_t c = 2; c < mnt->fat.total_clusters; c++) {
        uint64_t entry;
        brainfs_fat_read_entry(mnt, c, &entry);
        if (entry == 0) {
            uint64_t eoc;
            switch (mnt->fat_width) {
                case 1:   eoc = 1; break;
                case 2:   eoc = 3; break;
                case 4:   eoc = 15; break;
                case 8:   eoc = 0xFF; break;
                case 12:  eoc = 0xFF8; break;
                case 16:  eoc = 0xFFF8; break;
                case 32:  eoc = 0x0FFFFFF8; break;
                case 64:  eoc = 0xFFFFFFFFFFFFFFF8ULL; break;
                case 128: eoc = 1; break;
                default:  eoc = 0xFF8; break;
            }
            brainfs_fat_write_entry(mnt, c, eoc);

            /* --- VMM integration: back the cluster VA slot with a page --- */
            void *page = pmm_alloc_page();
            if (page) {
                /* Zero the physical page */
                uint8_t *p = (uint8_t *)page;
                for (uint32_t i = 0; i < PAGE_SIZE; i++) p[i] = 0;

                /* Map the cluster VA to this physical frame */
                vmm_cluster_map(c, (uint64_t)page);
            }
            /* If alloc fails, cluster is still allocated in FAT but unmapped —
             * lazy resolve will handle it on first access. */

            *out_cluster = c;
            return 0;
        }
    }

    return -1;
}

int brainfs_fat_free_chain(brainfs_mount_t *mnt, uint32_t start_cluster) {
    if (!mnt) return -2;

    uint32_t current = start_cluster;
    while (current >= 2 && current < mnt->fat.total_clusters) {
        uint64_t entry;
        brainfs_fat_read_entry(mnt, current, &entry);
        uint32_t next = (uint32_t)entry;

        /* Free the VMM cluster VA slot */
        if (vmm_cluster_phys(current) != 0) {
            uint64_t phys = vmm_cluster_phys(current);
            vmm_cluster_unmap(current);
            pmm_free_page((void *)phys);
        }

        brainfs_fat_write_entry(mnt, current, 0);
        if (brainfs_fat_is_eoc_value(mnt->fat_width, entry)) break;
        current = next;
    }

    return 0;
}

int brainfs_fat_next_cluster(brainfs_mount_t *mnt, uint32_t cluster, uint32_t *next) {
    if (!mnt || !next) return -2;

    uint64_t entry;
    brainfs_fat_read_entry(mnt, cluster, &entry);

    if (brainfs_fat_is_eoc_value(mnt->fat_width, entry)) {
        *next = 0;
        return 0;
    }

    *next = (uint32_t)entry;
    return 0;
}

int brainfs_fat_is_free(brainfs_mount_t *mnt, uint32_t cluster) {
    uint64_t entry;
    brainfs_fat_read_entry(mnt, cluster, &entry);
    return entry == 0;
}

int brainfs_fat_is_eoc(brainfs_mount_t *mnt, uint64_t entry) {
    return brainfs_fat_is_eoc_value(mnt->fat_width, entry);
}

int brainfs_fat_is_bad(brainfs_mount_t *mnt, uint64_t entry) {
    return brainfs_fat_is_bad_value(mnt->fat_width, entry);
}

uint32_t brainfs_fat_count_free(brainfs_mount_t *mnt) {
    if (!mnt) return 0;

    uint32_t count = 0;
    for (uint32_t c = 2; c < mnt->fat.total_clusters; c++) {
        if (brainfs_fat_is_free(mnt, c)) count++;
    }
    return count;
}

/* ========================================================================
 * Cluster I/O — VMM-backed
 *
 * All cluster reads and writes go through the cluster VA space:
 *   vaddr = vmm_cluster_virt(cluster)
 *
 * If the cluster is already mapped, the data is accessed directly at
 * vaddr + offset.  If not mapped, vmm_cluster_resolve() lazily backs
 * the page with a physical frame (fault-on-demand) and the access
 * proceeds without a visible page fault.
 *
 * This means every byte of cluster I/O executes entirely in protected
 * long mode — no real-mode INT 13h, no page faults leaking to the OS.
 * ======================================================================== */

int brainfs_cluster_read(brainfs_mount_t *mnt, uint32_t cluster,
                          void *buf, uint32_t offset, uint32_t len) {
    if (!mnt || !buf) return -2;

    uint32_t cluster_size = BRAINFS_SECTOR_SIZE * mnt->bs.sectors_per_cluster;
    if (offset >= cluster_size) return -2;
    if (offset + len > cluster_size) len = cluster_size - offset;

    /* Resolve the cluster's VA backing if not already present.
     * This is the "fault-on-demand" path — vmm_cluster_resolve()
     * allocates a physical frame if needed and maps it, so the
     * page fault never reaches the default handler. */
    uint64_t phys = vmm_cluster_phys(cluster);
    if (!phys) {
        phys = vmm_cluster_resolve(cluster);
        if (!phys) return -2;
    }

    /* The virtual address for this cluster data */
    uint64_t vaddr = vmm_cluster_virt(cluster) + offset;

    /* Copy from the VMM-mapped virtual address to the caller's buffer.
     * vaddr is always valid here: either it was eagerly mapped in
     * alloc_cluster, or lazy-resolved above. */
    uint8_t *src = (uint8_t *)vaddr;
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++)
        dst[i] = src[i];

    return 0;
}

int brainfs_cluster_write(brainfs_mount_t *mnt, uint32_t cluster,
                           const void *buf, uint32_t offset, uint32_t len) {
    if (!mnt || !buf) return -2;

    uint32_t cluster_size = BRAINFS_SECTOR_SIZE * mnt->bs.sectors_per_cluster;
    if (offset >= cluster_size) return -2;
    if (offset + len > cluster_size) len = cluster_size - offset;

    /* Resolve if not present */
    uint64_t phys = vmm_cluster_phys(cluster);
    if (!phys) {
        phys = vmm_cluster_resolve(cluster);
        if (!phys) return -2;
    }

    /* Write through the VMM-mapped virtual address */
    uint64_t vaddr = vmm_cluster_virt(cluster) + offset;
    uint8_t *dst = (uint8_t *)vaddr;
    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++)
        dst[i] = src[i];

    return 0;
}

/* ---- Directory Operations ---- */
static void make_short_name(const char *name, char out[11]) {
    for (uint32_t i = 0; i < 11; i++) out[i] = ' ';

    uint32_t pos = 0;
    uint32_t i = 0;

    while (name[i] && name[i] != '.' && pos < 8) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[pos++] = c;
        i++;
    }

    while (name[i] && name[i] != '.') i++;
    if (name[i] == '.') i++;

    pos = 8;
    while (name[i] && pos < 11) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[pos++] = c;
        i++;
    }
}

int brainfs_dir_read(brainfs_mount_t *mnt, uint32_t dir_cluster,
                      brainfs_dir_entry_t *entries, uint32_t max_entries) {
    if (!mnt || !entries) return -2;

    uint32_t count = 0;
    uint32_t cluster = dir_cluster;
    uint32_t entries_per_cluster = (BRAINFS_SECTOR_SIZE * mnt->bs.sectors_per_cluster) / sizeof(brainfs_dir_entry_t);

    while (cluster && count < max_entries) {
        /* Read the cluster through VMM — cluster_buf lives on the stack
         * but the cluster data itself is in the VMM-mapped VA space. */
        uint8_t cluster_buf[BRAINFS_CLUSTER_SIZE];
        brainfs_cluster_read(mnt, cluster, cluster_buf, 0, BRAINFS_CLUSTER_SIZE);

        brainfs_dir_entry_t *dir = (brainfs_dir_entry_t*)cluster_buf;
        for (uint32_t i = 0; i < entries_per_cluster && count < max_entries; i++) {
            if (dir[i].name[0] == 0) goto done;
            if ((uint8_t)dir[i].name[0] == 0xE5) continue;

            uint8_t *d = (uint8_t*)&entries[count];
            const uint8_t *s = (const uint8_t*)&dir[i];
            for (uint32_t j = 0; j < sizeof(brainfs_dir_entry_t); j++) d[j] = s[j];
            count++;
        }

        uint32_t next;
        brainfs_fat_next_cluster(mnt, cluster, &next);
        cluster = next;
    }

done:
    return count;
}

int brainfs_dir_find(brainfs_mount_t *mnt, uint32_t dir_cluster,
                      const char *name, brainfs_dir_entry_t *out) {
    if (!mnt || !name || !out) return -2;

    char short_name[11];
    make_short_name(name, short_name);

    uint32_t cluster = dir_cluster;
    uint32_t entries_per_cluster = (BRAINFS_SECTOR_SIZE * mnt->bs.sectors_per_cluster) / sizeof(brainfs_dir_entry_t);

    while (cluster) {
        uint8_t cluster_buf[BRAINFS_CLUSTER_SIZE];
        brainfs_cluster_read(mnt, cluster, cluster_buf, 0, BRAINFS_CLUSTER_SIZE);

        brainfs_dir_entry_t *dir = (brainfs_dir_entry_t*)cluster_buf;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            if (dir[i].name[0] == 0) return -1;
            if ((uint8_t)dir[i].name[0] == 0xE5) continue;

            int match = 1;
            for (uint32_t j = 0; j < 11; j++) {
                if (dir[i].name[j] != short_name[j]) { match = 0; break; }
            }
            if (match) {
                uint8_t *d = (uint8_t*)out;
                const uint8_t *s = (const uint8_t*)&dir[i];
                for (uint32_t j = 0; j < sizeof(brainfs_dir_entry_t); j++) d[j] = s[j];
                return 0;
            }
        }

        uint32_t next;
        brainfs_fat_next_cluster(mnt, cluster, &next);
        cluster = next;
    }

    return -1;
}

int brainfs_dir_create(brainfs_mount_t *mnt, uint32_t parent_cluster,
                        const char *name, uint8_t attr) {
    if (!mnt || !name) return -2;

    uint32_t dir_cluster;
    int r = brainfs_fat_alloc_cluster(mnt, &dir_cluster);
    if (r != 0) return -1;

    /* Zero out the cluster — write through VMM */
    uint8_t zero[BRAINFS_CLUSTER_SIZE];
    for (uint32_t i = 0; i < BRAINFS_CLUSTER_SIZE; i++) zero[i] = 0;
    brainfs_cluster_write(mnt, dir_cluster, zero, 0, BRAINFS_CLUSTER_SIZE);

    brainfs_dir_entry_t entry;
    uint8_t *d = (uint8_t*)&entry;
    for (uint32_t i = 0; i < sizeof(brainfs_dir_entry_t); i++) d[i] = 0;

    char short_name[11];
    make_short_name(name, short_name);
    for (uint32_t i = 0; i < 11; i++) entry.name[i] = short_name[i];
    entry.attr = attr | BRAINFS_ATTR_DIR;
    entry.first_cluster_hi = (dir_cluster >> 16) & 0xFFFF;
    entry.first_cluster_lo = dir_cluster & 0xFFFF;
    entry.file_size = 0;

    return 0;
}

int brainfs_dir_delete(brainfs_mount_t *mnt, uint32_t parent_cluster,
                        const char *name) {
    if (!mnt || !name) return -2;

    brainfs_dir_entry_t entry;
    int r = brainfs_dir_find(mnt, parent_cluster, name, &entry);
    if (r != 0) return -1;

    uint32_t first_cluster = ((uint32_t)entry.first_cluster_hi << 16) | entry.first_cluster_lo;

    if (first_cluster >= 2) {
        brainfs_fat_free_chain(mnt, first_cluster);
    }

    return 0;
}

/* ---- File Operations ---- */
int brainfs_file_open(brainfs_mount_t *mnt, const char *path, uint8_t mode,
                       brainfs_file_t **out) {
    if (!mnt || !path || !out) return -2;

    brainfs_file_t *file = 0;
    for (uint32_t i = 0; i < BRAINFS_MAX_OPEN_FILES; i++) {
        if (!mnt->open_files[i].open) {
            file = &mnt->open_files[i];
            break;
        }
    }
    if (!file) return -1;

    brainfs_dir_entry_t dir_entry;
    int r = brainfs_dir_find(mnt, mnt->root_cluster, path, &dir_entry);
    if (r != 0 && mode != 2) return -1;

    if (r == 0) {
        uint8_t *d = (uint8_t*)file;
        for (uint32_t i = 0; i < sizeof(brainfs_file_t); i++) d[i] = 0;

        uint32_t name_len = 0;
        while (path[name_len] && name_len < BRAINFS_MAX_PATH - 1) {
            file->path[name_len] = path[name_len];
            name_len++;
        }
        file->path[name_len] = 0;

        file->first_cluster = ((uint32_t)dir_entry.first_cluster_hi << 16) |
                               dir_entry.first_cluster_lo;
        file->current_cluster = file->first_cluster;
        file->file_size = dir_entry.file_size;
        file->position = 0;
        file->mode = mode;
        file->open = 1;
        file->dir_cluster = mnt->root_cluster;
        file->dir_index = 0;

        /* Pre-resolve the first cluster so the file's data is immediately
         * available in the VMM VA space without a fault on first read. */
        if (file->first_cluster >= 2) {
            if (vmm_cluster_phys(file->first_cluster) == 0)
                vmm_cluster_resolve(file->first_cluster);
        }

        *out = file;
        return 0;
    }

    return -1;
}

int brainfs_file_close(brainfs_file_t *file) {
    if (!file) return -2;
    file->open = 0;
    return 0;
}

int brainfs_file_read(brainfs_file_t *file, void *buf, uint32_t len, uint32_t *out_len) {
    if (!file || !buf || !out_len) return -2;
    if (!file->open) return -2;

    brainfs_mount_t *mnt = find_mount(0);
    if (!mnt) return -2;

    uint32_t bytes_read = 0;
    uint8_t *out = (uint8_t*)buf;

    while (len > 0 && file->position < file->file_size) {
        uint32_t cluster_size = BRAINFS_SECTOR_SIZE * mnt->bs.sectors_per_cluster;
        uint32_t offset_in_cluster = file->position % cluster_size;
        uint32_t to_read = cluster_size - offset_in_cluster;
        if (to_read > len) to_read = len;
        if (file->position + to_read > file->file_size)
            to_read = file->file_size - file->position;

        brainfs_cluster_read(mnt, file->current_cluster, out + bytes_read,
                             offset_in_cluster, to_read);

        file->position += to_read;
        bytes_read += to_read;
        len -= to_read;

        if (file->position % cluster_size == 0 && file->position < file->file_size) {
            uint32_t next;
            brainfs_fat_next_cluster(mnt, file->current_cluster, &next);
            if (next == 0) break;
            file->current_cluster = next;

            /* Pre-resolve the next cluster in the VMM VA space so the
             * next iteration of this loop hits a mapped page. */
            if (vmm_cluster_phys(next) == 0)
                vmm_cluster_resolve(next);
        }
    }

    *out_len = bytes_read;
    return 0;
}

int brainfs_file_write(brainfs_file_t *file, const void *buf, uint32_t len) {
    if (!file || !buf) return -2;
    if (!file->open || file->mode == 0) return -2;

    brainfs_mount_t *mnt = find_mount(0);
    if (!mnt) return -2;

    const uint8_t *src = (const uint8_t*)buf;
    uint32_t written = 0;

    while (written < len) {
        uint32_t cluster_size = BRAINFS_SECTOR_SIZE * mnt->bs.sectors_per_cluster;
        uint32_t offset_in_cluster = file->position % cluster_size;
        uint32_t to_write = cluster_size - offset_in_cluster;
        if (to_write > len - written) to_write = len - written;

        if (file->current_cluster == 0 || file->position >= file->file_size) {
            uint32_t new_cluster;
            int r = brainfs_fat_alloc_cluster(mnt, &new_cluster);
            if (r != 0) break;

            if (file->first_cluster == 0) {
                file->first_cluster = new_cluster;
                file->current_cluster = new_cluster;
            } else {
                brainfs_fat_write_entry(mnt, file->current_cluster, new_cluster);
                brainfs_fat_write_entry(mnt, new_cluster,
                    mnt->fat_width == 128 ? 1 :
                    mnt->fat_width == 64  ? 0xFFFFFFFFFFFFFFF8ULL :
                    mnt->fat_width == 32  ? 0x0FFFFFF8 :
                    mnt->fat_width == 16  ? 0xFFF8 :
                    mnt->fat_width == 12  ? 0xFF8 :
                    mnt->fat_width == 8   ? 0xFF :
                    mnt->fat_width == 4   ? 15 :
                    mnt->fat_width == 2   ? 3 : 1);
                file->current_cluster = new_cluster;
            }
        }

        brainfs_cluster_write(mnt, file->current_cluster, src + written,
                              offset_in_cluster, to_write);

        file->position += to_write;
        if (file->position > file->file_size) file->file_size = file->position;
        written += to_write;

        if (file->position % cluster_size == 0) {
            uint32_t next;
            brainfs_fat_next_cluster(mnt, file->current_cluster, &next);
            if (next == 0) break;
            file->current_cluster = next;
        }
    }

    return 0;
}

int brainfs_file_seek(brainfs_file_t *file, uint32_t offset) {
    if (!file) return -2;
    file->position = offset;
    return 0;
}

int brainfs_file_tell(brainfs_file_t *file, uint32_t *offset) {
    if (!file || !offset) return -2;
    *offset = file->position;
    return 0;
}

int brainfs_file_truncate(brainfs_file_t *file, uint32_t size) {
    if (!file || !file->open) return -2;

    brainfs_mount_t *mnt = find_mount(0);
    if (!mnt) return -2;

    if (size < file->file_size) {
        uint32_t cluster_size = BRAINFS_SECTOR_SIZE * mnt->bs.sectors_per_cluster;
        uint32_t clusters_needed = (size + cluster_size - 1) / cluster_size;

        uint32_t current = file->first_cluster;
        uint32_t count = 0;
        while (current && count < clusters_needed - 1) {
            uint32_t next;
            brainfs_fat_next_cluster(mnt, current, &next);
            current = next;
            count++;
        }

        if (current) {
            uint32_t next;
            brainfs_fat_next_cluster(mnt, current, &next);
            if (next) {
                brainfs_fat_free_chain(mnt, next);
                brainfs_fat_write_entry(mnt, current,
                    mnt->fat_width == 32 ? 0x0FFFFFF8 :
                    mnt->fat_width == 16 ? 0xFFF8 :
                    mnt->fat_width == 12 ? 0xFF8 :
                    mnt->fat_width == 8  ? 0xFF :
                    mnt->fat_width == 4  ? 15 :
                    mnt->fat_width == 2  ? 3 : 1);
            }
        }
    }

    file->file_size = size;
    if (file->position > size) file->position = size;
    return 0;
}

int brainfs_file_create(brainfs_mount_t *mnt, const char *path, uint8_t attr) {
    if (!mnt || !path) return -2;

    const char *name = path;
    while (*name && *name != '/') name++;
    if (*name == '/') name++;

    return brainfs_dir_create(mnt, mnt->root_cluster, name, attr);
}

int brainfs_file_delete(brainfs_mount_t *mnt, const char *path) {
    if (!mnt || !path) return -2;

    const char *name = path;
    while (*name && *name != '/') name++;
    if (*name == '/') name++;

    return brainfs_dir_delete(mnt, mnt->root_cluster, name);
}

int brainfs_file_exists(brainfs_mount_t *mnt, const char *path) {
    if (!mnt || !path) return -2;

    const char *name = path;
    while (*name && *name != '/') name++;
    if (*name == '/') name++;

    brainfs_dir_entry_t entry;
    return brainfs_dir_find(mnt, mnt->root_cluster, name, &entry) == 0 ? 1 : 0;
}

int brainfs_flush(brainfs_mount_t *mnt) {
    if (!mnt) return -2;
    if (mnt->fat.dirty) {
        mnt->fat.dirty = 0;
    }
    return 0;
}
