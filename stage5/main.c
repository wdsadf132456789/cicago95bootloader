#include <stdint.h>

#define VGA_BUF    ((volatile uint16_t *)0xB8000)
#define VGA_ROWS   25
#define VGA_COLS   80

#define COL_DEFAULT 0x07
#define COL_HDR     0x03
#define COL_OK      0x02
#define COL_WARN    0x0E
#define COL_ERR     0x04
#define COL_LABEL   0x0F
#define COL_DIM     0x08
#define COL_HI      0x0D
#define COL_LGREEN  0x0A
#define COL_LCYAN   0x0B

#define KBD_DATA    0x60
#define KBD_STAT    0x64

#define BOOT_INFO_ADDR 0x7000
#define PAGE_SIZE   8
#define MIN_STAGE   5
#define MAX_STAGE   100

#define STAGE_BASE_ADDR  0x30000
#define STAGE_ADDR_STEP  0x10000

typedef struct {
    uint64_t entry_point;
    uint32_t kernel_crc;
    uint32_t e820_count;
    uint64_t e820_addr;
    uint32_t module_count;
    uint64_t module_list_addr;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_bpp;
    uint64_t rsdp_addr;
    uint32_t lapic_count;
    uint64_t lapic_addr;
    uint32_t ioapic_count;
    uint64_t ioapic_addr;
    uint64_t boot_ticks;
    uint8_t  measured_hash[32];
    char     bootloader_id[16];
} boot_info_t;

static uint8_t crow = 0, ccol = 0;

static void pc(char c, uint8_t color) {
    if (c == '\n') { crow++; ccol = 0; return; }
    if (ccol >= VGA_COLS) { crow++; ccol = 0; }
    if (crow >= VGA_ROWS) return;
    VGA_BUF[crow * VGA_COLS + ccol++] = (uint16_t)color << 8 | (uint8_t)c;
}

static void ps(const char *s, uint8_t color) {
    while (*s) pc(*s++, color);
}

static void pxy(int x, int y, char c, uint8_t color) {
    if ((uint32_t)x >= VGA_COLS || (uint32_t)y >= VGA_ROWS) return;
    VGA_BUF[y * VGA_COLS + x] = (uint16_t)color << 8 | (uint8_t)c;
}

static void cls(void) {
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
        VGA_BUF[i] = (uint16_t)COL_DEFAULT << 8 | ' ';
    crow = 0; ccol = 0;
}

static void draw_bar(int y, const char *text, uint8_t color) {
    for (int x = 0; x < VGA_COLS; x++)
        pxy(x, y, ' ', color);
    int x = (VGA_COLS - 40) / 2;
    for (int i = 0; text[i] && x + i < VGA_COLS; i++)
        pxy(x + i, y, text[i], color);
}

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "dN"(port));
    return v;
}

static int kbd_hit(void) {
    return inb(KBD_STAT) & 0x01;
}

static uint8_t kbd_get(void) {
    return inb(KBD_DATA);
}

static void kbd_flush(void) {
    while (kbd_hit()) kbd_get();
}

static uint8_t kbd_wait_sc(void) {
    while (1) {
        while (!kbd_hit());
        uint8_t sc = kbd_get();
        if (!(sc & 0x80)) return sc;
    }
}

static void d32(uint32_t val) {
    char buf[12]; int i = 11; buf[i] = 0;
    if (val == 0) buf[--i] = '0';
    else while (val > 0) { buf[--i] = '0' + (val % 10); val /= 10; }
    ps(&buf[i], COL_DEFAULT);
}

static void h64(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    char buf[17]; buf[16] = 0;
    for (int j = 0; j < 16; j++)
        buf[j] = hex[(val >> (60 - j*4)) & 0xF];
    ps(buf, COL_DIM);
}

static int sc_to_digit(uint8_t sc) {
    if (sc >= 0x02 && sc <= 0x0B) {
        int d = sc - 0x01;
        if (d == 10) d = 0;
        return d;
    }
    return -1;
}

static void run_stage(int num) {
    uint64_t addr = STAGE_BASE_ADDR + (num - MIN_STAGE) * STAGE_ADDR_STEP;
    cls();
    draw_bar(3, "", COL_DEFAULT);

    char msg[] = "Stage 000 -- Press any key to return.";
    int mpos = 6;
    if (num >= 100) { msg[mpos++] = '0' + num / 100; }
    if (num >= 10)  { msg[mpos++] = '0' + (num / 10) % 10; }
    msg[mpos] = '0' + num % 10;

    int mx = (VGA_COLS - 38) / 2;
    for (int i = 0; msg[i]; i++)
        pxy(mx + i, 12, msg[i], COL_LGREEN);

    void (*fn)(void) = (void (*)(void))(uint64_t)addr;
    fn();

    cls();
}

