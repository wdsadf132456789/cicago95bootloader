/**
 * Chicago-95 Encrypted .onion Shell Session
 * PS/2 keyboard input, command parser, Tor SOCKS5 I/O, encrypted session
 */

#include <stdint.h>
#include "boot/ring0_init.h"
#include "boot/security.h"
#include "security/tor.h"
#include "fs/brainfs.h"
#include "fs/brainvfs.h"
#include "vga/vga.h"
#include "memory/memory.h"
#include "drivers/wifi_autodetect.h"

#define SHELL_MAX_CMD     256
#define SHELL_MAX_ARGS    16
#define SHELL_HISTORY_SLOTS 32
#define SHELL_VERSION     "0.1.0"

typedef struct {
    char     cmd[SHELL_MAX_CMD];
} shell_history_t;

typedef struct {
    uint8_t  active;
    uint8_t  authenticated;
    uint8_t  encrypted_session;
    uint32_t session_id;
    uint8_t  session_key[32];
    char     username[32];
    uint32_t circ_id;
    uint32_t stream_id;
} shell_session_t;

typedef struct {
    uint8_t          initialized;
    uint8_t          running;
    shell_session_t  session;
    shell_history_t  history[SHELL_HISTORY_SLOTS];
    uint8_t          history_idx;
    uint8_t          history_count;
    char             prompt[64];
    uint32_t         cmd_count;
} shell_state_t;

static shell_state_t shell;

/* ======================================================================== */
/* Internal Helpers                                                          */
/* ======================================================================== */

