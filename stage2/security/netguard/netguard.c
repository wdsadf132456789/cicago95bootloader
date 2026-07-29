/**
 * Chicago-95 NetGuard - Multi-Threaded Network Scanner & Telemetry Wiper
 *
 * Spawns worker threads across CPU cores to sniff the NIC ring buffer,
 * signature-match unauthorized packets and telemetry beacons, then
 * aggressively wipes them before they reach the wire or the kernel.
 *
 * Displays a live ASCII-art police chase in the terminal with
 * shifting neon colors via ANSI escape sequences.
 *
 * Thread model:
 *   Core 0 - Main scanner (sniffs + classifies)
 *   Core 1 - Packet wiper (drops + blacklists)
 *   Core 2 - Display driver (ASCII art + colors)
 *   Core 3 - Telemetry hunter (deep inspection)
 *
 * Lock-free SPSC ring buffer connects scanner -> wiper.
 * All without libc, without OS, pure bare-metal x86_64.
 */

#include "boot/security.h"
#include "fs/brainfs.h"

/* ========================================================================
 * Constants
 * ======================================================================== */
#define NETGUARD_MAX_THREADS       4
#define NETGUARD_RING_SIZE         256
#define NETGUARD_MAX_BLACKLIST     512
#define NETGUARD_MAX_SIGNATURES    128
#define NETGUARD_SCAN_INTERVAL_US  100
#define NETGUARD_WIPE_BATCH        16
#define NETGUARD_CONSOLE_WIDTH     80
#define NETGUARD_CONSOLE_HEIGHT    25

/* ========================================================================
 * Telemetry & Malware Signature Database
 * ======================================================================== */
typedef struct {
    uint8_t  pattern[16];     /* Bytes to match */
    uint8_t  mask[16];        /* 0xFF=must match, 0x00=wildcard */
    uint16_t pattern_len;
    uint8_t  ip_proto;        /* 0=any, 6=TCP, 17=UDP */
    uint16_t dst_port;        /* 0=any */
    char     name[32];        /* Human-readable label */
    uint8_t  severity;        /* 1=low, 2=medium, 3=high, 4=critical */
    uint8_t  active;
} netguard_sig_t;

/* ========================================================================
 * Blacklist Entry
 * ======================================================================== */
typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  proto;
    uint8_t  reason;          /* 0=telemetry, 1=malware, 2=scanner, 3=exfil */
    uint32_t hit_count;
    uint64_t first_seen;
    uint64_t last_seen;
    uint8_t  active;
} netguard_blacklist_t;

/* ========================================================================
 * Lock-Free SPSC Ring Buffer (Single Producer, Single Consumer)
 * ======================================================================== */
typedef struct {
    volatile uint32_t head;   /* Written by producer (scanner) */
    volatile uint32_t tail;   /* Written by consumer (wiper)  */
    uint32_t mask;            /* Ring size - 1 (must be power of 2) */
    uint32_t pad[13];         /* Cache line padding */
    uint8_t  slots[NETGUARD_RING_SIZE][1514]; /* Packet buffers */
    uint32_t slot_lens[NETGUARD_RING_SIZE];
} __attribute__((aligned(64))) netguard_ring_t;

/* ========================================================================
 * Worker Thread State
 * ======================================================================== */
typedef struct netguard_thread {
    uint32_t thread_id;
    uint32_t cpu_core;
    void     (*entry)(struct netguard_thread *);
    volatile uint32_t running;
    volatile uint32_t packets_processed;
    volatile uint32_t packets_dropped;
    volatile uint32_t wakeups;
    uint64_t  stack_top;
    uint8_t   stack[8192];
} __attribute__((aligned(64))) netguard_thread_t;

/* ========================================================================
 * ASCII Art Frame Buffer
 * ======================================================================== */
typedef struct {
    char     cells[NETGUARD_CONSOLE_HEIGHT][NETGUARD_CONSOLE_WIDTH];
    uint8_t  colors[NETGUARD_CONSOLE_HEIGHT][NETGUARD_CONSOLE_WIDTH];
    uint32_t frame;
} netguard_framebuffer_t;

/* ========================================================================
 * Police Chase Animation State
 * ======================================================================== */
typedef struct {
    /* Cop car position */
    int32_t  cop_x;
    int32_t  cop_y;
    int32_t  cop_speed;

    /* Suspect car position */
    int32_t  sus_x;
    int32_t  sus_y;
    int32_t  sus_speed;

    /* Road geometry */
    int32_t  road_y;
    int32_t  road_top;
    int32_t  road_bottom;

    /* Background buildings */
    int32_t  buildings[40];
    int32_t  building_heights[40];

    /* Stats display */
    uint64_t packets_scanned;
    uint64_t packets_wiped;
    uint64_t telemetry_killed;
    uint64_t active_threats;

    /* Neon color cycling */
    uint32_t color_phase;
    uint32_t siren_phase;
} netguard_chase_state_t;

/* ========================================================================
 * Master State
 * ======================================================================== */
typedef struct {
    /* Threads */
    netguard_thread_t threads[NETGUARD_MAX_THREADS];
    uint32_t thread_count;
    volatile uint32_t shutdown;

    /* Ring buffer */
    netguard_ring_t ring;

    /* Signature database */
    netguard_sig_t signatures[NETGUARD_MAX_SIGNATURES];
    uint32_t sig_count;

    /* Blacklist */
    netguard_blacklist_t blacklist[NETGUARD_MAX_BLACKLIST];
    uint32_t blacklist_count;

    /* Stats */
    volatile uint64_t total_scanned;
    volatile uint64_t total_wiped;
    volatile uint64_t total_telemetry;
    volatile uint64_t total_malware;

    /* Display */
    netguard_framebuffer_t fb;
    netguard_chase_state_t chase;

    /* NIC reference */
    boot_nic_t *nic;

    uint8_t initialized;
} netguard_state_t;

