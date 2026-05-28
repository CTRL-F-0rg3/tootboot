/* boot1.c — TootBoot stage 1
 * Displays boot menu, detects systems, launches kernel
 * VGA text mode 80x25 | Purple bg + green-white text
 */

#include "boot1.h"
#include "multiboot.h"

/* ---------------------------------------------------------------------------
 * VGA
 * ---------------------------------------------------------------------------*/
#define VGA_BASE        ((volatile uint16_t *)0xB8000)
#define VGA_WIDTH       80
#define VGA_HEIGHT      25

/* VGA color nibbles */
#define VGA_BG_PURPLE   0x5   /* magenta/purple */
#define VGA_FG_WHITE    0xF
#define VGA_FG_GREEN    0xA   /* bright green */
#define VGA_FG_LGRAY    0x7

#define ATTR(bg, fg)    (uint8_t)(((bg) << 4) | (fg))

#define COLOR_NORMAL    ATTR(VGA_BG_PURPLE, VGA_FG_WHITE)
#define COLOR_SELECTED  ATTR(VGA_BG_PURPLE, VGA_FG_GREEN)
#define COLOR_BORDER    ATTR(VGA_BG_PURPLE, VGA_FG_GREEN)
#define COLOR_TITLE     ATTR(VGA_BG_PURPLE, VGA_FG_GREEN)
#define COLOR_DIM       ATTR(VGA_BG_PURPLE, VGA_FG_LGRAY)

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long  uint64_t;

/* ---------------------------------------------------------------------------
 * Boot entry
 * ---------------------------------------------------------------------------*/
#define MAX_ENTRIES     16
#define TIMEOUT_SECS    5

typedef enum {
    OS_TANOS,
    OS_LINUX,
    OS_CUSTOM,
    OS_UNKNOWN
} OsType;

typedef struct {
    char    label[48];
    char    path[128];
    uint64_t load_addr;
    OsType  type;
    int     valid;
} BootEntry;

static BootEntry entries[MAX_ENTRIES];
static int       entry_count  = 0;
static int       selected     = 0;
static int       timeout      = TIMEOUT_SECS;

/* ---------------------------------------------------------------------------
 * VGA helpers
 * ---------------------------------------------------------------------------*/
static void vga_putchar(int x, int y, char c, uint8_t attr) {
    VGA_BASE[y * VGA_WIDTH + x] = (uint16_t)((attr << 8) | (uint8_t)c);
}

static void vga_puts(int x, int y, const char *s, uint8_t attr) {
    while (*s)
        vga_putchar(x++, y, *s++, attr);
}

static void vga_fill(char c, uint8_t attr) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BASE[i] = (uint16_t)((attr << 8) | (uint8_t)c);
}

static void vga_hline(int x, int y, int len, char c, uint8_t attr) {
    for (int i = 0; i < len; i++)
        vga_putchar(x + i, y, c, attr);
}

static int vga_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void vga_center(int y, const char *s, uint8_t attr) {
    int x = (VGA_WIDTH - vga_strlen(s)) / 2;
    if (x < 0) x = 0;
    vga_puts(x, y, s, attr);
}

/* Simple int-to-string for timeout counter */
static void vga_putint(int x, int y, int n, uint8_t attr) {
    char buf[12];
    int i = 10;
    buf[11] = '\0';
    if (n == 0) { vga_putchar(x, y, '0', attr); return; }
    while (n > 0 && i >= 0) {
        buf[i--] = '0' + (n % 10);
        n /= 10;
    }
    vga_puts(x, y, buf + i + 1, attr);
}

/* ---------------------------------------------------------------------------
 * Border & layout
 *
 *  Row 0     : top border
 *  Row 1     : "TootBoot" title
 *  Row 2     : subtitle
 *  Row 3     : separator
 *  Row 4..19 : entry list (max 16)
 *  Row 20    : separator
 *  Row 21    : key hints
 *  Row 22    : timeout bar
 *  Row 23    : separator
 *  Row 24    : bottom border
 * ---------------------------------------------------------------------------*/
#define BORDER_X1   2
#define BORDER_X2   77
#define ENTRY_X     5
#define ENTRY_Y     4
#define ENTRY_H     16

