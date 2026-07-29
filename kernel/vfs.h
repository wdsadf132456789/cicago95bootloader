#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>

/* Forward declarations */
struct vfs_node;
struct vfs_ops;

/* Node types */
#define VFS_FILE      1
#define VFS_DIR       2
#define VFS_CHARDEV   3
#define VFS_BLOCKDEV  4
#define VFS_SYMLINK   5

/* File flags */
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0100
#define O_TRUNC    0x0200
#define O_APPEND   0x0400

/* Maximum paths / name lengths */
#define VFS_NAME_LEN   64
#define VFS_PATH_LEN   256
#define VFS_MAX_FDS    16
#define VFS_MAX_CHILDREN 128
#define VFS_MAX_MOUNTS  8

/* File operations */
typedef struct vfs_ops {
    int     (*open)(struct vfs_node *node, uint32_t flags);
    int     (*close)(struct vfs_node *node);
    ssize_t (*read)(struct vfs_node *node, uint64_t offset, void *buf, size_t len);
    ssize_t (*write)(struct vfs_node *node, uint64_t offset, const void *buf, size_t len);
    int     (*readdir)(struct vfs_node *node, uint32_t index, struct vfs_node *out);
    int     (*finddir)(struct vfs_node *node, const char *name, struct vfs_node *out);
    int     (*create)(struct vfs_node *node, const char *name, uint32_t type);
    int     (*mkdir)(struct vfs_node *node, const char *name);
    int     (*unlink)(struct vfs_node *node, const char *name);
    int     (*stat)(struct vfs_node *node, void *statbuf);
} vfs_ops_t;

/* VFS node (both files and directories) */
typedef struct vfs_node {
    char        name[VFS_NAME_LEN];
    uint32_t    type;
    uint32_t    length;       /* file size in bytes */
    uint32_t    permissions;
    uint32_t    flags;
    uint64_t    inode;        /* filesystem-specific inode number */
    uint64_t    impl_data;    /* filesystem-specific private data */
    vfs_ops_t  *ops;

    /* Tree links */
    struct vfs_node *parent;
    struct vfs_node *children[VFS_MAX_CHILDREN];
    uint32_t         child_count;

    /* Device number (for char/block devs) */
    uint32_t    major;
    uint32_t    minor;
} vfs_node_t;

/* Per-process file descriptor entry */
typedef struct {
    vfs_node_t *node;
    uint32_t    flags;
    uint64_t    offset;
    int         in_use;
} vfs_fd_entry_t;

/* File descriptor table in process */
typedef struct {
    vfs_fd_entry_t entries[VFS_MAX_FDS];
    int count;
} vfs_fd_table_t;

/* Initialize VFS subsystem, mount root */
void vfs_init(void);

/* Mount a filesystem at a path */
int vfs_mount(const char *path, vfs_node_t *fs_root);

/* Path resolution */
vfs_node_t *vfs_resolve(const char *path);
vfs_node_t *vfs_open(const char *path, uint32_t flags);
int         vfs_close(vfs_node_t *node);

/* Read / write */
ssize_t vfs_read(vfs_node_t *node, uint64_t offset, void *buf, size_t len);
ssize_t vfs_write(vfs_node_t *node, uint64_t offset, const void *buf, size_t len);

/* Directory operations */
int vfs_mkdir(const char *path);
int vfs_readdir(vfs_node_t *node, uint32_t index, vfs_node_t *out);
vfs_node_t *vfs_finddir(vfs_node_t *node, const char *name);

/* File creation / deletion */
int vfs_create(const char *path, uint32_t type);
int vfs_unlink(const char *path);

/* Stat */
int vfs_stat(vfs_node_t *node, void *statbuf);

/* FD table helpers (per-process, called with current process) */
int         vfs_fd_alloc(vfs_node_t *node, uint32_t flags);
int         vfs_fd_close(int fd);
vfs_node_t *vfs_fd_get(int fd);
uint64_t   *vfs_fd_offset(int fd);
int         vfs_fd_dup(int oldfd);

/* Register a device node */
void vfs_register_dev(const char *path, vfs_node_t *node);

/* Built-in filesystem inits */
void devfs_init(void);
void procfs_init(void);
void tmpfs_init(void);

/* Create a new in-memory tmpfs node */
vfs_node_t *tmpfs_create_node(const char *name, uint32_t type);

#endif
