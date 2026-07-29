/**
 * Chicago-95 Boot Menu
 * Interactive security configuration display at boot time
 * Shows firewall status, DNS settings, WiFi status, MAC options
 */

#include "vga/vga.h"
#include "boot/security.h"

/* Menu items */
#define MENU_ITEM_COUNT    8
#define MENU_TIMEOUT_SEC   10

typedef struct {
    const char *label;
    uint8_t     enabled;
    uint8_t     color;
    void        (*toggle)(uint8_t);
    uint8_t     (*get)(void);
} menu_item_t;

/* Forward declarations */
static void toggle_fw(uint8_t val);
static void toggle_dns(uint8_t val);
static void toggle_wifi(uint8_t val);
static void toggle_mac(uint8_t val);
static void toggle_anti_ip(uint8_t val);
static void toggle_netguard(uint8_t val);
static void toggle_brainfs(uint8_t val);
static void toggle_boot(uint8_t val);
static uint8_t get_fw(void);
static uint8_t get_dns(void);
static uint8_t get_wifi(void);
static uint8_t get_mac(void);
static uint8_t get_anti_ip(void);
static uint8_t get_netguard(void);
static uint8_t get_brainfs(void);
static uint8_t get_boot(void);

static menu_item_t menu_items[MENU_ITEM_COUNT] = {
    { "Firewalls (FW-1..4) ", 1, 0x0A, toggle_fw,       get_fw },
    { "DNS Encryption     ", 1, 0x0B, toggle_dns,      get_dns },
    { "WiFi Encryption    ", 1, 0x09, toggle_wifi,     get_wifi },
    { "MAC Encrypters     ", 1, 0x0D, toggle_mac,      get_mac },
    { "Anti-IP Reader     ", 1, 0x0C, toggle_anti_ip,  get_anti_ip },
    { "NetGuard Scanner   ", 1, 0x0E, toggle_netguard, get_netguard },
    { "BrainFS Filesystem ", 1, 0x06, toggle_brainfs,  get_brainfs },
    { "Auto Boot          ", 0, 0x07, toggle_boot,     get_boot },
};

static int selected_item = 0;
static int boot_timeout = MENU_TIMEOUT_SEC;
static uint8_t boot_drive = 0;

/* Toggle/get callbacks */
static void toggle_fw(uint8_t v)       { menu_items[0].enabled = v; }
static void toggle_dns(uint8_t v)      { menu_items[1].enabled = v; }
static void toggle_wifi(uint8_t v)     { menu_items[2].enabled = v; }
static void toggle_mac(uint8_t v)      { menu_items[3].enabled = v; }
static void toggle_anti_ip(uint8_t v)  { menu_items[4].enabled = v; }
static void toggle_netguard(uint8_t v) { menu_items[5].enabled = v; }
static void toggle_brainfs(uint8_t v)  { menu_items[6].enabled = v; }
static void toggle_boot(uint8_t v)     { menu_items[7].enabled = v; }

static uint8_t get_fw(void)       { return menu_items[0].enabled; }
static uint8_t get_dns(void)      { return menu_items[1].enabled; }
static uint8_t get_wifi(void)     { return menu_items[2].enabled; }
static uint8_t get_mac(void)      { return menu_items[3].enabled; }
static uint8_t get_anti_ip(void)  { return menu_items[4].enabled; }
static uint8_t get_netguard(void) { return menu_items[5].enabled; }
static uint8_t get_brainfs(void)  { return menu_items[6].enabled; }
static uint8_t get_boot(void)     { return menu_items[7].enabled; }

/* ---- Draw ---- */

static void draw_title(void) {
    uint8_t white = VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    uint8_t cyan  = VGA_COLOR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);

    /* Draw title box */
    vga_text_draw_box(0, 10, 60, 3, cyan, 0);
    vga_text_puts_at(1, 25, "Chicago-95 BrainFS Boot Menu", white);
}

static void draw_menu(void) {
    uint8_t white   = VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    uint8_t green   = VGA_COLOR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    uint8_t yellow  = VGA_COLOR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    uint8_t red     = VGA_COLOR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    uint8_t selected = VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLUE);

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        uint8_t row = 5 + i;
        uint8_t color = (i == selected_item) ? selected : menu_items[i].color;
        uint8_t status_color = menu_items[i].enabled ? green : red;
        const char *status = menu_items[i].enabled ? "[ON] " : "[OFF]";

        vga_text_puts_at(row, 12, menu_items[i].label, color);
        vga_text_puts_at(row, 34, status, status_color);
    }

    /* Draw navigation hint */
    vga_text_puts_at(14, 12, "Arrow keys: navigate  Enter/Space: toggle", yellow);
    vga_text_puts_at(15, 12, "1-8: quick toggle  S: save & boot  Esc: boot now", yellow);
}