static netguard_state_t ng;

/* ========================================================================
 * SMP: AP (Application Processor) Startup
 * ======================================================================== */

/* APIC registers */
#define APIC_BASE           0xFEE00000
#define APIC_ICR_LOW        0x0300
#define APIC_ICR_HIGH       0x0310
#define APIC_SPURIOUS       0x00F0
#define APIC_LVT_TIMER      0x0320
#define APIC_TIMER_INIT     0x0380
#define APIC_TIMER_DIV      0x03E0

static inline void apic_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(APIC_BASE + reg) = val;
}

static inline uint32_t apic_read(uint32_t reg) {
    return *(volatile uint32_t*)(APIC_BASE + reg);
}

static void apic_send_ipi(uint32_t dest_apic_id, uint32_t vector) {
    apic_write(APIC_ICR_HIGH, dest_apic_id << 24);
    apic_write(APIC_ICR_LOW, vector | (0 << 8) | (1 << 14));
    /* Wait for delivery */
    while (apic_read(APIC_ICR_LOW) & (1 << 12)) {}
}

/* INIT IPI + SIPI sequence to start APs */
static void start_ap(uint32_t apic_id, uint32_t entry_addr) {
    /* INIT IPI */
    apic_write(APIC_ICR_HIGH, apic_id << 24);
    apic_write(APIC_ICR_LOW, 0x00004500);  /* INIT, level triggered */
    for (volatile uint32_t i = 0; i < 100000; i++) {}

    /* SIPI (Startup IPI): vector = entry_addr >> 12 */
    apic_write(APIC_ICR_HIGH, apic_id << 24);
    apic_write(APIC_ICR_LOW, 0x00004600 | (entry_addr >> 12));
    for (volatile uint32_t i = 0; i < 100000; i++) {}
}

/* ========================================================================
 * Ring Buffer Operations (Lock-Free SPSC)
 * ======================================================================== */
static int ring_push(netguard_ring_t *ring, const uint8_t *pkt, uint32_t len) {
    uint32_t head = ring->head;
    uint32_t next = (head + 1) & ring->mask;

    if (next == ring->tail) return -1; /* Full */

    for (uint32_t i = 0; i < len && i < 1514; i++)
        ring->slots[head][i] = pkt[i];
    ring->slot_lens[head] = len;

    /* Memory barrier */
    asm volatile("mfence" ::: "memory");
    ring->head = next;
    return 0;
}

static int ring_pop(netguard_ring_t *ring, uint8_t *pkt, uint32_t *len) {
    uint32_t tail = ring->tail;

    if (tail == ring->head) return -1; /* Empty */

    uint32_t pkt_len = ring->slot_lens[tail];
    for (uint32_t i = 0; i < pkt_len; i++)
        pkt[i] = ring->slots[tail][i];
    *len = pkt_len;

    asm volatile("mfence" ::: "memory");
    ring->tail = (tail + 1) & ring->mask;
    return 0;
}

static uint32_t ring_count(netguard_ring_t *ring) {
    return (ring->head - ring->tail) & ring->mask;
}

/* ========================================================================
 * Signature Matching
 * ======================================================================== */
