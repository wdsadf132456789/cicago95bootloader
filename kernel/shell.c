#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "timer.h"
#include "process.h"
#include "pci.h"
#include "kmalloc.h"
#include "kernel.h"
#include "ata.h"
#include "btop.h"
#include "kmsg.h"
#include "drivers/e1000.h"
#include "drivers/net.h"
#include "drivers/usb.h"
#include "drivers/xhci.h"
#include "vfs.h"
#include <stdint.h>
#include <stddef.h>

static char line_buffer[256];
static int line_pos = 0;

#define HISTORY_SIZE 32
static char history_buf[HISTORY_SIZE][256];
static int history_len = 0;
static int history_idx = -1;

static char current_dir[VFS_PATH_LEN] = "/";
static vfs_node_t *redir_file_node = NULL;
static uint64_t redir_file_offset = 0;

#define PIPE_BUF_SIZE 16384
static char pipe_buffer[PIPE_BUF_SIZE];
static size_t pipe_buffer_len = 0;

static int shell_redirect_writer(char c) {
    if (redir_file_node) {
        vfs_write(redir_file_node, redir_file_offset++, &c, 1);
        return 1;
    }
    return 0;
}

static int shell_pipe_writer(char c) {
    if (pipe_buffer_len < PIPE_BUF_SIZE - 1) {
        pipe_buffer[pipe_buffer_len++] = c;
        pipe_buffer[pipe_buffer_len] = '\0';
        return 1;
    }
    return 1;
}

