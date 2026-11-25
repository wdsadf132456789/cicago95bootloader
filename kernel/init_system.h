#ifndef INIT_SYSTEM_H
#define INIT_SYSTEM_H

#include <stdint.h>

#define INIT_NAME_LEN     32
#define INIT_MAX_MODULES  32
#define INIT_MAX_SERVICES 32

/* Services with this flag are started automatically by PID 1. */
#define INIT_SERVICE_AUTOSTART 0x01

typedef int  (*init_module_fn)(void);
typedef void (*init_service_fn)(void);

typedef struct {
    char name[INIT_NAME_LEN];
    init_module_fn load;
    int loaded;
    int failed;
} init_module_t;

typedef struct {
    char name[INIT_NAME_LEN];
    init_service_fn entry;
    uint32_t flags;
    int pid;
    int started;
} init_service_t;

/* Initialize the registries, register built-ins, and load modules. */
void init_system_init(void);

/* Registration API for built-in and future dynamically loaded code. */
int init_system_register_module(const char *name, init_module_fn load);
int init_system_register_service(const char *name, uint32_t flags,
                                 init_service_fn entry);

/* Load/start by name, or process the whole registry. */
int init_system_load_module(const char *name);
int init_system_load_modules(void);
int init_system_start_service(const char *name);
int init_system_start_services(void);

/* Introspection used by the boot log and shell commands. */
uint32_t init_system_module_count(void);
uint32_t init_system_service_count(void);
uint32_t init_system_loaded_module_count(void);
uint32_t init_system_running_service_count(void);

#endif
