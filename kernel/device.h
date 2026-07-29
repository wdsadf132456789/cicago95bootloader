#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>

/* Device types */
#define DEV_CHAR    1
#define DEV_BLOCK   2
#define DEV_NET     3

#define DEV_NAME_LEN 32
#define DEV_MAX      64

/* Device operations */
typedef struct {
    ssize_t (*read)(int minor, uint64_t offset, void *buf, size_t len);
    ssize_t (*write)(int minor, uint64_t offset, const void *buf, size_t len);
    int     (*ioctl)(int minor, int cmd, uint64_t arg);
    int     (*open)(int minor);
    int     (*close)(int minor);
} dev_ops_t;

/* Device descriptor */
typedef struct {
    char        name[DEV_NAME_LEN];
    uint32_t    type;       /* DEV_CHAR / DEV_BLOCK / DEV_NET */
    uint32_t    major;
    uint32_t    minor;
    dev_ops_t  *ops;
    uint64_t    impl_data;  /* driver-private */
    int         in_use;
} device_t;

/* Register a device */
int  device_register(const char *name, uint32_t type, uint32_t major,
                     uint32_t minor, dev_ops_t *ops);

/* Find devices */
device_t *device_find_by_name(const char *name);
device_t *device_find_by_major_minor(uint32_t major, uint32_t minor);

/* List all devices (prints to console) */
void device_list(void);

/* Initialize device framework and auto-register built-in devices */
void device_init(void);

#endif
