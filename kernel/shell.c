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
#include <stdint.h>
#include <stddef.h>

static char line_buffer[256];
static int line_pos = 0;

#define HISTORY_SIZE 32
static char history_buf[HISTORY_SIZE][256];
static int history_len = 0;
static int history_idx = -1;

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

static void print_prompt(void) {
    console_puts("chicago-95> ", CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4));
}

static void cmd_help(void) {
    console_puts("Chicago-95 Kernel Shell\n", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_puts("───────────────────────\n", CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4));
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
    console_puts("\nNavigation: Up/Down arrows = history, Ctrl+A = home, Ctrl+E = end\n",
                 CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4));
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

static void cmd_neofetch(void) {
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

    uint8_t cyan = CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4);
    uint8_t white = CONSOLE_WHITE | (CONSOLE_BLACK << 4);
    uint8_t green = CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4);
    uint8_t yellow = CONSOLE_YELLOW | (CONSOLE_BLACK << 4);
    uint8_t grey = CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4);
    uint8_t dim = CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4);

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
        for (int c = 0; c < SKY_W; c++) {
            char ch = sky[r][c];
            uint8_t cl = 0;
            if (ch >= '0' && ch <= '5')
                cl = ((ch - '0') & 1) ? (CONSOLE_BLUE | (CONSOLE_BLACK << 4)) : (CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4));
            else if (ch == 'o') cl = CONSOLE_YELLOW | (CONSOLE_BLACK << 4);
            else if (ch == '-') cl = CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4);
            else if (ch == '*' || ch == '|') cl = CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4);
            console_putc((ch >= '0' && ch <= '5') || ch == 'o' ? 0xDB : ch, cl);
        }
        switch (r) {
        case 0: nf_label("OS"); console_puts("Chicago-95", white); console_puts("  BrainFS x86_64", grey); break;
        case 1: nf_label("Host"); console_puts("Bare-metal x86_64", white); break;
        case 2: nf_label("Kernel"); console_puts("0.1.0-beta", yellow); break;
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
    for (int i = 0; i < 8; i++) console_putc(' ', CONSOLE_BLACK | (i << 4));
    for (int i = 8; i < 16; i++) console_putc(' ', CONSOLE_BLACK | (i << 4));
    console_puts("  chicago-95 bootloader", grey);
    console_putc('\n', white);
}

static int process_command(const char *cmd) {
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
    else {
        console_puts("Unknown command: ", CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts(cmd, CONSOLE_RED | (CONSOLE_BLACK << 4));
        console_puts("\nType 'help' for available commands.\n", CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4));
        return -1;
    }
    return 0;
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
