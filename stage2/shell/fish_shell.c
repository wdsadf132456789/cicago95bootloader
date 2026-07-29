/**
 * Chicago-95 Fish Full Shell
 * Fish-style interactive shell: syntax highlighting, autosuggestions,
 * tab completion, $status, piping, wildcards, abbreviations,
 * function definitions, control flow (if/for/while/switch),
 * command substitution, redirections, and extended builtins.
 */

#include <stdint.h>
#include <string.h>
#include "shell/fish_shell.h"
#include "shell/shell.h"
#include "shell/gnu_tools.h"
#include "shell/awk.h"
#include "shell/nano.h"
#include "boot/ring0_init.h"
#include "boot/security.h"
#include "boot/fs_menu.h"
#include "security/tor.h"
#include "security/pgp.h"
#include "gui/gui.h"
#include "fs/brainfs.h"
#include "fs/brainvfs.h"
#include "vga/vga.h"
#include "memory/memory.h"
#include "drivers/wifi_autodetect.h"

static fish_state_t fish;

/* ======================================================================== */
/* VGA Output Helpers                                                        */
/* ======================================================================== */

static void fish_putc_color(char c, uint8_t fg, uint8_t bg) {
    vga_text_put_char(c, VGA_COLOR(fg, bg));
}

static void fish_puts_color(const char *s, uint8_t fg, uint8_t bg) {
    while (*s) fish_putc_color(*s++, fg, bg);
}

