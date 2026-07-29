#include "init.h"
#include "shell.h"
#include "console.h"
#include "process.h"
#include "timer.h"
#include "keyboard.h"
#include <stdint.h>

void init_main(void) {
    console_puts("init: starting shell\n", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));
    shell_main();
}
