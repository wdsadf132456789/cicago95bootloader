/**
 * Chicago-95 BrainVFS - Virtual Filesystem Layer
 * Mount-point abstraction, file descriptors, path resolution
 * Sits on top of BrainFS
 */

#ifndef BRAIN_VFS_H
#define BRAIN_VFS_H

#include "fs/brainfs.h"

#define BRAINVFS_MAX_MOUNTS     8
#define BRAINVFS_MAX_FDS        64
#define BRAINVFS_MAX_PATH       512
#define BRAINVFS_MAX_FS_NAME    32

/* VFS node types */
#define VFS_TYPE_FILE           0x01
#define VFS_TYPE_DIR            0x02
#define VFS_TYPE_CHARDEV        0x03
#define VFS_TYPE_BLOCKDEV       0x04
#define VFS_TYPE_SYMLINK        0x05
#define VFS_TYPE_PIPE           0x06

/* VFS flags */
#define VFS_FLAG_READONLY       0x01
#define VFS_FLAG_HIDDEN         0x02
#define VFS_FLAG_SYSTEM         0x04
#define VFS_FLAG_MOUNTED        0x08

/* File descriptor flags */
#define FD_FLAG_OPEN            0x01
#define FD_FLAG_READ            0x02
#define FD_FLAG_WRITE           0x04
#define FD_FLAG_APPEND          0x08
#define FD_FLAG_TRUNCATE        0x10

/* Seek modes */
#define VFS_SEEK_SET            0
#define VFS_SEEK_CUR            1
#define VFS_SEK_END             2

/* ---- VFS Stat ---- */
typedef struct {
    uint32_t size;           /* File size in bytes */
    uint32_t type;           /* VFS_TYPE_* */
    uint32_t mode;           /* Permissions */
    uint32_t flags;          /* VFS_FLAG_* */
    uint32_t first_cluster;  /* First data cluster */
    uint32_t clusters;       /* Number of clusters used */
    uint16_t create_time;
    uint16_t create_date;
    uint16_t modify_time;
    uint16_t modify_date;
} vfs_stat_t;

/* ---- VFS Dir Entry ---- */
typedef struct {
    char     name[256];
    uint32_t type;
    uint32_t size;
    uint8_t  attr;
} vfs_dirent_t;

/* ---- VFS Operations table ---- */
typedef struct vfs_ops {
    int  (*open)(void *ctx, const char *path, uint32_t flags);
    int  (*close)(void *ctx, uint32_t fd);
    int  (*read)(void *ctx, uint32_t fd, void *buf, uint32_t len, uint32_t *out);
    int  (*write)(void *ctx, uint32_t fd, const void *buf, uint32_t len);
    int  (*seek)(void *ctx, uint32_t fd, int32_t offset, uint32_t whence, uint32_t *new_pos);
    int  (*tell)(void *ctx, uint32_t fd, uint32_t *pos);
    int  (*truncate)(void *ctx, uint32_t fd, uint32_t size);
    int  (*stat)(void *ctx, const char *path, vfs_stat_t *st);
    int  (*mkdir)(void *ctx, const char *path, uint32_t mode);
    int  (*rmdir)(void *ctx, const char *path);
    int  (*readdir)(void *ctx, const char *path, vfs_dirent_t *entries, uint32_t max);
    int  (*rename)(void *ctx, const char *old_path, const char *new_path);
    int  (*unlink)(void *ctx, const char *path);
    int  (*sync)(void *ctx);
} vfs_ops_t;

/* ---- VFS Mount Point ---- */
typedef struct {
    char     mount_path[128];       /* e.g. "/boot", "/" */
    char     fs_name[32];           /* e.g. "brainfs", "devfs" */
    uint8_t  drive;                 /* BrainFS drive letter */
    uint8_t  fat_width;             /* BrainFS FAT width */
    void     *fs_context;           /* FS-specific context */
    vfs_ops_t ops;                  /* Operations table */
    uint32_t flags;
    uint8_t  active;
} vfs_mount_t;

/* ---- VFS File Descriptor ---- */
typedef struct {
    uint32_t id;                    /* FD number */
    uint8_t  flags;                 /* FD_FLAG_* */
    uint32_t position;              /* Current offset */
    uint32_t size;                  /* File size */
    char     path[BRAINVFS_MAX_PATH];
    uint32_t mount_idx;             /* Index into mount table */
    void     *fs_handle;            /* FS-specific file handle */
    uint8_t  in_use;
} vfs_fd_t;

/* ---- VFS Global State ---- */
typedef struct {
    vfs_mount_t mounts[BRAINVFS_MAX_MOUNTS];
    uint32_t mount_count;
    vfs_fd_t fds[BRAINVFS_MAX_FDS];
    uint32_t fd_count;
    uint32_t next_fd;
    uint8_t  initialized;
} vfs_state_t;

/* ========================================================================
 * BrainVFS API
 * ======================================================================== */

/* Init */
int  vfs_init(void);
int  brainvfs_init(void);
int  brainvfs_mount(const char *device, const char *mount_path,
                    const char *fs_name, uint8_t flags);

/* Mount / Unmount */
int  vfs_mount(const char *mount_path, const char *fs_name, uint8_t drive,
               uint8_t fat_width, vfs_ops_t *ops);
int  vfs_umount(const char *mount_path);
vfs_mount_t *vfs_get_mount(const char *path);

/* Path resolution */
int  vfs_resolve_path(const char *full_path, vfs_mount_t **mnt,
                      char *rel_path, uint32_t rel_max);

/* File operations */
int  vfs_open(const char *path, uint32_t flags);
int  vfs_close(uint32_t fd);
int  vfs_read(uint32_t fd, void *buf, uint32_t len, uint32_t *out);
int  vfs_write(uint32_t fd, const void *buf, uint32_t len);
int  vfs_seek(uint32_t fd, int32_t offset, uint32_t whence);
int  vfs_tell(uint32_t fd, uint32_t *pos);
int  vfs_truncate(uint32_t fd, uint32_t size);
int  vfs_stat(const char *path, vfs_stat_t *st);
int  vfs_unlink(const char *path);
int  vfs_rename(const char *old_path, const char *new_path);

/* Directory operations */
int  vfs_mkdir(const char *path, uint32_t mode);
int  vfs_rmdir(const char *path);
int  vfs_readdir(const char *path, vfs_dirent_t *entries, uint32_t max);

/* Sync */
int  vfs_sync(uint32_t fd);
int  vfs_sync_all(void);

/* Utility */
int  vfs_get_fd(uint32_t id, vfs_fd_t **out);
int  vfs_path_depth(const char *path);

#endif /* BRAIN_VFS_H */