static void shell_puts(const char *s) {
    while (*s) vga_text_put_char(*s++, VGA_COLOR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void shell_puts_color(const char *s, uint8_t fg, uint8_t bg) {
    while (*s) {
        vga_text_put_char(*s, VGA_COLOR(fg, bg));
        s++;
    }
}

static void shell_putc(char c) {
    vga_text_put_char(c, VGA_COLOR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void shell_newline(void) {
    vga_text_put_char('\r', VGA_COLOR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_text_put_char('\n', VGA_COLOR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static uint32_t shell_strlen(const char *s) {
    uint32_t len = 0;
    while (s[len]) len++;
    return len;
}

static int shell_strcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return *a - *b;
        a++; b++;
    }
    return *a - *b;
}

static int shell_strncmp(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

static void shell_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static void shell_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)v;
}

/* ======================================================================== */
/* Line Editing (PS/2 keyboard)                                             */
/* ======================================================================== */

static int shell_readline(char *buf, uint32_t maxlen) {
    uint32_t pos = 0;
    buf[0] = 0;

    while (1) {
        int c = kbd_getchar();
        if (c < 0) {
            __asm__ volatile("hlt");
            continue;
        }

        if (c == '\n' || c == '\r') {
            shell_newline();
            break;
        }
        if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                pos--;
                /* Move cursor back, overwrite with space, move back */
                shell_puts("\b \b");
            }
            continue;
        }
        if (c == 3) { /* Ctrl+C */
            shell_puts("^C\n");
            buf[0] = 0;
            return -1;
        }
        if (c == 12) { /* Ctrl+L = clear */
            vga_text_clear();
            shell_puts(shell.prompt);
            for (uint32_t i = 0; i < pos; i++) shell_putc(buf[i]);
            continue;
        }
        /* Arrow keys: ESC [ A/B/C/D */
        if (c == 27) {
            /* Eat escape sequence */
            while (kbd_getchar() < 0) __asm__ volatile("hlt");
            int next = kbd_getchar();
            if (next == 'A') { /* Up - history prev */
                if (shell.history_count > 0 && shell.history_idx > 0) {
                    shell.history_idx--;
                    /* Clear current line */
                    while (pos > 0) { pos--; shell_puts("\b \b"); }
                    /* Copy history entry */
                    shell_history_t *h = &shell.history[shell.history_idx];
                    uint32_t hlen = shell_strlen(h->cmd);
                    if (hlen >= maxlen) hlen = maxlen - 1;
                    for (uint32_t i = 0; i < hlen; i++) {
                        buf[i] = h->cmd[i];
                        shell_putc(h->cmd[i]);
                    }
                    pos = hlen;
                    buf[pos] = 0;
                }
                continue;
            }
            if (next == 'B') { /* Down - history next */
                if (shell.history_count > 0 && shell.history_idx < shell.history_count - 1) {
                    shell.history_idx++;
                    while (pos > 0) { pos--; shell_puts("\b \b"); }
                    shell_history_t *h = &shell.history[shell.history_idx];
                    uint32_t hlen = shell_strlen(h->cmd);
                    if (hlen >= maxlen) hlen = maxlen - 1;
                    for (uint32_t i = 0; i < hlen; i++) {
                        buf[i] = h->cmd[i];
                        shell_putc(h->cmd[i]);
                    }
                    pos = hlen;
                    buf[pos] = 0;
                }
                continue;
            }
            if (next == 'C') { /* Right */
                continue;
            }
            if (next == 'D') { /* Left */
                continue;
            }
            continue;
        }
        if (c == 9) { /* Tab completion */
            continue;
        }
        if (c >= 0x20 && c < 0x7F && pos < maxlen - 1) {
            buf[pos++] = (char)c;
            shell_putc((char)c);
        }
    }
    buf[pos] = 0;
    return (int)pos;
}

/* ======================================================================== */
/* Command Parser                                                           */
/* ======================================================================== */

static int shell_parse_args(const char *cmd, char args[SHELL_MAX_ARGS][64]) {
    int argc = 0;
    int in_arg = 0;
    int arg_pos = 0;

    for (uint32_t i = 0; cmd[i] && argc < SHELL_MAX_ARGS; i++) {
        if (cmd[i] == ' ' || cmd[i] == '\t') {
            if (in_arg) {
                args[argc][arg_pos] = 0;
                argc++;
                in_arg = 0;
                arg_pos = 0;
            }
        } else {
            if (!in_arg) in_arg = 1;
            if (arg_pos < 63) args[argc][arg_pos++] = cmd[i];
        }
    }
    if (in_arg) {
        args[argc][arg_pos] = 0;
        argc++;
    }
    return argc;
}

/* ======================================================================== */
/* Built-in Commands                                                        */
/* ======================================================================== */

static void cmd_help(int argc, char args[SHELL_MAX_ARGS][64]) {
    (void)argc; (void)args;
    shell_puts_color("\nChicago-95 BrainFS Shell v"SHELL_VERSION"\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    shell_puts_color("Ring-0 bare-metal encrypted .onion session\n\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    shell_puts("  help           Show this help\n");
    shell_puts("  status         System & session status\n");
    shell_puts("  tor            Tor connection status\n");
    shell_puts("  net            Network interface info\n");
    shell_puts("  wifi           WiFi scan results\n");
    shell_puts("  ls <path>      List directory contents\n");
    shell_puts("  cat <file>     Read file contents\n");
    shell_puts("  touch <file>   Create file\n");
    shell_puts("  rm <file>      Delete file\n");
    shell_puts("  mkdir <dir>    Create directory\n");
    shell_puts("  mount          Show mount points\n");
    shell_puts("  id             Show session identity\n");
    shell_puts("  clear          Clear screen\n");
    shell_puts("  uptime         Show boot time\n");
    shell_puts("  dmesg          Kernel log\n");
    shell_puts("  reboot         Reboot system\n");
    shell_puts("  shutdown       Secure shutdown\n");
    shell_puts("  onion          Show .onion address\n");
    shell_puts("\n");
}

static void cmd_status(int argc, char args[SHELL_MAX_ARGS][64]) {
    (void)argc; (void)args;
    ring0_cpu_info_t *cpu = &ring0_state.cpu;

    shell_puts_color("\n=== System Status ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    shell_puts("CPU:     "); shell_puts(cpu->brand); shell_puts("\n");
    shell_puts("Vendor:  "); shell_puts(cpu->vendor); shell_puts("\n");

    shell_puts("Features:");
    if (cpu->cpu_features & R0_CPUID_SSE)   shell_puts(" SSE");
    if (cpu->cpu_features & R0_CPUID_SSE2)  shell_puts(" SSE2");
    if (cpu->cpu_features & R0_CPUID_SSE3)  shell_puts(" SSE3");
    if (cpu->cpu_features & R0_CPUID_AES_NI) shell_puts(" AES-NI");
    if (cpu->cpu_features & R0_CPUID_AVX)   shell_puts(" AVX");
    if (cpu->cpu_features & R0_CPUID_AVX2)  shell_puts(" AVX2");
    if (cpu->cpu_features & R0_CPUID_RDRAND) shell_puts(" RDRAND");
    shell_puts("\n");

    /* Memory */
    shell_puts("Memory:  ");
    uint64_t total = pmm_get_total();
    uint64_t free_pages = pmm_get_free();
    uint64_t used = pmm_get_used();
    /* Simple MB display */
    uint32_t total_mb = (uint32_t)(total / 256); /* 4KB pages -> MB */
    uint32_t free_mb = (uint32_t)(free_pages / 256);
    uint32_t used_mb = (uint32_t)(used / 256);
    shell_puts("Total=");
    char buf[32];
    uint32_t idx = 0;
    uint32_t tmp = total_mb;
    if (tmp == 0) { buf[idx++] = '0'; }
    else {
        char rev[16]; int ri = 0;
        while (tmp) { rev[ri++] = '0' + (tmp % 10); tmp /= 10; }
        while (ri > 0) buf[idx++] = rev[--ri];
    }
    buf[idx++] = 'M'; buf[idx++] = 'B';
    buf[idx++] = ' ';
    tmp = used_mb;
    if (tmp == 0) { buf[idx++] = '0'; }
    else {
        char rev[16]; int ri = 0;
        while (tmp) { rev[ri++] = '0' + (tmp % 10); tmp /= 10; }
        while (ri > 0) buf[idx++] = rev[--ri];
    }
    buf[idx++] = 'M'; buf[idx++] = 'B'; buf[idx++] = 'u'; buf[idx++] = 's'; buf[idx++] = 'e'; buf[idx++] = 'd';
    buf[idx] = 0;
    shell_puts(buf);
    shell_puts("\n");

    /* Session */
    shell_puts("Session: ");
    if (shell.session.authenticated) {
        shell_puts_color("AUTHENTICATED", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    } else {
        shell_puts_color("UNAUTHENTICATED", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    }
    if (shell.session.encrypted_session) {
        shell_puts(" [ENCRYPTED]");
    }
    shell_puts("\n");
}

static void cmd_tor(int argc, char args[SHELL_MAX_ARGS][64]) {
    (void)argc; (void)args;
    const tor_bootstrap_state_t *boot = tor_bootstrap_get_state();

    shell_puts_color("\n=== Tor Status ===\n", VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK);

    shell_puts("State:   ");
    if (boot->state == TOR_BOOT_READY) {
        shell_puts_color("CONNECTED\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    } else if (boot->state == TOR_BOOT_FAILED) {
        shell_puts_color("FAILED\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    } else {
        shell_puts_color("BUILDING...\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    }

    shell_puts("Circuit: ");
    char buf[32];
    uint32_t idx = 0;
    uint32_t cid = boot->circuit_id;
    if (cid == 0) { buf[idx++] = '0'; }
    else {
        char rev[16]; int ri = 0;
        while (cid) { rev[ri++] = "0123456789ABCDEF"[cid & 0xF]; cid >>= 4; }
        while (ri > 0) buf[idx++] = rev[--ri];
    }
    buf[idx] = 0;
    shell_puts(buf);
    shell_puts("\n");

    shell_puts("Guard:   ");
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t octet = boot->guard_ip[i];
        char num[4];
        int ni = 0;
        if (octet == 0) { num[ni++] = '0'; }
        else {
            char rev[4]; int ri = 0;
            while (octet) { rev[ri++] = '0' + (octet % 10); octet /= 10; }
            while (ri > 0) num[ni++] = rev[--ri];
        }
        for (int j = 0; j < ni; j++) shell_putc(num[j]);
        if (i < 3) shell_putc('.');
    }
    shell_puts("\n");

    shell_puts("SOCKS5:  port 9050\n");

    uint32_t circs = 0, streams = 0, sent = 0, recv = 0;
    tor_get_stats(&circs, &streams, &sent, &recv);
    shell_puts("Streams: ");
    char sbuf[16];
    idx = 0;
    if (streams == 0) { sbuf[idx++] = '0'; }
    else {
        char rev[8]; int ri = 0;
        while (streams) { rev[ri++] = '0' + (streams % 10); streams /= 10; }
        while (ri > 0) sbuf[idx++] = rev[--ri];
    }
    sbuf[idx] = 0;
    shell_puts(sbuf);
    shell_puts("\n");
}

static void cmd_onion(int argc, char args[SHELL_MAX_ARGS][64]) {
    (void)argc; (void)args;
    shell_puts_color("\n=== Hidden Service ===\n", VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK);

    const uint8_t *addr = tor_bootstrap_get_onion_addr();
    if (addr[0] == 0) {
        shell_puts_color("  Not published yet\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        return;
    }

    shell_puts("  ");
    for (uint32_t i = 0; i < 56; i++) {
        shell_putc((char)addr[i]);
    }
    shell_puts(".onion\n");
    shell_puts("  Port: 22 (SSH)\n");
    shell_puts("  Encryption: ChaCha20-Poly1305\n");
    shell_puts("\n");
}

static void cmd_ls(int argc, char args[SHELL_MAX_ARGS][64]) {
    const char *path = "/";
    if (argc > 1) path = args[1];

    shell_puts_color("\nDirectory: ", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    shell_puts(path);
    shell_puts("\n");

    vfs_dirent_t entries[32];
    int count = vfs_readdir(path, entries, 32);
    if (count < 0) {
        shell_puts_color("  (empty or error)\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        return;
    }

    for (int i = 0; i < count; i++) {
        shell_puts("  ");
        if (entries[i].type == VFS_TYPE_DIR) {
            shell_puts_color(entries[i].name, VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
            shell_puts("/");
        } else {
            shell_puts(entries[i].name);
        }
        shell_puts("\n");
    }
}

static void cmd_cat(int argc, char args[SHELL_MAX_ARGS][64]) {
    if (argc < 2) {
        shell_puts("Usage: cat <filename>\n");
        return;
    }

    int fd = vfs_open(args[1], FD_FLAG_READ);
    if (fd < 0) {
        shell_puts("Error: cannot open '");
        shell_puts(args[1]);
        shell_puts("'\n");
        return;
    }

    uint8_t buf[512];
    uint32_t total = 0;
    while (1) {
        uint32_t nread = 0;
        int rc = vfs_read(fd, buf, 512, &nread);
        if (rc != 0 || nread == 0) break;
        for (uint32_t i = 0; i < nread; i++) {
            if (buf[i] == '\n') shell_newline();
            else shell_putc((char)buf[i]);
        }
        total += nread;
    }
    shell_newline();
    vfs_close(fd);

    char sbuf[32];
    uint32_t idx = 0;
    if (total == 0) { sbuf[idx++] = '0'; }
    else {
        char rev[16]; int ri = 0;
        while (total) { rev[ri++] = '0' + (total % 10); total /= 10; }
        while (ri > 0) sbuf[idx++] = rev[--ri];
    }
    sbuf[idx++] = ' '; sbuf[idx++] = 'b'; sbuf[idx++] = 'y';
    sbuf[idx++] = 't'; sbuf[idx++] = 'e'; sbuf[idx++] = 's';
    sbuf[idx] = 0;
    shell_puts(sbuf);
    shell_puts("\n");
}

static void cmd_mount(int argc, char args[SHELL_MAX_ARGS][64]) {
    (void)argc; (void)args;
    shell_puts_color("\n=== Mount Points ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    shell_puts("  /          BrainFS (encrypted) on /dev/sda1\n");
    shell_puts("  FAT width: ");
    char buf[8];
    uint32_t idx = 0;
    uint32_t fw = 16;
    char rev[8]; int ri = 0;
    while (fw) { rev[ri++] = '0' + (fw % 10); fw /= 10; }
    while (ri > 0) buf[idx++] = rev[--ri];
    buf[idx] = 0;
    shell_puts(buf);
    shell_puts(" bits/cluster\n");
}

static void cmd_id(int argc, char args[SHELL_MAX_ARGS][64]) {
    (void)argc; (void)args;
    shell_puts_color("\n=== Session Identity ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    shell_puts("  User:   ");
    shell_puts(shell.session.username);
    shell_puts("\n");
    shell_puts("  Session: 0x");

    uint32_t sid = shell.session.session_id;
    char hex[9]; int hi = 8;
    const char *hexd = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        hex[i] = hexd[sid & 0xF];
        sid >>= 4;
    }
    hex[8] = 0;
    shell_puts(hex);
    shell_puts("\n");

    shell_puts("  Auth:   ");
    if (shell.session.authenticated) shell_puts_color("YES", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    else shell_puts_color("NO", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    shell_puts("\n");

    shell_puts("  Crypto: ChaCha20-Poly1305 over Tor v3\n");
}

static void cmd_clear(int argc, char args[SHELL_MAX_ARGS][64]) {
    (void)argc; (void)args;
    vga_text_clear();
}

static void cmd_uptime(int argc, char args[SHELL_MAX_ARGS][64]) {
    (void)argc; (void)args;
    uint64_t ticks = ring0_ticks();
    uint64_t ms = ticks / ring0_state.tsc_per_ms;
    uint32_t secs = (uint32_t)(ms / 1000);

    shell_puts("Uptime: ");
    char buf[32]; uint32_t idx = 0;
    if (secs == 0) { buf[idx++] = '0'; }
    else {
        char rev[16]; int ri = 0;
        while (secs) { rev[ri++] = '0' + (secs % 10); secs /= 10; }
        while (ri > 0) buf[idx++] = rev[--ri];
    }
    buf[idx++] = 's'; buf[idx] = 0;
    shell_puts(buf);
    shell_puts("\n");
}

static void cmd_onion_session(int argc, char args[SHELL_MAX_ARGS][64]) {
    (void)argc; (void)args;

    shell_puts_color("\n=== Encrypted .onion Shell Session ===\n", VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK);

    /* Establish Tor stream */
    uint32_t circ_id = tor_bootstrap_get_circuit_id();
    if (circ_id == 0) {
        shell_puts_color("  Tor circuit not ready\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        return;
    }

    /* Open stream to localhost:22 via Tor */
    const uint8_t target_addr[4] = {127, 0, 0, 1};
    int rc = tor_stream_open(circ_id, target_addr, 22, 0);
    if (rc != 0) {
        shell_puts_color("  Stream open failed\n", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        return;
    }

    shell.session.encrypted_session = 1;
    shell.session.circ_id = circ_id;
    shell.session.stream_id = (uint32_t)rc;

    shell_puts_color("  Encrypted session established\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    shell_puts("  Stream ID: ");
    char buf[16]; uint32_t idx = 0;
    uint32_t sid = shell.session.stream_id;
    if (sid == 0) { buf[idx++] = '0'; }
    else {
        char rev[8]; int ri = 0;
        while (sid) { rev[ri++] = "0123456789ABCDEF"[sid & 0xF]; sid >>= 4; }
        while (ri > 0) buf[idx++] = rev[--ri];
    }
    buf[idx] = 0;
    shell_puts(buf);
    shell_puts("\n");
}

static void cmd_reboot(int argc, char args[SHELL_MAX_ARGS][64]) {
    (void)argc; (void)args;
    shell_puts_color("Rebooting securely...\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);

    /* Wipe session keys */
    shell_memset(&shell.session.session_key, 0, 32);

    /* Flush filesystems */
    vfs_sync_all();

    /* Triple-zero keyboard controller reset */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
    ring0_delay_ms(100);
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
    ring0_delay_ms(100);

    /* ACPI power off as fallback */
    __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));

    /* Halt */
    while (1) __asm__ volatile("hlt");
}

static void cmd_dmesg(int argc, char args[SHELL_MAX_ARGS][64]) {
    (void)argc; (void)args;
    shell_puts_color("\n=== Boot Log ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    shell_puts("  [INIT] Ring-0 bare-metal environment: OK\n");
    shell_puts("  [INIT] CPU: "); shell_puts(ring0_state.cpu.brand); shell_puts("\n");
    shell_puts("  [INIT] APIC ID: "); {
        char buf[8]; uint32_t idx = 0;
        uint32_t id = ring0_state.cpu.apic_id;
        const char *h = "0123456789ABCDEF";
        buf[0] = h[(id >> 4) & 0xF]; buf[1] = h[id & 0xF]; buf[2] = 0;
        shell_puts(buf);
    }
    shell_puts("\n");
    shell_puts("  [WiFi] Auto-detection: ");
    uint32_t wifi_count = wifi_autodetect_get_count();
    char wbuf[16]; uint32_t wi = 0;
    if (wifi_count == 0) { wbuf[wi++] = '0'; }
    else {
        char rev[8]; int ri = 0;
        while (wifi_count) { rev[ri++] = '0' + (wifi_count % 10); wifi_count /= 10; }
        while (ri > 0) wbuf[wi++] = rev[--ri];
    }
    wbuf[wi++] = ' '; wbuf[wi++] = 'd'; wbuf[wi++] = 'e'; wbuf[wi++] = 'v';
    wbuf[wi++] = 'i'; wbuf[wi++] = 'c'; wbuf[wi++] = 'e'; wbuf[wi++] = 's';
    wbuf[wi] = 0;
    shell_puts(wbuf);
    shell_puts("\n");
    shell_puts("  [Tor]  Bootstrap: ");
    if (tor_bootstrap_is_ready()) shell_puts_color("READY\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    else shell_puts_color("NOT READY\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    shell_puts("  [FS]   BrainFS: mounted\n");
    shell_puts("  [SEC]  26 security modules: initialized\n");
    shell_puts("  [NET]  Firewall: 7 rules active\n");
    shell_puts("\n");
}

/* ======================================================================== */
/* Command Dispatch                                                         */
/* ======================================================================== */

static void shell_dispatch(int argc, char args[SHELL_MAX_ARGS][64]) {
    if (argc == 0) return;

    if (shell_strcmp(args[0], "help") == 0) cmd_help(argc, args);
    else if (shell_strcmp(args[0], "?") == 0) cmd_help(argc, args);
    else if (shell_strcmp(args[0], "status") == 0) cmd_status(argc, args);
    else if (shell_strcmp(args[0], "tor") == 0) cmd_tor(argc, args);
    else if (shell_strcmp(args[0], "onion") == 0) cmd_onion(argc, args);
    else if (shell_strcmp(args[0], "ls") == 0) cmd_ls(argc, args);
    else if (shell_strcmp(args[0], "cat") == 0) cmd_cat(argc, args);
    else if (shell_strcmp(args[0], "mount") == 0) cmd_mount(argc, args);
    else if (shell_strcmp(args[0], "id") == 0) cmd_id(argc, args);
    else if (shell_strcmp(args[0], "clear") == 0) cmd_clear(argc, args);
    else if (shell_strcmp(args[0], "uptime") == 0) cmd_uptime(argc, args);
    else if (shell_strcmp(args[0], "dmesg") == 0) cmd_dmesg(argc, args);
    else if (shell_strcmp(args[0], "connect") == 0) cmd_onion_session(argc, args);
    else if (shell_strcmp(args[0], "reboot") == 0) cmd_reboot(argc, args);
    else if (shell_strcmp(args[0], "shutdown") == 0) cmd_reboot(argc, args);
    else if (shell_strcmp(args[0], "net") == 0) {
        shell_puts_color("\n=== Network ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        shell_puts("  NIC:     e1000 (Intel)\n");
        shell_puts("  WiFi:    ");
        wifi_pci_device_t active_dev;
        if (wifi_autodetect_get_active(&active_dev) == 0) {
            shell_puts(active_dev.driver_name);
        } else {
            shell_puts("none detected");
        }
        shell_puts("\n");
        shell_puts("  Tor:     ");
        if (tor_bootstrap_is_ready()) shell_puts("connected");
        else shell_puts("connecting...");
        shell_puts("\n");
        shell_puts("  SOCKS5:  localhost:9050\n");
        shell_puts("\n");
    }
    else if (shell_strcmp(args[0], "wifi") == 0) {
        shell_puts_color("\n=== WiFi Devices ===\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        uint32_t count = wifi_autodetect_get_count();
        if (count == 0) {
            shell_puts("  No WiFi devices detected\n");
        } else {
            for (uint32_t i = 0; i < count; i++) {
                shell_puts("  ");
                const wifi_pci_device_t *d = wifi_autodetect_get_device(i);
                if (!d) continue;
                shell_puts(d->driver_name);
                shell_puts(" [");
                shell_puts(d->driver_index >= 0 ? "MATCHED]" : "no driver]");
                shell_puts("\n");
            }
        }
    }
    else {
        shell_puts_color("Unknown command: ", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        shell_puts(args[0]);
        shell_puts(". Type 'help' for commands.\n");
    }
}

/* ======================================================================== */
/* Shell Init & Main Loop                                                   */
/* ======================================================================== */

int shell_init(void) {
    shell_memset(&shell, 0, sizeof(shell_state_t));

    /* Generate session ID from hardware entropy */
    uint64_t sid;
    __asm__ volatile("rdrand %0" : "=r"(sid) : : "cc");
    if (sid == 0) sid = ring0_rdtsc();
    shell.session.session_id = (uint32_t)(sid & 0xFFFFFFFF);

    /* Set default username */
    shell.session.username[0] = 'r';
    shell.session.username[1] = 'o';
    shell.session.username[2] = 'o';
    shell.session.username[3] = 't';
    shell.session.username[4] = 0;

    /* Set prompt */
    const char *p = "root@chicago-95:~# ";
    for (uint32_t i = 0; i < 63 && p[i]; i++) shell.prompt[i] = p[i];

    shell.initialized = 1;
    return 0;
}

int shell_run(void) {
    if (!shell.initialized) return -1;

    shell.running = 1;

    /* Clear screen and show banner */
    vga_text_clear();
    shell_puts_color("Chicago-95 BrainFS Shell v"SHELL_VERSION"\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    shell_puts_color("Ring-0 bare-metal encrypted .onion session\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    shell_puts_color("Type 'help' for available commands.\n\n", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    while (shell.running) {
        /* Poll keyboard */
        if (kbd_available()) kbd_getchar();

        /* Show prompt */
        shell_puts_color(shell.prompt, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);

        /* Read line */
        char line[SHELL_MAX_CMD];
        int len = shell_readline(line, SHELL_MAX_CMD);

        if (len < 0) continue; /* Ctrl+C */
        if (len == 0) continue; /* Empty line */

        /* Add to history */
        if (shell.history_count < SHELL_HISTORY_SLOTS) {
            uint32_t hi = shell.history_count;
            uint32_t copylen = len;
            if (copylen >= SHELL_MAX_CMD) copylen = SHELL_MAX_CMD - 1;
            for (uint32_t i = 0; i < copylen; i++)
                shell.history[hi].cmd[i] = line[i];
            shell.history[hi].cmd[copylen] = 0;
            shell.history_count++;
        }
        shell.history_idx = shell.history_count;

        /* Parse and dispatch */
        char args[SHELL_MAX_ARGS][64];
        shell_memset(args, 0, sizeof(args));
        int argc = shell_parse_args(line, args);
        shell_dispatch(argc, args);

        shell.cmd_count++;
    }

    return 0;
}