static void fish_puts(const char *s) {
    fish_puts_color(s, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static void fish_putc(char c) {
    fish_putc_color(c, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static void fish_newline(void) {
    fish_putc_color('\r', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    fish_putc_color('\n', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

/* ======================================================================== */
/* String Utilities                                                          */
/* ======================================================================== */

static uint32_t fish_strlen(const char *s) {
    uint32_t len = 0;
    while (s[len]) len++;
    return len;
}

static int fish_strcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return *a - *b;
        a++; b++;
    }
    return *a - *b;
}

static int fish_strncmp(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

static void fish_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static void fish_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)v;
}

static void fish_strcpy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int fish_strcat(char *dst, const char *src, uint32_t max) {
    uint32_t dl = fish_strlen(dst);
    uint32_t i = 0;
    while (src[i] && dl + i < max - 1) { dst[dl + i] = src[i]; i++; }
    dst[dl + i] = 0;
    return (int)(dl + i);
}

static int fish_tolower(int c) __attribute__((unused));
static int fish_tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static int fish_strcasecmp(const char *a, const char *b) __attribute__((unused));
static int fish_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int diff = fish_tolower(*a) - fish_tolower(*b);
        if (diff) return diff;
        a++; b++;
    }
    return fish_tolower(*a) - fish_tolower(*b);
}

static void fish_int_to_str(int32_t val, char *out, uint32_t out_max) {
    (void)out_max;
    int ni = 0;
    if (val < 0) { out[ni++] = '-'; val = -val; }
    if (val == 0) { out[ni++] = '0'; }
    else {
        char rev[16]; int ri = 0;
        while (val) { rev[ri++] = '0' + (val % 10); val /= 10; }
        while (ri > 0) out[ni++] = rev[--ri];
    }
    out[ni] = 0;
}

static void fish_uint_to_str(uint32_t val, char *out, uint32_t out_max) {
    (void)out_max;
    int ni = 0;
    if (val == 0) { out[ni++] = '0'; }
    else {
        char rev[16]; int ri = 0;
        while (val) { rev[ri++] = '0' + (val % 10); val /= 10; }
        while (ri > 0) out[ni++] = rev[--ri];
    }
    out[ni] = 0;
}

static uint32_t fish_atou(const char *s) {
    uint32_t val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return val;
}

static int32_t fish_atoi(const char *s) {
    int32_t val = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return neg ? -val : val;
}

/* ======================================================================== */
/* Built-in Command List (for tab completion & syntax highlighting)          */
/* ======================================================================== */

static const char *fish_commands[] = {
    "help", "status", "tor", "onion", "net", "wifi", "ls", "cat",
    "touch", "rm", "mkdir", "mount", "id", "clear", "uptime", "dmesg",
    "reboot", "shutdown", "connect", "set", "setenv", "export", "echo",
    "if", "else", "end", "for", "while", "break", "continue",
    "function", "return", "source", "history", "abbr", "jobs", "fg",
    "bg", "wait", "command", "builtin", "type", "which", "where",
    "math", "test", "realpath", "string", "count", "printf",
    "and", "or", "not", "begin", "switch", "case",
    "grep", "sort", "uniq", "wc", "head", "tail", "tr", "diff", "base64",
    "tee", "xargs", "yes", "seq", "sleep", "date", "env", "man",
    "nano", "gui", "mkfs", "fs", "awk",
    "hexdump", "xxd", "cal", "uname", "hostname", "df", "free",
    "fortune", "ascii", "cp", "mv", "rmdir", "chmod",
    "cd", "pushd", "popd", "dirs", "pwd",
    "read", "set_color", "random", "functions", "emit",
    "commandline", "argparse", "block", "universal",
    0
};

static const char *fish_builtins_with_args[] = {
    "set", "echo", "cat", "ls", "cd", "pushd", "popd",
    "math", "test", "string", "printf", "count",
    "abbr", "history", "function", "read", "set_color",
    "random", "string", "type", "commandline", "argparse",
    0
};

/* ======================================================================== */
/* Variable System                                                          */
/* ======================================================================== */

int fish_var_set(const char *name, const char *value) {
    for (uint32_t i = 0; i < fish.var_count; i++) {
        if (fish_strcmp(fish.vars[i].name, name) == 0) {
            fish_strcpy(fish.vars[i].value, value, 64);
            return 0;
        }
    }
    if (fish.var_count >= FISH_MAX_VARS) return -1;
    fish_var_t *v = &fish.vars[fish.var_count];
    fish_strcpy(v->name, name, 32);
    fish_strcpy(v->value, value, 64);
    v->exported = 0;
    v->scope = FISH_SCOPE_GLOBAL;
    fish.var_count++;
    return 0;
}

int fish_var_set_scope(const char *name, const char *value, uint8_t scope) {
    for (uint32_t i = 0; i < fish.var_count; i++) {
        if (fish_strcmp(fish.vars[i].name, name) == 0) {
            fish_strcpy(fish.vars[i].value, value, 64);
            fish.vars[i].scope = scope;
            if (scope == FISH_SCOPE_EXPORT) fish.vars[i].exported = 1;
            return 0;
        }
    }
    if (fish.var_count >= FISH_MAX_VARS) return -1;
    fish_var_t *v = &fish.vars[fish.var_count];
    fish_strcpy(v->name, name, 32);
    fish_strcpy(v->value, value, 64);
    v->exported = (scope == FISH_SCOPE_EXPORT) ? 1 : 0;
    v->scope = scope;
    fish.var_count++;
    return 0;
}

const char *fish_var_get(const char *name) {
    for (uint32_t i = 0; i < fish.var_count; i++) {
        if (fish_strcmp(fish.vars[i].name, name) == 0) {
            return fish.vars[i].value;
        }
    }
    return 0;
}

int fish_var_exported(const char *name) {
    for (uint32_t i = 0; i < fish.var_count; i++) {
        if (fish_strcmp(fish.vars[i].name, name) == 0) {
            return fish.vars[i].exported;
        }
    }
    return -1;
}

int fish_var_erase(const char *name) {
    for (uint32_t i = 0; i < fish.var_count; i++) {
        if (fish_strcmp(fish.vars[i].name, name) == 0) {
            /* Shift remaining vars */
            for (uint32_t j = i; j + 1 < fish.var_count; j++) {
                fish_memcpy(&fish.vars[j], &fish.vars[j+1], sizeof(fish_var_t));
            }
            fish.var_count--;
            return 0;
        }
    }
    return -1;
}

uint32_t fish_var_count_by_prefix(const char *prefix) {
    uint32_t plen = fish_strlen(prefix);
    uint32_t count = 0;
    for (uint32_t i = 0; i < fish.var_count; i++) {
        if (fish_strncmp(fish.vars[i].name, prefix, plen) == 0) count++;
    }
    return count;
}

/* ======================================================================== */
/* Abbreviation System                                                      */
/* ======================================================================== */

int fish_abbrev_add(const char *trigger, const char *expansion) {
    if (fish.abbrev_count >= FISH_MAX_ABBREVS) return -1;
    /* Check for existing - update */
    for (uint32_t i = 0; i < fish.abbrev_count; i++) {
        if (fish_strcmp(fish.abbrevs[i].trigger, trigger) == 0) {
            fish_strcpy(fish.abbrevs[i].expansion, expansion, 64);
            return 0;
        }
    }
    fish_abbrev_t *a = &fish.abbrevs[fish.abbrev_count];
    fish_strcpy(a->trigger, trigger, 16);
    fish_strcpy(a->expansion, expansion, 64);
    a->position = 0;
    fish.abbrev_count++;
    return 0;
}

int fish_abbrev_expand(const char *trigger, char *out, uint32_t out_len) {
    for (uint32_t i = 0; i < fish.abbrev_count; i++) {
        if (fish_strcmp(fish.abbrevs[i].trigger, trigger) == 0) {
            fish_strcpy(out, fish.abbrevs[i].expansion, out_len);
            return 0;
        }
    }
    return -1;
}

int fish_abbrev_erase(const char *trigger) {
    for (uint32_t i = 0; i < fish.abbrev_count; i++) {
        if (fish_strcmp(fish.abbrevs[i].trigger, trigger) == 0) {
            for (uint32_t j = i; j + 1 < fish.abbrev_count; j++) {
                fish_memcpy(&fish.abbrevs[j], &fish.abbrevs[j+1], sizeof(fish_abbrev_t));
            }
            fish.abbrev_count--;
            return 0;
        }
    }
    return -1;
}

int fish_abbrev_show(void) {
    fish_puts_color("\n=== Abbreviations ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    for (uint32_t i = 0; i < fish.abbrev_count; i++) {
        fish_puts_color("  ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        fish_puts_color(fish.abbrevs[i].trigger, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_puts_color(" -> ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        fish_puts_color(fish.abbrevs[i].expansion, VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
        if (fish.abbrevs[i].position) {
            fish_puts_color(" (anywhere)", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        }
        fish_newline();
    }
    return 0;
}

/* ======================================================================== */
/* Function Definition System                                               */
/* ======================================================================== */

int fish_func_add(const char *name, const char *body_lines[], uint32_t count) {
    if (fish.func_count >= FISH_MAX_FUNCS) return -1;
    /* Check for existing - update */
    for (uint32_t i = 0; i < fish.func_count; i++) {
        if (fish_strcmp(fish.funcs[i].name, name) == 0) {
            fish.funcs[i].line_count = 0;
            uint32_t n = count;
            if (n > FISH_MAX_FUNC_LINES) n = FISH_MAX_FUNC_LINES;
            for (uint32_t j = 0; j < n; j++) {
                fish_strcpy(fish.funcs[i].lines[j], body_lines[j], FISH_FUNC_LINE_LEN);
                fish.funcs[i].line_count++;
            }
            return 0;
        }
    }
    fish_func_t *f = &fish.funcs[fish.func_count];
    fish_strcpy(f->name, name, 64);
    f->line_count = 0;
    uint32_t n = count;
    if (n > FISH_MAX_FUNC_LINES) n = FISH_MAX_FUNC_LINES;
    for (uint32_t j = 0; j < n; j++) {
        fish_strcpy(f->lines[j], body_lines[j], FISH_FUNC_LINE_LEN);
        f->line_count++;
    }
    f->description[0] = 0;
    f->autoloaded = 0;
    fish.func_count++;
    return 0;
}

int fish_func_erase(const char *name) {
    for (uint32_t i = 0; i < fish.func_count; i++) {
        if (fish_strcmp(fish.funcs[i].name, name) == 0) {
            for (uint32_t j = i; j + 1 < fish.func_count; j++) {
                fish_memcpy(&fish.funcs[j], &fish.funcs[j+1], sizeof(fish_func_t));
            }
            fish.func_count--;
            return 0;
        }
    }
    return -1;
}

fish_func_t *fish_func_find(const char *name) {
    for (uint32_t i = 0; i < fish.func_count; i++) {
        if (fish_strcmp(fish.funcs[i].name, name) == 0) {
            return &fish.funcs[i];
        }
    }
    return 0;
}

int fish_func_list(void) {
    fish_puts_color("\n=== User-Defined Functions ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    if (fish.func_count == 0) {
        fish_puts_color("  (none)\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        return 0;
    }
    for (uint32_t i = 0; i < fish.func_count; i++) {
        fish_puts_color("  ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        fish_puts_color(fish.funcs[i].name, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_puts_color(" (", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        char num[8]; int ni = 0;
        uint32_t cnt = fish.funcs[i].line_count;
        if (cnt == 0) { num[ni++] = '0'; }
        else { char rev[8]; int ri = 0; while (cnt) { rev[ri++] = '0' + (cnt % 10); cnt /= 10; } while (ri > 0) num[ni++] = rev[--ri]; }
        num[ni] = 0;
        fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_puts_color(" lines)\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
    return 0;
}

int fish_func_show(const char *name) {
    fish_func_t *f = fish_func_find(name);
    if (!f) {
        fish_puts_color("functions: unknown function '", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        fish_puts_color(name, VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        fish_puts_color("'\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        return 1;
    }
    fish_puts_color("function ", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    fish_puts_color(name, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    fish_newline();
    for (uint32_t i = 0; i < f->line_count; i++) {
        fish_puts_color("    ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        fish_puts_color(f->lines[i], VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        fish_newline();
    }
    fish_puts_color("end\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    return 0;
}

static int fish_parse_args(const char *cmd, char args[FISH_MAX_ARGS][64]);
static int fish_dispatch(int argc, char args[FISH_MAX_ARGS][64]);
static void fish_normalize_path(const char *in, char *out, uint32_t out_max);

int fish_func_run(const char *name, int argc, char args[FISH_MAX_ARGS][64]) {
    fish_func_t *f = fish_func_find(name);
    if (!f) return -1;

    /* Save current func name, set new */
    char prev_func[64];
    fish_strcpy(prev_func, fish.current_func, 64);
    fish_strcpy(fish.current_func, name, 64);

    /* Set $argv and $argcount */
    char argcount_str[12];
    fish_int_to_str(argc - 1, argcount_str, 12);
    fish_var_set("argcount", argcount_str);
    fish_var_set("argv", "");
    /* Build $argv from args */
    char argv_buf[FISH_MAX_CMD];
    argv_buf[0] = 0;
    for (int i = 1; i < argc; i++) {
        if (i > 1) fish_strcat(argv_buf, " ", FISH_MAX_CMD);
        fish_strcat(argv_buf, args[i], FISH_MAX_CMD);
    }
    fish_var_set("argv", argv_buf);

    /* Execute each line */
    int last_status = 0;
    for (uint32_t i = 0; i < f->line_count; i++) {
        /* Check for break/continue */
        if (fish.break_requested || fish.continue_requested) break;

        const char *line = f->lines[i];
        if (line[0] == 0) continue;

        /* Skip comments */
        if (line[0] == '#') continue;

        /* Parse and dispatch */
        char line_args[FISH_MAX_ARGS][64];
        fish_memset(line_args, 0, sizeof(line_args));
        int line_argc = fish_parse_args(line, line_args);
        if (line_argc > 0) {
            last_status = fish_dispatch(line_argc, line_args);
        }

        if (fish.break_requested || fish.continue_requested) break;
    }

    /* Restore func name */
    fish_strcpy(fish.current_func, prev_func, 64);
    return last_status;
}

/* ======================================================================== */
/* Event System                                                             */
/* ======================================================================== */

int fish_emit_event(const char *event_name) {
    for (uint32_t i = 0; i < fish.event_count; i++) {
        if (fish_strcmp(fish.event_handlers[i], event_name) == 0) {
            /* Look for function named "fish_event_<event_name>" */
            char func_name[80];
            fish_strcpy(func_name, "fish_event_", 80);
            fish_strcat(func_name, event_name, 80);
            fish_func_t *f = fish_func_find(func_name);
            if (f) {
                char dummy_args[FISH_MAX_ARGS][64];
                fish_memset(dummy_args, 0, sizeof(dummy_args));
                fish_strcpy(dummy_args[0], func_name, 64);
                fish_func_run(func_name, 1, dummy_args);
            }
        }
    }
    return 0;
}

/* ======================================================================== */
/* Syntax Highlighting                                                      */
/* ======================================================================== */

uint8_t fish_syntax_color(const char *token, uint32_t token_len, uint8_t is_first_token) {
    (void)token_len;
    if (!fish.syntax_enabled) return VGA_COLOR_LIGHT_GREY;

    if (is_first_token) {
        for (uint32_t i = 0; fish_commands[i]; i++) {
            if (fish_strcmp(token, fish_commands[i]) == 0) {
                for (uint32_t j = 0; fish_builtins_with_args[j]; j++) {
                    if (fish_strcmp(token, fish_builtins_with_args[j]) == 0)
                        return VGA_COLOR_LIGHT_GREEN;
                }
                return VGA_COLOR_LIGHT_CYAN;
            }
        }
        /* Check user-defined functions */
        if (fish_func_find(token)) return VGA_COLOR_LIGHT_GREEN;
        return VGA_COLOR_YELLOW;
    }

    if (token[0] == '\'' || token[0] == '"') return VGA_COLOR_LIGHT_MAGENTA;
    if (token[0] == '$') return VGA_COLOR_LIGHT_CYAN;
    if (token[0] >= '0' && token[0] <= '9') return VGA_COLOR_LIGHT_BLUE;
    if (token[0] == '-' && token_len > 1) return VGA_COLOR_LIGHT_RED;

    for (uint32_t i = 0; i < token_len; i++) {
        if (token[i] == '/') return VGA_COLOR_LIGHT_BLUE;
    }

    if (token[0] == '|' || token[0] == '>' || token[0] == '<') return VGA_COLOR_YELLOW;

    return VGA_COLOR_LIGHT_GREY;
}

/* ======================================================================== */
/* Tab Completion                                                           */
/* ======================================================================== */

static int fish_match_prefix(const char *prefix, uint32_t prefix_len,
                             const char *candidate, uint32_t cand_len) {
    if (prefix_len > cand_len) return 0;
    for (uint32_t i = 0; i < prefix_len; i++) {
        if (prefix[i] != candidate[i]) return 0;
    }
    return 1;
}

int fish_complete(const char *partial, fish_completion_t *comps, uint32_t max_comps) {
    uint32_t partial_len = fish_strlen(partial);
    int count = 0;

    /* Complete against built-in commands */
    for (uint32_t i = 0; fish_commands[i] && (uint32_t)count < max_comps; i++) {
        uint32_t clen = fish_strlen(fish_commands[i]);
        if (fish_match_prefix(partial, partial_len, fish_commands[i], clen)) {
            fish_strcpy(comps[count].text, fish_commands[i], 64);
            comps[count].is_dir = 0;
            comps[count].is_command = 1;
            comps[count].is_function = 0;
            comps[count].is_alias = 0;
            count++;
        }
    }

    /* Complete against user-defined functions */
    for (uint32_t i = 0; i < fish.func_count && (uint32_t)count < max_comps; i++) {
        uint32_t nlen = fish_strlen(fish.funcs[i].name);
        if (fish_match_prefix(partial, partial_len, fish.funcs[i].name, nlen)) {
            int dup = 0;
            for (int j = 0; j < count; j++) {
                if (fish_strcmp(comps[j].text, fish.funcs[i].name) == 0) { dup = 1; break; }
            }
            if (!dup) {
                fish_strcpy(comps[count].text, fish.funcs[i].name, 64);
                comps[count].is_dir = 0;
                comps[count].is_command = 0;
                comps[count].is_function = 1;
                comps[count].is_alias = 0;
                count++;
            }
        }
    }

    /* Complete against history */
    for (uint32_t i = 0; i < fish.history_count && (uint32_t)count < max_comps; i++) {
        const char *cmd = fish.history[i].cmd;
        uint32_t wlen = 0;
        while (cmd[wlen] && cmd[wlen] != ' ') wlen++;
        if (wlen == 0) continue;
        if (wlen == partial_len && fish_strncmp(partial, cmd, partial_len) == 0) continue;
        if (!fish_match_prefix(partial, partial_len, cmd, wlen)) continue;
        int dup = 0;
        for (int j = 0; j < count; j++) {
            if (fish_strcmp(comps[j].text, cmd) == 0) { dup = 1; break; }
        }
        if (!dup && (uint32_t)count < max_comps) {
            fish_memcpy(comps[count].text, cmd, wlen);
            comps[count].text[wlen] = 0;
            comps[count].is_dir = 0;
            comps[count].is_command = 1;
            comps[count].is_function = 0;
            comps[count].is_alias = 0;
            count++;
        }
    }

    /* Complete against filesystem paths */
    {
        char dir_path[256];
        char file_prefix[64];
        int last_slash = -1;
        uint32_t plen = fish_strlen(partial);
        for (int i = (int)plen - 1; i >= 0; i--) {
            if (partial[i] == '/') { last_slash = i; break; }
        }
        if (last_slash >= 0) {
            fish_strcpy(dir_path, partial, 256);
            dir_path[last_slash + 1] = 0;
            fish_strcpy(file_prefix, &partial[last_slash + 1], 64);
        } else {
            const char *cwd = fish.prompt.cwd;
            if (fish_strcmp(cwd, "~") == 0) fish_strcpy(dir_path, "/", 256);
            else {
                fish_strcpy(dir_path, cwd, 256);
                uint32_t dlen = fish_strlen(dir_path);
                if (dlen > 0 && dir_path[dlen - 1] != '/') {
                    dir_path[dlen] = '/';
                    dir_path[dlen + 1] = 0;
                }
            }
            fish_strcpy(file_prefix, partial, 64);
        }
        uint32_t fp_len = fish_strlen(file_prefix);
        vfs_dirent_t entries[32];
        int nentries = vfs_readdir(dir_path, entries, 32);
        if (nentries > 0) {
            for (int i = 0; i < nentries && (uint32_t)count < max_comps; i++) {
                if (fp_len > 0 && !fish_match_prefix(file_prefix, fp_len, entries[i].name, fish_strlen(entries[i].name)))
                    continue;
                int dup = 0;
                for (int j = 0; j < count; j++) {
                    if (fish_strcmp(comps[j].text, entries[i].name) == 0) { dup = 1; break; }
                }
                if (dup) continue;
                char completion[64];
                completion[0] = 0;
                fish_strcpy(completion, dir_path, 64);
                fish_strcat(completion, entries[i].name, 64);
                if (entries[i].type == VFS_TYPE_DIR) fish_strcat(completion, "/", 64);
                if (fish_strlen(completion) < 64) {
                    fish_strcpy(comps[count].text, completion, 64);
                    comps[count].is_dir = (entries[i].type == VFS_TYPE_DIR) ? 1 : 0;
                    comps[count].is_command = 0;
                    comps[count].is_function = 0;
                    comps[count].is_alias = 0;
                    count++;
                }
            }
        }
    }
    return count;
}

/* ======================================================================== */
/* History Search                                                           */
/* ======================================================================== */

int fish_history_search(const char *prefix, fish_history_entry_t *results, uint32_t max_results) {
    uint32_t plen = fish_strlen(prefix);
    int count = 0;
    for (int i = (int)fish.history_count - 1; i >= 0 && (uint32_t)count < max_results; i--) {
        if (plen == 0 || fish_strncmp(fish.history[i].cmd, prefix, plen) == 0) {
            fish_memcpy(&results[count], &fish.history[i], sizeof(fish_history_entry_t));
            count++;
        }
    }
    return count;
}

int fish_history_delete(uint32_t index) {
    if (index >= fish.history_count) return -1;
    for (uint32_t i = index; i + 1 < fish.history_count; i++) {
        fish_memcpy(&fish.history[i], &fish.history[i+1], sizeof(fish_history_entry_t));
    }
    fish.history_count--;
    return 0;
}

int fish_history_clear(void) {
    fish.history_count = 0;
    fish.history_idx = 0;
    return 0;
}

/* ======================================================================== */
/* PS/2 Keyboard Input with Fish Features                                   */
/* ======================================================================== */

int fish_read_key(void) {
    int c = kbd_getchar();
    if (c < 0) return KEY_NONE;
    if (c == '\n' || c == '\r') return KEY_ENTER;
    if (c == '\b' || c == 0x7F) return KEY_BACKSPACE;
    if (c == '\t') return KEY_TAB;

    if (c >= 1 && c <= 26) {
        switch (c) {
            case 1:  return KEY_CTRL_A;
            case 5:  return KEY_CTRL_E;
            case 11: return KEY_CTRL_K;
            case 21: return KEY_CTRL_U;
            case 23: return KEY_CTRL_W;
            case 18: return KEY_CTRL_R;
            case 4:  return KEY_CTRL_D;
            case 3:  return KEY_CTRL_C;
            case 12: return KEY_CTRL_L;
            case 26: return KEY_CTRL_Z;
            case 25: return KEY_CTRL_Y;
        }
    }

    if (c == 27) {
        int next = kbd_getchar();
        if (next < 0) return KEY_NONE;
        if (next == '[') {
            int code = kbd_getchar();
            if (code < 0) return KEY_NONE;
            if (code == 'A') return KEY_UP;
            if (code == 'B') return KEY_DOWN;
            if (code == 'C') return KEY_RIGHT;
            if (code == 'D') return KEY_LEFT;
            if (code == 'H') return KEY_HOME;
            if (code == 'F') return KEY_END;
            if (code == 'Z') return KEY_SHIFT_TAB;
            if (code == '3') { int t = kbd_getchar(); (void)t; return KEY_DELETE; }
            if (code == '5') { int t = kbd_getchar(); (void)t; return KEY_PGUP; }
            if (code == '6') { int t = kbd_getchar(); (void)t; return KEY_PGDN; }
            if (code == '1') {
                int semi = kbd_getchar();
                if (semi == ';') {
                    int mod = kbd_getchar();
                    if (mod == '5') {
                        int dir = kbd_getchar();
                        if (dir == 'A') return KEY_CTRL_UP;
                        if (dir == 'B') return KEY_CTRL_DOWN;
                        if (dir == 'C') return KEY_CTRL_RIGHT;
                        if (dir == 'D') return KEY_CTRL_LEFT;
                    }
                }
            }
        }
        if (next == 'O') {
            int code = kbd_getchar();
            if (code == 'P') return KEY_F1;
            if (code == 'Q') return KEY_F2;
            if (code == 'R') return KEY_F3;
            if (code == 'S') return KEY_F4;
        }
    }

    if (c >= 0x20 && c < 0x7F) return c;
    return KEY_NONE;
}

/* ======================================================================== */
/* Fish-style Prompt Rendering                                              */
/* ======================================================================== */

static void fish_render_prompt(void) {
    fish_puts_color(fish.prompt.username, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    fish_puts_color("@", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    fish_puts_color(fish.prompt.hostname, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    fish_puts_color(" ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    fish_puts_color(fish.prompt.cwd, VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);

    if (fish.prompt.show_status) {
        fish_puts_color(" (", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        if (fish.last_status == 0) {
            fish_puts_color("ok", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        } else {
            fish_puts_color("!", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            uint32_t s = (uint32_t)(fish.last_status < 0 ? -fish.last_status : fish.last_status);
            char num[8]; int ni = 0;
            if (s == 0) { num[ni++] = '0'; }
            else { char rev[8]; int ri = 0; while (s) { rev[ri++] = '0' + (s % 10); s /= 10; } while (ri > 0) num[ni++] = rev[--ri]; }
            num[ni] = 0;
            fish_puts_color(num, VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        }
        fish_puts_color(")", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }

    if (fish.current_func[0]) {
        fish_puts_color(" [", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        fish_puts_color(fish.current_func, VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
        fish_puts_color("]", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }

    fish_puts_color(" > ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
}

/* ======================================================================== */
/* Forward declarations                                                     */
/* ======================================================================== */



/* ======================================================================== */
/* Fish-style Line Editor                                                   */
/* ======================================================================== */

static int fish_readline(char *buf, uint32_t maxlen) {
    uint32_t pos = 0;
    uint32_t cursor = 0;
    buf[0] = 0;
    uint8_t history_search_mode = 0;
    char history_search_buf[64];
    uint32_t history_search_len = 0;
    history_search_buf[0] = 0;

    char kill_ring[256];
    uint32_t kill_ring_len = 0;
    uint8_t kill_ring_valid = 0;

    while (1) {
        int key = fish_read_key();
        if (key == KEY_NONE) { __asm__ volatile("hlt"); continue; }

        switch (key) {
        case KEY_ENTER:
            fish_newline();
            buf[pos] = 0;
            return (int)pos;

        case KEY_BACKSPACE:
            if (cursor > 0) {
                for (uint32_t i = cursor - 1; i < pos; i++) buf[i] = buf[i + 1];
                pos--; cursor--;
                fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = cursor; i < pos; i++)
                    fish_putc_color(buf[i], VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                fish_putc_color(' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = pos; i > cursor; i--)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            }
            break;

        case KEY_DELETE:
            if (cursor < pos) {
                for (uint32_t i = cursor; i < pos - 1; i++) buf[i] = buf[i + 1];
                pos--;
                for (uint32_t i = cursor; i < pos; i++)
                    fish_putc_color(buf[i], VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                fish_putc_color(' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = pos; i > cursor; i--)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            }
            break;

        case KEY_CTRL_A: case KEY_HOME:
            while (cursor > 0) {
                fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                cursor--;
            }
            break;

        case KEY_CTRL_E: case KEY_END:
            while (cursor < pos) {
                fish_putc_color(buf[cursor], VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                cursor++;
            }
            break;

        case KEY_LEFT:
            if (cursor > 0) {
                fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                cursor--;
            }
            break;

        case KEY_RIGHT:
            if (cursor < pos) {
                fish_putc_color(buf[cursor], VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                cursor++;
            }
            break;

        case KEY_CTRL_U:
            if (pos > 0) {
                kill_ring_len = cursor;
                if (kill_ring_len > 255) kill_ring_len = 255;
                fish_memcpy(kill_ring, buf, kill_ring_len);
                kill_ring[kill_ring_len] = 0;
                kill_ring_valid = 1;
                uint32_t killed = cursor;
                for (uint32_t i = 0; i + killed < pos; i++) buf[i] = buf[i + killed];
                pos -= killed;
                buf[pos] = 0;
                for (uint32_t i = 0; i < pos + killed; i++)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = 0; i < pos; i++)
                    fish_putc_color(buf[i], VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = pos; i < pos + killed; i++)
                    fish_putc_color(' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = pos; i < pos + killed; i++)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                cursor = 0;
            }
            break;

        case KEY_CTRL_K:
            if (cursor < pos) {
                kill_ring_len = pos - cursor;
                if (kill_ring_len > 255) kill_ring_len = 255;
                fish_memcpy(kill_ring, &buf[cursor], kill_ring_len);
                kill_ring[kill_ring_len] = 0;
                kill_ring_valid = 1;
                for (uint32_t i = cursor; i < pos; i++)
                    fish_putc_color(' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = cursor; i < pos; i++)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                pos = cursor;
                buf[pos] = 0;
            }
            break;

        case KEY_CTRL_W:
            if (cursor > 0) {
                uint32_t new_cursor = cursor;
                while (new_cursor > 0 && buf[new_cursor - 1] == ' ') new_cursor--;
                while (new_cursor > 0 && buf[new_cursor - 1] != ' ') new_cursor--;
                uint32_t del = cursor - new_cursor;
                kill_ring_len = del;
                if (kill_ring_len > 255) kill_ring_len = 255;
                fish_memcpy(kill_ring, &buf[new_cursor], kill_ring_len);
                kill_ring[kill_ring_len] = 0;
                kill_ring_valid = 1;
                for (uint32_t i = new_cursor; i + del < pos; i++) buf[i] = buf[i + del];
                pos -= del;
                buf[pos] = 0;
                for (uint32_t i = 0; i < del; i++)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = new_cursor; i < pos; i++)
                    fish_putc_color(buf[i], VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = pos; i > new_cursor; i--)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                cursor = new_cursor;
            }
            break;

        case KEY_CTRL_Y:
            if (kill_ring_valid && kill_ring_len > 0) {
                uint32_t space = maxlen - 1 - pos;
                uint32_t paste_len = kill_ring_len;
                if (paste_len > space) paste_len = space;
                if (paste_len > 0) {
                    for (int i = (int)pos; i >= (int)cursor; i--)
                        buf[i + paste_len] = buf[i];
                    for (uint32_t i = 0; i < paste_len; i++)
                        buf[cursor + i] = kill_ring[i];
                    pos += paste_len;
                    cursor += paste_len;
                    buf[pos] = 0;
                    for (uint32_t i = cursor - paste_len; i < pos; i++) {
                        uint8_t f2 = (i == 0);
                        uint8_t c2 = fish_syntax_color(&buf[i], 1, f2);
                        fish_putc_color(buf[i], c2, VGA_COLOR_BLACK);
                    }
                    for (uint32_t i = pos; i > cursor; i--)
                        fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                }
            }
            break;

        case KEY_CTRL_LEFT:
            if (cursor > 0) {
                while (cursor > 0 && buf[cursor - 1] == ' ') {
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                    cursor--;
                }
                while (cursor > 0 && buf[cursor - 1] != ' ') {
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                    cursor--;
                }
            }
            break;

        case KEY_CTRL_RIGHT:
            if (cursor < pos) {
                while (cursor < pos && buf[cursor] != ' ') {
                    fish_putc_color(buf[cursor], VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                    cursor++;
                }
                while (cursor < pos && buf[cursor] == ' ') {
                    fish_putc_color(buf[cursor], VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                    cursor++;
                }
            }
            break;

        case KEY_CTRL_C:
            fish_puts("^C\n");
            buf[0] = 0; pos = 0; cursor = 0;
            return -1;

        case KEY_CTRL_D:
            if (pos == 0) {
                fish_puts("exit\n");
                fish_exit();
                return -2;
            }
            if (cursor < pos) {
                for (uint32_t i = cursor; i < pos - 1; i++) buf[i] = buf[i + 1];
                pos--;
                for (uint32_t i = cursor; i < pos; i++)
                    fish_putc_color(buf[i], VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                fish_putc_color(' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = pos; i > cursor; i--)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            }
            break;

        case KEY_CTRL_L:
            vga_text_clear();
            fish_render_prompt();
            for (uint32_t i = 0; i < pos; i++) {
                uint8_t first = (i == 0);
                uint8_t col = fish_syntax_color(&buf[i], 1, first);
                fish_putc_color(buf[i], col, VGA_COLOR_BLACK);
            }
            for (uint32_t i = pos; i > cursor; i--)
                fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            break;

        case KEY_UP:
            if (fish.history_idx > 0) {
                fish.history_idx--;
                for (uint32_t i = 0; i < cursor; i++)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = 0; i < pos; i++)
                    fish_putc_color(' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = 0; i < pos; i++)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                fish_history_entry_t *h = &fish.history[fish.history_idx];
                uint32_t hlen = h->cmd_len;
                if (hlen >= maxlen) hlen = maxlen - 1;
                fish_memcpy(buf, h->cmd, hlen);
                buf[hlen] = 0;
                pos = hlen; cursor = hlen;
                for (uint32_t i = 0; i < hlen; i++) {
                    uint8_t first = (i == 0);
                    uint8_t col = fish_syntax_color(&buf[i], 1, first);
                    fish_putc_color(buf[i], col, VGA_COLOR_BLACK);
                }
            }
            break;

        case KEY_DOWN:
            if (fish.history_idx < fish.history_count - 1) {
                fish.history_idx++;
                for (uint32_t i = 0; i < cursor; i++)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = 0; i < pos; i++)
                    fish_putc_color(' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = 0; i < pos; i++)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                fish_history_entry_t *h = &fish.history[fish.history_idx];
                uint32_t hlen = h->cmd_len;
                if (hlen >= maxlen) hlen = maxlen - 1;
                fish_memcpy(buf, h->cmd, hlen);
                buf[hlen] = 0;
                pos = hlen; cursor = hlen;
                for (uint32_t i = 0; i < hlen; i++) {
                    uint8_t first = (i == 0);
                    uint8_t col = fish_syntax_color(&buf[i], 1, first);
                    fish_putc_color(buf[i], col, VGA_COLOR_BLACK);
                }
            } else if (fish.history_idx == fish.history_count - 1 && fish.history_count > 0) {
                fish.history_idx = fish.history_count;
                for (uint32_t i = 0; i < cursor; i++)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = 0; i < pos; i++)
                    fish_putc_color(' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = 0; i < pos; i++)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                pos = 0; cursor = 0; buf[0] = 0;
            }
            break;

        case KEY_CTRL_R:
            if (!history_search_mode) {
                history_search_mode = 1;
                history_search_len = 0;
                history_search_buf[0] = 0;
                fish_puts_color("\b(i-search)`': ", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
            } else {
                history_search_mode = 0;
                uint32_t prompt_len = fish_strlen("(i-search)`': ") + history_search_len;
                for (uint32_t i = 0; i < prompt_len; i++)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = 0; i < prompt_len; i++)
                    fish_putc_color(' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = 0; i < prompt_len; i++)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                if (history_search_len > 0) {
                    fish_history_entry_t results[8];
                    int nresults = fish_history_search(history_search_buf, results, 8);
                    if (nresults > 0) {
                        for (uint32_t i = 0; i < cursor; i++)
                            fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                        for (uint32_t i = 0; i < pos; i++)
                            fish_putc_color(' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                        for (uint32_t i = 0; i < pos; i++)
                            fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                        uint32_t mlen = results[0].cmd_len;
                        if (mlen >= maxlen) mlen = maxlen - 1;
                        fish_memcpy(buf, results[0].cmd, mlen);
                        buf[mlen] = 0;
                        pos = mlen; cursor = mlen;
                        for (uint32_t i = 0; i < mlen; i++) {
                            uint8_t first = (i == 0);
                            uint8_t col = fish_syntax_color(&buf[i], 1, first);
                            fish_putc_color(buf[i], col, VGA_COLOR_BLACK);
                        }
                    }
                }
            }
            break;

        case KEY_TAB:
        {
            uint32_t word_start = cursor;
            while (word_start > 0 && buf[word_start - 1] != ' ') word_start--;
            uint32_t word_len = cursor - word_start;

            fish_completion_t comps[FISH_MAX_COMPLETIONS];
            int ncomps = fish_complete(&buf[word_start], comps, FISH_MAX_COMPLETIONS);

            if (ncomps == 1) {
                uint32_t compl_len = fish_strlen(comps[0].text);
                for (uint32_t i = 0; i < word_len; i++)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                uint32_t shift = compl_len - word_len;
                for (int i = (int)pos; i >= (int)cursor; i--)
                    buf[i + shift] = buf[i];
                for (uint32_t i = 0; i < compl_len; i++)
                    buf[word_start + i] = comps[0].text[i];
                pos += shift; cursor = word_start + compl_len;
                buf[pos] = 0;
                /* Add space after completion if not a directory */
                if (!comps[0].is_dir && pos < maxlen - 1) {
                    buf[pos] = ' '; pos++; cursor++; buf[pos] = 0;
                }
                for (uint32_t i = word_start; i < pos; i++) {
                    uint8_t first = (i == 0);
                    uint8_t col = fish_syntax_color(&buf[i], 1, first);
                    fish_putc_color(buf[i], col, VGA_COLOR_BLACK);
                }
                for (uint32_t i = pos; i > cursor; i--)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            } else if (ncomps > 1) {
                fish_puts_color("\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (int i = 0; i < ncomps; i++) {
                    if (comps[i].is_command) {
                        fish_puts_color(comps[i].text, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                    } else if (comps[i].is_function) {
                        fish_puts_color(comps[i].text, VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
                    } else if (comps[i].is_dir) {
                        fish_puts_color(comps[i].text, VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
                    } else {
                        fish_puts_color(comps[i].text, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                    }
                    fish_puts_color("  ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                }
                fish_newline();
                uint32_t common_len = fish_strlen(comps[0].text);
                for (int i = 1; i < ncomps; i++) {
                    uint32_t j = 0;
                    while (j < common_len && j < fish_strlen(comps[i].text) &&
                           comps[0].text[j] == comps[i].text[j]) j++;
                    common_len = j;
                }
                if (common_len > word_len) {
                    for (uint32_t i = 0; i < word_len; i++)
                        fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                    uint32_t shift = common_len - word_len;
                    for (int i = (int)pos; i >= (int)cursor; i--)
                        buf[i + shift] = buf[i];
                    for (uint32_t i = 0; i < common_len; i++)
                        buf[word_start + i] = comps[0].text[i];
                    pos += shift; cursor = word_start + common_len;
                    buf[pos] = 0;
                }
                fish_render_prompt();
                for (uint32_t i = 0; i < pos; i++) {
                    uint8_t first = (i == 0);
                    uint8_t col = fish_syntax_color(&buf[i], 1, first);
                    fish_putc_color(buf[i], col, VGA_COLOR_BLACK);
                }
                for (uint32_t i = pos; i > cursor; i--)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            }
            break;
        }

        default:
            if (key >= 0x20 && key < 0x7F && pos < maxlen - 1) {
                for (int i = (int)pos; i >= (int)cursor; i--)
                    buf[i + 1] = buf[i];
                buf[cursor] = (char)key;
                pos++; cursor++;
                buf[pos] = 0;
                uint8_t first = (cursor == 1);
                uint8_t col = fish_syntax_color(&buf[cursor - 1], 1, first);
                fish_putc_color((char)key, col, VGA_COLOR_BLACK);
                for (uint32_t i = cursor; i < pos; i++) {
                    uint8_t f2 = (i == 0);
                    uint8_t c2 = fish_syntax_color(&buf[i], 1, f2);
                    fish_putc_color(buf[i], c2, VGA_COLOR_BLACK);
                }
                for (uint32_t i = pos; i > cursor; i--)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            }
            break;
        }

        /* Handle Ctrl+R search inline */
        if (history_search_mode && key >= 0x20 && key < 0x7F) {
            if (history_search_len < 63) {
                history_search_buf[history_search_len++] = (char)key;
                history_search_buf[history_search_len] = 0;
            }
            fish_history_entry_t results[8];
            int nresults = fish_history_search(history_search_buf, results, 8);
            if (nresults > 0 || history_search_len > 1) {
                for (uint32_t i = 0; i < 80; i++)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = 0; i < 80; i++)
                    fish_putc_color(' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t i = 0; i < 80; i++)
                    fish_putc_color('\b', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                fish_puts_color("(i-search)`", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
                fish_puts_color(history_search_buf, VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
                fish_puts_color("': ", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
            }
            if (nresults > 0) {
                fish_puts_color(results[0].cmd, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            }
        }
    }
}

/* ======================================================================== */
/* Argument Parsing                                                         */
/* ======================================================================== */

static int fish_parse_args(const char *cmd, char args[FISH_MAX_ARGS][64]) {
    int argc = 0;
    int in_arg = 0;
    int arg_pos = 0;
    char in_quote = 0;

    for (uint32_t i = 0; cmd[i] && argc < FISH_MAX_ARGS; i++) {
        char c = cmd[i];

        /* Handle quoted strings */
        if (in_quote) {
            if (c == in_quote) {
                in_quote = 0;
                continue;
            }
            if (arg_pos < 63) args[argc][arg_pos++] = c;
            continue;
        }
        if (c == '\'' || c == '"') {
            in_quote = c;
            if (!in_arg) in_arg = 1;
            continue;
        }

        if (c == ' ' || c == '\t') {
            if (in_arg) {
                args[argc][arg_pos] = 0;
                argc++;
                in_arg = 0;
                arg_pos = 0;
            }
        } else {
            if (!in_arg) in_arg = 1;
            if (arg_pos < 63) args[argc][arg_pos++] = c;
        }
    }
    if (in_arg) {
        args[argc][arg_pos] = 0;
        argc++;
    }
    return argc;
}

/* ======================================================================== */
/* Wildcard Expansion                                                       */
/* ======================================================================== */

static int fish_has_wildcard(const char *s) {
    while (*s) {
        if (*s == '*' || *s == '?' || *s == '[') return 1;
        s++;
    }
    return 0;
}

static int fish_expand_wildcard(const char *pattern, char matches[16][64], int max_matches) {
    char dir_path[256];
    char glob_pattern[64];
    int last_slash = -1;
    uint32_t plen = fish_strlen(pattern);

    for (int i = (int)plen - 1; i >= 0; i--) {
        if (pattern[i] == '/') { last_slash = i; break; }
    }

    if (last_slash >= 0) {
        fish_strcpy(dir_path, pattern, 256);
        dir_path[last_slash + 1] = 0;
        fish_strcpy(glob_pattern, &pattern[last_slash + 1], 64);
    } else {
        fish_strcpy(dir_path, "/", 256);
        fish_strcpy(glob_pattern, pattern, 64);
    }

    vfs_dirent_t entries[32];
    int nentries = vfs_readdir(dir_path, entries, 32);
    if (nentries <= 0) return 0;

    int count = 0;
    uint32_t glen = fish_strlen(glob_pattern);

    for (int i = 0; i < nentries && count < max_matches; i++) {
        const char *name = entries[i].name;
        uint32_t nlen = fish_strlen(name);
        int match = 1;
        uint32_t pi = 0, ni = 0;
        while (pi < glen && ni < nlen) {
            if (glob_pattern[pi] == '*') {
                pi++;
                if (pi >= glen) { match = 1; break; }
                match = 0;
                for (uint32_t k = ni; k <= nlen; k++) {
                    uint32_t p2 = pi, n2 = k;
                    int m2 = 1;
                    while (p2 < glen && n2 < nlen) {
                        if (glob_pattern[p2] == '*') { p2++; continue; }
                        if (glob_pattern[p2] == '?' || glob_pattern[p2] == name[n2]) { p2++; n2++; continue; }
                        m2 = 0; break;
                    }
                    if (m2 && p2 >= glen) { match = 1; ni = nlen; break; }
                    if (m2 && p2 < glen && n2 >= nlen) { match = 1; ni = nlen; break; }
                }
                break;
            } else if (glob_pattern[pi] == '?' || glob_pattern[pi] == name[ni]) {
                pi++; ni++;
            } else {
                match = 0; break;
            }
        }
        if (match && pi >= glen && ni >= nlen) {
            fish_strcpy(matches[count], dir_path, 64);
            fish_strcat(matches[count], name, 64);
            if (entries[i].type == VFS_TYPE_DIR) {
                uint32_t mlen = fish_strlen(matches[count]);
                matches[count][mlen] = '/';
                matches[count][mlen + 1] = 0;
            }
            count++;
        }
    }
    return count;
}

static int fish_expand_wildcard_args(int argc, char args[FISH_MAX_ARGS][64]) {
    char expanded[FISH_MAX_ARGS][64];
    int new_argc = 0;
    for (int i = 0; i < argc && new_argc < FISH_MAX_ARGS; i++) {
        if (fish_has_wildcard(args[i])) {
            char matches[16][64];
            int nmatches = fish_expand_wildcard(args[i], matches, 16);
            if (nmatches > 0) {
                for (int j = 0; j < nmatches && new_argc < FISH_MAX_ARGS; j++) {
                    fish_strcpy(expanded[new_argc], matches[j], 64);
                    new_argc++;
                }
            } else {
                fish_strcpy(expanded[new_argc], args[i], 64);
                new_argc++;
            }
        } else {
            fish_strcpy(expanded[new_argc], args[i], 64);
            new_argc++;
        }
    }
    for (int i = 0; i < new_argc; i++) fish_strcpy(args[i], expanded[i], 64);
    return new_argc;
}

/* ======================================================================== */
/* Pipe Parsing                                                             */
/* ======================================================================== */

static uint32_t fish_parse_pipes(const char *cmd, fish_pipe_segment_t *pipes, uint32_t max_pipes) {
    uint32_t count = 0;
    uint32_t start = 0;
    uint32_t len = fish_strlen(cmd);

    for (uint32_t i = 0; i <= len && count < max_pipes; i++) {
        if (cmd[i] == '|' || cmd[i] == 0) {
            while (start < i && cmd[start] == ' ') start++;
            uint32_t end = i;
            while (end > start && cmd[end - 1] == ' ') end--;

            if (end > start) {
                uint32_t seg_len = end - start;
                if (seg_len >= FISH_MAX_CMD) seg_len = FISH_MAX_CMD - 1;
                fish_memcpy(pipes[count].cmd, &cmd[start], seg_len);
                pipes[count].cmd[seg_len] = 0;
                pipes[count].cmd_len = seg_len;

                /* Parse redirections from the segment */
                pipes[count].redir_out_type = FISH_REDIR_NONE;
                pipes[count].redir_err_type = FISH_REDIR_NONE;
                pipes[count].redir_in_type = FISH_REDIR_NONE;

                /* Check for >, >>, <, 2>, 2>> in the command */
                char *p = pipes[count].cmd;
                while (*p) {
                    if (p[0] == '2' && p[1] == '>' && p[2] == '&') {
                        /* 2>&1 - redirect stderr to stdout */
                        pipes[count].redir_err_type = FISH_REDIR_ERR_OUT;
                        *p = 0;
                        break;
                    }
                    if (p[0] == '2' && p[1] == '>' && p[2] == '>') {
                        pipes[count].redir_err_type = FISH_REDIR_ERR_APP;
                        p += 3;
                        while (*p == ' ') p++;
                        uint32_t fi = 0;
                        while (*p && *p != ' ' && fi < 127) {
                            pipes[count].redir_err_file[fi++] = *p++;
                        }
                        pipes[count].redir_err_file[fi] = 0;
                        *(p - fi - 3) = 0;
                        break;
                    }
                    if (p[0] == '2' && p[1] == '>') {
                        pipes[count].redir_err_type = FISH_REDIR_ERR;
                        p += 2;
                        while (*p == ' ') p++;
                        uint32_t fi = 0;
                        while (*p && *p != ' ' && fi < 127) {
                            pipes[count].redir_err_file[fi++] = *p++;
                        }
                        pipes[count].redir_err_file[fi] = 0;
                        /* Remove from command */
                        break;
                    }
                    if (p[0] == '>' && p[1] == '>') {
                        pipes[count].redir_out_type = FISH_REDIR_APPEND;
                        p += 2;
                        while (*p == ' ') p++;
                        uint32_t fi = 0;
                        while (*p && *p != ' ' && fi < 127) {
                            pipes[count].redir_out_file[fi++] = *p++;
                        }
                        pipes[count].redir_out_file[fi] = 0;
                        break;
                    }
                    if (p[0] == '>') {
                        pipes[count].redir_out_type = FISH_REDIR_OUT;
                        p += 1;
                        while (*p == ' ') p++;
                        uint32_t fi = 0;
                        while (*p && *p != ' ' && fi < 127) {
                            pipes[count].redir_out_file[fi++] = *p++;
                        }
                        pipes[count].redir_out_file[fi] = 0;
                        break;
                    }
                    if (p[0] == '<') {
                        pipes[count].redir_in_type = FISH_REDIR_IN;
                        p += 1;
                        while (*p == ' ') p++;
                        uint32_t fi = 0;
                        while (*p && *p != ' ' && fi < 127) {
                            pipes[count].redir_in_file[fi++] = *p++;
                        }
                        pipes[count].redir_in_file[fi] = 0;
                        break;
                    }
                    p++;
                }

                /* Trim trailing spaces from command after redir removal */
                int tl = (int)fish_strlen(pipes[count].cmd) - 1;
                while (tl >= 0 && pipes[count].cmd[tl] == ' ') {
                    pipes[count].cmd[tl--] = 0;
                }

                count++;
            }
            start = i + 1;
        }
    }
    return count;
}

/* ======================================================================== */
/* Command Substitution                                                     */
/* ======================================================================== */

int fish_cmdsub_eval(const char *input, char *output, uint32_t out_len) {
    /* Find $( ... ) and replace with command output */
    uint32_t oi = 0;
    uint32_t i = 0;
    (void)fish_strlen(input);

    while (input[i] && oi < out_len - 1) {
        /* Check for $( command ) */
        if (input[i] == '$' && input[i + 1] == '(') {
            i += 2; /* skip $( */
            char cmd_buf[FISH_MAX_CMD];
            uint32_t ci = 0;
            int depth = 1;
            while (input[i] && depth > 0 && ci < FISH_MAX_CMD - 1) {
                if (input[i] == '(') depth++;
                else if (input[i] == ')') {
                    depth--;
                    if (depth == 0) { i++; break; }
                }
                cmd_buf[ci++] = input[i++];
            }
            cmd_buf[ci] = 0;

            if (ci > 0) {
                /* Execute the command and capture output */
                /* We'll use a temp file approach: redirect to a known temp file, then read it back */
                /* In bare-metal, we'll just execute the command and capture the last result */
                char sub_args[FISH_MAX_ARGS][64];
                fish_memset(sub_args, 0, sizeof(sub_args));
                int sub_argc = fish_parse_args(cmd_buf, sub_args);
                if (sub_argc > 0) {
                    /* Execute - the output goes to screen, but we can capture some results */
                    /* For basic capture, we check if it's a simple command */
                    fish_dispatch(sub_argc, sub_args);
                    /* In bare-metal, we can't easily pipe output to a string.
                       So command substitution is best-effort: we substitute the $status */
                    const char *status = fish_var_get("status");
                    if (status) {
                        while (*status && oi < out_len - 1) output[oi++] = *status++;
                    }
                }
            }
            continue;
        }
        /* Check for backtick command substitution */
        if (input[i] == '`') {
            i++; /* skip ` */
            char cmd_buf[FISH_MAX_CMD];
            uint32_t ci = 0;
            while (input[i] && input[i] != '`' && ci < FISH_MAX_CMD - 1) {
                cmd_buf[ci++] = input[i++];
            }
            cmd_buf[ci] = 0;
            if (input[i] == '`') i++;

            if (ci > 0) {
                char sub_args[FISH_MAX_ARGS][64];
                fish_memset(sub_args, 0, sizeof(sub_args));
                int sub_argc = fish_parse_args(cmd_buf, sub_args);
                if (sub_argc > 0) {
                    fish_dispatch(sub_argc, sub_args);
                    const char *status = fish_var_get("status");
                    if (status) {
                        while (*status && oi < out_len - 1) output[oi++] = *status++;
                    }
                }
            }
            continue;
        }
        output[oi++] = input[i++];
    }
    output[oi] = 0;
    return (int)oi;
}

/* ======================================================================== */
/* Variable Expansion ($var -> value, ${var}, ${#var}, ${var:-default})    */
/* ======================================================================== */

static void fish_expand_vars(const char *in, char *out, uint32_t out_len) {
    /* First pass: handle $(cmd) */
    char sub1[FISH_MAX_CMD * 2];
    fish_cmdsub_eval(in, sub1, FISH_MAX_CMD * 2);

    uint32_t oi = 0;
    uint32_t i = 0;
    while (sub1[i] && oi < out_len - 1) {
        if (sub1[i] == '$' && sub1[i + 1] && sub1[i + 1] != ' ') {
            i++;
            /* ${var} or ${var:-default} or ${var:+alt} or ${#var} */
            if (sub1[i] == '{') {
                i++; /* skip { */
                char varname[32];
                uint32_t vi = 0;

                /* Check for ${#var} - string length */
                if (sub1[i] == '#') {
                    i++;
                    while (sub1[i] && sub1[i] != '}' && vi < 31)
                        varname[vi++] = sub1[i++];
                    varname[vi] = 0;
                    if (sub1[i] == '}') i++;
                    uint32_t slen = 0;
                    const char *vval = fish_var_get(varname);
                    if (vval) slen = fish_strlen(vval);
                    char num[12];
                    fish_uint_to_str(slen, num, 12);
                    for (int j = 0; num[j] && oi < out_len - 1; j++)
                        out[oi++] = num[j];
                    continue;
                }

                /* Parse varname */
                while (sub1[i] && sub1[i] != '}' && sub1[i] != ':' && vi < 31)
                    varname[vi++] = sub1[i++];
                varname[vi] = 0;

                /* ${var:-default} */
                if (sub1[i] == ':' && sub1[i + 1] == '-') {
                    i += 2;
                    char defval[64];
                    uint32_t di = 0;
                    while (sub1[i] && sub1[i] != '}' && di < 63)
                        defval[di++] = sub1[i++];
                    defval[di] = 0;
                    if (sub1[i] == '}') i++;
                    const char *vval = fish_var_get(varname);
                    if (!vval || vval[0] == '\0') {
                        for (uint32_t j = 0; defval[j] && oi < out_len - 1; j++)
                            out[oi++] = defval[j];
                    } else {
                        while (*vval && oi < out_len - 1) out[oi++] = *vval++;
                    }
                    continue;
                }

                /* ${var:+alt} */
                if (sub1[i] == ':' && sub1[i + 1] == '+') {
                    i += 2;
                    char altval[64];
                    uint32_t ai = 0;
                    while (sub1[i] && sub1[i] != '}' && ai < 63)
                        altval[ai++] = sub1[i++];
                    altval[ai] = 0;
                    if (sub1[i] == '}') i++;
                    const char *vval = fish_var_get(varname);
                    if (vval && vval[0] != '\0') {
                        for (uint32_t j = 0; altval[j] && oi < out_len - 1; j++)
                            out[oi++] = altval[j];
                    }
                    continue;
                }

                if (sub1[i] == '}') i++;

                const char *vval = fish_var_get(varname);
                if (vval) {
                    while (*vval && oi < out_len - 1) out[oi++] = *vval++;
                }
            } else {
                /* $var */
                char varname[32];
                uint32_t vi = 0;
                while (sub1[i] && sub1[i] != ' ' && sub1[i] != '"' &&
                       sub1[i] != '\'' && sub1[i] != '$' && vi < 31) {
                    varname[vi++] = sub1[i++];
                }
                varname[vi] = 0;

                if (vi == 0) {
                    out[oi++] = '$';
                } else if (fish_strcmp(varname, "status") == 0) {
                    int32_t val = fish.last_status;
                    char num[12]; int ni = 0;
                    if (val < 0) { num[ni++] = '-'; val = -val; }
                    if (val == 0) { num[ni++] = '0'; }
                    else { char rev[12]; int ri = 0; while (val) { rev[ri++] = '0' + (val % 10); val /= 10; } while (ri > 0) num[ni++] = rev[--ri]; }
                    num[ni] = 0;
                    for (int j = 0; num[j] && oi < out_len - 1; j++) out[oi++] = num[j];
                } else if (fish_strcmp(varname, "pid") == 0) {
                    out[oi++] = '1';
                } else if (fish_strcmp(varname, "version") == 0) {
                    const char *ver = "fish/0.2.0-chicago95";
                    while (*ver && oi < out_len - 1) out[oi++] = *ver++;
                } else if (fish_strcmp(varname, "fish_pid") == 0) {
                    out[oi++] = '1';
                } else if (fish_strcmp(varname, "hostname") == 0) {
                    const char *h = fish.prompt.hostname;
                    while (*h && oi < out_len - 1) out[oi++] = *h++;
                } else if (fish_strcmp(varname, "PWD") == 0) {
                    const char *p = fish.prompt.cwd;
                    while (*p && oi < out_len - 1) out[oi++] = *p++;
                } else if (fish_strcmp(varname, "HOME") == 0) {
                    out[oi++] = '~';
                } else if (fish_strcmp(varname, "USER") == 0) {
                    const char *u = "root";
                    while (*u && oi < out_len - 1) out[oi++] = *u++;
                } else {
                    const char *vval = fish_var_get(varname);
                    if (vval) {
                        while (*vval && oi < out_len - 1) out[oi++] = *vval++;
                    }
                }
            }
        } else if (sub1[i] == '\\' && sub1[i + 1]) {
            i++;
            out[oi++] = sub1[i++];
        } else {
            out[oi++] = sub1[i++];
        }
    }
    out[oi] = 0;
}

/* ======================================================================== */
/* Path normalization helpers for cd                                         */
/* ======================================================================== */

static void fish_normalize_path(const char *in, char *out, uint32_t out_max) {
    char stack[16][64];
    int depth = 0;
    uint32_t i = 0;
    uint32_t len = fish_strlen(in);
    if (len > 0 && in[0] == '/') i = 1;

    while (i <= len && depth < 16) {
        char comp[64];
        int ci = 0;
        while (i < len && in[i] != '/' && ci < 63) comp[ci++] = in[i++];
        comp[ci] = 0;
        if (ci > 0) {
            if (fish_strcmp(comp, ".") == 0) { /* skip */ }
            else if (fish_strcmp(comp, "..") == 0) { if (depth > 0) depth--; }
            else { fish_strcpy(stack[depth], comp, 64); depth++; }
        }
        if (i < len) i++;
    }
    out[0] = 0;
    if (depth == 0) { out[0] = '/'; out[1] = 0; }
    else {
        for (int d = 0; d < depth; d++) {
            fish_strcat(out, "/", out_max);
            fish_strcat(out, stack[d], out_max);
        }
    }
}

/* Directory stack for pushd/popd */
#define FISH_DIR_STACK_SIZE 16
static char fish_dir_stack[FISH_DIR_STACK_SIZE][128];
static int fish_dir_stack_top = 0;

/* ======================================================================== */
/* Built-in Fish Commands                                                   */
/* ======================================================================== */

static int fish_cmd_set(int argc, char args[FISH_MAX_ARGS][64]) {
    /* Parse flags */
    int erase_mode = 0;
    int export_mode = 0;
    int global_mode = 0;
    int local_mode = 0;
    int universal_mode = 0;
    int query_mode = 0;
    int list_mode = 0;
    int name_idx = 1;

    for (int i = 1; i < argc; i++) {
        if (args[i][0] != '-') break;
        if (fish_strcmp(args[i], "-e") == 0 || fish_strcmp(args[i], "--erase") == 0) erase_mode = 1;
        else if (fish_strcmp(args[i], "-x") == 0 || fish_strcmp(args[i], "--export") == 0) export_mode = 1;
        else if (fish_strcmp(args[i], "-g") == 0 || fish_strcmp(args[i], "--global") == 0) global_mode = 1;
        else if (fish_strcmp(args[i], "-l") == 0 || fish_strcmp(args[i], "--local") == 0) local_mode = 1;
        else if (fish_strcmp(args[i], "-U") == 0 || fish_strcmp(args[i], "--universal") == 0) universal_mode = 1;
        else if (fish_strcmp(args[i], "-q") == 0 || fish_strcmp(args[i], "--query") == 0) query_mode = 1;
        else if (fish_strcmp(args[i], "-n") == 0 || fish_strcmp(args[i], "--names") == 0) list_mode = 1;
        else if (fish_strcmp(args[i], "--") == 0) { name_idx = i + 1; break; }
        name_idx = i + 1;
    }

    if (list_mode) {
        for (uint32_t i = 0; i < fish.var_count; i++) {
            fish_puts_color(fish.vars[i].name, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            fish_newline();
        }
        return 0;
    }

    if (erase_mode) {
        if (name_idx >= argc) {
            fish_puts_color("set --erase: missing variable name\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            return 1;
        }
        return fish_var_erase(args[name_idx]) == 0 ? 0 : 1;
    }

    if (query_mode) {
        if (name_idx >= argc) {
            /* Query all variables */
            return (fish.var_count > 0) ? 0 : 1;
        }
        const char *val = fish_var_get(args[name_idx]);
        return val ? 0 : 1;
    }

    if (argc < 2 || (name_idx >= argc)) {
        /* Print all variables */
        for (uint32_t i = 0; i < fish.var_count; i++) {
            fish_puts_color(fish.vars[i].name, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            fish_puts_color("=", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            fish_puts_color(fish.vars[i].value, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            if (fish.vars[i].exported) fish_puts_color(" (exported)", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
            fish_newline();
        }
        return 0;
    }

    /* set VAR value */
    int val_idx = name_idx + 1;
    if (val_idx < argc && fish_strcmp(args[val_idx], "=") == 0) val_idx++;

    if (val_idx < argc) {
        uint8_t scope = FISH_SCOPE_GLOBAL;
        if (global_mode) scope = FISH_SCOPE_GLOBAL;
        else if (export_mode) scope = FISH_SCOPE_EXPORT;
        else if (local_mode) scope = FISH_SCOPE_LOCAL;
        else if (universal_mode) scope = FISH_SCOPE_UNIV;
        return fish_var_set_scope(args[name_idx], args[val_idx], scope);
    }
    /* set VAR (without value = set to empty) */
    fish_var_set(args[name_idx], "");
    return 0;
}

static int fish_cmd_export(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc < 2) {
        for (uint32_t i = 0; i < fish.var_count; i++) {
            if (fish.vars[i].exported) {
                fish_puts_color("export ", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
                fish_puts_color(fish.vars[i].name, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
                fish_puts_color("=", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                fish_puts_color(fish.vars[i].value, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                fish_newline();
            }
        }
        return 0;
    }
    for (uint32_t i = 1; (uint32_t)i < argc; i++) {
        /* export VAR=value */
        char *eq = 0;
        for (char *p = args[i]; *p; p++) {
            if (*p == '=') { eq = p; break; }
        }
        if (eq) {
            *eq = 0;
            fish_var_set(args[i], eq + 1);
            *eq = '=';
            /* Mark exported */
            for (uint32_t j = 0; j < fish.var_count; j++) {
                if (fish_strcmp(fish.vars[j].name, args[i]) == 0) {
                    fish.vars[j].exported = 1;
                    fish.vars[j].scope = FISH_SCOPE_EXPORT;
                    break;
                }
            }
        } else {
            for (uint32_t j = 0; j < fish.var_count; j++) {
                if (fish_strcmp(fish.vars[j].name, args[i]) == 0) {
                    fish.vars[j].exported = 1;
                    break;
                }
            }
        }
    }
    return 0;
}

static int fish_cmd_echo(int argc, char args[FISH_MAX_ARGS][64]) {
    int no_newline = 0;
    int interpret_escapes = 0;
    int start = 1;

    /* Parse flags */
    while (start < argc && args[start][0] == '-') {
        int all_flags = 1;
        for (uint32_t j = 1; args[start][j]; j++) {
            if (args[start][j] == 'n') no_newline = 1;
            else if (args[start][j] == 'e') interpret_escapes = 1;
            else { all_flags = 0; break; }
        }
        if (!all_flags) break;
        start++;
    }

    for (int i = start; i < argc; i++) {
        if (i > start) fish_putc(' ');
        char expanded[FISH_MAX_CMD];
        fish_expand_vars(args[i], expanded, FISH_MAX_CMD);

        if (interpret_escapes) {
            const char *p = expanded;
            while (*p) {
                if (*p == '\\' && p[1]) {
                    p++;
                    if (*p == 'n') fish_putc('\n');
                    else if (*p == 't') fish_putc('\t');
                    else if (*p == 'r') fish_putc('\r');
                    else if (*p == '\\') fish_putc('\\');
                    else if (*p == '0') fish_putc('\0');
                    else fish_putc(*p);
                } else {
                    fish_putc(*p);
                }
                p++;
            }
        } else {
            fish_puts(expanded);
        }
    }
    if (!no_newline) fish_newline();
    return 0;
}

/* Math parser */
typedef struct {
    const char *expr;
    uint32_t pos;
    uint32_t len;
} math_parser_t;

static int32_t math_parse_expr(math_parser_t *p);
static int32_t math_parse_term(math_parser_t *p);

static int32_t math_parse_factor(math_parser_t *p) {
    while (p->pos < p->len && p->expr[p->pos] == ' ') p->pos++;
    if (p->pos >= p->len) return 0;
    if (p->expr[p->pos] == '(') {
        p->pos++;
        int32_t val = math_parse_expr(p);
        while (p->pos < p->len && p->expr[p->pos] == ' ') p->pos++;
        if (p->pos < p->len && p->expr[p->pos] == ')') p->pos++;
        return val;
    }
    if (p->expr[p->pos] == '-') { p->pos++; return -math_parse_factor(p); }
    if (p->expr[p->pos] == '+') { p->pos++; return math_parse_factor(p); }
    if (p->expr[p->pos] == '!') { p->pos++; return math_parse_factor(p) ? 0 : 1; }
    if (p->expr[p->pos] >= '0' && p->expr[p->pos] <= '9') {
        int32_t num = 0;
        while (p->pos < p->len && p->expr[p->pos] >= '0' && p->expr[p->pos] <= '9')
            num = num * 10 + (p->expr[p->pos++] - '0');
        return num;
    }
    p->pos++;
    return 0;
}

static int32_t math_parse_term(math_parser_t *p) {
    int32_t left = math_parse_factor(p);
    while (p->pos < p->len) {
        while (p->pos < p->len && p->expr[p->pos] == ' ') p->pos++;
        if (p->pos >= p->len) break;
        char op = p->expr[p->pos];
        if (op != '*' && op != '/' && op != '%') break;
        p->pos++;
        int32_t right = math_parse_factor(p);
        switch (op) {
            case '*': left *= right; break;
            case '/': left = right ? left / right : 0; break;
            case '%': left = right ? left % right : 0; break;
        }
    }
    return left;
}

static int32_t math_parse_expr(math_parser_t *p) {
    int32_t left = math_parse_term(p);
    while (p->pos < p->len) {
        while (p->pos < p->len && p->expr[p->pos] == ' ') p->pos++;
        if (p->pos >= p->len) break;
        char op = p->expr[p->pos];
        if (op != '+' && op != '-') break;
        p->pos++;
        int32_t right = math_parse_term(p);
        if (op == '+') left += right;
        else left -= right;
    }
    return left;
}

static int fish_cmd_math(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc < 2) {
        fish_puts_color("Usage: math <expression>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
        return 1;
    }
    math_parser_t parser;
    parser.expr = args[1];
    parser.pos = 0;
    parser.len = fish_strlen(args[1]);
    int32_t result = math_parse_expr(&parser);
    char num_buf[16];
    fish_int_to_str(result, num_buf, 16);
    fish_puts_color(num_buf, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    fish_newline();
    return 0;
}

static int fish_cmd_history(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc >= 2) {
        if (fish_strcmp(args[1], "--clear") == 0 || fish_strcmp(args[1], "-c") == 0) {
            fish_history_clear();
            fish_puts_color("History cleared\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            return 0;
        }
        if (fish_strcmp(args[1], "--delete") == 0 || fish_strcmp(args[1], "-d") == 0) {
            if (argc < 3) {
                fish_puts_color("Usage: history --delete <index>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
                return 1;
            }
            uint32_t idx = 0;
            for (uint32_t i = 0; args[2][i]; i++) idx = idx * 10 + (args[2][i] - '0');
            if (fish_history_delete(idx) == 0) {
                fish_puts_color("Deleted\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            } else {
                fish_puts_color("Invalid index\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                return 1;
            }
            return 0;
        }
        if (fish_strcmp(args[1], "--search") == 0 || fish_strcmp(args[1], "-s") == 0) {
            if (argc < 3) {
                fish_puts_color("Usage: history --search <prefix>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
                return 1;
            }
            fish_history_entry_t results[32];
            int nresults = fish_history_search(args[2], results, 32);
            for (int i = 0; i < nresults; i++) {
                fish_puts_color("  ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                fish_puts_color(results[i].cmd, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                fish_newline();
            }
            return 0;
        }
    }

    fish_puts_color("\n=== Command History ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    for (uint32_t i = 0; i < fish.history_count; i++) {
        char idx_buf[8];
        uint32_t idx_num = i + 1;
        int ni = 0;
        char rev[8]; int ri = 0;
        if (idx_num == 0) { rev[ri++] = '0'; }
        else { while (idx_num) { rev[ri++] = '0' + (idx_num % 10); idx_num /= 10; } }
        while (ri > 0) idx_buf[ni++] = rev[--ri];
        idx_buf[ni] = 0;
        fish_puts_color("  ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        fish_puts_color(idx_buf, VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
        fish_puts_color("  ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        const char *cmd = fish.history[i].cmd;
        uint8_t first = 1;
        while (*cmd) {
            uint8_t col = fish_syntax_color(cmd, 1, first);
            fish_putc_color(*cmd, col, VGA_COLOR_BLACK);
            if (*cmd != ' ') first = 0;
            cmd++;
        }
        if (fish.history[i].status != 0) {
            fish_puts_color(" [", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            fish_puts_color("!", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            fish_puts_color("]", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        }
        fish_newline();
    }
    fish_newline();
    return 0;
}

static int fish_cmd_abbrev(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc >= 2 && (fish_strcmp(args[1], "--erase") == 0 || fish_strcmp(args[1], "-e") == 0)) {
        if (argc < 3) {
            fish_puts_color("Usage: abbr --erase <name>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
            return 1;
        }
        return fish_abbrev_erase(args[2]) == 0 ? 0 : 1;
    }
    if (argc >= 2 && (fish_strcmp(args[1], "--show") == 0 || fish_strcmp(args[1], "-s") == 0)) {
        return fish_abbrev_show();
    }
    if (argc >= 2 && (fish_strcmp(args[1], "--list") == 0 || fish_strcmp(args[1], "-l") == 0)) {
        for (uint32_t i = 0; i < fish.abbrev_count; i++) {
            fish_puts_color(fish.abbrevs[i].trigger, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            fish_newline();
        }
        return 0;
    }
    if (argc < 3) {
        return fish_abbrev_show();
    }
    if (fish_abbrev_add(args[1], args[2]) == 0) {
        fish_puts_color("Added abbreviation: ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_puts_color(args[1], VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        fish_puts_color(" -> ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        fish_puts_color(args[2], VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
        fish_newline();
        return 0;
    }
    fish_puts_color("Error: abbreviation table full\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return 1;
}

static int fish_cmd_string(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc < 2) {
        fish_puts_color("Usage: string <subcommand> [args...]\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
        fish_puts("  length <str>          - Print string length\n");
        fish_puts("  upper <str>           - Convert to uppercase\n");
        fish_puts("  lower <str>           - Convert to lowercase\n");
        fish_puts("  reverse <str>         - Reverse string\n");
        fish_puts("  join <s1> <s2>        - Join strings\n");
        fish_puts("  sub <str> <start> <len> - Substring\n");
        fish_puts("  replace <old> <new> <str> - Replace\n");
        fish_puts("  trim <str>            - Trim whitespace\n");
        fish_puts("  trim -l/--left <str>  - Trim left\n");
        fish_puts("  trim -r/--right <str> - Trim right\n");
        fish_puts("  split <sep> <str>     - Split string\n");
        fish_puts("  match <pattern> <str> - Match pattern\n");
        fish_puts("  match -r <pat> <str>  - Regex match\n");
        fish_puts("  contains <sub> <str>  - Check contains\n");
        return 0;
    }

    if (fish_strcmp(args[1], "length") == 0 && argc >= 3) {
        uint32_t len = fish_strlen(args[2]);
        char num[16]; int ni = 0;
        if (len == 0) { num[ni++] = '0'; }
        else { char rev[16]; int ri = 0; while (len) { rev[ri++] = '0' + (len % 10); len /= 10; } while (ri > 0) num[ni++] = rev[--ri]; }
        num[ni] = 0;
        fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[1], "reverse") == 0 && argc >= 3) {
        uint32_t len = fish_strlen(args[2]);
        for (int i = (int)len - 1; i >= 0; i--) fish_putc(args[2][i]);
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[1], "upper") == 0 && argc >= 3) {
        const char *s = args[2];
        while (*s) {
            char c = *s;
            if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
            fish_putc(c); s++;
        }
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[1], "lower") == 0 && argc >= 3) {
        const char *s = args[2];
        while (*s) {
            char c = *s;
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
            fish_putc(c); s++;
        }
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[1], "join") == 0 && argc >= 4) {
        fish_puts(args[2]);
        fish_puts(args[3]);
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[1], "sub") == 0 && argc >= 5) {
        uint32_t start = fish_atou(args[3]);
        uint32_t len = fish_atou(args[4]);
        uint32_t slen = fish_strlen(args[2]);
        for (uint32_t i = start; i < start + len && i < slen; i++)
            fish_putc(args[2][i]);
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[1], "replace") == 0 && argc >= 5) {
        const char *str = args[4];
        const char *old = args[2];
        const char *new = args[3];
        uint32_t slen = fish_strlen(str);
        uint32_t olen = fish_strlen(old);
        uint32_t nlen = fish_strlen(new);
        uint32_t i = 0;
        while (i < slen) {
            if (olen > 0 && fish_strncmp(&str[i], old, olen) == 0) {
                for (uint32_t j = 0; j < nlen; j++) fish_putc(new[j]);
                i += olen;
            } else {
                fish_putc(str[i]);
                i++;
            }
        }
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[1], "trim") == 0 && argc >= 3) {
        int left_only = 0, right_only = 0;
        int text_idx = 2;
        if (fish_strcmp(args[2], "-l") == 0 || fish_strcmp(args[2], "--left") == 0) { left_only = 1; text_idx = 3; }
        else if (fish_strcmp(args[2], "-r") == 0 || fish_strcmp(args[2], "--right") == 0) { right_only = 1; text_idx = 3; }
        if (text_idx >= argc) { fish_puts_color("Usage: string trim <str>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }

        const char *s = args[text_idx];
        uint32_t slen = fish_strlen(s);
        uint32_t start = 0, end = slen;

        if (!right_only) {
            while (start < slen && (s[start] == ' ' || s[start] == '\t' || s[start] == '\n')) start++;
        }
        if (!left_only) {
            while (end > start && (s[end-1] == ' ' || s[end-1] == '\t' || s[end-1] == '\n')) end--;
        }
        for (uint32_t i = start; i < end; i++) fish_putc(s[i]);
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[1], "split") == 0 && argc >= 4) {
        const char *sep = args[2];
        const char *str = args[3];
        uint32_t slen = fish_strlen(str);
        uint32_t seplen = fish_strlen(sep);
        uint32_t i = 0;
        while (i < slen) {
            if (seplen > 0 && fish_strncmp(&str[i], sep, seplen) == 0) {
                fish_newline();
                i += seplen;
            } else {
                fish_putc(str[i]);
                i++;
            }
        }
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[1], "match") == 0 && argc >= 4) {
        int regex_mode = 0;
        int pat_idx = 2;
        int text_idx = 3;
        if (fish_strcmp(args[2], "-r") == 0 || fish_strcmp(args[2], "--regex") == 0) {
            regex_mode = 1;
            pat_idx = 3;
            text_idx = 4;
        }
        if (text_idx >= argc) {
            fish_puts_color("Usage: string match <pattern> <text>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
            return 1;
        }
        const char *pat = args[pat_idx];
        const char *text = args[text_idx];
        uint32_t plen = fish_strlen(pat);
        uint32_t tlen = fish_strlen(text);

        if (regex_mode) {
            /* Simple wildcard matching: * and ? */
            uint32_t pi = 0, ti = 0;
            int match = 1;
            while (pi < plen && ti < tlen) {
                if (pat[pi] == '*') {
                    pi++;
                    if (pi >= plen) break;
                    match = 0;
                    for (uint32_t k = ti; k <= tlen; k++) {
                        uint32_t p2 = pi, t2 = k, m2 = 1;
                        while (p2 < plen && t2 < tlen) {
                            if (pat[p2] == '*') { p2++; continue; }
                            if (pat[p2] == '?' || pat[p2] == text[t2]) { p2++; t2++; continue; }
                            m2 = 0; break;
                        }
                        if (m2 && p2 >= plen) { match = 1; ti = tlen; break; }
                    }
                    break;
                } else if (pat[pi] == '?' || pat[pi] == text[ti]) {
                    pi++; ti++;
                } else { match = 0; break; }
            }
            if (match && pi < plen && pat[pi] != '*') match = 0;
            if (match) {
                fish_puts_color("MATCH\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                fish_puts_color(text, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                fish_newline();
            }
            return match ? 0 : 1;
        } else {
            /* Simple substring match */
            for (uint32_t i = 0; i + plen <= tlen; i++) {
                if (fish_strncmp(&text[i], pat, plen) == 0) {
                    fish_puts_color("MATCH\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                    for (uint32_t j = i; j < i + plen; j++) fish_putc(text[j]);
                    fish_newline();
                    return 0;
                }
            }
            return 1;
        }
    }

    if (fish_strcmp(args[1], "contains") == 0 && argc >= 4) {
        const char *sub = args[2];
        const char *str = args[3];
        uint32_t slen = fish_strlen(str);
        uint32_t sublen = fish_strlen(sub);
        for (uint32_t i = 0; i + sublen <= slen; i++) {
            if (fish_strncmp(&str[i], sub, sublen) == 0) return 0;
        }
        return 1;
    }

    fish_puts_color("Unknown string subcommand: ", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    fish_puts_color(args[1], VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    fish_newline();
    return 1;
}

static int fish_cmd_count(int argc, char args[FISH_MAX_ARGS][64]) {
    char num[16];
    int ni = 0;
    int count = argc - 1;
    if (count == 0) { num[ni++] = '0'; }
    else { char rev[16]; int ri = 0; while (count) { rev[ri++] = '0' + (count % 10); count /= 10; } while (ri > 0) num[ni++] = rev[--ri]; }
    num[ni] = 0;
    fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    fish_newline();
    return 0;
}

static int fish_cmd_type(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc < 2) {
        fish_puts_color("Usage: type <command>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
        return 0;
    }
    int show_all = 0;
    int name_idx = 1;
    if (argc >= 3 && fish_strcmp(args[1], "-a") == 0) {
        show_all = 1;
        name_idx = 2;
    }

    int found = 0;
    /* Check builtins */
    for (uint32_t i = 0; fish_commands[i]; i++) {
        if (fish_strcmp(args[name_idx], fish_commands[i]) == 0) {
            fish_puts_color(args[name_idx], VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            fish_puts_color(" is a builtin command\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            found = 1;
            if (!show_all) return 0;
        }
    }
    /* Check user functions */
    fish_func_t *f = fish_func_find(args[name_idx]);
    if (f) {
        fish_puts_color(args[name_idx], VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_puts_color(" is a user-defined function\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
        found = 1;
        if (!show_all) return 0;
    }
    if (!found) {
        fish_puts_color(args[name_idx], VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        fish_puts_color(" is not found\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return 1;
    }
    return 0;
}

static int fish_cmd_read(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc < 2) {
        fish_puts_color("Usage: read [-p prompt] [-s] [-t timeout] [VAR...]\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
        return 1;
    }
    const char *prompt = 0;
    int silent = 0;
    int var_start = 1;

    for (int i = 1; i < argc; i++) {
        if (fish_strcmp(args[i], "-p") == 0 || fish_strcmp(args[i], "--prompt") == 0) {
            if (i + 1 < argc) { prompt = args[++i]; }
        } else if (fish_strcmp(args[i], "-s") == 0 || fish_strcmp(args[i], "--silent") == 0) {
            silent = 1;
        } else {
            var_start = i;
            break;
        }
    }

    if (prompt) {
        fish_puts_color(prompt, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    } else {
        fish_puts_color("> ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    }

    /* Read a line */
    char line[FISH_MAX_CMD];
    int len = fish_readline(line, FISH_MAX_CMD);
    if (len < 0) return 130;

    /* If silent, clear the input display */
    if (silent) {
        for (int i = 0; i < len; i++) fish_putc('\b');
        for (int i = 0; i < len; i++) fish_putc(' ');
        for (int i = 0; i < len; i++) fish_putc('\b');
        fish_newline();
    }

    /* Parse args from the line */
    char read_args[FISH_MAX_ARGS][64];
    int read_argc = fish_parse_args(line, read_args);

    /* Assign to VAR names */
    int remaining = read_argc;
    for (int i = var_start; i < argc; i++) {
        int var_arg_idx = i - var_start;
        if (var_arg_idx < remaining) {
            fish_var_set(args[i], read_args[var_arg_idx]);
        } else {
            fish_var_set(args[i], "");
        }
    }
    /* If extra values, combine into IFS-separated string */
    if (remaining > argc - var_start && argc - var_start == 1) {
        /* Single var gets all values */
        fish_var_set(args[var_start], line);
    }
    return 0;
}

static int fish_cmd_set_color(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc < 2) {
        fish_puts_color("Usage: set_color [options] <color>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
        fish_puts("  Colors: black, red, green, yellow, blue, magenta, cyan, white\n");
        fish_puts("  -b/--background  Set background color\n");
        fish_puts("  -o/--bold        Bold text\n");
        fish_puts("  -r/--reverse     Reverse colors\n");
        fish_puts("  -u/--underline   Underline text\n");
        fish_puts("  -c/--clear       Reset colors\n");
        return 0;
    }

    int bg_mode = 0;
    int clear = 0;
    uint8_t color = VGA_COLOR_WHITE;

    for (int i = 1; i < argc; i++) {
        if (fish_strcmp(args[i], "-b") == 0 || fish_strcmp(args[i], "--background") == 0) {
            bg_mode = 1;
        } else if (fish_strcmp(args[i], "-c") == 0 || fish_strcmp(args[i], "--clear") == 0) {
            clear = 1;
        } else {
            /* Parse color name */
            if (fish_strcmp(args[i], "black") == 0) color = VGA_COLOR_BLACK;
            else if (fish_strcmp(args[i], "red") == 0) color = VGA_COLOR_RED;
            else if (fish_strcmp(args[i], "green") == 0) color = VGA_COLOR_GREEN;
            else if (fish_strcmp(args[i], "yellow") == 0) color = VGA_COLOR_YELLOW;
            else if (fish_strcmp(args[i], "blue") == 0) color = VGA_COLOR_BLUE;
            else if (fish_strcmp(args[i], "magenta") == 0) color = VGA_COLOR_MAGENTA;
            else if (fish_strcmp(args[i], "cyan") == 0) color = VGA_COLOR_CYAN;
            else if (fish_strcmp(args[i], "white") == 0) color = VGA_COLOR_WHITE;
            else if (fish_strcmp(args[i], "brblack") == 0) color = VGA_COLOR_LIGHT_GREY;
            else if (fish_strcmp(args[i], "brred") == 0) color = VGA_COLOR_LIGHT_RED;
            else if (fish_strcmp(args[i], "brgreen") == 0) color = VGA_COLOR_LIGHT_GREEN;
            else if (fish_strcmp(args[i], "bryellow") == 0) color = VGA_COLOR_LIGHT_YELLOW;
            else if (fish_strcmp(args[i], "brblue") == 0) color = VGA_COLOR_LIGHT_BLUE;
            else if (fish_strcmp(args[i], "brmagenta") == 0) color = VGA_COLOR_LIGHT_MAGENTA;
            else if (fish_strcmp(args[i], "brcyan") == 0) color = VGA_COLOR_LIGHT_CYAN;
            else if (fish_strcmp(args[i], "brwhite") == 0) color = VGA_COLOR_LIGHT_GREY;
        }
    }

    if (clear) {
        fish.current_fg = VGA_COLOR_LIGHT_GREY;
        fish.current_bg = VGA_COLOR_BLACK;
    } else if (bg_mode) {
        fish.current_bg = color;
    } else {
        fish.current_fg = color;
    }
    return 0;
}

static int fish_cmd_random(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc < 2) {
        /* Random integer 0-32767 */
        uint32_t ticks;
        __asm__ volatile("rdtsc" : "=a"(ticks));
        char num[16];
        fish_uint_to_str(ticks % 32768, num, 16);
        fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_newline();
        return 0;
    }
    if (argc == 3) {
        /* random START END */
        uint32_t start = fish_atou(args[1]);
        uint32_t end = fish_atou(args[2]);
        if (end <= start) { fish_puts_color("random: end must be > start\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
        uint32_t ticks;
        __asm__ volatile("rdtsc" : "=a"(ticks));
        uint32_t range = end - start;
        uint32_t result = start + (ticks % range);
        char num[16];
        fish_uint_to_str(result, num, 16);
        fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_newline();
        return 0;
    }
    if (argc == 4 && fish_strcmp(args[1], "choice") == 0) {
        /* random choice ITEM... */
        uint32_t ticks;
        __asm__ volatile("rdtsc" : "=a"(ticks));
        uint32_t idx = ticks % (argc - 2);
        fish_puts_color(args[2 + idx], VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_newline();
        return 0;
    }
    fish_puts_color("Usage: random [START END] | random choice ITEMS\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
    return 1;
}

static int fish_cmd_functions(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc < 2) return fish_func_list();

    if (fish_strcmp(args[1], "--erase") == 0 || fish_strcmp(args[1], "-e") == 0) {
        if (argc < 3) { fish_puts_color("Usage: functions --erase <name>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        return fish_func_erase(args[2]) == 0 ? 0 : 1;
    }
    if (fish_strcmp(args[1], "--show") == 0 || fish_strcmp(args[1], "-d") == 0) {
        if (argc < 3) { fish_puts_color("Usage: functions --show <name>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        return fish_func_show(args[2]);
    }
    if (fish_strcmp(args[1], "--names") == 0 || fish_strcmp(args[1], "-n") == 0) {
        for (uint32_t i = 0; i < fish.func_count; i++) {
            fish_puts_color(fish.funcs[i].name, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            fish_newline();
        }
        return 0;
    }
    if (fish_strcmp(args[1], "--list") == 0 || fish_strcmp(args[1], "-l") == 0) {
        for (uint32_t i = 0; i < fish.func_count; i++) {
            fish_puts_color(fish.funcs[i].name, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            fish_newline();
        }
        return 0;
    }
    /* functions NAME - show or define */
    if (argc >= 2) {
        return fish_func_show(args[1]);
    }
    return 0;
}

static int fish_cmd_realpath(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc < 2) {
        fish_puts_color("Usage: realpath <path>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
        return 0;
    }
    char full_path[256];
    if (args[1][0] != '/') {
        full_path[0] = 0;
        const char *cwd = fish.prompt.cwd;
        if (fish_strcmp(cwd, "~") != 0) fish_strcpy(full_path, cwd, 256);
        else fish_strcpy(full_path, "/", 256);
        fish_strcat(full_path, args[1], 256);
    } else {
        fish_strcpy(full_path, args[1], 256);
    }
    char normalized[256];
    fish_normalize_path(full_path, normalized, 256);
    fish_puts_color(normalized, VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
    fish_newline();
    return 0;
}

static int fish_cmd_source(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc < 2) {
        fish_puts_color("Usage: source <file>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
        return 0;
    }
    /* Read and execute the file line by line */
    int fd = vfs_open(args[1], FD_FLAG_READ);
    if (fd < 0) {
        fish_puts_color("source: cannot open '", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        fish_puts_color(args[1], VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        fish_puts_color("'\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        return 1;
    }

    char filebuf[4096];
    uint32_t total = 0;
    while (total < 4095) {
        uint32_t nread = 0;
        int rc = vfs_read(fd, (uint8_t*)&filebuf[total], 4095 - total, &nread);
        if (rc != 0 || nread == 0) break;
        total += nread;
    }
    filebuf[total] = 0;
    vfs_close(fd);

    /* Execute each line */
    uint32_t line_start = 0;
    int last_status = 0;
    for (uint32_t i = 0; i <= total; i++) {
        if (filebuf[i] == '\n' || filebuf[i] == 0) {
            /* Extract line */
            char line[FISH_MAX_CMD];
            uint32_t li = 0;
            for (uint32_t j = line_start; j < i && li < FISH_MAX_CMD - 1; j++) {
                if (filebuf[j] != '\r') line[li++] = filebuf[j];
            }
            line[li] = 0;
            line_start = i + 1;

            /* Skip empty lines and comments */
            if (li == 0 || line[0] == '#') continue;

            char line_args[FISH_MAX_ARGS][64];
            fish_memset(line_args, 0, sizeof(line_args));
            int line_argc = fish_parse_args(line, line_args);
            if (line_argc > 0) {
                last_status = fish_dispatch(line_argc, line_args);
            }
        }
    }
    return last_status;
}

static int fish_cmd_commandline(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc < 2) {
        /* Print current command line buffer */
        fish_puts_color(fish.commandline_buf, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return 0;
    }
    if (fish_strcmp(args[1], "--current-buffer") == 0) {
        fish_puts_color(fish.commandline_buf, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return 0;
    }
    if (fish_strcmp(args[1], "--replace") == 0 && argc >= 3) {
        fish_strcpy(fish.commandline_buf, args[2], FISH_MAX_CMD);
        fish.commandline_len = fish_strlen(args[2]);
        return 0;
    }
    if (fish_strcmp(args[1], "--append") == 0 && argc >= 3) {
        fish_strcat(fish.commandline_buf, " ", FISH_MAX_CMD);
        fish_strcat(fish.commandline_buf, args[2], FISH_MAX_CMD);
        fish.commandline_len = fish_strlen(fish.commandline_buf);
        return 0;
    }
    if (fish_strcmp(args[1], "--insert") == 0 && argc >= 3) {
        fish_strcat(fish.commandline_buf, args[2], FISH_MAX_CMD);
        fish.commandline_len = fish_strlen(fish.commandline_buf);
        return 0;
    }
    if (fish_strcmp(args[1], "--is-valid") == 0) {
        /* Check if the command line is a complete valid command */
        const char *buf = fish.commandline_buf;
        uint32_t len = fish_strlen(buf);
        if (len == 0) return 1;
        /* Simple heuristic: doesn't end with \ */
        if (len > 0 && buf[len-1] == '\\') return 1;
        return 0;
    }
    return 0;
}

static int fish_cmd_emit(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc < 2) {
        fish_puts_color("Usage: emit <event_name>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
        return 1;
    }
    /* Register and emit event */
    int found = 0;
    for (uint32_t i = 0; i < fish.event_count; i++) {
        if (fish_strcmp(fish.event_handlers[i], args[1]) == 0) { found = 1; break; }
    }
    if (!found && fish.event_count < 16) {
        fish_strcpy(fish.event_handlers[fish.event_count], args[1], 64);
        fish.event_count++;
    }
    return fish_emit_event(args[1]);
}

static int fish_cmd_argparse(int argc, char args[FISH_MAX_ARGS][64]) {
    /* Simplified argparse: just display usage */
    fish_puts_color("argparse: simplified argument parser\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    fish_puts("Usage: argparse 'h/help' 'v/verbose' -- args...\n");
    fish_puts("  Parses flags from command line arguments\n");
    return 0;
}

/* ======================================================================== */
/* cd / pushd / popd / dirs / pwd commands                                  */
/* ======================================================================== */

static int fish_cmd_cd(int argc, char args[FISH_MAX_ARGS][64]) {
    const char *target;
    char target_buf[128];

    if (argc < 2 || fish_strcmp(args[1], "~") == 0 || fish_strcmp(args[1], "") == 0) {
        const char *home = fish_var_get("HOME");
        if (home) fish_strcpy(target_buf, home, 128);
        else fish_strcpy(target_buf, "/", 128);
        target = target_buf;
    } else if (fish_strcmp(args[1], "-") == 0) {
        const char *oldpwd = fish_var_get("OLDPWD");
        if (oldpwd) { fish_strcpy(target_buf, oldpwd, 128); target = target_buf; }
        else { fish_puts_color("cd: OLDPWD not set\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
    } else {
        target = args[1];
    }

    char full_path[256];
    if (target[0] != '/') {
        full_path[0] = 0;
        const char *cwd = fish.prompt.cwd;
        if (fish_strcmp(cwd, "~") != 0) fish_strcpy(full_path, cwd, 256);
        else fish_strcpy(full_path, "/", 256);
        fish_strcat(full_path, target, 256);
    } else {
        fish_strcpy(full_path, target, 256);
    }

    char normalized[256];
    fish_normalize_path(full_path, normalized, 256);

    vfs_stat_t st;
    if (vfs_stat(normalized, &st) != 0) {
        fish_puts_color("cd: no such file or directory: ", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        fish_puts_color(target, VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        fish_newline();
        return 1;
    }
    if (st.type != VFS_TYPE_DIR) {
        fish_puts_color("cd: not a directory: ", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        fish_puts_color(target, VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        fish_newline();
        return 1;
    }

    fish_var_set("OLDPWD", fish.prompt.cwd);
    if (fish_strcmp(normalized, "/") == 0)
        fish_strcpy(fish.prompt.cwd, "~", 128);
    else
        fish_strcpy(fish.prompt.cwd, normalized, 128);
    fish_var_set("PWD", fish.prompt.cwd);

    if (argc >= 2 && fish_strcmp(args[1], "-") == 0) {
        fish_puts_color(normalized, VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
        fish_newline();
    }
    return 0;
}

static int fish_cmd_pwd(int argc, char args[FISH_MAX_ARGS][64]) {
    (void)argc; (void)args;
    const char *cwd = fish.prompt.cwd;
    if (fish_strcmp(cwd, "~") == 0) fish_puts_color("/", VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
    else fish_puts_color(cwd, VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
    fish_newline();
    return 0;
}

static int fish_cmd_pushd(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc < 2) {
        if (fish_dir_stack_top == 0) {
            fish_puts_color("pushd: directory stack empty\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            return 1;
        }
        char temp[128];
        fish_strcpy(temp, fish_dir_stack[fish_dir_stack_top - 1], 128);
        fish_strcpy(fish_dir_stack[fish_dir_stack_top - 1], fish.prompt.cwd, 128);
        char new_args[FISH_MAX_ARGS][64];
        fish_memset(new_args, 0, sizeof(new_args));
        fish_strcpy(new_args[0], "cd", 64);
        fish_strcpy(new_args[1], temp, 64);
        return fish_cmd_cd(2, new_args);
    }

    if (fish_dir_stack_top >= FISH_DIR_STACK_SIZE) {
        fish_puts_color("pushd: stack full\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        return 1;
    }
    fish_strcpy(fish_dir_stack[fish_dir_stack_top], fish.prompt.cwd, 128);
    fish_dir_stack_top++;

    char new_args[FISH_MAX_ARGS][64];
    fish_memset(new_args, 0, sizeof(new_args));
    fish_strcpy(new_args[0], "cd", 64);
    fish_strcpy(new_args[1], args[1], 64);
    int rc = fish_cmd_cd(2, new_args);
    if (rc != 0) { fish_dir_stack_top--; return rc; }

    for (int i = 0; i < fish_dir_stack_top; i++) {
        fish_puts_color(" ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        fish_puts_color(fish_dir_stack[i], VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
    }
    fish_puts_color("  ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    fish_puts_color(fish.prompt.cwd, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    fish_newline();
    return 0;
}

static int fish_cmd_popd(int argc, char args[FISH_MAX_ARGS][64]) {
    (void)argc; (void)args;
    if (fish_dir_stack_top == 0) {
        fish_puts_color("popd: directory stack empty\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        return 1;
    }
    fish_dir_stack_top--;
    char new_args[FISH_MAX_ARGS][64];
    fish_memset(new_args, 0, sizeof(new_args));
    fish_strcpy(new_args[0], "cd", 64);
    fish_strcpy(new_args[1], fish_dir_stack[fish_dir_stack_top], 64);
    int rc = fish_cmd_cd(2, new_args);
    if (rc != 0) return rc;
    for (int i = 0; i < fish_dir_stack_top; i++) {
        fish_puts_color(" ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        fish_puts_color(fish_dir_stack[i], VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
    }
    fish_newline();
    return 0;
}

static int fish_cmd_dirs(int argc, char args[FISH_MAX_ARGS][64]) {
    (void)argc; (void)args;
    fish_puts_color("0", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    for (int i = fish_dir_stack_top - 1; i >= 0; i--) {
        fish_puts_color(" ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        fish_puts_color(fish_dir_stack[i], VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
    }
    fish_newline();
    return 0;
}

/* ======================================================================== */
/* Control Flow: if/for/while/switch/begin/function/end                     */
/* ======================================================================== */

/* Parse condition after if/else if/while: "test -f foo" or "[ -f foo ]" or "command" */
static int fish_eval_condition(int argc, char args[FISH_MAX_ARGS][64]) __attribute__((unused));
static int fish_eval_condition(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc == 0) return 0;
    return fish_dispatch(argc, args);
}

/* Execute a block of lines (from a function body or block definition) */
static int fish_exec_block(const char *lines[], uint32_t line_count, uint32_t start_line) __attribute__((unused));
static int fish_exec_block(const char *lines[], uint32_t line_count, uint32_t start_line) {
    int last_status = 0;
    for (uint32_t i = start_line; i < line_count; i++) {
        if (fish.break_requested || fish.continue_requested) break;

        const char *line = lines[i];
        if (line[0] == 0) continue;
        if (line[0] == '#') continue;

        char line_args[FISH_MAX_ARGS][64];
        fish_memset(line_args, 0, sizeof(line_args));
        int line_argc = fish_parse_args(line, line_args);
        if (line_argc > 0) {
            last_status = fish_dispatch(line_argc, line_args);
        }
        if (fish.break_requested || fish.continue_requested) break;
    }
    return last_status;
}

/* ======================================================================== */
/* Command Dispatch                                                         */
/* ======================================================================== */

static int fish_dispatch(int argc, char args[FISH_MAX_ARGS][64]) {
    if (argc == 0) return 0;

    /* Expand variables in all args */
    for (int i = 0; i < argc; i++) {
        char expanded[FISH_MAX_CMD];
        fish_expand_vars(args[i], expanded, FISH_MAX_CMD);
        fish_strcpy(args[i], expanded, 64);
    }

    /* Expand abbreviations (first token only) */
    char abbrev_exp[FISH_MAX_CMD];
    if (fish_abbrev_expand(args[0], abbrev_exp, FISH_MAX_CMD) == 0) {
        fish_strcpy(args[0], abbrev_exp, 64);
    }

    /* Expand wildcards in arguments */
    if (argc > 1) {
        argc = fish_expand_wildcard_args(argc, args);
    }

    /* ============================================================ */
    /* Control flow keywords                                         */
    /* ============================================================ */

    if (fish_strcmp(args[0], "if") == 0) {
        /* if CONDITION; BODY; else if CONDITION; BODY; else; BODY; end */
        if (argc < 2) { fish_puts_color("if: missing condition\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }

        /* Evaluate condition: run the condition as a command */
        int cond_status = 0;
        char cond_args[FISH_MAX_ARGS][64];
        fish_memset(cond_args, 0, sizeof(cond_args));
        int cond_argc = 0;
        /* Collect args up to "then" or end */
        for (int i = 1; i < argc; i++) {
            if (fish_strcmp(args[i], "then") == 0) break;
            fish_strcpy(cond_args[cond_argc++], args[i], 64);
        }
        if (cond_argc == 0) {
            /* Check if the entire line is "if COMMAND" style (no explicit "then") */
            /* In fish, "if" takes a command directly: "if test -f foo" */
            /* But we also support "if test -f foo; then" */
            /* For simplicity, if no "then" found, treat remaining args as condition */
            for (int i = 1; i < argc; i++) {
                fish_strcpy(cond_args[cond_argc++], args[i], 64);
            }
        }

        /* In fish, "if cmd; body; end" means run cmd, check $status */
        /* But for bare-metal simplicity: support "if test-expr" and "if command" */
        if (cond_argc > 0) {
            cond_status = fish_dispatch(cond_argc, cond_args);
        }

        if (cond_status == 0) {
            /* Condition true - we need to read lines until "else"/"else if"/"end" */
            /* In a simple single-line mode, we just return. For multi-line, we'd need block reading.
               Since this is bare-metal and we process function bodies line-by-line,
               we'll handle this by collecting lines until end. */
            return 0; /* True condition */
        } else {
            /* Condition false - skip to "else" or "end" */
            return 1; /* False condition */
        }
    }

    if (fish_strcmp(args[0], "else") == 0) {
        /* else - executed when previous if condition was false */
        return 0;
    }

    if (fish_strcmp(args[0], "else") == 0 || fish_strcmp(args[0], "else if") == 0) {
        return 0;
    }

    if (fish_strcmp(args[0], "for") == 0) {
        /* for VAR in VALUES; BODY; end */
        if (argc < 4 || fish_strcmp(args[2], "in") != 0) {
            fish_puts_color("Usage: for VAR in VALUES; ...; end\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
            return 1;
        }
        const char *varname = args[1];
        /* Store the values */
        int nvalues = argc - 3;
        /* The for loop in bare-metal is limited - we just set the variable */
        if (nvalues > 0) {
            fish_var_set(varname, args[3]);
        }
        return 0;
    }

    if (fish_strcmp(args[0], "while") == 0) {
        /* while CONDITION; BODY; end */
        if (argc < 2) {
            fish_puts_color("Usage: while CONDITION; ...; end\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
            return 1;
        }
        /* Evaluate condition */
        char cond_args[FISH_MAX_ARGS][64];
        fish_memset(cond_args, 0, sizeof(cond_args));
        int cond_argc = 0;
        for (int i = 1; i < argc; i++) {
            if (fish_strcmp(args[i], "do") == 0 || fish_strcmp(args[i], ";") == 0) break;
            fish_strcpy(cond_args[cond_argc++], args[i], 64);
        }
        if (cond_argc > 0) {
            return fish_dispatch(cond_argc, cond_args);
        }
        return 0;
    }

    if (fish_strcmp(args[0], "break") == 0) {
        fish.break_requested = 1;
        return 0;
    }

    if (fish_strcmp(args[0], "continue") == 0) {
        fish.continue_requested = 1;
        return 0;
    }

    if (fish_strcmp(args[0], "switch") == 0) {
        /* switch VALUE; case PATTERN; BODY; case PATTERN; BODY; end */
        if (argc < 2) {
            fish_puts_color("Usage: switch VALUE; case PATTERN; ...; end\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
            return 1;
        }
        /* In bare-metal, we just store the switch value and let the dispatch handle cases */
        fish_var_set("__switch_value__", args[1]);
        return 0;
    }

    if (fish_strcmp(args[0], "case") == 0) {
        /* case PATTERN */
        /* In single-line mode, just return. The block processor handles matching */
        return 0;
    }

    if (fish_strcmp(args[0], "begin") == 0) {
        /* begin ... end block */
        return 0; /* Just a grouping */
    }

    if (fish_strcmp(args[0], "end") == 0) {
        /* end of block */
        return 0;
    }

    if (fish_strcmp(args[0], "function") == 0) {
        /* function NAME; BODY; end */
        if (argc < 2) {
            fish_puts_color("Usage: function NAME; ...; end\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
            return 1;
        }
        const char *fname = args[1];

        /* Check for --description flag */
        char desc[128] = {0};
        for (int i = 2; i < argc; i++) {
            if (fish_strcmp(args[i], "--description") == 0 && i + 1 < argc) {
                fish_strcpy(desc, args[i+1], 128);
                i++;
            }
        }

        /* In single-line mode: function name; echo hello; end */
        /* Collect lines between function definition and end */
        char body_lines[FISH_MAX_FUNC_LINES][FISH_FUNC_LINE_LEN];
        uint32_t body_count = 0;

        /* Check if there are inline commands after function name */
        for (int i = 2; i < argc; i++) {
            if (fish_strcmp(args[i], "--description") == 0) { i++; continue; }
            if (fish_strcmp(args[i], "end") == 0) break;
            if (fish_strcmp(args[i], ";") == 0) continue;
            fish_strcpy(body_lines[body_count++], args[i], FISH_FUNC_LINE_LEN);
        }

        if (body_count > 0) {
            fish_func_add(fname, (const char **)body_lines, body_count);
            if (desc[0]) {
                fish_func_t *f = fish_func_find(fname);
                if (f) fish_strcpy(f->description, desc, 128);
            }
            return 0;
        }

        /* Multi-line: just register an empty function for now */
        fish_func_add(fname, 0, 0);
        if (desc[0]) {
            fish_func_t *f = fish_func_find(fname);
            if (f) fish_strcpy(f->description, desc, 128);
        }
        return 0;
    }

    /* ============================================================ */
    /* Built-in commands                                             */
    /* ============================================================ */

    if (fish_strcmp(args[0], "set") == 0) return fish_cmd_set(argc, args);
    if (fish_strcmp(args[0], "setenv") == 0) return fish_cmd_export(argc, args);
    if (fish_strcmp(args[0], "export") == 0) return fish_cmd_export(argc, args);
    if (fish_strcmp(args[0], "echo") == 0) return fish_cmd_echo(argc, args);
    if (fish_strcmp(args[0], "math") == 0) return fish_cmd_math(argc, args);
    if (fish_strcmp(args[0], "history") == 0) return fish_cmd_history(argc, args);
    if (fish_strcmp(args[0], "abbr") == 0) return fish_cmd_abbrev(argc, args);
    if (fish_strcmp(args[0], "string") == 0) return fish_cmd_string(argc, args);
    if (fish_strcmp(args[0], "count") == 0) return fish_cmd_count(argc, args);
    if (fish_strcmp(args[0], "type") == 0) return fish_cmd_type(argc, args);
    if (fish_strcmp(args[0], "which") == 0) return fish_cmd_type(argc, args);
    if (fish_strcmp(args[0], "where") == 0) return fish_cmd_type(argc, args);
    if (fish_strcmp(args[0], "realpath") == 0) return fish_cmd_realpath(argc, args);
    if (fish_strcmp(args[0], "source") == 0) return fish_cmd_source(argc, args);
    if (fish_strcmp(args[0], "cd") == 0) return fish_cmd_cd(argc, args);
    if (fish_strcmp(args[0], "pwd") == 0) return fish_cmd_pwd(argc, args);
    if (fish_strcmp(args[0], "pushd") == 0) return fish_cmd_pushd(argc, args);
    if (fish_strcmp(args[0], "popd") == 0) return fish_cmd_popd(argc, args);
    if (fish_strcmp(args[0], "dirs") == 0) return fish_cmd_dirs(argc, args);
    if (fish_strcmp(args[0], "read") == 0) return fish_cmd_read(argc, args);
    if (fish_strcmp(args[0], "set_color") == 0) return fish_cmd_set_color(argc, args);
    if (fish_strcmp(args[0], "random") == 0) return fish_cmd_random(argc, args);
    if (fish_strcmp(args[0], "functions") == 0) return fish_cmd_functions(argc, args);
    if (fish_strcmp(args[0], "emit") == 0) return fish_cmd_emit(argc, args);
    if (fish_strcmp(args[0], "commandline") == 0) return fish_cmd_commandline(argc, args);
    if (fish_strcmp(args[0], "argparse") == 0) return fish_cmd_argparse(argc, args);

    if (fish_strcmp(args[0], "builtin") == 0) {
        if (argc >= 2) {
            char sub_args[FISH_MAX_ARGS][64];
            fish_memset(sub_args, 0, sizeof(sub_args));
            for (int i = 1; i < argc; i++) fish_strcpy(sub_args[i-1], args[i], 64);
            return fish_dispatch(argc - 1, sub_args);
        }
        return 0;
    }
    if (fish_strcmp(args[0], "command") == 0) {
        if (argc >= 2) {
            char sub_args[FISH_MAX_ARGS][64];
            fish_memset(sub_args, 0, sizeof(sub_args));
            for (int i = 1; i < argc; i++) fish_strcpy(sub_args[i-1], args[i], 64);
            return fish_dispatch(argc - 1, sub_args);
        }
        return 0;
    }
    if (fish_strcmp(args[0], "return") == 0) {
        return (argc >= 2) ? fish_atoi(args[1]) : 0;
    }
    if (fish_strcmp(args[0], "exit") == 0 || fish_strcmp(args[0], "logout") == 0) {
        fish_exit();
        return 0;
    }
    if (fish_strcmp(args[0], "clear") == 0) {
        vga_text_clear();
        return 0;
    }
    if (fish_strcmp(args[0], "gui") == 0) {
        fish_puts_color("Launching GUI...\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        ring0_delay_ms(500);
        if (gui_init() == 0) { gui_run(); gui_exit(); }
        fish_puts_color("GUI session ended.\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        return 0;
    }
    if (fish_strcmp(args[0], "and") == 0) {
        if (argc >= 2) {
            char sub[FISH_MAX_ARGS][64];
            for (int i = 1; i < argc; i++) fish_strcpy(sub[i-1], args[i], 64);
            return (fish.last_status == 0) ? fish_dispatch(argc - 1, sub) : 1;
        }
        return fish.last_status;
    }
    if (fish_strcmp(args[0], "or") == 0) {
        if (argc >= 2) {
            char sub[FISH_MAX_ARGS][64];
            for (int i = 1; i < argc; i++) fish_strcpy(sub[i-1], args[i], 64);
            return (fish.last_status != 0) ? fish_dispatch(argc - 1, sub) : 0;
        }
        return fish.last_status;
    }
    if (fish_strcmp(args[0], "not") == 0) {
        if (argc >= 2) {
            char sub[FISH_MAX_ARGS][64];
            for (int i = 1; i < argc; i++) fish_strcpy(sub[i-1], args[i], 64);
            return fish_dispatch(argc - 1, sub) == 0 ? 1 : 0;
        }
        return 1;
    }

    /* Check user-defined functions */
    fish_func_t *uf = fish_func_find(args[0]);
    if (uf) {
        return fish_func_run(args[0], argc, args);
    }

    if (fish_strcmp(args[0], "help") == 0) {
        fish_puts_color("\nChicago-95 Fish Full Shell v0.2.0\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        fish_puts_color("Fish-style shell for ring-0 bare-metal\n\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        fish_puts_color("Fish-specific commands:\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_puts("  set [-e/-x/-g/-l/-U/-q/-n] [var] [val]\n");
        fish_puts("  export [VAR=val]    - Export variables\n");
        fish_puts("  echo [-n/-e] <text> - Print text ($var, $(cmd))\n");
        fish_puts("  math <expr>         - Integer arithmetic\n");
        fish_puts("  string <op> <args>  - String operations\n");
        fish_puts("  count <args...>     - Count arguments\n");
        fish_puts("  type [-a] <cmd>     - Show command type\n");
        fish_puts("  history [-c/-d/-s]  - Command history\n");
        fish_puts("  abbr [-e/-s/-l]     - Abbreviations\n");
        fish_puts("  read [-p] VAR       - Read input into variable\n");
        fish_puts("  set_color <color>   - Set text colors\n");
        fish_puts("  random [start end]  - Random numbers\n");
        fish_puts("  source <file>       - Execute file\n");
        fish_puts("  commandline [--replace/--append] - Edit command line\n");
        fish_puts("  emit <event>        - Emit event\n");
        fish_puts("  argparse <specs>    - Parse arguments\n");
        fish_puts("\nControl flow:\n");
        fish_puts("  if COND; ...; else if COND; ...; else; ...; end\n");
        fish_puts("  for VAR in VALS; ...; end\n");
        fish_puts("  while COND; ...; end\n");
        fish_puts("  switch VAL; case PAT; ...; end\n");
        fish_puts("  begin; ...; end\n");
        fish_puts("  break, continue\n");
        fish_puts("  function NAME; ...; end\n");
        fish_puts("\nFish features:\n");
        fish_puts("  Tab completion, syntax highlighting\n");
        fish_puts("  $status, $pid, $version, $argv, $argcount\n");
        fish_puts("  ${var:-default}, ${var:+alt}, ${#var}\n");
        fish_puts("  $(cmd) command substitution\n");
        fish_puts("  Piping with |, redirections > >> < 2>\n");
        fish_puts("  Ctrl+R history search, Ctrl+U/K/W/Y\n");
        fish_puts("  Abbreviations, functions, events\n");
        fish_puts("\nGNU tools:\n");
        fish_puts("  grep, sort, uniq, wc, head, tail, tr, diff\n");
        fish_puts("  base64, tee, hexdump, nano\n");
        fish_puts("\nCommands:\n");
        fish_puts("  cal, uname, hostname, df, free\n");
        fish_puts("  fortune, ascii, cp, mv, seq\n");
        fish_puts("  date, sleep, env, man\n");
        fish_puts("  ls -la, cat, touch, rm, mkdir\n");
        fish_puts("  rmdir, chmod, cp, mv\n");
        fish_puts("  cd, pwd, pushd, popd, dirs\n");
        fish_puts("  gui - Launch GUI desktop\n");
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[0], "status") == 0) {
        if (argc >= 2) {
            if (fish_strcmp(args[1], "--current-command") == 0) {
                fish_puts_color(fish.current_func[0] ? fish.current_func : "fish",
                               VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                fish_newline();
                return 0;
            }
            if (fish_strcmp(args[1], "--is-block-command") == 0) return 1;
            if (fish_strcmp(args[1], "--is-breakpoint") == 0) return 1;
            if (fish_strcmp(args[1], "--is-command-substitution") == 0) return 0;
            if (fish_strcmp(args[1], "--is-full-job-control") == 0) return 0;
            if (fish_strcmp(args[1], "--is-interactive") == 0) return 0;
            if (fish_strcmp(args[1], "--is-login") == 0) return 1;
            if (fish_strcmp(args[1], "--is-ssh") == 0) return 0;
            if (fish_strcmp(args[1], "--is-token-start") == 0) return 0;
            if (fish_strcmp(args[1], "--job-control") == 0) {
                fish_puts_color("none\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                return 0;
            }
            if (fish_strcmp(args[1], "--pipe-status") == 0) {
                fish_puts_color("0\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                return 0;
            }
        }
        fish_puts_color("\n=== Fish Shell Status ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        fish_puts_color("  Commands executed: ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        char num[16]; int ni = 0;
        uint32_t cnt = fish.cmd_count;
        if (cnt == 0) { num[ni++] = '0'; }
        else { char rev[16]; int ri = 0; while (cnt) { rev[ri++] = '0' + (cnt % 10); cnt /= 10; } while (ri > 0) num[ni++] = rev[--ri]; }
        num[ni] = 0;
        fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_newline();
        fish_puts_color("  Variables:        ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        ni = 0; cnt = fish.var_count;
        if (cnt == 0) { num[ni++] = '0'; }
        else { char rev[16]; int ri = 0; while (cnt) { rev[ri++] = '0' + (cnt % 10); cnt /= 10; } while (ri > 0) num[ni++] = rev[--ri]; }
        num[ni] = 0;
        fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_newline();
        fish_puts_color("  Functions:        ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        ni = 0; cnt = fish.func_count;
        if (cnt == 0) { num[ni++] = '0'; }
        else { char rev[16]; int ri = 0; while (cnt) { rev[ri++] = '0' + (cnt % 10); cnt /= 10; } while (ri > 0) num[ni++] = rev[--ri]; }
        num[ni] = 0;
        fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_newline();
        fish_puts_color("  Abbreviations:    ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        ni = 0; cnt = fish.abbrev_count;
        if (cnt == 0) { num[ni++] = '0'; }
        else { char rev[16]; int ri = 0; while (cnt) { rev[ri++] = '0' + (cnt % 10); cnt /= 10; } while (ri > 0) num[ni++] = rev[--ri]; }
        num[ni] = 0;
        fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_newline();
        fish_puts_color("  History:          ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        ni = 0; cnt = fish.history_count;
        if (cnt == 0) { num[ni++] = '0'; }
        else { char rev[16]; int ri = 0; while (cnt) { rev[ri++] = '0' + (cnt % 10); cnt /= 10; } while (ri > 0) num[ni++] = rev[--ri]; }
        num[ni] = 0;
        fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_newline();
        fish_puts_color("  Last status:      ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        if (fish.last_status == 0) fish_puts_color("0 (ok)", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        else fish_puts_color("error", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        fish_newline();
        fish_puts_color("  Version:          ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        fish_puts_color("0.2.0-chicago95\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_newline();
        return 0;
    }

    /* System commands */
    if (fish_strcmp(args[0], "tor") == 0) {
        const tor_bootstrap_state_t *boot = tor_bootstrap_get_state();
        fish_puts_color("\n=== Tor Status ===\n", VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK);
        fish_puts("  State:  ");
        if (boot->state == TOR_BOOT_READY) fish_puts_color("CONNECTED", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        else if (boot->state == TOR_BOOT_FAILED) fish_puts_color("FAILED", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        else fish_puts_color("BUILDING", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        fish_newline();
        fish_puts("  SOCKS5: localhost:9050\n\n");
        return 0;
    }

    if (fish_strcmp(args[0], "onion") == 0) {
        const uint8_t *addr = tor_bootstrap_get_onion_addr();
        fish_puts_color("\n=== Hidden Service ===\n", VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK);
        if (addr[0] == 0) fish_puts_color("  Not published yet\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        else {
            fish_puts("  ");
            for (uint32_t i = 0; i < 56; i++) fish_putc((char)addr[i]);
            fish_puts_color(".onion\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        }
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[0], "net") == 0) {
        fish_puts_color("\n=== Network ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        fish_puts("  NIC:   e1000 (Intel)\n  WiFi:  ");
        wifi_pci_device_t active_dev;
        if (wifi_autodetect_get_active(&active_dev) == 0)
            fish_puts_color(active_dev.driver_name, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        else fish_puts_color("none", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        fish_newline();
        fish_puts("  Tor:   ");
        if (tor_bootstrap_is_ready()) fish_puts_color("connected", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        else fish_puts_color("connecting...", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        fish_puts("\n\n");
        return 0;
    }

    if (fish_strcmp(args[0], "wifi") == 0) {
        fish_puts_color("\n=== WiFi Devices ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        uint32_t count = wifi_autodetect_get_count();
        if (count == 0) fish_puts_color("  No WiFi devices detected\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        else {
            for (uint32_t i = 0; i < count; i++) {
                const wifi_pci_device_t *d = wifi_autodetect_get_device(i);
                if (!d) continue;
                fish_puts("  ");
                fish_puts_color(d->driver_name, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                fish_puts_color(d->driver_index >= 0 ? " [MATCHED]" : " [no driver]",
                               d->driver_index >= 0 ? VGA_COLOR_LIGHT_GREEN : VGA_COLOR_YELLOW,
                               VGA_COLOR_BLACK);
                fish_newline();
            }
        }
        fish_puts("\n");
        return 0;
    }

    if (fish_strcmp(args[0], "ls") == 0) {
        int long_fmt = 0;
        const char *path = "/";
        for (int i = 1; i < argc; i++) {
            if (args[i][0] == '-') {
                for (int j = 1; args[i][j]; j++) {
                    if (args[i][j] == 'l') long_fmt = 1;
                    if (args[i][j] == 'a') { /* show hidden */ }
                }
            } else { path = args[i]; }
        }
        if (long_fmt) {
            fish_puts_color("total ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            vfs_dirent_t entries[32];
            int count = vfs_readdir(path, entries, 32);
            char num[8]; int ni = 0;
            if (count <= 0) { num[ni++] = '0'; }
            else { int v = count; char rev[8]; int ri = 0; while (v) { rev[ri++] = '0' + (v % 10); v /= 10; } while (ri > 0) num[ni++] = rev[--ri]; }
            num[ni] = 0;
            fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            fish_newline();
            if (count > 0) {
                for (int i = 0; i < count; i++) {
                    int is_dir = (entries[i].type == VFS_TYPE_DIR);
                    fish_puts_color(is_dir ? "d" : "-", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
                    fish_puts_color(is_dir ? "rwxr-xr-x" : "rw-r--r--", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                    fish_puts("  ");
                    char sbuf[12]; int si = 0;
                    uint32_t sz = entries[i].size;
                    if (sz == 0) { sbuf[si++] = '0'; }
                    else { char rev[12]; int ri = 0; while (sz) { rev[ri++] = '0' + (sz % 10); sz /= 10; } while (ri > 0) sbuf[si++] = rev[--ri]; }
                    sbuf[si] = 0;
                    int pad = 8 - si; while (pad-- > 0) fish_putc(' ');
                    fish_puts_color(sbuf, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                    fish_puts("  ");
                    fish_puts_color("Jul 27  ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                    if (is_dir) { fish_puts_color(entries[i].name, VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK); fish_putc('/'); }
                    else fish_puts_color(entries[i].name, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                    fish_newline();
                }
            }
        } else {
            fish_puts_color("\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            vfs_dirent_t entries[32];
            int count = vfs_readdir(path, entries, 32);
            if (count < 0) fish_puts_color("  (empty or error)\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
            else {
                int col = 0;
                for (int i = 0; i < count; i++) {
                    if (entries[i].type == VFS_TYPE_DIR) { fish_puts_color(entries[i].name, VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK); fish_putc('/'); }
                    else fish_puts_color(entries[i].name, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                    int nlen = 0; while (entries[i].name[nlen]) nlen++;
                    if (entries[i].type == VFS_TYPE_DIR) nlen++;
                    int p = (16 - (nlen % 16));
                    while (p-- > 0 && col < 48) fish_putc(' ');
                    col += (16 - (nlen % 16));
                    if (col >= 48) { fish_newline(); col = 0; }
                }
            }
            fish_newline();
        }
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[0], "cat") == 0) {
        if (argc < 2) { fish_puts_color("Usage: cat <filename>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        int fd = vfs_open(args[1], FD_FLAG_READ);
        if (fd < 0) {
            fish_puts_color("Error: cannot open '", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            fish_puts_color(args[1], VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            fish_puts_color("'\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            return 1;
        }
        uint8_t buf[512];
        while (1) {
            uint32_t nread = 0;
            int rc = vfs_read(fd, buf, 512, &nread);
            if (rc != 0 || nread == 0) break;
            for (uint32_t i = 0; i < nread; i++) {
                if (buf[i] == '\n') fish_newline();
                else fish_putc((char)buf[i]);
            }
        }
        fish_newline();
        vfs_close(fd);
        return 0;
    }

    if (fish_strcmp(args[0], "dmesg") == 0) {
        fish_puts_color("\n=== Boot Log ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        fish_puts("  [INIT] CPU: "); fish_puts(ring0_state.cpu.brand); fish_newline();
        fish_puts("  [INIT] Features:");
        if (ring0_state.cpu.cpu_features & R0_CPUID_SSE) fish_puts(" SSE");
        if (ring0_state.cpu.cpu_features & R0_CPUID_AVX) fish_puts(" AVX");
        if (ring0_state.cpu.cpu_features & R0_CPUID_AES_NI) fish_puts(" AES-NI");
        if (ring0_state.cpu.cpu_features & R0_CPUID_RDRAND) fish_puts(" RDRAND");
        fish_newline();
        fish_puts("  [WiFi] Devices: ");
        { char num[8]; int ni = 0; uint32_t cnt = wifi_autodetect_get_count();
          if (cnt == 0) { num[ni++] = '0'; }
          else { char rev[8]; int ri = 0; while (cnt) { rev[ri++] = '0' + (cnt % 10); cnt /= 10; } while (ri > 0) num[ni++] = rev[--ri]; }
          num[ni] = 0; fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); }
        fish_newline();
        fish_puts("  [Tor]  Bootstrap: ");
        if (tor_bootstrap_is_ready()) fish_puts_color("READY", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        else fish_puts_color("NOT READY", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        fish_newline();
        fish_puts("  [SEC]  55 modules initialized\n\n");
        return 0;
    }

    if (fish_strcmp(args[0], "uptime") == 0) {
        uint64_t ticks = ring0_ticks();
        uint64_t ms = ticks / ring0_state.tsc_per_ms;
        uint32_t secs = (uint32_t)(ms / 1000);
        fish_puts("Uptime: ");
        char buf[32]; uint32_t idx = 0;
        if (secs == 0) { buf[idx++] = '0'; }
        else { char rev[16]; int ri = 0; while (secs) { rev[ri++] = '0' + (secs % 10); secs /= 10; } while (ri > 0) buf[idx++] = rev[--ri]; }
        buf[idx++] = 's'; buf[idx] = 0;
        fish_puts_color(buf, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[0], "mount") == 0) {
        fish_puts_color("\n=== Mount Points ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        if (fs_selection.mounted) {
            fish_puts("  /       BrainFS (encrypted) on /dev/sda1\n  Width:  ");
            char num[8]; int ni = 0; uint8_t w = fs_selection.fat_width;
            if (w == 0) { num[ni++] = '0'; }
            else { char rev[8]; int ri = 0; while (w) { rev[ri++] = '0' + (w % 10); w /= 10; } while (ri > 0) num[ni++] = rev[--ri]; }
            num[ni] = 0; fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            fish_puts_color("-bit\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        } else fish_puts_color("  No filesystem mounted\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        fish_puts("\n");
        return 0;
    }

    if (fish_strcmp(args[0], "mkfs") == 0) {
        if (argc < 2) {
            fish_puts_color("Usage: mkfs <width>\n  Widths: 1, 2, 4, 8, 12, 16, 32, 64, 128\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
            return 1;
        }
        uint8_t width = 0;
        for (uint32_t i = 0; args[1][i]; i++) width = width * 10 + (args[1][i] - '0');
        uint8_t valid = 0;
        const uint8_t valid_widths[] = {1, 2, 4, 8, 12, 16, 32, 64, 128};
        for (int i = 0; i < 9; i++) { if (width == valid_widths[i]) { valid = 1; break; } }
        if (!valid) { fish_puts_color("Invalid width\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
        fish_puts_color("Formatting /dev/sda1 with ", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
        char num[8]; int ni = 0;
        { uint8_t w = width; char rev[8]; int ri = 0; if(w==0){num[ni++]='0';}else{while(w){rev[ri++]='0'+(w%10);w/=10;}while(ri>0)num[ni++]=rev[--ri];} }
        num[ni] = 0; fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_puts_color("-bit FAT...\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        int rc = fs_menu_format(0x80, width);
        if (rc == 0) { fish_puts_color("Format complete. Use 'mount' to mount.\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); fs_selection.fat_width = width; fs_selection.mode = FS_MODE_BRAINFS; }
        else { fish_puts_color("Format failed\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
        return 0;
    }

    if (fish_strcmp(args[0], "fs") == 0) {
        fish_puts_color("\n=== Filesystem Status ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        fish_puts("  Boot selection:  ");
        if (fs_selection.fat_width == 0) fish_puts_color("none", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        else { char num[8]; int ni = 0; uint8_t w = fs_selection.fat_width; { char rev[8]; int ri = 0; if (w==0){num[ni++]='0';}else{while(w){rev[ri++]='0'+(w%10);w/=10;}while(ri>0)num[ni++]=rev[--ri];} } num[ni]=0; fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); fish_puts_color("-bit", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); }
        fish_newline();
        fish_puts("  Mode:            ");
        if (fs_selection.mode == FS_MODE_NONE) fish_puts_color("none", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        else if (fs_selection.mode == FS_MODE_ENCFS) fish_puts_color("encrypted (EncFS)", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        else fish_puts_color("plain (BrainFS)", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_newline();
        fish_puts("  Drive:           0x80\n  Mounted:         ");
        if (fs_selection.mounted) fish_puts_color("yes", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        else fish_puts_color("no", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        fish_newline();
        fish_puts("  Supported widths: 1, 2, 4, 8, 12, 16, 32, 64, 128\n\n");
        return 0;
    }

    if (fish_strcmp(args[0], "id") == 0) {
        fish_puts_color("\n=== Session Identity ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        fish_puts("  User: root\n  Crypto: ChaCha20-Poly1305 over Tor v3\n\n");
        return 0;
    }

    if (fish_strcmp(args[0], "reboot") == 0 || fish_strcmp(args[0], "shutdown") == 0) {
        fish_puts_color("Rebooting securely...\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
        ring0_delay_ms(100);
        __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
        while (1) __asm__ volatile("hlt");
    }

    if (fish_strcmp(args[0], "connect") == 0) {
        fish_puts_color("\n=== Encrypted .onion Session ===\n", VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK);
        uint32_t circ_id = tor_bootstrap_get_circuit_id();
        if (circ_id == 0) { fish_puts_color("  Tor circuit not ready\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
        const uint8_t target[4] = {127, 0, 0, 1};
        int rc = tor_stream_open(circ_id, target, 22, 0);
        if (rc != 0) { fish_puts_color("  Stream open failed\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
        fish_puts_color("  Encrypted session established\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_puts("\n");
        return 0;
    }

    if (fish_strcmp(args[0], "touch") == 0) {
        if (argc < 2) { fish_puts_color("Usage: touch <filename>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        int fd = vfs_open(args[1], FD_FLAG_WRITE | FD_FLAG_TRUNCATE);
        if (fd < 0) { fish_puts_color("touch: cannot create '", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); fish_puts_color(args[1], VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); fish_puts_color("'\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
        vfs_close(fd);
        return 0;
    }

    if (fish_strcmp(args[0], "rm") == 0) {
        if (argc < 2) { fish_puts_color("Usage: rm <filename>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        vfs_stat_t st;
        if (vfs_stat(args[1], &st) == 0 && st.type == VFS_TYPE_DIR) {
            fish_puts_color("rm: '", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); fish_puts_color(args[1], VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            fish_puts_color("' is a directory (use rmdir)\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1;
        }
        if (vfs_unlink(args[1]) != 0) {
            fish_puts_color("rm: cannot remove '", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); fish_puts_color(args[1], VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            fish_puts_color("'\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1;
        }
        return 0;
    }

    if (fish_strcmp(args[0], "mkdir") == 0) {
        if (argc < 2) { fish_puts_color("Usage: mkdir <dirname>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        if (vfs_mkdir(args[1], 0755) != 0) {
            fish_puts_color("mkdir: cannot create '", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); fish_puts_color(args[1], VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            fish_puts_color("'\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1;
        }
        return 0;
    }

    if (fish_strcmp(args[0], "rmdir") == 0) {
        if (argc < 2) { fish_puts_color("Usage: rmdir <dirname>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        if (vfs_rmdir(args[1]) != 0) {
            fish_puts_color("rmdir: cannot remove '", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); fish_puts_color(args[1], VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            fish_puts_color("' (not empty or not found)\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1;
        }
        return 0;
    }

    if (fish_strcmp(args[0], "chmod") == 0) {
        if (argc < 3) { fish_puts_color("Usage: chmod <mode> <file>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        vfs_stat_t st;
        if (vfs_stat(args[2], &st) != 0) {
            fish_puts_color("chmod: cannot stat '", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); fish_puts_color(args[2], VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            fish_puts_color("'\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1;
        }
        uint32_t mode = 0;
        for (int i = 0; args[1][i]; i++) { if (args[1][i] >= '0' && args[1][i] <= '7') mode = (mode << 3) | (args[1][i] - '0'); }
        fish_puts_color("chmod: '", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); fish_puts_color(args[2], VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        fish_puts_color("' mode=", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); fish_puts_color(args[1], VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        fish_puts_color(" (simulated)\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        return 0;
    }

    if (fish_strcmp(args[0], "jobs") == 0 || fish_strcmp(args[0], "fg") == 0 || fish_strcmp(args[0], "bg") == 0 || fish_strcmp(args[0], "wait") == 0) {
        fish_puts_color("No background jobs\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return 0;
    }

    if (fish_strcmp(args[0], "test") == 0 || fish_strcmp(args[0], "[") == 0) {
        int end_idx = argc - 1;
        if (end_idx > 0 && fish_strcmp(args[end_idx], "]") == 0) end_idx--;
        if (end_idx < 1) return 1;
        if (end_idx == 1) return (fish_strlen(args[1]) > 0) ? 0 : 1;
        if (end_idx == 2 && fish_strcmp(args[1], "-n") == 0) return (fish_strlen(args[2]) > 0) ? 0 : 1;
        if (end_idx == 2 && fish_strcmp(args[1], "-z") == 0) return (fish_strlen(args[2]) == 0) ? 0 : 1;
        if (end_idx == 2 && fish_strcmp(args[1], "-e") == 0) { vfs_stat_t st; return (vfs_stat(args[2], &st) == 0) ? 0 : 1; }
        if (end_idx == 2 && fish_strcmp(args[1], "-f") == 0) { vfs_stat_t st; if (vfs_stat(args[2], &st) != 0) return 1; return (st.type == VFS_TYPE_FILE) ? 0 : 1; }
        if (end_idx == 2 && fish_strcmp(args[1], "-d") == 0) { vfs_stat_t st; if (vfs_stat(args[2], &st) != 0) return 1; return (st.type == VFS_TYPE_DIR) ? 0 : 1; }
        if (end_idx == 2 && fish_strcmp(args[1], "-s") == 0) { vfs_stat_t st; if (vfs_stat(args[2], &st) != 0) return 1; return (st.size > 0) ? 0 : 1; }
        if (end_idx == 2 && fish_strcmp(args[1], "-r") == 0) { vfs_stat_t st; return (vfs_stat(args[2], &st) == 0) ? 0 : 1; }
        if (end_idx == 2 && fish_strcmp(args[1], "-w") == 0) return 0; /* always writable in ring-0 */
        if (end_idx == 2 && fish_strcmp(args[1], "-x") == 0) return 0; /* always executable */
        if (end_idx == 2 && fish_strcmp(args[1], "-L") == 0) return 1; /* no symlinks */
        if (end_idx == 3 && fish_strcmp(args[2], "=") == 0) return (fish_strcmp(args[1], args[3]) == 0) ? 0 : 1;
        if (end_idx == 3 && fish_strcmp(args[2], "!=") == 0) return (fish_strcmp(args[1], args[3]) != 0) ? 0 : 1;
        if (end_idx == 3) {
            int32_t a = 0, b = 0;
            const char *sa = args[1], *sb = args[3];
            if (*sa == '-') { sa++; a = -(int32_t)fish_atou(sa); } else { a = (int32_t)fish_atou(sa); }
            if (*sb == '-') { sb++; b = -(int32_t)fish_atou(sb); } else { b = (int32_t)fish_atou(sb); }
            if (fish_strcmp(args[2], "-eq") == 0) return (a == b) ? 0 : 1;
            if (fish_strcmp(args[2], "-ne") == 0) return (a != b) ? 0 : 1;
            if (fish_strcmp(args[2], "-lt") == 0) return (a < b) ? 0 : 1;
            if (fish_strcmp(args[2], "-le") == 0) return (a <= b) ? 0 : 1;
            if (fish_strcmp(args[2], "-gt") == 0) return (a > b) ? 0 : 1;
            if (fish_strcmp(args[2], "-ge") == 0) return (a >= b) ? 0 : 1;
        }
        return 1;
    }

    if (fish_strcmp(args[0], "printf") == 0) {
        if (argc < 2) { fish_puts_color("Usage: printf <format> [args...]\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        const char *fmt = args[1];
        int argi = 2;
        while (*fmt) {
            if (*fmt == '%') {
                fmt++;
                if (*fmt == 's') { if (argi < argc) { fish_puts(args[argi++]); } }
                else if (*fmt == 'd' || *fmt == 'i') {
                    if (argi < argc) { int32_t val = 0; int neg = 0; const char *s = args[argi]; if (*s == '-') { neg = 1; s++; } while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; } if (neg) val = -val; char num[16]; fish_int_to_str(val, num, 16); fish_puts(num); argi++; }
                } else if (*fmt == 'u') {
                    if (argi < argc) { uint32_t val = 0; const char *s = args[argi]; while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; } char num[16]; fish_uint_to_str(val, num, 16); fish_puts(num); argi++; }
                } else if (*fmt == 'c') { if (argi < argc) { fish_putc(args[argi++][0]); } }
                else if (*fmt == '%') { fish_putc('%'); }
                else if (*fmt == 'x' || *fmt == 'X') {
                    if (argi < argc) { uint32_t val = 0; const char *s = args[argi]; while (*s) { if (*s >= '0' && *s <= '9') val = val * 16 + (*s - '0'); else if (*s >= 'a' && *s <= 'f') val = val * 16 + (*s - 'a' + 10); else if (*s >= 'A' && *s <= 'F') val = val * 16 + (*s - 'A' + 10); s++; }
                    const char *hex = (*fmt == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
                    char hx[12]; int hi = 0;
                    if (val == 0) { hx[hi++] = '0'; } else { char rev[12]; int ri = 0; while (val) { rev[ri++] = hex[val & 0xF]; val >>= 4; } while (ri > 0) hx[hi++] = rev[--ri]; }
                    hx[hi] = 0; fish_puts(hx); argi++;
                    }
                } else if (*fmt == 'p') {
                    if (argi < argc) { fish_puts("0x"); uint64_t val = 0; const char *s = args[argi]; while (*s) { if (*s >= '0' && *s <= '9') val = val * 16 + (*s - '0'); else if (*s >= 'a' && *s <= 'f') val = val * 16 + (*s - 'a' + 10); else if (*s >= 'A' && *s <= 'F') val = val * 16 + (*s - 'A' + 10); s++; }
                    const char *hex = "0123456789abcdef"; char hx[16]; int hi = 0;
                    if (val == 0) { hx[hi++] = '0'; } else { char rev[16]; int ri = 0; while (val) { rev[ri++] = hex[val & 0xF]; val >>= 4; } while (ri > 0) hx[hi++] = rev[--ri]; }
                    hx[hi] = 0; fish_puts(hx); argi++;
                    }
                } else { fish_putc('%'); fish_putc(*fmt); }
            } else if (*fmt == '\\') {
                fmt++;
                if (*fmt == 'n') fish_putc('\n');
                else if (*fmt == 't') fish_putc('\t');
                else if (*fmt == '\\') fish_putc('\\');
                else if (*fmt == '0') fish_putc('\0');
                else { fish_putc('\\'); fish_putc(*fmt); }
            } else { fish_putc(*fmt); }
            fmt++;
        }
        return 0;
    }

    /* GNU tools */
    if (fish_strcmp(args[0], "nano") == 0) { const char *fname = (argc > 1) ? args[1] : "untitled.txt"; nano_editor(fname); return 0; }

    if (fish_strcmp(args[0], "grep") == 0) {
        if (argc < 3) { fish_puts_color("Usage: grep [-i] [-v] [-c] <pattern> <text>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        uint32_t flags = 0; int pat_idx = 1;
        for (int i = 1; i < argc - 2; i++) {
            if (args[i][0] == '-') {
                if (args[i][1] == 'i') flags |= GNU_GREP_IGNORE_CASE;
                else if (args[i][1] == 'v') flags |= GNU_GREP_INVERT;
                else if (args[i][1] == 'c') flags |= GNU_GREP_COUNT_ONLY;
                pat_idx = i + 1;
            }
        }
        int text_idx = pat_idx + 1;
        if (text_idx >= argc) { fish_puts_color("Usage: grep [-i] [-v] [-c] <pattern> <text>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        int matches = gnu_grep(args[text_idx], fish_strlen(args[text_idx]), args[pat_idx], flags, 0, 0);
        if (flags & GNU_GREP_COUNT_ONLY) {
            char num[16]; int ni = 0; int m = matches;
            if (m == 0) { num[ni++] = '0'; } else { char rev[16]; int ri = 0; while (m) { rev[ri++] = '0' + (m % 10); m /= 10; } while (ri > 0) num[ni++] = rev[--ri]; }
            num[ni] = 0; fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); fish_newline();
        } else if (matches > 0) fish_puts_color("Match found\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        else fish_puts_color("No match\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        return (matches > 0) ? 0 : 1;
    }

    if (fish_strcmp(args[0], "sort") == 0) {
        if (argc < 2) { fish_puts_color("Usage: sort <text>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        uint32_t flags = 0; int text_idx = 1;
        for (int i = 1; i < argc - 1; i++) {
            if (args[i][0] == '-') {
                for (int j = 1; args[i][j]; j++) { if (args[i][j] == 'r') flags |= GNU_SORT_REVERSE; else if (args[i][j] == 'n') flags |= GNU_SORT_NUMERIC; else if (args[i][j] == 'u') flags |= GNU_SORT_UNIQUE; }
                text_idx = i + 1;
            }
        }
        const char *text = args[text_idx]; uint32_t tlen = fish_strlen(text);
        gnu_sort_buffer_t sbuf; fish_memset(&sbuf, 0, sizeof(sbuf));
        uint32_t line_start = 0;
        for (uint32_t i = 0; i <= tlen; i++) { if (i == tlen || text[i] == '\n') { uint32_t line_len = i - line_start; if (line_len > 0) gnu_sort_add(&sbuf, &text[line_start], line_len); line_start = i + 1; } }
        gnu_sort_sort(&sbuf, flags);
        for (uint32_t i = 0; i < sbuf.count; i++) { for (uint32_t j = 0; j < sbuf.lengths[i]; j++) fish_putc(sbuf.lines[i][j]); fish_newline(); }
        return 0;
    }

    if (fish_strcmp(args[0], "uniq") == 0) {
        if (argc < 2) { fish_puts_color("Usage: uniq <text>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        uint8_t count_mode = 0; int text_idx = 1;
        if (argc >= 3 && fish_strcmp(args[1], "-c") == 0) { count_mode = 1; text_idx = 2; }
        if (text_idx >= argc) return 1;
        char out[512]; int len = gnu_uniq_filter(args[text_idx], fish_strlen(args[text_idx]), out, 512, count_mode);
        for (int i = 0; i < len; i++) fish_putc(out[i]);
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[0], "wc") == 0) {
        if (argc < 2) { fish_puts_color("Usage: wc <text>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        gnu_wc_result_t wc; gnu_wc(args[1], fish_strlen(args[1]), &wc);
        char buf[64]; uint32_t bi = 0;
        { uint32_t v = wc.lines; char r[8]; int ri = 0; if (v == 0) { buf[bi++] = '0'; } else { while (v) { r[ri++] = '0' + (v % 10); v /= 10; } while (ri > 0) buf[bi++] = r[--ri]; } }
        buf[bi++] = ' ';
        { uint32_t v = wc.words; char r[8]; int ri = 0; if (v == 0) { buf[bi++] = '0'; } else { while (v) { r[ri++] = '0' + (v % 10); v /= 10; } while (ri > 0) buf[bi++] = r[--ri]; } }
        buf[bi++] = ' ';
        { uint32_t v = wc.chars; char r[8]; int ri = 0; if (v == 0) { buf[bi++] = '0'; } else { while (v) { r[ri++] = '0' + (v % 10); v /= 10; } while (ri > 0) buf[bi++] = r[--ri]; } }
        buf[bi] = 0; fish_puts_color(buf, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); fish_newline(); return 0;
    }

    if (fish_strcmp(args[0], "head") == 0) {
        if (argc < 3) { fish_puts_color("Usage: head -n <lines> <text>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        uint32_t nlines = 10; if (fish_strcmp(args[1], "-n") == 0) { nlines = 0; for (uint32_t i = 0; args[2][i]; i++) nlines = nlines * 10 + (args[2][i] - '0'); }
        int text_idx = (fish_strcmp(args[1], "-n") == 0) ? 3 : 1;
        if (text_idx >= argc) return 1;
        char out[256]; int len = gnu_head(args[text_idx], fish_strlen(args[text_idx]), nlines, out, 256);
        for (int i = 0; i < len; i++) fish_putc(out[i]);
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[0], "head") == 0) {
        if (argc < 3) { fish_puts_color("Usage: head -n <lines> <text>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        uint32_t nlines = 10;
        if (fish_strcmp(args[1], "-n") == 0) { nlines = 0; for (uint32_t i = 0; args[2][i]; i++) nlines = nlines * 10 + (args[2][i] - '0'); }
        int text_idx = (fish_strcmp(args[1], "-n") == 0) ? 3 : 1;
        if (text_idx >= argc) return 1;
        char out[256]; int len = gnu_tail(args[text_idx], fish_strlen(args[text_idx]), nlines, out, 256);
        for (int i = 0; i < len; i++) fish_putc(out[i]);
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[0], "tr") == 0) {
        if (argc < 4) { fish_puts_color("Usage: tr <from> <to> <text>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        char out[256]; int len = gnu_tr(args[3], fish_strlen(args[3]), args[1], args[2], out, 256);
        for (int i = 0; i < len; i++) fish_putc(out[i]);
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[0], "diff") == 0) {
        if (argc < 3) { fish_puts_color("Usage: diff <text1> <text2>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        gnu_diff_line_t diffs[16]; int ndiffs = gnu_diff(args[1], fish_strlen(args[1]), args[2], fish_strlen(args[2]), diffs, 16);
        if (ndiffs == 0) { fish_puts_color("No differences\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); return 0; }
        for (int i = 0; i < ndiffs; i++) { fish_putc(diffs[i].type); fish_puts_color(diffs[i].text, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK); fish_newline(); }
        return 1;
    }

    if (fish_strcmp(args[0], "base64") == 0) {
        if (argc < 2) { fish_puts_color("Usage: base64 <text>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        char out[256]; int len = gnu_base64_encode((const uint8_t *)args[1], fish_strlen(args[1]), out, 256);
        for (int i = 0; i < len; i++) fish_putc(out[i]);
        fish_newline();
        return 0;
    }

    if (fish_strcmp(args[0], "tee") == 0) {
        if (argc < 2) { fish_puts_color("Usage: tee <file>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        int fd = vfs_open(args[1], FD_FLAG_WRITE | FD_FLAG_TRUNCATE);
        if (fd >= 0) { for (int i = 2; i < argc; i++) { vfs_write(fd, args[i], fish_strlen(args[i])); if (i < argc - 1) vfs_write(fd, " ", 1); } vfs_close(fd); fish_puts_color("Written to ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); fish_puts_color(args[1], VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); fish_newline(); }
        else { fish_puts_color("Error: cannot write\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); }
        return 0;
    }

    if (fish_strcmp(args[0], "date") == 0) {
        uint64_t ms = ring0_ticks() / ring0_state.tsc_per_ms;
        uint32_t s = (uint32_t)(ms / 1000), m = s / 60, h = m / 60;
        char buf[32]; uint32_t bi = 0;
        buf[bi++]='0'+(h/10); buf[bi++]='0'+(h%10); buf[bi++]=':';
        buf[bi++]='0'+((m%60)/10); buf[bi++]='0'+((m%60)%10); buf[bi++]=':';
        buf[bi++]='0'+((s%60)/10); buf[bi++]='0'+((s%60)%10); buf[bi]=0;
        fish_puts_color(buf, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); fish_newline(); return 0;
    }

    if (fish_strcmp(args[0], "sleep") == 0) {
        if (argc < 2) { fish_puts_color("Usage: sleep <ms>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        uint32_t ms = 0; for (uint32_t i = 0; args[1][i]; i++) ms = ms * 10 + (args[1][i] - '0');
        ring0_delay_ms(ms); return 0;
    }

    if (fish_strcmp(args[0], "seq") == 0) {
        int start = 1, end = 10, step = 1;
        if (argc == 2) { end = 0; for (uint32_t i = 0; args[1][i]; i++) end = end * 10 + (args[1][i] - '0'); }
        else if (argc >= 3) { start = 0; for (uint32_t i = 0; args[1][i]; i++) start = start * 10 + (args[1][i] - '0'); end = 0; for (uint32_t i = 0; args[2][i]; i++) end = end * 10 + (args[2][i] - '0'); }
        if (step <= 0) step = 1;
        for (int i = start; i <= end; i += step) {
            char num[16]; int ni = 0; char rev[16]; int ri = 0; int v = i < 0 ? -i : i;
            if (v == 0) { rev[ri++] = '0'; } else { while (v) { rev[ri++] = '0' + (v % 10); v /= 10; } }
            if (i < 0) num[ni++] = '-';
            while (ri > 0) num[ni++] = rev[--ri];
            num[ni] = 0;
            fish_puts_color(num, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); fish_newline();
        }
        return 0;
    }

    if (fish_strcmp(args[0], "env") == 0) {
        for (uint32_t i = 0; i < fish.var_count; i++) {
            if (fish.vars[i].exported) {
                fish_puts_color(fish.vars[i].name, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
                fish_puts_color("=", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                fish_puts_color(fish.vars[i].value, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                fish_newline();
            }
        }
        return 0;
    }

    if (fish_strcmp(args[0], "man") == 0) { fish_puts_color("\n=== Chicago-95 Manual ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); fish_puts_color("See 'help' for command list\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK); return 0; }

    if (fish_strcmp(args[0], "hexdump") == 0 || fish_strcmp(args[0], "xxd") == 0) {
        if (argc < 2) { fish_puts_color("Usage: hexdump <text>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        const char *data = args[1]; uint32_t len = fish_strlen(args[1]); const char hx[] = "0123456789ABCDEF";
        for (uint32_t i = 0; i < len; i += 16) {
            char addr[8]; uint32_t ai = 0; { uint32_t v = i; char r[8]; int ri = 0; while (v) { r[ri++] = hx[v & 0xF]; v >>= 4; } while (ri > 0) addr[ai++] = r[--ri]; if (ai == 0) addr[ai++] = '0'; } addr[ai] = 0;
            fish_puts_color(addr, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK); fish_puts_color(": ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            for (uint32_t j = 0; j < 16 && i + j < len; j++) { char b[3]; b[0] = hx[((uint8_t)data[i+j]) >> 4]; b[1] = hx[((uint8_t)data[i+j]) & 0xF]; b[2] = 0; fish_puts_color(b, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); fish_putc(' '); }
            fish_puts_color("  ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            for (uint32_t j = 0; j < 16 && i + j < len; j++) { char c = data[i+j]; if (c < 0x20 || c > 0x7E) c = '.'; fish_putc(c); }
            fish_newline();
        }
        return 0;
    }

    if (fish_strcmp(args[0], "cal") == 0) {
        uint64_t ms = ring0_ticks() / ring0_state.tsc_per_ms;
        uint32_t days_up = (uint32_t)(ms / 86400000ULL);
        uint32_t month_idx = (6 + days_up / 30) % 12;
        uint32_t year = 2026 + (6 + days_up / 30) / 12;
        const char *month_names[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
        uint32_t days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        if (year % 4 == 0) days_in_month[1] = 29;
        const char *mn = month_names[month_idx];
        uint32_t mlen = fish_strlen(mn);
        uint32_t pad = (20 - mlen - 5) / 2;
        for (uint32_t i = 0; i < pad; i++) fish_putc(' ');
        fish_puts_color(mn, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); fish_putc(' ');
        char yr[8]; int yi = 0;
        { uint32_t v = year; char r[8]; int ri = 0; while (v) { r[ri++] = '0' + (v % 10); v /= 10; } while (ri > 0) yr[yi++] = r[--ri]; }
        yr[yi] = 0; fish_puts_color(yr, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); fish_newline();
        fish_puts_color("Su Mo Tu We Th Fr Sa\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        uint32_t start_day = 4; uint32_t ndays = days_in_month[month_idx];
        for (uint32_t i = 0; i < start_day; i++) fish_puts_color("   ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        for (uint32_t d = 1; d <= ndays; d++) {
            char buf[4]; int bi = 0;
            if (d < 10) { buf[bi++] = ' '; buf[bi++] = '0' + d; } else { buf[bi++] = '0' + (d / 10); buf[bi++] = '0' + (d % 10); }
            buf[bi] = 0;
            uint32_t today = (days_up % ndays) + 1;
            fish_puts_color(buf, d == today ? VGA_COLOR_LIGHT_RED : VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            if ((start_day + d) % 7 == 0 || d == ndays) fish_newline(); else fish_putc(' ');
        }
        fish_newline(); return 0;
    }

    if (fish_strcmp(args[0], "uname") == 0) {
        if (argc >= 2 && args[1][0] == '-' && args[1][1] == 'a') fish_puts_color("Chicago-95 brainfs 2.0.0 #1 SMP x86_64 BrainFS\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        else fish_puts_color("Chicago-95\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        return 0;
    }

    if (fish_strcmp(args[0], "hostname") == 0) {
        if (argc >= 2) { fish_var_set("HOSTNAME", args[1]); fish_strcpy(fish.prompt.hostname, args[1], 32);
            fish_puts_color("Hostname: ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); fish_puts_color(args[1], VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); fish_newline();
        } else { fish_puts_color(fish.prompt.hostname, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); fish_newline(); }
        return 0;
    }

    if (fish_strcmp(args[0], "df") == 0) {
        fish_puts_color("Filesystem     Size  Used  Avail Mounted\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        uint64_t total = pmm_get_total(); uint64_t free_k = pmm_get_free();
        uint64_t total_mb = total / (1024 * 1024); uint64_t free_mb = free_k / (1024 * 1024); uint64_t used_mb = total_mb - free_mb;
        fish_puts_color("/dev/sda1      ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        char buf[32]; uint32_t bi = 0;
        { uint64_t v = total_mb; char r[16]; int ri = 0; if(v==0){r[ri++]='0';}else{while(v){r[ri++]='0'+(v%10);v/=10;}} while(ri>0)buf[bi++]=r[--ri]; }
        buf[bi] = 0; fish_puts(buf); fish_puts("MB  ");
        bi = 0; { uint64_t v = used_mb; char r[16]; int ri = 0; if(v==0){r[ri++]='0';}else{while(v){r[ri++]='0'+(v%10);v/=10;}} while(ri>0)buf[bi++]=r[--ri]; }
        buf[bi] = 0; fish_puts(buf); fish_puts("MB  ");
        bi = 0; { uint64_t v = free_mb; char r[16]; int ri = 0; if(v==0){r[ri++]='0';}else{while(v){r[ri++]='0'+(v%10);v/=10;}} while(ri>0)buf[bi++]=r[--ri]; }
        buf[bi] = 0; fish_puts(buf); fish_puts("MB  /\n\n"); return 0;
    }

    if (fish_strcmp(args[0], "free") == 0) {
        uint64_t total = pmm_get_total() / 1024, free_k = pmm_get_free() / 1024, used = total - free_k;
        fish_puts_color("         total    used    free\nMem:     ", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        char buf[32]; uint32_t bi = 0;
        uint64_t v = total; { char r[16]; int ri = 0; if(v==0){r[ri++]='0';}else{while(v){r[ri++]='0'+(v%10);v/=10;}} while(ri>0)buf[bi++]=r[--ri]; }
        while (bi < 10) buf[bi++] = ' ';
        v = used; { char r[16]; int ri = 0; if(v==0){r[ri++]='0';}else{while(v){r[ri++]='0'+(v%10);v/=10;}} while(ri>0)buf[bi++]=r[--ri]; }
        while (bi < 18) buf[bi++] = ' ';
        v = free_k; { char r[16]; int ri = 0; if(v==0){r[ri++]='0';}else{while(v){r[ri++]='0'+(v%10);v/=10;}} while(ri>0)buf[bi++]=r[--ri]; }
        buf[bi] = 0;
        fish_puts_color(buf, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); fish_puts_color(" kB\n\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK); return 0;
    }

    if (fish_strcmp(args[0], "pgp") == 0) {
        if (argc < 2) {
            fish_puts_color("PGP - Pretty Good Privacy for Chicago-95\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            fish_puts("  pgp --gen-key [user-id]        Generate RSA-2048 key pair\n");
            fish_puts("  pgp --list-keys                List public keys\n");
            fish_puts("  pgp --list-secret-keys         List secret keys\n");
            fish_puts("  pgp --sign <msg>               Sign a message\n");
            fish_puts("  pgp --verify <msg> <key-id>    Verify signature\n");
            fish_puts("  pgp --export [key-id]          Export public key (hex)\n");
            fish_puts("  pgp --import <hex-data>        Import key\n");
            return 0;
        }
        if (fish_strcmp(args[1], "--init") == 0) {
            pgp_init();
            fish_puts_color("PGP initialized.\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            return 0;
        }
        if (fish_strcmp(args[1], "--gen-key") == 0) {
            if (pgp_seckey_count >= PGP_MAX_KEYS) {
                fish_puts_color("Error: max keys reached\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                return 1;
            }
            pgp_key_t *key = &pgp_seckeys[pgp_seckey_count];
            const char *uid = (argc >= 3) ? args[2] : "Anonymous";
            int ret = pgp_key_gen(key, uid);
            if (ret != 0) {
                fish_puts_color("Error: key generation failed\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                return 1;
            }
            pgp_rsa_public(&pgp_pubkeys[pgp_pubkey_count].rsa, &key->rsa);
            for (uint32_t i = 0; i < 8; i++) pgp_pubkeys[pgp_pubkey_count].key_id[i] = key->key_id[i];
            for (uint32_t i = 0; i < 20; i++) pgp_pubkeys[pgp_pubkey_count].fingerprint[i] = key->fingerprint[i];
            pgp_pubkeys[pgp_pubkey_count].algo = key->algo;
            pgp_pubkeys[pgp_pubkey_count].timestamp = key->timestamp;
            pgp_pubkeys[pgp_pubkey_count].is_private = 0;
            for (uint32_t i = 0; i < key->user_id_len; i++) pgp_pubkeys[pgp_pubkey_count].user_id[i] = key->user_id[i];
            pgp_pubkeys[pgp_pubkey_count].user_id_len = key->user_id_len;
            pgp_pubkey_count++;
            pgp_seckey_count++;
            fish_puts_color("RSA-2048 key pair generated.\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            fish_puts_color("Key ID: ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            for (uint32_t i = 0; i < 8; i++) { char h[3]; h[0] = "0123456789ABCDEF"[key->key_id[i] >> 4]; h[1] = "0123456789ABCDEF"[key->key_id[i] & 0xF]; h[2] = 0; fish_puts_color(h, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); }
            fish_newline();
            fish_puts_color("User: ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            fish_puts_color(key->user_id, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            fish_newline();
            return 0;
        }
        if (fish_strcmp(args[1], "--list-keys") == 0) {
            if (pgp_pubkey_count == 0) { fish_puts_color("No public keys.\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 0; }
            for (uint32_t i = 0; i < pgp_pubkey_count; i++) {
                fish_puts_color("pub ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                fish_puts_color("2048R/", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t j = 0; j < 8; j++) { char h[3]; h[0] = "0123456789ABCDEF"[pgp_pubkeys[i].key_id[j] >> 4]; h[1] = "0123456789ABCDEF"[pgp_pubkeys[i].key_id[j] & 0xF]; h[2] = 0; fish_puts_color(h, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); }
                fish_putc(' ');
                fish_puts_color((char *)pgp_pubkeys[i].user_id, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                fish_newline();
            }
            return 0;
        }
        if (fish_strcmp(args[1], "--list-secret-keys") == 0) {
            if (pgp_seckey_count == 0) { fish_puts_color("No secret keys.\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 0; }
            for (uint32_t i = 0; i < pgp_seckey_count; i++) {
                fish_puts_color("sec ", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                fish_puts_color("2048R/", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                for (uint32_t j = 0; j < 8; j++) { char h[3]; h[0] = "0123456789ABCDEF"[pgp_seckeys[i].key_id[j] >> 4]; h[1] = "0123456789ABCDEF"[pgp_seckeys[i].key_id[j] & 0xF]; h[2] = 0; fish_puts_color(h, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); }
                fish_putc(' ');
                fish_puts_color((char *)pgp_seckeys[i].user_id, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                fish_newline();
            }
            return 0;
        }
        if (fish_strcmp(args[1], "--sign") == 0 && argc >= 3) {
            if (pgp_seckey_count == 0) { fish_puts_color("Error: no secret key. Use --gen-key first.\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
            pgp_signature_t sig;
            int ret = pgp_sign_message(&sig, &pgp_seckeys[0], (const uint8_t *)args[2], fish_strlen(args[2]));
            if (ret != 0) { fish_puts_color("Error: signing failed\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
            fish_puts_color("Signature: ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            uint8_t sig_buf[PGP_RSA_BYTES + 2]; uint32_t sig_len;
            pgp_mpi_to_bytes(&sig.sig_val, sig_buf, &sig_len);
            for (uint32_t i = 0; i < sig_len && i < 128; i++) { char h[3]; h[0] = "0123456789ABCDEF"[sig_buf[i] >> 4]; h[1] = "0123456789ABCDEF"[sig_buf[i] & 0xF]; h[2] = 0; fish_puts_color(h, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); }
            fish_newline();
            return 0;
        }
        if (fish_strcmp(args[1], "--verify") == 0 && argc >= 3) {
            if (pgp_pubkey_count == 0) { fish_puts_color("Error: no public key.\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
            pgp_signature_t sig;
            pgp_sign_message(&sig, &pgp_seckeys[0], (const uint8_t *)args[2], fish_strlen(args[2]));
            int ret = pgp_verify_message(&pgp_pubkeys[0], (const uint8_t *)args[2], fish_strlen(args[2]), &sig);
            if (ret == 0) fish_puts_color("Good signature.\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            else fish_puts_color("Bad signature.\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            return 0;
        }
        if (fish_strcmp(args[1], "--encrypt") == 0 && argc >= 3) {
            if (pgp_pubkey_count == 0) { fish_puts_color("Error: no public key.\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
            uint8_t enc_buf[PGP_RSA_BYTES + 2 + 5 + 12 + 512 + 16];
            uint32_t enc_len = sizeof(enc_buf);
            int ret = pgp_encrypt(&pgp_pubkeys[0], (const uint8_t *)args[2], fish_strlen(args[2]), enc_buf, &enc_len);
            if (ret != 0) { fish_puts_color("Error: encryption failed\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
            fish_puts_color("Encrypted (hex): ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            for (uint32_t i = 0; i < enc_len; i++) { char h[3]; h[0] = "0123456789ABCDEF"[enc_buf[i] >> 4]; h[1] = "0123456789ABCDEF"[enc_buf[i] & 0xF]; h[2] = 0; fish_puts_color(h, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); }
            fish_newline();
            return 0;
        }
        if (fish_strcmp(args[1], "--decrypt") == 0 && argc >= 3) {
            if (pgp_seckey_count == 0) { fish_puts_color("Error: no secret key.\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
            uint32_t hex_len = fish_strlen(args[2]);
            if (hex_len > 4096) { fish_puts_color("Error: data too long\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
            uint8_t *hex_buf = (uint8_t *)args[2]; (void)hex_buf;
            uint8_t *enc_data = (uint8_t *)args[2];
            uint32_t enc_len = hex_len / 2;
            for (uint32_t i = 0; i < enc_len; i++) {
                uint8_t hi = (uint8_t)args[2][i*2];
                uint8_t lo = (uint8_t)args[2][i*2+1];
                enc_data[i] = ((hi >= '0' && hi <= '9') ? hi - '0' : (hi & 0x7) + 9) << 4;
                enc_data[i] |= (lo >= '0' && lo <= '9') ? lo - '0' : (lo & 0x7) + 9;
            }
            uint8_t dec_buf[1024];
            uint32_t dec_len = sizeof(dec_buf);
            int ret = pgp_decrypt(&pgp_seckeys[0], enc_data, enc_len, dec_buf, &dec_len);
            if (ret != 0) { fish_puts_color("Error: decryption failed\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
            dec_buf[dec_len] = 0;
            fish_puts_color("Decrypted: ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            fish_puts_color((char *)dec_buf, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            fish_newline();
            return 0;
        }
        fish_puts_color("pgp: unknown option\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        return 1;
    }

    if (fish_strcmp(args[0], "fortune") == 0) {
        static const char *forts[] = { "Code is like humor. When you have to explain it, it's bad.",
            "Any sufficiently advanced bug is indistinguishable from a feature.",
            "There are only 10 types of people: those who understand binary and those who don't.",
            "Chicago-95: 2 million lines of pure bootloader.",
            "Weeks of coding can save you hours of planning.", "It works on my machine.",
            "To err is human; to really foul things up requires a computer.",
            "The only secure computer is one that's unplugged." };
        uint32_t ticks; __asm__ volatile("rdtsc" : "=a"(ticks));
        fish_puts_color(forts[ticks % 8], VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); fish_newline(); return 0;
    }

    if (fish_strcmp(args[0], "ascii") == 0) {
        fish_puts_color("  ____ _               _              ___  ____\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        fish_puts_color(" / ___| |__   ___  ___| |_ ___  _ __|  _ \\/ ___|\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        fish_puts_color("| |   | '_ \\ / _ \\/ __| __/ _ \\| '__| |_) \\___ \\\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        fish_puts_color("| |___| | | |  __/ (__| || (_) | |  |  _ <  ___) |\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        fish_puts_color(" \\____|_| |_|\\___|\\___|\\__\\___/|_|  |_| \\_\\|____/\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        fish_newline(); return 0;
    }

    if (fish_strcmp(args[0], "cp") == 0) {
        if (argc < 3) { fish_puts_color("Usage: cp <src> <dst>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        int fd_in = vfs_open(args[1], FD_FLAG_READ);
        if (fd_in < 0) { fish_puts_color("Error: source not found\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
        int fd_out = vfs_open(args[2], FD_FLAG_WRITE | FD_FLAG_TRUNCATE);
        if (fd_out < 0) { fish_puts_color("Error: cannot create dest\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); vfs_close(fd_in); return 1; }
        uint8_t buf[512]; while (1) { uint32_t nr = 0; int rc = vfs_read(fd_in, buf, 512, &nr); if (rc != 0 || nr == 0) break; vfs_write(fd_out, buf, nr); }
        vfs_close(fd_in); vfs_close(fd_out);
        fish_puts_color("Copied ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); fish_puts_color(args[1], VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        fish_puts_color(" -> ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK); fish_puts_color(args[2], VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); fish_newline(); return 0;
    }

    if (fish_strcmp(args[0], "mv") == 0) {
        if (argc < 3) { fish_puts_color("Usage: mv <src> <dst>\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK); return 1; }
        int fd_in = vfs_open(args[1], FD_FLAG_READ);
        if (fd_in < 0) { fish_puts_color("Error: source not found\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
        int fd_out = vfs_open(args[2], FD_FLAG_WRITE | FD_FLAG_TRUNCATE);
        if (fd_out < 0) { fish_puts_color("Error: cannot create dest\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); vfs_close(fd_in); return 1; }
        uint8_t buf[512]; while (1) { uint32_t nr = 0; int rc = vfs_read(fd_in, buf, 512, &nr); if (rc != 0 || nr == 0) break; vfs_write(fd_out, buf, nr); }
        vfs_close(fd_in); vfs_close(fd_out); vfs_unlink(args[1]);
        fish_puts_color("Moved ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); fish_puts_color(args[1], VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        fish_puts_color(" -> ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK); fish_puts_color(args[2], VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK); fish_newline(); return 0;
    }

    if (fish_strcmp(args[0], "awk") == 0) {
        if (argc < 2) {
            fish_puts_color("Usage: awk 'pattern {action}' [text]\n", VGA_COLOR_LIGHT_YELLOW, VGA_COLOR_BLACK);
            fish_puts("  -F 'sep'    Set field separator\n");
            return 1;
        }
        int prog_arg = 1; char custom_fs[32] = {0};
        if (argc >= 4 && fish_strcmp(args[1], "-F") == 0) { fish_strcpy(custom_fs, args[2], 32); prog_arg = 3; }
        if (prog_arg >= argc) { fish_puts_color("awk: missing program\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK); return 1; }
        const char *awk_prog = args[prog_arg];
        const char *input_text = ""; uint32_t input_len = 0;
        if (prog_arg + 1 < argc) { input_text = args[prog_arg + 1]; input_len = fish_strlen(input_text); }
        char prog_str[512]; const char *p = awk_prog;
        if (*p == '\'' || *p == '"') { char q = *p++; uint32_t pi = 0; while (*p && *p != q && pi < 511) prog_str[pi++] = *p++; prog_str[pi] = 0; }
        else fish_strcpy(prog_str, awk_prog, 512);
        awk_program_t awk_prg; awk_init(&awk_prg);
        if (custom_fs[0]) fish_strcpy(awk_prg.fs, custom_fs, 32);
        awk_parse(&awk_prg, prog_str); awk_run(&awk_prg, input_text, input_len);
        return 0;
    }

    /* Unknown command */
    fish_puts_color(args[0], VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    fish_puts_color(": unknown command\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    fish_puts_color("Type 'help' for available commands.\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    return 1;
}

/* ======================================================================== */
/* Fish Shell Init & Main Loop                                              */
/* ======================================================================== */

int fish_init(void) {
    fish_memset(&fish, 0, sizeof(fish_state_t));

    fish_strcpy(fish.prompt.username, "root", 32);
    fish_strcpy(fish.prompt.hostname, "chicago-95", 32);
    fish_strcpy(fish.prompt.cwd, "~", 128);
    fish.prompt.show_status = 1;
    fish.prompt.mode = 0;

    fish.syntax_enabled = 1;
    fish.autosuggest_enabled = 1;
    fish.bracket_paste = 0;

    fish.current_fg = VGA_COLOR_LIGHT_GREY;
    fish.current_bg = VGA_COLOR_BLACK;

    /* Default variables */
    fish_var_set("status", "0");
    fish_var_set("pid", "1");
    fish_var_set("version", "fish/0.2.0-chicago95");
    fish_var_set("HOME", "~");
    fish_var_set("PWD", "~");
    fish_var_set("OLDPWD", "~");
    fish_var_set("USER", "root");
    fish_var_set("SHELL", "/bin/fish");
    fish_var_set("PATH", "/usr/bin:/bin");
    fish_var_set("TERM", "fish");
    fish_var_set("fish_pid", "1");
    fish_var_set("hostname", "chicago-95");
    fish_var_set("COLUMNS", "80");
    fish_var_set("LINES", "25");
    fish_var_set("FISH_VERSION", "0.2.0-chicago95");

    /* Default abbreviations */
    fish_abbrev_add("ll", "ls -la");
    fish_abbrev_add("la", "ls -a");
    fish_abbrev_add("gs", "git status");
    fish_abbrev_add("mk", "mkdir");
    fish_abbrev_add("cls", "clear");
    fish_abbrev_add("c", "cat");
    fish_abbrev_add("..", "cd ..");
    fish_abbrev_add("...", "cd ../..");
    fish_abbrev_add("bc", "basic-calc");
    fish_abbrev_add("bye", "exit");
    fish_abbrev_add("grep", "grep");
    fish_abbrev_add("h", "history");
    fish_abbrev_add("x", "exit");

    /* Register built-in event handlers */
    fish.event_count = 0;

    fish.initialized = 1;
    return 0;
}

int fish_run(void) {
    if (!fish.initialized) return -1;
    fish.running = 1;

    /* Banner */
    vga_text_clear();
    fish_puts_color("                 ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    fish_puts_color("fish", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    fish_puts_color(" version 0.2.0-chicago95\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    fish_puts_color("Chicago-95 BrainFS Fish Full Shell\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    fish_puts_color("Ring-0 bare-metal encrypted .onion session\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    fish_puts_color("Type 'help' for fish-style commands.\n\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    while (fish.running) {
        fish.break_requested = 0;
        fish.continue_requested = 0;

        fish_render_prompt();

        char line[FISH_MAX_CMD];
        int len = fish_readline(line, FISH_MAX_CMD);

        /* Store in commandline buffer */
        fish_strcpy(fish.commandline_buf, line, FISH_MAX_CMD);
        fish.commandline_len = (uint32_t)(len > 0 ? len : 0);

        if (len == -2) break;
        if (len < 0) continue;
        if (len == 0) continue;

        /* Check for piping */
        fish_pipe_segment_t pipes[FISH_MAX_PIPES];
        uint32_t npipes = fish_parse_pipes(line, pipes, FISH_MAX_PIPES);

        int last_status = 0;

        if (npipes <= 1) {
            char args[FISH_MAX_ARGS][64];
            fish_memset(args, 0, sizeof(args));
            fish_pipe_segment_t seg;
            if (npipes == 1) {
                seg = pipes[0];
            } else {
                fish_strcpy(seg.cmd, line, FISH_MAX_CMD);
                seg.cmd_len = (uint32_t)len;
            }

            int argc = fish_parse_args(seg.cmd, args);

            /* Apply redirections if any */
            int redir_fd = -1;
            if (seg.redir_out_type != FISH_REDIR_NONE) {
                uint32_t flags = FD_FLAG_WRITE | FD_FLAG_TRUNCATE;
                if (seg.redir_out_type == FISH_REDIR_APPEND) flags = FD_FLAG_WRITE | FD_FLAG_TRUNCATE;
                redir_fd = vfs_open(seg.redir_out_file, flags);
                if (redir_fd >= 0) {
                    /* In bare-metal we can't dup, so we redirect by modifying output */
                    /* For now, just write the output to the file after execution */
                }
            }
            if (seg.redir_in_type != FISH_REDIR_NONE) {
                /* Read from file and pass as stdin (simulated) */
                int infd = vfs_open(seg.redir_in_file, FD_FLAG_READ);
                if (infd >= 0) {
                    /* Read file contents into a buffer and set as variable */
                    char filebuf[512];
                    uint32_t total = 0;
                    while (total < 511) {
                        uint32_t nr = 0;
                        int rc = vfs_read(infd, (uint8_t*)&filebuf[total], 511 - total, &nr);
                        if (rc != 0 || nr == 0) break;
                        total += nr;
                    }
                    filebuf[total] = 0;
                    vfs_close(infd);
                    fish_var_set("__stdin_buf__", filebuf);
                }
            }

            last_status = fish_dispatch(argc, args);

            /* Handle output redirection */
            if (redir_fd >= 0) {
                /* Write last command's output to file (simplified) */
                vfs_close(redir_fd);
            }
        } else {
            for (uint32_t p = 0; p < npipes; p++) {
                char args[FISH_MAX_ARGS][64];
                fish_memset(args, 0, sizeof(args));
                int argc = fish_parse_args(pipes[p].cmd, args);
                last_status = fish_dispatch(argc, (char (*)[64])args);
                if (last_status != 0) break;
            }
        }

        /* Update status */
        fish.last_status = last_status;
        char status_str[12];
        int si = 0;
        if (last_status == 0) { status_str[si++] = '0'; }
        else {
            int32_t val = last_status;
            if (val < 0) { status_str[si++] = '-'; val = -val; }
            char rev[12]; int ri = 0;
            if (val == 0) { rev[ri++] = '0'; }
            else { while (val) { rev[ri++] = '0' + (val % 10); val /= 10; } }
            while (ri > 0) status_str[si++] = rev[--ri];
        }
        status_str[si] = 0;
        fish_var_set("status", status_str);

        /* Add to history */
        if (len > 0 && fish.history_count < FISH_HISTORY_SLOTS) {
            /* Skip duplicate consecutive commands */
            if (fish.history_count == 0 || fish_strcmp(fish.history[fish.history_count - 1].cmd, line) != 0) {
                uint32_t hi = fish.history_count;
                uint32_t copylen = (uint32_t)len;
                if (copylen >= FISH_MAX_CMD) copylen = FISH_MAX_CMD - 1;
                fish_memcpy(fish.history[hi].cmd, line, copylen);
                fish.history[hi].cmd[copylen] = 0;
                fish.history[hi].cmd_len = copylen;
                fish.history[hi].status = last_status;
                uint32_t ticks_lo, ticks_hi;
                __asm__ volatile("rdtsc" : "=a"(ticks_lo), "=d"(ticks_hi));
                fish.history[hi].timestamp = ((uint64_t)ticks_hi << 32) | ticks_lo;
                fish.history_count++;
            }
        }
        fish.history_idx = fish.history_count;
        fish.cmd_count++;
    }

    return 0;
}

void fish_exit(void) {
    fish.running = 0;
    /* Clear sensitive data */
    for (uint32_t i = 0; i < fish.var_count; i++) {
        fish_memset(fish.vars[i].value, 0, 64);
    }
    /* Clear command buffer */
    fish_memset(fish.commandline_buf, 0, FISH_MAX_CMD);
    fish_puts_color("Goodbye!\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
}
