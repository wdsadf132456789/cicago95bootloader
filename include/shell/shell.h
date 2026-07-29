/**
 * Chicago-95 Encrypted .onion Shell Session + Fish Shell
 */

#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

/* Basic shell */
int shell_init(void);
int shell_run(void);
void shell_exit(void);

/* Fish shell (extends basic shell) */
int  fish_init(void);
int  fish_run(void);
void fish_exit(void);

#endif
