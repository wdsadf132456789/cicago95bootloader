/**
 * Chicago-95 btop - System Monitor
 * Text-mode dashboard showing CPU, memory, disk, network, processes
 */

#include "btop.h"
#include "console.h"
#include "timer.h"
#include "keyboard.h"
#include "process.h"
#include "kmalloc.h"
#include "pci.h"
#include "ata.h"
#include "drivers/e1000.h"
#include "drivers/net.h"
#include "kernel.h"

/* ---- CPUID ---- */

static char cpu_vendor[13];
static char cpu_brand[49];
static int  cpu_cores;
static int  cpu_has_brand;

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf));
}

static void btop_cpuid(void) {
    uint32_t eax, ebx, ecx, edx;

    cpuid(0, &eax, &ebx, &ecx, &edx);
    *(uint32_t *)(cpu_vendor + 0) = ebx;
    *(uint32_t *)(cpu_vendor + 4) = edx;
    *(uint32_t *)(cpu_vendor + 8) = ecx;
    cpu_vendor[12] = '\0';

    cpuid(1, &eax, &ebx, &ecx, &edx);
    cpu_cores = (ebx >> 16) & 0xFF;
    if (cpu_cores == 0) cpu_cores = 1;

    /* Try extended CPUID for brand string */
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000004) {
        uint32_t *brand = (uint32_t *)cpu_brand;
        cpuid(0x80000002, &brand[0], &brand[1], &brand[2], &brand[3]);
        cpuid(0x80000003, &brand[4], &brand[5], &brand[6], &brand[7]);
        cpuid(0x80000004, &brand[8], &brand[9], &brand[10], &brand[11]);
        cpu_brand[48] = '\0';
        cpu_has_brand = 1;
    }
}

/* ---- Drawing helpers ---- */

#define COLOR_HEADER   (CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4))
#define COLOR_LABEL    (CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4))
#define COLOR_VALUE    (CONSOLE_WHITE | (CONSOLE_BLACK << 4))
#define COLOR_BAR_FG   (CONSOLE_LIGHT_GREEN | (CONSOLE_BLACK << 4))
#define COLOR_BAR_BG   (CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4))
#define COLOR_WARN     (CONSOLE_YELLOW | (CONSOLE_BLACK << 4))
#define COLOR_CRIT     (CONSOLE_LIGHT_RED | (CONSOLE_BLACK << 4))
#define COLOR_DIVIDER  (CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4))
#define COLOR_TITLE    (CONSOLE_WHITE | (CONSOLE_BLUE << 4))
#define COLOR_STATUS   (CONSOLE_BLACK | (CONSOLE_LIGHT_CYAN << 4))

static void draw_rect(int x, int y, int w, int h, uint8_t color) {
    for (int row = y; row < y + h && row < BTOP_HEIGHT; row++) {
        console_set_cursor(x, row);
        for (int col = 0; col < w && x + col < BTOP_WIDTH; col++) {
            console_putc(' ', color);
        }
    }
}

static void draw_hline(int x, int y, int len, uint8_t color) {
    console_set_cursor(x, y);
    for (int i = 0; i < len && x + i < BTOP_WIDTH; i++)
        console_putc(' ', color);
}

static void draw_bar(int x, int y, int width, int pct, uint8_t fg, uint8_t bg) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int filled = (width * pct) / 100;
    console_set_cursor(x, y);
    for (int i = 0; i < width; i++) {
        console_putc(i < filled ? '|' : ' ', i < filled ? fg : bg);
    }
}

static void draw_text(int x, int y, const char *s, uint8_t color) {
    console_set_cursor(x, y);
    console_puts(s, color);
}

static void draw_char(int x, int y, char c, uint8_t color) {
    console_set_cursor(x, y);
    console_putc(c, color);
}

static void draw_hex32(int x, int y, uint32_t val, uint8_t color) {
    static const char hex[] = "0123456789ABCDEF";
    console_set_cursor(x, y);
    console_putc('0', color);
    console_putc('x', color);
    for (int i = 7; i >= 0; i--) {
        console_putc(hex[(val >> (i * 4)) & 0xF], color);
    }
}

static int uint_to_str(uint64_t val, char *buf) {
    char tmp[21];
    int len = 0;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    while (val) { tmp[len++] = '0' + (val % 10); val /= 10; }
    for (int i = 0; i < len; i++) buf[i] = tmp[len - 1 - i];
    buf[len] = '\0';
    return len;
}

