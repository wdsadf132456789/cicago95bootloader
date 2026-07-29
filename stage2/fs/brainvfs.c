/**
 * Chicago-95 BrainVFS - Virtual Filesystem Implementation
 * Mount-point abstraction, file descriptor table, path resolution
 * Pluggable backend operations for different FS types
 */

#include "fs/brainvfs.h"
#include "boot/security.h"

/* Global VFS state */
static vfs_state_t vfs;

/* ---- Init ---- */
int vfs_init(void) {
    for (uint32_t i = 0; i < BRAINVFS_MAX_MOUNTS; i++)
        vfs.mounts[i].active = 0;
    for (uint32_t i = 0; i < BRAINVFS_MAX_FDS; i++)
        vfs.fds[i].in_use = 0;

    vfs.mount_count = 0;
    vfs.fd_count = 0;
    vfs.next_fd = 3;  /* 0=stdin, 1=stdout, 2=stderr reserved */
    vfs.initialized = 1;

    return 0;
}

/* ---- Mount ---- */
int vfs_mount(const char *mount_path, const char *fs_name, uint8_t drive,
               uint8_t fat_width, vfs_ops_t *ops) {
    if (!mount_path || !fs_name) return -1;
    if (vfs.mount_count >= BRAINVFS_MAX_MOUNTS) return -1;

    /* Check not already mounted */
    for (uint32_t i = 0; i < vfs.mount_count; i++) {
        if (vfs.mounts[i].active) {
            int match = 1;
            for (uint32_t j = 0; mount_path[j] || vfs.mounts[i].mount_path[j]; j++) {
                if (mount_path[j] != vfs.mounts[i].mount_path[j]) { match = 0; break; }
            }
            if (match) return -1; /* Already mounted */
        }
    }

    vfs_mount_t *mnt = &vfs.mounts[vfs.mount_count];

    /* Copy mount path */
    uint32_t i;
    for (i = 0; mount_path[i] && i < 127; i++)
        mnt->mount_path[i] = mount_path[i];
    mnt->mount_path[i] = 0;

    /* Copy FS name */
    for (i = 0; fs_name[i] && i < 31; i++)
        mnt->fs_name[i] = fs_name[i];
    mnt->fs_name[i] = 0;

    mnt->drive = drive;
    mnt->fat_width = fat_width;
    mnt->flags = VFS_FLAG_MOUNTED;

    if (ops) {
        uint8_t *d = (uint8_t*)&mnt->ops;
        const uint8_t *s = (const uint8_t*)ops;
        for (uint32_t j = 0; j < sizeof(vfs_ops_t); j++) d[j] = s[j];
    }

    mnt->active = 1;
    vfs.mount_count++;

    return 0;
}

/* ---- Unmount ---- */
int vfs_umount(const char *mount_path) {
    if (!mount_path) return -1;

    for (uint32_t i = 0; i < vfs.mount_count; i++) {
        if (!vfs.mounts[i].active) continue;

        int match = 1;
        for (uint32_t j = 0; mount_path[j] || vfs.mounts[i].mount_path[j]; j++) {
            if (mount_path[j] != vfs.mounts[i].mount_path[j]) { match = 0; break; }
        }

        if (match) {
            /* Close all FDs on this mount */
            for (uint32_t fd = 0; fd < BRAINVFS_MAX_FDS; fd++) {
                if (vfs.fds[fd].in_use && vfs.fds[fd].mount_idx == i) {
                    vfs_close(vfs.fds[fd].id);
                }
            }

            /* Sync if possible */
            if (vfs.mounts[i].ops.sync) {
                vfs.mounts[i].ops.sync(vfs.mounts[i].fs_context);
            }

            vfs.mounts[i].active = 0;
            vfs.mount_count--;
            return 0;
        }
    }

    return -1;
}

/* ---- Find mount for path ---- */
vfs_mount_t *vfs_get_mount(const char *path) {
    if (!path) return 0;

    vfs_mount_t *best = 0;
    uint32_t best_depth = 0;

    for (uint32_t i = 0; i < vfs.mount_count; i++) {
        if (!vfs.mounts[i].active) continue;

        /* Check if path starts with mount_path */
        int match = 1;
        for (uint32_t j = 0; vfs.mounts[i].mount_path[j]; j++) {
            if (path[j] != vfs.mounts[i].mount_path[j]) { match = 0; break; }
        }

        if (match) {
            uint32_t depth = vfs_path_depth(vfs.mounts[i].mount_path);
            if (depth >= best_depth) {
                best_depth = depth;
                best = &vfs.mounts[i];
            }
        }
    }

    return best;
}

/* ---- Resolve path to mount + relative path ---- */
int vfs_resolve_path(const char *full_path, vfs_mount_t **mnt,
                      char *rel_path, uint32_t rel_max) {
    if (!full_path || !mnt || !rel_path) return -1;

    vfs_mount_t *mount = vfs_get_mount(full_path);
    if (!mount) return -1;

    *mnt = mount;

    /* Compute relative path (skip mount_path prefix) */
    uint32_t prefix_len = 0;
    while (mount->mount_path[prefix_len]) prefix_len++;

    uint32_t i = 0;
    while (full_path[prefix_len + i] && i < rel_max - 1) {
        rel_path[i] = full_path[prefix_len + i];
        i++;
    }
    rel_path[i] = 0;

    /* Remove leading slash */
    if (rel_path[0] == '/') {
        for (uint32_t j = 0; rel_path[j]; j++)
            rel_path[j] = rel_path[j + 1];
    }

    return 0;
}

