/**
 * Chicago-95 Init System
 *
 * Maintains small static registries of built-in kernel modules and services.
 * Modules are loaded during kernel_main(); services are launched as processes
 * by PID 1 once the process scheduler is running.
 */

#include "init_system.h"
#include "kernel.h"
#include "kmsg.h"
#include "device.h"
#include "process.h"
#include "kmalloc.h"
#include "shell.h"
#include "btop.h"

static init_module_t modules[INIT_MAX_MODULES];
static init_service_t services[INIT_MAX_SERVICES];
static uint32_t module_count = 0;
static uint32_t service_count = 0;

/* ---- Built-in modules --------------------------------------------------- */

/*
 * The kernel initializes its subsystems directly in kernel_main().  These
 * module loaders confirm that the matching device objects were registered,
 * so a failed device setup is reported by the init system instead of being
 * silently ignored.
 */
static int module_console(void) {
    return device_find_by_name("console") ? 0 : -1;
}

static int module_serial(void) {
    return device_find_by_name("serial0") ? 0 : -1;
}

static int module_keyboard(void) {
    return device_find_by_name("keyboard") ? 0 : -1;
}

static int module_timer(void) {
    return device_find_by_name("timer") ? 0 : -1;
}

static int module_storage(void) {
    if (device_find_by_name("sda")) return 0;
    if (device_find_by_name("nvme0")) return 0;
    return -1;
}

/* ---- Built-in services -------------------------------------------------- */

static void service_shell(void) {
    shell_main();
}

static void service_btop(void) {
    btop_run();
}

static void service_sysmon(void) {
    while (1) {
        kmsg_info("init: sysmon free pages %u", (uint64_t)pmm_get_free_pages());
        process_sleep(1000);
    }
}

/* ---- Registry helpers --------------------------------------------------- */

static void copy_name(char *dst, const char *src) {
    size_t len = strlen(src);
    if (len >= INIT_NAME_LEN) len = INIT_NAME_LEN - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static init_module_t *find_module(const char *name) {
    for (uint32_t i = 0; i < module_count; i++) {
        if (strcmp(modules[i].name, name) == 0) return &modules[i];
    }
    return NULL;
}

static init_service_t *find_service(const char *name) {
    for (uint32_t i = 0; i < service_count; i++) {
        if (strcmp(services[i].name, name) == 0) return &services[i];
    }
    return NULL;
}

/* ---- Registration API --------------------------------------------------- */

int init_system_register_module(const char *name, init_module_fn load) {
    if (!name || !load) return -1;
    if (module_count >= INIT_MAX_MODULES) return -1;
    if (find_module(name)) return -1;

    init_module_t *module = &modules[module_count++];
    copy_name(module->name, name);
    module->load = load;
    module->loaded = 0;
    module->failed = 0;
    return 0;
}

int init_system_register_service(const char *name, uint32_t flags,
                                 init_service_fn entry) {
    if (!name || !entry) return -1;
    if (service_count >= INIT_MAX_SERVICES) return -1;
    if (find_service(name)) return -1;

    init_service_t *service = &services[service_count++];
    copy_name(service->name, name);
    service->entry = entry;
    service->flags = flags;
    service->pid = -1;
    service->started = 0;
    return 0;
}

/* ---- Load and start API ------------------------------------------------- */

int init_system_load_module(const char *name) {
    init_module_t *module = find_module(name);
    if (!module) return -1;
    if (module->loaded) return 0;

    if (module->load() != 0) {
        module->failed = 1;
        kmsg_warn("init: module '%s' failed to load", module->name);
        return -1;
    }

    module->loaded = 1;
    module->failed = 0;
    kmsg_info("init: module '%s' loaded", module->name);
    return 0;
}

int init_system_load_modules(void) {
    int failures = 0;
    for (uint32_t i = 0; i < module_count; i++) {
        if (init_system_load_module(modules[i].name) != 0) failures++;
    }
    return failures ? -1 : 0;
}

int init_system_start_service(const char *name) {
    init_service_t *service = find_service(name);
    if (!service) return -1;
    if (service->started) return service->pid;

    int pid = process_create(service->entry, service->name, RING0);
    if (pid < 0) {
        kmsg_err("init: service '%s' failed to start", service->name);
        return -1;
    }

    service->pid = pid;
    service->started = 1;
    kmsg_info("init: service '%s' started (PID %d)", service->name, (int64_t)pid);
    return pid;
}

int init_system_start_services(void) {
    int failures = 0;
    for (uint32_t i = 0; i < service_count; i++) {
        if (!(services[i].flags & INIT_SERVICE_AUTOSTART)) continue;
        if (init_system_start_service(services[i].name) < 0) failures++;
    }
    return failures ? -1 : 0;
}

/* ---- Introspection ------------------------------------------------------ */

uint32_t init_system_module_count(void) {
    return module_count;
}

uint32_t init_system_service_count(void) {
    return service_count;
}

uint32_t init_system_loaded_module_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < module_count; i++) {
        if (modules[i].loaded && !modules[i].failed) count++;
    }
    return count;
}

uint32_t init_system_running_service_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < service_count; i++) {
        if (services[i].started) count++;
    }
    return count;
}

/* ---- Built-in registry setup -------------------------------------------- */

void init_system_init(void) {
    memset(modules, 0, sizeof(modules));
    memset(services, 0, sizeof(services));
    module_count = 0;
    service_count = 0;

    init_system_register_module("console", module_console);
    init_system_register_module("serial", module_serial);
    init_system_register_module("keyboard", module_keyboard);
    init_system_register_module("timer", module_timer);
    init_system_register_module("storage", module_storage);

    init_system_register_service("sysmon", INIT_SERVICE_AUTOSTART, service_sysmon);
    init_system_register_service("shell", INIT_SERVICE_AUTOSTART, service_shell);
    init_system_register_service("btop", 0, service_btop);

    init_system_load_modules();

    kmsg_info("init system ready (%u modules loaded, %u services registered)",
              (uint64_t)init_system_loaded_module_count(),
              (uint64_t)service_count);
}