/* Unicode-ish box chars in CP437 */
#define CHAR_TL     '\xDA'   /* ┌ */
#define CHAR_TR     '\xBF'   /* ┐ */
#define CHAR_BL     '\xC0'   /* └ */
#define CHAR_BR     '\xD9'   /* ┘ */
#define CHAR_H      '\xC4'   /* ─ */
#define CHAR_V      '\xB3'   /* │ */
#define CHAR_LT     '\xC3'   /* ├ */
#define CHAR_RT     '\xB4'   /* ┤ */
#define CHAR_ARROW  '\x10'   /* ► */

static void draw_border(void) {
    /* top */
    vga_putchar(BORDER_X1,     0, CHAR_TL, COLOR_BORDER);
    vga_hline  (BORDER_X1 + 1, 0, BORDER_X2 - BORDER_X1 - 1, CHAR_H, COLOR_BORDER);
    vga_putchar(BORDER_X2,     0, CHAR_TR, COLOR_BORDER);

    /* sides */
    for (int r = 1; r <= 23; r++) {
        vga_putchar(BORDER_X1, r, CHAR_V, COLOR_BORDER);
        vga_putchar(BORDER_X2, r, CHAR_V, COLOR_BORDER);
    }

    /* separator after title */
    vga_putchar(BORDER_X1,     3, CHAR_LT, COLOR_BORDER);
    vga_hline  (BORDER_X1 + 1, 3, BORDER_X2 - BORDER_X1 - 1, CHAR_H, COLOR_BORDER);
    vga_putchar(BORDER_X2,     3, CHAR_RT, COLOR_BORDER);

    /* separator before hints */
    vga_putchar(BORDER_X1,     20, CHAR_LT, COLOR_BORDER);
    vga_hline  (BORDER_X1 + 1, 20, BORDER_X2 - BORDER_X1 - 1, CHAR_H, COLOR_BORDER);
    vga_putchar(BORDER_X2,     20, CHAR_RT, COLOR_BORDER);

    /* bottom */
    vga_putchar(BORDER_X1,     24, CHAR_BL, COLOR_BORDER);
    vga_hline  (BORDER_X1 + 1, 24, BORDER_X2 - BORDER_X1 - 1, CHAR_H, COLOR_BORDER);
    vga_putchar(BORDER_X2,     24, CHAR_BR, COLOR_BORDER);
}

static void draw_title(void) {
    vga_center(1, "TootBoot  v0.1.0", COLOR_TITLE);
    vga_center(2, "Use arrow keys to select  |  ENTER to boot", COLOR_DIM);
}

static void draw_hints(void) {
    vga_puts(ENTRY_X, 21, "[UP/DOWN] Navigate   [ENTER] Boot   [E] Edit   [C] Command line", COLOR_DIM);
}

static void draw_timeout(void) {
    /* clear line */
    vga_hline(BORDER_X1 + 1, 22, BORDER_X2 - BORDER_X1 - 1, ' ', COLOR_DIM);

    if (timeout > 0) {
        vga_puts(ENTRY_X, 22, "Auto-boot in ", COLOR_DIM);
        vga_putint(ENTRY_X + 13, 22, timeout, COLOR_SELECTED);
        vga_puts(ENTRY_X + 14, 22, "s  (any key to cancel)", COLOR_DIM);
    } else {
        vga_puts(ENTRY_X, 22, "Timeout cancelled.", COLOR_DIM);
    }
}

static void draw_entries(void) {
    for (int i = 0; i < ENTRY_H; i++) {
        int row = ENTRY_Y + i;
        /* clear row inside border */
        vga_hline(BORDER_X1 + 1, row, BORDER_X2 - BORDER_X1 - 1, ' ', COLOR_NORMAL);

        if (i >= entry_count) continue;

        uint8_t attr = (i == selected) ? COLOR_SELECTED : COLOR_NORMAL;

        if (i == selected)
            vga_putchar(ENTRY_X - 2, row, CHAR_ARROW, COLOR_SELECTED);
        else
            vga_putchar(ENTRY_X - 2, row, ' ', COLOR_NORMAL);

        /* OS type tag */
        const char *tag = "[ ??? ]";
        switch (entries[i].type) {
            case OS_TANOS:  tag = "[ TanOS ]"; break;
            case OS_LINUX:  tag = "[ Linux ]"; break;
            case OS_CUSTOM: tag = "[Custom ]"; break;
            default:        break;
        }
        vga_puts(ENTRY_X, row, tag, attr);
        vga_puts(ENTRY_X + 10, row, entries[i].label, attr);
    }
}