static int format_size(uint64_t bytes, char *buf) {
    if (bytes >= 1073741824ULL) {
        uint64_t gb = bytes / 1073741824ULL;
        uint64_t rem = (bytes % 1073741824ULL) / 107374182ULL;
        int n = uint_to_str(gb, buf);
        buf[n++] = '.';
        buf[n++] = '0' + (int)rem;
        buf[n++] = 'G';
        buf[n] = '\0';
        return n;
    } else if (bytes >= 1048576ULL) {
        uint64_t mb = bytes / 1048576ULL;
        uint64_t rem = (bytes % 1048576ULL) / 104857ULL;
        int n = uint_to_str(mb, buf);
        buf[n++] = '.';
        buf[n++] = '0' + (int)rem;
        buf[n++] = 'M';
        buf[n] = '\0';
        return n;
    } else {
        uint64_t kb = bytes / 1024ULL;
        int n = uint_to_str(kb, buf);
        buf[n++] = 'K';
        buf[n] = '\0';
        return n;
    }
}

/* ---- CPU usage estimation ---- */

static uint64_t last_total_ticks = 0;

static int estimate_cpu_usage(void) {
    uint64_t now = timer_get_ticks();
    uint64_t dt = now - last_total_ticks;
    last_total_ticks = now;

    if (dt == 0) return 0;

    /* Rough estimate: if scheduler switched recently, CPU was busy */
    int processes_active = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state != PROC_UNUSED)
            processes_active++;
    }

    /* Busy = more processes than cores */
    int usage = ((processes_active > cpu_cores ? processes_active : cpu_cores) * 100) / 256;
    if (usage > 100) usage = 100;
    if (usage < 5 && processes_active > 0) usage = 5;

    return usage;
}

/* ---- Main btop render ---- */

