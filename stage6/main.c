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
#define COL_PROMPT  0x0B

#define KBD_DATA    0x60
#define KBD_STAT    0x64

#define BOOT_INFO_ADDR 0x7000

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

static void cls(void) {
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
        VGA_BUF[i] = (uint16_t)COL_DEFAULT << 8 | ' ';
    crow = 0; ccol = 0;
}

static void scroll(void) {
    for (int r = 1; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            VGA_BUF[(r-1)*VGA_COLS+c] = VGA_BUF[r*VGA_COLS+c];
    for (int c = 0; c < VGA_COLS; c++)
        VGA_BUF[(VGA_ROWS-1)*VGA_COLS+c] = (uint16_t)COL_DEFAULT << 8 | ' ';
    if (crow > 0) crow--;
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

static char kbd_wait(void) {
    while (1) {
        while (!kbd_hit());
        uint8_t sc = kbd_get();
        if (sc & 0x80) continue;
        static const char map[] = {
            0,0,'1','2','3','4','5','6','7','8','9','0','-','=',0,
            0,'q','w','e','r','t','y','u','i','o','p','[',']',0,
            0,'a','s','d','f','g','h','j','k','l',';','\'','`',
            0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
        };
        if (sc < sizeof(map) && map[sc]) return map[sc];
        if (sc == 0x1C) return '\n';
        if (sc == 0x0E) return '\b';
    }
}

static void readline(char *buf, int max) {
    int i = 0;
    while (1) {
        char c = kbd_wait();
        if (c == '\n') {
            pc('\n', COL_DEFAULT);
            buf[i] = 0;
            return;
        }
        if (c == '\b' && i > 0) {
            i--;
            pc('\b', COL_DEFAULT);
            pc(' ', COL_DEFAULT);
            pc('\b', COL_DEFAULT);
            continue;
        }
        if (i < max - 1 && c >= ' ') {
            buf[i++] = c;
            pc(c, COL_DEFAULT);
        }
    }
}

static int seq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

void stage6_entry(void) {
    cls();
    ps("========================================\n", COL_HDR);
    ps("   Chicago-95 Recovery Shell v1.0\n", COL_HI);
    ps("========================================\n\n", COL_HDR);

    boot_info_t *bi = (boot_info_t *)BOOT_INFO_ADDR;

    ps("  System: ", COL_LABEL);
    ps(bi->bootloader_id, COL_DEFAULT);
    ps("\n", COL_DEFAULT);
    ps("  Type 'help' for commands.\n\n", COL_DIM);

    while (1) {
        if (crow >= VGA_ROWS - 2) scroll();

        ps("recovery> ", COL_PROMPT);

        char cmd[64];
        readline(cmd, 64);

        if (seq(cmd, "help") == 0) {
            ps("  Available commands:\n", COL_LABEL);
            ps("    boot     - Boot the loaded kernel\n", COL_DEFAULT);
            ps("    info     - Show system information\n", COL_DEFAULT);
            ps("    modules  - List loaded boot modules\n", COL_DEFAULT);
            ps("    cls      - Clear screen\n", COL_DEFAULT);
            ps("    reboot   - Reboot the system\n", COL_DEFAULT);
            ps("    halt     - Halt the system\n", COL_DEFAULT);
            ps("    help     - Show this help\n", COL_DEFAULT);
            continue;
        }

        if (seq(cmd, "cls") == 0) {
            cls();
            ps("========================================\n", COL_HDR);
            ps("   Chicago-95 Recovery Shell v1.0\n", COL_HI);
            ps("========================================\n\n", COL_HDR);
            continue;
        }

        if (seq(cmd, "info") == 0) {
            ps("\n  System Information:\n", COL_LABEL);
            ps("  Kernel entry: 0x", COL_DIM);
            { const char *h = "0123456789ABCDEF"; char b[17];
              for (int j=0;j<16;j++) b[j]=h[(bi->entry_point>>(60-j*4))&0xF];
              b[16]=0; ps(b, COL_DEFAULT); }
            ps("\n", COL_DEFAULT);
            ps("  E820 entries: ", COL_DIM);
            { char b[12]; int i=11; b[i]=0; uint32_t v=bi->e820_count;
              if(v==0) b[--i]='0'; else while(v>0){b[--i]='0'+(v%10);v/=10;}
              ps(&b[i], COL_DEFAULT); }
            ps("\n", COL_DEFAULT);
            ps("  Modules: ", COL_DIM);
            { char b[12]; int i=11; b[i]=0; uint32_t v=bi->module_count;
              if(v==0) b[--i]='0'; else while(v>0){b[--i]='0'+(v%10);v/=10;}
              ps(&b[i], COL_DEFAULT); }
            ps("\n\n", COL_DEFAULT);
            continue;
        }

        if (seq(cmd, "modules") == 0) {
            if (bi->module_count == 0) {
                ps("  No modules loaded.\n", COL_WARN);
            } else {
                ps("  Loaded modules:\n", COL_OK);
                uint64_t *mod_list = (uint64_t *)(unsigned long)bi->module_list_addr;
                for (uint32_t i = 0; i < bi->module_count; i++) {
                    ps("    [", COL_DIM);
                    { char b[12]; int j=11; b[j]=0; uint32_t v=i;
                      if(v==0) b[--j]='0'; else while(v>0){b[--j]='0'+(v%10);v/=10;}
                      ps(&b[j], COL_DIM); }
                    ps("] 0x", COL_DIM);
                    { const char *h = "0123456789ABCDEF"; char b[17];
                      for (int j=0;j<16;j++) b[j]=h[(mod_list[i]>>(60-j*4))&0xF];
                      b[16]=0; ps(b, COL_DEFAULT); }
                    ps("\n", COL_DEFAULT);
                }
            }
            ps("\n", COL_DEFAULT);
            continue;
        }

        if (seq(cmd, "boot") == 0) {
            ps("  Booting kernel at 0x", COL_OK);
            { const char *h = "0123456789ABCDEF"; char b[17];
              for (int j=0;j<16;j++) b[j]=h[(bi->entry_point>>(60-j*4))&0xF];
              b[16]=0; ps(b, COL_OK); }
            ps("...\n\n", COL_OK);

            void (*entry)(boot_info_t *) = (void (*)(boot_info_t *))bi->entry_point;
            __asm__ volatile("mov $0x90000, %%rsp\n" : : : "memory");
            __asm__ volatile("xor %%rbp, %%rbp\n" : : : "rbp");
            entry(bi);

            ps("Kernel returned. Halting.\n", COL_ERR);
            __asm__ volatile("cli\n1: hlt\njmp 1b");
        }

        if (seq(cmd, "reboot") == 0) {
            ps("  Rebooting...\n", COL_WARN);
            __asm__ volatile(
                "movw $0x1234, 0x472\n"
                "outb %%al, %%dx\n"
                : : "a"((uint8_t)0xFE), "d"((uint16_t)0x64)
                : "memory"
            );
            __asm__ volatile("1: hlt\njmp 1b");
        }

        if (seq(cmd, "halt") == 0) {
            ps("  Halted.\n", COL_WARN);
            __asm__ volatile("cli\n1: hlt\njmp 1b");
        }

        if (cmd[0] != 0) {
            ps("  Unknown command: ", COL_ERR);
            ps(cmd, COL_ERR);
            ps("\n", COL_ERR);
        }
    }
}