static void show_system_info(boot_info_t *bi) {
    cls();
    ps("========================================\n", COL_HDR);
    ps("   System Information\n", COL_HI);
    ps("========================================\n\n", COL_HDR);

    ps("  Kernel entry:     0x", COL_LABEL); h64(bi->entry_point); ps("\n", COL_DEFAULT);
    ps("  Bootloader ID:    ", COL_LABEL); ps(bi->bootloader_id, COL_DEFAULT); ps("\n", COL_DEFAULT);
    ps("  E820 entries:     ", COL_LABEL); d32(bi->e820_count); ps("\n", COL_DEFAULT);
    ps("  Module count:     ", COL_LABEL); d32(bi->module_count); ps("\n", COL_DEFAULT);
    ps("  RSDP address:     0x", COL_LABEL); h64(bi->rsdp_addr); ps("\n", COL_DEFAULT);
    ps("  LAPIC count:      ", COL_LABEL); d32(bi->lapic_count); ps("\n", COL_DEFAULT);
    ps("  I/O APIC count:   ", COL_LABEL); d32(bi->ioapic_count); ps("\n\n", COL_DEFAULT);
    ps("  Stages 5-100 loaded at 0x30000-0x9F0000\n", COL_DIM);

    ps("\n\n  Press any key to return to menu...", COL_DIM);
    kbd_flush();
    kbd_wait_sc();
    cls();
}

void stage5_entry(void) {
    boot_info_t *bi = (boot_info_t *)BOOT_INFO_ADDR;
    int page = 0;
    int total_pages = (MAX_STAGE - MIN_STAGE + 1 + PAGE_SIZE - 1) / PAGE_SIZE;
    kbd_flush();

    while (1) {
        cls();
        draw_bar(0, "  Chicago-95 Boot Manager v2.0  ", COL_HI);

        int start_stage = MIN_STAGE + page * PAGE_SIZE;
        int end_stage = start_stage + PAGE_SIZE - 1;
        if (end_stage > MAX_STAGE) end_stage = MAX_STAGE;

        for (int y = 0; y < VGA_COLS; y++) {
            pxy(y, 1, 0xCD, COL_LCYAN);
            pxy(y, 23, 0xCD, COL_LCYAN);
        }

        ps("\n\n  Page ", COL_LCYAN);
        d32(page + 1);
        ps("/", COL_LCYAN);
        d32(total_pages);
        ps("  (Stages ", COL_LCYAN);
        d32(start_stage);
        ps("-", COL_LCYAN);
        d32(end_stage);
        ps(")\n\n", COL_LCYAN);

        for (int i = 0; i < PAGE_SIZE; i++) {
            int s = start_stage + i;
            if (s > MAX_STAGE) break;
            ps("    [", COL_DIM);
            pxy(4, 5 + i, '1' + i, COL_LABEL);
            ps("]  Stage ", COL_DIM);
            d32(s);

            if (s == 5)
                ps("  (Boot Manager)", COL_LCYAN);
            else if (s == 6)
                ps("  (Recovery Shell)", COL_WARN);
            else if (s == 7)
                ps("  (Tetris)", COL_HI);
            else if (s == 8)
                ps("  (Snake)", COL_LGREEN);

            ps("\n", COL_DEFAULT);
        }

        ps("\n", COL_DEFAULT);
        draw_bar(22, "  [K]Kernel [I]Info [R]Reboot [H]Halt [<][>]Page [9][0]+digit  ", COL_DIM);

        while (1) {
            uint8_t sc = kbd_wait_sc();
            int handled = 0;

            if (sc >= 0x02 && sc <= 0x09) {
                int idx = sc - 0x02;
                int n = start_stage + idx;
                if (n <= MAX_STAGE) { run_stage(n); handled = 1; }
            }

            if (sc == 0x0A || sc == 0x0B) {
                int first = (sc == 0x0A) ? 9 : 0;
                uint8_t sc2 = kbd_wait_sc();
                int second = sc_to_digit(sc2);
                if (second >= 0) {
                    int n = first * 10 + second;
                    if (n >= MIN_STAGE && n <= MAX_STAGE) {
                        run_stage(n);
                        handled = 1;
                    }
                }
            }

            if (sc == 0x25) { ps("boot\n\n", COL_OK); ps("Jumping to kernel...\n", COL_OK); void (*entry)(boot_info_t *) = (void (*)(boot_info_t *))bi->entry_point; __asm__ volatile("mov $0x90000, %%rsp\n" : : : "memory"); __asm__ volatile("xor %%rbp, %%rbp\n" : : : "rbp"); entry(bi); __asm__ volatile("cli\n1: hlt\njmp 1b"); handled = 1; }
            if (sc == 0x17) { show_system_info(bi); handled = 1; }
            if (sc == 0x13) { ps("reboot\n\n", COL_WARN); __asm__ volatile("movw $0x1234, 0x472\noutb %%al, %%dx\n" : : "a"((uint8_t)0xFE), "d"((uint16_t)0x64) : "memory"); __asm__ volatile("1: hlt\njmp 1b"); handled = 1; }
            if (sc == 0x23) { ps("halt\n\n", COL_WARN); __asm__ volatile("cli\n1: hlt\njmp 1b"); handled = 1; }
            if (sc == 0x33) { page++; if (page >= total_pages) page = 0; handled = 1; }
            if (sc == 0x34) { page--; if (page < 0) page = total_pages - 1; handled = 1; }

            if (handled) break;
        }
    }
}