static void btop_render(void) {
    char buf[64];

    /* Clear screen */
    draw_rect(0, 0, BTOP_WIDTH, BTOP_HEIGHT, CONSOLE_BLACK | (CONSOLE_BLACK << 4));

    /* Title bar */
    draw_rect(0, 0, BTOP_WIDTH, 1, COLOR_TITLE);
    draw_text(2, 0, "Chicago-95 System Monitor", COLOR_TITLE);
    uint64_t uptime = timer_get_seconds();
    uint64_t mins = uptime / 60;
    uint64_t hrs = mins / 60;
    mins %= 60;
    uint64_t secs = uptime % 60;

    char uptime_buf[32];
    int p = 0;
    if (hrs > 0) { uptime_buf[p++] = '0' + (hrs / 10); uptime_buf[p++] = '0' + (hrs % 10); uptime_buf[p++] = 'h'; }
    uptime_buf[p++] = '0' + (mins / 10); uptime_buf[p++] = '0' + (mins % 10); uptime_buf[p++] = 'm';
    uptime_buf[p++] = '0' + (secs / 10); uptime_buf[p++] = '0' + (secs % 10); uptime_buf[p++] = 's';
    uptime_buf[p] = '\0';
    draw_text(BTOP_WIDTH - 2 - p, 0, uptime_buf, COLOR_TITLE);

    /* ---- CPU section (rows 1-4) ---- */
    draw_text(1, 1, "CPU", COLOR_HEADER);
    draw_hline(0, 2, BTOP_WIDTH, COLOR_DIVIDER);

    /* CPU model */
    if (cpu_has_brand) {
        /* Trim leading spaces */
        const char *model = cpu_brand;
        while (*model == ' ') model++;
        draw_text(1, 3, model, COLOR_VALUE);
    } else {
        draw_text(1, 3, cpu_vendor, COLOR_VALUE);
    }

    /* CPU usage bar */
    int cpu_pct = estimate_cpu_usage();
    draw_text(56, 1, "Usage:", COLOR_LABEL);
    draw_bar(63, 1, 15, cpu_pct,
             cpu_pct > 90 ? COLOR_CRIT : cpu_pct > 70 ? COLOR_WARN : COLOR_BAR_FG,
             COLOR_BAR_BG);

    /* ---- Memory section (rows 4-8) ---- */
    draw_hline(0, 4, BTOP_WIDTH, COLOR_DIVIDER);
    draw_text(1, 5, "MEM", COLOR_HEADER);

    uint64_t total_pages = pmm_get_total_pages();
    uint64_t free_pages = pmm_get_free_pages();
    uint64_t used_pages = total_pages - free_pages;
    uint64_t total_bytes = total_pages * 4096;
    uint64_t used_bytes = used_pages * 4096;

    int mem_pct = total_pages > 0 ? (int)((used_pages * 100) / total_pages) : 0;

    draw_text(56, 5, "Usage:", COLOR_LABEL);
    draw_bar(63, 5, 15, mem_pct,
             mem_pct > 90 ? COLOR_CRIT : mem_pct > 70 ? COLOR_WARN : COLOR_BAR_FG,
             COLOR_BAR_BG);

    /* Physical memory line */
    draw_text(1, 6, "  Phys:", COLOR_LABEL);
    format_size(used_bytes, buf);
    int len = 0;
    while (buf[len]) len++;
    buf[len++] = '/';
    format_size(total_bytes, buf + len);
    draw_text(9, 6, buf, COLOR_VALUE);

    /* Kernel heap */
    size_t heap_used = kheap_used();
    size_t heap_total = kheap_total();
    draw_text(1, 7, "  Heap:", COLOR_LABEL);
    format_size(heap_used, buf);
    len = 0;
    while (buf[len]) len++;
    buf[len++] = '/';
    format_size(heap_total, buf + len);
    draw_text(9, 7, buf, COLOR_VALUE);

    /* ---- Disk section (rows 8-11) ---- */
    draw_hline(0, 8, BTOP_WIDTH, COLOR_DIVIDER);
    draw_text(1, 9, "DISK", COLOR_HEADER);

    int disk_count = ata_get_detected_count();
    if (disk_count == 0) {
        draw_text(6, 9, "No drives detected", COLOR_WARN);
    } else {
        int row = 10;
        for (int i = 0; i < disk_count && i < 4; i++) {
            ata_info_t *info = ata_get_info(i);
            if (!info || !info->present) continue;

            char label[8] = "  hd?";
            label[4] = 'a' + i;
            draw_text(1, row, label, COLOR_LABEL);
            draw_text(7, row, info->model, COLOR_VALUE);

            /* Size on right */
            uint64_t size = (uint64_t)info->max_lba48 * info->sector_size;
            if (info->max_lba48 == 0) size = (uint64_t)info->max_lba * info->sector_size;
            format_size(size, buf);
            draw_text(BTOP_WIDTH - 12, row, buf, COLOR_VALUE);
            row++;
        }
        if (row == 10) {
            draw_text(6, 9, "No drives ready", COLOR_WARN);
        }
    }

    /* ---- Network section (rows 12-14) ---- */
    draw_hline(0, 12, BTOP_WIDTH, COLOR_DIVIDER);
    draw_text(1, 13, "NET", COLOR_HEADER);

    if (e1000_is_ready()) {
        uint32_t ip = net_get_ip();
        uint8_t mac[6];
        net_get_mac(mac);

        draw_text(5, 13, "eth0:", COLOR_LABEL);

        /* IP */
        p = 0;
        buf[p++] = '0' + (ip & 0xFF) / 100;  /* skip leading zeros manually */
        buf[p++] = '0' + ((ip & 0xFF) / 10) % 10;
        buf[p++] = '0' + (ip & 0xFF) % 10;
        buf[p++] = '.';
        buf[p++] = '0' + ((ip >> 8) & 0xFF) / 100;
        buf[p++] = '0' + (((ip >> 8) & 0xFF) / 10) % 10;
        buf[p++] = '0' + ((ip >> 8) & 0xFF) % 10;
        buf[p++] = '.';
        buf[p++] = '0' + ((ip >> 16) & 0xFF) / 100;
        buf[p++] = '0' + (((ip >> 16) & 0xFF) / 10) % 10;
        buf[p++] = '0' + ((ip >> 16) & 0xFF) % 10;
        buf[p++] = '.';
        buf[p++] = '0' + ((ip >> 24) & 0xFF) / 100;
        buf[p++] = '0' + (((ip >> 24) & 0xFF) / 10) % 10;
        buf[p++] = '0' + ((ip >> 24) & 0xFF) % 10;
        buf[p] = '\0';
        draw_text(11, 13, buf, COLOR_VALUE);

        /* MAC */
        p = 0;
        for (int i = 0; i < 6; i++) {
            static const char hex[] = "0123456789ABCDEF";
            if (i > 0) buf[p++] = ':';
            buf[p++] = hex[(mac[i] >> 4) & 0xF];
            buf[p++] = hex[mac[i] & 0xF];
        }
        buf[p] = '\0';
        draw_text(28, 13, buf, COLOR_LABEL);

        /* Link status */
        draw_text(50, 13, "UP", COLOR_BAR_FG);

    } else {
        draw_text(5, 13, "No NIC detected", COLOR_WARN);
    }

    /* ---- PCI devices (row 14) ---- */
    draw_hline(0, 14, BTOP_WIDTH, COLOR_DIVIDER);
    int pci_count = pci_get_device_count();
    draw_text(1, 15, "PCI", COLOR_HEADER);

    if (pci_count == 0) {
        draw_text(5, 15, "No devices", COLOR_WARN);
    } else {
        char cntbuf[8];
        uint_to_str(pci_count, cntbuf);
        int cplen = 0;
        while (cntbuf[cplen]) cplen++;
        cntbuf[cplen++] = ' ';
        cntbuf[cplen++] = 'd';
        cntbuf[cplen++] = 'e';
        cntbuf[cplen++] = 'v';
        cntbuf[cplen++] = 's';
        cntbuf[cplen] = '\0';
        draw_text(5, 15, cntbuf, COLOR_VALUE);
    }

    /* ---- Process table (rows 16-24) ---- */
    draw_hline(0, 16, BTOP_WIDTH, COLOR_DIVIDER);
    draw_text(1, 17, "PROCESSES", COLOR_HEADER);

    /* Header */
    draw_text(1, 18, " PID  STATE     NAME", COLOR_LABEL);
    draw_text(1, 18, " PID", COLOR_LABEL);

    int row = 19;
    int proc_count = 0;
    for (int i = 0; i < MAX_PROCESSES && row < BTOP_HEIGHT - 1; i++) {
        if (processes[i].state != PROC_UNUSED) {
            proc_count++;

            /* PID */
            char pidbuf[8];
            uint_to_str(processes[i].pid, pidbuf);
            /* Pad to 5 chars */
            int pidlen = 0;
            while (pidbuf[pidlen]) pidlen++;
            int pad = 5 - pidlen;
            console_set_cursor(1, row);
            for (int j = 0; j < pad; j++) console_putc(' ', COLOR_VALUE);
            console_puts(pidbuf, COLOR_VALUE);

            /* State */
            int sx = 7;
            switch (processes[i].state) {
                case PROC_READY:   draw_text(sx, row, "READY", COLOR_BAR_FG); break;
                case PROC_RUNNING: draw_text(sx, row, "RUN  ", COLOR_BAR_FG); break;
                case PROC_BLOCKED: draw_text(sx, row, "BLOCK", COLOR_WARN); break;
                case PROC_ZOMBIE:  draw_text(sx, row, "ZOMB ", COLOR_CRIT); break;
                default:           draw_text(sx, row, "?????", COLOR_CRIT); break;
            }

            /* Ring */
            draw_char(14, row,
                      processes[i].ring == RING0 ? '0' : '3',
                      COLOR_LABEL);

            /* Name */
            draw_text(16, row, processes[i].name, COLOR_VALUE);

            /* Stack usage (approximate from kernel_stack base) */
            draw_text(52, row, processes[i].ring == RING0 ? "kernel" : "user", COLOR_LABEL);

            row++;
        }
    }

    /* Fill empty rows */
    while (row < BTOP_HEIGHT - 1) {
        draw_hline(0, row, BTOP_WIDTH, CONSOLE_BLACK | (CONSOLE_BLACK << 4));
        row++;
    }

    /* ---- Status bar ---- */
    draw_rect(0, BTOP_HEIGHT - 1, BTOP_WIDTH, 1, COLOR_STATUS);
    draw_text(1, BTOP_HEIGHT - 1, " q:Quit ", COLOR_STATUS);

    char cntbuf2[16];
    uint_to_str(proc_count, cntbuf2);
    int cl = 0;
    while (cntbuf2[cl]) cl++;
    cntbuf2[cl++] = ' ';
    cntbuf2[cl++] = 'p';
    cntbuf2[cl++] = 'r';
    cntbuf2[cl++] = 'o';
    cntbuf2[cl++] = 'c';
    cntbuf2[cl] = '\0';
    draw_text(BTOP_WIDTH - 14, BTOP_HEIGHT - 1, cntbuf2, COLOR_STATUS);
}

/* ---- Main loop ---- */

void btop_run(void) {
    btop_cpuid();

    console_clear();

    while (1) {
        btop_render();

        /* Wait for refresh or keypress */
        for (volatile int i = 0; i < BTOP_REFRESH_MS * 10000; i++) {
            int c = keyboard_getchar();
            if (c == 'q' || c == 'Q' || c == 27) {
                console_clear();
                return;
            }
        }
    }
}