/* ---- Path depth ---- */
int vfs_path_depth(const char *path) {
    if (!path) return 0;
    int depth = 0;
    for (uint32_t i = 0; path[i]; i++) {
        if (path[i] == '/') depth++;
    }
    return depth;
}

/* ---- Get FD by id ---- */
int vfs_get_fd(uint32_t id, vfs_fd_t **out) {
    if (!out) return -1;
    for (uint32_t i = 0; i < BRAINVFS_MAX_FDS; i++) {
        if (vfs.fds[i].in_use && vfs.fds[i].id == id) {
            *out = &vfs.fds[i];
            return 0;
        }
    }
    return -1;
}

/* ---- Open ---- */
int vfs_open(const char *path, uint32_t flags) {
    if (!path || !vfs.initialized) return -1;

    vfs_mount_t *mnt = 0;
    char rel_path[256];
    if (vfs_resolve_path(path, &mnt, rel_path, 256) != 0) return -1;
    if (!mnt) return -1;

    /* Find free FD */
    vfs_fd_t *fd = 0;
    for (uint32_t i = 0; i < BRAINVFS_MAX_FDS; i++) {
        if (!vfs.fds[i].in_use) {
            fd = &vfs.fds[i];
            break;
        }
    }
    if (!fd) return -1;

    /* Open through FS backend */
    int fs_fd = -1;
    if (mnt->ops.open) {
        fs_fd = mnt->ops.open(mnt->fs_context, rel_path, flags);
    }
    if (fs_fd < 0 && !(flags & FD_FLAG_WRITE)) return -1;

    /* Fill FD entry */
    fd->id = vfs.next_fd++;
    fd->flags = FD_FLAG_OPEN;
    if (flags & FD_FLAG_READ)   fd->flags |= FD_FLAG_READ;
    if (flags & FD_FLAG_WRITE)  fd->flags |= FD_FLAG_WRITE;
    if (flags & FD_FLAG_APPEND) fd->flags |= FD_FLAG_APPEND;
    fd->position = 0;
    fd->mount_idx = 0;
    for (uint32_t i = 0; i < vfs.mount_count; i++) {
        if (&vfs.mounts[i] == mnt) { fd->mount_idx = i; break; }
    }

    uint32_t path_len = 0;
    while (path[path_len] && path_len < BRAINVFS_MAX_PATH - 1) {
        fd->path[path_len] = path[path_len];
        path_len++;
    }
    fd->path[path_len] = 0;

    fd->fs_handle = (void*)(uint64_t)fs_fd;
    fd->in_use = 1;
    vfs.fd_count++;

    return fd->id;
}

/* ---- Close ---- */
int vfs_close(uint32_t fd_id) {
    vfs_fd_t *fd = 0;
    if (vfs_get_fd(fd_id, &fd) != 0) return -1;

    vfs_mount_t *mnt = &vfs.mounts[fd->mount_idx];
    if (mnt->ops.close) {
        int fs_fd = (int)(uint64_t)fd->fs_handle;
        mnt->ops.close(mnt->fs_context, fs_fd);
    }

    fd->in_use = 0;
    vfs.fd_count--;
    return 0;
}

/* ---- Read ---- */
int vfs_read(uint32_t fd_id, void *buf, uint32_t len, uint32_t *out) {
    vfs_fd_t *fd = 0;
    if (vfs_get_fd(fd_id, &fd) != 0) return -1;
    if (!(fd->flags & FD_FLAG_READ)) return -1;

    vfs_mount_t *mnt = &vfs.mounts[fd->mount_idx];
    if (mnt->ops.read) {
        int fs_fd = (int)(uint64_t)fd->fs_handle;
        int r = mnt->ops.read(mnt->fs_context, fs_fd, buf, len, out);
        if (r == 0) {
            fd->position += *out;
        }
        return r;
    }

    *out = 0;
    return 0;
}

/* ---- Write ---- */
int vfs_write(uint32_t fd_id, const void *buf, uint32_t len) {
    vfs_fd_t *fd = 0;
    if (vfs_get_fd(fd_id, &fd) != 0) return -1;
    if (!(fd->flags & FD_FLAG_WRITE)) return -1;

    vfs_mount_t *mnt = &vfs.mounts[fd->mount_idx];
    if (mnt->ops.write) {
        int fs_fd = (int)(uint64_t)fd->fs_handle;
        int r = mnt->ops.write(mnt->fs_context, fs_fd, buf, len);
        if (r == 0) {
            fd->position += len;
        }
        return r;
    }

    return 0;
}