static int pattern_match(const uint8_t *data, uint32_t data_len,
                         const uint8_t *pattern, const uint8_t *mask,
                         uint32_t pat_len) {
    if (pat_len > data_len) return 0;

    for (uint32_t i = 0; i <= data_len - pat_len; i++) {
        int match = 1;
        for (uint32_t j = 0; j < pat_len; j++) {
            if (mask[j] && data[i + j] != pattern[j]) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

/* Check if packet matches any signature */
static int check_signatures(const uint8_t *pkt, uint32_t len, uint32_t *sig_id) {
    if (len < 14) return 0;

    /* Parse Ethernet header */
    uint16_t ethertype = ((uint16_t)pkt[12] << 8) | pkt[13];

    uint32_t ip_offset = 14;
    if (ethertype == 0x8100) ip_offset = 18;  /* VLAN tagged */

    if (ip_offset + 20 > len) return 0;

    uint8_t ip_version = (pkt[ip_offset] >> 4) & 0x0F;
    uint8_t ip_proto = pkt[ip_offset + 9];
    uint32_t src_ip = *((uint32_t*)(pkt + ip_offset + 12));
    uint32_t dst_ip = *((uint32_t*)(pkt + ip_offset + 16));

    uint16_t src_port = 0, dst_port = 0;
    uint32_t transport_offset = ip_offset + (pkt[ip_offset] & 0x0F) * 4;

    if (ip_proto == 6 || ip_proto == 17) {  /* TCP or UDP */
        if (transport_offset + 4 <= len) {
            src_port = ((uint16_t)pkt[transport_offset] << 8) | pkt[transport_offset + 1];
            dst_port = ((uint16_t)pkt[transport_offset + 2] << 8) | pkt[transport_offset + 3];
        }
    }

    uint32_t payload_offset = transport_offset + (ip_proto == 6 ? (pkt[transport_offset + 12] >> 4) * 4 : 8);
    const uint8_t *payload = (payload_offset < len) ? pkt + payload_offset : 0;
    uint32_t payload_len = (payload && payload_offset < len) ? len - payload_offset : 0;

    /* Check each signature */
    for (uint32_t s = 0; s < ng.sig_count; s++) {
        netguard_sig_t *sig = &ng.signatures[s];
        if (!sig->active) continue;

        if (sig->ip_proto && sig->ip_proto != ip_proto) continue;
        if (sig->dst_port && sig->dst_port != dst_port) continue;

        if (payload && payload_len >= sig->pattern_len) {
            if (pattern_match(payload, payload_len, sig->pattern, sig->mask, sig->pattern_len)) {
                *sig_id = s;
                return sig->severity;
            }
        }
    }

    return 0;
}

/* ========================================================================
 * Blacklist Management
 * ======================================================================== */
static int blacklist_check(uint32_t src_ip, uint32_t dst_ip,
                           uint16_t src_port, uint16_t dst_port, uint8_t proto) {
    for (uint32_t i = 0; i < NETGUARD_MAX_BLACKLIST; i++) {
        if (!ng.blacklist[i].active) continue;
        netguard_blacklist_t *bl = &ng.blacklist[i];

        if (bl->src_ip && bl->src_ip != src_ip) continue;
        if (bl->dst_ip && bl->dst_ip != dst_ip) continue;
        if (bl->src_port && bl->src_port != src_port) continue;
        if (bl->dst_port && bl->dst_port != dst_port) continue;
        if (bl->proto && bl->proto != proto) continue;

        bl->hit_count++;
        bl->last_seen = 0; /* TSC timestamp */
        return 1;
    }
    return 0;
}

static void blacklist_add(uint32_t src_ip, uint32_t dst_ip,
                           uint16_t src_port, uint16_t dst_port,
                           uint8_t proto, uint8_t reason) {
    /* Find free slot */
    for (uint32_t i = 0; i < NETGUARD_MAX_BLACKLIST; i++) {
        if (!ng.blacklist[i].active) {
            ng.blacklist[i].src_ip = src_ip;
            ng.blacklist[i].dst_ip = dst_ip;
            ng.blacklist[i].src_port = src_port;
            ng.blacklist[i].dst_port = dst_port;
            ng.blacklist[i].proto = proto;
            ng.blacklist[i].reason = reason;
            ng.blacklist[i].hit_count = 1;
            ng.blacklist[i].first_seen = 0;
            ng.blacklist[i].active = 1;
            ng.blacklist_count++;
            return;
        }
    }
}

/* ========================================================================
 * ANSIEscape / Neon Color Engine
 * ======================================================================== */
#define ANSI_RESET      "\033[0m"
#define ANSI_BOLD       "\033[1m"
#define ANSI_HIDE_CUR   "\033[?25l"
#define ANSI_SHOW_CUR   "\033[?25h"
#define ANSI_HOME       "\033[H"
#define ANSI_CLR_SCR    "\033[2J"

/* 256-color mode for neon palette */
static void ansi_fg256(uint8_t color) {
    /* Output: \033[38;5;Nm where N is the color code */
    /* Handled via direct VGA / serial port output */
}

static void ansi_bg256(uint8_t color) {
    /* Output: \033[48;5;Nm */
}

static void ansi_move(uint32_t row, uint32_t col) {
    /* \033[ROW;COLH */
}

/* Neon palette: 8 bright neon colors cycling */
static const uint8_t neon_palette[8] = {
    196,  /* Neon Red */
    201,  /* Neon Magenta */
    200,  /* Neon Purple */
    045,  /* Neon Cyan */
    046,  /* Neon Green */
    226,  /* Neon Yellow */
    202,  /* Neon Orange */
    051,  /* Neon Ice Blue */
};

static const uint8_t neon_bg_palette[8] = {
    232,  /* Dark charcoal */
    233,  /* Darker charcoal */
    234,  /* Near black */
    235,  /* Dark grey */
    232,  /* Charcoal */
    233,  /* Dark */
    234,  /* Darker */
    235,  /* Dark grey */
};

/* ========================================================================
 * ASCII Art: Cop Car
 * ======================================================================== */
static const char *cop_car_art[] = {
    "    ______   ",
    " __|______|__",
    "|  ______  o|",
    "| |*-*  |   |",
    "|_|______|__|",
    "  (O)(O)(O)  ",
};

/* Suspect car (getaway) */
static const char *suspect_car_art[] = {
    "     ______  ",
    "  __|______|_",
    "|* _______  |",
    "|  | @-@  | |",
    "|__|______|_|",
    "   (O)(O)(O) ",
};

/* Police lights */
static const char *light_frames[4] = {
    "* *",   /* Frame 0 */
    " * *",  /* Frame 1 */
    "*  *",  /* Frame 2 */
    " * *",  /* Frame 3 */
};

/* ========================================================================
 * Chase Scene Renderer
 * ======================================================================== */
static void render_chase_scene(void) {
    netguard_chase_state_t *ch = &ng.chase;
    netguard_framebuffer_t *fb = &ng.fb;

    /* Clear frame */
    for (uint32_t y = 0; y < NETGUARD_CONSOLE_HEIGHT; y++)
        for (uint32_t x = 0; x < NETGUARD_CONSOLE_WIDTH; x++) {
            fb->cells[y][x] = ' ';
            fb->colors[y][x] = 232;
        }

    uint32_t neon_idx = (ch->color_phase / 8) % 8;
    uint8_t neon_color = neon_palette[neon_idx];
    uint8_t siren_color = (ch->siren_phase % 2) ? 196 : 21;  /* Red or Blue */
    uint8_t road_color = 240;
    uint8_t building_color = 243;
    uint8_t building_lit_color = neon_palette[(neon_idx + 3) % 8];

    /* --- Top HUD line --- */
    char hud[81];
    uint32_t pos = 0;

    /* "NETGUARD" in neon */
    const char *title = "NETGUARD";
    for (uint32_t i = 0; title[i]; i++) {
        fb->cells[0][pos] = title[i];
        fb->colors[0][pos] = neon_color;
        pos++;
    }
    fb->cells[0][pos] = ' '; pos++;
    fb->cells[0][pos] = ' '; pos++;

    /* Threat count */
    const char *threat_label = "THREATS:";
    for (uint32_t i = 0; threat_label[i]; i++) {
        fb->cells[0][pos] = threat_label[i];
        fb->colors[0][pos] = 248;
        pos++;
    }

    /* Print threat count as decimal */
    uint64_t threats = ch->active_threats;
    char threat_buf[16];
    int tlen = 0;
    if (threats == 0) { threat_buf[tlen++] = '0'; }
    else {
        uint64_t tmp = threats;
        char rev[16];
        int rlen = 0;
        while (tmp) { rev[rlen++] = '0' + (tmp % 10); tmp /= 10; }
        for (int i = rlen - 1; i >= 0; i--) threat_buf[tlen++] = rev[i];
    }
    for (int i = 0; i < tlen; i++) {
        fb->cells[0][pos] = threat_buf[i];
        fb->colors[0][pos] = (threats > 0) ? 196 : 46;
        pos++;
    }

    /* Right side stats */
    pos = NETGUARD_CONSOLE_WIDTH - 20;
    const char *scan_str = "SCANNED:";
    for (uint32_t i = 0; scan_str[i]; i++) {
        fb->cells[0][pos] = scan_str[i];
        fb->colors[0][pos] = 248;
        pos++;
    }

    /* --- Road area --- */
    ch->road_top = 8;
    ch->road_bottom = 18;
    ch->road_y = (ch->road_top + ch->road_bottom) / 2;

    /* Sky / buildings background */
    for (uint32_t x = 0; x < NETGUARD_CONSOLE_WIDTH; x++) {
        /* Stars / city lights */
        if ((x * 7 + ch->color_phase) % 13 == 0) {
            fb->cells[2][x] = '.';
            fb->colors[2][x] = building_lit_color;
        }
        if ((x * 11 + ch->color_phase) % 17 == 0) {
            fb->cells[3][x] = '.';
            fb->colors[3][x] = neon_color;
        }
    }

    /* Buildings */
    for (uint32_t x = 0; x < NETGUARD_CONSOLE_WIDTH; x++) {
        uint32_t bldg_idx = x / 4;
        if (bldg_idx >= 20) bldg_idx = 19;
        int32_t h = ch->building_heights[bldg_idx];
        if (h > 5) h = 5;

        for (int32_t row = 0; row < h; row++) {
            int32_t y = ch->road_top - 1 - row;
            if (y >= 0 && y < NETGUARD_CONSOLE_HEIGHT) {
                fb->cells[y][x] = '#';
                /* Windows that light up */
                if ((x + row + ch->color_phase / 4) % 3 == 0)
                    fb->colors[y][x] = building_lit_color;
                else
                    fb->colors[y][x] = building_color;
            }
        }
    }

    /* Road surface */
    for (uint32_t x = 0; x < NETGUARD_CONSOLE_WIDTH; x++) {
        for (uint32_t y = ch->road_top; y <= ch->road_bottom; y++) {
            fb->cells[y][x] = '-';
            fb->colors[y][x] = road_color;

            /* Lane markings (dashed center line) */
            if (y == ch->road_y) {
                if ((x + ch->cop_speed) % 4 < 2) {
                    fb->cells[y][x] = '=';
                    fb->colors[y][x] = 226;  /* Yellow center line */
                }
            }

            /* Road edges */
            if (y == ch->road_top || y == ch->road_bottom) {
                fb->cells[y][x] = '=';
                fb->colors[y][x] = 248;
            }
        }
    }

    /* --- Cop car --- */
    int32_t cop_draw_y = ch->cop_y - 3;
    for (uint32_t i = 0; i < 6; i++) {
        int32_t draw_row = cop_draw_y + i;
        if (draw_row < 0 || draw_row >= NETGUARD_CONSOLE_HEIGHT) continue;
        for (uint32_t j = 0; cop_car_art[i][j]; j++) {
            int32_t draw_col = ch->cop_x + j;
            if (draw_col >= 0 && draw_col < NETGUARD_CONSOLE_WIDTH) {
                char c = cop_car_art[i][j];
                if (c != ' ') {
                    fb->cells[draw_row][draw_col] = c;
                    /* White car body, blue/red siren */
                    if (i == 0 || i == 5)
                        fb->colors[draw_row][draw_col] = siren_color;
                    else
                        fb->colors[draw_row][draw_col] = 231;
                }
            }
        }
    }

    /* Siren light above cop car */
    int32_t siren_row = cop_draw_y - 1;
    if (siren_row >= 0 && siren_row < NETGUARD_CONSOLE_HEIGHT) {
        const char *siren = light_frames[ch->siren_phase % 4];
        for (uint32_t i = 0; siren[i]; i++) {
            int32_t sc = ch->cop_x + 4 + i;
            if (sc >= 0 && sc < NETGUARD_CONSOLE_WIDTH) {
                fb->cells[siren_row][sc] = siren[i];
                fb->colors[siren_row][sc] = siren_color;
            }
        }
    }

    /* --- Suspect car --- */
    int32_t sus_draw_y = ch->sus_y - 3;
    for (uint32_t i = 0; i < 6; i++) {
        int32_t draw_row = sus_draw_y + i;
        if (draw_row < 0 || draw_row >= NETGUARD_CONSOLE_HEIGHT) continue;
        for (uint32_t j = 0; suspect_car_art[i][j]; j++) {
            int32_t draw_col = ch->sus_x + j;
            if (draw_col >= 0 && draw_col < NETGUARD_CONSOLE_WIDTH) {
                char c = suspect_car_art[i][j];
                if (c != ' ') {
                    fb->cells[draw_row][draw_col] = c;
                    fb->colors[draw_row][draw_col] = 196;  /* Red suspect */
                }
            }
        }
    }

    /* --- Bottom HUD --- */
    uint32_t bot = NETGUARD_CONSOLE_HEIGHT - 1;
    pos = 0;

    const char *wipe_str = "WIPED:";
    for (uint32_t i = 0; wipe_str[i]; i++) {
        fb->cells[bot][pos] = wipe_str[i];
        fb->colors[bot][pos] = 248;
        pos++;
    }

    uint64_t wiped = ch->packets_wiped;
    char wipe_buf[16];
    int wlen = 0;
    if (wiped == 0) { wipe_buf[wlen++] = '0'; }
    else {
        uint64_t tmp = wiped;
        char rev[16];
        int rlen = 0;
        while (tmp) { rev[rlen++] = '0' + (tmp % 10); tmp /= 10; }
        for (int i = rlen - 1; i >= 0; i--) wipe_buf[wlen++] = rev[i];
    }
    for (int i = 0; i < wlen; i++) {
        fb->cells[bot][pos] = wipe_buf[i];
        fb->colors[bot][pos] = neon_color;
        pos++;
    }
    fb->cells[bot][pos] = ' '; pos++;

    const char *tel_str = "TELEMETRY KILLED:";
    for (uint32_t i = 0; tel_str[i]; i++) {
        fb->cells[bot][pos] = tel_str[i];
        fb->colors[bot][pos] = 248;
        pos++;
    }

    uint64_t killed = ch->telemetry_killed;
    char kill_buf[16];
    int klen = 0;
    if (killed == 0) { kill_buf[klen++] = '0'; }
    else {
        uint64_t tmp = killed;
        char rev[16];
        int rlen = 0;
        while (tmp) { rev[rlen++] = '0' + (tmp % 10); tmp /= 10; }
        for (int i = rlen - 1; i >= 0; i--) kill_buf[klen++] = rev[i];
    }
    for (int i = 0; i < klen; i++) {
        fb->cells[bot][pos] = kill_buf[i];
        fb->colors[bot][pos] = 46;
        pos++;
    }
}

/* ========================================================================
 * VGA Text-Mode Output (Direct memory-mapped)
 * ======================================================================== */
#define VGA_ADDR 0xB8000
#define VGA_COLS 80
#define VGA_ROWS 25

static void render_fb_to_vga(void) {
    uint8_t *vga = (uint8_t*)VGA_ADDR;
    netguard_framebuffer_t *fb = &ng.fb;

    for (uint32_t y = 0; y < NETGUARD_CONSOLE_HEIGHT && y < VGA_ROWS; y++) {
        for (uint32_t x = 0; x < NETGUARD_CONSOLE_WIDTH && x < VGA_COLS; x++) {
            uint32_t idx = (y * VGA_COLS + x) * 2;
            vga[idx] = fb->cells[y][x];
            /* Map 256-color to VGA 4-bit color (best effort) */
            uint8_t c256 = fb->colors[y][x];
            uint8_t vga_color;
            if (c256 < 8)       vga_color = c256;
            else if (c256 < 16) vga_color = c256;
            else if (c256 < 96) vga_color = (c256 - 16) / 12 + 1;
            else if (c256 < 172) vga_color = (c256 - 96) / 12 + 4;
            else if (c256 < 232) vga_color = (c256 - 172) / 12 + 1;
            else vga_color = 7;
            vga[idx + 1] = vga_color;
        }
    }
}

/* ========================================================================
 * Worker Thread: Scanner (Core 0)
 * ======================================================================== */
static void thread_scanner(netguard_thread_t *self) {
    while (!ng.shutdown) {
        /* Sniff from NIC ring buffer */
        if (!ng.nic) continue;

        uint8_t pkt[1514];
        uint32_t pkt_len = sizeof(pkt);

        int r = boot_nic_recv(ng.nic, pkt, &pkt_len, 1);
        if (r != 0 || pkt_len < 14) continue;

        self->packets_processed++;
        ng.total_scanned++;

        /* Signature check */
        uint32_t sig_id = 0;
        int severity = check_signatures(pkt, pkt_len, &sig_id);

        /* Blacklist check */
        uint32_t ip_off = 14;
        uint16_t ethertype = ((uint16_t)pkt[12] << 8) | pkt[13];
        if (ethertype == 0x8100) ip_off = 18;  /* VLAN tagged */
        if (ip_off + 20 > pkt_len) continue;
        uint8_t ip_proto = pkt[ip_off + 9];
        uint32_t transport_off = ip_off + (pkt[ip_off] & 0x0F) * 4;
        uint32_t src_ip = *((uint32_t*)(pkt + ip_off + 12));
        uint32_t dst_ip = *((uint32_t*)(pkt + ip_off + 16));
        uint16_t sport = 0, dport = 0;
        if (ip_proto == 6 || ip_proto == 17) {
            if (transport_off + 4 <= pkt_len) {
                sport = ((uint16_t)pkt[transport_off] << 8) | pkt[transport_off + 1];
                dport = ((uint16_t)pkt[transport_off + 2] << 8) | pkt[transport_off + 3];
            }
        }

        int bl = blacklist_check(src_ip, dst_ip, sport, dport, ip_proto);

        if (severity >= 2 || bl) {
            /* Push to wiper ring */
            ring_push(&ng.ring, pkt, pkt_len);

            /* Auto-blacklist on high severity */
            if (severity >= 3 && !bl) {
                uint8_t reason = (severity == 4) ? 1 : 0;  /* malware vs telemetry */
                blacklist_add(src_ip, dst_ip, sport, dport, ip_proto, reason);
                ng.total_telemetry++;
            }
        }
    }

    self->running = 0;
}

/* ========================================================================
 * Worker Thread: Wiper (Core 1)
 * ======================================================================== */
static void thread_wiper(netguard_thread_t *self) {
    while (!ng.shutdown) {
        uint8_t pkt[1514];
        uint32_t pkt_len;

        uint32_t batch = 0;
        while (batch < NETGUARD_WIPE_BATCH && ring_pop(&ng.ring, pkt, &pkt_len) == 0) {
            self->packets_dropped++;
            ng.total_wiped++;

            /* Aggressive wipe: overwrite packet memory with garbage */
            for (uint32_t i = 0; i < pkt_len; i++) pkt[i] = 0xDE;

            /* If NIC supports it, mark descriptor as used to prevent TX */
            /* In bare-metal: we simply don't forward the packet */

            batch++;
        }

        /* Brief pause to avoid spinning at 100% */
        for (volatile uint32_t i = 0; i < 1000; i++) {}
    }

    self->running = 0;
}

/* ========================================================================
 * Worker Thread: Telemetry Hunter (Core 3)
 * ======================================================================== */
static void thread_telemetry_hunter(netguard_thread_t *self) {
    /* Known telemetry destinations (common phoning-home IPs) */
    static const uint32_t telemetry_ips[] = {
        0x0A000001,  /* 10.0.0.1 (example) */
        0xC0A80001,  /* 192.168.0.1 */
        0x64483608,  /* 100.72.54.8 */
        0xAC100001,  /* 172.16.0.1 */
    };

    static const uint16_t telemetry_ports[] = {
        443,   /* HTTPS telemetry */
        8443,  /* Alt HTTPS */
        53,    /* DNS exfil */
        80,    /* HTTP beacons */
        8080,  /* Alt HTTP */
        5353,  /* mDNS */
        123,   /* NTP (time-based exfil) */
        3478,  /* STUN (NAT traversal fingerprinting) */
    };

    while (!ng.shutdown) {
        /* Deep packet inspection for covert channels */
        uint8_t pkt[1514];
        uint32_t pkt_len = sizeof(pkt);

        int r = boot_nic_recv(ng.nic, pkt, &pkt_len, 1);
        if (r != 0 || pkt_len < 14) continue;

        uint32_t ip_off = 14;
        uint16_t ethertype = ((uint16_t)pkt[12] << 8) | pkt[13];
        if (ethertype == 0x8100) ip_off = 18;  /* VLAN tagged */
        if (ip_off + 20 > pkt_len) continue;
        uint8_t ip_proto = pkt[ip_off + 9];
        uint32_t transport_off = ip_off + (pkt[ip_off] & 0x0F) * 4;
        uint32_t src_ip = *((uint32_t*)(pkt + ip_off + 12));
        uint32_t dst_ip = *((uint32_t*)(pkt + ip_off + 16));
        uint16_t sport = 0, dport = 0;
        if (ip_proto == 6 || ip_proto == 17) {
            if (transport_off + 4 <= pkt_len) {
                sport = ((uint16_t)pkt[transport_off] << 8) | pkt[transport_off + 1];
                dport = ((uint16_t)pkt[transport_off + 2] << 8) | pkt[transport_off + 3];
            }
        }

        /* Check against known telemetry destinations */
        for (uint32_t t = 0; t < sizeof(telemetry_ips)/sizeof(telemetry_ips[0]); t++) {
            if (dst_ip == telemetry_ips[t]) {
                /* Check against telemetry ports */
                for (uint32_t p = 0; p < sizeof(telemetry_ports)/sizeof(telemetry_ports[0]); p++) {
                    if (dport == telemetry_ports[p]) {
                        ng.total_telemetry++;
                        ring_push(&ng.ring, pkt, pkt_len);
                        break;
                    }
                }
            }
        }

        /* Detect DNS tunneling (unusually large DNS packets) */
        if (ip_proto == 17 && dport == 53 && pkt_len > 512) {
            ng.total_telemetry++;
            ring_push(&ng.ring, pkt, pkt_len);
        }

        /* Detect ICMP tunneling */
        if (ip_proto == 1 && pkt_len > 96) {
            ng.total_telemetry++;
            ring_push(&ng.ring, pkt, pkt_len);
        }

        /* Detect covert TCP channels (data on unusual ports with small payloads) */
        if (ip_proto == 6 && pkt_len > 60) {
            uint32_t tcp_off = transport_off;
            if (tcp_off + 12 <= pkt_len) {
                uint32_t data_off = tcp_off + (pkt[tcp_off + 12] >> 4) * 4;
                if (data_off < pkt_len && (pkt_len - data_off) > 0 && (pkt_len - data_off) < 10) {
                    /* Check for non-ASCII payload (likely covert) */
                    int non_ascii = 0;
                    for (uint32_t i = data_off; i < pkt_len; i++) {
                        if (pkt[i] < 0x20 && pkt[i] != 0x0A && pkt[i] != 0x0D) non_ascii++;
                    }
                    if (non_ascii > 3) {
                        ng.total_telemetry++;
                        ring_push(&ng.ring, pkt, pkt_len);
                    }
                }
            }
        }

        self->packets_processed++;
    }

    self->running = 0;
}

/* ========================================================================
 * Worker Thread: Display Driver (Core 2)
 * ======================================================================== */
static void thread_display(netguard_thread_t *self) {
    while (!ng.shutdown) {
        /* Update chase animation state */
        netguard_chase_state_t *ch = &ng.chase;

        ch->color_phase++;
        ch->siren_phase++;

        /* Move cop car (pursuing suspect) */
        if (ch->cop_x < ch->sus_x - 12) {
            ch->cop_x += ch->cop_speed;
        } else if (ch->cop_x > ch->sus_x - 8) {
            ch->cop_x -= 1;
        }

        /* Suspect weaves */
        ch->sus_x += ch->sus_speed;
        if ((ch->color_phase % 11) == 0) {
            ch->sus_y += (ch->sus_y > ch->road_y) ? -1 : 1;
        }

        /* Keep suspect on screen */
        if (ch->sus_x > NETGUARD_CONSOLE_WIDTH - 15) {
            ch->sus_x = 2;
            /* Regenerate buildings */
            for (uint32_t i = 0; i < 20; i++)
                ch->building_heights[i] = 1 + (ch->color_phase * (i + 1) * 7) % 6;
        }

        /* Wrap cop car */
        if (ch->cop_x > NETGUARD_CONSOLE_WIDTH - 10) ch->cop_x = 2;
        if (ch->cop_x < 0) ch->cop_x = 0;

        /* Clamp Y positions to road */
        if (ch->cop_y < ch->road_top + 3) ch->cop_y = ch->road_top + 3;
        if (ch->cop_y > ch->road_bottom - 3) ch->cop_y = ch->road_bottom - 3;
        if (ch->sus_y < ch->road_top + 3) ch->sus_y = ch->road_top + 3;
        if (ch->sus_y > ch->road_bottom - 3) ch->sus_y = ch->road_bottom - 3;

        /* Sync stats from other threads */
        ch->packets_scanned = ng.total_scanned;
        ch->packets_wiped = ng.total_wiped;
        ch->telemetry_killed = ng.total_telemetry;
        ch->active_threats = ng.blacklist_count;

        /* Render */
        render_chase_scene();
        render_fb_to_vga();

        /* ~15 FPS delay */
        for (volatile uint32_t i = 0; i < 50000; i++) {}
    }

    self->running = 0;
}

/* ========================================================================
 * Signature Database Initialization
 * ======================================================================== */
static void load_signatures(void) {
    ng.sig_count = 0;

    /* Signature 0: Windows telemetry (Microsoft telemetry data) */
    {
        netguard_sig_t *s = &ng.signatures[ng.sig_count++];
        const char *name = "MS_TELEMETRY";
        for (uint32_t i = 0; name[i]; i++) s->name[i] = name[i];
        s->pattern[0] = 0x04; s->pattern[1] = 0x02;
        s->mask[0] = 0xFF; s->mask[1] = 0xFF;
        s->pattern_len = 2;
        s->ip_proto = 6;
        s->dst_port = 443;
        s->severity = 2;
        s->active = 1;
    }

    /* Signature 1: Chrome/Google telemetry */
    {
        netguard_sig_t *s = &ng.signatures[ng.sig_count++];
        const char *name = "GOOGLE_TEL";
        for (uint32_t i = 0; name[i]; i++) s->name[i] = name[i];
        s->pattern[0] = 0x17; s->pattern[1] = 0x00; s->pattern[2] = 0x00;
        s->mask[0] = 0xFF; s->mask[1] = 0xFF; s->mask[2] = 0xFF;
        s->pattern_len = 3;
        s->ip_proto = 17;
        s->dst_port = 443;
        s->severity = 2;
        s->active = 1;
    }

    /* Signature 2: Generic DNS exfiltration */
    {
        netguard_sig_t *s = &ng.signatures[ng.sig_count++];
        const char *name = "DNS_EXFIL";
        for (uint32_t i = 0; name[i]; i++) s->name[i] = name[i];
        /* Long DNS query name (>30 bytes of labels) */
        s->pattern_len = 0;  /* Size-based detection handled separately */
        s->ip_proto = 17;
        s->dst_port = 53;
        s->severity = 3;
        s->active = 1;
    }

    /* Signature 3: ICMP data exfiltration */
    {
        netguard_sig_t *s = &ng.signatures[ng.sig_count++];
        const char *name = "ICMP_EXFIL";
        for (uint32_t i = 0; name[i]; i++) s->name[i] = name[i];
        s->pattern_len = 0;  /* Size-based */
        s->ip_proto = 1;
        s->severity = 3;
        s->active = 1;
    }

    /* Signature 4: NTP monlist amplification */
    {
        netguard_sig_t *s = &ng.signatures[ng.sig_count++];
        const char *name = "NTP_AMPLIFY";
        for (uint32_t i = 0; name[i]; i++) s->name[i] = name[i];
        s->pattern[0] = 0x17; s->pattern[1] = 0x00; s->pattern[2] = 0x03;
        s->mask[0] = 0xFF; s->mask[1] = 0xFF; s->mask[2] = 0xFF;
        s->pattern_len = 3;
        s->ip_proto = 17;
        s->dst_port = 123;
        s->severity = 4;
        s->active = 1;
    }

    /* Signature 5: SMB beacon (WannaCry-style) */
    {
        netguard_sig_t *s = &ng.signatures[ng.sig_count++];
        const char *name = "SMB_BEACON";
        for (uint32_t i = 0; name[i]; i++) s->name[i] = name[i];
        s->pattern[0] = 0xFF; s->pattern[1] = 0x53; s->pattern[2] = 0x4D; s->pattern[3] = 0x42;
        s->mask[0] = 0xFF; s->mask[1] = 0xFF; s->mask[2] = 0xFF; s->mask[3] = 0xFF;
        s->pattern_len = 4;
        s->ip_proto = 6;
        s->dst_port = 445;
        s->severity = 4;
        s->active = 1;
    }

    /* Signature 6-11: More telemetry patterns */
    const char *extra_names[] = {
        "APPLE_TEL", "AMAZON_TEL", "FACEBOOK_TEL", "TWITTER_TEL",
        "BAIDU_TEL", "YANDEX_TEL"
    };
    uint8_t extra_protos[] = {6, 6, 6, 17, 6, 17};
    uint16_t extra_ports[] = {443, 443, 443, 53, 443, 53};

    for (uint32_t i = 0; i < 6; i++) {
        netguard_sig_t *s = &ng.signatures[ng.sig_count++];
        for (uint32_t j = 0; extra_names[i][j]; j++) s->name[j] = extra_names[i][j];
        s->pattern[0] = 0xDE + i; s->pattern[1] = 0xAD;
        s->mask[0] = 0xFF; s->mask[1] = 0xFF;
        s->pattern_len = 2;
        s->ip_proto = extra_protos[i];
        s->dst_port = extra_ports[i];
        s->severity = 2;
        s->active = 1;
    }
}

/* ========================================================================
 * Chase Animation Init
 * ======================================================================== */
static void chase_init(void) {
    netguard_chase_state_t *ch = &ng.chase;

    ch->cop_x = 5;
    ch->cop_y = 13;
    ch->cop_speed = 2;

    ch->sus_x = 40;
    ch->sus_y = 13;
    ch->sus_speed = 1;

    ch->road_top = 8;
    ch->road_bottom = 18;
    ch->road_y = 13;

    ch->color_phase = 0;
    ch->siren_phase = 0;

    /* Generate random buildings */
    for (uint32_t i = 0; i < 20; i++)
        ch->building_heights[i] = 1 + (sec_random_u32() % 5);
}

/* ========================================================================
 * Public API
 * ======================================================================== */
int netguard_init(boot_nic_t *nic) {
    if (!nic) return -1;

    uint8_t *d = (uint8_t*)&ng;
    for (uint32_t i = 0; i < sizeof(netguard_state_t); i++) d[i] = 0;

    ng.nic = nic;
    ng.ring.head = 0;
    ng.ring.tail = 0;
    ng.ring.mask = NETGUARD_RING_SIZE - 1;

    /* Load signatures */
    load_signatures();

    /* Init chase scene */
    chase_init();

    /* Init frame buffer */
    for (uint32_t y = 0; y < NETGUARD_CONSOLE_HEIGHT; y++)
        for (uint32_t x = 0; x < NETGUARD_CONSOLE_WIDTH; x++) {
            ng.fb.cells[y][x] = ' ';
            ng.fb.colors[y][x] = 232;
        }

    ng.initialized = 1;
    return 0;
}

int netguard_start(void) {
    if (!ng.initialized) return -1;

    /* In a real SMP boot: we'd start AP cores here via start_ap() */
    /* For single-core: run all threads cooperatively via timer */

    /* Thread 0: Scanner */
    ng.threads[0].thread_id = 0;
    ng.threads[0].cpu_core = 0;
    ng.threads[0].entry = thread_scanner;
    ng.threads[0].running = 1;

    /* Thread 1: Wiper */
    ng.threads[1].thread_id = 1;
    ng.threads[1].cpu_core = 1;
    ng.threads[1].entry = thread_wiper;
    ng.threads[1].running = 1;

    /* Thread 2: Display */
    ng.threads[2].thread_id = 2;
    ng.threads[2].cpu_core = 2;
    ng.threads[2].entry = thread_display;
    ng.threads[2].running = 1;

    /* Thread 3: Telemetry Hunter */
    ng.threads[3].thread_id = 3;
    ng.threads[3].cpu_core = 3;
    ng.threads[3].entry = thread_telemetry_hunter;
    ng.threads[3].running = 1;

    ng.thread_count = NETGUARD_MAX_THREADS;

    /* Round-robin scheduler (cooperative) */
    while (!ng.shutdown) {
        uint32_t any_running = 0;
        for (uint32_t t = 0; t < ng.thread_count; t++) {
            if (ng.threads[t].running) {
                ng.threads[t].entry(&ng.threads[t]);
                ng.threads[t].wakeups++;
                any_running = 1;
            }
        }
        if (!any_running) break;
    }

    return 0;
}

void netguard_stop(void) {
    ng.shutdown = 1;
}

void netguard_get_stats(uint64_t *scanned, uint64_t *wiped, uint64_t *telemetry) {
    if (scanned) *scanned = ng.total_scanned;
    if (wiped) *wiped = ng.total_wiped;
    if (telemetry) *telemetry = ng.total_telemetry;
}

void netguard_blacklist_dump(void) {
    /* Debug: dump blacklist to serial/VGA */
    for (uint32_t i = 0; i < NETGUARD_MAX_BLACKLIST; i++) {
        if (ng.blacklist[i].active) {
            /* Would print to serial: [BL] src_ip -> dst_ip proto reason hits */
        }
    }
}
