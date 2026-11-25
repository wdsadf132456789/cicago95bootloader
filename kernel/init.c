#include "init.h"
#include "init_system.h"
#include "console.h"
#include "process.h"

void init_main(void) {
    console_puts("init: starting services\n",
                 CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));

    init_system_start_services();

    console_puts("init: services online\n",
                 CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));

    /* PID 1 stays alive and reaps nothing; services do the actual work. */
    while (1) {
        process_sleep(1000);
    }
}