/* ---- Seek ---- */
int vfs_seek(uint32_t fd_id, int32_t offset, uint32_t whence) {
    vfs_fd_t *fd = 0;
    if (vfs_get_fd(fd_id, &fd) != 0) return -1;

    uint32_t new_pos = 0;
    switch (whence) {
        case VFS_SEEK_SET: new_pos = offset; break;
        case VFS_SEEK_CUR: new_pos = fd->position + offset; break;
        case VFS_SEK_END:  new_pos = fd->size + offset; break;
        default: return -1;
    }

    fd->position = new_pos;
    return 0;
}

/* ---- Tell ---- */
int vfs_tell(uint32_t fd_id, uint32_t *pos) {
    vfs_fd_t *fd = 0;
    if (vfs_get_fd(fd_id, &fd) != 0) return -1;
    *pos = fd->position;
    return 0;
}

/* ---- Truncate ---- */
int vfs_truncate(uint32_t fd_id, uint32_t size) {
    vfs_fd_t *fd = 0;
    if (vfs_get_fd(fd_id, &fd) != 0) return -1;

    vfs_mount_t *mnt = &vfs.mounts[fd->mount_idx];
    if (mnt->ops.truncate) {
        int fs_fd = (int)(uint64_t)fd->fs_handle;
        return mnt->ops.truncate(mnt->fs_context, fs_fd, size);
    }

    return -1;
}

/* ---- Stat ---- */
int vfs_stat(const char *path, vfs_stat_t *st) {
    if (!path || !st) return -1;

    vfs_mount_t *mnt = 0;
    char rel_path[256];
    if (vfs_resolve_path(path, &mnt, rel_path, 256) != 0) return -1;
    if (!mnt) return -1;

    if (mnt->ops.stat) {
        return mnt->ops.stat(mnt->fs_context, rel_path, st);
    }

    return -1;
}

/* ---- Unlink ---- */
int vfs_unlink(const char *path) {
    if (!path) return -1;

    vfs_mount_t *mnt = 0;
    char rel_path[256];
    if (vfs_resolve_path(path, &mnt, rel_path, 256) != 0) return -1;

    if (mnt->ops.unlink) {
        return mnt->ops.unlink(mnt->fs_context, rel_path);
    }

    return -1;
}

/* ---- Rename ---- */
int vfs_rename(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) return -1;

    vfs_mount_t *mnt_old = 0, *mnt_new = 0;
    char rel_old[256], rel_new[256];
    if (vfs_resolve_path(old_path, &mnt_old, rel_old, 256) != 0) return -1;
    if (vfs_resolve_path(new_path, &mnt_new, rel_new, 256) != 0) return -1;
    if (mnt_old != mnt_new) return -1;  /* Cross-mount rename not supported */

    if (mnt_old->ops.rename) {
        return mnt_old->ops.rename(mnt_old->fs_context, rel_old, rel_new);
    }

    return -1;
}

/* ---- Mkdir ---- */
int vfs_mkdir(const char *path, uint32_t mode) {
    if (!path) return -1;

    vfs_mount_t *mnt = 0;
    char rel_path[256];
    if (vfs_resolve_path(path, &mnt, rel_path, 256) != 0) return -1;

    if (mnt->ops.mkdir) {
        return mnt->ops.mkdir(mnt->fs_context, rel_path, mode);
    }

    return -1;
}

/* ---- Rmdir ---- */
int vfs_rmdir(const char *path) {
    if (!path) return -1;

    vfs_mount_t *mnt = 0;
    char rel_path[256];
    if (vfs_resolve_path(path, &mnt, rel_path, 256) != 0) return -1;

    if (mnt->ops.rmdir) {
        return mnt->ops.rmdir(mnt->fs_context, rel_path);
    }

    return -1;
}

/* ---- Readdir ---- */
int vfs_readdir(const char *path, vfs_dirent_t *entries, uint32_t max) {
    if (!path || !entries) return -1;

    vfs_mount_t *mnt = 0;
    char rel_path[256];
    if (vfs_resolve_path(path, &mnt, rel_path, 256) != 0) return -1;

    if (mnt->ops.readdir) {
        return mnt->ops.readdir(mnt->fs_context, rel_path, entries, max);
    }

    return -1;
}

/* ---- Sync ---- */
int vfs_sync(uint32_t fd_id) {
    vfs_fd_t *fd = 0;
    if (vfs_get_fd(fd_id, &fd) != 0) return -1;

    vfs_mount_t *mnt = &vfs.mounts[fd->mount_idx];
    if (mnt->ops.sync) {
        return mnt->ops.sync(mnt->fs_context);
    }

    return 0;
}

int vfs_sync_all(void) {
    for (uint32_t i = 0; i < vfs.mount_count; i++) {
        if (vfs.mounts[i].active && vfs.mounts[i].ops.sync) {
            vfs.mounts[i].ops.sync(vfs.mounts[i].fs_context);
        }
    }
    return 0;
}

int brainvfs_init(void) {
    return vfs_init();
}

int brainvfs_mount(const char *device, const char *mount_path,
                    const char *fs_name, uint8_t flags) {
    (void)device;
    (void)flags;
    return vfs_mount(mount_path, fs_name, 0, 16, (void *)0);
}
