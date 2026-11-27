/**
 * Chicago-95 Init System
 *
 * Maintains small static registries of built-in kernel modules and services.
 * Modules are loaded during kernel_main(); services are launched as processes
 * by PID 1 once the process scheduler is running.
 *
 * Boot modules come from the shared boot_info_t ABI filled in by stage 3.
 */

#include "init_system.h"
#include "boot_info.h"
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
static int module_console(uint64_t arg1, uint64_t arg2) {
    (void)arg1;
    (void)arg2;
    return device_find_by_name("console") ? 0 : -1;
}

static int module_serial(uint64_t arg1, uint64_t arg2) {
    (void)arg1;
    (void)arg2;
    return device_find_by_name("serial0") ? 0 : -1;
}

static int module_keyboard(uint64_t arg1, uint64_t arg2) {
    (void)arg1;
    (void)arg2;
    return device_find_by_name("keyboard") ? 0 : -1;
}

static int module_timer(uint64_t arg1, uint64_t arg2) {
    (void)arg1;
    (void)arg2;
    return device_find_by_name("timer") ? 0 : -1;
}

static int module_storage(uint64_t arg1, uint64_t arg2) {
    (void)arg1;
    (void)arg2;
    if (device_find_by_name("sda")) return 0;
    if (device_find_by_name("nvme0")) return 0;
    return -1;
}

/* ---- Boot modules ------------------------------------------------------- */

static int module_boot(uint64_t address, uint64_t size) {
    if (address == 0 || size < sizeof(module_image_header_t)) {
        kmsg_warn("init: boot module image is too small");
        return -1;
    }

    const module_image_header_t *hdr =
        (const module_image_header_t *)(unsigned long)address;
    if (hdr->magic != MODULE_IMAGE_MAGIC) {
        kmsg_warn("init: boot module at 0x%x has no MOD1 header",
                  (uint64_t)address);
        return -1;
    }
    if (hdr->version != MODULE_IMAGE_VERSION) {
        kmsg_warn("init: boot module '%s' has unsupported version %u",
                  hdr->name, (uint64_t)hdr->version);
        return -1;
    }

    kmsg_info("init: module '%s' image @ 0x%x (%u bytes, flags 0x%x)",
              hdr->name, (uint64_t)address, (uint64_t)size,
              (uint64_t)hdr->flags);

    if (hdr->flags & BOOT_MODULE_FLAG_DATA) {
        const char *bytes = (const char *)(unsigned long)address;
        uint64_t pos = sizeof(*hdr);
        char text[97];
        uint32_t out = 0;

        while (pos < size && bytes[pos] == '\0') pos++;
        while (pos < size && out < sizeof(text) - 1 &&
               bytes[pos] && bytes[pos] != '\n') {
            text[out++] = bytes[pos++];
        }
        text[out] = '\0';
        if (out) kmsg_info("init: module '%s' says: %s", hdr->name, text);
    }

    return 0;
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
    uint32_t len = 0;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[len] && len < INIT_NAME_LEN - 1) len++;
    for (uint32_t i = 0; i < len; i++) dst[i] = src[i];
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

int init_system_register_module(const char *name, init_module_fn load,
                                uint64_t arg1, uint64_t arg2,
                                uint32_t origin) {
    if (!name || !load) return -1;
    if (module_count >= INIT_MAX_MODULES) return -1;
    if (find_module(name)) return -1;

    init_module_t *module = &modules[module_count++];
    copy_name(module->name, name);
    module->load = load;
    module->arg1 = arg1;
    module->arg2 = arg2;
    module->origin = origin;
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

    if (module->load(module->arg1, module->arg2) != 0) {
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

int init_system_load_boot_modules(void) {
    if (!kernel_boot_info_valid) return 0;
    if (kernel_boot_info.magic != BOOT_INFO_MAGIC) return 0;

    uint32_t count = kernel_boot_info.module_count;
    if (count > BOOT_MODULE_MAX) count = BOOT_MODULE_MAX;

    int failures = 0;
    for (uint32_t i = 0; i < count; i++) {
        boot_module_t *mod = &kernel_boot_info.modules[i];
        if (mod->address == 0 || mod->size == 0) {
            kmsg_warn("init: boot module %u has no image", (uint64_t)i);
            failures++;
            continue;
        }

        if (init_system_register_module(mod->name, module_boot,
                                        mod->address, mod->size,
                                        INIT_MODULE_BOOT) != 0) {
            kmsg_warn("init: boot module '%s' could not be registered",
                      mod->name);
            failures++;
            continue;
        }

        if (init_system_load_module(mod->name) != 0) failures++;
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

const init_module_t *init_system_get_module(uint32_t index) {
    if (index >= module_count) return NULL;
    return &modules[index];
}

const init_service_t *init_system_get_service(uint32_t index) {
    if (index >= service_count) return NULL;
    return &services[index];
}

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
    for (uint32_t i = 0; i < INIT_MAX_MODULES; i++) {
        modules[i].name[0] = '\0';
        modules[i].load = NULL;
        modules[i].arg1 = 0;
        modules[i].arg2 = 0;
        modules[i].origin = 0;
        modules[i].loaded = 0;
        modules[i].failed = 0;
    }
    for (uint32_t i = 0; i < INIT_MAX_SERVICES; i++) {
        services[i].name[0] = '\0';
        services[i].entry = NULL;
        services[i].flags = 0;
        services[i].pid = -1;
        services[i].started = 0;
    }
    module_count = 0;
    service_count = 0;

    init_system_register_module("console", module_console, 0, 0,
                                INIT_MODULE_BUILTIN);
    init_system_register_module("serial", module_serial, 0, 0,
                                INIT_MODULE_BUILTIN);
    init_system_register_module("keyboard", module_keyboard, 0, 0,
                                INIT_MODULE_BUILTIN);
    init_system_register_module("timer", module_timer, 0, 0,
                                INIT_MODULE_BUILTIN);
    init_system_register_module("storage", module_storage, 0, 0,
                                INIT_MODULE_BUILTIN);

    init_system_register_service("sysmon", INIT_SERVICE_AUTOSTART, service_sysmon);
    init_system_register_service("shell", INIT_SERVICE_AUTOSTART, service_shell);
    init_system_register_service("btop", 0, service_btop);

    init_system_load_modules();
    init_system_load_boot_modules();

    kmsg_info("init system ready (%u modules loaded, %u services registered)",
              (uint64_t)init_system_loaded_module_count(),
              (uint64_t)service_count);

    for (uint32_t i = 0; i < module_count; i++) {
        const init_module_t *module = &modules[i];
        kmsg_info("init module[%u]: %s (%s, %s)",
                  (uint64_t)i, module->name,
                  module->origin == INIT_MODULE_BOOT ? "boot" : "builtin",
                  module->failed ? "failed" : (module->loaded ? "loaded" : "pending"));
    }
    for (uint32_t i = 0; i < service_count; i++) {
        const init_service_t *service = &services[i];
        kmsg_info("init service[%u]: %s (%s)",
                  (uint64_t)i, service->name,
                  service->flags & INIT_SERVICE_AUTOSTART ? "autostart" : "manual");
    }
}