static void redraw(void) {
    draw_border();
    draw_title();
    draw_entries();
    draw_hints();
    draw_timeout();
}

/* ---------------------------------------------------------------------------
 * System detection
 * Scans partition table (MBR) for known signatures.
 * Each found OS is added to entries[].
 * ---------------------------------------------------------------------------*/

/* MBR partition entry */
typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_start;
    uint32_t lba_size;
} MbrPartition;

/* Well-known partition type bytes */
#define PART_LINUX_DATA  0x83
#define PART_LINUX_SWAP  0x82
#define PART_FAT32       0x0B
#define PART_FAT32_LBA   0x0C
#define PART_NTFS        0x07
#define PART_TANOS       0x74   /* 't' — custom TanOS type */
#define PART_REDOX       0x72   /* 'r' — custom Redox type  */

static void add_entry(const char *label, uint64_t load_addr, OsType type) {
    if (entry_count >= MAX_ENTRIES) return;
    BootEntry *e = &entries[entry_count++];

    /* copy label */
    int i = 0;
    while (label[i] && i < 47) { e->label[i] = label[i]; i++; }
    e->label[i] = '\0';

    e->load_addr = load_addr;
    e->type      = type;
    e->valid     = 1;
}

static void detect_systems(void) {
    /* MBR lives at physical 0x7C00, partition table at offset 446 */
    uint8_t *mbr = (uint8_t *)0x7C00;

    /* Verify MBR signature */
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return;

    MbrPartition *parts = (MbrPartition *)(mbr + 446);

    for (int i = 0; i < 4; i++) {
        if (parts[i].lba_size == 0) continue;

        switch (parts[i].type) {
            case PART_TANOS:
                add_entry("TanOS", (uint64_t)parts[i].lba_start * 512, OS_TANOS);
                break;
            case PART_LINUX_DATA:
                add_entry("Linux", (uint64_t)parts[i].lba_start * 512, OS_LINUX);
                break;
            case PART_REDOX:
                add_entry("Redox OS", (uint64_t)parts[i].lba_start * 512, OS_CUSTOM);
                break;
            default:
                break;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Config file entries (manual additions)
 * Format parsed from a flat config loaded by Boot0 into memory at 0x8000:
 *   ENTRY TanOS /boot/tanos.bin
 *   ENTRY Linux /boot/vmlinuz
 * ---------------------------------------------------------------------------*/
#define CONFIG_ADDRESS  ((char *)0x8000)
#define CONFIG_MAX      4096

static int str_startswith(const char *s, const char *prefix) {
    while (*prefix)
        if (*s++ != *prefix++) return 0;
    return 1;
}

static void skip_spaces(const char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

static int read_token(const char **p, char *out, int max) {
    int n = 0;
    while (**p && **p != ' ' && **p != '\t' && **p != '\n' && n < max - 1)
        out[n++] = *(*p)++;
    out[n] = '\0';
    return n;
}

static void parse_config(void) {
    const char *p = CONFIG_ADDRESS;
    const char *end = p + CONFIG_MAX;

    while (p < end && *p) {
        /* skip blank lines and comments */
        if (*p == '#' || *p == '\n' || *p == '\r') {
            while (*p && *p != '\n') p++;
            p++;
            continue;
        }

        if (str_startswith(p, "ENTRY")) {
            p += 5;
            skip_spaces(&p);

            char label[48], path[128];
            read_token(&p, label, 48);
            skip_spaces(&p);
            read_token(&p, path, 128);

            /* Determine type from label */
            OsType t = OS_CUSTOM;
            if (label[0]=='T' && label[1]=='a') t = OS_TANOS;
            else if (label[0]=='L' && label[1]=='i') t = OS_LINUX;

            if (entry_count < MAX_ENTRIES) {
                add_entry(label, 0, t);
                /* store path too */
                int i = 0;
                while (path[i] && i < 127) {
                    entries[entry_count - 1].path[i] = path[i];
                    i++;
                }
                entries[entry_count - 1].path[i] = '\0';
            }
        }

        while (p < end && *p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

/* ---------------------------------------------------------------------------
 * Keyboard (PS/2 via port 0x60)
 * ---------------------------------------------------------------------------*/
#define KEY_UP    0x48
#define KEY_DOWN  0x50
#define KEY_ENTER 0x1C
#define KEY_E     0x12
#define KEY_C     0x2E

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

/* Returns scancode or 0 if no key ready */
static uint8_t kb_poll(void) {
    if (inb(0x64) & 0x01)
        return inb(0x60);
    return 0;
}

/* ---------------------------------------------------------------------------
 * PIT (8253) based timing
 * Channel 0 runs at 18.2 Hz by default (IRQ0, not connected to us).
 * We reprogram channel 2 to count down from a known value and poll it.
 * 1193182 Hz / 1193 ≈ 1000 ticks/sec (1ms per tick)
 * ---------------------------------------------------------------------------*/
#define PIT_CH2_DATA  0x42
#define PIT_CMD       0x43
#define PIT_KBD_CTRL  0x61

static void pit_init_1ms(void) {
    /* Channel 2, mode 0 (one-shot), binary, reload = 1193 (~1ms) */
    outb(PIT_CMD, 0xB0);            /* channel 2, lobyte/hibyte, mode 0 */
    outb(PIT_CH2_DATA, 0xA9);       /* reload low:  1193 & 0xFF = 0xA9 */
    outb(PIT_CH2_DATA, 0x04);       /* reload high: 1193 >> 8   = 0x04 */
}

/* Wait approximately ms milliseconds using PIT channel 2 polling */
static void pit_wait_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        /* Enable gate for channel 2 */
        uint8_t ctrl = inb(PIT_KBD_CTRL);
        outb(PIT_KBD_CTRL, (ctrl & 0xFC) | 0x01);

        /* Reload counter */
        outb(PIT_CMD, 0xB0);
        outb(PIT_CH2_DATA, 0xA9);
        outb(PIT_CH2_DATA, 0x04);

        /* Poll OUT pin (bit 5 of port 0x61) until it goes high */
        while (!(inb(PIT_KBD_CTRL) & 0x20))
            __asm__ volatile ("pause");
    }
}

/* PIC: remap IRQs so spurious IRQ0 doesn't cause exceptions in 64-bit */
static void pic_remap(void) {
    /* ICW1 */
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    /* ICW2: remap IRQ0-7 to 0x20, IRQ8-15 to 0x28 */
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    /* ICW3 */
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    /* ICW4 */
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    /* Mask all interrupts */
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

/* ---------------------------------------------------------------------------
 * Boot dispatch
 * ---------------------------------------------------------------------------*/
static void __attribute__((noreturn)) boot_entry(BootEntry *e) {
    /* TODO: load kernel from disk to e->load_addr, pass multiboot2 info */

    /* For now: jump to load address directly */
    void (*kernel)(void) = (void (*)(void))(uint64_t)e->load_addr;
    kernel();

    /* Should never reach here */
    __asm__ volatile ("cli; hlt");
    __builtin_unreachable();
}

/* ---------------------------------------------------------------------------
 * Entry point — called from boot0.asm
 * rdi = TootBoot ABI header ptr
 * rsi = TOOTBOOT_MAGIC
 * rdx = PML4 address
 * ---------------------------------------------------------------------------*/
void __attribute__((section(".boot1_entry"))) boot1_entry_point(void) {
    /* Clear screen with purple background */
    vga_fill(' ', COLOR_NORMAL);

    /* Discover systems */
    detect_systems();
    parse_config();

    /* Fallback: always show TanOS if nothing detected */
    if (entry_count == 0)
        add_entry("TanOS (default)", 0, OS_TANOS);

    redraw();

    pic_remap();
    pit_init_1ms();

    /* Main loop — 1ms ticks, count to 1000 for 1 second */
    uint32_t tick = 0;

    while (1) {
        /* Poll keyboard */
        uint8_t key = kb_poll();
        if (key & 0x80) goto next_tick;  /* ignore key-release scancodes */

        if (key) {
            timeout = -1;

            switch (key) {
                case KEY_UP:
                    if (selected > 0) { selected--; redraw(); }
                    break;
                case KEY_DOWN:
                    if (selected < entry_count - 1) { selected++; redraw(); }
                    break;
                case KEY_ENTER:
                    boot_entry(&entries[selected]);
                    break;
                default:
                    break;
            }
        }

next_tick:
        if (timeout < 0) {
            pit_wait_ms(1);
            continue;
        }

        /* 1ms tick — count to 1000 for 1 second */
        pit_wait_ms(1);
        tick++;

        if (tick >= 1000) {
            tick = 0;
            if (timeout > 0) {
                timeout--;
                draw_timeout();
            } else {
                boot_entry(&entries[0]);
            }
        }
    }
}