static char *strstr_simple(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack, *n = needle;
            while (*h && *n && *h == *n) {
                h++;
                n++;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

static void strncpy_safe(char *dst, const char *src, size_t n) {
    if (n == 0) return;
    size_t i = 0;
    while (i < n - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void strcat_safe(char *dst, const char *src, size_t n) {
    size_t dlen = strlen(dst);
    if (dlen >= n - 1) return;
    strncpy_safe(dst + dlen, src, n - dlen);
}

static void resolve_path(const char *input, char *output, size_t max_len) {
    if (!input || !*input) {
        strncpy_safe(output, current_dir, max_len);
        return;
    }

    char temp[VFS_PATH_LEN];
    if (input[0] == '/') {
        strncpy_safe(temp, input, sizeof(temp));
    } else {
        size_t cd_len = strlen(current_dir);
        strncpy_safe(temp, current_dir, sizeof(temp));
        if (cd_len > 0 && current_dir[cd_len - 1] != '/') {
            strcat_safe(temp, "/", sizeof(temp));
        }
        strcat_safe(temp, input, sizeof(temp));
    }

    char buf[VFS_PATH_LEN];
    strncpy_safe(buf, temp, sizeof(buf));

    char *parts[32];
    int part_count = 0;

    char *curr = buf;
    while (*curr == '/') curr++;

    while (*curr) {
        char *start = curr;
        while (*curr && *curr != '/') curr++;
        if (*curr == '/') {
            *curr = '\0';
            curr++;
        }

        if (strcmp(start, ".") == 0 || strlen(start) == 0) {
            /* skip */
        } else if (strcmp(start, "..") == 0) {
            if (part_count > 0) part_count--;
        } else {
            if (part_count < 32) {
                parts[part_count++] = start;
            }
        }
        while (*curr == '/') curr++;
    }

    if (part_count == 0) {
        strncpy_safe(output, "/", max_len);
    } else {
        output[0] = '\0';
        for (int i = 0; i < part_count; i++) {
            strcat_safe(output, "/", max_len);
            strcat_safe(output, parts[i], max_len);
        }
    }
}

static void history_push(const char *line) {
    if (line[0] == '\0') return;
    if (history_len > 0 && strcmp(history_buf[(history_len - 1) % HISTORY_SIZE], line) == 0) return;
    strcpy(history_buf[history_len % HISTORY_SIZE], line);
    history_len++;
    history_idx = history_len;
}

static const char *history_up(void) {
    if (history_len == 0) return 0;
    if (history_idx > 0) history_idx--;
    return history_buf[history_idx % HISTORY_SIZE];
}

static const char *history_down(void) {
    if (history_idx < history_len - 1) {
        history_idx++;
        return history_buf[history_idx % HISTORY_SIZE];
    }
    history_idx = history_len;
    return 0;
}

static void cmd_help(void);
static void cmd_clear(void);
static void cmd_reboot(void);
static void cmd_halt(void);
static void cmd_ps(void);
static void cmd_mem(void);
static void cmd_pci(void);
static void cmd_date(void);
static void cmd_ifconfig(void);
static void cmd_usb(void);
static void cmd_ping(const char *args);
static void cmd_btop(void);
static void cmd_dmesg(void);
static void cmd_neofetch(void);
static void cmd_hyfetch(void);
static void cmd_pwd(void);
static void cmd_cd(const char *args);
static void cmd_ls(const char *args);
static void cmd_cat(const char *args);
static void cmd_touch(const char *args);
static void cmd_mkdir(const char *args);
static void cmd_rm(const char *args);
static void cmd_write(const char *args);
static void cmd_echo(const char *args);
static void cmd_stat(const char *args);
static void cmd_grep(const char *args, const char *pipe_in);
static void cmd_head(const char *args, const char *pipe_in);
static void cmd_tail(const char *args, const char *pipe_in);
static void cmd_wc(const char *args, const char *pipe_in);
static void cmd_uptime(void);

static void print_prompt(void) {
    console_puts("chicago-95", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));
    console_puts(":", CONSOLE_WHITE | (CONSOLE_BLACK << 4));
    console_puts(current_dir, CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_puts("> ", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));
}

static void cmd_help(void) {
    console_puts("Chicago-95 Kernel Shell v0.1.2-Alpha\n", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_puts("────────────────────────────────────\n", CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4));
    console_puts("  help           Show this help\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
    console_puts("  clear          Clear screen\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
    console_puts("  reboot         Reboot system\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
    console_puts("  halt           Halt system\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
    console_puts("  ps             List processes\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
    console_puts("  mem            Memory info\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
    console_puts("  pci            PCI devices\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
    console_puts("  date           Timer ticks / uptime\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
    console_puts("  ifconfig       Network interface info\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
    console_puts("  usb            USB devices\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
    console_puts("  ping <ip>      Ping a host\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
    console_puts("  btop           System monitor\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
    console_puts("  dmesg          Kernel log buffer\n", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_puts("  neofetch       System info display\n", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_puts("  hyfetch        System info with pride flag colors\n", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_puts("  pwd            Print working directory\n", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));
    console_puts("  cd <path>      Change directory\n", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));
    console_puts("  ls [path]      List directory contents\n", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));
    console_puts("  cat <file>     Display file content\n", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));
    console_puts("  touch <file>   Create empty file\n", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));
    console_puts("  mkdir <dir>    Create directory\n", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));
    console_puts("  rm <file>      Remove file\n", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));
    console_puts("  write <f> <t>  Write text to file\n", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));
    console_puts("  echo <text>    Print text (supports > file)\n", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));
    console_puts("  stat <path>    Display file/dir status\n", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));
    console_puts("  grep <pat> [f] Search pattern in file/stream\n", CONSOLE_YELLOW | (CONSOLE_BLACK << 4));
    console_puts("  head [-n N] [f]Print first N lines\n", CONSOLE_YELLOW | (CONSOLE_BLACK << 4));
    console_puts("  tail [-n N] [f]Print last N lines\n", CONSOLE_YELLOW | (CONSOLE_BLACK << 4));
    console_puts("  wc [file]      Count lines, words, bytes\n", CONSOLE_YELLOW | (CONSOLE_BLACK << 4));
    console_puts("  uptime         Display system uptime\n", CONSOLE_YELLOW | (CONSOLE_BLACK << 4));
    console_puts("\nNavigation: Up/Down = history, Ctrl+A = home, Ctrl+E = end\n",
                 CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4));
    console_puts("Pipes & Redirection: 'cmd1 | cmd2', '>' (overwrite), '>>' (append)\n",
                 CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4));
}

static void cmd_pwd(void) {
    console_puts(current_dir, CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
}

static void cmd_cd(const char *args) {
    while (*args == ' ') args++;
    if (*args == '\0') {
        strcpy(current_dir, "/");
        return;
    }

    char target[VFS_PATH_LEN];
    resolve_path(args, target, sizeof(target));

    vfs_node_t *node = vfs_resolve(target);
    if (!node) {
        console_puts("cd: no such file or directory: ", CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(args, CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
        return;
    }

    if (node->type != VFS_DIR) {
        console_puts("cd: not a directory: ", CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(args, CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
        return;
    }

    strcpy(current_dir, target);
}

static void cmd_ls(const char *args) {
    while (*args == ' ') args++;
    char target[VFS_PATH_LEN];
    resolve_path(*args ? args : ".", target, sizeof(target));

    vfs_node_t *dir = vfs_resolve(target);
    if (!dir) {
        console_puts("ls: cannot access ", CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(target, CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(": No such file or directory\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
        return;
    }

    if (dir->type != VFS_DIR) {
        console_printf("  %-16s %8u bytes  [FILE]\n", dir->name, dir->length);
        return;
    }

    console_printf("Directory of %s:\n", target);
    vfs_node_t child;
    uint32_t idx = 0;
    while (vfs_readdir(dir, idx++, &child) == 0) {
        const char *type_str = "FILE";
        uint8_t color = CONSOLE_WHITE;
        if (child.type == VFS_DIR) {
            type_str = "DIR";
            color = CONSOLE_LIGHT_CYAN;
        } else if (child.type == VFS_CHARDEV) {
            type_str = "CHAR";
            color = CONSOLE_YELLOW;
        } else if (child.type == VFS_BLOCKDEV) {
            type_str = "BLOCK";
            color = CONSOLE_LIGHT_RED;
        }

        console_puts("  ", CONSOLE_WHITE | (CONSOLE_BLACK << 4));
        console_puts(child.name, color | (CONSOLE_BLACK << 4));
        int pad = 20 - (int)strlen(child.name);
        if (pad < 1) pad = 1;
        while (pad--) console_putc(' ', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
        console_printf("%8u bytes  [%s]\n", child.length, type_str);
    }
}

static void cmd_cat(const char *args) {
    while (*args == ' ') args++;
    if (*args == '\0') {
        console_puts("Usage: cat <file>\n", CONSOLE_YELLOW | (CONSOLE_BLACK << 4));
        return;
    }

    char target[VFS_PATH_LEN];
    resolve_path(args, target, sizeof(target));

    vfs_node_t *node = vfs_open(target, O_RDONLY);
    if (!node) {
        console_puts("cat: ", CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(target, CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(": No such file\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
        return;
    }

    if (node->type == VFS_DIR) {
        console_puts("cat: ", CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(target, CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(": Is a directory\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
        vfs_close(node);
        return;
    }

    char buf[512];
    uint64_t offset = 0;
    ssize_t n;
    while ((n = vfs_read(node, offset, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        console_puts(buf, CONSOLE_WHITE | (CONSOLE_BLACK << 4));
        offset += n;
    }
    vfs_close(node);
}

static void cmd_touch(const char *args) {
    while (*args == ' ') args++;
    if (*args == '\0') {
        console_puts("Usage: touch <file>\n", CONSOLE_YELLOW | (CONSOLE_BLACK << 4));
        return;
    }

    char target[VFS_PATH_LEN];
    resolve_path(args, target, sizeof(target));

    int res = vfs_create(target, VFS_FILE);
    if (res < 0) {
        console_puts("touch: failed to create ", CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(target, CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
    }
}

static void cmd_mkdir(const char *args) {
    while (*args == ' ') args++;
    if (*args == '\0') {
        console_puts("Usage: mkdir <dir>\n", CONSOLE_YELLOW | (CONSOLE_BLACK << 4));
        return;
    }

    char target[VFS_PATH_LEN];
    resolve_path(args, target, sizeof(target));

    int res = vfs_mkdir(target);
    if (res < 0) {
        console_puts("mkdir: failed to create directory ", CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(target, CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
    }
}

static void cmd_rm(const char *args) {
    while (*args == ' ') args++;
    if (*args == '\0') {
        console_puts("Usage: rm <file>\n", CONSOLE_YELLOW | (CONSOLE_BLACK << 4));
        return;
    }

    char target[VFS_PATH_LEN];
    resolve_path(args, target, sizeof(target));

    int res = vfs_unlink(target);
    if (res < 0) {
        console_puts("rm: failed to remove ", CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(target, CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
    }
}

static void cmd_write(const char *args) {
    while (*args == ' ') args++;
    if (*args == '\0') {
        console_puts("Usage: write <file> <text>\n", CONSOLE_YELLOW | (CONSOLE_BLACK << 4));
        return;
    }

    char file_part[VFS_PATH_LEN];
    int idx = 0;
    while (*args && *args != ' ' && idx < (int)sizeof(file_part) - 1) {
        file_part[idx++] = *args++;
    }
    file_part[idx] = '\0';

    while (*args == ' ') args++;

    char target[VFS_PATH_LEN];
    resolve_path(file_part, target, sizeof(target));

    vfs_node_t *node = vfs_open(target, O_WRONLY | O_CREAT | O_TRUNC);
    if (!node) {
        vfs_create(target, VFS_FILE);
        node = vfs_open(target, O_WRONLY | O_TRUNC);
    }

    if (!node) {
        console_puts("write: failed to open ", CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(target, CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
        return;
    }

    size_t text_len = strlen(args);
    vfs_write(node, 0, args, text_len);
    vfs_write(node, text_len, "\n", 1);
    vfs_close(node);
}

static void cmd_echo(const char *args) {
    while (*args == ' ') args++;
    console_puts(args, CONSOLE_WHITE | (CONSOLE_BLACK << 4));
    console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
}

static void cmd_stat(const char *args) {
    while (*args == ' ') args++;
    if (*args == '\0') {
        console_puts("Usage: stat <path>\n", CONSOLE_YELLOW | (CONSOLE_BLACK << 4));
        return;
    }

    char target[VFS_PATH_LEN];
    resolve_path(args, target, sizeof(target));

    vfs_node_t *node = vfs_resolve(target);
    if (!node) {
        console_puts("stat: cannot stat ", CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(target, CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(": No such file or directory\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
        return;
    }

    console_puts("  File: ", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_puts(node->name, CONSOLE_WHITE | (CONSOLE_BLACK << 4));
    console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));

    console_printf("  Size: %u bytes\t", node->length);
    console_puts("Type: ", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    switch (node->type) {
        case VFS_FILE:     console_puts("Regular File\n", CONSOLE_WHITE | (CONSOLE_BLACK << 4)); break;
        case VFS_DIR:      console_puts("Directory\n", CONSOLE_WHITE | (CONSOLE_BLACK << 4)); break;
        case VFS_CHARDEV:  console_puts("Char Device\n", CONSOLE_WHITE | (CONSOLE_BLACK << 4)); break;
        case VFS_BLOCKDEV: console_puts("Block Device\n", CONSOLE_WHITE | (CONSOLE_BLACK << 4)); break;
        default:           console_puts("Unknown\n", CONSOLE_WHITE | (CONSOLE_BLACK << 4)); break;
    }

    console_printf("  Inode: %u\t\tChildren: %u\n", (uint32_t)node->inode, node->child_count);
}

static void cmd_grep(const char *args, const char *pipe_in) {
    while (*args == ' ') args++;
    if (*args == '\0') {
        console_puts("Usage: grep <pattern> [file]\n", CONSOLE_YELLOW | (CONSOLE_BLACK << 4));
        return;
    }

    char pattern[128];
    int idx = 0;
    while (*args && *args != ' ' && idx < (int)sizeof(pattern) - 1) {
        pattern[idx++] = *args++;
    }
    pattern[idx] = '\0';

    while (*args == ' ') args++;

    const char *source = NULL;
    char *file_buf = NULL;
    vfs_node_t *node = NULL;

    if (*args != '\0') {
        char target[VFS_PATH_LEN];
        resolve_path(args, target, sizeof(target));
        node = vfs_open(target, O_RDONLY);
        if (!node) {
            console_puts("grep: ", CONSOLE_RED | (CONSOLE_BLACK << 4));
            console_puts(target, CONSOLE_RED | (CONSOLE_BLACK << 4));
            console_puts(": No such file\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
            return;
        }

        uint32_t sz = node->length;
        file_buf = (char *)kmalloc(sz + 1);
        if (!file_buf) {
            vfs_close(node);
            console_puts("grep: out of memory\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
            return;
        }
        ssize_t n = vfs_read(node, 0, file_buf, sz);
        if (n < 0) n = 0;
        file_buf[n] = '\0';
        source = file_buf;
    } else if (pipe_in && *pipe_in) {
        source = pipe_in;
    } else {
        console_puts("grep: missing input file or piped stream\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
        return;
    }

    const char *line_start = source;
    while (*line_start) {
        const char *line_end = line_start;
        while (*line_end && *line_end != '\n') line_end++;

        size_t line_len = line_end - line_start;
        char line_tmp[512];
        if (line_len >= sizeof(line_tmp)) line_len = sizeof(line_tmp) - 1;
        memcpy(line_tmp, line_start, line_len);
        line_tmp[line_len] = '\0';

        if (strstr_simple(line_tmp, pattern) != NULL) {
            console_puts(line_tmp, CONSOLE_WHITE | (CONSOLE_BLACK << 4));
            console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
        }

        line_start = line_end;
        if (*line_start == '\n') line_start++;
    }

    if (file_buf) kfree(file_buf);
    if (node) vfs_close(node);
}

static void cmd_head(const char *args, const char *pipe_in) {
    while (*args == ' ') args++;
    int max_lines = 10;

    if (strncmp(args, "-n ", 3) == 0) {
        args += 3;
        while (*args == ' ') args++;
        max_lines = 0;
        while (*args >= '0' && *args <= '9') {
            max_lines = max_lines * 10 + (*args - '0');
            args++;
        }
        while (*args == ' ') args++;
    }

    const char *source = NULL;
    char *file_buf = NULL;
    vfs_node_t *node = NULL;

    if (*args != '\0') {
        char target[VFS_PATH_LEN];
        resolve_path(args, target, sizeof(target));
        node = vfs_open(target, O_RDONLY);
        if (!node) {
            console_puts("head: ", CONSOLE_RED | (CONSOLE_BLACK << 4));
            console_puts(target, CONSOLE_RED | (CONSOLE_BLACK << 4));
            console_puts(": No such file\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
            return;
        }
        uint32_t sz = node->length;
        file_buf = (char *)kmalloc(sz + 1);
        if (!file_buf) {
            vfs_close(node);
            console_puts("head: out of memory\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
            return;
        }
        ssize_t n = vfs_read(node, 0, file_buf, sz);
        if (n < 0) n = 0;
        file_buf[n] = '\0';
        source = file_buf;
    } else if (pipe_in && *pipe_in) {
        source = pipe_in;
    } else {
        console_puts("head: missing input file or piped stream\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
        return;
    }

    int lines_printed = 0;
    const char *p = source;
    while (*p && lines_printed < max_lines) {
        console_putc(*p, CONSOLE_WHITE | (CONSOLE_BLACK << 4));
        if (*p == '\n') lines_printed++;
        p++;
    }
    if (p > source && *(p - 1) != '\n') {
        console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
    }

    if (file_buf) kfree(file_buf);
    if (node) vfs_close(node);
}

static void cmd_tail(const char *args, const char *pipe_in) {
    while (*args == ' ') args++;
    int max_lines = 10;

    if (strncmp(args, "-n ", 3) == 0) {
        args += 3;
        while (*args == ' ') args++;
        max_lines = 0;
        while (*args >= '0' && *args <= '9') {
            max_lines = max_lines * 10 + (*args - '0');
            args++;
        }
        while (*args == ' ') args++;
    }

    const char *source = NULL;
    char *file_buf = NULL;
    vfs_node_t *node = NULL;

    if (*args != '\0') {
        char target[VFS_PATH_LEN];
        resolve_path(args, target, sizeof(target));
        node = vfs_open(target, O_RDONLY);
        if (!node) {
            console_puts("tail: ", CONSOLE_RED | (CONSOLE_BLACK << 4));
            console_puts(target, CONSOLE_RED | (CONSOLE_BLACK << 4));
            console_puts(": No such file\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
            return;
        }
        uint32_t sz = node->length;
        file_buf = (char *)kmalloc(sz + 1);
        if (!file_buf) {
            vfs_close(node);
            console_puts("tail: out of memory\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
            return;
        }
        ssize_t n = vfs_read(node, 0, file_buf, sz);
        if (n < 0) n = 0;
        file_buf[n] = '\0';
        source = file_buf;
    } else if (pipe_in && *pipe_in) {
        source = pipe_in;
    } else {
        console_puts("tail: missing input file or piped stream\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
        return;
    }

    int total_lines = 0;
    for (const char *p = source; *p; p++) {
        if (*p == '\n') total_lines++;
    }

    int skip_lines = total_lines - max_lines;
    if (skip_lines < 0) skip_lines = 0;

    int current_line = 0;
    const char *p = source;
    while (*p && current_line < skip_lines) {
        if (*p == '\n') current_line++;
        p++;
    }

    while (*p) {
        console_putc(*p, CONSOLE_WHITE | (CONSOLE_BLACK << 4));
        p++;
    }
    if (p > source && *(p - 1) != '\n') {
        console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
    }

    if (file_buf) kfree(file_buf);
    if (node) vfs_close(node);
}

static void cmd_wc(const char *args, const char *pipe_in) {
    while (*args == ' ') args++;
    const char *source = NULL;
    char *file_buf = NULL;
    vfs_node_t *node = NULL;
    const char *label = "stream";

    if (*args != '\0') {
        char target[VFS_PATH_LEN];
        resolve_path(args, target, sizeof(target));
        node = vfs_open(target, O_RDONLY);
        if (!node) {
            console_puts("wc: ", CONSOLE_RED | (CONSOLE_BLACK << 4));
            console_puts(target, CONSOLE_RED | (CONSOLE_BLACK << 4));
            console_puts(": No such file\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
            return;
        }
        uint32_t sz = node->length;
        file_buf = (char *)kmalloc(sz + 1);
        if (!file_buf) {
            vfs_close(node);
            console_puts("wc: out of memory\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
            return;
        }
        ssize_t n = vfs_read(node, 0, file_buf, sz);
        if (n < 0) n = 0;
        file_buf[n] = '\0';
        source = file_buf;
        label = node->name;
    } else if (pipe_in && *pipe_in) {
        source = pipe_in;
    } else {
        console_puts("wc: missing input file or piped stream\n", CONSOLE_RED | (CONSOLE_BLACK << 4));
        return;
    }

    uint32_t lines = 0, words = 0, bytes = 0;
    int in_word = 0;
    for (const char *p = source; *p; p++) {
        bytes++;
        if (*p == '\n') lines++;
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }

    console_printf("  %u  %u  %u  %s\n", lines, words, bytes, label);

    if (file_buf) kfree(file_buf);
    if (node) vfs_close(node);
}

static void cmd_uptime(void) {
    uint64_t sec = timer_get_seconds();
    uint64_t ticks = timer_get_ticks();
    uint32_t hrs = (uint32_t)(sec / 3600);
    uint32_t mins = (uint32_t)((sec % 3600) / 60);
    uint32_t secs = (uint32_t)(sec % 60);

    console_puts("System Uptime: ", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_printf("%uh %um %us (%u ticks @ 100Hz)\n", hrs, mins, secs, (uint32_t)ticks);
}

static void cmd_clear(void) {
    console_clear();
}

static void cmd_reboot(void) {
    kmsg_warn("User initiated reboot");
    outb(0x64, 0xFE);
    while (1) hlt();
}

static void cmd_halt(void) {
    kmsg_warn("User initiated halt");
    console_puts("System halted.\n", CONSOLE_YELLOW | (CONSOLE_BLACK << 4));
    cli();
    while (1) hlt();
}

static void cmd_ps(void) {
    console_puts("  PID  STATE     RING  NAME\n", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    for (int i = 0; i < 16; i++) {
        if (processes[i].state != PROC_UNUSED) {
            console_printf("  %d    ", processes[i].pid);
            switch (processes[i].state) {
                case PROC_READY:   console_puts("READY   ", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4)); break;
                case PROC_RUNNING: console_puts("RUNNING ", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4)); break;
                case PROC_BLOCKED: console_puts("BLOCKED ", CONSOLE_YELLOW | (CONSOLE_BLACK << 4)); break;
                case PROC_ZOMBIE:  console_puts("ZOMBIE  ", CONSOLE_RED | (CONSOLE_BLACK << 4)); break;
                default:           console_puts("???     ", CONSOLE_RED | (CONSOLE_BLACK << 4)); break;
            }
            console_printf("  %s   ", processes[i].ring == 0 ? "R0" : "R3");
            console_puts(processes[i].name, CONSOLE_WHITE | (CONSOLE_BLACK << 4));
            console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
        }
    }
}

static void cmd_mem(void) {
    uint64_t total = pmm_get_total_pages();
    uint64_t free = pmm_get_free_pages();
    uint64_t used = total - free;
    uint64_t total_kb = (total * 4096) / 1024;
    uint64_t used_kb = (used * 4096) / 1024;
    uint64_t free_kb = (free * 4096) / 1024;
    uint32_t pct = total ? (uint32_t)(used * 100 / total) : 0;

    console_puts("Memory:\n", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_printf("  Total: %u KB (%u pages)\n", total_kb, (uint32_t)total);
    console_printf("  Used:  %u KB (%u%%)\n", used_kb, pct);
    console_printf("  Free:  %u KB\n", free_kb);

    /* Draw usage bar */
    console_puts("  [", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
    int bars = pct / 5;
    for (int i = 0; i < 20; i++) {
        if (i < bars)
            console_putc('|', CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));
        else
            console_putc('.', CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4));
    }
    console_puts("] ", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
    console_printf("%u%%\n", pct);
}

static void cmd_pci(void) {
    int count = pci_get_device_count();
    if (count == 0) {
        console_puts("No PCI devices found.\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
        return;
    }
    console_puts("  Bus:Slot  Vendor  Device  Class.Sub\n", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    for (int i = 0; i < count; i++) {
        pci_device_t *dev = pci_get_device(i);
        if (dev) {
            console_printf("  %d:%d      %x    %x    %d.%d\n",
                dev->bus, dev->slot, dev->vendor_id, dev->device_id,
                dev->class, dev->subclass);
        }
    }
}

static void cmd_date(void) {
    uint64_t ticks = timer_get_ticks();
    uint64_t seconds = timer_get_seconds();
    uint32_t hrs = (uint32_t)(seconds / 3600);
    uint32_t mins = (uint32_t)((seconds % 3600) / 60);
    uint32_t secs = (uint32_t)(seconds % 60);
    console_printf("Uptime: %uh %um %us (%u ticks)\n", hrs, mins, secs, (uint32_t)ticks);
}

static uint32_t parse_ip(const char *s) {
    uint32_t a = 0, b = 0, c = 0, d = 0;
    while (*s >= '0' && *s <= '9') a = a * 10 + (*s++ - '0');
    if (*s == '.') s++;
    while (*s >= '0' && *s <= '9') b = b * 10 + (*s++ - '0');
    if (*s == '.') s++;
    while (*s >= '0' && *s <= '9') c = c * 10 + (*s++ - '0');
    if (*s == '.') s++;
    while (*s >= '0' && *s <= '9') d = d * 10 + (*s++ - '0');
    return a | (b << 8) | (c << 16) | (d << 24);
}

static void cmd_ifconfig(void) {
    uint32_t ip = net_get_ip();
    uint32_t mask = net_get_mask();
    uint32_t gw = net_get_gateway();
    uint8_t mac[6];
    net_get_mac(mac);

    console_puts("eth0:\n", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_printf("  IP:      %d.%d.%d.%d\n", ip & 0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF);
    console_printf("  Mask:    %d.%d.%d.%d\n", mask & 0xFF, (mask>>8)&0xFF, (mask>>16)&0xFF, (mask>>24)&0xFF);
    console_printf("  Gateway: %d.%d.%d.%d\n", gw & 0xFF, (gw>>8)&0xFF, (gw>>16)&0xFF, (gw>>24)&0xFF);
    console_printf("  MAC:     %x:%x:%x:%x:%x:%x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    console_printf("  Status:  %s\n", e1000_is_ready() ? "UP" : "DOWN");
}

static void cmd_usb(void) {
    int ports = xhci.max_ports;
    console_printf("xHCI: %d port(s)\n", ports);
    int devs = usb_get_device_count();
    console_printf("USB devices: %d\n", devs);
    if (devs == 0) {
        console_puts("  (none)\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
    }
}

static void cmd_ping(const char *args) {
    while (*args == ' ') args++;
    if (*args == '\0') {
        console_puts("Usage: ping <ip>\n", CONSOLE_YELLOW | (CONSOLE_BLACK << 4));
        return;
    }
    uint32_t dst = parse_ip(args);
    console_printf("PING %d.%d.%d.%d:\n", dst&0xFF,(dst>>8)&0xFF,(dst>>16)&0xFF,(dst>>24)&0xFF);

    int received = 0;
    for (int i = 0; i < 4; i++) {
        net_send_ping(dst, 0x4395, i + 1);

        for (volatile int j = 0; j < 5000000; j++) {
            net_poll();
            if (net_ping_reply_rx()) {
                console_printf("  64 bytes from %d.%d.%d.%d: icmp_seq=%d\n",
                    dst&0xFF,(dst>>8)&0xFF,(dst>>16)&0xFF,(dst>>24)&0xFF, i+1);
                received++;
                goto next;
            }
        }
        console_puts("  timeout\n", CONSOLE_YELLOW | (CONSOLE_BLACK << 4));
next:   ;
    }
    console_printf("\n%d packets transmitted, %d received\n", 4, received);
}

static void cmd_btop(void) {
    btop_run();
}

static void cmd_dmesg(void) {
    uint32_t count = kmsg_count();
    if (count == 0) {
        console_puts("No kernel messages.\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        kmsg_entry_t *e = kmsg_get(i);
        if (!e) continue;

        /* Timestamp: ticks -> seconds */
        uint64_t sec = e->timestamp / 100;
        uint32_t hrs = (uint32_t)(sec / 3600);
        uint32_t mins = (uint32_t)((sec % 3600) / 60);
        uint32_t secs = (uint32_t)(sec % 60);

        console_printf("[%u:%02u:%02u] ", hrs, mins, secs);

        uint8_t lvl_color = kmsg_level_color(e->level);
        console_puts(kmsg_level_str(e->level), lvl_color);
        console_puts(" ", CONSOLE_WHITE | (CONSOLE_BLACK << 4));
        console_puts(e->msg, CONSOLE_WHITE | (CONSOLE_BLACK << 4));
        console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
    }
}

static void fmt_disk(uint64_t bytes) {
    if (bytes >= (1ULL << 30))
        console_printf("%u.%u GiB", (uint32_t)(bytes >> 30), (uint32_t)(((bytes >> 20) & 1023) * 10 / 1024));
    else if (bytes >= (1ULL << 20))
        console_printf("%u MiB", (uint32_t)(bytes >> 20));
    else if (bytes >= (1ULL << 10))
        console_printf("%u KiB", (uint32_t)(bytes >> 10));
    else
        console_printf("%u B", (uint32_t)bytes);
}

static void nf_label(const char *label) {
    int len = 0;
    while (label[len]) len++;
    for (int i = 0; i < 38 + (8 - len); i++) console_putc(' ', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
    console_puts(label, CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_putc(' ', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
}

static void cmd_fetch(int hy) {
    uint32_t ip = net_get_ip();
    uint64_t total_pages = pmm_get_total_pages();
    uint64_t free_pages = pmm_get_free_pages();
    uint64_t used_pages = total_pages - free_pages;
    uint32_t total_mb = (uint32_t)((total_pages * 4096) / (1024 * 1024));
    uint32_t used_mb = (uint32_t)((used_pages * 4096) / (1024 * 1024));
    uint32_t pct = total_pages ? (uint32_t)((used_pages * 100) / total_pages) : 0;
    uint64_t seconds = timer_get_seconds();
    uint32_t hrs = (uint32_t)(seconds / 3600);
    uint32_t mins = (uint32_t)((seconds % 3600) / 60);
    uint32_t secs = (uint32_t)(seconds % 60);
    int pci_count = pci_get_device_count();
    int usb_count = usb_get_device_count();

    uint32_t eax, ebx, ecx, edx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    uint32_t ebx1;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx1) : "a"(1) : "ecx", "edx");
    int cores = (int)((ebx1 >> 16) & 0xFF);
    if (cores == 0) cores = 1;

    char brand[49] = {0};
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000002));
    *((uint32_t*)&brand[0]) = eax;
    *((uint32_t*)&brand[4]) = ebx;
    *((uint32_t*)&brand[8]) = ecx;
    *((uint32_t*)&brand[12]) = edx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000003));
    *((uint32_t*)&brand[16]) = eax;
    *((uint32_t*)&brand[20]) = ebx;
    *((uint32_t*)&brand[24]) = ecx;
    *((uint32_t*)&brand[28]) = edx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000004));
    *((uint32_t*)&brand[32]) = eax;
    *((uint32_t*)&brand[36]) = ebx;
    *((uint32_t*)&brand[40]) = ecx;
    *((uint32_t*)&brand[44]) = edx;

    const char *cpu_name = brand;
    while (*cpu_name == ' ') cpu_name++;
    char cpu[33];
    int ci = 0;
    while (cpu_name[ci] && ci < 32) { cpu[ci] = cpu_name[ci]; ci++; }
    cpu[ci] = 0;

    int procs = 0;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (processes[i].state != PROC_UNUSED) procs++;

    int disk_count = 0;
    uint64_t disk_bytes = 0;
    for (int i = 0; i < 4; i++) {
        ata_info_t *d = ata_get_info(i);
        if (d) {
            disk_count++;
            uint64_t lba = d->lba48 ? d->max_lba48 : d->max_lba;
            disk_bytes += lba * (d->sector_size ? d->sector_size : 512);
        }
    }

    uint8_t white = CONSOLE_WHITE | (CONSOLE_BLACK << 4);
    uint8_t green = CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4);
    uint8_t yellow = CONSOLE_YELLOW | (CONSOLE_BLACK << 4);
    uint8_t grey = CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4);
    uint8_t dim = CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4);

    /* HyFetch pride-flag palette (rainbow), mapped to VGA 16 colors:
     *   #E50000 red, #FF8D00 orange, #FFEE00 yellow,
     *   #028121 green, #004CFF blue, #770088 purple */
    static const uint8_t pride_fg[6] = {
        CONSOLE_RED | (CONSOLE_BLACK << 4),
        CONSOLE_BROWN | (CONSOLE_BLACK << 4),
        CONSOLE_YELLOW | (CONSOLE_BLACK << 4),
        CONSOLE_GREEN | (CONSOLE_BLACK << 4),
        CONSOLE_BLUE | (CONSOLE_BLACK << 4),
        CONSOLE_MAGENTA | (CONSOLE_BLACK << 4)
    };
    static const uint8_t pride_bg[6] = {
        CONSOLE_BLACK | (CONSOLE_RED << 4),
        CONSOLE_BLACK | (CONSOLE_BROWN << 4),
        CONSOLE_BLACK | (CONSOLE_YELLOW << 4),
        CONSOLE_BLACK | (CONSOLE_GREEN << 4),
        CONSOLE_BLACK | (CONSOLE_BLUE << 4),
        CONSOLE_BLACK | (CONSOLE_MAGENTA << 4)
    };

    enum { SKY_W = 36, SKY_H = 12 };
    char sky[SKY_H][SKY_W + 1];
    for (int r = 0; r < SKY_H; r++) {
        for (int c = 0; c < SKY_W; c++) sky[r][c] = ' ';
        sky[r][SKY_W] = 0;
    }
    static const uint8_t sp[6][3] = {
        {0, 4, 5}, {5, 5, 8}, {11, 7, 10}, {19, 6, 9}, {26, 4, 6}, {31, 4, 4}
    };
    for (int b = 0; b < 6; b++) {
        int sx = sp[b][0], w = sp[b][1], h = sp[b][2];
        for (int r = 0; r < h; r++) {
            int row = 10 - r;
            for (int c = 0; c < w; c++) {
                int x = sx + c;
                char ch = (char)('0' + b);
                if (r != h - 1 && ((x * 7 + row * 13 + b * 3) % 11) == 0) ch = 'o';
                sky[row][x] = ch;
            }
        }
    }
    for (int c = 0; c < SKY_W; c++) sky[11][c] = '-';
    sky[0][14] = '*';

    for (int r = 0; r < SKY_H; r++) {
        int pride_idx = (r - 3) * 6 / 8;   /* gradient across building rows 3..10 */
        if (pride_idx < 0) pride_idx = 0;
        if (pride_idx > 5) pride_idx = 5;
        for (int c = 0; c < SKY_W; c++) {
            char ch = sky[r][c];
            uint8_t cl = 0;
            if (ch >= '0' && ch <= '5')
                cl = hy ? pride_fg[pride_idx]
                        : (((ch - '0') & 1) ? (CONSOLE_BLUE | (CONSOLE_BLACK << 4)) : (CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4)));
            else if (ch == 'o') cl = CONSOLE_YELLOW | (CONSOLE_BLACK << 4);
            else if (ch == '-') cl = CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4);
            else if (ch == '*' || ch == '|') cl = CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4);
            console_putc((ch >= '0' && ch <= '5') || ch == 'o' ? 0xDB : ch, cl);
        }
        switch (r) {
        case 0: nf_label("OS"); console_puts("Chicago-95", white); console_puts("  BrainFS x86_64", grey); break;
        case 1: nf_label("Host"); console_puts("Bare-metal x86_64", white); break;
        case 2: nf_label("Kernel"); console_puts("0.1.2-Alpha", yellow); break;
        case 3: nf_label("Uptime"); console_printf("%uh %um %us", hrs, mins, secs); break;
        case 4: nf_label("Shell"); console_puts("bfsh 0.1 (cmd)", white); break;
        case 5: nf_label("Terminal"); console_puts("VGA 80x25", white); break;
        case 6: nf_label("CPU"); console_printf("%s (%d cores)", cpu, cores); break;
        case 7: nf_label("Memory"); console_printf("%u MiB / %u MiB ", used_mb, total_mb);
                console_putc('[', grey);
                for (int i = 0; i < 10; i++)
                    console_putc(0xDB, (i * 10 < (int)pct) ? (CONSOLE_YELLOW | (CONSOLE_BLACK << 4)) : (CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4)));
                console_putc(']', grey);
                console_printf(" %u%%", pct);
                break;
        case 8: nf_label("Disk"); console_printf("%d drive(s) ", disk_count); fmt_disk(disk_bytes); break;
        case 9: nf_label("NIC");
                if (e1000_is_ready()) {
                    console_puts("e1000 UP", green);
                    console_printf(" (%d.%d.%d.%d)", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
                } else {
                    console_puts("none", dim);
                }
                break;
        case 10: nf_label("PCI"); console_printf("%d device(s)", pci_count); break;
        case 11: nf_label("USB"); console_printf("%d device(s)", usb_count); break;
        }
        console_putc('\n', white);
    }

    nf_label("Procs"); console_printf("%d running", procs);
    console_putc('\n', white);
    nf_label("Boot"); console_puts("BIOS", white);
    console_putc('\n', white);
    nf_label("Theme"); console_puts("Chicago-95", white);
    console_putc('\n', white);

    console_putc('\n', white);
    for (int i = 0; i < 38; i++) console_putc(' ', white);
    for (int i = 0; i < 31; i++) console_putc('-', dim);
    console_putc('\n', dim);
    for (int i = 0; i < 38; i++) console_putc(' ', white);
    if (hy) {
        for (int i = 0; i < 6; i++)
            for (int b = 0; b < 6; b++) console_putc(' ', pride_bg[i]);
        console_puts("  chicago-95 bootloader", grey);
    } else {
        for (int i = 0; i < 8; i++) console_putc(' ', CONSOLE_BLACK | (i << 4));
        for (int i = 8; i < 16; i++) console_putc(' ', CONSOLE_BLACK | (i << 4));
        console_puts("  chicago-95 bootloader", grey);
    }
    console_putc('\n', white);
}

static void cmd_neofetch(void) { cmd_fetch(0); }
static void cmd_hyfetch(void) { cmd_fetch(1); }

static int dispatch_single_cmd(const char *cmd, const char *pipe_in) {
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return 0;

    if (strcmp(cmd, "help") == 0) cmd_help();
    else if (strcmp(cmd, "clear") == 0) cmd_clear();
    else if (strcmp(cmd, "reboot") == 0) cmd_reboot();
    else if (strcmp(cmd, "halt") == 0) cmd_halt();
    else if (strcmp(cmd, "ps") == 0) cmd_ps();
    else if (strcmp(cmd, "mem") == 0) cmd_mem();
    else if (strcmp(cmd, "pci") == 0) cmd_pci();
    else if (strcmp(cmd, "date") == 0) cmd_date();
    else if (strcmp(cmd, "ifconfig") == 0) cmd_ifconfig();
    else if (strcmp(cmd, "usb") == 0 || strcmp(cmd, "lsusb") == 0) cmd_usb();
    else if (strncmp(cmd, "ping ", 5) == 0) cmd_ping(cmd + 5);
    else if (strcmp(cmd, "btop") == 0) cmd_btop();
    else if (strcmp(cmd, "dmesg") == 0) cmd_dmesg();
    else if (strcmp(cmd, "neofetch") == 0) cmd_neofetch();
    else if (strcmp(cmd, "hyfetch") == 0) cmd_hyfetch();
    else if (strcmp(cmd, "pwd") == 0) cmd_pwd();
    else if (strncmp(cmd, "cd ", 3) == 0 || strcmp(cmd, "cd") == 0) cmd_cd(cmd + 2);
    else if (strncmp(cmd, "ls ", 3) == 0 || strcmp(cmd, "ls") == 0) cmd_ls(cmd + 2);
    else if (strncmp(cmd, "cat ", 4) == 0) cmd_cat(cmd + 4);
    else if (strncmp(cmd, "touch ", 6) == 0) cmd_touch(cmd + 6);
    else if (strncmp(cmd, "mkdir ", 6) == 0) cmd_mkdir(cmd + 6);
    else if (strncmp(cmd, "rm ", 3) == 0) cmd_rm(cmd + 3);
    else if (strncmp(cmd, "write ", 6) == 0) cmd_write(cmd + 6);
    else if (strncmp(cmd, "echo ", 5) == 0) cmd_echo(cmd + 5);
    else if (strncmp(cmd, "stat ", 5) == 0) cmd_stat(cmd + 5);
    else if (strncmp(cmd, "grep ", 5) == 0 || strcmp(cmd, "grep") == 0) cmd_grep(cmd + 4, pipe_in);
    else if (strncmp(cmd, "head ", 5) == 0 || strcmp(cmd, "head") == 0) cmd_head(cmd + 4, pipe_in);
    else if (strncmp(cmd, "tail ", 5) == 0 || strcmp(cmd, "tail") == 0) cmd_tail(cmd + 4, pipe_in);
    else if (strncmp(cmd, "wc ", 3) == 0 || strcmp(cmd, "wc") == 0) cmd_wc(cmd + 2, pipe_in);
    else if (strcmp(cmd, "uptime") == 0) cmd_uptime();
    else {
        console_puts("Unknown command: ", CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(cmd, CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts("\nType 'help' for available commands.\n", CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4));
        return -1;
    }
    return 0;
}

static int process_command(const char *cmd_input) {
    char cmd_copy[256];
    strncpy_safe(cmd_copy, cmd_input, sizeof(cmd_copy));

    char *cmd = cmd_copy;
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return 0;

    /* Check for pipe operator '|' */
    char *pipe_pos = NULL;
    for (char *p = cmd; *p; p++) {
        if (*p == '|') {
            pipe_pos = p;
            break;
        }
    }

    if (pipe_pos) {
        *pipe_pos = '\0';
        char *cmd1 = cmd;
        char *cmd2 = pipe_pos + 1;

        pipe_buffer_len = 0;
        pipe_buffer[0] = '\0';

        console_set_redirect(shell_pipe_writer);
        dispatch_single_cmd(cmd1, NULL);
        console_set_redirect(NULL);

        return dispatch_single_cmd(cmd2, pipe_buffer);
    }

    /* Check for redirection operators > or >> */
    char *redir = 0;
    int append_mode = 0;

    char *p = cmd;
    while (*p) {
        if (p[0] == '>' && p[1] == '>') {
            redir = p;
            append_mode = 1;
            break;
        } else if (p[0] == '>' && p[1] != '>') {
            if (!redir) {
                redir = p;
                append_mode = 0;
            }
        }
        p++;
    }

    vfs_node_t *out_node = NULL;

    if (redir) {
        *redir = '\0';
        char *outfile = redir + (append_mode ? 2 : 1);
        while (*outfile == ' ') outfile++;

        if (*outfile != '\0') {
            char target[VFS_PATH_LEN];
            resolve_path(outfile, target, sizeof(target));

            uint32_t flags = O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC);
            out_node = vfs_open(target, flags);
            if (!out_node) {
                vfs_create(target, VFS_FILE);
                out_node = vfs_open(target, flags);
            }

            if (out_node) {
                redir_file_node = out_node;
                redir_file_offset = append_mode ? out_node->length : 0;
                console_set_redirect(shell_redirect_writer);
            } else {
                console_puts("shell: failed to open output file: ", CONSOLE_RED | (CONSOLE_BLACK << 4));
                console_puts(target, CONSOLE_RED | (CONSOLE_BLACK << 4));
                console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
                return -1;
            }
        }
    }

    /* Trim trailing spaces */
    int len = (int)strlen(cmd);
    while (len > 0 && cmd[len - 1] == ' ') {
        cmd[--len] = '\0';
    }

    int ret = dispatch_single_cmd(cmd, NULL);

    if (redir && out_node) {
        console_set_redirect(NULL);
        vfs_close(out_node);
        redir_file_node = NULL;
        redir_file_offset = 0;
    }

    return ret;
}

void shell_main(void) {
    console_puts("\nChicago-95 Shell v1.0\n", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_puts("Type 'help' for available commands.\n\n", CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4));

    while (1) {
        line_pos = 0;
        line_buffer[0] = '\0';
        history_idx = history_len;
        print_prompt();

        while (1) {
            int c = keyboard_getchar();
            if (c < 0) {
                __asm__ volatile ("hlt");
                continue;
            }

            if (c == '\n') {
                console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
                line_buffer[line_pos] = '\0';
                history_push(line_buffer);
                process_command(line_buffer);
                break;
            } else if (c == '\b') {
                if (line_pos > 0) {
                    line_pos--;
                    line_buffer[line_pos] = '\0';
                    console_putc('\b', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
                }
            } else if (c == 1) {
                /* Ctrl+A: move to start */
                while (line_pos > 0) {
                    line_pos--;
                    console_putc('\b', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
                }
            } else if (c == 5) {
                /* Ctrl+E: move to end */
                while (line_buffer[line_pos] != '\0' && line_pos < 255) {
                    console_putc(line_buffer[line_pos], CONSOLE_WHITE | (CONSOLE_BLACK << 4));
                    line_pos++;
                }
            } else if (c == 11) {
                /* Ctrl+K: kill to end */
                console_puts("        \b\b\b\b\b\b\b\b", CONSOLE_WHITE | (CONSOLE_BLACK << 4));
                line_buffer[line_pos] = '\0';
            } else if (c == 21) {
                /* Ctrl+U: kill to start */
                while (line_pos > 0) {
                    line_pos--;
                    line_buffer[line_pos] = ' ';
                    console_putc('\b', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
                }
                line_buffer[0] = '\0';
            } else if (c == 12) {
                /* Ctrl+L: clear */
                console_clear();
                print_prompt();
                for (int i = 0; i < line_pos; i++)
                    console_putc(line_buffer[i], CONSOLE_WHITE | (CONSOLE_BLACK << 4));
            } else if (c == 0x48) {
                /* Up arrow: history previous */
                const char *prev = history_up();
                if (prev) {
                    /* Clear current line */
                    while (line_pos > 0) {
                        line_pos--;
                        console_putc('\b', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
                    }
                    strcpy(line_buffer, prev);
                    line_pos = strlen(line_buffer);
                    console_puts(line_buffer, CONSOLE_WHITE | (CONSOLE_BLACK << 4));
                }
            } else if (c == 0x50) {
                /* Down arrow: history next */
                const char *next = history_down();
                /* Clear current line */
                while (line_pos > 0) {
                    line_pos--;
                    console_putc('\b', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
                }
                if (next) {
                    strcpy(line_buffer, next);
                    line_pos = strlen(line_buffer);
                } else {
                    line_buffer[0] = '\0';
                    line_pos = 0;
                }
                console_puts(line_buffer, CONSOLE_WHITE | (CONSOLE_BLACK << 4));
            } else if (c >= 32 && c < 127 && line_pos < 255) {
                line_buffer[line_pos++] = c;
                line_buffer[line_pos] = '\0';
                console_putc(c, CONSOLE_WHITE | (CONSOLE_BLACK << 4));
            }
        }
    }
}