static void draw_status_bar(void) {
    uint8_t cyan = VGA_COLOR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);

    vga_text_draw_hline(17, 0, 80, '-', cyan);
    vga_text_puts_at(18, 2, "Modules:", VGA_COLOR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));

    /* Count enabled modules */
    int count = 0;
    for (int i = 0; i < MENU_ITEM_COUNT; i++)
        if (menu_items[i].enabled) count++;

    vga_text_puts_at(18, 12, "                         ", VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    /* Print module count */
    char buf[8] = {0};
    int tmp = count;
    int pos = 0;
    if (tmp == 0) { buf[pos++] = '0'; }
    else {
        char rev[8] = {0};
        int r = 0;
        while (tmp > 0) { rev[r++] = '0' + (tmp % 10); tmp /= 10; }
        while (r > 0) { buf[pos++] = rev[--r]; }
    }
    buf[pos++] = '/';
    tmp = MENU_ITEM_COUNT;
    {
        char rev[8] = {0};
        int r = 0;
        while (tmp > 0) { rev[r++] = '0' + (tmp % 10); tmp /= 10; }
        while (r > 0) { buf[pos++] = rev[--r]; }
    }
    vga_text_puts_at(18, 21, buf, VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_text_puts_at(18, 26, "active", cyan);
}

static void draw_timeout(int remaining) {
    uint8_t red   = VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_RED);
    uint8_t white = VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    vga_text_puts_at(20, 2, "                                        ", white);

    if (remaining > 0) {
        char msg[40] = "Booting in ";
        int pos = 11;
        char num[4] = {0};
        int tmp = remaining;
        int n = 0;
        if (tmp == 0) { num[n++] = '0'; }
        else {
            char rev[4] = {0};
            int r = 0;
            while (tmp > 0) { rev[r++] = '0' + (tmp % 10); tmp /= 10; }
            while (r > 0) { num[n++] = rev[--r]; }
        }
        int i;
        for (i = 0; i < n && pos < 38; i++) msg[pos++] = num[i];
        msg[pos++] = 's';
        msg[pos] = '\0';
        vga_text_puts_at(20, 2, msg, white);
    } else {
        vga_text_puts_at(20, 2, "Booting now...", red);
    }
}

/* ---- Keyboard input ---- */

static inline uint8_t menu_inb(uint16_t port) {
    uint8_t val;
    asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static int menu_key_available(void) {
    return menu_inb(0x64) & 0x01;
}

static uint8_t menu_get_key(void) {
    while (!menu_key_available());
    return menu_inb(0x60);
}

/* ---- Menu entry point ---- */

int boot_menu_show(uint8_t drive) {
    boot_drive = drive;
    selected_item = 0;

    vga_text_clear();

    draw_title();
    draw_menu();
    draw_status_bar();
    draw_timeout(boot_timeout);

    /* Main loop */
    int timeout_counter = 0;
    while (1) {
        /* Check for key */
        if (menu_key_available()) {
            uint8_t key = menu_get_key();
            timeout_counter = 0; /* Reset timeout on any key */

            switch (key) {
                case 0x48: /* Up arrow */
                    selected_item--;
                    if (selected_item < 0) selected_item = MENU_ITEM_COUNT - 1;
                    break;
                case 0x50: /* Down arrow */
                    selected_item++;
                    if (selected_item >= MENU_ITEM_COUNT) selected_item = 0;
                    break;
                case 0x1C: /* Enter */
                case 0x39: /* Space */
                    menu_items[selected_item].enabled ^= 1;
                    break;
                case 0x01: /* Esc - boot now */
                    return 1;
                /* Number keys 1-8 */
                case 0x02: case 0x03: case 0x04: case 0x05:
                case 0x06: case 0x07: case 0x08: case 0x09:
                    selected_item = key - 0x02;
                    menu_items[selected_item].enabled ^= 1;
                    break;
                case 0x1F: /* S - save & boot */
                    return 1;
            }
            draw_menu();
            draw_status_bar();
        }

        /* Tick delay (~18.2 Hz) */
        asm volatile(
            "push %%ax; mov $0x86, %%ah; int $0x15; pop %%ax"
            : : : "memory"
        );

        timeout_counter++;
        if (timeout_counter >= 18) { /* ~1 second */
            timeout_counter = 0;
            boot_timeout--;
            draw_timeout(boot_timeout);
            if (boot_timeout <= 0) return 1;
        }
    }
}

uint8_t boot_menu_get_config(uint8_t item) {
    if (item < MENU_ITEM_COUNT)
        return menu_items[item].enabled;
    return 0;
}

void boot_menu_set_timeout(int seconds) {
    boot_timeout = seconds;
}
